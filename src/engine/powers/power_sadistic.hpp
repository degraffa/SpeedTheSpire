#pragma once

// Sadistic -- native power-hook body (registry/powers.yaml id 10,
// PowerId::SADISTIC).
//
// Applying a DEBUFF to a different, Artifact-free creature deals it `amount`
// THORNS damage.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_sadistic(CombatState& s, Hook hook,
                           const HookContext& ctx) noexcept;

}  // namespace sts::engine
