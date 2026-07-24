#pragma once

// Native bodies for the potion-support-powers follow-up batch
// (registry/powers.yaml ids 14-20, "--- potion-support-powers follow-up
// (discharges the B3.23/B3.24 deferrals) ---"): the power rows the ~9 Ironclad
// potions and two common relics needed. Dexterity (id 14) is a BLOCK-pipeline
// modifier with no hook body, so it has no entry here.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_lose_dexterity(CombatState& s, Hook hook,
                                 const HookContext& ctx) noexcept;
void power_native_thorns(CombatState& s, Hook hook,
                         const HookContext& ctx) noexcept;
void power_native_plated_armor(CombatState& s, Hook hook,
                               const HookContext& ctx) noexcept;
void power_native_regen(CombatState& s, Hook hook,
                        const HookContext& ctx) noexcept;
void power_native_ritual(CombatState& s, Hook hook,
                         const HookContext& ctx) noexcept;
void power_native_curl_up(CombatState& s, Hook hook,
                          const HookContext& ctx) noexcept;

}  // namespace sts::engine
