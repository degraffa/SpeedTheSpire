#pragma once

// The Bomb -- native power-hook body (registry/powers.yaml id 84,
// PowerId::THE_BOMB).
//
// An INSTANCED power: every play appends its own slot with its own fuse
// (PowerSlot.amount, in turns) and its own payload (PowerSlot.counter, the
// damage). At the end of each player turn every live instance ticks down by one,
// and the instance that was standing at 1 detonates for its damage against every
// enemy and leaves the list in the same end-of-turn.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_the_bomb(CombatState& s, Hook hook,
                           const HookContext& ctx) noexcept;

}  // namespace sts::engine
