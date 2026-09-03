#pragma once

// Surrounded -- native power-hook body (registry/powers.yaml id 136,
// PowerId::SURROUNDED).
//
// No per-power-list body, and no `hooks:` either: SurroundedPower declares its
// ctor and updateDescription and NOTHING ELSE (SurroundedPower.java, 30 lines) --
// it overrides no hook at all. Its whole effect is being FOUND by
// AbstractMonster.applyBackAttack (AbstractMonster.java:1015-1017), which reads
// the player's power list rather than being called by it. That reader lives in
// back_attack.hpp. The definition below exists and is deliberately empty -- see
// power_surrounded.cpp for why the row is `native: true` anyway.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_surrounded(CombatState& s, Hook hook,
                             const HookContext& ctx) noexcept;

}  // namespace sts::engine
