#pragma once

// Native bodies for the UNCOMMON-tier relics (registry/relics.yaml tier
// UNCOMMON). Every row in this tier now has a real combat body. The generated
// dispatch table (STS_REGISTRY_NATIVE_RELICS) odr-uses a handler for EVERY
// `native: true` row, so a forgotten body is a link error, not a silent no-op.
//
// Pantograph was this tier's last DEFERRED body, and is deferred no longer:
// registry/monsters.yaml now carries the `enemy_type` column (AbstractMonster
// .type, AbstractMonster.java:99), so "is this a boss fight?" is a data question
// answered by MonsterDef::is_boss(). See the body in relics_uncommon.cpp for the
// Java it mirrors and for why the test is on the MONSTER, not on the room.

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

void relic_native_mummified_hand(CombatState& s, RelicHook hook, RelicSlot& slot,
                                 const RelicHookContext& ctx) noexcept;

void relic_native_pantograph(CombatState& s, RelicHook hook, RelicSlot& slot,
                             const RelicHookContext& ctx) noexcept;

}  // namespace sts::engine
