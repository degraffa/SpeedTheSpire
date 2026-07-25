#pragma once

// Rupture -- native power-hook body (registry/powers.yaml id 9,
// PowerId::RUPTURE).
//
// HP lost to the owner's OWN effects (not to enemy damage) grants `amount`
// Strength.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_rupture(CombatState& s, Hook hook,
                          const HookContext& ctx) noexcept;

}  // namespace sts::engine
