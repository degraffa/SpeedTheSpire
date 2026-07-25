#pragma once

// Evolve -- native power-hook body (registry/powers.yaml id 26,
// PowerId::EVOLVE).
//
// Drawing a STATUS card draws `amount` more, unless No Draw is active.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_evolve(CombatState& s, Hook hook,
                         const HookContext& ctx) noexcept;

}  // namespace sts::engine
