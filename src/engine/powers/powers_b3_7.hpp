#pragma once

// Native bodies for the B3.7 red-uncommon-power cards' powers
// (registry/powers.yaml ids 26-27, "--- B3.7 red-uncommon-power cards ---").

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_evolve(CombatState& s, Hook hook,
                         const HookContext& ctx) noexcept;
void power_native_fire_breathing(CombatState& s, Hook hook,
                                 const HookContext& ctx) noexcept;

}  // namespace sts::engine
