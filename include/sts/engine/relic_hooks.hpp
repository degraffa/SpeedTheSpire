#pragma once

// Relic-hook framework (Stage B B3.24) -- the relic side of the combat hook
// dispatch. B3.2 built the POWER hook framework and left the relic dispatch
// stages as ordered structural call sites ("relics onPlayCard (acq order) -- none
// yet (B3.24+)"). This layer is the first relic consumer: it answers "when a game
// event fires (combat starts, a turn starts/ends, a card is played/exhausted, HP
// is lost, combat is won, ...), which of the player's relics respond, IN WHAT
// ORDER, and what do they queue?"
//
// ACQUISITION ORDER (design doc §4.3 / stage-a trap 8): relics are stored and
// triggered in the order they were obtained. Every dispatch function here iterates
// the relic list index 0..count-1 -- that IS acquisition order -- and, at each
// hook, relics fire AFTER powers where the frozen Java interleaves them
// (GameActionManager card-play fan-out: player powers -> monster powers -> relics;
// CardGroup.moveToExhaustPile: relics -> player powers; AbstractCreature.addBlock:
// player relics onPlayerGainedBlock -> powers onGainedBlock). The power-vs-relic
// ordering lives at the wired call sites (power_hooks.cpp / action_queue.cpp), not
// here; this module owns the relic-list iteration order.
//
// A relic's response is either DATA (a RelicHook->effect-program binding in the
// generated RelicDef table, relics.hpp) or NATIVE (the escape hatch
// dispatch_native_relic_hook below -- for counter relics (Nunchaku/Pen Nib),
// conditionals (Orichalcum), once-per-combat flags (Centennial Puzzle), and heals
// (Burning Blood/Blood Vial)). The registry marks each relic `native` or not and
// lists the hooks it binds (registry/relics.yaml).
//
// RELIC HOOK SET (DISTINCT from the power Hook enum): relics carry battle-start /
// turn-start / end-turn / victory / on-attack hooks that powers do not
// (AbstractRelic.java:492-620). The generated sts::registry::RelicHook mirrors the
// enum below; relics.hpp pins the two byte-equal.
//
// COMBAT STORAGE SEAM (B4.3): CombatState carries no relic list yet -- adding one
// is a schema change owned by B4.3 (RunState population + additive fields). Until
// it lands, player_relics() returns an EMPTY view, so the dispatch sites wired
// into power_hooks.cpp / action_queue.cpp are genuine no-ops and the 20 combat
// fixtures stay byte-identical. The acquisition-order dispatch and per-relic combat
// behaviour are proven by relic_hooks_test.cpp constructing relic lists directly;
// when B4.3 gives CombatState its relic mirror, player_relics() returns it and the
// wiring lights up with a one-line change.

#include <cstdint>

#include "sts/engine/combat_state.hpp"  // CombatState
#include "sts/engine/run_state.hpp"     // RelicSlot (the {relic_id, counter} slot)
#include "sts/engine/types.hpp"         // RelicId, CardId, PowerId

namespace sts::engine {

// --- Relic hook identity tags -----------------------------------------------
//
// AUTHORITATIVE mirror of the generated sts::registry::RelicHook (relics.hpp pins
// the two byte-equal). Values are pinned/append-only (design doc §4.4); the frozen
// acquisition-order dispatch lives in this module's functions (the list iteration),
// NOT in these numbers -- a RelicHook is an identity tag, not a sequence position.
enum class RelicHook : uint8_t {
    AT_PRE_BATTLE = 0,               // atPreBattle (pre-combat reset)
    AT_BATTLE_START = 1,             // atBattleStart
    AT_BATTLE_START_PRE_DRAW = 2,    // atBattleStartPreDraw
    AT_TURN_START = 3,               // atTurnStart (pre-draw)
    AT_TURN_START_POST_DRAW = 4,     // atTurnStartPostDraw
    ON_PLAYER_END_TURN = 5,          // onPlayerEndTurn (applyEndOfTurnRelics)
    ON_USE_CARD = 6,                 // onUseCard
    ON_PLAY_CARD = 7,                // onPlayCard
    ON_EXHAUST = 8,                  // onExhaust
    ON_CARD_DRAW = 9,                // onCardDraw
    ON_GAINED_BLOCK = 10,            // onPlayerGainedBlock
    WAS_HP_LOST = 11,                // wasHPLost / onLoseHp
    ON_ATTACK = 12,                  // onAttack / onAttackToChangeDamage
    ON_VICTORY = 13,                 // onVictory (combat end)
};

inline constexpr int kRelicHookCount = 14;

// --- RelicHookContext --------------------------------------------------------
//
// The event parameters a relic hook body may read. Only the fields relevant to a
// given hook are set; the rest are value-init defaults.
struct RelicHookContext {
    uint16_t card_id = 0;          // CardId of the card involved (play/use/exhaust)
    uint8_t card_pool_index = 0;   // its pool index (redirects; unused by S1 relics)
    uint8_t card_is_attack = 0;    // 1 if that card is an ATTACK (Nunchaku/Pen Nib)
    int32_t amount = 0;            // event amount: block gained / hp lost
};

// --- Combat relic view -------------------------------------------------------
//
// The player's relics in acquisition order. RETURNS AN EMPTY VIEW today: CombatState
// has no relic mirror yet (that additive field is B4.3's schema-owned change). When
// it lands, this returns {s.relics, s.relic_count}. The wired dispatch sites call
// this, so they are pure no-ops until then (fixtures byte-identical).
struct RelicView {
    RelicSlot* relics;
    uint8_t count;
};
[[nodiscard]] RelicView player_relics(CombatState& s) noexcept;

// --- Generic dispatch --------------------------------------------------------
//
// Fire `hook` over the relic list in ACQUISITION ORDER (index 0..count-1). Each
// responding relic (a binding for `hook` exists) either runs its data program
// (queues its steps, player-relative) or routes to the native escape hatch (which
// may mutate the relic's counter). `relics` is mutable so counter relics persist
// their counter in the RelicSlot (stage-a §4.3's {relic_id, counter}).
void dispatch_relic_hook(CombatState& s, RelicSlot* relics, uint8_t count,
                         RelicHook hook, const RelicHookContext& ctx) noexcept;

// --- Per-hook entry points (mirror the frozen call sites) --------------------
// Each fills the relevant RelicHookContext fields and calls dispatch_relic_hook.

void dispatch_relics_at_battle_start(CombatState& s, RelicSlot* relics,
                                     uint8_t count) noexcept;
void dispatch_relics_at_turn_start(CombatState& s, RelicSlot* relics,
                                   uint8_t count) noexcept;
void dispatch_relics_on_player_end_turn(CombatState& s, RelicSlot* relics,
                                        uint8_t count) noexcept;
void dispatch_relics_on_use_card(CombatState& s, RelicSlot* relics, uint8_t count,
                                 uint16_t card_id, uint8_t pool_index) noexcept;
void dispatch_relics_on_play_card(CombatState& s, RelicSlot* relics, uint8_t count,
                                  uint16_t card_id) noexcept;
void dispatch_relics_on_exhaust(CombatState& s, RelicSlot* relics, uint8_t count,
                                uint16_t card_id) noexcept;
void dispatch_relics_on_gained_block(CombatState& s, RelicSlot* relics,
                                     uint8_t count, int32_t amount) noexcept;
void dispatch_relics_was_hp_lost(CombatState& s, RelicSlot* relics, uint8_t count,
                                 int32_t amount) noexcept;
void dispatch_relics_on_victory(CombatState& s, RelicSlot* relics,
                                uint8_t count) noexcept;

// --- Native escape hatch -----------------------------------------------------
//
// The bespoke-relic dispatch: a switch on RelicId for relics whose hook body is not
// a static effect program (counters, conditionals, once-per-combat flags, heals).
// `slot` is the responding relic's live slot (counter mutation writes here). Relics
// marked `native: true` route here; the framework calls this instead of queuing
// steps.
void dispatch_native_relic_hook(CombatState& s, RelicHook hook, RelicId relic_id,
                                RelicSlot& slot,
                                const RelicHookContext& ctx) noexcept;

}  // namespace sts::engine
