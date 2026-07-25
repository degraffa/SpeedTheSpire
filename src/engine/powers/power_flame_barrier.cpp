// Flame Barrier -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_flame_barrier.hpp for what this power does.

#include "power_flame_barrier.hpp"

#include <cstdint>
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_flame_barrier(CombatState& s, Hook hook,
                                const HookContext& ctx) noexcept {
    // FlameBarrierPower (B3.6). onAttacked (FlameBarrierPower.java:
    // 53-59): reflect `amount` THORNS damage to a DISTINCT attacker --
    // op_damage already gated dispatch to NORMAL src != tgt after
    // decrementBlock (fires whether or not the hit penetrated); the
    // owner != attacker guard is re-checked. addToTop, THORNS-typed
    // (skips the NORMAL-only power modifiers -- a Vulnerable attacker
    // is NOT amplified). atStartOfTurn (:62-64): addToBot
    // RemoveSpecificPowerAction -- gone at the player's next turn start.
    if (hook == Hook::ON_ATTACKED) {
        if (ctx.source == ctx.owner) {
            return;
        }
        ActionQueueItem dmg{};
        dmg.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
        dmg.src = ctx.owner;
        dmg.tgt = ctx.source;
        dmg.amount = ctx.power_amount;
        dmg.flags = make_damage_flags(DamageType::THORNS);
        add_to_top(s, dmg);  // addToTop (FlameBarrierPower.java:56)
        return;
    }
    if (hook == Hook::AT_START_OF_TURN) {
        ActionQueueItem rem{};
        rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
        rem.src = ctx.owner;
        rem.tgt = ctx.owner;
        rem.flags = make_apply_power_flags(PowerId::FLAME_BARRIER);
        add_to_bottom(s, rem);  // addToBot (FlameBarrierPower.java:63)
        return;
    }
}

}  // namespace sts::engine
