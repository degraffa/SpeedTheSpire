// Corruption -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_corruption.hpp for what this power does.

#include "power_corruption.hpp"

#include "sts/engine/cards.hpp"         // card_def, CardType (Corruption skill check)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_corruption(CombatState& s, Hook hook,
                             const HookContext& ctx) noexcept {
    // CorruptionPower: a played SKILL is redirected to exhaust (onUseCard);
    // a drawn SKILL costs 0 this turn (onCardDraw). Both key off the card
    // being a SKILL. The pool index identifies the specific instance.
    if (ctx.card_pool_index >= kCardPoolCap) {
        return;
    }
    const CardId cid = static_cast<CardId>(ctx.card_id);
    const CardDef* cd = card_def(cid);
    if (cd == nullptr || cd->type != CardType::SKILL) {
        return;
    }
    if (hook == Hook::ON_USE_CARD) {
        // Corruption mutates UseCardAction.exhaustCard, not card.exhaust
        // (CorruptionPower.java:45-49). EXHAUST_ON_USE_ONCE is the action-local
        // lifetime mirror: op_use_card clears it after this filing, including
        // when Strange Spoon redirects the card to discard.
        s.card_pool[ctx.card_pool_index].flags |=
            card_flag_bit(CardFlag::EXHAUST_ON_USE_ONCE);
    } else if (hook == Hook::ON_CARD_DRAW) {
        // setCostForTurn(0) (AbstractCard.java:2001-2011): assign, and mark the
        // instance cost-modified-for-turn when the new value differs from the
        // card's own base cost, so the end-turn sweep restores it
        // (AbstractRoom.endTurn:397-405 -> resetAttributes:2035-2045). Without the
        // mark the free cost would outlive the turn.
        CardInstance& c = s.card_pool[ctx.card_pool_index];
        c.cost_now = 0;
        if (card_cost(*cd, c.upgrade) != 0) {
            c.flags = static_cast<uint16_t>(
                c.flags | card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN));
        }
    }
}

}  // namespace sts::engine
