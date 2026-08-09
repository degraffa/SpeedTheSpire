#pragma once

// Power-hook framework -- the full hook set real content triggers,
// dispatched through the pump in the FROZEN stage-a §5.2/§5.3/§5.4/§5.5 order,
// replacing the skeleton's no-op stubs. This layer answers "when a game event fires (a
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
//     AbstractPower base only; the fan-out's power stages are player+monster,
//     the relic stage is live and the card stages are structural extension
//     points for the card-level hooks that curses will need.)
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
// REGRESSION INVARIANT ("replacing the stubs must not shift any fixture"):
// every dispatch site is a pure no-op when no hook-bearing power is
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
    ON_POWER_REMOVED = 14,          // a power's own onRemove, at the removal
                                    // choke point (remove_slot_at)
    DURING_TURN = 15,               // applyTurnPowers, right after a monster's
                                    // takeTurn
    ON_AFTER_USE_CARD = 16,         // UseCardAction.update -- NOT the ctor
                                    // fan-out that ON_USE_CARD models
    ON_INFLICT_DAMAGE = 17,         // the ATTACKER's powers, after wasHPLost
};

inline constexpr int kHookCount = 18;

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
    int32_t power_counter = 0;           // the responding power's SECOND number
                                         // (PowerSlot.counter, types.hpp); 0 for every
                                         // power that declares no meaning for it.
    uint8_t power_slot = 0;              // the responding power's INDEX in its owner's
                                         // list. `this` in Java terms: a native body
                                         // that mutates its own slot (Panache's
                                         // countdown) or names it in a queued
                                         // instance-targeted action (The Bomb's
                                         // ReducePowerAction) needs to know WHICH slot
                                         // is speaking, which a PowerId cannot say for
                                         // an INSTANCED power. Valid only for the
                                         // duration of the dispatch call; like
                                         // damage_type above it is a transient
                                         // dispatch field, in NO serialized struct, so
                                         // it has no fixture impact.
    uint8_t damage_type = 0;             // for on_attacked / was_hp_lost /
                                         // on_inflict_damage: the incoming
                                         // DamageInfo.DamageType (interp.hpp DamageType;
                                         // 0 == NORMAL). Plated Armor's wasHPLost reads
                                         // it (no decrement on THORNS/HP_LOSS). Transient
                                         // dispatch field -- NOT part of any serialized
                                         // struct, so no fixture impact.
    bool source_null = false;            // for on_attacked / was_hp_lost /
                                         // on_inflict_damage: the incoming
                                         // DamageInfo had a NULL owner (interp.hpp
                                         // kDamageNullSource -- Explosive Potion's
                                         // matrix). `source` still reads the queue
                                         // slot's src byte (there is no null actor
                                         // encoding), so a body's Java-side
                                         // `info.owner != null` gate tests THIS field
                                         // (CurlUpPower.java:38 and siblings).
                                         // Transient dispatch field -- NOT part of any
                                         // serialized struct, so no fixture impact.
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
// `target` is the monster the card was played at (kActorPlayer for a self/none
// card) -- the `action.target` a replay power reads to aim its copy
// (DoubleTapPower.java:46-49).
// `energy_on_use` is the played card's AbstractCard.energyOnUse at fan-out
// time -- non-zero only for an X-cost play (resolve_card_play's hoisted
// derivation, WITHOUT Chemical X's repetition boost). Necronomicon's
// `cost == -1 && energyOnUse >= 2` arm (Necronomicon.java:62) is its reader.
void dispatch_on_use_card(CombatState& state, uint8_t played_pool_index,
                          uint16_t card_id,
                          uint8_t target = kActorPlayer,
                          int32_t energy_on_use = 0) noexcept;

// --- Single-source hooks (player-power list order == §5.5) ------------------

// onExhaust (§5.5): relics onExhaust -> player powers onExhaust, in application
// (power-list) order -> card.triggerOnExhaust (the exhausted card's own
// on_exhaust program -- Sentinel's addToTop energy; CardGroup.moveToExhaustPile:
// 851-857). `pool_index`/`card_id` identify the exhausted instance (the pool row
// selects the base vs upgraded on-exhaust program). Feel No Pain + Dark Embrace
// resolving on ONE exhaust is the case that pins this -- the list order here
// decides their sequence.
void dispatch_on_exhaust(CombatState& state, uint8_t pool_index,
                         uint16_t card_id) noexcept;

// onCardDraw: player powers, once per drawn card. `pool_index`/`card_id` identify
// the drawn card (Corruption zeroes a drawn skill's cost).
void dispatch_on_card_draw(CombatState& state, uint8_t pool_index,
                           uint16_t card_id) noexcept;

// §5.4 pre-card end-of-turn powers (atEndOfTurnPreEndTurnCards -- Metallicize):
// player powers, before the hand-card end-of-turn triggers.
void dispatch_at_end_of_turn_pre_card(CombatState& state) noexcept;

// atEndOfTurn (Combust): player powers, via applyEndOfTurnTriggers.
void dispatch_at_end_of_turn(CombatState& state) noexcept;

// atEndOfRound (MonsterGroup.applyEndOfTurnPowers, MonsterGroup.java:290-304):
// fires once per round when the player's next turn begins (GameActionManager.java:
// 329-332). Order: each LIVE monster's applyEndOfTurnTriggers (monster powers'
// atEndOfTurn) -> player powers atEndOfRound -> each LIVE monster's powers
// atEndOfRound. The monster-Ritual Strength ramp (Cultist) fires in the last step.
// No-op unless a monster/player holds a power binding these hooks, so the
// jaw-worm-only fixtures are unchanged.
void dispatch_at_end_of_round(CombatState& state) noexcept;

// Start-of-turn player powers (§5.2 step 6): pre-draw and post-draw phases.
void dispatch_at_start_of_turn(CombatState& state) noexcept;
void dispatch_at_start_of_turn_post_draw(CombatState& state) noexcept;

// atStartOfTurn for ONE monster's powers -- the second half of
// MonsterGroup.applyPreTurnLogic (MonsterGroup.java:98-105), which calls
// m.applyStartOfTurnPowers() (AbstractCreature.java:529-533) immediately after
// the Barricade-gated loseBlock(). Kept separate from dispatch_at_start_of_turn
// because the two fire a whole monster phase apart: the player's is
// GameActionManager's step 6, the monster's is MonsterStartTurnAction. The caller
// (action_queue.cpp's apply_pre_turn_logic) owns the isDying/isEscaping filter.
void dispatch_monster_at_start_of_turn(CombatState& state,
                                       uint8_t monster_index) noexcept;

// onGainedBlock (Juggernaut): fires after `actor` gains `amount` block (>0), on
// the actor's own powers (the player path; monster block gain has no S1 consumer).
void dispatch_on_gained_block(CombatState& state, uint8_t actor,
                              int32_t amount) noexcept;

// onAttacked (AbstractPlayer.damage:1425-1426 / AbstractMonster.damage:667): the
// VICTIM's powers fire when `victim` takes a NORMAL attack from a DISTINCT
// `attacker`, AFTER block absorption and REGARDLESS of whether damage penetrated
// (`amount` is the post-block damage). Thorns reflects THORNS damage to the
// attacker here. op_damage dispatches this only for NORMAL src != tgt damage
// (THORNS/HP_LOSS never trigger it -- no thorns-vs-thorns loop), but it DOES
// dispatch a null-source NORMAL hit (Explosive Potion's matrix): the game's
// loop runs unconditionally and each body's `info.owner != null` gate is what
// fails, via `source_null` (-> HookContext::source_null). No-op unless a power
// binds ON_ATTACKED.
void dispatch_on_attacked(CombatState& state, uint8_t victim, uint8_t attacker,
                          int32_t amount, bool source_null = false) noexcept;

// --- The other half of onAttacked (S2.26) ------------------------------------
//
// THE JAVA HAS ONE UNCONDITIONAL LOOP; THIS ENGINE HAS TWO GATED ONES, AND THIS
// COMMENT IS WHY THAT IS EQUIVALENT RATHER THAN A SHORTCUT.
//
// AbstractMonster.damage / AbstractPlayer.damage run
//
//     for (AbstractPower p : this.powers) damageAmount = p.onAttacked(info, damageAmount);
//
// with NO type test and NO owner test and NO `info.owner != this` test
// (AbstractMonster.java:665-667). Every guard lives in the individual bodies.
// dispatch_on_attacked above hoists the common guards to the CALL SITE, which
// was correct while every binder had them: Thorns, Sharp Hide, Angry, Flight,
// Malleable and (this batch's) Reactive each spell `info.type != THORNS &&
// != HP_LOSS` -- or the equivalent `== NORMAL` -- plus `info.owner != null`, so
// firing only for NORMAL src != tgt damage produced the same answers with less
// work.
//
// ShiftingPower (powers.yaml 104) is the first binder with NEITHER guard. Its
// whole condition is `damageAmount > 0` (ShiftingPower.java:33), so in the real
// game a THORNS reflect (Flame Barrier, a Thorns tick, a Constricted tick) or an
// HP_LOSS onto a Transient DOES swing its Strength. Under the hoisted gate alone
// it would not -- a reachable, playable divergence.
//
// The fix is NOT to widen dispatch_on_attacked. Widening it would push every
// existing binder onto a call path it currently gets excluded from for free,
// changing six landed powers to fix one, and each would then need the guard
// re-added by hand -- six chances to get it wrong, in a batch that is not the
// owner of any of them. Instead this function walks the COMPLEMENT of the gate
// (non-NORMAL damage, and self-sourced damage) and admits ONLY the powers that
// declare no guard of their own. The union of the two walks is exactly the
// Java's single loop.
//
// THE ADMITTED SET IS A CLOSED, ENUMERATED LIST -- power_is_on_attacked_type_
// tolerant below -- and it is native code rather than a registry column
// deliberately: it is a claim about what a BODY does not check, which only the
// body's author can make, and a new hook value would be a scarce id spent on a
// one-member set. When a second member arrives, the list grows and this comment
// is the checklist for admitting it.
//
// No-op unless the victim holds an admitted power, so every landed fixture and
// every corpus replay is byte-identical.
void dispatch_on_attacked_type_tolerant(CombatState& state, uint8_t victim,
                                        uint8_t attacker, int32_t amount,
                                        uint8_t damage_type,
                                        bool source_null = false) noexcept;

// Does `id`'s onAttacked body declare NO damage-type guard and NO
// `info.owner != null` guard, so that the Java would fire it on damage the
// NORMAL-only dispatch gate excludes?
//
// Checked against every landed ON_ATTACKED binder, one at a time -- this is the
// enumeration the comment above promises, and the reason each is OUT is the
// line of Java that guards it:
//
//   THORNS       ThornsPower.java:44        `info.type != THORNS`, `owner != null`
//   NEXT_TURN_BLOCK / the one-shot guard    `info.type == NORMAL`
//   SHARP_HIDE   SharpHidePower.java:47     `info.type == NORMAL`, `owner != null`
//   ANGRY        AngryPower.java:35         `!= HP_LOSS && != THORNS`, `owner != null`
//   FLIGHT       FlightPower.java:67        `!= HP_LOSS && != THORNS`, `owner != null`
//   MALLEABLE    MalleablePower.java:63     `== NORMAL`, `owner != null`
//   REACTIVE     ReactivePower.java:40      `!= HP_LOSS && != THORNS`, `owner != null`
//   SHIFTING     ShiftingPower.java:33      -- NOTHING. The only member.
[[nodiscard]] constexpr bool power_is_on_attacked_type_tolerant(
    PowerId id) noexcept {
    return id == PowerId::SHIFTING;
}

// wasHPLost: the victim's powers, after an HP write of `amount` (>0). `source` is
// the actor that caused the loss (self for LOSE_HP / a card; the attacker for
// unblocked DAMAGE). `damage_type` is the incoming DamageInfo.DamageType (interp.hpp
// DamageType; 0 == NORMAL, default). Rupture's guard fires only when `source ==
// victim`; Plated Armor's fires only for a NORMAL attack from a distinct attacker
// with a NON-NULL owner (PlatedArmorPower.java:58), which is what `source_null`
// carries (-> HookContext::source_null).
void dispatch_was_hp_lost(CombatState& state, uint8_t victim, uint8_t source,
                          int32_t amount, uint8_t damage_type = 0,
                          bool source_null = false) noexcept;

// onDeath (AbstractMonster.die: `if (currentHealth <= 0 && triggerRelics) for
// (AbstractPower p : this.powers) p.onDeath();`, AbstractMonster.java:928-932):
// the DYING actor's own powers fire, in power-list == application order, at the
// moment its HP reaches 0 -- BEFORE the relics' onMonsterDeath fan-out die() runs
// immediately after (:933-937). Dispatched from the monster-death edge of
// op_damage / op_lose_hp (interp_damage.cpp), the same edge
// dispatch_relics_on_monster_death already fires from.
//
// TWO THINGS THE JAVA PINS HERE. `isDying` is set BEFORE the power walk (:927),
// so a body that asks "is the battle ending?" sees this monster as already
// counted -- which is what makes the LAST Fungi Beast's Spore Cloud release
// nothing while an earlier one's releases Vulnerable. And the walk is gated on
// `triggerRelics`, which SuicideAction passes as false for a splitting slime
// (SuicideAction.java:29-36), so the SUICIDE opcode deliberately does NOT
// dispatch this.
//
// No-op unless a power on the dying actor binds ON_DEATH.
void dispatch_on_death(CombatState& state, uint8_t actor) noexcept;

// onRemove (AbstractPower.onRemove, AbstractPower.java:186-188 -- an empty base
// every override extends): fires on the power being DESTROYED, and on nothing
// else. Dispatched from remove_slot_at (interp/interp_powers.cpp), which is the
// single choke point every destruction reaches -- REMOVE_POWER (op_remove_power),
// REDUCE_POWER's fall-to-zero (op_reduce_power) and REMOVE_DEBUFFS (which expands
// into REMOVE_POWER items). The Java has no such choke point: every caller ends at
// AbstractCreature.removePower / RemoveSpecificPowerAction.update
// (RemoveSpecificPowerAction.java:29-40), and BOTH call p.onRemove() before the
// list drops the object, which is the property reproduced here.
//
// UNLIKE EVERY OTHER HOOK ON THIS PAGE, THIS ONE IS NOT A FAN-OUT. It fires
// exactly one body -- the REMOVED power's own -- because `onRemove` is a
// self-notification, not an event other powers observe. So it takes the slot
// being destroyed rather than an actor, and routes straight to that PowerId's
// native handler.
//
// FIRES BEFORE THE SLOT CLEARS, which is load-bearing in both directions: the
// body can still read its own {amount, counter} (Flight's stored amount), and a
// body that queues an action naming its owner is queuing while the list still has
// the shape the game's did. It must NOT mutate the list -- remove_slot_at is
// mid-compaction -- so a body that wants to remove something else queues a
// REMOVE_POWER item instead of touching slots directly.
//
// No-op unless the removed power is `native` AND lists on_power_removed. Every
// S1 power that reaches remove_slot_at today binds nothing here, so all landed
// fixtures dispatch nothing and stay byte-identical.
void dispatch_on_power_removed(CombatState& state, uint8_t owner,
                               const PowerSlot& slot,
                               uint8_t slot_index) noexcept;

// --- The three S2.2F Act-2/3 framework hooks ---------------------------------
//
// All three land as DISPATCH SITES with no binder. That is the point of the
// task: four monster batches run in parallel behind this, and each needs one of
// these fired from a place only the framework can decide. A hook with no binder
// is a genuine no-op -- dispatch_actor_powers skips any power with no binding
// for it -- so every landed fixture is byte-identical.

// duringTurn (AbstractCreature.applyTurnPowers, :535-539), invoked from
// GameActionManager.java:322-323 as `m.takeTurn(); m.applyTurnPowers();`.
// SYNCHRONOUS and immediately after the monster's turn body, so anything a body
// queues here lands BEHIND everything takeTurn queued -- the trailing
// RollMoveAction included. Walks the ACTING monster's own powers, in slot order;
// no other actor participates.
//
// The two binders coming are ExplosivePower (the Exploder's 3-turn countdown,
// then SuicideAction + 30 THORNS to the player) and FadingPower (the
// Transient's). Both are countdown-then-self-destruct, which is why the "after
// takeTurn" half matters: the monster attacks on the turn it dies.
//
// No-op unless a power on the acting monster binds DURING_TURN.
void dispatch_during_turn(CombatState& state, uint8_t monster_index) noexcept;

// onAfterUseCard (UseCardAction.update, UseCardAction.java:79-88).
//
// DISTINCT FROM ON_USE_CARD (1) IN BOTH RESPECTS, and conflating them is the
// mistake this comment exists to prevent. ON_USE_CARD is the UseCardAction
// CONSTRUCTOR fan-out (:20-45) -- it runs BEFORE the played card's own actions
// are queued, and it walks player powers -> player relics -> hand/discard/draw
// cards -> monster powers. This one runs from update(), AFTER the card's program
// has resolved, and it walks PLAYER POWERS then MONSTER POWERS only: no relics,
// no card-level stages. A counter bound to the wrong one counts at the wrong
// moment and in the wrong order.
//
// Binders coming: SlowPower (Giant Head -- one stack per card played) and
// TimeWarpPower (Time Eater -- the 12th card ends the player's turn).
//
// No-op unless a power binds ON_AFTER_USE_CARD.
void dispatch_on_after_use_card(CombatState& state, uint8_t played_pool_index,
                                uint16_t card_id) noexcept;

// onInflictDamage (AbstractPower.onInflictDamage), fired from
// AbstractPlayer.damage (:1449-1453) over `info.owner.powers` -- the ATTACKER's
// list, which is what separates it from WAS_HP_LOST (12), whose walk is the
// VICTIM's. Folding one into the other would use the wrong power list and lose
// the ordering guarantee below.
//
// POSITION IS LOAD-BEARING. It sits inside the `damageAmount > 0` block, after
// the victim's powers' and relics' wasHPLost and before the currentHealth write:
//     powers->onLoseHp, relics->onLoseHp,
//     powers->wasHPLost, relics->wasHPLost,
//     info.owner.powers->onInflictDamage,      <-- here
//     currentHealth -= damageAmount
// This engine writes HP before dispatching wasHPLost (it follows AbstractCreature
// ordering), so the equivalent position is immediately after dispatch_was_hp_lost.
//
// `amount` is the Java's `damageAmount` -- the post-block, post-relic-modifier
// damage, NOT the clamped HP delta wasHPLost uses. Player-victim only: the Java
// has this call in AbstractPlayer.damage and nowhere else.
//
// Binder coming: PainfulStabsPower (Book of Stabbing -- one Wound per hit).
//
// No-op unless a power on the attacker binds ON_INFLICT_DAMAGE.
void dispatch_on_inflict_damage(CombatState& state, uint8_t attacker,
                                uint8_t victim, int32_t amount,
                                uint8_t damage_type = 0,
                                bool source_null = false) noexcept;

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
