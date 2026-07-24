#pragma once

// Native relic-hook plumbing (internal to src/engine -- NOT public API). The
// native escape hatch used to be one 380-line switch in relic_hooks.cpp; it is
// now per-batch translation units under src/engine/relics/ plus the dispatch
// table in relic_hooks.cpp, mirroring the monster_dispatch.cpp split (per-monster
// TUs + a switch returning function pointers, `default: return nullptr` for the
// not-yet-implemented ids).
//
// Each native relic's body is a free function with the RelicNativeFn signature,
// declared in its batch header (relics_b3_24.hpp, relics_b3_25.hpp, ...). Adding
// a relic batch (B3.26 rares+shop, B3.27 boss+specials) is: one new .cpp, one new
// header, one CMakeLists line, one table line -- no edits to the files an earlier
// batch owns.

#include <cstdint>

#include "sts/engine/combat_state.hpp"  // CombatState
#include "sts/engine/relic_hooks.hpp"   // RelicHook, RelicHookContext
#include "sts/engine/run_state.hpp"     // RelicSlot
#include "sts/engine/types.hpp"         // RelicId

namespace sts::engine {

// One native relic's hook body. `slot` is the responding relic's live slot
// (counter mutation writes here); the body inspects `hook` and returns without
// queuing anything for the hooks it does not answer.
using RelicNativeFn = void (*)(CombatState& s, RelicHook hook, RelicSlot& slot,
                               const RelicHookContext& ctx) noexcept;

// The dispatch table (relic_hooks.cpp): the native body for `id`, or nullptr for
// a relic whose combat body is DEFERRED / an unrecognized id.
[[nodiscard]] RelicNativeFn relic_native_fn(RelicId id) noexcept;

// Heal the player by `n`, clamped to max HP (HealAction semantics). No HEAL opcode
// exists (and none is added for B3.24); a pure heal has no queue-ordering interplay
// with other S1 relic effects, so it is applied directly at dispatch time.
inline void heal_player(CombatState& s, int32_t n) noexcept {
    int32_t hp = static_cast<int32_t>(s.player_hp) + n;
    if (hp > s.player_max_hp) {
        hp = s.player_max_hp;
    }
    s.player_hp = static_cast<int16_t>(hp);
}

}  // namespace sts::engine
