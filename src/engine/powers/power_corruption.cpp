// Corruption -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_corruption.hpp for what this power does.

#include "power_corruption.hpp"

#include "../interp/interp_cards.hpp"   // set_cost_for_turn (AbstractCard.setCostForTurn)
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
        // setCostForTurn(-9) (CorruptionPower.java:39-41 -> AbstractCard.java:
        // 2001-2011): assign (clamped at 0), and mark the instance
        // cost-modified-for-turn when the new value differs from the card's own
        // AbstractCard.cost, so the end-turn sweep restores it
        // (AbstractRoom.endTurn:397-405 -> resetAttributes:2035-2045). Without
        // the mark the free cost would outlive the turn.
        //
        // `!= this.cost` IS THE INSTANCE'S COST, NOT THE REGISTRY ROW, and that
        // distinction is the whole reason this calls the shared
        // set_cost_for_turn instead of writing the two fields by hand. Under
        // Corruption the ApplyPowerAction constructor has already run
        // modifyCostForCombat(-9) over the draw pile (ApplyPowerAction.java:
        // 54-57), so a drawn SKILL usually arrives with cost == 0 -- the game
        // then finds costForTurn == cost and leaves isCostModifiedForTurn
        // FALSE, and the card stays free for the rest of the combat. Reading
        // card_cost(registry) here instead saw a Defend_R at 1, set the
        // this-turn bit, and the very next reset_cost_for_turn (exhaust, or the
        // end-of-turn pass) handed the card back its registry cost.
        //
        // LIVE WITNESS: s2v3_wave1_STS239327_ps13
        // (run_STS239327_a20_ironclad.jsonl), floor 50, the Act-3 double boss.
        // Corruption+ is played at seq 594; the turn-3 hand at seq 595 shows two
        // Defends at 0 on both sides, and at seq 597 the first of them lands in
        // the exhaust pile -- `exhaust capture: Defend_R@0` against
        // `exhaust sim: Defend_R@1`, and the same for every later Defend
        // through seq 635.
        //
        // It also fixes the second field: set_cost_for_turn saves the instance's
        // real base in SAVED_BASE_COST when it is off the registry row, so a
        // Snecko-rolled SKILL drawn under Corruption reverts to the ROLLED cost
        // at end of turn rather than to the registry one.
        set_cost_for_turn(s, ctx.card_pool_index, 0);
    }
}

}  // namespace sts::engine
