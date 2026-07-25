#pragma once

// Rage -- native power-hook body (registry/powers.yaml id 12,
// PowerId::RAGE).
//
// Each ATTACK played gains the owner `amount` Block; the power removes itself
// at end of turn, so it lasts exactly one turn.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_rage(CombatState& s, Hook hook,
                       const HookContext& ctx) noexcept;

}  // namespace sts::engine
