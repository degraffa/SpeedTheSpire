// Intangible (monster) -- native power-hook body. One translation unit per
// power; see power_native.hpp for the dispatch plumbing and
// power_intangible_monster.hpp for the two-class split this row exists for.

#include "power_intangible_monster.hpp"

#include <cstdint>

#include "power_native.hpp"             // PowerNativeSig, actor_power_list
#include "sts/engine/action_queue.hpp"  // add_to_bottom
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_intangible_monster(CombatState& s, Hook hook,
                                     const HookContext& ctx) noexcept {
    if (hook != Hook::AT_END_OF_TURN) {
        return;
    }
    const PowerListView pv = actor_power_list(s, ctx.owner);
    if (pv.slots == nullptr || ctx.power_slot >= pv.count) {
        return;
    }
    PowerSlot& slot = pv.slots[ctx.power_slot];

    // `if (this.justApplied) { this.justApplied = false; return; }` (:56-59) --
    // the turn the power arrived is spent clearing the latch, which is what
    // makes a 1-turn Intangible actually cover the player's next turn.
    if (slot.counter != 0) {
        slot.counter = 0;
        return;
    }
    // `if (this.amount == 0) addToBot(RemoveSpecificPowerAction(...)); else
    // addToBot(ReducePowerAction(..., 1));` (:61-65). BOTH ARMS ARE SPELLED.
    // They agree at the Nemesis's amount 1 -- op_reduce_power's reduce-to-zero
    // already routes through remove_slot_at -- but the Java distinguishes them
    // and a zero-amount instance is reachable in principle (nothing here forbids
    // an `IntangiblePower(owner, 0)`), so the 0 arm is implemented rather than
    // asserted away. This is where id 29's row DOES collapse the two, and the
    // difference between the rows is deliberate.
    ActionQueueItem it{};
    it.src = ctx.owner;
    it.tgt = ctx.owner;
    it.flags = make_apply_power_flags(PowerId::INTANGIBLE_MONSTER);
    if (slot.amount == 0) {
        it.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
    } else {
        it.opcode = static_cast<uint16_t>(Opcode::REDUCE_POWER);
        it.amount = 1;
    }
    add_to_bottom(s, it);
}

}  // namespace sts::engine
