// The played-card limbo window -- the GENERAL case (stage-b-tasks.md deferred
// obligation "A played card is in no pile while its own effects resolve").
//
// AbstractPlayer.useCard (AbstractPlayer.java:1358-1384) runs, in order:
//   c.use(...)                                  -- queues the card's own actions
//   addToBottom(new UseCardAction(c, monster))  -- the FILING action, queued LAST
//   hand.triggerOnOtherCardPlayed(c)
//   hand.removeCard(c); cardInUse = c           -- the card leaves the hand NOW
// so from removeCard until UseCardAction.update (UseCardAction.java:77-137)
// resolves, the played card is in NO pile: not hand, not draw, not discard, not
// exhaust. Every effect the card queued resolves inside that window, and
// anything those effects observe about the piles must NOT see the played card.
// UseCardAction.update then rolls Strange Spoon (:109-113), and files the card
// (discard :127 / exhaust :129-131 / POWER empower :95-108 / purge poof :89-94).
//
// Each test here pins one observable consequence of that window that the engine
// used to get wrong by moving the card to its destination pile at resolve time
// (each cites the exact Java). They were written RED against that behaviour and
// are satisfied by modelling limbo with the CombatState `limbo` pile (design
// stage-a §4.2 budgets it) plus a queued USE_CARD action at UseCardAction's
// queue position.

#include <cstdint>
#include <span>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/relic_hooks.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

CombatState MakeCombat(int16_t energy = 6, int16_t monster_hp = 100) {
    CombatState s{};
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.player_energy = energy;
    s.monster_count = 1;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = monster_hp;
    s.monsters[0].max_hp = monster_hp;
    s.monster_attacks_queued = 1;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    // Valid streams so a reshuffle inside a test is well-defined.
    s.shuffle_rng = from_seed(7);
    s.card_random_rng = from_seed(7);
    return s;
}

CardPoolIndex AddCard(CombatState& s, CardId id, uint8_t upgrade = 0) {
    uint8_t pi = 0;
    while (pi < kCardPoolCap &&
           s.card_pool[pi].card_id != static_cast<uint16_t>(CardId::NONE)) {
        ++pi;
    }
    const CardDef* d = card_def(id);
    EXPECT_NE(d, nullptr);
    s.card_pool[pi].card_id = static_cast<uint16_t>(id);
    s.card_pool[pi].upgrade = upgrade;
    s.card_pool[pi].cost_now = card_cost(*d, upgrade);
    s.card_pool[pi].flags = card_flags(*d, upgrade);
    return pi;
}

CardPoolIndex AddHand(CombatState& s, CardId id, uint8_t upgrade = 0) {
    const CardPoolIndex pi = AddCard(s, id, upgrade);
    s.hand[s.hand_count++] = pi;
    return pi;
}

CardPoolIndex AddDiscard(CombatState& s, CardId id, uint8_t upgrade = 0) {
    const CardPoolIndex pi = AddCard(s, id, upgrade);
    s.discard[s.discard_count++] = pi;
    return pi;
}

bool PileHas(const CardPoolIndex* pile, uint8_t count, CardPoolIndex pi) {
    for (uint8_t i = 0; i < count; ++i) {
        if (pile[i] == pi) {
            return true;
        }
    }
    return false;
}

void Step(CombatState& s, Action a) {
    StepResult r{};
    advance(std::span<CombatState>(&s, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&r, 1));
}

// --- 1. A draw inside the played card's own program must not reshuffle the
//        played card into the draw pile.
//
// ShrugItOff.use (ShrugItOff.java:35-38): GainBlockAction then
// DrawCardAction(1). With the draw pile empty, DrawCardAction.update
// (DrawCardAction.java:74-90) queues EmptyDeckShuffleAction over the DISCARD
// pile -- and the played Shrug It Off is cardInUse (AbstractPlayer.java:1375),
// in no pile, so the reshuffle covers the pre-play discard only. It cannot be
// shuffled into the draw pile, cannot change the Fisher-Yates permutation of
// the other cards, and cannot be drawn back by its own draw. UseCardAction
// files it into the now-empty discard afterwards (UseCardAction.java:127).
TEST(CardLimbo, ShrugItOffReshuffleExcludesThePlayedCard) {
    CombatState s = MakeCombat();
    const CardPoolIndex shrug = AddHand(s, CardId::SHRUG_IT_OFF);
    const CardPoolIndex d0 = AddDiscard(s, CardId::STRIKE);
    const CardPoolIndex d1 = AddDiscard(s, CardId::DEFEND);
    const CardPoolIndex d2 = AddDiscard(s, CardId::STRIKE);
    ASSERT_EQ(s.draw_count, 0);

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);

    // The reshuffle covered {d0, d1, d2} only; one of them was drawn.
    EXPECT_EQ(s.hand_count, 1);
    EXPECT_FALSE(PileHas(s.hand, s.hand_count, shrug))
        << "the played card must never be drawn back by its own draw";
    EXPECT_EQ(s.draw_count, 2);
    EXPECT_FALSE(PileHas(s.draw, s.draw_count, shrug))
        << "the played card was shuffled into the draw pile (limbo ignored)";
    ASSERT_EQ(s.discard_count, 1)
        << "UseCardAction files the played card into the emptied discard";
    EXPECT_EQ(s.discard[0], shrug);
    // Exactly one shuffle_rng draw was consumed (one CardGroup.shuffle).
    EXPECT_EQ(s.shuffle_rng.counter, 1);
    (void)d0; (void)d1; (void)d2;
}

// --- 2. The filing happens at UseCardAction's queue position, so a card the
//        program itself adds to the discard lands BEFORE the played card.
//
// Anger.use (Anger.java:39-43): DamageAction, then
// MakeTempCardInDiscardAction(copy). The copy reaches the discard while the
// played Anger is still cardInUse; UseCardAction.update then appends the played
// Anger (UseCardAction.java:127). Discard order is observable state: it is the
// exact input of the next reshuffle's Fisher-Yates and the slot order of a
// discard-pile grid select. The engine used to append the played Anger first.
TEST(CardLimbo, AngerCopyLandsInDiscardBeforeThePlayedAnger) {
    CombatState s = MakeCombat();
    const CardPoolIndex anger = AddHand(s, CardId::ANGER);

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);

    ASSERT_EQ(s.discard_count, 2);
    EXPECT_NE(s.discard[0], anger) << "the copy is filed first (MakeTempCard "
                                      "resolves before UseCardAction)";
    EXPECT_EQ(s.card_pool[s.discard[0]].card_id,
              static_cast<uint16_t>(CardId::ANGER));
    EXPECT_EQ(s.discard[1], anger) << "the played Anger is filed last";
}

// --- 3. Local compensations do not compose: Deep Breath's own trailing DRAW.
//
// DeepBreath.use (DeepBreath.java:32-39): the guarded double shuffle, then
// UNCONDITIONALLY DrawCardAction(1). Played with BOTH piles empty: the guard
// (discardPile.size() > 0) sees an empty discard -- the played Deep Breath is
// cardInUse, in no pile -- so both shuffles are skipped; the draw then finds
// deckSize + discardSize == 0 and draws nothing (DrawCardAction.java:66-70).
// The engine used to have the played card sitting in the discard: RESHUFFLE_ALL
// excluded it (the local compensation), but the trailing DRAW's reshuffle did
// not -- it pulled the played Deep Breath into the draw pile and drew it back
// into the hand, out of an otherwise empty deck.
TEST(CardLimbo, DeepBreathWithEmptyPilesDrawsNothing) {
    CombatState s = MakeCombat();
    const CardPoolIndex deep = AddHand(s, CardId::DEEP_BREATH);
    ASSERT_EQ(s.draw_count, 0);
    ASSERT_EQ(s.discard_count, 0);

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);

    EXPECT_EQ(s.hand_count, 0) << "nothing to draw: both piles were empty";
    EXPECT_EQ(s.draw_count, 0);
    ASSERT_EQ(s.discard_count, 1);
    EXPECT_EQ(s.discard[0], deep);
    EXPECT_EQ(s.shuffle_rng.counter, 0) << "no shuffle of any kind happened";
}

// --- 4. Perfected Strike counts ITSELF on a hand play.
//
// AbstractPlayer.playCard (AbstractPlayer.java:1285-1302) queues the hovered
// card WITHOUT removing it from hand.group, and useCard calls
// c.calculateCardDamage(monster) (:1361) BEFORE hand.removeCard(c) (:1373) --
// so PerfectedStrike.countCards (PerfectedStrike.java:37-52), which scans
// hand + draw + discard, sees the played card still in the HAND and counts it
// (it carries CardTags.STRIKE, :34). Base 6 + 2 x 1 = 8 with no other Strike
// anywhere. (An AUTOPLAYED Perfected Strike sits in the limbo CardGroup at
// that moment and is NOT counted -- covered by the limbo pile, which the count
// never scans.) The engine used to exclude the source unconditionally.
TEST(CardLimbo, PerfectedStrikeCountsItselfOnAHandPlay) {
    CombatState s = MakeCombat(6, 100);
    AddHand(s, CardId::PERFECTED_STRIKE);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 100 - 8) << "6 + 2*1: the played card itself "
                                            "is still in the hand at count time";
}

TEST(CardLimbo, UpgradedPerfectedStrikeCountsItselfAtThree) {
    CombatState s = MakeCombat(6, 100);
    AddHand(s, CardId::PERFECTED_STRIKE, 1);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 100 - 9) << "6 + 3*1";
}

// --- 5. Strange Spoon rolls at UseCardAction.update time -- AFTER the card's
//        own program has consumed its cardRandomRng draws, not before.
//
// UseCardAction.update (UseCardAction.java:109-113) rolls the spoon boolean
// when the action RESOLVES, which is after every action the card queued ahead
// of it. Fiend Fire (FiendFireAction.update:32-46) consumes one cardRandomRng
// draw per random hand exhaust first. The engine used to roll the spoon at
// play-resolution time, putting the boolean BEFORE the exhaust draws in the
// stream -- a different consumption order over the same stream.
TEST(CardLimbo, StrangeSpoonRollsAfterTheCardsOwnRandomDraws) {
    // Expected-side derivation, game order, on a copy of the stream:
    //   [exhaust pick over 2 cards, spoon boolean].
    // The SECOND single-card ExhaustAction fires with one card left in hand,
    // and hand.size() <= amount exhausts with NO roll (ExhaustAction's
    // no-screen branch), so the program consumes exactly ONE cardRandomRng
    // draw -- and the spoon boolean is the SECOND draw of the stream, not the
    // first. Only the POSITION of the boolean is under test, so derive the
    // expected spoon outcome with the engine's own golden-tested primitives.
    bool expected_spoon = false;
    {
        RngStream probe = from_seed(11);
        (void)random(probe, 1);           // FiendFire exhaust pick #1 (2 cards)
        expected_spoon = random_boolean(probe);  // THEN the spoon
    }

    CombatState s = MakeCombat(6, 100);
    s.card_random_rng = from_seed(11);
    {   // Strange Spoon into the combat relic mirror.
        RelicSlot* r = s.relics;
        r[0].relic_id = static_cast<uint16_t>(RelicId::STRANGE_SPOON);
        r[0].counter = -1;
        s.relic_count = 1;
    }
    AddHand(s, CardId::FIEND_FIRE);
    AddHand(s, CardId::STRIKE);
    AddHand(s, CardId::DEFEND);

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);

    EXPECT_EQ(s.card_random_rng.counter, 2)
        << "one exhaust pick (the second is forced) + one spoon boolean";
    if (expected_spoon) {
        ASSERT_EQ(s.discard_count, 1) << "spoon proc: Fiend Fire is discarded";
        EXPECT_EQ(s.card_pool[s.discard[0]].card_id,
                  static_cast<uint16_t>(CardId::FIEND_FIRE));
        EXPECT_EQ(s.exhaust_count, 2);
    } else {
        EXPECT_EQ(s.discard_count, 0);
        ASSERT_EQ(s.exhaust_count, 3);
        // 6. And the played card's own exhaust is filed LAST -- after the two
        // hand cards its program exhausted (they resolved ahead of
        // UseCardAction). Exhaust order is the slot order of an Exhume grid.
        EXPECT_EQ(s.card_pool[s.exhaust[2]].card_id,
                  static_cast<uint16_t>(CardId::FIEND_FIRE))
            << "the played card lands in the exhaust pile after the cards its "
               "own program exhausted";
    }
}

// --- 7. The limbo window is visible at a blocking choice: the played card is
//        in the limbo pile, not the discard.
//
// Headbutt with >= 2 other discard cards blocks on a grid select
// (DiscardPileToTopOfDeckAction). While the prompt is open the played Headbutt
// is cardInUse -- the game's discard pile does NOT contain it, and the grid
// has exactly the two real discard cards. The engine used to file it into the
// discard early and carve it back out of the choice with a per-item exclusion
// (choice_excluded_index); the general model keeps it out of the pile itself.
TEST(CardLimbo, HeadbuttPromptSeesOnlyTheRealDiscardCards) {
    CombatState s = MakeCombat();
    const CardPoolIndex head = AddHand(s, CardId::HEADBUTT);
    AddDiscard(s, CardId::DEFEND);
    AddDiscard(s, CardId::STRIKE);

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));

    ASSERT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    ActionMask m{};
    legal_actions(s, m);
    ASSERT_TRUE(m.choice_pending);
    EXPECT_TRUE(m.choice_from_discard);
    EXPECT_EQ(s.discard_count, 2)
        << "the played Headbutt is in limbo, not the discard";
    ASSERT_EQ(s.limbo_count, 1);
    EXPECT_EQ(s.limbo[0], head);

    Step(s, make_action(ActionVerb::CHOOSE, 1));  // retrieve the Strike
    EXPECT_EQ(s.limbo_count, 0) << "UseCardAction filed the card away";
    ASSERT_EQ(s.discard_count, 2)
        << "the chosen card moved to the draw top; Headbutt joined the discard";
    EXPECT_EQ(s.card_pool[s.discard[s.discard_count - 1]].card_id,
              static_cast<uint16_t>(CardId::HEADBUTT));
}

// --- 8. The engine's halt-at-death collapse still produces a normalized
//        terminal state.
//
// A lethal single-DAMAGE card leaves only its queued UseCardAction when the
// terminal check runs. The game drains that filing action during the death
// animation; the headless pump stops immediately instead. Its terminal flush
// therefore performs the no-RNG/no-hook filing and consumes that internal
// action, while preserving the established policy for any other trailing
// gameplay actions. This is the exact shape pinned by the independent fixt16
// combat fixture (lethal Strike: empty queue, played card in discard).
TEST(CardLimbo, LethalStrikeFilesCardAndConsumesOnlyItsFilingAction) {
    CombatState s = MakeCombat(6, 6);
    const CardPoolIndex strike = AddHand(s, CardId::STRIKE);
    ASSERT_TRUE(queue_card_play(s, 0, 0));

    pump(s);

    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::COMBAT_OVER));
    EXPECT_EQ(s.monsters[0].hp, 0);
    EXPECT_EQ(s.limbo_count, 0);
    ASSERT_EQ(s.discard_count, 1);
    EXPECT_EQ(s.discard[0], strike);
    EXPECT_EQ(s.action_count, 0)
        << "the terminal flush consumed the pending UseCardAction";
}

// --- 9. A synchronous replay enters immediately behind the resolving card.
//
// GameActionManager.getNextAction keeps cardQueue[0] in place through
// player.useCard and removes it only afterwards (:193-298). DoubleTapPower's
// UseCardAction-constructor hook calls addCardQueueItem(..., true) during that
// window, whose index-1 insertion (:102-108) must therefore promote the replay
// ahead of a play that was already waiting behind the original.
TEST(CardLimbo, DoubleTapReplayPrecedesAnAlreadyQueuedPlay) {
    CombatState s = MakeCombat();
    s.player_powers[0].power_id =
        static_cast<uint16_t>(PowerId::DOUBLE_TAP);
    s.player_powers[0].amount = 1;
    s.player_power_count = 1;
    const CardPoolIndex first = AddHand(s, CardId::STRIKE);
    const CardPoolIndex second = AddHand(s, CardId::ANGER);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    ASSERT_TRUE(queue_card_play(s, 1, 0));
    ASSERT_EQ(s.card_queue_count, 2);

    const PumpStepResult r = pump_step(s, default_monster_turn);

    EXPECT_EQ(r.outcome, PumpOutcome::RAN_CARD_QUEUE);
    ASSERT_EQ(s.card_queue_count, 2);
    const CardPoolIndex replay = s.card_queue[0].card_index;
    EXPECT_NE(replay, first);
    EXPECT_NE(replay, second);
    EXPECT_TRUE(
        has_card_flag(s.card_pool[replay].flags, CardFlag::PURGE_ON_USE));
    EXPECT_EQ(s.card_queue[1].card_index, second)
        << "the pre-existing play stays behind Double Tap's index-1 replay";
}

}  // namespace
}  // namespace sts::engine
