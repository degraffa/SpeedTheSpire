#pragma once

// Lose Strength -- native power-hook body (registry/powers.yaml id 13,
// PowerId::LOSE_STRENGTH).
//
// At end of turn, subtract `amount` Strength and remove itself -- the
// temporary-Strength reversal behind Flex.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_lose_strength(CombatState& s, Hook hook,
                                const HookContext& ctx) noexcept;

}  // namespace sts::engine
