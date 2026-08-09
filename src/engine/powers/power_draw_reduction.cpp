// Draw Reduction -- native power-hook body. One translation unit per power; see
// power_native.hpp for the dispatch plumbing and power_draw_reduction.hpp for
// why only one of the class's three overrides is a hook.

#include "power_draw_reduction.hpp"

#include <cstdint>

#include "power_native.hpp"             // PowerNativeSig, actor_power_list
#include "sts/engine/action_queue.hpp"  // add_to_bottom
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_draw_reduction(CombatState& s, Hook hook,
                                 const HookContext& ctx) noexcept {
    if (hook != Hook::AT_END_OF_ROUND) {
        return;
    }
    // Read the responding slot BY INDEX. Draw Reduction is single-instance in
    // practice (only Head Slam applies it, and a second application stacks the
    // live slot), but ctx.power_slot is the contract for "which instance is
    // speaking" and costs nothing.
    const PowerListView pv = actor_power_list(s, ctx.owner);
    if (pv.slots == nullptr || ctx.power_slot >= pv.count) {
        return;
    }
    PowerSlot& slot = pv.slots[ctx.power_slot];

    // `if (this.justApplied) { this.justApplied = false; return; }` (:37-41) --
    // the round the debuff arrived is spent clearing the latch, not decrementing.
    if (slot.counter != 0) {
        slot.counter = 0;
        return;
    }
    // `addToBot(new ReducePowerAction(owner, owner, POWER_ID, 1))` (:44).
    // ReducePowerAction REMOVES rather than reduces when the request meets the
    // stack (ReducePowerAction.java:45-51), and op_reduce_power routes that
    // through remove_slot_at -- which is what gives the derived game_hand_size()
    // its `++gameHandSize` back without an onRemove body.
    ActionQueueItem reduce{};
    reduce.opcode = static_cast<uint16_t>(Opcode::REDUCE_POWER);
    reduce.src = ctx.owner;
    reduce.tgt = ctx.owner;
    reduce.amount = 1;
    reduce.flags = make_apply_power_flags(PowerId::DRAW_REDUCTION);
    add_to_bottom(s, reduce);
}

}  // namespace sts::engine
