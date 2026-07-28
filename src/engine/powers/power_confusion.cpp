// Confusion -- native power-hook body. One translation unit per power; see
// power_native.hpp for the dispatch plumbing and power_confusion.hpp for what
// this power does.

#include "power_confusion.hpp"

#include <cstdint>

#include "../interp/interp_cards.hpp"  // randomize_card_cost (the shared roll)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_confusion(CombatState& s, Hook hook,
                            const HookContext& ctx) noexcept {
    // ConfusionPower.onCardDraw (ConfusionPower.java:38-48):
    //
    //   if (card.cost >= 0) {
    //       int newCost = AbstractDungeon.cardRandomRng.random(3);
    //       if (card.cost != newCost) { card.costForTurn = card.cost = newCost;
    //                                   card.isCostModified = true; }
    //       card.freeToPlayOnce = false;
    //   }
    //
    // The whole roll -- the unconditional draw inside the `cost >= 0` branch,
    // the XCOST/UNPLAYABLE exemption, the comparison against the BASE cost, and
    // the permanent (not this-turn) write -- lives in randomize_card_cost
    // (interp/interp_cards.hpp), which the four numbered notes at its definition
    // derive line by line. RandomizeHandCostAction (Snecko Oil) is the same body
    // with `clear_free_to_play_once` false; sharing it is deliberate, because a
    // hand-written second copy is exactly how the two diverged before.
    //
    // What is Confusion's ALONE is the `true` below: `card.freeToPlayOnce =
    // false` (:46) is the last line of the branch and sits OUTSIDE the
    // `card.cost != newCost` comparison, so a card that had been granted one
    // free play loses the grant even on a roll that changes nothing. The rest of
    // the equality branch writes nothing at all -- notably a live
    // COST_MODIFIED_FOR_TURN survives it, which is the divergence this call
    // site used to carry.
    //
    // Per-DRAW ordering is the DRAW opcode's: one onCardDraw per newly drawn
    // card, in draw order (interp.cpp), so a hand of Wounds and Slimeds moves
    // the stream not at all.
    if (hook != Hook::ON_CARD_DRAW || ctx.card_pool_index >= kCardPoolCap) {
        return;
    }
    randomize_card_cost(s, ctx.card_pool_index, /*clear_free_to_play_once=*/true);
}

}  // namespace sts::engine
