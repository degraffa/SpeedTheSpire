// Angry -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_angry.hpp for what this power does.

#include "power_angry.hpp"

#include <cstdint>
#include "sts/engine/action_queue.hpp"  // add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_angry(CombatState& s, Hook hook,
                        const HookContext& ctx) noexcept {
    // AngryPower.onAttacked (AngryPower.java:34-41):
    //   if (info.owner != null && damageAmount > 0 && info.type != HP_LOSS
    //       && info.type != THORNS)
    //       addToTop(ApplyPowerAction(owner, owner, StrengthPower(owner, amount)))
    // op_damage already gates dispatch_on_attacked to NORMAL damage from a
    // DISTINCT attacker, which covers `info.owner != null` and both type
    // exclusions, and it passes the POST-block damage as ctx.amount. So the only
    // guard left to re-check is damageAmount > 0 -- Angry, unlike Curl Up, does
    // NOT fire on a fully-blocked hit.
    if (hook != Hook::ON_ATTACKED || ctx.amount <= 0) {
        return;
    }
    ActionQueueItem up{};
    up.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    up.src = ctx.owner;
    up.tgt = ctx.owner;
    up.amount = ctx.power_amount;  // StrengthPower(owner, this.amount)
    up.flags = make_apply_power_flags(PowerId::STRENGTH);
    add_to_top(s, up);             // addToTop (AngryPower.java:37)
}

}  // namespace sts::engine
