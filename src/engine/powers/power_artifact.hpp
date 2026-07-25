#pragma once

// Artifact -- native power-hook body (registry/powers.yaml id 4,
// PowerId::ARTIFACT).
//
// No per-power-list body: Artifact's only effect is the TARGET-side debuff
// nullify, applied inline at the APPLY_POWER site in
// apply_power_blocked_by_artifact (power_hooks.cpp). The definition below
// exists and is deliberately empty -- see power_artifact.cpp for why.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_artifact(CombatState& s, Hook hook,
                           const HookContext& ctx) noexcept;

}  // namespace sts::engine
