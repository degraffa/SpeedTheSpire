#pragma once

// Native bodies for the BOSS-tier relics (registry/relics.yaml tier BOSS). The
// generated dispatch table (STS_REGISTRY_NATIVE_RELICS) odr-uses a handler for
// EVERY `native: true` row, so a forgotten body is a link error, not a silent
// no-op.
//
// The tier's inert relics are NOT in this file. A boss relic whose only effect is
// `++energyMaster`, a run-layer marker, or an observation-layer change binds no
// hook at all and therefore has no handler; its row, its pool slot and its
// relicRng draw are what make it correct (registry/relics.yaml carries the reason
// per row, and the BOSS section header carries the shared energyMaster deferral).
// This header holds only the relics that DO respond to a combat hook.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/relic_hooks.hpp"  // RelicHook, RelicHookContext
#include "sts/engine/run_state.hpp"    // RelicSlot

namespace sts::engine {

void relic_native_velvet_choker(CombatState& s, RelicHook hook, RelicSlot& slot,
                                const RelicHookContext& ctx) noexcept;
void relic_native_snecko_eye(CombatState& s, RelicHook hook, RelicSlot& slot,
                             const RelicHookContext& ctx) noexcept;
void relic_native_philosophers_stone(CombatState& s, RelicHook hook,
                                     RelicSlot& slot,
                                     const RelicHookContext& ctx) noexcept;
void relic_native_black_blood(CombatState& s, RelicHook hook, RelicSlot& slot,
                              const RelicHookContext& ctx) noexcept;
void relic_native_mark_of_pain(CombatState& s, RelicHook hook, RelicSlot& slot,
                               const RelicHookContext& ctx) noexcept;
void relic_native_runic_cube(CombatState& s, RelicHook hook, RelicSlot& slot,
                             const RelicHookContext& ctx) noexcept;

// Velvet Choker's canPlay veto (VelvetChoker.java:77-84), read by legal_actions
// (advance.cpp) rather than by a hook: once the relic's counter reaches the
// 6-card play limit NO card is playable for the rest of the turn. It lives here
// beside the counter body that produces the number, so the two cannot drift.
inline constexpr int16_t kVelvetChokerPlayLimit = 6;

}  // namespace sts::engine
