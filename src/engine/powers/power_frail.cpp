// Frail -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_frail.hpp for what this power does.

#include "power_frail.hpp"

#include "power_duration_debuff.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_frail(CombatState& s, Hook hook,
                        const HookContext& ctx) noexcept {
    // FrailPower.atEndOfRound (FrailPower.java:40-49). Identical to Vulnerable's
    // and Weak's, so it shares their body -- including the OWNER generality the
    // former CombatState.flags latch could not express: a monster-owned Frail now
    // ticks like the player's.
    duration_debuff_at_end_of_round(s, hook, ctx, PowerId::FRAIL);
}

}  // namespace sts::engine
