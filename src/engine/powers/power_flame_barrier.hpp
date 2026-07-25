#pragma once

// Flame Barrier -- native power-hook body (registry/powers.yaml id 25,
// PowerId::FLAME_BARRIER).
//
// Reflects `amount` THORNS damage at each distinct attacker; removed at the
// owner's next turn start.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_flame_barrier(CombatState& s, Hook hook,
                                const HookContext& ctx) noexcept;

}  // namespace sts::engine
