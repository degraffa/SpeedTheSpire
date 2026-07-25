#pragma once

// Plated Armor -- native power-hook body (registry/powers.yaml id 17,
// PowerId::PLATED_ARMOR).
//
// Gains `amount` Block in the pre-card end-of-turn phase; loses one stack to
// each real attack (not to THORNS / HP_LOSS / self damage), removed at 0.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_plated_armor(CombatState& s, Hook hook,
                               const HookContext& ctx) noexcept;

}  // namespace sts::engine
