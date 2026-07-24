#pragma once

// Native bodies for the B3.2 framework powers (registry/powers.yaml ids 4-12,
// "--- B3.2 framework powers ---"): the powers that exist to prove the §5.2-5.5
// hook dispatch order. Artifact (id 4) has no per-power-list body -- its target-
// side nullify lives in apply_power_blocked_by_artifact -- so it has no entry
// here and power_native_fn maps it to nullptr.
//
// Rage (id 12) is the B3.6 completion of the B3.2 stub; it stays with its
// introducing batch.

#include "sts/engine/power_hooks.hpp"  // Hook, HookContext
#include "sts/engine/combat_state.hpp"

namespace sts::engine {

void power_native_sadistic(CombatState& s, Hook hook,
                           const HookContext& ctx) noexcept;
void power_native_dark_embrace(CombatState& s, Hook hook,
                               const HookContext& ctx) noexcept;
void power_native_combust(CombatState& s, Hook hook,
                          const HookContext& ctx) noexcept;
void power_native_rupture(CombatState& s, Hook hook,
                          const HookContext& ctx) noexcept;
void power_native_corruption(CombatState& s, Hook hook,
                             const HookContext& ctx) noexcept;
void power_native_rage(CombatState& s, Hook hook,
                       const HookContext& ctx) noexcept;

}  // namespace sts::engine
