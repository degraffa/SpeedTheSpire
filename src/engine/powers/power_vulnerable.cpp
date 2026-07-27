// Vulnerable -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and power_vulnerable.hpp for
// what this power does.

#include "power_vulnerable.hpp"

#include "power_duration_debuff.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_vulnerable(CombatState& s, Hook hook,
                             const HookContext& ctx) noexcept {
    // VulnerablePower.atEndOfRound (VulnerablePower.java:44-53).
    duration_debuff_at_end_of_round(s, hook, ctx, PowerId::VULNERABLE);
}

}  // namespace sts::engine
