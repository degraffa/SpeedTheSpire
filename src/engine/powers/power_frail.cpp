// Frail -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_frail.hpp for what this power does.

#include "power_frail.hpp"

#include <cstdint>
#include "power_native.hpp"             // find_power
#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_frail(CombatState& s, Hook hook,
                        const HookContext& ctx) noexcept {
    // FrailPower.atEndOfRound (FrailPower.java:42-54): a newly-created,
    // monster-sourced player instance consumes justApplied without losing
    // duration. Later rounds reduce one stack and remove at zero. This
    // dispatcher runs after monster turns and before next-turn setup.
    if (hook != Hook::AT_END_OF_ROUND || ctx.owner != kActorPlayer) {
        return;
    }
    PowerSlot* fp = find_power(s, kActorPlayer, PowerId::FRAIL);
    if (fp == nullptr) {
        s.flags &= ~kCombatFlagFrailJustApplied;
        return;
    }
    if ((s.flags & kCombatFlagFrailJustApplied) != 0u) {
        s.flags &= ~kCombatFlagFrailJustApplied;
        return;
    }
    ActionQueueItem reduce{};
    reduce.opcode = static_cast<uint16_t>(Opcode::REDUCE_POWER);
    reduce.src = ctx.owner;
    reduce.tgt = ctx.owner;
    reduce.amount = 1;
    reduce.flags = make_apply_power_flags(PowerId::FRAIL);
    add_to_bottom(s, reduce);  // ReducePowerAction, FrailPower.java:52
}

}  // namespace sts::engine
