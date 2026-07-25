// Rupture -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_rupture.hpp for what this power does.

#include "power_rupture.hpp"

#include <cstdint>
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags, CardPile
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_rupture(CombatState& s, Hook hook,
                          const HookContext& ctx) noexcept {
    // RupturePower.wasHPLost: fire ONLY when the HP loss was self-inflicted
    // (info.owner == owner) -- card HP loss, not unblocked enemy damage.
    if (hook != Hook::WAS_HP_LOST) {
        return;
    }
    if (ctx.source != ctx.owner || ctx.amount <= 0) {
        return;  // attribution guard (RupturePower.java:61)
    }
    ActionQueueItem gain{};
    gain.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    gain.src = ctx.owner;
    gain.tgt = ctx.owner;
    gain.amount = ctx.power_amount;  // +amount Strength
    gain.flags = make_apply_power_flags(PowerId::STRENGTH);
    add_to_top(s, gain);             // addToTop (RupturePower.java:63)
}

}  // namespace sts::engine
