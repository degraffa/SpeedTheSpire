// Time Warp -- native power-hook body. One translation unit per power; see
// power_native.hpp for the dispatch plumbing and power_time_warp.hpp for what
// this power does and why the turn end is inline.

#include "power_time_warp.hpp"

#include <cstdint>

#include "power_native.hpp"             // PowerNativeSig, actor_power_list
#include "sts/engine/action_queue.hpp"  // add_to_bottom
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, execute_opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_time_warp(CombatState& s, Hook hook,
                            const HookContext& ctx) noexcept {
    if (hook != Hook::ON_AFTER_USE_CARD) {
        return;
    }
    // The responding slot is named by INDEX, not by id: `this.amount` is the
    // instance's own number, and ctx.power_slot is what says which instance is
    // speaking (power_hooks.hpp). Nothing in the game grants a second Time Warp,
    // but reading the slot by index costs nothing and is the contract.
    const PowerListView pv = actor_power_list(s, ctx.owner);
    if (pv.slots == nullptr || ctx.power_slot >= pv.count) {
        return;
    }
    PowerSlot& slot = pv.slots[ctx.power_slot];

    // `++this.amount;` (:60) -- SYNCHRONOUS, every card, no filter of any kind.
    // Purged cards, POWERs, status and curse plays all count: the hook fires once
    // per UseCardAction.update, which is once per play.
    slot.amount = static_cast<int16_t>(slot.amount + 1);
    if (slot.amount != static_cast<int16_t>(kTimeWarpCountdown)) {
        return;  // `if (this.amount == 12)` (:61)
    }
    slot.amount = 0;  // (:62)

    // callEndTurnEarlySequence() (:63) -- SYNCHRONOUS, and it must be: it clears
    // the pending card plays, so anything the player queued behind this card must
    // not resolve. Executed through the opcode's own body rather than duplicated;
    // see the header for why this is not add_to_bottom.
    ActionQueueItem end{};
    end.opcode = static_cast<uint16_t>(Opcode::END_PLAYER_TURN);
    execute_opcode(s, end);

    // `for (AbstractMonster m : AbstractDungeon.getMonsters().monsters)
    //      this.addToBot(new ApplyPowerAction(m, m, new StrengthPower(m, 2), 2));`
    // (:66-67). NO liveness guard -- every RECORD gets an item, in group order,
    // and a dead one's item no-ops when ApplyPowerAction.update reads
    // isDeadOrEscaped at resolve time. The Time Eater is solo, so in practice
    // this is a one-element loop; the general walk is written because the shape
    // is shared and because "solo" is an encounter fact, not a class fact.
    for (uint8_t m = 0; m < s.monster_count && m < kMonsterCap; ++m) {
        ActionQueueItem apply{};
        apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        apply.src = m;  // ApplyPowerAction(m, m, ...) -- the monster is its own source
        apply.tgt = m;
        apply.amount = kTimeWarpStrength;
        apply.flags = make_apply_power_flags(PowerId::STRENGTH);
        add_to_bottom(s, apply);
    }
}

}  // namespace sts::engine
