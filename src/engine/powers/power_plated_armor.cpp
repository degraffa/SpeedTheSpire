// Plated Armor -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_plated_armor.hpp for what this power does.

#include "power_plated_armor.hpp"

#include <cstdint>
#include "power_native.hpp"             // PowerNativeSig (the shared handler signature)
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // MonsterIntent (the ARMOR_BREAK telegraph)
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"   // kShelledParasiteMoveStunned

namespace sts::engine {

void power_native_plated_armor(CombatState& s, Hook hook,
                               const HookContext& ctx) noexcept {
    if (hook == Hook::AT_END_OF_TURN_PRE_CARD) {
        // GainBlockAction(owner, amount) at the §5.4 pre-card phase (the same
        // slot as Metallicize). A direct GainBlockAction -> kBlockNoPowers, so
        // Plated Armor block does NOT get Dexterity (PlatedArmorPower.java:72).
        ActionQueueItem blk{};
        blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
        blk.src = ctx.owner;
        blk.tgt = ctx.owner;
        blk.amount = ctx.power_amount;
        blk.flags = kBlockNoPowers;
        add_to_bottom(s, blk);  // addToBot (PlatedArmorPower.java:72)
        return;
    }
    if (hook == Hook::WAS_HP_LOST) {
        // Lose 1 stack on a real attack from a distinct creature; NOT on a
        // THORNS / HP_LOSS / self / NULL-OWNER loss (`info.owner != null`,
        // PlatedArmorPower.java:57-58 -- a null-source pure-matrix hit,
        // kDamageNullSource, does not shed a stack).
        if (ctx.source_null ||
            ctx.damage_type == static_cast<uint8_t>(DamageType::THORNS) ||
            ctx.damage_type == static_cast<uint8_t>(DamageType::HP_LOSS) ||
            ctx.source == ctx.owner || ctx.amount <= 0) {
            return;
        }
        // `addToBot(new ReducePowerAction(owner, owner, "Plated Armor", 1))`
        // (PlatedArmorPower.java:58) -- a QUEUED reduction, which is both what
        // the Java does and the only form that reaches the removal choke point.
        //
        // THIS USED TO DECREMENT THE SLOT IN PLACE AND ZERO power_id AT 0, which
        // was wrong twice over. It was wrong in TIMING (a synchronous write where
        // the Java queues), and -- the reason it had to change -- it BYPASSED
        // remove_slot_at (interp/interp_powers.cpp), so the destroyed power's own
        // onRemove never fired. Nothing noticed while Plated Armor's only owner
        // was the player, because PlatedArmorPower has no onRemove; the Shelled
        // Parasite made it load-bearing, since its ARMOR_BREAK stun telegraph IS
        // the removal edge (ShelledParasite.java:151-159, reached from
        // PlatedArmorPower's removal). Routing through REDUCE_POWER fixes both:
        // op_reduce_power subtracts, and calls remove_slot_at at <= 0, which is
        // where ON_POWER_REMOVED is dispatched.
        //
        // NOT an instance-keyed reduce: Plated Armor is not an instanced power
        // (one slot per owner), so the plain first-match-by-id that
        // ReducePowerAction's ID constructor uses is exact.
        ActionQueueItem red{};
        red.opcode = static_cast<uint16_t>(Opcode::REDUCE_POWER);
        red.src = ctx.owner;
        red.tgt = ctx.owner;
        red.amount = 1;
        red.flags = make_apply_power_flags(PowerId::PLATED_ARMOR);
        add_to_bottom(s, red);  // addToBot (PlatedArmorPower.java:58)
        return;
    }
    if (hook == Hook::ON_POWER_REMOVED) {
        // PlatedArmorPower.onRemove (PlatedArmorPower.java:65-70):
        //     if (!this.owner.isPlayer) {
        //         this.addToBot(new ChangeStateAction(
        //             (AbstractMonster)this.owner, "ARMOR_BREAK"));
        //     }
        //
        // The isPlayer gate is why a player whose Thread-and-Needle armour runs
        // out sees nothing at all. This is the OTHER half of the reroute above:
        // while the body decremented its own slot in place, removal never
        // reached remove_slot_at and this could not fire.
        if (ctx.owner == kActorPlayer || ctx.owner >= kMonsterCap) {
            return;
        }
        // ShelledParasite.changeState("ARMOR_BREAK")
        // (ShelledParasite.java:151-159) is three AnimateHopActions and two
        // WaitActions -- all presentation -- plus setMove(STUNNED, Intent.STUN)
        // and a createIntent repaint. The Java sends the ChangeStateAction to any
        // non-player owner and lets the monster's own changeState ignore an
        // unknown key; checking the id here reproduces exactly that, since the
        // Shelled Parasite is the only class defining an ARMOR_BREAK state.
        if (s.monsters[ctx.owner].monster_id !=
            static_cast<uint16_t>(MonsterId::SHELLED_PARASITE)) {
            return;
        }
        ActionQueueItem sm{};
        sm.opcode = static_cast<uint16_t>(Opcode::SET_MOVE);
        sm.src = ctx.owner;
        sm.tgt = ctx.owner;
        sm.amount = sts::registry::kShelledParasiteMoveStunned;
        sm.flags = static_cast<uint32_t>(MonsterIntent::STUN);
        add_to_bottom(s, sm);  // the queued ChangeStateAction's setMove (:157)
        return;
    }
}

}  // namespace sts::engine
