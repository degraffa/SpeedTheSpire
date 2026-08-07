// Hex -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and power_hex.hpp for what
// this power does.

#include "power_hex.hpp"

#include <cstdint>
#include "power_native.hpp"             // PowerNativeSig (the shared handler signature)
#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActor*
#include "sts/engine/cards.hpp"         // card_def, CardType (the ATTACK guard)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, CardPile, make_make_card_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_hex(CombatState& s, Hook hook,
                      const HookContext& ctx) noexcept {
    if (hook != Hook::ON_USE_CARD) {
        return;
    }
    // HexPower.onUseCard (HexPower.java:36-42):
    //
    //     if (card.type != AbstractCard.CardType.ATTACK) {
    //         this.flash();
    //         this.addToBot(new MakeTempCardInDrawPileAction(
    //             new Dazed(), this.amount, true, true));
    //     }
    //
    // The test is a bare `!= ATTACK`, so it is NOT "skills and powers": STATUS
    // and CURSE plays trigger it too, which is why this reads the card's real
    // type rather than testing for the two types a player usually plays.
    const CardDef* cd = card_def(static_cast<CardId>(ctx.card_id));
    if (cd == nullptr || cd->type == CardType::ATTACK) {
        return;  // the card-type guard (:38) -- the ONE thing that makes this native
    }
    // MakeTempCardInDrawPileAction(new Dazed(), amount, cardsToPreview=true,
    // randomSpot=true) -> CardGroup.addToRandomSpot per copy, which costs ONE
    // card_random_rng random(size-1) EACH and ZERO when the draw pile is empty
    // (the empty-group branch appends without drawing). op_make_card's
    // DRAW_RANDOM branch reproduces both, inside its own per-copy loop, so the
    // count of copies rides in `amount` and the stream accounting is the
    // opcode's, not this body's.
    ActionQueueItem mk{};
    mk.opcode = static_cast<uint16_t>(Opcode::MAKE_CARD);
    mk.src = static_cast<uint8_t>(CardPile::DRAW_RANDOM);  // the destination pile
    mk.tgt = kActorPlayer;  // unused by MAKE_CARD; must not read as a fan-out sentinel
    mk.amount = ctx.power_amount;  // `this.amount` -- the Hex stack (:40)
    mk.flags = make_make_card_flags(static_cast<uint16_t>(CardId::DAZED));
    add_to_bottom(s, mk);  // addToBot (:40)
}

}  // namespace sts::engine
