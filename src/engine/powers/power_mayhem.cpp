// Mayhem -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and power_mayhem.hpp for what
// this power does.

#include "power_mayhem.hpp"

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActorRandomEnemy
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, kPlayCardFromDrawTop

namespace sts::engine {

// MayhemPower.atStartOfTurn (MayhemPower.java:33-39):
//
//     this.flash();
//     for (int i = 0; i < this.amount; ++i) {
//         this.addToBot(new /* Unavailable Anonymous Inner Class!! */);
//     }
//
// EVIDENCE GAP, stated rather than papered over: CFR could not decompile the
// queued action -- MayhemPower.java:37 is literally the comment above and no
// MayhemPower$1.java exists in the reference tree. The body below is a
// RECONSTRUCTION from the two verified in-repo parallels that queue the same
// effect, both read in full:
//   * DistilledChaosPotion.use (DistilledChaosPotion.java:41) --
//       addToBot(new PlayTopCardAction(
//           AbstractDungeon.getCurrRoom().monsters.getRandomMonster(
//               null, true, AbstractDungeon.cardRandomRng), false))
//   * Havoc.use (Havoc.java:31) -- the same shape with exhausts = TRUE.
// Mayhem is the DistilledChaos shape: exhausts = FALSE (the played card files
// normally -- Mayhem is the card that does NOT burn the deck, which is exactly
// what distinguishes it from Havoc).
//
// WHY THE WRAPPER MATTERS. In DistilledChaosPotion the getRandomMonster call is
// an ARGUMENT, evaluated when the PlayTopCardAction is CONSTRUCTED. Mayhem
// queues an anonymous action instead, so its roll happens when that deferred
// action EXECUTES -- during the start-of-turn queue drain, not during the power
// hook walk. Queuing one kActorRandomEnemy PLAY_CARD per stack reproduces
// exactly that: execute_opcode resolves the sentinel with ONE
// roll_random_target (== getRandomMonster(null, true, cardRandomRng), one
// card_random_rng draw over the live monsters) at EXECUTE time, before
// op_play_card's own pile checks -- the same order the Java has, where the
// argument is evaluated before PlayTopCardAction.update runs.
//
// NO exhaust flag (contrast op_play_top_draw / Havoc, which sets
// EXHAUST_ON_USE_ONCE), and add_to_bottom per stack so two stacks play two
// cards in stack order. Everything else -- the empty-draw reshuffle, the
// both-piles-empty no-op, the free autoplay, the unplayable-card no-trigger
// filing and the dequeue-time canUse revalidation -- is already
// PLAY_CARD/kPlayCardFromDrawTop's landed behaviour (op_play_card,
// interp/interp_cards.cpp), which the ledger mandates reusing unchanged.
//
// NOTE (documented, not a silent difference): when NO monster is in the fight
// roll_random_target returns kActorPlayer and execute_opcode's random-enemy
// fan-out drops the item without playing anything, whereas the Java would still
// play the top card at a null target. Unreachable from a real turn start --
// every monster being dead ends the combat before the start-of-turn sequence --
// and MayhemPower, unlike MagnetismPower, has no areMonstersBasicallyDead guard
// of its own to model.
void power_native_mayhem(CombatState& s, Hook hook,
                         const HookContext& ctx) noexcept {
    if (hook != Hook::AT_START_OF_TURN) {
        return;
    }
    for (int32_t i = 0; i < ctx.power_amount; ++i) {
        ActionQueueItem play{};
        play.opcode = static_cast<uint16_t>(Opcode::PLAY_CARD);
        play.src = ctx.owner;
        play.tgt = kActorRandomEnemy;
        play.amount = 0;  // unused: the source is the draw-pile top
        play.flags = kPlayCardFromDrawTop;
        add_to_bottom(s, play);
    }
}

}  // namespace sts::engine
