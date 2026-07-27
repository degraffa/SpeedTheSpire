#pragma once

// Vulnerable -- native power-hook body (registry/powers.yaml id 2,
// PowerId::VULNERABLE).
//
// The x1.5 damage-received multiplier is the native damage pipeline and binds no
// hook. What lives here is the DURATION: one stack decays at end of round,
// skipping the round the debuff arrived. Shared body in
// power_duration_debuff.hpp.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_vulnerable(CombatState& s, Hook hook,
                             const HookContext& ctx) noexcept;

}  // namespace sts::engine
