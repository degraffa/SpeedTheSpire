#pragma once

// Back Attack -- native power-hook body (registry/powers.yaml id 137,
// PowerId::BACK_ATTACK).
//
// No per-power-list body, and no `hooks:`: BackAttackPower declares its ctor and
// updateDescription and NOTHING ELSE (BackAttackPower.java, 41 lines). THE 1.5x
// IS NOT IN THIS POWER -- it is hard-coded twice in AbstractMonster
// (calculateDamage :982-984 for the intent number, applyPowers :998-1013 for the
// real hit), both gated on applyBackAttack(). This power is a MARKER: it drives
// the intent icon, and it is what the two removal paths name. The mechanism, the
// marker's lifetime and the one declared deviation are all in back_attack.hpp.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_back_attack(CombatState& s, Hook hook,
                              const HookContext& ctx) noexcept;

}  // namespace sts::engine
