#pragma once

// PublicView -- the versioned public-information observation record
// (docs/training-plan.md §2.1). A pull API beside legal_actions():
// encode_public_view(rc, out) flattens everything a perfect-memory player could
// know from the revealed action-observation history into one fixed-layout,
// trivially-copyable POD. It deliberately does NOT ride on StepResult (which
// embeds the full-state observation buffer by value on the hot path --
// fattening it would tax every advance() call whether or not the caller wants
// the full view).
//
// SCOPE (task T0.1): the COMBAT section plus the v1 schema skeleton. The
// combat section closes every gap in the full-state observation stub -- the
// OTHER surface, whose whole rule is that nothing training-facing reads it
// (omniscient_observation.hpp):  [omniscient-boundary-ok]
// player powers, monster block, FULL per-monster power lists (no 4-of-24
// truncation), the draw pile as an UNORDERED MULTISET, discard / exhaust /
// limbo contents, and the potion belt.
//
// SCOPE (task T0.2, v2): everything ABOVE combat. Three additions, all
// TAIL-APPENDED so v1 offsets are untouched (an ADDITIVE change -- see the
// audit doc's schema-evolution note and its v2 version-log entry):
//   * the ALWAYS-BLOCK (plan §2.1): hp/max-hp/gold/floor/ascension, the master
//     deck, relics + displayed counters, the full current-act map including
//     the burning-elite node, every pity / membership counter, and the
//     consumed encounter prefix.
//   * the PER-PHASE SCREEN sections: reward / shop / event / Neow / rest /
//     treasure, each a projection of the RunController transient struct that
//     is ON SCREEN in that phase -- and each gated by the phase, because a
//     transient struct may hold data the player has NOT seen while a different
//     screen is up (Dead Adventurer stocks rc.rewards with the rewards the
//     player never searched out BEFORE its combat starts, AbstractEvent
//     bodies roll their scratch at room entry). TreasureChest is the masking
//     trap plan §2.1 names: contents roll at construction, so pre-open only
//     the chest SIZE is carried.
//   * the LEGAL-ACTION MASK as an observation channel (plan §2.1): the
//     RunActionMask bytes are a member of this record, so a legality bit
//     computed from hidden state is caught by the same twin tests as the rest
//     of the view rather than living in a channel nobody hashes.
//   * the KNOWLEDGE PROJECTION (knowledge.hpp, plan §2.2/§3.1): per-draw-slot
//     order-constraint annotations (relative rank + exact from-top position,
//     the flags plan §3.1's card token reads), the Frozen-Eye full-order flag,
//     and the revealed monster construction rolls.
// The reserved fields below are the v1 forward structure S2/S3 fill in; v2
// populates `keys_reserved` and `act_reserved` with their declared meanings
// (both are live public RunState fields today), which is the note's additive
// case 1.
//
// THE INFORMATION CONTRACT (plan §1): public = derivable by a perfect-memory
// player from revealed history, NOT "currently on screen". The encoder hides
// RNG *realizations*, never rules. Concretely, in this section:
//   * the draw pile's CONTENTS are public (the player tracks the multiset);
//     its ORDER is hidden -- so draw[] is canonically sorted, making the
//     encoding order-invariant (the T0.5 hidden-twin byte-equality property).
//     Hand / discard / exhaust / limbo order IS public (each card's arrival
//     was an observed event), so those piles encode in engine order.
//   * Runic Dome intent suppression happens HERE, exactly as it does in the
//     other observation surface (the game's two rendering guards,
//     AbstractMonster.java:258/:749, touch no game state -- the full write-up
//     is at omniscient_observation.hpp's encoder).  [omniscient-boundary-ok]
//     move_history stays visible under the Dome: past moves were observed as
//     they resolved; only the telegraphed NEXT move is hidden.
//   * MonsterState.pad0 is EXCLUDED: it is per-type scratch that can hold an
//     unrevealed construction roll (the Louse bite damage, rolled at spawn).
//     Revealed rolls surface through the T0.2 KnowledgeState projection, not
//     by leaking the raw byte.
//
// The field-by-field audit against CombatState -- every field classified
// mapped / derived / excluded-hidden, the completeness proof this encoder is
// held to, and the input T0.5's total-byte tripwire consumes -- lives in
// docs/public-view-audit.md, together with the schema-evolution note
// (which changes are additive vs breaking). Keep all three in step: this
// layout, that table, PUBLIC_VIEW_VERSION.
//
// VERSIONING: one stamp, PUBLIC_VIEW_VERSION, stored as a real field (the
// trajectory-record convention the full-state observation buffer follows for
// SCHEMA_VERSION -- this struct is a consumer-facing record, not a hashed
// engine state, so the stamp is per-instance data). It is deliberately INDEPENDENT of the engine
// SCHEMA_VERSION: a CombatState layout change that does not alter what is
// public (e.g. a new hidden flags bit) must not invalidate training shards,
// and a view-only change (a new encoded field) is invisible to engine traces.

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// Bumped by ANY change to this file's layout or field semantics; the audit
// doc's schema-evolution note says which changes are additive vs breaking.
// v1: T0.1 -- combat block + skeleton (the initial layout, 3760 bytes).
// v2: T0.2 -- run always-block, per-phase screen sections, the RunActionMask
//             channel and the KnowledgeState projection, all appended at the
//             tail (ADDITIVE), plus the declared population of keys_reserved
//             and act_reserved.
// v3: S2.13 -- `event_flags_hi`, the FIRED bitset's second word (event ids
//             32..63, the Act-2/3 events). APPENDED AFTER action_mask, i.e. at
//             the true struct tail, because `event_flags` may not be widened in
//             place: the contract above forbids changing an existing field's
//             width. A v2 record reinterprets losslessly under the v3 reading
//             with `event_flags_hi == 0`, which is exactly "no Act-2/3 event
//             has fired" -- true of every v2 record, since v2 predates their
//             being drawable at all. Additive case 2.
// v4: S2.2F -- kMonsterCap 7 -> 23 (engine schema v7). THE FIRST BREAKING
//             CHANGE: this is NOT a tail append. Three things inside this
//             record are sized by kMonsterCap -- `monsters[kMonsterCap]`,
//             the `monster_roll_known` / `monster_roll` pair, and (inside the
//             embedded mask channel) RunActionMask's per-(card, monster) and
//             per-(potion, monster) target grids. The monster block sits in the
//             MIDDLE of the record, so every offset after it MOVES, the mask
//             channel included: kPublicViewFixedBytes 5656 -> 8312,
//             sizeof(PvMask) 376 -> 616, sizeof(PublicView) 6036 -> 8932.
//             A v3 record therefore CANNOT be reinterpreted as v4 -- neither
//             additive case applies, and a consumer holding v3 observations
//             must re-encode rather than reread. This is the case the audit's
//             schema-evolution note calls breaking, and the version stamp is
//             what makes it detectable rather than silent.
// v5: S2.28 -- `second_boss_reserved` populated (ADDITIVE, the audit's case 1:
//             a declared-reserved field gaining its meaning; no offset moved).
// v6: S2.32 -- kEventOptionCap and kEventBoardCap 12 -> 20, for The Library's
//             twenty-card read board (TheLibrary.java:66-91: one option per
//             rolled card, pick exactly one). BREAKING, the v4 shape again:
//             PvEvent's board array sits mid-record and RunActionMask's
//             can_choose_event_option is embedded in the mask channel, so
//             offsets after each move. A v5 record cannot be reinterpreted as
//             v6; consumers re-encode. Match and Keep's twelve slots keep
//             their indices; board slots >= 12 are simply empty for it.
// v7: S3.51 -- `victory_kind` (RunVictoryKind) and `act4_floor_base`, the two
//             live bytes of RunState's schema-v9 pad carve (S3.31/S3.32).
//             ADDITIVE, case 2 (tail append): both are appended AFTER
//             `event_flags_hi`, i.e. at the struct's true physical tail (the
//             v3 note's reasoning applies again -- the mask channel is a
//             member, so "the tail" is past it), with two explicit pad bytes
//             rounding the addition back to a 4-byte multiple so nothing
//             compiler-inserted appears. No v6 offset moves; `sizeof` grows
//             8988 -> 8992. Declared "not present" value: **zero**, i.e.
//             RunVictoryKind::NONE and "no Act-4 crossing yet" -- and it is a
//             truthful reading of every ACTUAL v6-era PublicView: `victory_kind`
//             is written only at a run's terminal and no observable view is
//             ever taken after one (the run is over; legal_actions is empty),
//             while `act4_floor_base` is written only from the Act-4 crossing
//             onward (S3.32) and nothing in this tree calls encode_public_view
//             against such a state before this task -- the only consumer
//             (the training repo) does not exist yet. So no stored v6 record
//             anywhere loses information by this bump; it simply could not
//             have needed the two new bytes. keys_reserved/act_reserved/the
//             map array needed NO changes: they already carry Act 4's keys,
//             act index and the constant 5x7 special map generically (they
//             are copied wholesale regardless of which act is current).
inline constexpr uint32_t PUBLIC_VIEW_VERSION = 7;

// --- PvCard -----------------------------------------------------------------

// One card entry in the view. Carries the full public per-instance state:
// id, upgrade, the live cost (cost_now), and the runtime flags word --
// CardFlag bits are all consequences of observed public events (a Forethought
// grant, a this-turn cost write), and FREE_TO_PLAY_ONCE in particular is
// load-bearing information cost_now does not carry (the game keeps the cost
// and suppresses the spend, AbstractCard.java:888). CardInstance.misc is NOT
// carried: its only meaning today is transient mid-resolution scratch
// (AUTOPLAY_X_ENERGY payload) that never survives to a decision point.
struct PvCard {
    uint16_t card_id;  // CardId; NONE (0) == empty slot
    uint8_t upgrade;
    uint8_t cost_now;
    uint16_t flags;    // CardFlag bits (types.hpp)
};

static_assert(std::is_trivially_copyable_v<PvCard>);
static_assert(sizeof(PvCard) == 6, "PvCard layout drifted");

// --- PvPower ----------------------------------------------------------------

// One power entry. Unlike ObsPower this carries `counter`, the second
// oracle-visible per-instance number (The Bomb's damage, Panache's damage --
// see PowerSlot in types.hpp); for the duration debuffs it is the justApplied
// latch, which is derivable from public history (the application was an
// observed event) and always 0 at a WAITING_ON_USER boundary anyway.
// PowerSlot.pad0 (explicit padding) is not carried.
struct PvPower {
    uint16_t power_id;  // PowerId; NONE (0) == empty slot
    int16_t amount;
    int16_t counter;
};

static_assert(std::is_trivially_copyable_v<PvPower>);
static_assert(sizeof(PvPower) == 6, "PvPower layout drifted");

// --- PvMonster --------------------------------------------------------------

// One monster slot, at CombatState fidelity where the data is public:
// block (the stub's biggest gap), the raw flags word (every allocated bit --
// type-scoped and global alike -- latches a consequence of an OBSERVED event:
// splits, curl-ups, mode shifts, escapes; audited bit-by-bit in the audit
// doc), move_history, and the FULL kPowerCap power list (the stub truncates at
// 4). `intent` is suppressed to 0 under Runic Dome, exactly as in
// omniscient_encode_observation.  [omniscient-boundary-ok]
// MonsterState.pad0 is deliberately absent (see the header comment: it can
// hold an unrevealed construction roll).
struct PvMonster {
    uint16_t monster_id;  // MonsterId; NONE (0) == empty slot
    int16_t hp;
    int16_t max_hp;
    int16_t block;
    uint32_t flags;             // MonsterState.flags (two-region bitfield)
    uint8_t move_history[3];    // last 3 move ids, [0] = most recent (public)
    uint8_t intent;             // telegraphed next move; 0 under Runic Dome
    uint8_t occupied;           // 1 = live monster record in this slot
    uint8_t power_count;        // true live power count (never truncated here)
    uint8_t pad0[2];            // explicit padding, always zero
    PvPower powers[kPowerCap];  // FULL list -- all 24 slots
};

static_assert(std::is_trivially_copyable_v<PvMonster>);
static_assert(sizeof(PvMonster) == 20 + 6 * kPowerCap, "PvMonster layout drifted");

// --- v2 element types (T0.2) --------------------------------------------------

// One relic with its DISPLAYED counter (plan §2.1 always-block). Projected from
// RunState.relics outside combat and from the CombatState relic MIRROR while
// combat_active -- in-combat counter ticks (Kunai, Ink Bottle, ...) live in the
// mirror until the end-of-combat fold-back, so reading RunState there would
// show the player a stale number the screen does not.
struct PvRelic {
    uint16_t relic_id;  // RelicId; NONE (0) == empty slot
    int16_t counter;    // the relic's displayed per-instance counter
};

static_assert(std::is_trivially_copyable_v<PvRelic>);
static_assert(sizeof(PvRelic) == 4, "PvRelic layout drifted");

// One act-map node, at RunState.MapNode fidelity. The whole current-act map is
// public: the map screen draws every node and every edge from the moment the
// act starts. The burning-elite node is NOT a node field -- it is the
// (emerald_x, emerald_y) pair below, which is drawn on the same screen.
struct PvMapNode {
    uint8_t room_type;  // RoomType (0 == no node at this grid position)
    uint8_t edges;      // outgoing-edge bitfield into the next row
};

static_assert(std::is_trivially_copyable_v<PvMapNode>);
static_assert(sizeof(PvMapNode) == 2, "PvMapNode layout drifted");

// One assembled reward-screen row (RunRewardItem). Everything here is public
// ONCE THE SCREEN IS OPEN -- assembly and the screen opening are the same step
// -- which is why the encoder gates the whole section on the screen actually
// being up (see the header comment: Dead Adventurer pre-loads rc.rewards
// before its combat, and those rows are rolls the player has not seen).
struct PvRewardItem {
    int32_t gold;                           // GOLD / STOLEN_GOLD base amount
    int32_t bonus_gold;                     // Golden Idol's +25 % (GOLD only)
    uint16_t id;                            // RelicId (RELIC) / PotionId (POTION)
    uint16_t card_ids[kRewardCardCap];      // CARDS: the offer
    uint8_t card_upgrades[kRewardCardCap];  // CARDS: per-offer upgrade count
    uint8_t kind;                           // RewardItemKind (0 == empty row)
    uint8_t card_count;                     // CARDS: number offered
};

static_assert(std::is_trivially_copyable_v<PvRewardItem>);
static_assert(sizeof(PvRewardItem) == 12 + 3 * kRewardCardCap,
              "PvRewardItem layout drifted");

// The reward screen. `active` is the gate: 1 exactly when the screen is on
// screen (COMBAT_REWARD, or Neow's ITEM_REWARD sub-screen); when it is 0 every
// other field reads zero.
struct PvReward {
    PvRewardItem items[kRewardItemCap];
    uint8_t count;
    uint8_t open_card_item;  // index of the open CARD pick screen, else 0xFF
    uint8_t active;
    uint8_t pad0;  // explicit padding, always zero
};

static_assert(std::is_trivially_copyable_v<PvReward>);
static_assert(sizeof(PvReward) == 4 + kRewardItemCap * sizeof(PvRewardItem),
              "PvReward layout drifted");

// One shop shelf row. The whole current-visit stock is public once the floor is
// entered (plan §2.4): the merchant draws every card, relic and potion with its
// price -- including a Courier-restocked row, whose identity has been a real,
// seeded CardId since S3.24 (shop.hpp's courier_restock_stream) rather than an
// unnameable sentinel.
struct PvShopSlot {
    uint16_t id;     // CardId / RelicId / PotionId per shelf
    int16_t price;   // the priced-through cost for this visit
    uint8_t sold;    // 1 once bought
    uint8_t upgrade; // card shelves only
};

static_assert(std::is_trivially_copyable_v<PvShopSlot>);
static_assert(sizeof(PvShopSlot) == 6, "PvShopSlot layout drifted");

struct PvShop {
    PvShopSlot colored[kShopColoredCount];
    PvShopSlot colorless[kShopColorlessCount];
    PvShopSlot relics[kShopRelicCount];
    PvShopSlot potions[kShopPotionCount];
    int16_t actual_purge_cost;  // this visit's removal price
    uint8_t sale_index;         // which colored shelf carries the sale tag
    uint8_t screen;             // ShopScreenKind
    uint8_t purge_available;
    uint8_t active;             // 1 while the merchant's floor is on screen
};

static_assert(std::is_trivially_copyable_v<PvShop>);
static_assert(sizeof(PvShop) ==
                  6 + sizeof(PvShopSlot) * (kShopColoredCount + kShopColorlessCount +
                                            kShopRelicCount + kShopPotionCount),
              "PvShop layout drifted");

// One Match-and-Keep board slot. THE IDENTITY IS MASKED: GremlinMatchGame deals
// twelve FACE-DOWN cards from a miscRng shuffle (shrines.cpp), so a slot's
// card_id is an unrevealed realization until the slot is flipped or matched.
// `revealed` is the declared gate -- card_id/upgrade read zero unless it is 1.
//
// KNOWN COARSENING (plan §2.4 pins this row for T0.4's sampler): a
// perfect-memory player also remembers the identities of a MISSED pair, which
// the game flips back face down. The engine keeps no record of past flips, so
// the view under-informs there rather than leaking. Widening it needs a
// per-slot "seen" latch in the event body, which is T0.4's Match-and-Keep row.
struct PvEventBoardCard {
    uint16_t card_id;   // CardId, or 0 while unrevealed
    uint8_t upgrade;    // 0 while unrevealed
    uint8_t taken;      // 1 once the pair was matched and left the board
    uint8_t revealed;   // 1 iff this slot's identity is public
    uint8_t pad0;       // explicit padding, always zero
};

static_assert(std::is_trivially_copyable_v<PvEventBoardCard>);
static_assert(sizeof(PvEventBoardCard) == 6, "PvEventBoardCard layout drifted");

// The open event dialog. `scratch0..3` are EVENT-DEFINED, so they are carried
// under a PER-EVENT publicity mask (event_scratch_public_mask, public_view.cpp)
// rather than wholesale: most events park displayed numbers there (Scrap Ooze's
// chance/damage, We Meet Again's three offers), but Dead Adventurer packs the
// shuffled reward order and the identity of the elite it will spring -- pure
// unrevealed rolls. A masked-out word reads zero.
struct PvEvent {
    uint16_t event_id;   // EventId (0 == none)
    int16_t scratch[4];  // per-event, masked per event (0 where not public)
    uint8_t screen;      // event-defined page index
    uint8_t grid_kind;   // EventGridKind
    uint8_t active;      // 1 while an event body owns the screen
    uint8_t scratch_public_mask;  // bit i == 1 iff scratch[i] is carried
    PvEventBoardCard board[kEventBoardCap];
};

static_assert(std::is_trivially_copyable_v<PvEvent>);
static_assert(sizeof(PvEvent) ==
                  14 + kEventBoardCap * sizeof(PvEventBoardCard),
              "PvEvent layout drifted");

// The floor-0 Neow blessing. The four rolled options are DISPLAYED (the whole
// point of the screen), so option types and drawbacks are public the moment the
// run starts.
struct PvNeow {
    int16_t hp_bonus;                          // frozen at blessing time
    uint16_t grid_picked[kNeowGridPickCap];    // master-deck indices, pick order
    uint8_t option_type[kNeowOptionCount];     // NeowRewardType per slot
    uint8_t option_drawback[kNeowOptionCount]; // NeowDrawback per slot
    uint8_t screen;        // NeowScreen
    uint8_t chosen;        // activated option, or kNeowNoChoice
    uint8_t grid_mode;     // NeowGridMode
    uint8_t grid_needed;
    uint8_t grid_done;
    uint8_t active;        // 1 while phase == NEOW
};

static_assert(std::is_trivially_copyable_v<PvNeow>);
static_assert(sizeof(PvNeow) == 8 + 2 * kNeowOptionCount + 2 * kNeowGridPickCap,
              "PvNeow layout drifted");

// The legal-action mask, carried BY VALUE inside the view (plan §2.1: "the
// legal-action mask is an observation channel ... RunActionMask bytes are part
// of the hashed public serialization and sit under the same twin tests").
// Embedding it is what makes that structural rather than a convention: nothing
// downstream can hash the view and forget the mask.
//
// RunActionMask is all bool / uint8 / uint8-arrays, so alignof is 1 and it has
// no implicit padding of its own; the explicit tail pad below rounds the whole
// view back to its 4-byte alignment so PublicView keeps no compiler-inserted
// bytes either. The pad is 1..4 bytes, never 0, so the array is always valid.
inline constexpr std::size_t kPvMaskPad = 4 - (sizeof(RunActionMask) % 4);

struct PvMask {
    RunActionMask mask;
    uint8_t pad_end[kPvMaskPad];  // explicit padding, always zero
};

static_assert(std::is_trivially_copyable_v<PvMask>);
static_assert(alignof(RunActionMask) == 1,
              "RunActionMask gained a member wider than a byte -- re-check "
              "PvMask's padding and the PublicView layout walk");
static_assert(sizeof(PvMask) % 4 == 0, "PvMask must round the view to align 4");

// --- PublicView -------------------------------------------------------------

// Field groups, in layout order (offsets are pinned by the layout-walk
// static_asserts in tests/public_view_test.cpp):
//   header    : version stamp, run-phase echo, combat-section validity flag.
//   combat    : the full combat block (valid iff combat_active == 1; zeroed
//               otherwise). Piles carry card VALUES, never card_pool indices
//               (pool indices are engine bookkeeping, not information).
//   belt      : the potion belt (RunState-owned; public in every phase).
//   reserved  : zero-filled v1 forward structure for known S2/S3 content
//               (plan §2.1): keys bitflags (RunState.keys exists today), the
//               boss-relic choice screen's three slots, the second-boss slot
//               (A20 double boss), and the act index (1..4). Populating a
//               reserved field with its declared meaning is an ADDITIVE
//               change; see the schema-evolution note.
struct PublicView {
    // -- header --
    uint32_t public_view_version;  // PUBLIC_VIEW_VERSION at encode time
    uint8_t run_phase;             // RunPhase echo (public screen identity)
    uint8_t combat_active;         // 1 = the combat section below is live
    uint8_t combat_phase;          // CombatPhase (0 unless combat_active)
    uint8_t stance;                // player stance id (0 = None)

    // -- combat: header/player scalars --
    uint16_t turn;
    uint16_t combat_gold;           // gold accrued inside this combat (public)
    uint32_t combat_flags;          // CombatState.flags (audited bit-by-bit)
    int16_t player_hp;
    int16_t player_max_hp;
    int16_t player_block;
    int16_t player_energy;
    uint8_t cards_played_this_turn;
    uint8_t player_power_count;
    uint8_t hand_count;
    uint8_t draw_count;
    uint8_t discard_count;
    uint8_t exhaust_count;
    uint8_t limbo_count;
    uint8_t monster_count;

    // -- combat: player powers (full list -- the stub carried none) --
    PvPower player_powers[kPowerCap];

    // -- combat: piles as card values --
    PvCard hand[kHandCap];        // engine order (public: visible on screen)
    PvCard draw[kDrawCap];        // CANONICALLY SORTED unordered multiset --
                                  // ascending (card_id, upgrade, cost_now,
                                  // flags); the sort key is part of the schema
    PvCard discard[kDiscardCap];  // engine order (public: arrivals observed)
    PvCard exhaust[kExhaustCap];  // engine order (public: exhausts observed)
    PvCard limbo[kLimboCap];      // engine order (mid-resolution holding zone)

    // -- combat: monsters --
    PvMonster monsters[kMonsterCap];

    // -- belt (public in every phase, combat or not) --
    uint16_t potions[kPotionCap];  // PotionId per slot; NONE (0) == empty
    uint8_t potion_slot_count;     // live slot count (A11 removes one)

    // -- reserved v1 forward structure. `keys_reserved` and `act_reserved` are
    //    POPULATED from v2 on (both name live public RunState fields: the
    //    campfire's Recall writes the ruby key in S1, and `act` is 1 there);
    //    the other two stay zero until S2 lands their screens. Populating a
    //    reserved field with its declared meaning is the audit note's additive
    //    case 1 -- a v1 record reads zero, which for keys means "no keys" and
    //    for act is remapped to 1 by the declared reader rule. --
    uint8_t keys_reserved;                   // kKey* bitflags (run_state.hpp)
    uint16_t boss_relic_choice_reserved[3];  // S2 boss-chest relic screen
    // POPULATED FROM v5 (S2.28). The EncounterDef id of the SECOND Act-3 boss
    // of an A20 double-boss run -- boss_list[1] of that act's shuffle -- and 0
    // in every other state, which is the declared "not present" value a v4
    // record already reads truthfully (no v4 run could reach a second boss).
    //
    // CARRIED ONLY ONCE IT IS PUBLIC, i.e. from the moment the double-boss
    // transition puts the player in the second boss room; before that the
    // identity is a hidden realization the belief sampler continues, and
    // carrying it would leak. That is not a nicety -- the hidden-twin gate
    // compares this field, so an early populate FAILS rather than leaks.
    //
    // It is NOT redundant with `current_encounter_id`, which is written only
    // while the phase is COMBAT / COMBAT_REWARD: at RUN_OVER -- the state every
    // finished run is observed in -- this is the only record of WHICH second
    // boss the run was decided by.
    uint16_t second_boss_reserved;           // S2 A20 double-boss slot
    uint8_t act_reserved;                    // act index 1..4
    uint8_t pad_tail[3];                     // explicit padding, always zero

    // ======================= v2 (T0.2) tail append ==========================
    // Everything below was APPENDED at the tail: no v1 field moved, changed
    // width, or changed meaning, so a v1 record reinterprets losslessly under
    // the v2 reading with zeros here (the audit note's additive case 2, whose
    // "zero == not present" declaration each group below states).

    // -- always-block: run scalars (plan §2.1) --
    int32_t gold;
    float event_pity_monster;   // EventHelper.MONSTER_CHANCE, tracker state
    float event_pity_shop;      // EventHelper.SHOP_CHANCE
    float event_pity_treasure;  // EventHelper.TREASURE_CHANCE
    uint32_t event_flags;       // one-shot event FIRED bitset (fires observed)
    uint32_t shop_flags;        // one-shot shop bitset
    int16_t run_hp;             // out-of-combat truth; == player_hp in combat
    int16_t run_max_hp;
    int16_t card_blizz_randomizer;  // rare/uncommon reward pity
    int16_t blizzard_potion_mod;    // potion-drop ratchet
    int16_t purge_cost;             // the run-persistent ramping removal price
    uint16_t floor;
    uint16_t master_deck_count;
    uint16_t event_membership;    // REMAINING-pool bitsets: bit set == still in
    uint16_t special_membership;  // the pool (initial lists are public rules,
                                  // every removal was an observed event)
    uint16_t boss_ids[kBossIdCap];  // act boss identity, displayed on the map

    // -- always-block / screen-flow scalars --
    uint8_t ascension;
    uint8_t shrine_membership;
    uint8_t relic_count;
    uint8_t cur_x;            // own map column; kNeowColumn at Neow
    uint8_t room_type;        // RoomType of the room being occupied
    uint8_t combat_outcome;   // RunCombatOutcome of the last combat
    uint8_t pending_bottle;   // MasterBottleKind of the modal bottle grid
    uint8_t emerald_x;        // burning-elite node, or kNoEmeraldNode --
    uint8_t emerald_y;        // drawn on the map screen from act start
    uint8_t monster_cursor;   // == the consumed prefix lengths below
    uint8_t elite_cursor;
    uint8_t boss_cursor;
    // The encounter the player is currently INSIDE (registry EncounterDef.id),
    // or 0. Revealed by room entry; the unconsumed list suffix is a hidden
    // realization T0.4 continues as a Markov chain, and is never carried.
    uint8_t current_encounter_id;
    uint8_t rest_screen;      // RestScreen while phase == REST_SITE, else 0
    // TreasureChest, masked (plan §2.1's named trap): the size is drawn on the
    // room before any interaction, the CONTENTS roll at construction and are
    // revealed only by the open action. Tier/gold read zero until opened.
    uint8_t chest_size;
    uint8_t chest_relic_tier;
    uint8_t chest_has_gold;
    uint8_t chest_opened;
    // -- KnowledgeState projection (knowledge.hpp; plan §2.2/§3.1) --
    uint8_t knowledge_chain_count;   // length of the known relative-order chain
    uint8_t knowledge_exact_prefix;  // how many of it are at exact positions
    uint8_t knowledge_full_order;    // 1 == the chain IS the pile (Frozen Eye)
    uint8_t monster_roll_known[kMonsterCap];  // revealed construction rolls,
    uint8_t monster_roll[kMonsterCap];        // parallel to monsters[]

    // The consumed encounter prefix (plan §1 tracker state), as registry
    // EncounterDef ids. Entry i is meaningful for i < the matching cursor;
    // boss_prefix[0] is additionally always carried when the boss list exists,
    // because the act boss is public from the map screen.
    uint8_t monster_prefix[kMaxMonsterList];
    uint8_t elite_prefix[kMaxEliteList];
    uint8_t boss_prefix[kMaxBossList];

    // Per-draw-slot order-constraint annotations, PARALLEL TO draw[] (which is
    // canonically sorted, so these are the only way order knowledge reaches the
    // view -- unsorting would put the hidden arrangement back in the bytes).
    //   draw_constraint_rank[i] : 0 == unconstrained, else the 1-based position
    //       of that card in the known relative-order chain (chain[0] == 1 is
    //       nearest the top). Two slots with a rank are known to sit in rank
    //       order; nothing is claimed about the gaps between them.
    //   draw_exact_pos[i]       : 0 == not exactly known, else the 1-based
    //       from-the-top position of that card. Under Frozen Eye every slot
    //       carries one, which reconstructs the true order exactly.
    // Ties are assigned to the lowest matching sorted slot, so two states with
    // the same knowledge over the same multiset annotate identically.
    uint8_t draw_constraint_rank[kDrawCap];
    uint8_t draw_exact_pos[kDrawCap];

    // -- always-block: the collections --
    PvCard master_deck[kMasterDeckCap];  // ENGINE ORDER, not sorted: the master
                                         // deck's order is public (it is the
                                         // fold of observed acquisitions) AND
                                         // it is the index space the mask's
                                         // can_choose_master_deck[] addresses
    PvRelic relics[kRelicCap];           // with displayed counters
    PvMapNode map[kMapRows * kMapCols];  // the full current-act map

    // -- per-phase screen sections (each carries its own `active` gate) --
    PvReward rewards;
    PvShop shop;
    PvEvent event;
    PvNeow neow;

    // -- the legal-action mask channel (always live) --
    PvMask action_mask;

    // ======================= v3 (S2.13) tail append =========================
    // The FIRED bitset's second word: event ids 32..63 at bit (id-32), the
    // Act-2/3 events S2.02 registered and S2.13 made drawable. It sits AFTER
    // action_mask rather than beside `event_flags` because only a true tail
    // append leaves every v2 offset -- the mask channel's included -- where it
    // was. Zero means "none of ids 32..63 has fired", which is also what a v2
    // record reads as. Public for the same reason `event_flags` is: each fire
    // was an observed event.
    //
    // PvMask is asserted 4-aligned-and-4-sized above, so this needs no pad.
    uint32_t event_flags_hi;

    // ======================= v7 (S3.51) tail append ==========================
    // The run-outcome kind and the Act-4 floor base -- RunState's schema-v9
    // pad carve (S3.31/S3.32), encoded here from v7 on. Both are excluded from
    // v6 (docs/public-view-audit.md: "S3.32 writes it, S3.51 encodes it").
    uint8_t victory_kind;      // RunVictoryKind (run_state.hpp); 0 == NONE
    uint8_t act4_floor_base;   // the floor Act 4 was constructed at; 0 == none
    uint8_t pad_v7[2];         // explicit padding, always zero -- rounds the
                               // v7 tail back to a 4-byte multiple
};

static_assert(std::is_trivially_copyable_v<PublicView>,
              "PublicView must be trivially copyable (POD observation record)");
// v1 prefix (unchanged, and every v1 offset with it):
//   32 (header + combat scalars) + 144 (player powers) + 60 (hand)
//   + 3*768 (draw/discard/exhaust) + 48 (limbo) + 1148 (monsters)
//   + 10 (potions) + 1 (slot count) + 10 (reserved) + 3 (pad_tail) == 3760.
// v2 tail: 50 (run scalars incl. boss_ids) + 21 (screen-flow / chest /
//   knowledge scalars) + 14 (monster rolls) + 29 (encounter prefixes)
//   + 256 (draw annotations) + 768 (master deck) + 160 (relics) + 210 (map)
//   + 196 (rewards) + 84 (shop) + 86 (event) + 22 (Neow) == 1896, so the fixed
//   part ends at 5656 and the mask channel closes the record.
// The layout-walk static_asserts in tests/public_view_test.cpp prove there is
// no implicit padding anywhere in this figure.
// v3 tail: 4 (event_flags_hi), appended after the mask channel.
// v4 (S2.2F): kMonsterCap 7 -> 23 moves this. The monster block grows
// 7 -> 23 PvMonster (164 B each, +2624) and the two kMonsterCap-sized roll
// arrays grow +32, so the fixed part goes 5656 -> 8312. Measured, not derived.
// v6 (S2.32): kEventBoardCap 12 -> 20 grows PvEvent's board by 8 x 6 B, so the
// fixed part goes 8312 -> 8360 (the mask channel's own growth is in PvMask).
inline constexpr std::size_t kPublicViewFixedBytes = 8360;
// v7 (S3.51): 4 more tail bytes AFTER event_flags_hi and the mask channel --
// victory_kind + act4_floor_base + pad_v7[2].
inline constexpr std::size_t kPublicViewV7TailBytes = 4;
static_assert(sizeof(PublicView) == kPublicViewFixedBytes + sizeof(PvMask) +
                                        sizeof(uint32_t) +
                                        kPublicViewV7TailBytes,
              "PublicView size changed -- bump PUBLIC_VIEW_VERSION, update the "
              "audit table (docs/public-view-audit.md) and its schema-evolution "
              "note, and re-check the layout-walk asserts");
static_assert(sizeof(PublicView) == 8992,
              "PublicView size changed -- see the assert above. This literal is "
              "pinned deliberately: a RunActionMask that grows is a public-view "
              "schema change too, and must be reviewed like any other. It was "
              "6032 through v2; v3 tail-appended event_flags_hi (6036); v4 "
              "grew kMonsterCap 7 -> 23, which moves the monster block, the "
              "roll arrays AND the mask channel's target grids (8932); v6 grew "
              "the two event caps 12 -> 20 for The Library's board (+48 fixed, "
              "+8 mask -> 8988); v7 tail-appended victory_kind + "
              "act4_floor_base + 2 pad bytes (8992)");
static_assert(offsetof(PublicView, action_mask) == kPublicViewFixedBytes,
              "the fixed part must end exactly where kPublicViewFixedBytes "
              "says. A TAIL APPEND may never move the mask channel; a "
              "kMonsterCap change moves it by construction, which is precisely "
              "why that is a BREAKING public-view version bump and not an "
              "additive one");

// --- encode_public_view ------------------------------------------------------

// Fill `out` from `rc`. `out` is fully overwritten (every byte, padding and
// reserved fields included, is assigned), so the caller need not pre-zero it.
// No heap allocation. The combat section is populated only while
// rc.phase == RunPhase::COMBAT; in every other phase it reads zero and
// combat_active == 0. Each run-phase screen section is likewise gated by its
// own phase and reports that through its `active` byte.
//
// COST: this also fills the embedded mask channel, i.e. it runs a full
// legal_actions(rc, ...) internally. That is deliberate (see PvMask), and it is
// why this is a pull API and not something advance() pays for on every step;
// callers on the hot path keep using legal_actions() directly.
void encode_public_view(const RunController& rc, PublicView& out) noexcept;

// --- public_hash -------------------------------------------------------------

// XXH3-64 over the whole OBJECT REPRESENTATION of a PublicView -- the same
// primitive hash_state() (state_hash.hpp) applies to CombatState/RunState, and
// the identity of a public information set: two states a perfect-memory player
// could not tell apart hash equal, and any difference the player CAN see
// changes the hash.
//
// THE MASK IS IN THE HASH, structurally. RunActionMask is a member of this
// struct (see PvMask), so hashing sizeof(PublicView) bytes hashes the legality
// channel too. Nothing downstream can hash "the view" and forget the mask,
// which is the property plan §2.1 asks for.
//
// WHY HASHING RAW BYTES IS SOUND HERE. A byte hash is only meaningful when
// every byte is deterministic, and both halves of that hold by test rather than
// by convention:
//   * PublicView has NO IMPLICIT PADDING. Every gap is a declared pad member
//     (pad0 / pad_tail / PvMask::pad_end), and the layout-walk tests in
//     tests/public_view_test.cpp require each member to start where the
//     previous one ended -- for the struct itself and for every element type it
//     contains. (This is the same discipline conventions.md §8 records for
//     RunState, where undeclared gaps produced a host-dependent hash.)
//   * encode_public_view() ASSIGNS EVERY BYTE. Its first statement is
//     `out = PublicView{}`: the aggregate value-init zero-fills every member,
//     explicit pads included, and the memberwise assignment then carries those
//     zeros into `out`. Because no implicit padding exists, "every member" is
//     "every byte" -- so a caller may hash a view encoded into a buffer holding
//     arbitrary previous garbage and get the same answer as into a fresh one.
//     PublicHash.EncodingTwiceIntoDirtyBuffersHashesEqual pins exactly that.
//
// The hash is NOT stable across PUBLIC_VIEW_VERSION bumps (the version stamp is
// itself a hashed field, and a layout change moves everything after it) and is
// NOT a substitute for hash_state(): it deliberately cannot distinguish states
// that differ only in hidden realizations.
[[nodiscard]] uint64_t public_hash(const PublicView& view) noexcept;

// Convenience: encode `rc` into a temporary view and hash it. Equivalent to
// encode_public_view() followed by public_hash(), and it pays that encode --
// including the internal legal_actions() call PvMask costs. A caller that wants
// both the view and its hash should encode once and hash the result.
[[nodiscard]] uint64_t public_hash(const RunController& rc) noexcept;

}  // namespace sts::engine
