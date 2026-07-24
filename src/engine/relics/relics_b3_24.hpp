#pragma once

// Native bodies for the B3.24 starter + common relics (registry/relics.yaml
// tiers STARTER and COMMON). The B3.24 commons whose combat body is DEFERRED
// (Akabeko / Ancient Tea Set / Art of War / Boot / Preserved Insect / Toy
// Ornithopter) have no entry here -- relic_native_fn maps them to nullptr and
// documents each deferral at that table entry.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/relic_hooks.hpp"  // RelicHook, RelicHookContext
#include "sts/engine/run_state.hpp"    // RelicSlot

namespace sts::engine {

void relic_native_burning_blood(CombatState& s, RelicHook hook, RelicSlot& slot,
                                const RelicHookContext& ctx) noexcept;
void relic_native_blood_vial(CombatState& s, RelicHook hook, RelicSlot& slot,
                             const RelicHookContext& ctx) noexcept;
void relic_native_centennial_puzzle(CombatState& s, RelicHook hook,
                                    RelicSlot& slot,
                                    const RelicHookContext& ctx) noexcept;
void relic_native_orichalcum(CombatState& s, RelicHook hook, RelicSlot& slot,
                             const RelicHookContext& ctx) noexcept;
void relic_native_nunchaku(CombatState& s, RelicHook hook, RelicSlot& slot,
                           const RelicHookContext& ctx) noexcept;
void relic_native_pen_nib(CombatState& s, RelicHook hook, RelicSlot& slot,
                          const RelicHookContext& ctx) noexcept;
void relic_native_happy_flower(CombatState& s, RelicHook hook, RelicSlot& slot,
                               const RelicHookContext& ctx) noexcept;
void relic_native_lantern(CombatState& s, RelicHook hook, RelicSlot& slot,
                          const RelicHookContext& ctx) noexcept;
void relic_native_red_skull(CombatState& s, RelicHook hook, RelicSlot& slot,
                            const RelicHookContext& ctx) noexcept;

}  // namespace sts::engine
