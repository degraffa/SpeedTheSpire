// Explosive -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and power_explosive.hpp for
// what this power does.

#include "power_explosive.hpp"

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActorPlayer
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags, make_damage_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

// ExplosivePower.duringTurn (ExplosivePower.java:45-57), read in full:
//
//     if (this.amount == 1 && !this.owner.isDying) {
//         addToBot(new VFXAction(new ExplosionSmallEffect(...), 0.1f));
//         addToBot(new SuicideAction((AbstractMonster) this.owner));
//         DamageInfo di = new DamageInfo(this.owner, 30, DamageType.THORNS);
//         addToBot(new DamageAction(AbstractDungeon.player, di, FIRE, true));
//     } else {
//         addToBot(new ReducePowerAction(owner, owner, POWER_ID, 1));
//         this.updateDescription();
//     }
//
// FIVE things that pins, in the order a reader meets them.
//
// (1) WHERE THIS FIRES FROM. duringTurn's only call site in the whole game is
//     AbstractCreature.applyTurnPowers (:535-539), invoked from
//     GameActionManager.java:322-323 as `m.takeTurn(); m.applyTurnPowers();` --
//     SYNCHRONOUSLY, at queue time. So everything below is appended BEHIND
//     whatever takeTurn queued, including the Exploder's own trailing
//     RollMoveAction. The monster attacks on the turn it self-destructs. That
//     call site is Hook::DURING_TURN (S2.2F), dispatched from the pump.
//
// (2) THE isDying GUARD. An Exploder already killed this turn does NOT explode --
//     no Suicide and, more to the point, no 30 damage. This engine models isDying
//     as hp <= 0 (die() and SuicideAction both zero HP), which is what the guard
//     below tests. Note the guard is on the DETONATION arm only: a dying owner
//     with a fuse above 1 still takes the else arm and queues a reduce, which is
//     harmless and is reproduced rather than short-circuited.
//
// (3) THE ORDER INSIDE THE ARM IS LOAD-BEARING. Suicide resolves BEFORE the
//     damage, so the player takes the blast from an already-dead monster. The
//     VFXAction between them is presentation and draws nothing.
//
// (4) SuicideAction's 1-ARG CTOR DEFAULTS triggerRelics TO TRUE
//     (SuicideAction.java:17-19) -- the opposite of the large-slime split, which
//     passes false. So the power onDeath and relic onMonsterDeath fan-outs DO
//     run for an exploding Exploder (Spore Cloud, Gremlin Horn). That is `flags`
//     bit 0 on the SUICIDE item.
//
// (5) THE DAMAGE IS THORNS AND PURE. The DamageAction's 4th argument is
//     isMultiDamage/isPure `true` and the type is THORNS, so neither the
//     Exploder's Strength (it has none, but the Orb Walker's ally power could
//     have given it one in principle) nor the player's Vulnerable moves the 30.
//     Block still absorbs it.
void power_native_explosive(CombatState& s, Hook hook,
                            const HookContext& ctx) noexcept {
    if (hook != Hook::DURING_TURN) {
        return;
    }
    const bool owner_is_dying =
        ctx.owner < kMonsterCap && s.monsters[ctx.owner].hp <= 0;
    if (ctx.power_amount != 1 || owner_is_dying) {
        // addToBot ReducePowerAction(owner, owner, POWER_ID, 1) (:55). Not
        // instanced, so the plain PowerId form of REDUCE_POWER is the right
        // handle; a reduce of 1 from 1 removes the slot, which is unreachable
        // here (amount 1 takes the other arm unless the owner is dying).
        ActionQueueItem reduce{};
        reduce.opcode = static_cast<uint16_t>(Opcode::REDUCE_POWER);
        reduce.src = ctx.owner;
        reduce.tgt = ctx.owner;
        reduce.amount = 1;
        reduce.flags = make_apply_power_flags(PowerId::EXPLOSIVE);
        add_to_bottom(s, reduce);
        return;
    }

    ActionQueueItem suicide{};
    suicide.opcode = static_cast<uint16_t>(Opcode::SUICIDE);
    suicide.src = ctx.owner;
    suicide.tgt = ctx.owner;
    suicide.flags = 1u;  // SuicideAction(m): triggerRelics defaults to TRUE
    add_to_bottom(s, suicide);

    ActionQueueItem dmg{};
    dmg.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    dmg.src = ctx.owner;  // `new DamageInfo(this.owner, 30, THORNS)`
    dmg.tgt = kActorPlayer;
    dmg.amount = kExplosiveBlastDamage;
    dmg.flags = make_damage_flags(DamageType::THORNS);
    add_to_bottom(s, dmg);
}

}  // namespace sts::engine
