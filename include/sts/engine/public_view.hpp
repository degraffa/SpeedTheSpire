#pragma once

// PublicView -- the versioned public-information observation record
// (docs/training-plan.md §2.1). A pull API beside legal_actions():
// encode_public_view(rc, out) flattens everything a perfect-memory player could
// know from the revealed action-observation history into one fixed-layout,
// trivially-copyable POD. It deliberately does NOT ride on StepResult (which
// embeds ObsBuffer by value on the hot path -- fattening it would tax every
// advance() call whether or not the caller wants the full view).
//
// SCOPE (task T0.1): the COMBAT section plus the v1 schema skeleton. The
// combat section closes every gap in the ObsBuffer stub (observation.hpp):
// player powers, monster block, FULL per-monster power lists (no 4-of-24
// truncation), the draw pile as an UNORDERED MULTISET, discard / exhaust /
// limbo contents, and the potion belt. Run-phase screen sections, the
// RunActionMask observation channel, and the KnowledgeState draw-order
// projection are T0.2; the reserved fields below are the v1 forward structure
// they and S2/S3 fill in.
//
// THE INFORMATION CONTRACT (plan §1): public = derivable by a perfect-memory
// player from revealed history, NOT "currently on screen". The encoder hides
// RNG *realizations*, never rules. Concretely, in this section:
//   * the draw pile's CONTENTS are public (the player tracks the multiset);
//     its ORDER is hidden -- so draw[] is canonically sorted, making the
//     encoding order-invariant (the T0.5 hidden-twin byte-equality property).
//     Hand / discard / exhaust / limbo order IS public (each card's arrival
//     was an observed event), so those piles encode in engine order.
//   * Runic Dome intent suppression happens HERE, exactly as in
//     encode_observation (the game's two rendering guards,
//     AbstractMonster.java:258/:749, touch no game state -- see the write-up
//     at observation.hpp's encoder). move_history stays visible under the
//     Dome: past moves were observed as they resolved; only the telegraphed
//     NEXT move is hidden.
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
// trajectory-record convention ObsBuffer follows for SCHEMA_VERSION -- this
// struct is a consumer-facing record, not a hashed engine state, so the stamp
// is per-instance data). It is deliberately INDEPENDENT of the engine
// SCHEMA_VERSION: a CombatState layout change that does not alter what is
// public (e.g. a new hidden flags bit) must not invalidate training shards,
// and a view-only change (a new encoded field) is invisible to engine traces.

#include <cstdint>
#include <type_traits>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// Bumped by ANY change to this file's layout or field semantics; the audit
// doc's schema-evolution note says which changes are additive vs breaking.
// v1: T0.1 -- combat block + skeleton (this initial layout).
inline constexpr uint32_t PUBLIC_VIEW_VERSION = 1;

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
// encode_observation. MonsterState.pad0 is deliberately absent (see the
// header comment: it can hold an unrevealed construction roll).
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

    // -- reserved v1 forward structure (all zero in v1; see the header
    //    comment and the audit doc's schema-evolution note) --
    uint8_t keys_reserved;                   // kKey* bitflags (run_state.hpp)
    uint16_t boss_relic_choice_reserved[3];  // S2 boss-chest relic screen
    uint16_t second_boss_reserved;           // S2 A20 double-boss slot
    uint8_t act_reserved;                    // act index 1..4
    uint8_t pad_tail[3];                     // explicit padding, always zero
};

static_assert(std::is_trivially_copyable_v<PublicView>,
              "PublicView must be trivially copyable (POD observation record)");
// 32 (header + combat scalars) + 144 (player powers) + 60 (hand)
// + 3*768 (draw/discard/exhaust) + 48 (limbo) + 1148 (monsters)
// + 10 (potions) + 1 (slot count) + 10 (reserved) + 3 (pad_tail) == 3760.
// The layout-walk static_asserts in tests/public_view_test.cpp prove there is
// no implicit padding anywhere in this figure.
static_assert(sizeof(PublicView) == 3760,
              "PublicView size changed -- bump PUBLIC_VIEW_VERSION, update the "
              "audit table (docs/public-view-audit.md) and its schema-evolution "
              "note, and re-check the layout-walk asserts");

// --- encode_public_view ------------------------------------------------------

// Fill `out` from `rc`. `out` is fully overwritten (every byte, padding and
// reserved fields included, is assigned), so the caller need not pre-zero it.
// No heap allocation. The combat section is populated only while
// rc.phase == RunPhase::COMBAT; in every other phase it reads zero and
// combat_active == 0 (run-phase screen sections are T0.2).
void encode_public_view(const RunController& rc, PublicView& out) noexcept;

}  // namespace sts::engine
