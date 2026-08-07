// The Bomb -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_the_bomb.hpp for what this power does.

#include "power_the_bomb.hpp"

#include <cstdint>

#include "power_native.hpp"             // PowerListView / actor_power_list
#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_damage_flags, instance key
#include "sts/engine/types.hpp"

namespace sts::engine {

// TheBombPower (TheBombPower.java:20-54), read in full.
//
// INSTANCED (registry `instanced: true`): the ctor builds its ID as the literal
// "TheBomb" plus an ever-increasing static offset (:31-32), so
// AbstractCreature.getPower never matches an existing bomb and ApplyPowerAction
// adds a brand-new AbstractPower every time. Two bombs are two independent
// fuses, never a stack of 2 -- which is exactly what op_apply_power's `instanced`
// append path reproduces.
//
// TWO NUMBERS: `amount` is the fuse in turns (3 at construction, TheBomb.java:32)
// and `counter` is the private `damage` (:26,35 -- 40 base / 50 upgraded,
// TheBomb.java:27,44).
//
// atEndOfTurn(isPlayer) (:40-48) in full:
//     if (!getMonsters().areMonstersBasicallyDead()) {
//         addToBot(new ReducePowerAction(owner, owner, this, 1));
//         if (this.amount == 1)
//             addToBot(new DamageAllEnemiesAction(null,
//                 createDamageMatrix(damage, true), THORNS, FIRE));
//     }
// Note THREE things this pins.
//
// (1) The guard is checked ONCE and covers BOTH queued actions: with every
//     monster dying or escaped the bomb does not tick at all -- its fuse is
//     FROZEN, not merely silent.
//
// (2) The explosion test reads `amount` AT HOOK TIME, i.e. BEFORE its own queued
//     reduce resolves. So a bomb standing at 1 queues [reduce, damage] and the
//     reduce -- 1 from 1 -- takes the instance to zero, which ReducePowerAction
//     turns into a RemoveSpecificPowerAction on that exact instance (:45-51).
//     The bomb is therefore GONE in the same end-of-turn it detonates. Counting
//     from the turn it was played: 3 -> 2, 2 -> 1, then the hook sees 1 and it
//     both reduces (removing) and explodes -- the end of the THIRD turn.
//
// (3) The player-side sweep this fires from is applyEndOfTurnTriggers, which runs
//     at the TOP of endTurn -- BEFORE the hand is discarded (AbstractCreature.
//     java:547-553 via AbstractRoom.java:393-396), which is where the engine's
//     AT_END_OF_TURN dispatch already sits.
void power_native_the_bomb(CombatState& s, Hook hook,
                           const HookContext& ctx) noexcept {
    if (hook != Hook::AT_END_OF_TURN) {
        return;
    }
    // !areMonstersBasicallyDead (MonsterGroup.java:90-95): every monster
    // isDying || isEscaping. monster_dead_or_escaped is that predicate
    // (combat_state.hpp) -- an ESCAPED Looter counts as gone even at full HP.
    bool any_in_fight = false;
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        if (!monster_basically_dead(s.monsters[i])) {
            any_in_fight = true;
            break;
        }
    }
    if (!any_in_fight) {
        return;
    }
    const PowerListView pv = actor_power_list(s, ctx.owner);
    if (ctx.power_slot >= pv.count) {
        return;  // defensive: the dispatcher's index must be live
    }
    const PowerSlot& slot = pv.slots[ctx.power_slot];

    // addToBot ReducePowerAction(owner, owner, THIS INSTANCE, 1) (:43). The
    // instance key is the slot's own {amount, counter} captured NOW -- see
    // interp.hpp for why an index cannot be the handle once a sibling bomb's
    // removal compacts the list ahead of this item.
    ActionQueueItem reduce{};
    reduce.opcode = static_cast<uint16_t>(Opcode::REDUCE_POWER);
    reduce.src = ctx.owner;
    reduce.tgt = ctx.owner;
    reduce.amount = 1;
    reduce.flags = make_power_instance_flags(PowerId::THE_BOMB, slot.amount,
                                             slot.counter);
    add_to_bottom(s, reduce);

    if (slot.amount != 1) {
        return;
    }
    // addToBot DamageAllEnemiesAction(null, createDamageMatrix(damage, true),
    // THORNS, FIRE) (:45). isPureDamage == true builds the matrix WITHOUT
    // applyPowers (DamageInfo.java:126-136) and THORNS independently skips every
    // NORMAL-only power hook, so neither the player's Strength nor a target's
    // Vulnerable/Weak moves this number. The Java source is `null`; src only
    // selects an ownership branch this type never runs and gates the
    // NORMAL-only onAttacked fan-out, so the owner lane is equivalent here.
    ActionQueueItem dmg{};
    dmg.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    dmg.src = ctx.owner;
    dmg.tgt = (ctx.owner == kActorPlayer) ? kActorAllEnemies : kActorPlayer;
    dmg.amount = slot.counter;
    dmg.flags = make_damage_flags(DamageType::THORNS);
    add_to_bottom(s, dmg);
}

}  // namespace sts::engine
