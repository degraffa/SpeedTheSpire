#pragma once

// Next Turn Block -- native power-hook body (registry/powers.yaml id 23,
// PowerId::NEXT_TURN_BLOCK).
//
// Gains `amount` Block at the owner's next turn start, then removes itself --
// the "Block at the start of your next turn" of Self-Forming Clay.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_next_turn_block(CombatState& s, Hook hook,
                                  const HookContext& ctx) noexcept;

}  // namespace sts::engine
