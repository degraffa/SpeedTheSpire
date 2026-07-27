// Magnetism -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and power_magnetism.hpp for
// what this power does.

#include "power_magnetism.hpp"

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActorPlayer
#include "sts/engine/cards.hpp"         // kColorlessCombatPool
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, CardPile, make_make_card_flags
#include "sts/engine/rng_stream.hpp"    // random

namespace sts::engine {

// MagnetismPower.atStartOfTurn (MagnetismPower.java:30-38) in full:
//
//     if (!AbstractDungeon.getMonsters().areMonstersBasicallyDead()) {
//         this.flash();
//         for (int i = 0; i < this.amount; ++i) {
//             this.addToBot(new MakeTempCardInHandAction(
//                 AbstractDungeon.returnTrulyRandomColorlessCardInCombat()
//                     .makeCopy(), 1, false));
//         }
//     }
//
// NATIVE for two reasons a data program cannot express: the
// areMonstersBasicallyDead gate (the DarkEmbracePower precedent, powers.yaml id
// 7), and the ROLL TIMING below.
//
// ROLL TIMING IS THE WHOLE POINT. returnTrulyRandomColorlessCardInCombat() is an
// ARGUMENT of the MakeTempCardInHandAction constructor, so every stack's pool
// draw happens WHILE THE HOOK RUNS -- synchronously inside
// applyStartOfTurnPowers -- and only the card CREATION is deferred to the queued
// action. Queuing a RANDOM_COLORLESS_TO_HAND item instead would move those draws
// to the queue drain, which is observable the moment another start-of-turn power
// sits AHEAD of this one in the power list and consumes card_random_rng from a
// queued action of its own (Mayhem, whose deferred play rolls its target at
// execute time). So: the draws happen here, and what is queued is the exact
// MakeTempCardInHandAction the Java queues -- MAKE_CARD into CardPile::HAND,
// count 1, whose op_make_card body IS that action, hand-cap spill to the discard
// pile included (MakeTempCardInHandAction.java:71-77).
//
// Pool: returnTrulyRandomColorlessCardInCombat (AbstractDungeon.java:981-996)
// walks srcColorlessCardPool skipping CardTags.HEALING and takes ONE
// cardRandomRng random(size-1) -- the generated kColorlessCombatPool, exactly
// the pool opcode 52 (Jack of All Trades) draws over, with the same one-draw
// accounting and the same documented registry-id pool-ORDER deviation.
//
// The copies are BASE library copies at their registry cost: makeCopy() (not
// makeStatEquivalentCopy) and no setCostForTurn anywhere in this power --
// contrast Transmutation, which re-costs its copies for the turn.
//
// areMonstersBasicallyDead (MonsterGroup.java:90-95) is "every monster is dying
// or escaped"; monster_dead_or_escaped is that predicate here (the same shape
// power_dark_embrace.cpp already uses for the same Java call).
void power_native_magnetism(CombatState& s, Hook hook,
                            const HookContext& ctx) noexcept {
    if (hook != Hook::AT_START_OF_TURN) {
        return;
    }
    bool any_live = false;
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        any_live = any_live || !monster_dead_or_escaped(s.monsters[i]);
    }
    if (!any_live) {
        return;
    }
    static_assert(kColorlessCombatPoolCount > 0,
                  "Magnetism needs a non-empty colorless combat pool");
    for (int32_t i = 0; i < ctx.power_amount; ++i) {
        const int32_t pick = random(
            s.card_random_rng,
            static_cast<int32_t>(kColorlessCombatPoolCount) - 1);
        ActionQueueItem make{};
        make.opcode = static_cast<uint16_t>(Opcode::MAKE_CARD);
        make.src = static_cast<uint8_t>(CardPile::HAND);
        make.tgt = kActorPlayer;
        make.amount = 1;
        make.flags = make_make_card_flags(static_cast<uint16_t>(
            kColorlessCombatPool[static_cast<unsigned>(pick)]));
        add_to_bottom(s, make);
    }
}

}  // namespace sts::engine
