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
#include "sts/engine/monster_dispatch.hpp"
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

CardPoolIndex AddDrawTop(CombatState& s, CardId id, uint8_t upgrade = 0) {
    const CardPoolIndex pi = AddCard(s, id, upgrade);
    s.draw[s.draw_count++] = pi;
    return pi;
}

void GiveRelic(CombatState& s, RelicId id) {
    s.relics[s.relic_count].relic_id = static_cast<uint16_t>(id);
    s.relics[s.relic_count].counter = -1;
    ++s.relic_count;
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

// --- 7. exhaustOnUseOnce is consumed by UseCardAction.
//
// PlayTopCardAction sets card.exhaustOnUseOnce for Havoc's target (:48).
// UseCardAction snapshots that into exhaustCard in its constructor (:36-38),
// then clears exhaustOnUseOnce after the Spoon/file decision (:132). It is not
// the card's permanent `exhaust` attribute. If Spoon saves the target, or
// Exhume later retrieves it from the exhaust pile, an ordinary replay of a
// normally non-exhausting card must discard.
TEST(CardLimbo, HavocSpoonSaveConsumesOneShotExhaustBeforeOrdinaryReplay) {
    int64_t seed = 0;
    for (;; ++seed) {
        RngStream probe = from_seed(seed);
        (void)random(probe, 0);  // Havoc getRandomMonster with one live target
        if (random_boolean(probe)) {
            break;  // choose a deterministic Spoon-save seed
        }
    }

    CombatState s = MakeCombat(6, 100);
    s.card_random_rng = from_seed(seed);
    GiveRelic(s, RelicId::STRANGE_SPOON);
    AddHand(s, CardId::HAVOC);
    const CardPoolIndex strike = AddDrawTop(s, CardId::STRIKE);

    ASSERT_TRUE(queue_card_play(s, 0, kActorPlayer));
    pump(s);
    ASSERT_TRUE(PileHas(s.discard, s.discard_count, strike))
        << "the selected seed makes Strange Spoon save Havoc's target";
    EXPECT_FALSE(has_card_flag(s.card_pool[strike].flags,
                               CardFlag::EXHAUST_ON_USE_ONCE))
        << "UseCardAction.java:132 consumes exhaustOnUseOnce after Spoon";

    // Simulate drawing the saved Strike, then play it normally. It no longer
    // qualifies for Spoon and must take the ordinary discard route.
    uint8_t strike_slot = 0;
    while (strike_slot < s.discard_count &&
           s.discard[strike_slot] != strike) {
        ++strike_slot;
    }
    ASSERT_LT(strike_slot, s.discard_count);
    for (uint8_t i = static_cast<uint8_t>(strike_slot + 1);
         i < s.discard_count; ++i) {
        s.discard[i - 1] = s.discard[i];
    }
    --s.discard_count;
    s.hand[s.hand_count++] = strike;
    const int32_t rng_before = s.card_random_rng.counter;
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.card_random_rng.counter, rng_before)
        << "ordinary Strike must not make a second Spoon roll";
    EXPECT_TRUE(PileHas(s.discard, s.discard_count, strike));
    EXPECT_FALSE(PileHas(s.exhaust, s.exhaust_count, strike));
}

TEST(CardLimbo, HavocExhumeReplayConsumesOneShotExhaust) {
    CombatState s = MakeCombat(8, 100);
    AddHand(s, CardId::HAVOC);
    const CardPoolIndex exhume = AddHand(s, CardId::EXHUME);
    const CardPoolIndex strike = AddDrawTop(s, CardId::STRIKE);

    ASSERT_TRUE(queue_card_play(s, 0, kActorPlayer));
    pump(s);
    ASSERT_TRUE(PileHas(s.exhaust, s.exhaust_count, strike));
    EXPECT_FALSE(has_card_flag(s.card_pool[strike].flags,
                               CardFlag::EXHAUST_ON_USE_ONCE));

    // Exhume's one-card branch retrieves Strike automatically; Exhume itself
    // then exhausts through its queued UseCardAction.
    ASSERT_TRUE(queue_card_play(s, 0, kActorPlayer));
    pump(s);
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], strike);
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, exhume));

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_TRUE(PileHas(s.discard, s.discard_count, strike))
        << "the Exhumed ordinary Strike discards on its next play";
    EXPECT_FALSE(PileHas(s.exhaust, s.exhaust_count, strike));
}

// A lethal DamageAction invokes clearPostCombatActions, whose allowlist retains
// UseCardAction (DamageAction.java:88-91; GameActionManager.java:130-136).
// Therefore the filing action still consumes Strange Spoon RNG and
// moveToExhaustPile still fires onExhaust hooks. With one other hand card,
// Fiend Fire's random ExhaustAction is forced (zero RNG): the only draw is the
// deliberately-selected false Spoon boolean. Feel No Pain queues one BLOCK for
// the hand card before lethal damage and a second for Fiend Fire at terminal
// filing; both stay queued under the engine's established immediate halt.
TEST(CardLimbo, TerminalUseCardStillRollsSpoonAndDispatchesOnExhaustInOrder) {
    int64_t seed = 0;
    for (;; ++seed) {
        RngStream probe = from_seed(seed);
        if (!random_boolean(probe)) {
            break;  // force the played Fiend Fire to exhaust, not Spoon-save
        }
    }

    CombatState s = MakeCombat(6, 7);
    s.card_random_rng = from_seed(seed);
    GiveRelic(s, RelicId::STRANGE_SPOON);
    s.player_powers[0].power_id =
        static_cast<uint16_t>(PowerId::FEEL_NO_PAIN);
    s.player_powers[0].amount = 3;
    s.player_power_count = 1;
    const CardPoolIndex fiend = AddHand(s, CardId::FIEND_FIRE);
    const CardPoolIndex strike = AddHand(s, CardId::STRIKE);

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);

    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::COMBAT_OVER));
    EXPECT_EQ(s.card_random_rng.counter, 1)
        << "lethal filing still executes Strange Spoon's boolean";
    ASSERT_EQ(s.exhaust_count, 2);
    EXPECT_EQ(s.exhaust[0], strike)
        << "Fiend Fire's own program exhausts the hand first";
    EXPECT_EQ(s.exhaust[1], fiend)
        << "the retained UseCardAction files the source afterwards";
    ASSERT_EQ(s.action_count, 2)
        << "Feel No Pain fires for both exhausts under the terminal halt";
    for (uint8_t i = 0; i < s.action_count; ++i) {
        const ActionQueueItem& item =
            s.action_queue[(s.action_head + i) % kActionQueueCap];
        EXPECT_EQ(static_cast<Opcode>(item.opcode), Opcode::BLOCK);
        EXPECT_EQ(item.amount, 3);
    }
}

// --- 8. The limbo window is visible at a blocking choice: the played card is
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

// --- 9. The engine's halt-at-death collapse still produces a normalized
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

// --- 10. A synchronous replay enters immediately behind the resolving card.
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

// --- 11. A queued autoplay is revalidated when it reaches the card queue.
//
// GameActionManager.getNextAction rolls a random target (when needed), then
// calls canUse BEFORE any play hooks or use() (:209-249). AbstractCard.canUse
// delegates to cardPlayable, which rejects an enemy-target card whose selected
// monster is dying (:854-859, :916-924). A rejected autoplay still receives a
// no-trigger UseCardAction for filing (:285-301), but it does not spend energy,
// increment cardsPlayedThisTurn, fire Rage, queue damage, or retarget.
TEST(CardLimbo, QueuedAutoplayCancelsWhenItsSelectedTargetDies) {
    CombatState s = MakeCombat(/*energy=*/6, /*monster_hp=*/6);
    s.monster_count = 2;
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[1].hp = 30;
    s.monsters[1].max_hp = 30;
    s.player_powers[0].power_id = static_cast<uint16_t>(PowerId::RAGE);
    s.player_powers[0].amount = 3;
    s.player_power_count = 1;
    const CardPoolIndex strike = AddDrawTop(s, CardId::STRIKE);
    const int32_t card_rng_before = s.card_random_rng.counter;
    const int32_t shuffle_rng_before = s.shuffle_rng.counter;

    ActionQueueItem autoplay{};
    autoplay.opcode = static_cast<uint16_t>(Opcode::PLAY_CARD);
    autoplay.src = kActorPlayer;
    autoplay.tgt = 0;
    autoplay.flags = kPlayCardFromDrawTop;
    add_to_bottom(s, autoplay);

    // Actions outrank cardQueue resolution. This hit therefore kills the
    // selected monster after PLAY_CARD has put Strike in limbo/cardQueue, but
    // before that queued play reaches GameActionManager's canUse gate.
    ActionQueueItem lethal{};
    lethal.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    lethal.src = kActorPlayer;
    lethal.tgt = 0;
    lethal.amount = 6;
    add_to_bottom(s, lethal);

    pump(s);

    EXPECT_EQ(s.monsters[0].hp, 0);
    EXPECT_EQ(s.monsters[1].hp, 30) << "a rejected play is never retargeted";
    EXPECT_EQ(s.cards_played_this_turn, 0);
    EXPECT_EQ(s.player_block, 0) << "Rage's onUseCard hook must not fire";
    EXPECT_EQ(s.player_energy, 6) << "autoplay cancellation spends no energy";
    EXPECT_EQ(s.card_random_rng.counter, card_rng_before);
    EXPECT_EQ(s.shuffle_rng.counter, shuffle_rng_before);
    ASSERT_EQ(s.discard_count, 1)
        << "the no-trigger UseCardAction still files the autoplayed card";
    EXPECT_EQ(s.discard[0], strike);
    EXPECT_EQ(s.exhaust_count, 0);
    EXPECT_EQ(s.limbo_count, 0);
    EXPECT_EQ(s.action_count, 0);
    EXPECT_EQ(s.card_queue_count, 0);
}

// --- 12. Double Tap keeps the original target on its autoplay replay.
//
// DoubleTapPower.onUseCard copies the original target into a front-queued,
// purge-on-use autoplay (:43-66). If the first Strike kills that monster while
// another remains, the replay reaches the same canUse rejection above: it
// neither retargets nor fires play/use hooks, and its no-trigger UseCardAction
// merely purges the temporary limbo copy.
TEST(CardLimbo, DoubleTapReplayCancelsWhenOriginalTargetDies) {
    CombatState s = MakeCombat(/*energy=*/6, /*monster_hp=*/6);
    s.monster_count = 2;
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[1].hp = 30;
    s.monsters[1].max_hp = 30;
    s.player_powers[0].power_id =
        static_cast<uint16_t>(PowerId::DOUBLE_TAP);
    s.player_powers[0].amount = 1;
    s.player_powers[1].power_id = static_cast<uint16_t>(PowerId::RAGE);
    s.player_powers[1].amount = 3;
    s.player_power_count = 2;
    const CardPoolIndex strike = AddHand(s, CardId::STRIKE);
    const int32_t card_rng_before = s.card_random_rng.counter;
    const int32_t shuffle_rng_before = s.shuffle_rng.counter;

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);

    EXPECT_EQ(s.monsters[0].hp, 0);
    EXPECT_EQ(s.monsters[1].hp, 30) << "Double Tap preserves, never rerolls, target";
    EXPECT_EQ(s.cards_played_this_turn, 1)
        << "only the original passes canUse and enters the hook sequence";
    EXPECT_EQ(s.player_block, 3) << "Rage fires once for the original only";
    EXPECT_EQ(s.player_energy, 5)
        << "the original costs one; the rejected autoplay copy is free";
    EXPECT_EQ(s.card_random_rng.counter, card_rng_before);
    EXPECT_EQ(s.shuffle_rng.counter, shuffle_rng_before);
    ASSERT_EQ(s.discard_count, 1);
    EXPECT_EQ(s.discard[0], strike);
    EXPECT_EQ(s.exhaust_count, 0);
    EXPECT_EQ(s.limbo_count, 0) << "the replay's UseCardAction purged its copy";
    EXPECT_EQ(s.action_count, 0);
    EXPECT_EQ(s.card_queue_count, 0);
    ASSERT_EQ(s.player_power_count, 1);
    EXPECT_EQ(s.player_powers[0].power_id,
              static_cast<uint16_t>(PowerId::RAGE))
        << "Double Tap spent exactly once on the original";
}

TEST(CardLimbo, OrdinaryQueuedCardRejectedByCanUseStaysInHand) {
    CombatState s = MakeCombat(6, 0);
    s.monster_count = 2;
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[1].hp = 30;
    s.monsters[1].max_hp = 30;
    const CardPoolIndex strike = AddHand(s, CardId::STRIKE);

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);

    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], strike);
    EXPECT_EQ(s.cards_played_this_turn, 0);
    EXPECT_EQ(s.player_energy, 6);
    EXPECT_EQ(s.discard_count, 0);
    EXPECT_EQ(s.exhaust_count, 0);
}

TEST(CardLimbo, HavocAutoplayStillRejectsAnUnplayableStatus) {
    CombatState s = MakeCombat(6, 100);
    AddHand(s, CardId::HAVOC);
    const CardPoolIndex wound = AddDrawTop(s, CardId::WOUND);

    ASSERT_TRUE(queue_card_play(s, 0, kActorPlayer));
    pump(s);

    EXPECT_EQ(s.cards_played_this_turn, 1) << "only Havoc passes canUse";
    EXPECT_EQ(s.player_energy, 5);
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, wound))
        << "failed autoplay still gets no-trigger UseCardAction filing";
    EXPECT_FALSE(has_card_flag(s.card_pool[wound].flags,
                               CardFlag::EXHAUST_ON_USE_ONCE));
}

TEST(CardLimbo, DoubleTapReplayIsRevalidatedAgainstNormality) {
    CombatState s = MakeCombat(6, 100);
    s.cards_played_this_turn = 2;
    s.player_powers[0].power_id =
        static_cast<uint16_t>(PowerId::DOUBLE_TAP);
    s.player_powers[0].amount = 1;
    s.player_power_count = 1;
    AddHand(s, CardId::STRIKE);
    AddHand(s, CardId::NORMALITY);

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);

    EXPECT_EQ(s.monsters[0].hp, 94);
    EXPECT_EQ(s.cards_played_this_turn, 3);
    EXPECT_EQ(s.player_energy, 5);
    EXPECT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.card_pool[s.hand[0]].card_id,
              static_cast<uint16_t>(CardId::NORMALITY));
}

TEST(CardLimbo, DoubleTapReplayIsRevalidatedAgainstVelvetChoker) {
    CombatState s = MakeCombat(6, 100);
    s.cards_played_this_turn = 5;
    GiveRelic(s, RelicId::VELVET_CHOKER);
    s.relics[0].counter = 5;
    s.player_powers[0].power_id =
        static_cast<uint16_t>(PowerId::DOUBLE_TAP);
    s.player_powers[0].amount = 1;
    s.player_power_count = 1;
    AddHand(s, CardId::STRIKE);

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);

    EXPECT_EQ(s.monsters[0].hp, 94);
    EXPECT_EQ(s.cards_played_this_turn, 6);
    EXPECT_EQ(s.relics[0].counter, 6);
    EXPECT_EQ(s.player_energy, 5);
}

TEST(CardLimbo, EscapedEnemySuppressionExcludesSelfAndEnemyCards) {
    CombatState enemy = MakeCombat(6, 100);
    enemy.monster_count = 2;
    enemy.monsters[0].flags |= kMonsterFlagEscaped;
    enemy.monsters[1].monster_id =
        static_cast<uint16_t>(MonsterId::JAW_WORM);
    enemy.monsters[1].hp = 100;
    enemy.monsters[1].max_hp = 100;
    const CardPoolIndex strike = AddHand(enemy, CardId::STRIKE);
    ASSERT_TRUE(queue_card_play(enemy, 0, 0));
    pump(enemy);
    EXPECT_EQ(enemy.cards_played_this_turn, 1)
        << "the escaped-target split is after hooks/counters";
    EXPECT_EQ(enemy.player_energy, 6);
    ASSERT_EQ(enemy.hand_count, 1);
    EXPECT_EQ(enemy.hand[0], strike);
    EXPECT_EQ(enemy.discard_count, 0);

    CombatState autoplay = MakeCombat(6, 100);
    autoplay.monster_count = 2;
    autoplay.monsters[0].flags |= kMonsterFlagEscaped;
    autoplay.monsters[1].monster_id =
        static_cast<uint16_t>(MonsterId::JAW_WORM);
    autoplay.monsters[1].hp = 100;
    autoplay.monsters[1].max_hp = 100;
    const CardPoolIndex auto_strike = AddDrawTop(autoplay, CardId::STRIKE);
    ActionQueueItem play{};
    play.opcode = static_cast<uint16_t>(Opcode::PLAY_CARD);
    play.tgt = 0;
    play.flags = kPlayCardFromDrawTop;
    add_to_bottom(autoplay, play);
    pump(autoplay);
    EXPECT_EQ(autoplay.cards_played_this_turn, 1);
    EXPECT_EQ(autoplay.player_energy, 6);
    EXPECT_FALSE(PileHas(autoplay.limbo, autoplay.limbo_count, auto_strike));
    EXPECT_FALSE(PileHas(autoplay.discard, autoplay.discard_count, auto_strike));
    EXPECT_FALSE(PileHas(autoplay.exhaust, autoplay.exhaust_count, auto_strike))
        << "post-hook ENEMY suppression removes limbo without filing";

    CombatState both = MakeCombat(6, 100);
    both.monster_count = 2;
    both.monsters[0].flags |= kMonsterFlagEscaped;
    both.monsters[0].intent = static_cast<uint8_t>(MonsterIntent::ATTACK);
    both.monsters[1].monster_id =
        static_cast<uint16_t>(MonsterId::JAW_WORM);
    both.monsters[1].hp = 100;
    both.monsters[1].max_hp = 100;
    AddHand(both, CardId::SPOT_WEAKNESS);
    ASSERT_TRUE(queue_card_play(both, 0, 0));
    pump(both);
    EXPECT_EQ(both.cards_played_this_turn, 1);
    EXPECT_EQ(both.player_energy, 5);
    EXPECT_EQ(both.hand_count, 0);
    EXPECT_EQ(both.discard_count, 1);
    ASSERT_EQ(both.player_power_count, 1);
    EXPECT_EQ(both.player_powers[0].power_id,
              static_cast<uint16_t>(PowerId::STRENGTH));
    EXPECT_EQ(both.player_powers[0].amount, 3);
}

TEST(CardLimbo, FailedAutoplayUsesSpoonAndOnExhaustBeforeCombatEnds) {
    int64_t seed = 0;
    for (;; ++seed) {
        RngStream probe = from_seed(seed);
        if (!random_boolean(probe)) {
            break;
        }
    }
    CombatState s = MakeCombat(6, 100);
    s.card_random_rng = from_seed(seed);
    s.cards_played_this_turn = 3;
    GiveRelic(s, RelicId::STRANGE_SPOON);
    AddHand(s, CardId::NORMALITY);
    const CardPoolIndex sentinel = AddDrawTop(s, CardId::SENTINEL);
    ActionQueueItem play{};
    play.opcode = static_cast<uint16_t>(Opcode::PLAY_CARD);
    play.tgt = 0;
    play.flags = kPlayCardFromDrawTop | kPlayCardExhaust;
    add_to_bottom(s, play);

    pump(s);

    EXPECT_EQ(s.card_random_rng.counter, 1);
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, sentinel));
    EXPECT_EQ(s.player_energy, 8) << "Sentinel.onExhaust resolved";
    EXPECT_FALSE(has_card_flag(s.card_pool[sentinel].flags,
                               CardFlag::EXHAUST_ON_USE_ONCE));
}

TEST(CardLimbo, TerminalQueuedAutoplayUsesSpoonAndOnExhaustFiling) {
    int64_t seed = 0;
    for (;; ++seed) {
        RngStream probe = from_seed(seed);
        if (!random_boolean(probe)) {
            break;
        }
    }
    CombatState s = MakeCombat(6, 6);
    s.card_random_rng = from_seed(seed);
    GiveRelic(s, RelicId::STRANGE_SPOON);
    const CardPoolIndex sentinel = AddDrawTop(s, CardId::SENTINEL);
    ActionQueueItem play{};
    play.opcode = static_cast<uint16_t>(Opcode::PLAY_CARD);
    play.tgt = 0;
    play.flags = kPlayCardFromDrawTop | kPlayCardExhaust;
    add_to_bottom(s, play);
    ActionQueueItem lethal{};
    lethal.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    lethal.tgt = 0;
    lethal.amount = 6;
    add_to_bottom(s, lethal);

    pump(s);

    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::COMBAT_OVER));
    EXPECT_EQ(s.card_random_rng.counter, 1);
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, sentinel));
    EXPECT_FALSE(has_card_flag(s.card_pool[sentinel].flags,
                               CardFlag::EXHAUST_ON_USE_ONCE));
    ASSERT_EQ(s.action_count, 1);
    const ActionQueueItem& on_exhaust = s.action_queue[s.action_head];
    EXPECT_EQ(static_cast<Opcode>(on_exhaust.opcode), Opcode::GAIN_ENERGY);
    EXPECT_EQ(on_exhaust.amount, 2);
}

TEST(CardLimbo, AutoplayDoesNotPermanentlyZeroLaterPlayCost) {
    int64_t seed = 0;
    for (;; ++seed) {
        RngStream probe = from_seed(seed);
        (void)random(probe, 0);
        if (random_boolean(probe)) {
            break;
        }
    }
    CombatState s = MakeCombat(6, 100);
    s.card_random_rng = from_seed(seed);
    GiveRelic(s, RelicId::STRANGE_SPOON);
    AddHand(s, CardId::HAVOC);
    const CardPoolIndex strike = AddDrawTop(s, CardId::STRIKE);
    ASSERT_TRUE(queue_card_play(s, 0, kActorPlayer));
    pump(s);
    EXPECT_EQ(s.player_energy, 5);
    EXPECT_EQ(s.card_pool[strike].cost_now, 1);

    uint8_t slot = 0;
    while (slot < s.discard_count && s.discard[slot] != strike) {
        ++slot;
    }
    ASSERT_LT(slot, s.discard_count);
    for (uint8_t i = static_cast<uint8_t>(slot + 1); i < s.discard_count; ++i) {
        s.discard[i - 1] = s.discard[i];
    }
    --s.discard_count;
    s.hand[s.hand_count++] = strike;
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.player_energy, 4);
}

TEST(CardLimbo, DrawTopAutoplayXCostPreservesPlayerEnergy) {
    CombatState s = MakeCombat(3, 100);
    AddDrawTop(s, CardId::WHIRLWIND);
    ActionQueueItem play{};
    play.opcode = static_cast<uint16_t>(Opcode::PLAY_CARD);
    play.tgt = kActorPlayer;
    play.flags = kPlayCardFromDrawTop;
    add_to_bottom(s, play);
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 85);
    EXPECT_EQ(s.player_energy, 3);
}

TEST(CardLimbo, DoubleTapXCostReplayKeepsOriginalEnergyOnUse) {
    CombatState s = MakeCombat(3, 100);
    s.player_powers[0].power_id =
        static_cast<uint16_t>(PowerId::DOUBLE_TAP);
    s.player_powers[0].amount = 1;
    s.player_power_count = 1;
    const CardPoolIndex whirlwind = AddHand(s, CardId::WHIRLWIND);
    s.card_pool[whirlwind].misc = 77;
    ASSERT_TRUE(queue_card_play(s, 0, kActorPlayer));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 70)
        << "original and replay each execute energyOnUse == 3";
    EXPECT_EQ(s.player_energy, 0);
    EXPECT_EQ(s.card_pool[whirlwind].misc, 77)
        << "capturing the purge copy must not overwrite persistent source misc";
}

TEST(CardLimbo, RandomTargetDrawPrecedesFailedGateAndIsNotRepeated) {
    CombatState s = MakeCombat(6, 100);
    s.monster_count = 2;
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[1].hp = 100;
    s.monsters[1].max_hp = 100;
    s.cards_played_this_turn = 3;
    const CardPoolIndex strike = AddHand(s, CardId::STRIKE);
    AddHand(s, CardId::NORMALITY);
    CardDef random_def = *card_def(CardId::STRIKE);
    random_def.random_target = true;
    random_def.target_kind = CardTargetKind::RANDOM_ENEMY;

    const int32_t before = s.card_random_rng.counter;
    const uint8_t target = resolve_play_target(s, random_def, 0);
    EXPECT_LT(target, s.monster_count);
    EXPECT_EQ(s.card_random_rng.counter, before + 1)
        << "dequeue resolves random targeting before canUse";
    EXPECT_FALSE(card_can_use(s, strike, target, /*autoplay=*/false));
    EXPECT_EQ(s.card_random_rng.counter, before + 1)
        << "the failed gate neither rerolls nor consumes another draw";
}

}  // namespace
}  // namespace sts::engine
