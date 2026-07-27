#pragma once

// Panache -- native power-hook body (registry/powers.yaml id 83,
// PowerId::PANACHE).
//
// A 5-card countdown in PowerSlot.amount and an accumulated damage number in
// PowerSlot.counter. Every card the player USES decrements the countdown; the
// 5th resets it to 5 and fires the accumulated damage at every enemy as PURE
// (unmodifiable) THORNS damage. The countdown -- but not the damage -- also
// resets at the start of every turn, so partial progress does not carry over.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_panache(CombatState& s, Hook hook,
                          const HookContext& ctx) noexcept;

}  // namespace sts::engine
