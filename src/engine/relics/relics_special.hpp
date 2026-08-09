#pragma once

// Native bodies for the SPECIAL-tier relics (registry/relics.yaml tier SPECIAL,
// the Act-1-event-reachable set). The generated dispatch table
// (STS_REGISTRY_NATIVE_RELICS) odr-uses a handler for EVERY `native: true` row,
// so a forgotten body is a link error, not a silent no-op.
//
// SPECIAL relics belong to NO dungeon pool: they are granted by an event or by
// Neow, so unlike every other tier their row set is not relicRng-visible.
//
// (This header used to describe Warped Tongs as this tier's one DEFERRED empty
// body; that went stale when Opcode::UPGRADE_RANDOM_CARD landed and its body
// went live -- the conventions §8 "comment asserting X" shape, corrected by
// S2.34, which also closed the tier's remaining deferrals: nothing in this
// tier is deferred any more.)

#include "sts/engine/combat_state.hpp"
#include "sts/engine/relic_hooks.hpp"  // RelicHook, RelicHookContext
#include "sts/engine/run_state.hpp"    // RelicSlot

namespace sts::engine {

void relic_native_neows_lament(CombatState& s, RelicHook hook, RelicSlot& slot,
                               const RelicHookContext& ctx) noexcept;
void relic_native_face_of_cleric(CombatState& s, RelicHook hook, RelicSlot& slot,
                                 const RelicHookContext& ctx) noexcept;
void relic_native_gremlin_mask(CombatState& s, RelicHook hook, RelicSlot& slot,
                               const RelicHookContext& ctx) noexcept;
void relic_native_red_mask(CombatState& s, RelicHook hook, RelicSlot& slot,
                           const RelicHookContext& ctx) noexcept;
void relic_native_warped_tongs(CombatState& s, RelicHook hook, RelicSlot& slot,
                               const RelicHookContext& ctx) noexcept;

// --- S2.34: the payout-relic combat bodies -----------------------------------
void relic_native_enchiridion(CombatState& s, RelicHook hook, RelicSlot& slot,
                              const RelicHookContext& ctx) noexcept;
void relic_native_nilrys_codex(CombatState& s, RelicHook hook, RelicSlot& slot,
                               const RelicHookContext& ctx) noexcept;
void relic_native_necronomicon(CombatState& s, RelicHook hook, RelicSlot& slot,
                               const RelicHookContext& ctx) noexcept;
void relic_native_mutagenic_strength(CombatState& s, RelicHook hook,
                                     RelicSlot& slot,
                                     const RelicHookContext& ctx) noexcept;

}  // namespace sts::engine
