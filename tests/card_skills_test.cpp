// B3.4 red common skills (tier-2, constructed states + directed advance scripts).
//
// The five cards beyond the skeleton's Shrug It Off, each hand-computed from the
// cited Java (read in full), plus the CHOOSE-in-combat plumbing they introduce:
//   * Armaments (Armaments.java / ArmamentsAction.java) -- 5 block; upgrade ONE
//     chosen upgradeable hand card (base) or ALL of them (upgraded); grid choice.
//   * Flex (Flex.java / LoseStrengthPower.java) -- +N Strength now, -N Strength at
//     end of turn (the LoseStrength reversal power self-removes).
//   * Havoc (Havoc.java / PlayTopCardAction.java) -- play the top draw card free
//     and exhaust it; one card_random_rng monster-target roll; empty/reshuffle.
//   * True Grit (TrueGrit.java / ExhaustAction.java) -- 7/9 block; exhaust ONE
//     RANDOM hand card (base) or ONE CHOSEN hand card (upgraded).
//   * Warcry (Warcry.java / PutOnDeckAction.java) -- draw 1/2, then put ONE chosen
//     hand card on top of the draw pile; exhausts.
//
// CHOOSE-in-combat: a CHOOSE_CARD at the head of the action queue BLOCKS the pump
// (WAITING_ON_USER) when a real selection is needed; legal_actions exposes the
// eligible slots via can_choose[]; advance(CHOOSE, slot) resolves one selection.
// Forced (eligible <= amount) and RANDOM selections auto-resolve with no prompt.

#include <cstdint>
#include <span>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/power_hooks.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

// A minimal player-turn combat: one Jaw Worm, player 80 HP, WAITING_ON_USER, the
// monster-turn gate primed so a mid-turn play resolves without a monster turn.
CombatState MakeCombat(int16_t monster_hp = 50, int16_t energy = 3) {
    CombatState s{};
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.player_energy = energy;
    s.monster_count = 1;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = monster_hp;
    s.monsters[0].max_hp = monster_hp;
    s.monster_attacks_queued = 1;
    s.turn_has_ended = 0;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    return s;
}

// Allocate a pool row for `id` at upgrade level `upgrade` (cost/flags seeded from
// the registry) and return its pool index.
CardPoolIndex AddCard(CombatState& s, CardId id, uint8_t upgrade = 0) {
    uint8_t slot = 0;
    while (slot < kCardPoolCap &&
           s.card_pool[slot].card_id != static_cast<uint16_t>(CardId::NONE)) {
        ++slot;
    }
    const CardDef* def = card_def(id);
    s.card_pool[slot].card_id = static_cast<uint16_t>(id);
    s.card_pool[slot].upgrade = upgrade;
    s.card_pool[slot].cost_now = card_cost(*def, upgrade);
    s.card_pool[slot].flags = card_flags(*def, upgrade);
    return slot;
}

CardPoolIndex AddToHand(CombatState& s, CardId id, uint8_t upgrade = 0) {
    const CardPoolIndex idx = AddCard(s, id, upgrade);
    s.hand[s.hand_count++] = idx;
    return idx;
}

// Push a card onto the TOP of the draw pile (draw[draw_count-1] == top, drawn
// next). The last card pushed is the top.
CardPoolIndex AddToDrawTop(CombatState& s, CardId id, uint8_t upgrade = 0) {
    const CardPoolIndex idx = AddCard(s, id, upgrade);
    s.draw[s.draw_count++] = idx;
    return idx;
}

bool InPile(const CardPoolIndex* pile, uint8_t count, CardPoolIndex idx) {
    for (uint8_t i = 0; i < count; ++i) {
        if (pile[i] == idx) {
            return true;
        }
    }
    return false;
}

const PowerSlot* FindPlayerPower(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.player_powers[i];
        }
    }
    return nullptr;
}

void Play(CombatState& s, uint8_t hand_slot, uint8_t target = 0) {
    queue_card_play(s, hand_slot, target);
    pump(s, default_monster_turn);
}

StepResult Step(CombatState& s, Action a) {
    StepResult r{};
    advance(std::span<CombatState>(&s, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&r, 1));
    return r;
}

// ===========================================================================
// gen.py <-> engine CHOOSE_CARD encoding pin (byte-equal, no drift)
// ===========================================================================

TEST(CardSkillsEncoding, GeneratedChooseFlagsMatchEngineEncoding) {
    const CardDef* arm = card_def(CardId::ARMAMENTS);
    ASSERT_NE(arm, nullptr);
    EXPECT_EQ(static_cast<uint16_t>(arm->steps[1].op),
              static_cast<uint16_t>(Opcode::CHOOSE_CARD));
    EXPECT_EQ(arm->steps[1].extra, make_choose_flags(ChoiceKind::UPGRADE, false));
    EXPECT_EQ(arm->steps[1].amount, 1);
    EXPECT_EQ(arm->upgraded_steps[1].extra,
              make_choose_flags(ChoiceKind::UPGRADE, false));
    EXPECT_EQ(arm->upgraded_steps[1].amount, 99);  // upgrade-all

    const CardDef* tg = card_def(CardId::TRUE_GRIT);
    ASSERT_NE(tg, nullptr);
    EXPECT_EQ(tg->steps[1].extra, make_choose_flags(ChoiceKind::EXHAUST, true));
    EXPECT_EQ(tg->upgraded_steps[1].extra,
              make_choose_flags(ChoiceKind::EXHAUST, false));

    const CardDef* wc = card_def(CardId::WARCRY);
    ASSERT_NE(wc, nullptr);
    EXPECT_EQ(wc->steps[1].extra,
              make_choose_flags(ChoiceKind::PUT_ON_DRAW_TOP, false));
}

// ===========================================================================
// Flex -- temporary Strength (LoseStrengthPower reversal)
// ===========================================================================

TEST(CardSkillsFlex, AppliesStrengthAndLoseStrength) {
    CombatState s = MakeCombat();
    AddToHand(s, CardId::FLEX);      // pool 0, hand slot 0
    Play(s, 0);

    const PowerSlot* str = FindPlayerPower(s, PowerId::STRENGTH);
    const PowerSlot* lose = FindPlayerPower(s, PowerId::LOSE_STRENGTH);
    ASSERT_NE(str, nullptr);
    ASSERT_NE(lose, nullptr);
    EXPECT_EQ(str->amount, 2);       // magicNumber 2
    EXPECT_EQ(lose->amount, 2);
}

TEST(CardSkillsFlex, UpgradedAppliesFour) {
    CombatState s = MakeCombat();
    AddToHand(s, CardId::FLEX, /*upgrade=*/1);
    Play(s, 0);

    EXPECT_EQ(FindPlayerPower(s, PowerId::STRENGTH)->amount, 4);
    EXPECT_EQ(FindPlayerPower(s, PowerId::LOSE_STRENGTH)->amount, 4);
}

TEST(CardSkillsFlex, StrengthRevertsAtEndOfTurnAndPowerSelfRemoves) {
    CombatState s = MakeCombat();
    AddToHand(s, CardId::FLEX);
    Play(s, 0);
    ASSERT_EQ(FindPlayerPower(s, PowerId::STRENGTH)->amount, 2);

    // End the turn: LoseStrengthPower.atEndOfTurn applies Strength -2 (net 0) and
    // removes itself (LoseStrengthPower.java:40-44). default_monster_turn keeps the
    // monster inert so only the end-of-turn bookkeeping is observed.
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    pump(s, default_monster_turn);

    // B3.6 Java-exactness fix: StrengthPower.stackPower removes the slot when a
    // stack lands on exactly 0 (addToTop RemoveSpecificPowerAction,
    // StrengthPower.java:48-53) -- so after the reversal the Strength slot is
    // GONE, not a 0-amount residue (the pre-B3.6 expectation contradicted the
    // cited Java; recorded in the B3.6 ledger Log).
    EXPECT_EQ(FindPlayerPower(s, PowerId::STRENGTH), nullptr)
        << "Strength stacked to exactly 0 removes the slot";
    EXPECT_EQ(FindPlayerPower(s, PowerId::LOSE_STRENGTH), nullptr)
        << "LoseStrengthPower removes itself after firing";
}

// ===========================================================================
// True Grit -- block + exhaust (random base / chosen upgraded)
// ===========================================================================

TEST(CardSkillsTrueGrit, BaseBlocksAndExhaustsOneRandomHandCard) {
    CombatState s = MakeCombat();
    s.card_random_rng = from_seed(1234);
    AddToHand(s, CardId::TRUE_GRIT);      // slot 0
    const CardPoolIndex a = AddToHand(s, CardId::STRIKE);  // slot 1
    const CardPoolIndex b = AddToHand(s, CardId::DEFEND);  // slot 2

    const int32_t before = s.card_random_rng.counter;
    Play(s, 0);

    EXPECT_EQ(s.player_block, 7);                       // baseBlock 7
    EXPECT_EQ(s.card_random_rng.counter, before + 1)    // one getRandomCard draw
        << "a random exhaust of one card consumes exactly one card_random_rng draw";
    EXPECT_EQ(s.hand_count, 1);                         // two others, one exhausted
    EXPECT_EQ(s.exhaust_count, 1);
    // Exactly one of the two non-TrueGrit cards was exhausted; the other remains.
    const bool a_gone = InPile(s.exhaust, s.exhaust_count, a);
    const bool b_gone = InPile(s.exhaust, s.exhaust_count, b);
    EXPECT_TRUE(a_gone != b_gone) << "precisely one random card exhausted";
    EXPECT_TRUE(InPile(s.hand, s.hand_count, a_gone ? b : a));
}

TEST(CardSkillsTrueGrit, ForcedExhaustsWholeHandWhenAtOrBelowAmount) {
    // Hand after playing True Grit has a single card -> ExhaustAction's
    // hand.size() <= amount branch exhausts it with NO random draw.
    CombatState s = MakeCombat();
    s.card_random_rng = from_seed(99);
    AddToHand(s, CardId::TRUE_GRIT);         // slot 0
    const CardPoolIndex only = AddToHand(s, CardId::STRIKE);  // slot 1

    const int32_t before = s.card_random_rng.counter;
    Play(s, 0);

    EXPECT_EQ(s.player_block, 7);
    EXPECT_EQ(s.card_random_rng.counter, before)
        << "a forced (hand <= amount) exhaust draws no card_random_rng";
    EXPECT_EQ(s.hand_count, 0);
    EXPECT_TRUE(InPile(s.exhaust, s.exhaust_count, only));
}

TEST(CardSkillsTrueGrit, UpgradedPromptsChoiceMaskAndExhaustsSelection) {
    CombatState s = MakeCombat();
    const CardPoolIndex tg = AddToHand(s, CardId::TRUE_GRIT, /*upgrade=*/1);  // 0
    const CardPoolIndex a = AddToHand(s, CardId::STRIKE);   // slot 1
    const CardPoolIndex b = AddToHand(s, CardId::DEFEND);   // slot 2
    (void)tg;

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));

    // Block landed (upgradeBlock -> 9), then the pump blocked on the CHOOSE.
    EXPECT_EQ(s.player_block, 9);
    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));

    ActionMask m{};
    legal_actions(s, m);
    EXPECT_TRUE(m.choice_pending);
    EXPECT_FALSE(m.can_end_turn);
    // Hand now holds the two non-TrueGrit cards (slots 0,1); both are choosable.
    EXPECT_EQ(s.hand_count, 2);
    EXPECT_TRUE(m.can_choose[0]);
    EXPECT_TRUE(m.can_choose[1]);
    EXPECT_FALSE(m.can_choose[2]);
    EXPECT_FALSE(m.can_play[0]);  // no plays while choosing

    // Choose hand slot 0 (card `a`): it is exhausted, the choice resolves, control
    // returns with no pending choice.
    Step(s, make_action(ActionVerb::CHOOSE, 0));
    EXPECT_TRUE(InPile(s.exhaust, s.exhaust_count, a));
    EXPECT_TRUE(InPile(s.hand, s.hand_count, b));
    EXPECT_EQ(s.hand_count, 1);
    ActionMask m2{};
    legal_actions(s, m2);
    EXPECT_FALSE(m2.choice_pending);
    EXPECT_TRUE(m2.can_end_turn);
}

// ===========================================================================
// Warcry -- draw then put a card back on top of draw; exhausts
// ===========================================================================

TEST(CardSkillsWarcry, DrawsThenForcedPutBackAndExhausts) {
    // Empty hand except Warcry; one card in the draw pile. Draw 1 -> hand holds the
    // drawn card only -> PutOnDeck's hand.size() <= amount branch moves it back
    // (no screen). Net: card cycles back on top; Warcry exhausts.
    CombatState s = MakeCombat();
    const CardPoolIndex wc = AddToHand(s, CardId::WARCRY);      // slot 0
    const CardPoolIndex top = AddToDrawTop(s, CardId::STRIKE);

    Play(s, 0);

    EXPECT_TRUE(InPile(s.exhaust, s.exhaust_count, wc)) << "Warcry exhausts";
    EXPECT_EQ(s.hand_count, 0);
    EXPECT_EQ(s.draw_count, 1);
    EXPECT_EQ(s.draw[s.draw_count - 1], top) << "the drawn card is put back on top";
}

TEST(CardSkillsWarcry, UpgradedDrawsTwo) {
    CombatState s = MakeCombat();
    AddToHand(s, CardId::WARCRY, /*upgrade=*/1);   // slot 0
    AddToDrawTop(s, CardId::STRIKE);
    AddToDrawTop(s, CardId::DEFEND);
    AddToDrawTop(s, CardId::BASH);

    Play(s, 0);

    // Drew 2 (magic -> 2), then put 1 chosen back... but hand > 1 here means a
    // CHOOSE prompt opens. So instead check the drawn count directly: hand holds
    // the 2 drawn, then blocks on the put-back CHOOSE.
    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    ActionMask m{};
    legal_actions(s, m);
    EXPECT_TRUE(m.choice_pending);
    EXPECT_EQ(s.hand_count, 2) << "Warcry+ drew 2 cards";
}

TEST(CardSkillsWarcry, PutBackChoicePromptAndResolution) {
    CombatState s = MakeCombat();
    AddToHand(s, CardId::WARCRY);                  // slot 0
    const CardPoolIndex keep = AddToHand(s, CardId::STRIKE);  // slot 1 (already in hand)
    const CardPoolIndex drawn = AddToDrawTop(s, CardId::DEFEND);

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));

    // After draw: hand = {keep, drawn} (2 cards) -> CHOOSE put-on-draw-top blocks.
    ActionMask m{};
    legal_actions(s, m);
    ASSERT_TRUE(m.choice_pending);
    EXPECT_EQ(s.hand_count, 2);
    EXPECT_TRUE(m.can_choose[0]);
    EXPECT_TRUE(m.can_choose[1]);

    // Put `keep` (slot 0) back on top of the draw pile.
    Step(s, make_action(ActionVerb::CHOOSE, 0));
    EXPECT_EQ(s.hand_count, 1);
    EXPECT_TRUE(InPile(s.hand, s.hand_count, drawn));
    EXPECT_EQ(s.draw[s.draw_count - 1], keep) << "chosen card is on top of draw";
}

// ===========================================================================
// Armaments -- block + upgrade in combat (one chosen / all)
// ===========================================================================

TEST(CardSkillsArmaments, BaseForcedUpgradeOfTheSoleUpgradeableCard) {
    CombatState s = MakeCombat();
    AddToHand(s, CardId::ARMAMENTS);                       // slot 0
    const CardPoolIndex strike = AddToHand(s, CardId::STRIKE);  // slot 1 (upgrade 0)

    Play(s, 0);

    EXPECT_EQ(s.player_block, 5);                          // baseBlock 5
    EXPECT_EQ(s.card_pool[strike].upgrade, 1)
        << "the sole upgradeable card is upgraded with no prompt";
}

TEST(CardSkillsArmaments, BasePromptsWhenMultipleUpgradeableAndUpgradesSelection) {
    CombatState s = MakeCombat();
    AddToHand(s, CardId::ARMAMENTS);                       // slot 0
    const CardPoolIndex a = AddToHand(s, CardId::STRIKE);  // slot 1
    const CardPoolIndex b = AddToHand(s, CardId::DEFEND);  // slot 2

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));
    EXPECT_EQ(s.player_block, 5);

    ActionMask m{};
    legal_actions(s, m);
    ASSERT_TRUE(m.choice_pending);
    EXPECT_TRUE(m.can_choose[0]);   // both upgradeable
    EXPECT_TRUE(m.can_choose[1]);

    Step(s, make_action(ActionVerb::CHOOSE, 1));  // choose `b`
    EXPECT_EQ(s.card_pool[a].upgrade, 0) << "unchosen card stays base";
    EXPECT_EQ(s.card_pool[b].upgrade, 1) << "chosen card is upgraded";
    ActionMask m2{};
    legal_actions(s, m2);
    EXPECT_FALSE(m2.choice_pending);
}

TEST(CardSkillsArmaments, UpgradedUpgradesEveryUpgradeableCardWithNoPrompt) {
    CombatState s = MakeCombat();
    AddToHand(s, CardId::ARMAMENTS, /*upgrade=*/1);        // slot 0
    const CardPoolIndex a = AddToHand(s, CardId::STRIKE);              // upgrade 0
    const CardPoolIndex b = AddToHand(s, CardId::DEFEND);              // upgrade 0
    const CardPoolIndex done = AddToHand(s, CardId::BASH, /*upgrade=*/1);  // already up

    Play(s, 0);

    EXPECT_EQ(s.player_block, 5);
    EXPECT_EQ(s.card_pool[a].upgrade, 1);
    EXPECT_EQ(s.card_pool[b].upgrade, 1);
    EXPECT_EQ(s.card_pool[done].upgrade, 1)
        << "an already-upgraded card is not eligible / not double-upgraded";
    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    ActionMask m{};
    legal_actions(s, m);
    EXPECT_FALSE(m.choice_pending) << "upgrade-all never prompts";
}

TEST(CardSkillsArmaments, UpgradeChoiceMaskExcludesAlreadyUpgradedCards) {
    CombatState s = MakeCombat();
    AddToHand(s, CardId::ARMAMENTS);                       // slot 0
    AddToHand(s, CardId::STRIKE);                          // slot 1: upgradeable
    AddToHand(s, CardId::DEFEND);                          // slot 2: upgradeable
    AddToHand(s, CardId::BASH, /*upgrade=*/1);             // slot 3: NOT upgradeable

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));

    ActionMask m{};
    legal_actions(s, m);
    ASSERT_TRUE(m.choice_pending);
    // Hand (post-play) = {Strike, Defend, Bash+}; only the first two are choosable.
    EXPECT_TRUE(m.can_choose[0]);
    EXPECT_TRUE(m.can_choose[1]);
    EXPECT_FALSE(m.can_choose[2]) << "an already-upgraded card is not a legal choice";
}

// ===========================================================================
// Havoc -- play top of draw free, exhaust it (PlayTopCardAction)
// ===========================================================================

TEST(CardSkillsHavoc, PlaysTopDrawCardFreeAndExhaustsIt) {
    CombatState s = MakeCombat(/*monster_hp=*/50);
    s.card_random_rng = from_seed(7);
    AddToHand(s, CardId::HAVOC);                     // slot 0, pool 0
    const CardPoolIndex strike = AddToDrawTop(s, CardId::STRIKE);  // pool 1, draw top

    const int32_t rng_before = s.card_random_rng.counter;
    Play(s, 0);

    EXPECT_EQ(s.card_random_rng.counter, rng_before + 1)
        << "getRandomMonster consumes one card_random_rng draw";
    EXPECT_EQ(s.monsters[0].hp, 44) << "the played Strike dealt 6";
    EXPECT_TRUE(InPile(s.exhaust, s.exhaust_count, strike))
        << "the Havoc-played card is exhausted (exhaustOnUseOnce)";
    EXPECT_EQ(s.draw_count, 0);
}

TEST(CardSkillsHavoc, EmptyDrawAndDiscardPlaysNothing) {
    CombatState s = MakeCombat();
    s.card_random_rng = from_seed(7);
    AddToHand(s, CardId::HAVOC);   // slot 0; no draw / discard cards

    const int32_t rng_before = s.card_random_rng.counter;
    const int16_t hp_before = s.monsters[0].hp;
    Play(s, 0);

    // getRandomMonster is still evaluated in Havoc.use (one draw), but with both
    // piles empty PlayTopCardAction plays nothing.
    EXPECT_EQ(s.card_random_rng.counter, rng_before + 1);
    EXPECT_EQ(s.monsters[0].hp, hp_before);
    EXPECT_EQ(s.exhaust_count, 0);
}

TEST(CardSkillsHavoc, ReshufflesDiscardWhenDrawEmpty) {
    CombatState s = MakeCombat(/*monster_hp=*/50);
    s.card_random_rng = from_seed(7);
    s.shuffle_rng = from_seed(42);
    AddToHand(s, CardId::HAVOC);                              // slot 0
    // Draw empty; the only playable card is in the discard pile.
    const CardPoolIndex strike = AddCard(s, CardId::STRIKE);  // pool 1
    s.discard[s.discard_count++] = strike;

    const int32_t shuffle_before = s.shuffle_rng.counter;
    Play(s, 0);

    EXPECT_EQ(s.shuffle_rng.counter, shuffle_before + 1)
        << "an empty draw pile reshuffles the discard in (one shuffle_rng draw)";
    EXPECT_EQ(s.monsters[0].hp, 44) << "the reshuffled Strike was played for 6";
    EXPECT_TRUE(InPile(s.exhaust, s.exhaust_count, strike));
}

// ===========================================================================
// Directed script: Armaments upgrade carries into a later play of that card
// ===========================================================================

TEST(CardSkillsDirected, ArmamentsUpgradeTakesEffectOnLaterPlay) {
    // Play Armaments choosing to upgrade a Flex; the upgraded Flex then applies +4
    // Strength (vs +2 base) when played. Exercises CHOOSE -> in-combat upgrade ->
    // the upgrade dimension carrying into a later play. (Strike/Defend define no
    // upgraded program yet -- that is B3.3 -- so Flex, which has an upgraded row, is
    // the observable probe.)
    CombatState s = MakeCombat(/*monster_hp=*/50);
    AddToHand(s, CardId::ARMAMENTS);                       // slot 0
    const CardPoolIndex flex = AddToHand(s, CardId::FLEX);  // slot 1
    AddToHand(s, CardId::DEFEND);                          // slot 2 (forces the prompt)

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));     // play Armaments
    ActionMask m{};
    legal_actions(s, m);
    ASSERT_TRUE(m.choice_pending);
    Step(s, make_action(ActionVerb::CHOOSE, 0));           // upgrade Flex (slot 0 now)
    ASSERT_EQ(s.card_pool[flex].upgrade, 1);

    // Find Flex's current hand slot and play it.
    uint8_t flex_slot = 0xFF;
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        if (s.hand[i] == flex) {
            flex_slot = i;
            break;
        }
    }
    ASSERT_NE(flex_slot, 0xFF);
    Step(s, make_action(ActionVerb::PLAY_CARD, flex_slot, 0));
    const PowerSlot* str = FindPlayerPower(s, PowerId::STRENGTH);
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->amount, 4) << "the upgraded Flex applies +4 Strength, not +2";
}

}  // namespace
}  // namespace sts::engine
