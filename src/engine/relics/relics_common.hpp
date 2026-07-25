#pragma once

// Native bodies for the B3.24 common relics (registry/relics.yaml tier COMMON),
// including the six whose combat body is DEFERRED
// (Akabeko / Ancient Tea Set / Art of War / Boot / Preserved Insect / Toy
// Ornithopter). Those six are DELIBERATELY EMPTY definitions rather than
// omissions: the generated dispatch table (STS_REGISTRY_NATIVE_RELICS) odr-uses
// a handler for EVERY `native: true` row, so a forgotten body is a link error.
// "Deferred" therefore has to be written down -- and it is, at each empty
// definition in relics_common.cpp, together with its reason.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/relic_hooks.hpp"  // RelicHook, RelicHookContext
#include "sts/engine/run_state.hpp"    // RelicSlot

namespace sts::engine {

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

// --- DEFERRED combat bodies (deliberately empty; see relics_common.cpp) ------
void relic_native_akabeko(CombatState& s, RelicHook hook, RelicSlot& slot,
                          const RelicHookContext& ctx) noexcept;
void relic_native_ancient_tea_set(CombatState& s, RelicHook hook,
                                  RelicSlot& slot,
                                  const RelicHookContext& ctx) noexcept;
void relic_native_art_of_war(CombatState& s, RelicHook hook, RelicSlot& slot,
                             const RelicHookContext& ctx) noexcept;
void relic_native_boot(CombatState& s, RelicHook hook, RelicSlot& slot,
                       const RelicHookContext& ctx) noexcept;
void relic_native_preserved_insect(CombatState& s, RelicHook hook,
                                   RelicSlot& slot,
                                   const RelicHookContext& ctx) noexcept;
void relic_native_toy_ornithopter(CombatState& s, RelicHook hook,
                                  RelicSlot& slot,
                                  const RelicHookContext& ctx) noexcept;

}  // namespace sts::engine
