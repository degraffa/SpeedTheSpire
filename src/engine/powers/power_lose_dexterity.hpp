#pragma once

// Lose Dexterity -- native power-hook body (registry/powers.yaml id 15,
// PowerId::LOSE_DEXTERITY).
//
// At end of turn, subtract `amount` Dexterity and remove itself -- the
// Dexterity mirror of Lose Strength.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_lose_dexterity(CombatState& s, Hook hook,
                                 const HookContext& ctx) noexcept;

}  // namespace sts::engine
