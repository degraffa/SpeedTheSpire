#pragma once

// Regenerate Monster -- native power-hook body (registry/powers.yaml id 91,
// PowerId::REGENERATE_MONSTER).
//
// Heals `amount` at end of turn. UNLIKE PowerId::REGEN (id 18, the player's
// "Regeneration"), this power NEVER decays -- RegenerateMonsterPower
// (RegenerateMonsterPower.java:37-43) queues a bare HealAction, not a
// RegenAction, so there is no isPlayer-gated decrement to reproduce. See
// power_regen.hpp/.cpp for the sibling this body is copied from, minus that
// decrement tail.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_regenerate_monster(CombatState& s, Hook hook,
                                     const HookContext& ctx) noexcept;

}  // namespace sts::engine
