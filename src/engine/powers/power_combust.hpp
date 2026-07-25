#pragma once

// Combust -- native power-hook body (registry/powers.yaml id 8,
// PowerId::COMBUST).
//
// At end of turn: lose the accumulated hidden hpLoss, then deal `amount`
// THORNS damage to every enemy.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_combust(CombatState& s, Hook hook,
                          const HookContext& ctx) noexcept;

}  // namespace sts::engine
