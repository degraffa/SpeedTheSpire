#pragma once

// Confusion -- native power-hook body (registry/powers.yaml id 59,
// PowerId::CONFUSION).
//
// Snecko Eye's debuff: every card drawn with a non-negative cost is re-rolled to
// a cost in 0..3, one cardRandomRng draw per such card, in draw order.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_confusion(CombatState& s, Hook hook,
                            const HookContext& ctx) noexcept;

}  // namespace sts::engine
