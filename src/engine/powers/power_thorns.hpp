#pragma once

// Thorns -- native power-hook body (registry/powers.yaml id 16,
// PowerId::THORNS).
//
// A NORMAL attack from a different creature is reflected back at the attacker
// for `amount` THORNS damage.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_thorns(CombatState& s, Hook hook,
                         const HookContext& ctx) noexcept;

}  // namespace sts::engine
