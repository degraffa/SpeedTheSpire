#pragma once

// Dark Embrace -- native power-hook body (registry/powers.yaml id 7,
// PowerId::DARK_EMBRACE).
//
// Each card exhausted while a monster still lives draws `amount` cards.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_dark_embrace(CombatState& s, Hook hook,
                               const HookContext& ctx) noexcept;

}  // namespace sts::engine
