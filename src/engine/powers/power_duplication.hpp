#pragma once

// Duplication -- native power-hook body (registry/powers.yaml id 92,
// PowerId::DUPLICATION; the game's POWER_ID string is "DuplicationPower").
//
// The next `amount` cards the player plays are played twice. Granted only by
// the Duplication Potion. Double Tap's replay verb without the ATTACK filter,
// and with a decay-one-per-round tail instead of Double Tap's end-of-turn
// whole-power removal.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_duplication(CombatState& s, Hook hook,
                              const HookContext& ctx) noexcept;

}  // namespace sts::engine
