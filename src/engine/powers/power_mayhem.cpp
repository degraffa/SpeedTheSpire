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
// The anonymous action is now RECOVERED -- MayhemPower$1.java, decompiled from
// the shipped desktop-1.0.jar (byte-identical outer class; provenance header
// in the recovered file) fills the :37 hole:
//
//     public void update() {
//         this.addToBot(new PlayTopCardAction(AbstractDungeon.getCurrRoom()
//             .monsters.getRandomMonster(null, true,
//                                        AbstractDungeon.cardRandomRng),
//                                        false));
//         this.isDone = true;
//     }
//
// TWO QUEUE LEVELS, and the level count is the whole mechanic. The $1 items
// are queued by the hook AHEAD of the turn's DrawCardAction
// (GameActionManager.java:361), but each $1 only ROLLS ITS TARGET (the
// getRandomMonster is the PlayTopCardAction's constructor argument, evaluated
// when $1 executes -- before the draw resolves) and addToBots the real play to
// the very END of the queue, BEHIND the draw and behind anything else step 6
// queued. So Mayhem plays the POST-draw top card -- pile [A..F] top-first:
// draw A-E, play F -- and at >= 2 stacks every target roll is spent before
// any play resolves. The previous single-level reconstruction here (one
// kActorRandomEnemy PLAY_CARD per stack, derived from the Distilled Chaos /
// Havoc parallels) played the PRE-draw top; the recovered class disproved it,
// and the three CardColorlessRaresMayhem tests were re-based RED-first.
//
// Reproduced with kPlayCardDeferRoll (interp.hpp): the hook queues one
// deferred item per stack; op_play_card's defer branch rolls one live-monster
// target (== getRandomMonster(null, true, cardRandomRng), one card_random_rng
// draw) and re-queues the play, bit cleared and target baked, at the bottom.
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
// the defer branch's roll_random_target returns kActorPlayer and the re-queued
// play carries that as a plain target; the Java would play the top card at a
// null target. Unreachable from a real turn start -- every monster being dead
// ends the combat before the start-of-turn sequence -- and MayhemPower, unlike
// MagnetismPower, has no areMonstersBasicallyDead guard of its own to model.
void power_native_mayhem(CombatState& s, Hook hook,
                         const HookContext& ctx) noexcept {
    if (hook != Hook::AT_START_OF_TURN) {
        return;
    }
    for (int32_t i = 0; i < ctx.power_amount; ++i) {
        ActionQueueItem play{};
        play.opcode = static_cast<uint16_t>(Opcode::PLAY_CARD);
        play.src = ctx.owner;
        play.tgt = kActorPlayer;  // ignored: the defer branch rolls at execute
        play.amount = 0;  // unused: the source is the draw-pile top
        play.flags = kPlayCardFromDrawTop | kPlayCardDeferRoll;
        add_to_bottom(s, play);
    }
}

}  // namespace sts::engine
