#pragma once

// CombatState -- the full in-combat game state (design doc §4.2), budget
// <= 8192 bytes (see the static_assert at the bottom of this file for why the
// original 4 KB figure was raised). This is the struct the batch API's advance()
// steps (design doc §7) and the diff harness snapshots (design doc §8). It is
// derived from RunState at combat start and folded back at combat end; the two
// never alias
// (design doc §4.4). The RunState->CombatState derivation is LIVE:
// enter_combat (src/engine/run_advance.cpp) mirrors combat_begin but seeds the
// player sheet (hp/max_hp) and the relic mirror from the run. combat_begin
// (advance.hpp) remains the standalone entry point that takes no RunState.
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
#include "sts/engine/run_state.hpp"  // RelicSlot, kRelicCap (combat relic mirror)

namespace sts::engine {

// --- Capacities (design doc §4.2 table) -------------------------------------

inline constexpr int kPowerCap = 24;       // player and per-monster power slots
inline constexpr int kCardPoolCap = 160;   // one shared card-instance pool
inline constexpr int kHandCap = 10;
inline constexpr int kDrawCap = 128;
inline constexpr int kDiscardCap = 128;
inline constexpr int kExhaustCap = 128;
inline constexpr int kLimboCap = 8;
// kMonsterCap grew 5 -> 7 for multi-monster combat + the encounter framework.
// The largest INITIAL S1 group is 5 (Lots of Slimes), but mid-combat splits retain
// dead records in place (SuicideAction/die() never remove from MonsterGroup.monsters,
// SuicideAction.java:29-34 / AbstractMonster.java:925-951): Slime Boss fully split =
// 1 boss(dead) + 2 Large(dead) + 4 Medium = 7 simultaneous records. Sentry's
// index-parity opener and SpawnMonsterAction smart-positioning read list indices
// among ALL (incl. dead) records, so index identity must be stable -- growing the
// cap (rather than compacting dead records) preserves it. Sized once here so the
// framework covers the worst split case above (scoping report §1.5/§6). Budget: MonsterState
// is 112 B, so 7 slots = 784 B; sizeof(CombatState) 3672 -> 3896, inside the 4 KB
// ceiling (static_assert below). This is a schema change -> SCHEMA_VERSION 3 -> 4.
inline constexpr int kMonsterCap = 7;
inline constexpr int kActionQueueCap = 64;
inline constexpr int kCardQueueCap = 16;
inline constexpr int kMonsterQueueCap = 5;
// preTurnActions capacity (design doc §5.1). `addToTurnStart` prepends only
// start-of-next-turn relic/power actions here, a handful per turn in the
// skeleton's scope, so 16 is generous headroom (16 * 12 B = 192 B, comfortably
// inside CombatState's size budget -- a CEILING of 8192 B against an actual
// sizeof of 3896 B; see the static_assert at the bottom of this file, and note
// the ceiling was raised from 4096 on 2026-07-24). Sized to match the main
// action ring's element type/idiom rather than trimmed to a tight bound.
inline constexpr int kPreTurnActionQueueCap = 16;

// CombatState.flags bit for FrailPower.justApplied on the player. Shame and
// Act-1 monster Frail applications construct FrailPower(..., true), so a newly
// created instance skips its first atEndOfRound decrement. Stacking an existing
// instance preserves the current latch (ApplyPowerAction stacks the existing
// object rather than replacing it). The reserved header flags keep this state
// inside the frozen POD without a schema/layout change.
inline constexpr uint32_t kCombatFlagFrailJustApplied = 1u << 0;

// CombatState.flags bit for the room's cannotLose latch (monster split framework).
// Set by the CANNOT_LOSE opcode and cleared by CAN_LOSE (CannotLoseAction.java:
// 12-15 / CanLoseAction.java:12-15). While set, the pump's all-monsters-dead
// victory transition is suppressed (AbstractMonster.updateDeathAnimation:869
// gates endBattle() on !cannotLose), so a splitting slime's suicide cannot end
// the combat before its children's SPAWN_MONSTER actions resolve. Player death
// is NOT gated (the Java latch only guards the victory branch).
inline constexpr uint32_t kCombatFlagCannotLose = 1u << 1;

// CombatState.flags bit for the room's `mugged` latch (AbstractRoom.mugged).
// Set SYNCHRONOUSLY when a thief's Escape move runs (Looter.takeTurn case
// ESCAPE, Looter.java:128 -- before the queued EscapeAction resolves). The
// reward layer reads it: a mugged room's combat reward keeps no gold and the
// thief's stolen gold is gone with it. Nothing in the combat layer reads it
// back.
inline constexpr uint32_t kCombatFlagMugged = 1u << 2;

// CombatState.flags bit for the PLAYER's escape (Smoke Bomb). SmokeBomb.use
// (SmokeBomb.java:37-48) marks the room `smoked` and sets player.isEscaping +
// a 2.5s escapeTimer; the timer expiring is what ends the battle
// (AbstractPlayer.updateEscapeAnimation, AbstractPlayer.java:2281-2292 --
// endBattle() unconditionally, NOT gated on cannotLose). With no animation
// clock the three Java fields (smoked, isEscaping, the timer) collapse into
// this one bit; the pump's combat-over check reads it every step, which is what
// lets a combat end while monsters are still alive.
inline constexpr uint32_t kCombatFlagPlayerEscaped = 1u << 3;

// CombustPower keeps a private hpLoss counter distinct from its visible damage
// amount. The player-owned counter lives in otherwise-reserved CombatState
// flags: the cards are self-only, and this preserves the frozen PowerSlot/POD
// layout while reproducing CombustPower.stackPower's +1 hpLoss per application.
inline constexpr uint32_t kCombatFlagCombustHpLossShift = 8u;
inline constexpr uint32_t kCombatFlagCombustHpLossMask = 0xFFu << kCombatFlagCombustHpLossShift;

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
//
// PER-MONSTER-TYPE fields (`flags`, `pad0`) carry monster-specific meaning,
// interpreted only by that monster's native module:
//   * `flags` bit kMonsterFlagRitualSkip -- Cultist: the RitualPower's `skipFirst`
//     (RitualPower.java:19,46-55). Set when the Cultist casts Incantation; the
//     RITUAL native at_end_of_round body consumes it to skip the first tick.
//   * `flags` bit kMonsterFlagCurlUpTriggered -- Louse: CurlUpPower.triggered
//     (CurlUpPower.java:25,38-43). Set synchronously by onAttacked before its
//     queued GainBlock/RemoveSpecificPower actions resolve, preventing a queued
//     multi-hit attack from triggering more than once; cleared on power removal.
//   * `pad0` -- MONSTER-TYPE-SCOPED SCRATCH, with NO single global layout. Each
//     monster module owns the WHOLE byte and subdivides it (or does not) as it
//     needs, because no record is ever two monster types at once. Read it only
//     after checking `monster_id`, and do not treat another monster's
//     subdivision as a constraint on yours. Current interpretations:
//       - Louse (Normal/Defensive): the per-instance rolled bite damage
//         (monsterHpRng draw in the ctor, LouseNormal.java:60). 5..8 fits a byte;
//         the louse turn reads it for the BITE DamageAction (monster_louse.cpp).
//       - Gremlin Wizard: `currentCharge`, the 1..4 charge-up counter that gates
//         Ultimate Blast (GremlinWizard.java:43,60-73). A plain whole-byte
//         counter. See monster_gremlin.cpp.
//       - Lagavulin: two 4-bit counters, idleCount in the low nibble and
//         debuffTurnCount in the high nibble (Lagavulin.java:70-71). Both are
//         bounded by their own state machine (idleCount stops mattering at 3,
//         debuffTurnCount is reset to 0 the turn after it reaches 2), so a
//         nibble each is ample. See monster_lagavulin.cpp.
//       - The Guardian: the HP baseline its Mode Shift accumulator measures
//         cumulative damage from -- a WHOLE-byte value, not a packed pair. The
//         Guardian's fixed 240/250 HP sheet (TheGuardian.java:97-106) fits
//         uint8_t's 0..255 with room to spare. See monster_guardian.cpp.
//       - Red Slaver: BIT FLAGS, not a counter -- bit 0 is
//         SlaverRed.usedEntangle (SlaverRed.java:55,90), the once-per-combat
//         latch its getMove reads at :145 and :149. Its sibling latch
//         `firstTurn` (:56,140-141) needs no storage: it is true only during the
//         init() rollMove, which is a distinct entry point here. This is the
//         first pad0 user to subdivide the byte into flags rather than treat it
//         as one value, which is exactly what "no single global layout" above
//         permits. See monster_slaver.hpp/.cpp.
//       - Hexaghost: the Divider per-hit base damage, computed on the ACTIVATE
//         turn as `player.currentHealth / 12 + 1` (Hexaghost.java:151) and
//         spent by the six hits of the NEXT turn (:157-165). Another WHOLE-byte
//         value: it saturates at 255, which needs a player above 3047 HP to
//         reach. See monster_hexaghost.cpp.
//   * `flags` bit kMonsterFlagSplitTriggered -- large slimes: the
//     `splitTriggered` one-shot latch (AcidSlime_L.java:71,150 /
//     SpikeSlime_L.java:66,138). Set synchronously by the damage() interrupt
//     the first time currentHealth falls to <= maxHealth/2 so later hits do not
//     re-queue the split telegraph.
//   * `flags` bits kMonsterFlagLagavulin{Asleep,IsOut,OutTriggered} -- Lagavulin:
//     the three independent booleans of its sleep/wake machine (`asleep` set once
//     by the ctor argument, `isOut` set by changeState("OPEN"), `isOutTriggered`
//     the one-shot latch that stops a second hit re-stunning it;
//     Lagavulin.java:67-69,85,88-91,185,204).
//   * `flags` bits kMonsterFlagGuardian* -- The Guardian: the two mode latches
//     plus the count of Defensive-Mode flips so far, which is what makes the
//     mode-shift threshold GROW (TheGuardian.java:61,245).
//   * `flags` bits kMonsterFlagHexaghostOrb* / *BurnUpgraded -- Hexaghost:
//     `orbActiveCount` (the ONLY thing its six presentation orbs contribute to
//     combat state) and the `burnUpgraded` latch (Hexaghost.java:92-93). See
//     monster_hexaghost.hpp for why the orbs need no records of their own.
// These are mutually exclusive across monster types (a Cultist never bites; a
// Louse never has Ritual), so no field is read under two meanings at once.
struct MonsterState {
    uint16_t monster_id;              // MonsterId; NONE == empty slot
    int16_t hp;
    int16_t max_hp;
    int16_t block;
    uint16_t flags;                   // reserved per-monster bitfield (see above)
    uint8_t move_history[3];          // last 3 move ids, [0] = most recent
    uint8_t intent;                   // telegraphed next move id
    uint8_t power_count;              // live length of powers[]
    uint8_t pad0;                     // monster-type-scoped scratch byte (see above)
    PowerSlot powers[kPowerCap];
};

// Cultist RitualPower.skipFirst (MonsterState.flags bit): while set, the RITUAL
// power's next at_end_of_round Strength tick is skipped (RitualPower.java:48-53).
inline constexpr uint16_t kMonsterFlagRitualSkip = 0x0001u;
inline constexpr uint16_t kMonsterFlagCurlUpTriggered = 0x0002u;
// Large slime splitTriggered latch (AcidSlime_L.java:71,145-151 /
// SpikeSlime_L.java:66,133-139); see the MonsterState comment above.
inline constexpr uint16_t kMonsterFlagSplitTriggered = 0x0004u;
// Lagavulin's three sleep/wake booleans (Lagavulin.java:67-69). `asleep` is the
// ctor argument and never changes; `isOut` means the shell is open (set by
// changeState("OPEN"), :185); `isOutTriggered` is the one-shot latch that makes
// the damage() wake fire at most once (:69,204). They are NOT redundant: between
// the damage interrupt and the queued open, isOutTriggered is set while isOut is
// still false, and a LETHAL hit latches isOutTriggered without ever opening
// (changeState's !isDying guard, :184).
inline constexpr uint16_t kMonsterFlagLagavulinAsleep = 0x0008u;
inline constexpr uint16_t kMonsterFlagLagavulinIsOut = 0x0010u;
inline constexpr uint16_t kMonsterFlagLagavulinOutTriggered = 0x0020u;

// Bits 0x0008 / 0x0010 / 0x0020 belong to Lagavulin's three sleep/wake booleans
// and are not free; this batch was allocated from 0x0040 up rather than
// appending into them. A gap in this bitfield is legal -- nothing indexes it.
//
// The Guardian (TheGuardian.java, see monster_guardian.hpp). Two latches and a
// counter, all read only by monster_guardian.cpp:
//   * OPEN               -- `isOpen` (:76,248,262): Offensive Mode, the only
//                           mode in which damage accumulates toward a shift.
//   * CLOSE_UP_TRIGGERED -- `closeUpTriggered` (:77,263,289): set SYNCHRONOUSLY
//                           the instant the threshold is reached, so the hits
//                           still queued behind a multi-hit attack cannot
//                           trigger a second flip before the queued change of
//                           state resolves and clears `isOpen`.
//   * SHIFT_COUNT        -- how many times Defensive Mode has been entered. The
//                           threshold is dmgThreshold + 10 * this
//                           (dmgThresholdIncrease, :61,245), which is what makes
//                           the flip point GROW each cycle. Three bits (0..7) is
//                           ample: at A20 the thresholds run 40, 50, 60, 70 ...,
//                           so reaching an 8th flip would need 40+50+...+110 ==
//                           600 cumulative damage against a 250 HP sheet. The
//                           increment saturates rather than wrapping.
inline constexpr uint16_t kMonsterFlagGuardianOpen = 0x0040u;
inline constexpr uint16_t kMonsterFlagGuardianCloseUpTriggered = 0x0080u;
inline constexpr uint16_t kMonsterFlagGuardianShiftShift = 8u;
inline constexpr uint16_t kMonsterFlagGuardianShiftMask = 0x0700u;

// Hexaghost (Hexaghost.java, see monster_hexaghost.hpp). Bits 0x0001..0x0700
// are taken by the five monsters above, so this batch was allocated from 0x0800
// up; 0x8000 is left free.
//   * ORB_COUNT     -- `orbActiveCount` (:93), the whole combat-relevant residue
//                      of the six presentation orbs. getMove switches on it
//                      (:224-252) and nothing else reads it. Its range is 0..6:
//                      changeState "Activate" sets 6 (:268), "Activate Orb"
//                      increments and stops the cycle at 6 (:278-280), and
//                      "Deactivate" resets to 0 (:289) -- so three bits hold it
//                      exactly, with 7 unreachable.
//   * BURN_UPGRADED -- `burnUpgraded` (:92,205-207): a one-shot set by the first
//                      Inferno, after which every Burn Sear creates is upgraded
//                      (:183-185). Never cleared.
inline constexpr uint16_t kMonsterFlagHexaghostOrbShift = 11u;
inline constexpr uint16_t kMonsterFlagHexaghostOrbMask = 0x3800u;
inline constexpr uint16_t kMonsterFlagHexaghostBurnUpgraded = 0x4000u;

// ESCAPED -- the first GLOBAL (not monster-type-scoped) flag bit: the monster
// left the fight ALIVE. AbstractMonster.escape (AbstractMonster.java:915-919)
// sets `isEscaping`; the escape animation then latches `escaped`
// (updateEscapeAnimation, :894-906). This engine has no animation clock, so the
// two Java booleans collapse into this one bit, set by the ESCAPE opcode
// (EscapeAction.java:21-28 -> escape()) at resolve time. The Looter is the only
// Act-1 producer (Looter.java:126-133); the gremlins' move 99 is unreachable in
// Act 1 (no escapeNext()/deathReact() caller) and stays unmodelled.
//
// An escaped monster keeps its positive hp -- it is NOT dying -- so every
// liveness read that means "in the fight" must test monster_dead_or_escaped()
// below rather than hp alone. Being global, this bit is read without checking
// monster_id, unlike every type-scoped bit above.
//
// This takes the last free MonsterState.flags bit. That is acceptable because
// the type-scoped bits above are MUTUALLY EXCLUSIVE across monster types (no
// record is two types at once), so a future monster needing scratch bits may
// deliberately overlap another type's allocation -- only genuinely global bits
// like this one must be unique, and escape is the only global creature state
// the Java carries that hp does not already express.
inline constexpr uint16_t kMonsterFlagEscaped = 0x8000u;

// --- Liveness predicates ------------------------------------------------------
// The game's monster liveness is NOT `hp > 0`: it is `isDying || isEscaping`
// (MonsterGroup.areMonstersBasicallyDead, MonsterGroup.java:90-95) or
// isDeadOrEscaped (AbstractCreature.java:780-790) depending on the site. This
// engine models isDying as hp <= 0 (die() and SuicideAction both zero HP) and
// isEscaping/escaped as kMonsterFlagEscaped, so "dead or escaped" is one shared
// predicate. halfDead has no S1 producer (no Awakened One / Darklings), so the
// Java's halfDead terms are constant-false here.
[[nodiscard]] inline bool monster_escaped(const MonsterState& m) noexcept {
    return (m.flags & kMonsterFlagEscaped) != 0u;
}
[[nodiscard]] inline bool monster_dead_or_escaped(const MonsterState& m) noexcept {
    return m.hp <= 0 || monster_escaped(m);
}

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

    // -- monster queue (design doc §5.1: monsters awaiting turn). Cap stays 5
    //    even though kMonsterCap grew to 7: queueMonsters only enqueues
    //    LIVE monsters (MonsterGroup.queueMonsters skips dead/escaped,
    //    MonsterGroup.java:117-122), and the max simultaneously-alive S1 group is
    //    5 (Lots of Slimes). Splits raise the RECORD count past 5 but not the live
    //    count (a splitting parent dies as its children spawn), so 5 is the true
    //    turn-queue bound; the 2 extra monster RECORD slots are for dead-in-place
    //    retention, not extra queued turns. --
    MonsterQueueItem monster_queue[kMonsterQueueCap];
    uint8_t monster_queue_count;
    uint8_t monster_attacks_queued;   // design doc §5.2 step 4 flag (0/1)

    // -- combat relic mirror: an addition beyond design §4.3's literal RunState
    //    list. The player's relics in
    //    acquisition order (== trigger order, trap 8), mirrored from RunState.relics
    //    at combat_begin so in-combat relic hooks (relic_hooks.hpp player_relics)
    //    read a live list rather than an empty view. This field is only the
    //    STORAGE the dispatch reads; the run-level fold-back that populates and
    //    refreshes it across combats lives in run_advance.cpp
    //    (enter_combat). Capacity == kRelicCap (== RunState.relics) so the fold is
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
// Size budget (design doc §4.2, raised 4096 -> 8192 by the project owner on
// 2026-07-24; recorded in stage-b-design.md §11).
//
// Actual sizeof(CombatState) at the time of the change: 3896 B. Under the old
// 4096 ceiling that left ~200 B of headroom, while the most recent capacity
// bump alone (kMonsterCap 5 -> 7) cost 224 B. The remaining relic tiers and the
// unbuilt run-layer content are still to land, so the next routine capacity bump
// would have tripped this assert mid-task, with no authority to move a frozen
// budget.
//
// 8192 == RunState's existing ceiling (run_state.hpp:213), so the two budgets
// are now at parity. Raising the ceiling is deliberately the cheap option: it
// changes no field, no offset, and no serialized layout, so SCHEMA_VERSION is
// NOT bumped and no golden fixture is regenerated. The alternative (trimming
// the POD layout to fit) would have been a schema + fixture change -- exactly
// the stop-the-line class of change this avoids.
//
// This is a CEILING, not a target. sizeof(CombatState) is unchanged at 3896 B.
static_assert(sizeof(CombatState) <= 8192,
              "CombatState exceeds its 8 KB budget (design doc §4.2)");

}  // namespace sts::engine
