#pragma once

// Explosive -- native power-hook body (registry/powers.yaml id 100,
// PowerId::EXPLOSIVE).
//
// A fuse on the Exploder. Every DURING_TURN (applyTurnPowers, right after its
// owner's takeTurn) it either ticks down by one or -- at 1, and only if the
// owner is not already dying -- kills its owner and hits the player for a flat
// 30 THORNS damage.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

// ExplosivePower's DAMAGE_AMOUNT (ExplosivePower.java:30). A private constant,
// NOT the power's `amount` field -- `amount` is the fuse in turns.
inline constexpr int32_t kExplosiveBlastDamage = 30;

void power_native_explosive(CombatState& s, Hook hook,
                            const HookContext& ctx) noexcept;

}  // namespace sts::engine
