#pragma once

// Double Tap -- native power-hook body (registry/powers.yaml PowerId::DOUBLE_TAP).
//
// The next `amount` ATTACKs played are played a second time; the power removes
// itself once the counter reaches zero, and again at the end of the turn.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_double_tap(CombatState& s, Hook hook,
                             const HookContext& ctx) noexcept;

}  // namespace sts::engine
