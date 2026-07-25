#pragma once

// Anger -- native power-hook body (registry/powers.yaml id 33,
// PowerId::ANGER).
//
// Every SKILL the player plays gives the power's OWNER (the Gremlin Nob)
// `amount` Strength. Attacks and powers do not trigger it.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_anger(CombatState& s, Hook hook,
                        const HookContext& ctx) noexcept;

}  // namespace sts::engine
