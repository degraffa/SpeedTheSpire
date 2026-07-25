#pragma once

// Native bodies for the RARE-tier relics (registry/relics.yaml tier RARE). The
// generated dispatch table (STS_REGISTRY_NATIVE_RELICS) odr-uses a handler for
// EVERY `native: true` row, so a forgotten body is a link error, not a silent
// no-op.
//
// Two of this tier's bodies are DEFERRED and are EXPLICIT EMPTY functions in
// relics_rare.cpp rather than omissions -- Dead Branch (needs the unfiltered
// all-red combat card pool that returnTrulyRandomCardInCombat draws from) and
// Gambling Chip (needs an optional multi-select hand screen). Each carries its
// reason and its Java citation at the definition, so the decision is on the
// record where the implementation will go.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/relic_hooks.hpp"  // RelicHook, RelicHookContext
#include "sts/engine/run_state.hpp"    // RelicSlot

namespace sts::engine {

void relic_native_bird_faced_urn(CombatState& s, RelicHook hook, RelicSlot& slot,
                                 const RelicHookContext& ctx) noexcept;
void relic_native_captains_wheel(CombatState& s, RelicHook hook, RelicSlot& slot,
                                 const RelicHookContext& ctx) noexcept;
void relic_native_charons_ashes(CombatState& s, RelicHook hook, RelicSlot& slot,
                                const RelicHookContext& ctx) noexcept;
void relic_native_du_vu_doll(CombatState& s, RelicHook hook, RelicSlot& slot,
                             const RelicHookContext& ctx) noexcept;
void relic_native_girya(CombatState& s, RelicHook hook, RelicSlot& slot,
                        const RelicHookContext& ctx) noexcept;
void relic_native_incense_burner(CombatState& s, RelicHook hook, RelicSlot& slot,
                                 const RelicHookContext& ctx) noexcept;
void relic_native_pocketwatch(CombatState& s, RelicHook hook, RelicSlot& slot,
                              const RelicHookContext& ctx) noexcept;
void relic_native_stone_calendar(CombatState& s, RelicHook hook, RelicSlot& slot,
                                 const RelicHookContext& ctx) noexcept;

// --- DEFERRED combat bodies (explicit empty definitions) ---------------------
void relic_native_dead_branch(CombatState& s, RelicHook hook, RelicSlot& slot,
                              const RelicHookContext& ctx) noexcept;
void relic_native_gambling_chip(CombatState& s, RelicHook hook, RelicSlot& slot,
                                const RelicHookContext& ctx) noexcept;

}  // namespace sts::engine
