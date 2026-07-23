#pragma once

// Power-hook framework (Stage B B3.2) -- the full hook set real content triggers,
// dispatched through the pump in the FROZEN stage-a §5.2/§5.3/§5.4/§5.5 order,
// replacing A4.3's no-op stubs. This layer answers "when a game event fires (a
// card is played, a card is exhausted, a turn ends, a debuff is applied, HP is
// lost, ...), which powers respond, in what order, and what do they queue?"
//
// A power's response is either DATA (a hook->effect-program binding in the
// generated PowerDef table, powers.hpp) or NATIVE (the escape hatch
// dispatch_native_hook below -- for the irreducibly bespoke powers: guards,
// counters, interception, redirects). The registry marks each power `native` or
// not and lists the hooks it binds (registry/powers.yaml).
//
// FROZEN ORDER (verified against the decompiled Java, read in full before coding):
//   * §5.3 card-play fan-out (GameActionManager.java:222-245): player powers ->
//     each monster's powers -> relics(acq order) -> stance -> blights -> hand
//     cards -> discard cards -> draw cards. (No S1 POWER overrides onPlayCard --
//     ArbstractPower base only; the fan-out's power stages are player+monster,
//     the relic/card stages are structural extension points that light up with
//     relics (B3.24+) and card-level hooks (curses, B3.9).)
//   * UseCardAction fan-out (UseCardAction.java:41-64) -- DIFFERENT order:
//     player powers -> player relics -> hand -> discard -> draw cards -> monster
//     powers (monsters LAST, not second). Distinct dispatch from onPlayCard.
//   * §5.4 end-of-turn (GameActionManager.callEndOfTurnActions:369-377):
//     applyEndOfTurnRelics -> applyEndOfTurnPreCardPowers (atEndOfTurnPreEndTurnCards,
//     Metallicize) -> orbs -> hand cards triggerOnEndOfTurnForPlayingCard
//     (Burn/Regret/Decay) -> stance.onEndOfTurn. atEndOfTurn (Combust) fires
//     separately via applyEndOfTurnTriggers (AbstractCreature.java:548-553).
//   * §5.5 onExhaust list order (CardGroup.moveToExhaustPile:851-856): relics
//     onExhaust -> player powers onExhaust (in power-list == application order)
//     -> card.triggerOnExhaust.
//   * APPLY_POWER interception (ApplyPowerAction.java:106-138): SOURCE powers
//     onApplyPower FIRST (Sadistic), THEN the target-side Artifact+DEBUFF nullify
//     (the debuff does NOT land; Artifact is consumed). Opposite sides of one op.
//   * wasHPLost (AbstractPlayer.damage:1445-1447): the victim's powers wasHPLost
//     fire after the HP write; Rupture's guard `info.owner == owner` (attribution)
//     fires it only for self-inflicted (card) HP loss, NOT unblocked enemy damage.
//   * onGainedBlock (AbstractCreature.addBlock:426-433): player relics
//     onPlayerGainedBlock -> (if amount>0) player powers onGainedBlock (Juggernaut).
//
// REGRESSION INVARIANT (B3.2 acceptance: "the stub removal must not shift any
// fixture"): every dispatch site is a pure no-op when no hook-bearing power is
// present. The 3 skeleton powers (Strength/Vulnerable/Weak) bind NO hooks here
// (their behaviour is the native DAMAGE-pipeline atDamageGive/Receive in
// interp.cpp, unchanged), so all 20 combat fixtures -- which use only those --
// dispatch nothing and stay byte-identical.

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // kActorPlayer (HookContext defaults)
#include "sts/engine/combat_state.hpp"  // CombatState, PowerSlot
#include "sts/engine/types.hpp"         // PowerId, CardId

namespace sts::engine {

// --- Hook identity tags -----------------------------------------------------
//
// AUTHORITATIVE mirror of the generated sts::registry::Hook (powers.hpp pins the
// two byte-equal). Values are pinned/append-only (design doc §4.4); the frozen
// dispatch ORDER lives in this module's functions, NOT in these numbers -- a Hook
// is an identity tag, not a sequence position.
enum class Hook : uint8_t {
    ON_PLAY_CARD = 0,               // §5.3 card-play fan-out
    ON_USE_CARD = 1,                // UseCardAction fan-out
    ON_EXHAUST = 2,                 // §5.5 CardGroup.moveToExhaustPile
    ON_CARD_DRAW = 3,               // per drawn card, in draw
    AT_END_OF_TURN_PRE_CARD = 4,    // §5.4 applyEndOfTurnPreCardPowers
    AT_END_OF_TURN = 5,             // applyEndOfTurnTriggers
    AT_END_OF_ROUND = 6,            // decrement-at-round-end
    AT_START_OF_TURN = 7,           // §5.2 step 6, pre-draw
    AT_START_OF_TURN_POST_DRAW = 8, // §5.2 step 6, post-draw
    ON_GAINED_BLOCK = 9,            // inside the BLOCK opcode
    ON_ATTACKED = 10,               // DAMAGE receive path
    ON_APPLY_POWER = 11,            // inside the APPLY_POWER opcode
    WAS_HP_LOST = 12,               // after an HP write (LOSE_HP / DAMAGE)
    ON_DEATH = 13,                  // actor death
};

inline constexpr int kHookCount = 14;

// --- HookContext ------------------------------------------------------------
//
// The event parameters a hook body may read. Only the fields relevant to a given
// hook are set; the rest are their value-init defaults. `owner` is filled by the
// dispatcher per-power (the power's owner actor) so a native/data body always
// knows whose power it is without threading it separately.
struct HookContext {
    uint8_t owner = kActorPlayer;        // the responding power's owner actor
    uint8_t source = kActorPlayer;       // acting actor (attacker / power applier)
    uint8_t target = kActorPlayer;       // recipient (apply-power target / card target)
    uint16_t card_id = 0;                // CardId of the card involved (play/exhaust/draw)
    uint8_t card_pool_index = 0;         // pool index of that card (use/draw redirects)
    uint16_t applied_power_id = 0;       // PowerId being applied (on_apply_power)
    int32_t amount = 0;                  // the EVENT amount: block gained / hp lost
    int32_t power_amount = 0;            // the responding power's stack amount
                                         // (PowerSlot.amount) -- drives stack-scaled
                                         // effects (a hook step's `amount: 0` sentinel
                                         // substitutes this; native bodies read it).
};

// --- Card-play fan-outs (distinct source orders) ----------------------------

// §5.3 onPlayCard fan-out. Queues, in the frozen source order, every responding
// power's / (later) relic's / card's hook effects. Called from resolve_card_play
// (card_play.cpp) as the head cardQueue item resolves.
void dispatch_on_play_card(CombatState& state, uint16_t card_id,
                           uint8_t target) noexcept;

// UseCardAction onUseCard fan-out (player powers -> relics -> hand/discard/draw
// cards -> monster powers). `played_pool_index` is the played card's pool row so
// a native body (Corruption) can redirect it (e.g. set its EXHAUST flag). Returns
// nothing; redirects mutate state directly.
void dispatch_on_use_card(CombatState& state, uint8_t played_pool_index,
                          uint16_t card_id) noexcept;

// --- Single-source hooks (player-power list order == §5.5) ------------------

// onExhaust (§5.5): relics onExhaust -> player powers onExhaust, in application
// (power-list) order. `card_id` is the exhausted card. Feel No Pain + Dark Embrace
// resolving on one exhaust is the B3.7 acceptance -- the list order here decides
// their sequence.
void dispatch_on_exhaust(CombatState& state, uint16_t card_id) noexcept;

// onCardDraw: player powers, once per drawn card. `pool_index`/`card_id` identify
// the drawn card (Corruption zeroes a drawn skill's cost).
void dispatch_on_card_draw(CombatState& state, uint8_t pool_index,
                           uint16_t card_id) noexcept;

// §5.4 pre-card end-of-turn powers (atEndOfTurnPreEndTurnCards -- Metallicize):
// player powers, before the hand-card end-of-turn triggers.
void dispatch_at_end_of_turn_pre_card(CombatState& state) noexcept;

// atEndOfTurn (Combust): player powers, via applyEndOfTurnTriggers.
void dispatch_at_end_of_turn(CombatState& state) noexcept;

// Start-of-turn player powers (§5.2 step 6): pre-draw and post-draw phases.
void dispatch_at_start_of_turn(CombatState& state) noexcept;
void dispatch_at_start_of_turn_post_draw(CombatState& state) noexcept;

// onGainedBlock (Juggernaut): fires after `actor` gains `amount` block (>0), on
// the actor's own powers (the player path; monster block gain has no S1 consumer).
void dispatch_on_gained_block(CombatState& state, uint8_t actor,
                              int32_t amount) noexcept;

// wasHPLost: the victim's powers, after an HP write of `amount` (>0). `source` is
// the actor that caused the loss (self for LOSE_HP / a card; the attacker for
// unblocked DAMAGE). Rupture's guard fires only when `source == victim`.
void dispatch_was_hp_lost(CombatState& state, uint8_t victim, uint8_t source,
                          int32_t amount) noexcept;

// --- APPLY_POWER interception (opposite sides of the opcode) -----------------

// Fire the SOURCE's onApplyPower hooks (Sadistic) BEFORE a power lands. Called
// from op_apply_power. `applied_power_id`/`applied_type_is_debuff` describe the
// power about to be applied; `target` is its recipient. (Sadistic queues damage
// on a debuffed target here.)
void dispatch_on_apply_power_source(CombatState& state, uint8_t source,
                                    uint8_t target, uint16_t applied_power_id,
                                    bool applied_is_debuff) noexcept;

// Target-side Artifact nullify (ApplyPowerAction.java:131-138): if `target` has
// Artifact AND the applied power is a DEBUFF, consume one Artifact stack and
// report that the debuff must NOT land. Returns true == the power is nullified
// (op_apply_power then skips applying it).
[[nodiscard]] bool apply_power_blocked_by_artifact(CombatState& state,
                                                   uint8_t target,
                                                   bool applied_is_debuff) noexcept;

// --- Native escape hatch -----------------------------------------------------
//
// The bespoke-power dispatch: a switch on PowerId for powers whose hook body is
// not expressible as a static effect program (guards, source/target conditions,
// per-instance counters, redirects). `ctx.owner` is the power's owner; the
// specific ctx fields carry the event. Powers marked `native: true` in the
// registry route here; the framework calls this instead of queuing steps.
void dispatch_native_hook(CombatState& state, Hook hook, PowerId power_id,
                          const HookContext& ctx) noexcept;

}  // namespace sts::engine
