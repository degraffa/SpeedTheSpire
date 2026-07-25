#pragma once

// Corruption -- native power-hook body (registry/powers.yaml id 11,
// PowerId::CORRUPTION).
//
// A SKILL costs 0 when drawn and is redirected to exhaust when played.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_corruption(CombatState& s, Hook hook,
                             const HookContext& ctx) noexcept;

}  // namespace sts::engine
