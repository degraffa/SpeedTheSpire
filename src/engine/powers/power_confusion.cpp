// Confusion -- native power-hook body. One translation unit per power; see
// power_native.hpp for the dispatch plumbing and power_confusion.hpp for what
// this power does.

#include "power_confusion.hpp"

#include <cstdint>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/rng_stream.hpp"  // random (INCLUSIVE bound -- stage-a trap 3)
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
    // Three things about that body are load-bearing and easy to get wrong:
    //
    // (1) THE DRAW IS UNCONDITIONAL INSIDE THE cost >= 0 BRANCH. It happens
    //     BEFORE the `card.cost != newCost` comparison, so a card that rolls its
    //     own current cost still consumes exactly one cardRandomRng draw. One
    //     draw per qualifying card, in DRAW ORDER (the DRAW opcode fires
    //     onCardDraw per newly-drawn card, in order -- interp.cpp).
    // (2) `card.cost < 0` cards consume NO draw. In the Java those are the
    //     X-cost cards (cost -1) and the unplayable status/curse rows
    //     (cost < -1); here that is exactly the XCOST / UNPLAYABLE instance
    //     flags, which the generator sets from those same negative sentinels
    //     (stsgen/emit/cards.py parse_card_flags) and which nothing ever clears.
    //     So a hand of Wounds and Slimeds moves the stream not at all.
    // (3) It writes card.cost, not just costForTurn -- the new cost is PERMANENT
    //     for that instance, not this-turn-only. cost_now is therefore written
    //     WITHOUT setting COST_MODIFIED_FOR_TURN, and any such bit already on the
    //     instance is CLEARED: after this assignment the Java's cost and
    //     costForTurn are equal, so its end-of-turn resetAttributes is a no-op,
    //     and leaving the bit set would make the engine's end-of-turn sweep
    //     (action_queue.cpp -> reset_cost_for_turn) restore the REGISTRY cost and
    //     silently undo Confusion.
    if (hook != Hook::ON_CARD_DRAW || ctx.card_pool_index >= kCardPoolCap) {
        return;
    }
    CardInstance& c = s.card_pool[ctx.card_pool_index];
    if (has_card_flag(c.flags, CardFlag::XCOST) ||
        has_card_flag(c.flags, CardFlag::UNPLAYABLE)) {
        return;  // card.cost < 0: no draw, no change
    }
    const int32_t new_cost = random(s.card_random_rng, 3);  // random(3) == 0..3
    c.cost_now = static_cast<uint8_t>(new_cost);
    c.flags = static_cast<uint16_t>(
        c.flags & ~card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN));
}

}  // namespace sts::engine
