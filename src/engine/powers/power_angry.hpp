#pragma once

// Angry -- native power-hook body (registry/powers.yaml id 40, PowerId::ANGRY).
//
// Every real hit the owner takes makes it permanently angrier: it gains
// `amount` Strength. Only GremlinWarrior applies it, before the first turn.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_angry(CombatState& s, Hook hook,
                        const HookContext& ctx) noexcept;

}  // namespace sts::engine
