// Double Tap -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_double_tap.hpp for what this power does.

#include "power_double_tap.hpp"

#include <cstdint>

#include "../interp/interp_cards.hpp"   // op_play_card (the shared replay verb)
#include "power_native.hpp"             // find_power (the counter decrement)
#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActor*
#include "sts/engine/cards.hpp"         // card_def, CardType (the ATTACK guard)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, kPlayCard*, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_double_tap(CombatState& s, Hook hook,
                             const HookContext& ctx) noexcept {
    // DoubleTapPower.onUseCard (DoubleTapPower.java:43-66): if the played card is
    // an ATTACK, is not itself a purge-on-use replay copy, and the counter is
    // still positive -- makeSameInstanceOf it into limbo, set purgeOnUse, and
    // addCardQueueItem(copy, m, energyOnUse, ignoreEnergyTotal = true, autoplay =
    // true) in FRONT of the card queue; then --amount, removing the power once the
    // counter hits zero. The enqueue is SYNCHRONOUS -- it happens inside the
    // UseCardAction constructor (AbstractPlayer.useCard:1370), not through a
    // queued action -- which is why this body calls the shared PLAY_CARD verb
    // directly instead of queueing an item.
    if (hook == Hook::ON_USE_CARD) {
        if (ctx.power_amount <= 0 || ctx.card_pool_index >= kCardPoolCap) {
            return;
        }
        const CardDef* cd = card_def(static_cast<CardId>(ctx.card_id));
        if (cd == nullptr || cd->type != CardType::ATTACK) {
            return;  // `card.type == CardType.ATTACK` (:44)
        }
        if (has_card_flag(s.card_pool[ctx.card_pool_index].flags,
                          CardFlag::PURGE_ON_USE)) {
            return;  // `!card.purgeOnUse` (:44) -- a replay is not itself replayed
        }
        op_play_card(s, ctx.target, static_cast<int>(ctx.card_pool_index),
                     kPlayCardCopy | kPlayCardPurge | kPlayCardQueueFront);
        PowerSlot* slot = find_power(s, ctx.owner, PowerId::DOUBLE_TAP);
        if (slot == nullptr) {
            return;
        }
        slot->amount = static_cast<int16_t>(slot->amount - 1);  // --this.amount
        if (slot->amount == 0) {
            ActionQueueItem rem{};
            rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
            rem.src = ctx.owner;
            rem.tgt = ctx.owner;
            rem.flags = make_apply_power_flags(PowerId::DOUBLE_TAP);
            add_to_bottom(s, rem);  // addToBot RemoveSpecificPowerAction (:63)
        }
        return;
    }
    // atEndOfTurn (:68-73): a player owner queues its own removal, so an unspent
    // charge does not survive the turn.
    if (hook == Hook::AT_END_OF_TURN) {
        if (ctx.owner != kActorPlayer) {
            return;  // `if (isPlayer)` (:70)
        }
        ActionQueueItem rem{};
        rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
        rem.src = ctx.owner;
        rem.tgt = ctx.owner;
        rem.flags = make_apply_power_flags(PowerId::DOUBLE_TAP);
        add_to_bottom(s, rem);
        return;
    }
}

}  // namespace sts::engine
