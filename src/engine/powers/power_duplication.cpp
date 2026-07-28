// Duplication -- native power-hook body. One translation unit per power; see
// power_native.hpp for the dispatch plumbing and power_duplication.hpp for
// what this power does.

#include "power_duplication.hpp"

#include <cstdint>

#include "../interp/interp_cards.hpp"   // op_play_card (the shared replay verb)
#include "power_native.hpp"             // find_power (the counter decrement)
#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, kPlayCard*, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_duplication(CombatState& s, Hook hook,
                              const HookContext& ctx) noexcept {
    // DuplicationPower.onUseCard (DuplicationPower.java:39-62): if the played
    // card is not itself a purge-on-use replay copy and the counter is still
    // positive -- makeSameInstanceOf it into limbo, set purgeOnUse, and
    // addCardQueueItem(copy, m, energyOnUse, ignoreEnergyTotal = true,
    // autoplay = true) in FRONT of the card queue; then --amount, removing the
    // power once the counter hits zero. Byte-for-byte Double Tap's replay
    // (DoubleTapPower.java:43-66, power_double_tap.cpp) with ONE guard fewer:
    // there is NO `card.type == ATTACK` clause -- ANY card type is replayed
    // (:40 is `!card.purgeOnUse && this.amount > 0` and nothing else). The
    // enqueue is SYNCHRONOUS -- it happens inside the UseCardAction
    // constructor (AbstractPlayer.useCard:1370) -- which is why this body
    // calls the shared PLAY_CARD verb directly instead of queueing an item.
    // calculateCardDamage(m) (:52-54) is a preview repaint; the replay's real
    // damage is recomputed when the copy resolves.
    if (hook == Hook::ON_USE_CARD) {
        if (ctx.power_amount <= 0 || ctx.card_pool_index >= kCardPoolCap) {
            return;
        }
        if (has_card_flag(s.card_pool[ctx.card_pool_index].flags,
                          CardFlag::PURGE_ON_USE)) {
            return;  // `!card.purgeOnUse` (:40) -- a replay is not re-replayed
        }
        op_play_card(s, ctx.target, static_cast<int>(ctx.card_pool_index),
                     kPlayCardCopy | kPlayCardPurge | kPlayCardQueueFront);
        PowerSlot* slot = find_power(s, ctx.owner, PowerId::DUPLICATION);
        if (slot == nullptr) {
            return;
        }
        slot->amount = static_cast<int16_t>(slot->amount - 1);  // --this.amount
        if (slot->amount == 0) {
            ActionQueueItem rem{};
            rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
            rem.src = ctx.owner;
            rem.tgt = ctx.owner;
            rem.flags = make_apply_power_flags(PowerId::DUPLICATION);
            add_to_bottom(s, rem);  // addToBot RemoveSpecificPowerAction (:59)
        }
        return;
    }
    // atEndOfRound (:65-71) -- NOT Double Tap's atEndOfTurn whole-power
    // removal: an unspent charge SURVIVES the turn it was drunk and decays by
    // one per round. `if (amount == 0) addToBot(Remove) else
    // addToBot(ReducePowerAction(owner, owner, POWER_ID, 1))`; the reduce of
    // 1-from-1 removes rather than reduces (ReducePowerAction.java:45-51), so
    // the potion's base amount 1 is gone after one round -- the g6b STS01221
    // witness (drunk seq 103, amount 1 at seq 104's `end`, absent by seq
    // 105). The amount == 0 branch is unreachable through this path -- the
    // onUseCard decrement queues the removal itself at zero -- and is
    // implemented anyway because the Java writes it, exactly as
    // power_duration_debuff does for the same branch.
    if (hook == Hook::AT_END_OF_ROUND) {
        ActionQueueItem item{};
        item.src = ctx.owner;
        item.tgt = ctx.owner;
        item.flags = make_apply_power_flags(PowerId::DUPLICATION);
        if (ctx.power_amount == 0) {
            item.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
            item.amount = 0;
        } else {
            item.opcode = static_cast<uint16_t>(Opcode::REDUCE_POWER);
            item.amount = 1;
        }
        add_to_bottom(s, item);
        return;
    }
}

}  // namespace sts::engine
