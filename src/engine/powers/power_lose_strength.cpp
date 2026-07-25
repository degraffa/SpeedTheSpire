// Lose Strength -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_lose_strength.hpp for what this power does.

#include "power_lose_strength.hpp"

#include <cstdint>
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_lose_strength(CombatState& s, Hook hook,
                                const HookContext& ctx) noexcept {
    // LoseStrengthPower.atEndOfTurn (LoseStrengthPower.java:40-44): at end
    // of turn, addToBot ApplyPower(Strength, -amount) then
    // RemoveSpecificPower(self) -- the "temporary Strength" reversal (Flex).
    // Both queued (addToBot), so the Strength reduction and the self-removal
    // resolve on later pump iterations, NOT during this power-list walk.
    if (hook != Hook::AT_END_OF_TURN) {
        return;
    }
    ActionQueueItem down{};
    down.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    down.src = ctx.owner;
    down.tgt = ctx.owner;
    down.amount = -ctx.power_amount;  // Strength -amount
    down.flags = make_apply_power_flags(PowerId::STRENGTH);
    add_to_bottom(s, down);
    ActionQueueItem rem{};
    rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
    rem.src = ctx.owner;
    rem.tgt = ctx.owner;
    rem.flags = make_apply_power_flags(PowerId::LOSE_STRENGTH);
    add_to_bottom(s, rem);
}

}  // namespace sts::engine
