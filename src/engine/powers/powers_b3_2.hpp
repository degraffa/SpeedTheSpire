#pragma once

// Native bodies for the B3.2 framework powers (registry/powers.yaml ids 4-12,
// "--- B3.2 framework powers ---"): the powers that exist to prove the §5.2-5.5
// hook dispatch order. Artifact (id 4) has no per-power-list body -- its target-
// side nullify lives in apply_power_blocked_by_artifact -- but the generated
// dispatch table (STS_REGISTRY_NATIVE_POWERS) odr-uses a handler for EVERY
// `native: true` row, so "deliberately no body" is spelled out as an explicit
// empty definition below rather than as a missing symbol.
//
// Rage (id 12) is the B3.6 completion of the B3.2 stub; it stays with its
// introducing batch.

#include "sts/engine/power_hooks.hpp"  // Hook, HookContext
#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Artifact: deliberately EMPTY -- see the definition in powers_b3_2.cpp.
void power_native_artifact(CombatState& s, Hook hook,
                           const HookContext& ctx) noexcept;
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
