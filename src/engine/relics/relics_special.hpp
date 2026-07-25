#pragma once

// Native bodies for the SPECIAL-tier relics (registry/relics.yaml tier SPECIAL,
// the Act-1-event-reachable set). The generated dispatch table
// (STS_REGISTRY_NATIVE_RELICS) odr-uses a handler for EVERY `native: true` row,
// so a forgotten body is a link error, not a silent no-op.
//
// SPECIAL relics belong to NO dungeon pool: they are granted by an Act-1 event or
// by Neow, so unlike every other tier their row set is not relicRng-visible. What
// makes them worth registering now is the translator join key and the combat
// behaviour of the three that have any.
//
// One of this tier's bodies is DEFERRED and is an EXPLICIT EMPTY function in
// relics_special.cpp rather than an omission -- Warped Tongs, whose
// UpgradeRandomCardAction is shuffleRng-consuming and has no opcode. Its reason
// and citation sit at the definition.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/relic_hooks.hpp"  // RelicHook, RelicHookContext
#include "sts/engine/run_state.hpp"    // RelicSlot

namespace sts::engine {

void relic_native_neows_lament(CombatState& s, RelicHook hook, RelicSlot& slot,
                               const RelicHookContext& ctx) noexcept;
void relic_native_face_of_cleric(CombatState& s, RelicHook hook, RelicSlot& slot,
                                 const RelicHookContext& ctx) noexcept;

// --- DEFERRED combat body (explicit empty definition) ------------------------
void relic_native_warped_tongs(CombatState& s, RelicHook hook, RelicSlot& slot,
                               const RelicHookContext& ctx) noexcept;

}  // namespace sts::engine
