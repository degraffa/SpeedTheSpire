// Lose Dexterity -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_lose_dexterity.hpp for what this power does.

#include "power_lose_dexterity.hpp"

#include <cstdint>
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_lose_dexterity(CombatState& s, Hook hook,
                                 const HookContext& ctx) noexcept {
    // LoseDexterityPower.atEndOfTurn (LoseDexterityPower.java:38-42): addToBot
    // ApplyPower(Dexterity, -amount) then RemoveSpecificPower(self) -- the
    // exact mirror of LoseStrength/Flex (id 13). Both queued (addToBot), so the
    // Dexterity reduction + self-removal resolve on later pump iterations.
    if (hook != Hook::AT_END_OF_TURN) {
        return;
    }
    ActionQueueItem down{};
    down.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    down.src = ctx.owner;
    down.tgt = ctx.owner;
    down.amount = -ctx.power_amount;  // Dexterity -amount
    down.flags = make_apply_power_flags(PowerId::DEXTERITY);
    add_to_bottom(s, down);
    ActionQueueItem rem{};
    rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
    rem.src = ctx.owner;
    rem.tgt = ctx.owner;
    rem.flags = make_apply_power_flags(PowerId::LOSE_DEXTERITY);
    add_to_bottom(s, rem);
}

}  // namespace sts::engine
