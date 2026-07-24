#pragma once

// Native bodies for the B3.6 red-uncommon-skill powers (registry/powers.yaml
// ids 24-25, "--- B3.6 red-uncommon-skill powers (ids 24-25) ---"). No Draw
// (id 24) is a pure marker power read by other bodies, so it has no entry here.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_flame_barrier(CombatState& s, Hook hook,
                                const HookContext& ctx) noexcept;

}  // namespace sts::engine
