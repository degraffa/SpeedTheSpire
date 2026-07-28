// Thorns -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_thorns.hpp for what this power does.

#include "power_thorns.hpp"

#include <cstdint>
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags

namespace sts::engine {

void power_native_thorns(CombatState& s, Hook hook,
                         const HookContext& ctx) noexcept {
    // ThornsPower.onAttacked (ThornsPower.java:50-57): reflect `amount`
    // THORNS damage back to the attacker. op_damage already gated this to a
    // NORMAL attack from a distinct creature; the owner != attacker guard is
    // re-checked, and so is `info.owner != null` (:52) -- a null-source hit
    // (kDamageNullSource) reflects nothing, which also means the reflected
    // DAMAGE below never targets the meaningless src byte of a null-source
    // item. THORNS type -> the reflected DAMAGE skips all NORMAL-only power
    // modifiers, so a Vulnerable attacker does NOT amplify it.
    if (hook != Hook::ON_ATTACKED || ctx.source_null ||
        ctx.source == ctx.owner) {
        return;
    }
    ActionQueueItem dmg{};
    dmg.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    dmg.src = ctx.owner;        // the thorns-haver owns the reflected damage
    dmg.tgt = ctx.source;       // ... dealt to the attacker
    dmg.amount = ctx.power_amount;
    dmg.flags = make_damage_flags(DamageType::THORNS);
    add_to_top(s, dmg);         // addToTop (ThornsPower.java:48)
}

}  // namespace sts::engine
