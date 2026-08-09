// Fading -- native power-hook body. One translation unit per power; see
// power_native.hpp for the dispatch plumbing and power_fading.hpp for the
// after-takeTurn ordering, the triggerRelics=true suicide and the dead gold
// field.

#include "power_fading.hpp"

#include <cstdint>

#include "power_native.hpp"             // PowerNativeSig
#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActorPlayer
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_fading(CombatState& s, Hook hook,
                         const HookContext& ctx) noexcept {
    if (hook != Hook::DURING_TURN) {
        return;
    }
    // FadingPower.duringTurn (FadingPower.java:37-46):
    //
    //   if (this.amount == 1 && !this.owner.isDying) {
    //       addToBot(new VFXAction(new ExplosionSmallEffect(...), 0.1f));
    //       addToBot(new SuicideAction((AbstractMonster) this.owner));
    //   } else {
    //       addToBot(new ReducePowerAction(owner, owner, POWER_ID, 1));
    //       this.updateDescription();
    //   }
    //
    // The VFXAction is presentation and draws no seeded RNG. updateDescription
    // is a string.
    //
    // The dispatcher only reaches a monster's powers through
    // dispatch_during_turn, which the pump calls for the ACTING monster, so
    // ctx.owner is a monster slot here. Guard it anyway rather than indexing on
    // trust: a player-owned Fading is unconstructible in Acts 1-3 and this body
    // has nothing sensible to do for one.
    if (ctx.owner == kActorPlayer || ctx.owner >= kMonsterCap) {
        return;
    }
    // THE JAVA'S `else` COVERS TWO CASES, NOT ONE, and getting that wrong is the
    // easy mistake here: the death arm's condition is `amount == 1 && !isDying`,
    // so an owner at amount 1 that is ALREADY DYING falls to the reduce, not out
    // of the body. That reduce is the one call that CAN take the slot to zero and
    // reach op_reduce_power's removal path. Unobservable in practice -- the owner
    // is mid-death -- but it is what the Java does, and a `return` there would be
    // a silent second divergence hiding behind an unreachability claim.
    const bool dies_now = ctx.power_amount == 1 && s.monsters[ctx.owner].hp > 0;
    if (!dies_now) {
        // The COUNTDOWN arm. A queued reduce, not a synchronous write: the Java
        // addToBot's a ReducePowerAction, so the tick resolves through the pump
        // behind everything takeTurn queued, and the slot still reads its
        // pre-tick amount to anything that looks in between.
        ActionQueueItem reduce{};
        reduce.opcode = static_cast<uint16_t>(Opcode::REDUCE_POWER);
        reduce.src = ctx.owner;
        reduce.tgt = ctx.owner;
        reduce.amount = 1;
        // No instance key: Fading is not an `instanced` power, so the
        // first-match-by-id form is exact (interp.hpp's REDUCE_POWER note).
        reduce.flags = make_apply_power_flags(PowerId::FADING);
        add_to_bottom(s, reduce);
        return;
    }
    // The DEATH arm: amount 1 and `!this.owner.isDying`.
    ActionQueueItem suicide{};
    suicide.opcode = static_cast<uint16_t>(Opcode::SUICIDE);
    suicide.src = ctx.owner;
    suicide.tgt = ctx.owner;
    // flags bit 0 == triggerRelics, and it is SET: the 1-arg SuicideAction ctor
    // defaults it to true (SuicideAction.java:17-19). The splitting slimes pass
    // false and clear it; this is the other arm, and it is what makes a
    // Transient's fade-out run the full death edge -- the monster's die() body,
    // its powers' onDeath, the player's relics' onMonsterDeath.
    suicide.flags = 1u;
    add_to_bottom(s, suicide);
}

}  // namespace sts::engine
