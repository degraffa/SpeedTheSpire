// Slow -- native power-hook body. One translation unit per power; see
// power_native.hpp for the dispatch plumbing and power_slow.hpp for why the
// reset is what makes this native.

#include "power_slow.hpp"

#include <cstdint>

#include "power_native.hpp"             // PowerNativeSig, actor_power_list
#include "sts/engine/action_queue.hpp"  // add_to_bottom
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_slow(CombatState& s, Hook hook,
                       const HookContext& ctx) noexcept {
    if (hook == Hook::ON_AFTER_USE_CARD) {
        // SlowPower.onAfterUseCard (SlowPower.java:51-54):
        //
        //     this.addToBot(new ApplyPowerAction(this.owner, this.owner,
        //                       new SlowPower(this.owner, 1), 1));
        //
        // NO card-type test, NO owner test and NO amount test -- every play,
        // including a Status or a Curse, stacks one. Routed through APPLY_POWER
        // rather than written into the slot so the priority re-sort and the
        // Artifact / onApplyPower interception stay on the path: Slow is a
        // DEBUFF, so a monster holding Artifact really would eat the stack, and
        // that door should be the same door for every debuff.
        ActionQueueItem apply{};
        apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        apply.src = ctx.owner;  // ApplyPowerAction(owner, owner, ...)
        apply.tgt = ctx.owner;
        apply.amount = 1;
        apply.flags = make_apply_power_flags(PowerId::SLOW);
        add_to_bottom(s, apply);
        return;
    }
    if (hook != Hook::AT_END_OF_ROUND) {
        return;
    }
    // SlowPower.atEndOfRound (:38-41) is `this.amount = 0; updateDescription();`
    // and NOTHING ELSE. A SYNCHRONOUS write to the live object, with no action
    // queued and no removal -- so the slot survives at zero and the next card
    // takes it straight back to 1. Written by slot INDEX (ctx.power_slot) rather
    // than by id, which is the contract for "which instance is speaking".
    const PowerListView pv = actor_power_list(s, ctx.owner);
    if (pv.slots == nullptr || ctx.power_slot >= pv.count) {
        return;
    }
    pv.slots[ctx.power_slot].amount = 0;
}

}  // namespace sts::engine
