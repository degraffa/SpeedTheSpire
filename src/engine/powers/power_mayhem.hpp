#pragma once

// Mayhem -- native power-hook body (registry/powers.yaml id 81,
// PowerId::MAYHEM).
//
// At the start of each of the player's turns (pre-draw), play the top card of
// the draw pile once per stack, aimed at a random living monster.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_mayhem(CombatState& s, Hook hook,
                         const HookContext& ctx) noexcept;

}  // namespace sts::engine
