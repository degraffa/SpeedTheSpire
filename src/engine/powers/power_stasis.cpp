// Stasis -- native power-hook body. One translation unit per power; see
// power_native.hpp for the dispatch plumbing and power_stasis.hpp for what this
// power does and why it is native.

#include "power_stasis.hpp"

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // add_to_bottom, ActionQueueItem, kActorPlayer
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_stasis_return_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_stasis(CombatState& s, Hook hook,
                         const HookContext& ctx) noexcept {
    // StasisPower.onDeath (StasisPower.java:38-44).
    if (hook != Hook::ON_DEATH) {
        return;
    }
    // The slot's counter carries the stolen pool index + 1. Zero means the
    // power is holding nothing, which no real apply writes (APPLY_STASIS only
    // applies the power after a successful theft) -- defensive, not a branch
    // the Java has.
    if (ctx.power_counter <= 0 ||
        ctx.power_counter > static_cast<int16_t>(kCardPoolCap)) {
        return;
    }
    const int pool_index = static_cast<int>(ctx.power_counter) - 1;
    // `player.hand.size() != 10` (:39) is read HERE, at queue time; the hand
    // arm's resolve-time spill lives in op_stasis_return. addToBot in the Java,
    // add_to_bottom here.
    const bool to_hand = s.hand_count != kHandCap;
    ActionQueueItem ret{};
    ret.opcode = static_cast<uint16_t>(Opcode::STASIS_RETURN);
    ret.src = ctx.owner;
    ret.tgt = kActorPlayer;
    ret.amount = pool_index;
    ret.flags = make_stasis_return_flags(to_hand);
    add_to_bottom(s, ret);
}

}  // namespace sts::engine
