#pragma once

// Native bodies for the SHOP-tier relics (registry/relics.yaml tier SHOP). The
// generated dispatch table (STS_REGISTRY_NATIVE_RELICS) odr-uses a handler for
// EVERY `native: true` row, so a forgotten body is a link error, not a silent
// no-op.
//
// Two bodies here are DEFERRED and are EXPLICIT EMPTY functions in
// relics_shop.cpp -- Sling of Courage (the room's eliteTrigger flag has no
// combat-state representation) and Orange Pellets' on-use-card half (no opcode
// removes every DEBUFF the way RemoveDebuffsAction does). Each carries its reason
// and its Java citation at the definition.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/relic_hooks.hpp"  // RelicHook, RelicHookContext
#include "sts/engine/run_state.hpp"    // RelicSlot

namespace sts::engine {

void relic_native_brimstone(CombatState& s, RelicHook hook, RelicSlot& slot,
                            const RelicHookContext& ctx) noexcept;
void relic_native_hand_drill(CombatState& s, RelicHook hook, RelicSlot& slot,
                             const RelicHookContext& ctx) noexcept;
void relic_native_medical_kit(CombatState& s, RelicHook hook, RelicSlot& slot,
                              const RelicHookContext& ctx) noexcept;

// --- DEFERRED combat bodies (explicit empty definitions) ---------------------
void relic_native_orange_pellets(CombatState& s, RelicHook hook, RelicSlot& slot,
                                 const RelicHookContext& ctx) noexcept;
void relic_native_sling_of_courage(CombatState& s, RelicHook hook,
                                   RelicSlot& slot,
                                   const RelicHookContext& ctx) noexcept;

}  // namespace sts::engine
