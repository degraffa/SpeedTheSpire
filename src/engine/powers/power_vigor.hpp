#pragma once

// Vigor -- native power-hook body (registry/powers.yaml id 87, PowerId::VIGOR).
//
// An attacker-side flat ADD to NORMAL damage (the atDamageGive case lives in
// interp_damage.cpp, not here). This translation unit carries the other half:
// the first ATTACK played consumes it.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_vigor(CombatState& s, Hook hook,
                        const HookContext& ctx) noexcept;

}  // namespace sts::engine
