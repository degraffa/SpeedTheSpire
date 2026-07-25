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
        // card.exhaustOnUseOnce: mark this instance to exhaust on play.
        s.card_pool[ctx.card_pool_index].flags |=
            card_flag_bit(CardFlag::EXHAUST);
    } else if (hook == Hook::ON_CARD_DRAW) {
        s.card_pool[ctx.card_pool_index].cost_now = 0;  // setCostForTurn(0)
    }
}

}  // namespace sts::engine
