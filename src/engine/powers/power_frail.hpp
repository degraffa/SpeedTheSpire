#pragma once

// Frail -- native power-hook body (registry/powers.yaml id 21,
// PowerId::FRAIL).
//
// A duration debuff on the player: one stack decays at end of round, skipping
// the round it was applied. The Block reduction itself is a BLOCK-pipeline
// modifier, not a hook.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_frail(CombatState& s, Hook hook,
                        const HookContext& ctx) noexcept;

}  // namespace sts::engine
