#pragma once

// Native bodies for the B3.25 uncommon relics (registry/relics.yaml tier
// UNCOMMON), including the two whose combat body is DEFERRED (Mummified Hand,
// Pantograph). Those two are DELIBERATELY EMPTY definitions rather than
// omissions: the generated dispatch table (STS_REGISTRY_NATIVE_RELICS) odr-uses
// a handler for EVERY `native: true` row, so a forgotten body is a link error.
// "Deferred" therefore has to be written down -- and it is, at each empty
// definition in relics_uncommon.cpp, together with its reason.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/relic_hooks.hpp"  // RelicHook, RelicHookContext
#include "sts/engine/run_state.hpp"    // RelicSlot

namespace sts::engine {

void relic_native_blue_candle(CombatState& s, RelicHook hook, RelicSlot& slot,
                              const RelicHookContext& ctx) noexcept;
void relic_native_gremlin_horn(CombatState& s, RelicHook hook, RelicSlot& slot,
                               const RelicHookContext& ctx) noexcept;
void relic_native_horn_cleat(CombatState& s, RelicHook hook, RelicSlot& slot,
                             const RelicHookContext& ctx) noexcept;
void relic_native_ink_bottle(CombatState& s, RelicHook hook, RelicSlot& slot,
                             const RelicHookContext& ctx) noexcept;
void relic_native_kunai(CombatState& s, RelicHook hook, RelicSlot& slot,
                        const RelicHookContext& ctx) noexcept;
void relic_native_letter_opener(CombatState& s, RelicHook hook, RelicSlot& slot,
                                const RelicHookContext& ctx) noexcept;
void relic_native_ornamental_fan(CombatState& s, RelicHook hook, RelicSlot& slot,
                                 const RelicHookContext& ctx) noexcept;
void relic_native_shuriken(CombatState& s, RelicHook hook, RelicSlot& slot,
                           const RelicHookContext& ctx) noexcept;
void relic_native_sundial(CombatState& s, RelicHook hook, RelicSlot& slot,
                          const RelicHookContext& ctx) noexcept;
void relic_native_self_forming_clay(CombatState& s, RelicHook hook,
                                    RelicSlot& slot,
                                    const RelicHookContext& ctx) noexcept;

// --- DEFERRED combat bodies (deliberately empty; see relics_uncommon.cpp) -----
void relic_native_mummified_hand(CombatState& s, RelicHook hook, RelicSlot& slot,
                                 const RelicHookContext& ctx) noexcept;
void relic_native_pantograph(CombatState& s, RelicHook hook, RelicSlot& slot,
                             const RelicHookContext& ctx) noexcept;

}  // namespace sts::engine
