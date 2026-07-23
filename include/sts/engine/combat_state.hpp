#pragma once

// CombatState -- the full in-combat game state (design doc §4.2), budget
// <= 4096 bytes. This is the struct the batch API's advance() steps (design doc
// §7) and the diff harness snapshots (design doc §8). It is derived from
// RunState at combat start and folded back at combat end; the two never alias
// (design doc §4.4). A full RunState->CombatState derivation is not yet
// implemented; this struct is the storage shape.
//
// Design doc §4.1 principles enforced here:
//   * trivially copyable, no pointers, no heap -- snapshot is a memcpy;
//   * fixed-capacity arrays with counts -- overflow is a hard assert in the
//     rule code that fills them, not a reallocation;
//   * all cross-references are indices, never pointers (piles hold uint8_t
//     indices into the card-instance pool, monster/card queues hold indices);
//   * value-initialized before use so byte-hashing is padding-stable -- the
//     struct is a plain aggregate (no user-declared constructors) precisely so
//     `CombatState s{};` zero-fills it, including padding (design doc §4.1,
//     verified by state_test's "two value-initialized states hash-equal" case).
//
// SCOPE (design doc §9): the M1 walking skeleton is Ironclad vs. Jaw Worm only.
// Most capacities below (128-deep piles, 5 monster slots, 24 power slots) are
// intentional forward design per the §4.2 table -- the skeleton exercises only
// a small corner of them. Fields whose exact bit-layout the design doc leaves
// open (the `flags` words, `misc`, monster `intent`/`move_history` encodings)
// are minimal documented placeholders; their semantics are defined by the
// action-queue pump, monster AI, and effect layers, not invented here.
//
// The four action queues (design doc §5.1) are `action_queue` (the game's
// `actions` list), `pre_turn_actions` (the game's `preTurnActions` list;
// `addToTurnStart` prepends here, GameActionManager.java:59,145), `card_queue`,
// and `monster_queue`. `pre_turn_actions` uses the same `ActionQueueItem`
// element type as the main queue, and `turn_has_ended` is the bookkeeping flag
// the pump's §5.2-step-6 gate reads.

#include <cstdint>
#include <type_traits>

#include "sts/engine/schema.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/engine/run_state.hpp"  // RelicSlot, kRelicCap (combat relic mirror, B4.3)

namespace sts::engine {

// --- Capacities (design doc §4.2 table) -------------------------------------

inline constexpr int kPowerCap = 24;       // player and per-monster power slots
inline constexpr int kCardPoolCap = 160;   // one shared card-instance pool
inline constexpr int kHandCap = 10;
inline constexpr int kDrawCap = 128;
inline constexpr int kDiscardCap = 128;
inline constexpr int kExhaustCap = 128;
inline constexpr int kLimboCap = 8;
inline constexpr int kMonsterCap = 5;
inline constexpr int kActionQueueCap = 64;
inline constexpr int kCardQueueCap = 16;
inline constexpr int kMonsterQueueCap = 5;
// preTurnActions capacity (design doc §5.1). `addToTurnStart` prepends only
// start-of-next-turn relic/power actions here, a handful per turn in the
// skeleton's scope, so 16 is generous headroom (16 * 12 B = 192 B, well inside
// CombatState's ~784 B of remaining 4 KB budget). Sized to match the main
// action ring's element type/idiom rather than trimmed to a tight bound.
inline constexpr int kPreTurnActionQueueCap = 16;

// kCardPoolCap == 160 fits in a uint8_t index (0..159 <= 255), so every pile
// stores its members as uint8_t indices into card_pool.
using CardPoolIndex = uint8_t;
static_assert(kCardPoolCap <= 256,
              "card-pool indices are uint8_t; pool cannot exceed 256 rows");

// --- CombatPhase ------------------------------------------------------------

// Coarse combat phase (design doc §4.2 header group). The action-queue pump
// drives the fine-grained WAITING_ON_USER / resolving distinction via the
// queues; this enum is the top-level state the batch API reports. Minimal
// placeholder set for the skeleton -- extended as later phases need it.
enum class CombatPhase : uint8_t {
    NONE = 0,          // uninitialized (value-init default)
    WAITING_ON_USER,   // player may act (design doc §5.2 step 7)
    RESOLVING,         // pump is draining queues
    COMBAT_OVER,       // player or all monsters dead
};

// --- ActionQueueItem (design doc §5.1, §4.2) --------------------------------

// One entry of the main action ring (`actions`). Storage only; the queue
// mechanics live in action_queue.hpp. `opcode` indexes the effect-interpreter
// op set (design doc §5.5/§6: DAMAGE/BLOCK/APPLY_POWER/...);
// `src`/`tgt` are actor indices (player is a reserved sentinel, monsters are
// monster-array indices); `amount` is the signed op argument; `flags` is a
// reserved per-action bitfield. Field widths are exactly design doc §4.2's
// `{opcode: u16, src: u8, tgt: u8, amount: i32, flags: u32}` -> 12 bytes, no
// padding.
struct ActionQueueItem {
    uint16_t opcode;
    uint8_t src;
    uint8_t tgt;
    int32_t amount;
    uint32_t flags;
};

static_assert(std::is_trivially_copyable_v<ActionQueueItem>);
static_assert(sizeof(ActionQueueItem) == 12,
              "ActionQueueItem must be 12 bytes (design doc §4.2: "
              "u16+u8+u8+i32+u32)");

// --- CardQueueItem (design doc §5.1, §4.2) ----------------------------------

// One pending card play. Storage only; the index-1 front-insertion rule and the
// null-card end-turn sentinel (design doc §5.1, trap 9) live in the queue
// mechanics (action_queue.hpp). `card_index` references the shared card-instance
// pool; `target` is a monster-array index (or a reserved sentinel for no/auto
// target).
struct CardQueueItem {
    CardPoolIndex card_index;
    uint8_t target;
};

static_assert(std::is_trivially_copyable_v<CardQueueItem>);
static_assert(sizeof(CardQueueItem) == 2);

// --- MonsterQueueItem (design doc §5.1, §4.2) -------------------------------

// One monster awaiting its turn. Storage only; queueMonsters / takeTurn
// ordering lives in the pump (action_queue.hpp). `monster_index` references the
// monster array; `flags` is a reserved per-entry bitfield (e.g. a future
// "already acted" marker).
struct MonsterQueueItem {
    uint8_t monster_index;
    uint8_t flags;
};

static_assert(std::is_trivially_copyable_v<MonsterQueueItem>);
static_assert(sizeof(MonsterQueueItem) == 2);

// --- MonsterState (design doc §4.2 monsters row) ----------------------------

// One monster slot: `{hp, maxHp, block, move history ×3, intent, flags}` plus 24
// power slots (design doc §4.2). hp/max_hp/block are int16 to match the player's
// widths (A20 numbers are small; signed leaves headroom for transient negative
// bookkeeping). `move_history` holds the last three move ids most-recent-first
// -- Jaw Worm's AI needs last-move and last-two-moves (design doc §9); the move
// id encoding is defined by the monster module (monster_jaw_worm.hpp), here it
// is an opaque uint8_t. `intent` is the telegraphed next move id. `flags` is a
// reserved per-monster bitfield. `power_count` is the live length of `powers`
// (parallels the pile counts); empty slots also read PowerId::NONE.
struct MonsterState {
    uint16_t monster_id;              // MonsterId; NONE == empty slot
    int16_t hp;
    int16_t max_hp;
    int16_t block;
    uint16_t flags;                   // reserved per-monster bitfield
    uint8_t move_history[3];          // last 3 move ids, [0] = most recent
    uint8_t intent;                   // telegraphed next move id
    uint8_t power_count;              // live length of powers[]
    uint8_t pad0;                     // explicit; keeps layout deterministic
    PowerSlot powers[kPowerCap];
};

static_assert(std::is_trivially_copyable_v<MonsterState>);
static_assert(sizeof(MonsterState) == 16 + 4 * kPowerCap,
              "MonsterState layout drifted -- update SCHEMA_VERSION");

// --- CombatState ------------------------------------------------------------

struct CombatState {
    // Schema stamp (design doc §8) -- compile-time, no per-instance storage.
    static constexpr uint32_t kSchemaVersion = SCHEMA_VERSION;

    // -- header (design doc §4.2 header group) --
    uint8_t phase;                    // CombatPhase
    uint8_t pad_header;               // explicit padding, value-init zeroed
    uint16_t turn;                    // turn counter (design doc §5.2 step 6)
    uint32_t flags;                   // reserved combat-wide bitfield

    // -- player (design doc §4.2 player group) --
    int16_t player_hp;
    int16_t player_max_hp;
    int16_t player_block;
    int16_t player_energy;            // small; int16 matches the hp/block widths
    uint8_t stance;                   // stance id (0 = None); skeleton is stanceless
    uint8_t cards_played_this_turn;   // per-turn counter (design doc §4.2)
    uint8_t player_power_count;       // live length of player_powers[]
    uint8_t pad_player;               // explicit padding
    PowerSlot player_powers[kPowerCap];

    // -- shared card-instance pool (design doc §4.2: "one pool, piles
    //    reference it"). Piles below store uint8_t indices into this array. --
    CardInstance card_pool[kCardPoolCap];

    // -- piles: index lists into card_pool + counts (design doc §4.2) --
    CardPoolIndex hand[kHandCap];
    CardPoolIndex draw[kDrawCap];
    CardPoolIndex discard[kDiscardCap];
    CardPoolIndex exhaust[kExhaustCap];
    CardPoolIndex limbo[kLimboCap];
    uint8_t hand_count;
    uint8_t draw_count;
    uint8_t discard_count;
    uint8_t exhaust_count;
    uint8_t limbo_count;
    uint8_t monster_count;            // live length of monsters[]
    uint8_t pad_piles[2];             // explicit padding to a 2-byte boundary

    // -- monsters (design doc §4.2) --
    MonsterState monsters[kMonsterCap];

    // -- action queue: fixed ring + bookkeeping (design doc §5.1/§4.2). Storage
    //    only; the queue mechanics live in action_queue.hpp. head/tail/count are
    //    the ring cursors the pump maintains. --
    ActionQueueItem action_queue[kActionQueueCap];
    uint8_t action_head;
    uint8_t action_tail;
    uint8_t action_count;
    uint8_t pad_actionq;              // explicit padding

    // -- pre-turn action queue (design doc §5.1: `preTurnActions`).
    //    `addToTurnStart` prepends here (GameActionManager.java:145); the pump
    //    drains it right after the main action queue (§5.2 step 2). Same ring
    //    shape/cursors as `action_queue`. --
    ActionQueueItem pre_turn_actions[kPreTurnActionQueueCap];
    uint8_t pre_turn_head;
    uint8_t pre_turn_tail;
    uint8_t pre_turn_count;
    // Set by the end-turn sentinel (design doc §5.2 step 3 / §5.4), cleared by
    // the start-of-turn sequence (§5.2 step 6); gates that step-6 branch. The
    // game's `GameActionManager.turnHasEnded`.
    uint8_t turn_has_ended;           // 0/1; fills what would be ring padding

    // -- card queue (design doc §5.1: pending card plays, cap 16) --
    CardQueueItem card_queue[kCardQueueCap];
    uint8_t card_queue_count;
    uint8_t pad_cardq;                // explicit padding

    // -- monster queue (design doc §5.1: monsters awaiting turn, cap 5) --
    MonsterQueueItem monster_queue[kMonsterQueueCap];
    uint8_t monster_queue_count;
    uint8_t monster_attacks_queued;   // design doc §5.2 step 4 flag (0/1)

    // -- combat relic mirror (B4.3, orchestrator-approved addition beyond the
    //    block's literal RunState list; see the B4.3 Log). The player's relics in
    //    acquisition order (== trigger order, trap 8), mirrored from RunState.relics
    //    at combat_begin so in-combat relic hooks (relic_hooks.hpp player_relics)
    //    read the live list instead of the empty view B3.24 left as a seam. The
    //    RUN-LEVEL fold-back (populating/refreshing this across combats) is B4.4's
    //    (its deliverable lists "relic counters"); this is only the storage the
    //    dispatch reads. Capacity == kRelicCap (== RunState.relics) so the fold is
    //    a plain array copy; kRelicCap = 40 covers S1 A20 runs (which can
    //    accumulate ~30+ relics) with margin. Value-init leaves it empty, so the
    //    20 combat fixtures carry a zeroed mirror (dispatch stays a no-op there). --
    RelicSlot relics[kRelicCap];
    uint8_t relic_count;
    uint8_t pad_relics[7];            // explicit padding (keeps the RNG block aligned)

    // -- RNG: the 5 floor-scoped streams (design doc §3.4 / §3.6). Named
    //    exactly as the game's streams so combat_begin() can derive each via
    //    floor_stream(seed, floor) with an obvious 1:1 mapping. RngStream is
    //    8-byte aligned, so CombatState is 8-byte aligned and the compiler
    //    inserts (value-init-zeroed) padding ahead of this block. --
    RngStream monster_hp_rng;         // monster max-HP rolls
    RngStream ai_rng;                 // monster move selection
    RngStream shuffle_rng;            // deck shuffles (feeds the JDK LCG)
    RngStream card_random_rng;        // in-combat card randomness (random targets)
    RngStream misc_rng;               // everything else
};

static_assert(std::is_trivially_copyable_v<CombatState>,
              "CombatState must be trivially copyable (design doc §4.1: "
              "snapshot = memcpy)");
static_assert(sizeof(CombatState) <= 4096,
              "CombatState exceeds its 4 KB budget (design doc §4.2)");

}  // namespace sts::engine
