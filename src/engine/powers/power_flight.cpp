// Flight -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and power_flight.hpp for what
// this power does and why storedAmount lives in PowerSlot.counter.

#include "power_flight.hpp"

#include <cstdint>
#include "power_native.hpp"                 // PowerNativeSig, actor_power_list
#include "sts/engine/action_queue.hpp"      // add_to_bottom / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_byrd.hpp"      // clear_byrd_flying (the other half of GROUNDED)
#include "sts/engine/monster_dispatch.hpp"  // MonsterIntent (the GROUNDED telegraph)
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"   // kByrdMoveStunned

namespace sts::engine {

void power_native_flight(CombatState& s, Hook hook,
                         const HookContext& ctx) noexcept {
    if (hook == Hook::AT_START_OF_TURN) {
        // FlightPower.atStartOfTurn (FlightPower.java:47-51):
        //     this.amount = this.storedAmount;
        // A FULL REFRESH, not a decay -- every stack the owner shed to the
        // player's attacks last turn is back. Dispatched for a MONSTER owner from
        // apply_pre_turn_logic (src/engine/action_queue.cpp), of which this is the
        // first binder. Synchronous, exactly as the Java's field write is.
        //
        // Written through ctx.power_slot rather than a find-by-id, so this is the
        // slot that is speaking even though Flight is not an instanced power --
        // the dispatcher guarantees the index is valid for the call.
        const PowerListView pv = actor_power_list(s, ctx.owner);
        if (ctx.power_slot < pv.count) {
            pv.slots[ctx.power_slot].amount =
                static_cast<int16_t>(ctx.power_counter);  // storedAmount
        }
        return;
    }
    if (hook == Hook::ON_ATTACKED) {
        // FlightPower.onAttacked (FlightPower.java:65-73):
        //
        //   Boolean willLive =
        //       calculateDamageTakenAmount(damageAmount, info.type)
        //           < (float)this.owner.currentHealth;
        //   if (info.owner != null && info.type != HP_LOSS && info.type != THORNS
        //       && damageAmount > 0 && willLive) {
        //       addToBot(new ReducePowerAction(owner, owner, POWER_ID, 1));
        //   }
        //
        // The two TYPE terms are already satisfied at this dispatch site:
        // op_damage fires ON_ATTACKED only for NORMAL damage (interp_damage.cpp),
        // which is also why ctx.damage_type is not consulted here. `info.owner !=
        // null` is ctx.source_null (a null-source pure-matrix hit does not shed a
        // stack), and `damageAmount > 0` is ctx.amount.
        if (ctx.source_null || ctx.amount <= 0) {
            return;
        }
        // THE HALVING IS APPLIED A SECOND TIME HERE, AND THAT IS THE GAME'S
        // ARITHMETIC, NOT A BUG TO FIX. `damageAmount` reaching onAttacked has
        // ALREADY been halved once by this power's own atDamageFinalReceive pass
        // (the FLIGHT case in interp_damage.cpp), and calculateDamageTakenAmount
        // halves whatever it is handed -- so the survival test is made against a
        // QUARTER of the pre-Flight damage. Reproduced, not corrected.
        //
        // Float comparison, matching the Java's `float < (float)currentHealth`.
        // `currentHealth` is the owner's PRE-HIT health: op_damage dispatches
        // ON_ATTACKED before it writes HP, which is where AbstractMonster.damage
        // (:667 vs :674) also puts it.
        int32_t owner_hp = 0;
        if (ctx.owner == kActorPlayer) {
            owner_hp = s.player_hp;
        } else if (ctx.owner < kMonsterCap) {
            owner_hp = s.monsters[ctx.owner].hp;
        } else {
            return;
        }
        const float taken = static_cast<float>(ctx.amount) / 2.0f;
        if (!(taken < static_cast<float>(owner_hp))) {
            // A KILLING BLOW SHEDS NO STACK. This is what decides whether a dying
            // Byrd queues a GROUNDED it would never act on: it does not.
            return;
        }
        ActionQueueItem red{};
        red.opcode = static_cast<uint16_t>(Opcode::REDUCE_POWER);
        red.src = ctx.owner;
        red.tgt = ctx.owner;
        red.amount = 1;
        red.flags = make_apply_power_flags(PowerId::FLIGHT);
        add_to_bottom(s, red);  // addToBot (:70)
        return;
    }
    if (hook == Hook::ON_POWER_REMOVED) {
        // FlightPower.onRemove (FlightPower.java:75-78):
        //     addToBot(new ChangeStateAction((AbstractMonster)owner, "GROUNDED"));
        //
        // Reached from remove_slot_at, so BOTH ways the power can die get here:
        // the onAttacked reduce falling to zero, and a bare
        // RemoveSpecificPowerAction. The cast is unconditional in the Java because
        // the Byrd is its only owner in the whole game; this checks the id rather
        // than assuming it, since a hand-built state could put Flight anywhere.
        if (ctx.owner >= kMonsterCap) {
            return;
        }
        MonsterState& m = s.monsters[ctx.owner];
        if (m.monster_id != static_cast<uint16_t>(MonsterId::BYRD)) {
            return;  // no other monster defines a GROUNDED state
        }
        // Byrd.changeState("GROUNDED") (Byrd.java:162-170), minus the animation
        // and hitbox lines, is three things: setMove(STUNNED, Intent.STUN),
        // createIntent() (pure presentation -- it rebuilds the intent IMAGE from
        // the move that was just set) and `isFlying = false`. There is NO isDying
        // guard, unlike Lagavulin's changeState("OPEN"), so this fires on a dying
        // Byrd too; nothing observes it, and it is reproduced as written.
        //
        // THE TELEGRAPH IS QUEUED AND THE LATCH IS SYNCHRONOUS -- the split
        // Lagavulin's change_state_open already makes, for the same reason. The
        // SET_MOVE goes through the queue because ChangeStateAction is addToBot
        // and the move it pushes onto the history ring is observable ordering.
        // The isFlying write is synchronous because its ONLY reader is the Byrd's
        // own getMove, and nothing can run one in between: Flight is shed to the
        // PLAYER's attacks, on the player's turn, while the Byrd's own roll
        // happens at the end of the Byrd's turn. (The one attacker that acts
        // during the Byrd's turn is a Thorns reflect, and Flight's onAttacked
        // ignores THORNS damage outright, so it never reaches this hook.)
        clear_byrd_flying(m);
        ActionQueueItem sm{};
        sm.opcode = static_cast<uint16_t>(Opcode::SET_MOVE);
        sm.src = ctx.owner;
        sm.tgt = ctx.owner;
        sm.amount = sts::registry::kByrdMoveStunned;
        sm.flags = static_cast<uint32_t>(MonsterIntent::STUN);
        add_to_bottom(s, sm);  // the queued ChangeStateAction's setMove (:163)
        return;
    }
}

}  // namespace sts::engine
