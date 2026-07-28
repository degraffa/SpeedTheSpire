#pragma once

// Pen Nib -- native power-hook body (registry/powers.yaml id 88,
// PowerId::PEN_NIB).
//
// An attacker-side x2 on NORMAL damage (the atDamageGive case lives in
// interp_damage.cpp, not here). This translation unit carries the other half:
// the ATTACK that spends the doubling removes it.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_pen_nib(CombatState& s, Hook hook,
                          const HookContext& ctx) noexcept;

}  // namespace sts::engine
