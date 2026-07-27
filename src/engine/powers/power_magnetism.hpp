#pragma once

// Magnetism -- native power-hook body (registry/powers.yaml id 82,
// PowerId::MAGNETISM).
//
// At the start of each of the player's turns (pre-draw), and only while the
// fight is still live, add one random COLORLESS card to the hand per stack.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_magnetism(CombatState& s, Hook hook,
                            const HookContext& ctx) noexcept;

}  // namespace sts::engine
