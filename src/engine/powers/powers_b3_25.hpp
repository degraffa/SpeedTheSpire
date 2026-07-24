#pragma once

// Native body for the B3.25 relic-applied power (registry/powers.yaml id 23,
// "--- B3.25 relic-applied power (Self-Forming Clay) ---").

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_next_turn_block(CombatState& s, Hook hook,
                                  const HookContext& ctx) noexcept;

}  // namespace sts::engine
