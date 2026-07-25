#pragma once

// Curl Up -- native power-hook body (registry/powers.yaml id 20,
// PowerId::CURL_UP).
//
// The first non-lethal NORMAL attack gains the owner `amount` Block and
// removes the power -- once per combat.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_curl_up(CombatState& s, Hook hook,
                          const HookContext& ctx) noexcept;

}  // namespace sts::engine
