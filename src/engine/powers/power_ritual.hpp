#pragma once

// Ritual -- native power-hook body (registry/powers.yaml id 19,
// PowerId::RITUAL).
//
// Grants `amount` Strength: each end of TURN for a player owner, each end of
// ROUND after the first for a monster owner.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_ritual(CombatState& s, Hook hook,
                         const HookContext& ctx) noexcept;

}  // namespace sts::engine
