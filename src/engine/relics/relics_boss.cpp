// BOSS-tier relics -- native hook bodies. Parameters a body does not read are
// left unnamed to keep -Wextra quiet; the signature is the uniform RelicNativeFn.
//
// Provenance for each relic is on its registry row; the per-body comments here
// cite the exact Java lines the body mirrors.

#include "relics_boss.hpp"

#include <cstdint>

#include "../interp/interp_powers.hpp"  // op_apply_power (Philosopher's Stone)
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags, CardPile
#include "sts/engine/monster_snecko.hpp"  // kConfusionAppliedAmount (shared with GLARE)
#include "sts/engine/run_state.hpp"     // RelicSlot
#include "sts/engine/types.hpp"

namespace sts::engine {

void relic_native_velvet_choker(CombatState& /*s*/, RelicHook hook,
                                RelicSlot& slot,
                                const RelicHookContext& /*ctx*/) noexcept {
    // VelvetChoker (VelvetChoker.java:57-89):
    //   atBattleStart / atTurnStart   counter = 0
    //   onPlayCard                    if (counter < 6) { ++counter; ... }
    //   canPlay                       false once counter >= 6   [legal_actions]
    //   onVictory                     counter = -1
    //
    // The `< 6` guard on the increment is not cosmetic: it CLAMPS the counter at
    // exactly 6 rather than letting it run on, which is why -1 (out of combat)
    // and 6 (spent) are the only two states the veto ever sees. The veto itself
    // lives in legal_actions (advance.cpp) because it is AbstractCard.canUse's
    // relic fan-out (AbstractCard.java:876-879), not a queued effect.
    switch (hook) {
        case RelicHook::AT_BATTLE_START:
        case RelicHook::AT_TURN_START:
            slot.counter = 0;
            break;
        case RelicHook::ON_PLAY_CARD:
            if (slot.counter < kVelvetChokerPlayLimit) {
                ++slot.counter;
            }
            break;
        case RelicHook::ON_VICTORY:
            slot.counter = -1;  // VelvetChoker.java:86-89
            break;
        default:
            break;
    }
}

void relic_native_snecko_eye(CombatState& s, RelicHook hook,
                             RelicSlot& /*slot*/,
                             const RelicHookContext& /*ctx*/) noexcept {
    // SneckoEye.atPreBattle (SneckoEye.java:39-43): addToBot ApplyPowerAction(
    // player, player, new ConfusionPower(player)).
    //
    // The HOOK is the whole point. atPreBattle is fired by
    // AbstractPlayer.applyPreCombatLogic (AbstractPlayer.java:1885-1890), which
    // is the LAST line of preBattlePrep (:1607) -- before the turn-1 block queues
    // its opening DrawCardAction (AbstractRoom.java:236-258). So Confusion is on
    // the player before a single card is drawn, and the very first hand is
    // cost-randomised. Binding this to atBattleStart instead would put it AFTER
    // the opening draw and silently spare the first hand.
    //
    // THE APPLIED AMOUNT IS -1, NOT 1. This comment used to read "so the stack
    // amount is the 1 an ApplyPowerAction without an explicit amount uses", and
    // that claim about the Java was simply false; the code matched the comment,
    // so the engine wrote 1. The actual chain:
    //
    //   * ConfusionPower's ctor takes only the owner and assigns no amount
    //     (ConfusionPower.java:23-31), so the object carries AbstractPower's
    //     field initializer `public int amount = -1` (AbstractPower.java:65).
    //   * SneckoEye.atPreBattle uses the 3-ARG ApplyPowerAction (SneckoEye.java:
    //     42), whose ctor forwards `powerToApply.amount` as the stack amount
    //     (ApplyPowerAction.java:80-82) -- i.e. -1. There is no "default 1"
    //     form: the 3-arg ctor IS the no-explicit-amount form, and it reads the
    //     power's own field.
    //   * On a NEW slot the game adds the POWER OBJECT itself
    //     (ApplyPowerAction.update:164-166; AbstractCreature.addPower:506-513 on
    //     the direct path), so -1 is what the slot holds and what
    //     CommunicationMod reports.
    //
    // Verified against a live capture rather than reasoned alone, per the S2.22
    // adjudication procedure: tests/golden/oracle_corpus/act1_a20_50 carries
    // `{"amount": -1, "name": "Confusion", "id": "Confusion"}` on the player's
    // power list. The powers.yaml row's `stack: none` was right for the wrong
    // reason and stays; the MECHANISM is AbstractPower.stackPower's
    // `amount == -1` early return (:152-158), which op_apply_power now
    // reproduces -- reachable, because the Snecko's GLARE can land Confusion on
    // a player who already carries this relic's.
    //
    // The amount is behaviourally inert (nothing reads it; power_confusion.cpp's
    // onCardDraw body ignores it), so this changes no simulated outcome -- but it
    // is ORACLE-VISIBLE, which is the whole reason to be right about it.
    if (hook != RelicHook::AT_PRE_BATTLE) {
        return;
    }
    ActionQueueItem p{};
    p.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    p.src = kActorPlayer;
    p.tgt = kActorPlayer;
    p.amount = kConfusionAppliedAmount;
    p.flags = make_apply_power_flags(PowerId::CONFUSION);
    add_to_bottom(s, p);  // addToBot (SneckoEye.java:42)
}

void relic_native_philosophers_stone(CombatState& s, RelicHook hook,
                                     RelicSlot& /*slot*/,
                                     const RelicHookContext& /*ctx*/) noexcept {
    // PhilosopherStone.atBattleStart (PhilosopherStone.java:41-48): for every
    // monster, addToTop RelicAboveCreatureAction (cosmetic) and then
    //     m.addPower(new StrengthPower(m, 1));
    // -- a DIRECT AbstractCreature.addPower, NOT an ApplyPowerAction. It is
    // synchronous: the Strength is on every monster before anything else queued
    // at battle start resolves, which is why this body applies rather than queues.
    //
    // op_apply_power is reused rather than re-implementing the slot append/stack.
    // Its interception chain is provably inert for THIS call shape, which is what
    // makes the reuse faithful rather than merely convenient:
    //   * source hooks       src == tgt == the monster; the only S1 source-side
    //                        onApplyPower is Sadistic, a player power, and the
    //                        fan-out is over the SOURCE's powers.
    //   * Champion Belt      requires the applied power to be Vulnerable.
    //   * Ginger / Turnip    require tgt == the player.
    //   * Artifact nullify   requires a DEBUFF; Strength(+1) is a BUFF (the
    //                        negative-amount flip needs amount <= 0).
    // Passing the monster as BOTH src and tgt mirrors `new StrengthPower(m, 1)`,
    // whose owner is m.
    if (hook != RelicHook::AT_BATTLE_START) {
        return;
    }
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        if (s.monsters[i].monster_id == static_cast<uint16_t>(MonsterId::NONE)) {
            continue;
        }
        op_apply_power(s, i, i, PowerId::STRENGTH, 1);
    }
}

void relic_native_black_blood(CombatState& s, RelicHook hook,
                              RelicSlot& /*slot*/,
                              const RelicHookContext& /*ctx*/) noexcept {
    // BlackBlood.onVictory (BlackBlood.java:24-31): addToTop
    // RelicAboveCreatureAction, then `if (p.currentHealth > 0) p.heal(12)`.
    // Burning Blood's onVictory (heal 6) is the same shape WITHOUT the guard --
    // BurningBlood.java:30-33 heals unconditionally -- so the `> 0` test is
    // reproduced here rather than hoisted into the shared seam.
    // Routed through heal_player_with_relics so Magic Flower's x1.5 applies
    // exactly where it applies for Burning Blood.
    if (hook == RelicHook::ON_VICTORY && s.player_hp > 0) {
        heal_player_with_relics(s, 12);
    }
}

void relic_native_mark_of_pain(CombatState& s, RelicHook hook,
                               RelicSlot& /*slot*/,
                               const RelicHookContext& /*ctx*/) noexcept {
    // MarkOfPain.atBattleStart (MarkOfPain.java:26-31): addToBot
    // RelicAboveCreatureAction, then addToBot
    //     MakeTempCardInDrawPileAction(new Wound(), 2, true, true)
    // -- amount 2, randomSpot true, autoPosition true. randomSpot routes each
    // copy through CardGroup.addToRandomSpot, i.e. ONE cardRandomRng draw per
    // copy (and none into an empty pile), which is exactly what the MAKE_CARD
    // opcode's DRAW_RANDOM destination does (interp/interp_cards.cpp).
    //
    // Native rather than a DATA hook only because a relic effect step carries no
    // `card:` / `pile:` fields -- the step shape is {op, target, amount, power}.
    // The queue END is addToBot, matching the Java.
    if (hook != RelicHook::AT_BATTLE_START) {
        return;
    }
    ActionQueueItem w{};
    w.opcode = static_cast<uint16_t>(Opcode::MAKE_CARD);
    w.src = static_cast<uint8_t>(CardPile::DRAW_RANDOM);
    w.tgt = kActorPlayer;  // unused by MAKE_CARD, but must not read as a fan-out
    w.amount = 2;
    w.flags = make_make_card_flags(static_cast<uint16_t>(CardId::WOUND));
    add_to_bottom(s, w);
}

void relic_native_runic_cube(CombatState& s, RelicHook hook,
                             RelicSlot& /*slot*/,
                             const RelicHookContext& ctx) noexcept {
    // RunicCube.wasHPLost (RunicCube.java:24-31): if the room phase is COMBAT and
    // damageAmount > 0, addToTop DrawCardAction(player, 1) then addToTop
    // RelicAboveCreatureAction. The phase test is satisfied by construction here
    // -- the relic wasHPLost fan-out only runs inside a combat -- but the
    // `> 0` test is not vacuous: it is also enforced one level up, in
    // dispatch_relics_was_hp_lost (relic_hooks.cpp), and is repeated here so the
    // body is correct on its own terms rather than by a caller's courtesy.
    if (hook != RelicHook::WAS_HP_LOST || ctx.amount <= 0) {
        return;
    }
    ActionQueueItem d{};
    d.opcode = static_cast<uint16_t>(Opcode::DRAW);
    d.src = kActorPlayer;
    d.tgt = kActorPlayer;
    d.amount = 1;
    add_to_top(s, d);  // addToTop (RunicCube.java:28)
}

}  // namespace sts::engine
