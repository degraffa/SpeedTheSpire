#pragma once

// Native bodies for the BOSS-tier relics (registry/relics.yaml tier BOSS). The
// generated dispatch table (STS_REGISTRY_NATIVE_RELICS) odr-uses a handler for
// EVERY `native: true` row, so a forgotten body is a link error, not a silent
// no-op.
//
// The tier's HOOKLESS relics are NOT in this file, and "hookless" is not the
// same as "inert". A boss relic whose effect is `++energyMaster`, a campfire
// lock, a run-layer marker or an observation-layer change binds no hook and
// therefore has no handler: it is read at its consumer instead -- energy_master
// (action_queue.cpp), build_rest_menu's veto sweep (rest_sites.cpp),
// encode_observation (observation.hpp), gain_gold, the potion doors. Those reads
// are what make the row correct, alongside its pool slot and its relicRng draw;
// registry/relics.yaml carries the reason per row. This header holds only the
// relics that DO respond to a combat hook.

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
