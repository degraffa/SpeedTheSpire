// B3.25 relic-applied power (Self-Forming Clay) -- native hook body (moved
// verbatim out of power_hooks.cpp's escape-hatch switch; see power_native.hpp
// for the split's rationale).

#include "powers_b3_25.hpp"

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_next_turn_block(CombatState& s, Hook hook,
                                  const HookContext& ctx) noexcept {
    // NextTurnBlockPower.atStartOfTurn (NextTurnBlockPower.java:44-50):
    // addToBot GainBlockAction(owner, amount) then addToBot
    // RemoveSpecificPowerAction(self). A direct GainBlockAction -> no
    // Dexterity (kBlockNoPowers); both queued, so the block lands AFTER
    // start_of_turn's synchronous block decay -- exactly the game's
    // "gain Block at the start of your next turn" (Self-Forming Clay).
    if (hook != Hook::AT_START_OF_TURN) {
        return;
    }
    ActionQueueItem blk{};
    blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
    blk.src = ctx.owner;
    blk.tgt = ctx.owner;
    blk.amount = ctx.power_amount;
    blk.flags = kBlockNoPowers;
    add_to_bottom(s, blk);
    ActionQueueItem rem{};
    rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
    rem.src = ctx.owner;
    rem.tgt = ctx.owner;
    rem.flags = make_apply_power_flags(PowerId::NEXT_TURN_BLOCK);
    add_to_bottom(s, rem);
}

}  // namespace sts::engine
