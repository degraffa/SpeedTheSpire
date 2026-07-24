#pragma once

// Native body for the B3.9 curse-applied debuff (registry/powers.yaml id 21,
// "--- B3.9 curse-applied debuff ---").

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_frail(CombatState& s, Hook hook,
                        const HookContext& ctx) noexcept;

}  // namespace sts::engine
