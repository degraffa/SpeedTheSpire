#pragma once

// Native combat bodies for the STARTER-tier relics (registry/relics.yaml tier
// STARTER). Burning Blood, the Ironclad's starting relic, is the whole tier.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/relic_hooks.hpp"  // RelicHook, RelicHookContext
#include "sts/engine/run_state.hpp"    // RelicSlot

namespace sts::engine {

void relic_native_burning_blood(CombatState& s, RelicHook hook, RelicSlot& slot,
                                const RelicHookContext& ctx) noexcept;

}  // namespace sts::engine
