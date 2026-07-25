#pragma once

// Fire Breathing -- native power-hook body (registry/powers.yaml id 27,
// PowerId::FIRE_BREATHING).
//
// Drawing a STATUS or CURSE card deals `amount` THORNS damage to every
// enemy.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_fire_breathing(CombatState& s, Hook hook,
                                 const HookContext& ctx) noexcept;

}  // namespace sts::engine
