#pragma once

// Native bodies for the B3.25 uncommon relics (registry/relics.yaml tier
// UNCOMMON). The B3.25 uncommons whose combat body is DEFERRED (Mummified Hand,
// Pantograph) have no entry here -- relic_native_fn maps them to nullptr and
// documents each deferral at that table entry.

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

}  // namespace sts::engine
