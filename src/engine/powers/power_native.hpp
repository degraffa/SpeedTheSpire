#pragma once

// Native power-hook plumbing (internal to src/engine -- NOT public API). The
// native escape hatch used to be one 500-line switch in power_hooks.cpp; it is
// now ONE TRANSLATION UNIT PER POWER under src/engine/powers/ plus the dispatch
// table in power_hooks.cpp, mirroring the monster_dispatch.cpp split (per-monster
// TUs + a switch returning function pointers, `default: return nullptr` for the
// not-yet-implemented ids).
//
// Each native power's body is a free function with the PowerNativeFn signature,
// declared in the header named for that power (power_combust.hpp declares
// power_native_combust, and so on). The dispatch table itself is GENERATED from
// registry/powers.yaml (the STS_REGISTRY_NATIVE_POWERS X-macro in the generated
// power_table.hpp), so adding a power is: one new .cpp, one new header, one
// CMakeLists line -- and NO edit to power_hooks.cpp or to any other power's
// files.

#include <cstdint>

#include "sts/engine/combat_state.hpp"  // CombatState, PowerSlot, kMonsterCap
#include "sts/engine/power_hooks.hpp"   // Hook, HookContext
#include "sts/engine/types.hpp"         // PowerId

namespace sts::engine {

// One native power's hook body. `ctx.owner` is the power's owner actor and
// `ctx.power_amount` its stack amount; the body inspects `hook` and returns
// without queuing anything for the hooks it does not answer.
//
// PowerNativeSig is the FUNCTION type (PowerNativeFn is a pointer to it); the
// generated dispatch table declares each handler as `extern PowerNativeSig fn;`,
// which both spells the declaration once and pins every native body to exactly
// this signature (a mismatched definition mangles differently -> link error).
using PowerNativeSig = void(CombatState& s, Hook hook,
                            const HookContext& ctx) noexcept;
using PowerNativeFn = PowerNativeSig*;

// The dispatch table (power_hooks.cpp, generated from registry/powers.yaml): the
// native body for `id`, or nullptr for a non-native / unrecognized id. A native
// row with a deliberately empty body (Artifact) maps to that empty body, not to
// nullptr -- see the STS_REGISTRY_NATIVE_POWERS expansion in power_hooks.cpp.
[[nodiscard]] PowerNativeFn power_native_fn(PowerId id) noexcept;

// The power-slot list for an actor (kActorPlayer -> player_powers; a monster slot
// -> that monster's powers). Out-of-range -> empty.
struct PowerListView {
    PowerSlot* slots;
    uint8_t count;
};
[[nodiscard]] inline PowerListView actor_power_list(CombatState& s,
                                                    uint8_t actor) noexcept {
    if (actor == kActorPlayer) {
        return PowerListView{s.player_powers, s.player_power_count};
    }
    if (actor < kMonsterCap) {
        return PowerListView{s.monsters[actor].powers, s.monsters[actor].power_count};
    }
    return PowerListView{nullptr, 0};
}

// Does `actor` currently hold `pid` (with a live slot)? Also returns the slot for
// mutation (Artifact consume).
[[nodiscard]] inline PowerSlot* find_power(CombatState& s, uint8_t actor,
                                           PowerId pid) noexcept {
    const PowerListView pv = actor_power_list(s, actor);
    for (uint8_t i = 0; i < pv.count; ++i) {
        if (pv.slots[i].power_id == static_cast<uint16_t>(pid)) {
            return &pv.slots[i];
        }
    }
    return nullptr;
}

}  // namespace sts::engine
