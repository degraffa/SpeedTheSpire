#pragma once

// Native body for the B3.4 (Flex) power (registry/powers.yaml id 13,
// "--- B3.4 (Flex): temporary-Strength reversal power ---").

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_lose_strength(CombatState& s, Hook hook,
                                const HookContext& ctx) noexcept;

}  // namespace sts::engine
