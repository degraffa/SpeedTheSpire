// The colorless UNCOMMON cards:
// per-card base/upgraded tier-2 behaviour, Deep Breath's fused double reshuffle
// and its shuffle_rng accounting, Impatience's conditional draw, Mind Blast's
// draw-pile-sized base, Madness's cardRandomRng REJECTION-SAMPLING draw
// accounting, No Block's modifyBlockLast pass (and the two-pass ordering that
// makes it right), and a directed public advance()/legal_actions() script.
// Expected values are hand-computed from the cited decompiled Java (see the
// per-row provenance in registry/cards.yaml).
//
// The two interior CardId gaps B3.10c owns are pinned here too: a
// reservation nothing asserts is indistinguishable from an omission.

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/piles.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/rng_jdk.hpp"
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
    return s;
}

CombatState MakeThree(int16_t energy = 6) {
    CombatState s = MakeCombat(energy, 100);
    s.monster_count = 3;
    for (uint8_t i = 0; i < 3; ++i) {
        s.monsters[i].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
        s.monsters[i].hp = 100;
        s.monsters[i].max_hp = 100;
    }
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

CardPoolIndex AddDrawTop(CombatState& s, CardId id, uint8_t upgrade = 0) {
    const CardPoolIndex pi = AddCard(s, id, upgrade);
    s.draw[s.draw_count++] = pi;
    return pi;
}

CardPoolIndex AddDiscard(CombatState& s, CardId id, uint8_t upgrade = 0) {
    const CardPoolIndex pi = AddCard(s, id, upgrade);
    s.discard[s.discard_count++] = pi;
    return pi;
}

void AddPower(CombatState& s, uint8_t actor, PowerId id, int16_t amount) {
    PowerSlot* slots = actor == kActorPlayer ? s.player_powers
                                             : s.monsters[actor].powers;
    uint8_t* count = actor == kActorPlayer ? &s.player_power_count
                                           : &s.monsters[actor].power_count;
    slots[*count] = PowerSlot{static_cast<uint16_t>(id), amount};
    ++*count;
}

const PowerSlot* FindPower(const CombatState& s, uint8_t actor, PowerId id) {
    const PowerSlot* slots = actor == kActorPlayer ? s.player_powers
                                                   : s.monsters[actor].powers;
    const uint8_t count = actor == kActorPlayer ? s.player_power_count
                                                : s.monsters[actor].power_count;
    for (uint8_t i = 0; i < count; ++i) {
        if (slots[i].power_id == static_cast<uint16_t>(id)) {
            return &slots[i];
        }
    }
    return nullptr;
}

bool PileHas(const CardPoolIndex* pile, uint8_t count, CardPoolIndex pi) {
    for (uint8_t i = 0; i < count; ++i) {
        if (pile[i] == pi) {
            return true;
        }
    }
    return false;
}

void Play(CombatState& s, uint8_t hand_slot = 0, uint8_t target = 0) {
    ASSERT_TRUE(queue_card_play(s, hand_slot, target));
    pump(s, default_monster_turn);
}

void EndTurn(CombatState& s) {
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    pump(s, default_monster_turn);
}

StepResult Step(CombatState& s, Action a) {
    StepResult r{};
    advance(std::span<CombatState>(&s, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&r, 1));
    return r;
}

int FindHandSlot(const CombatState& s, CardId id) {
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        if (s.card_pool[s.hand[i]].card_id == static_cast<uint16_t>(id)) {
            return i;
        }
    }
    return -1;
}

// ===========================================================================
// Registry roster + the reserved interior ids
// ===========================================================================

TEST(CardColorlessUncommonsRegistry, ExactRosterInCardLibraryOrder) {
    // CardLibrary.addColorlessCards (CardLibrary.java:799-834) adds the colorless
    // cards ALPHABETICALLY; ids 92-111 map 1:1 onto the twenty UNCOMMONs in that
    // order and these fourteen are the non-contiguous subset this batch owns.
    const std::array<std::pair<int, CardId>, 14> roster{{
        {92, CardId::BANDAGE_UP},        {93, CardId::BLIND},
        {95, CardId::DEEP_BREATH},       {97, CardId::DRAMATIC_ENTRANCE},
        {99, CardId::FINESSE},           {100, CardId::FLASH_OF_STEEL},
        {102, CardId::GOOD_INSTINCTS},   {103, CardId::IMPATIENCE},
        {105, CardId::MADNESS},          {106, CardId::MIND_BLAST},
        {107, CardId::PANACEA},          {108, CardId::PANIC_BUTTON},
        {110, CardId::SWIFT_STRIKE},     {111, CardId::TRIP},
    }};
    int previous = 91;  // the last id before this batch (Reaper)
    for (const auto& [id, card] : roster) {
        EXPECT_EQ(static_cast<int>(card), id);
        EXPECT_GT(id, previous) << "library order must be non-decreasing in id";
        previous = id;
        const CardDef* d = card_def(card);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(d->id, card);
    }
}

TEST(CardColorlessUncommonsRegistry, InteriorIdsStayReserved) {
    // 101 Forethought and 109 Purity belong to mandatory next task B3.10c.
    // They must stay empty in this branch, not quietly backfilled.
    for (const int reserved : {101, 109}) {
        EXPECT_EQ(card_def(static_cast<CardId>(reserved)), nullptr)
            << "id " << reserved << " is reserved and must have no row";
    }
}

TEST(CardColorlessUncommonsRegistry, CostsFlagsAndTargeting) {
    struct Row {
        CardId id;
        uint8_t cost;
        uint8_t up_cost;
        CardType type;
        bool needs_target;
    };
    const std::array<Row, 14> rows{{
        {CardId::BANDAGE_UP, 0, 0, CardType::SKILL, false},
        {CardId::BLIND, 0, 0, CardType::SKILL, true},
        {CardId::DEEP_BREATH, 0, 0, CardType::SKILL, false},
        {CardId::DRAMATIC_ENTRANCE, 0, 0, CardType::ATTACK, false},
        {CardId::FINESSE, 0, 0, CardType::SKILL, false},
        {CardId::FLASH_OF_STEEL, 0, 0, CardType::ATTACK, true},
        {CardId::GOOD_INSTINCTS, 0, 0, CardType::SKILL, false},
        {CardId::IMPATIENCE, 0, 0, CardType::SKILL, false},
        // Madness and Mind Blast are the only two whose upgrade is a COST change
        // (upgradeBaseCost, Madness.java:38 / MindBlast.java:65).
        {CardId::MADNESS, 1, 0, CardType::SKILL, false},
        {CardId::MIND_BLAST, 2, 1, CardType::ATTACK, true},
        {CardId::PANACEA, 0, 0, CardType::SKILL, false},
        {CardId::PANIC_BUTTON, 0, 0, CardType::SKILL, false},
        {CardId::SWIFT_STRIKE, 0, 0, CardType::ATTACK, true},
        {CardId::TRIP, 0, 0, CardType::SKILL, true},
    }};
    for (const Row& r : rows) {
        const CardDef* d = card_def(r.id);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(card_cost(*d, 0), r.cost);
        EXPECT_EQ(card_cost(*d, 1), r.up_cost);
        EXPECT_EQ(d->type, r.type);
        EXPECT_EQ(d->needs_target, r.needs_target);
        EXPECT_FALSE(d->random_target);
    }

    // The five exhausting rows (AbstractCard.exhaust = true in each ctor), on BOTH
    // upgrade rows -- no upgrade in this batch adds or removes an exhaust.
    for (const CardId id : {CardId::BANDAGE_UP, CardId::DRAMATIC_ENTRANCE,
                            CardId::MADNESS, CardId::PANACEA,
                            CardId::PANIC_BUTTON}) {
        const CardDef* d = card_def(id);
        ASSERT_NE(d, nullptr);
        EXPECT_TRUE(has_card_flag(card_flags(*d, 0), CardFlag::EXHAUST));
        EXPECT_TRUE(has_card_flag(card_flags(*d, 1), CardFlag::EXHAUST));
    }
    // The two innate rows (isInnate = true: DramaticEntrance.java:32,
    // MindBlast.java:34).
    for (const CardId id : {CardId::DRAMATIC_ENTRANCE, CardId::MIND_BLAST}) {
        const CardDef* d = card_def(id);
        ASSERT_NE(d, nullptr);
        EXPECT_TRUE(has_card_flag(card_flags(*d, 0), CardFlag::INNATE));
        EXPECT_TRUE(has_card_flag(card_flags(*d, 1), CardFlag::INNATE));
    }
    // Swift Strike is the only Strike-tagged row here (SwiftStrike.java:32);
    // Flash of Steel deliberately is not (its ctor adds no tags).
    EXPECT_TRUE(card_def(CardId::SWIFT_STRIKE)->is_strike);
    EXPECT_FALSE(card_def(CardId::FLASH_OF_STEEL)->is_strike);
}

TEST(CardColorlessUncommonsRegistry, B310bRowsPoolsAndSparseIds) {
    struct Row {
        CardId id;
        int raw_id;
        uint8_t cost;
        bool target;
    };
    const std::array<Row, 4> rows{{
        {CardId::DARK_SHACKLES, 94, 0, true},
        {CardId::DISCOVERY, 96, 1, false},
        {CardId::ENLIGHTENMENT, 98, 0, false},
        {CardId::JACK_OF_ALL_TRADES, 104, 0, false},
    }};
    for (const Row& r : rows) {
        EXPECT_EQ(static_cast<int>(r.id), r.raw_id);
        const CardDef* d = card_def(r.id);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(d->base_cost, r.cost);
        EXPECT_EQ(d->needs_target, r.target);
        EXPECT_EQ(d->type, CardType::SKILL);
    }
    EXPECT_EQ(static_cast<int>(PowerId::SHACKLED), 78);

    auto colorless_has = [](CardId id) {
        return std::find(kColorlessCombatPool.begin(),
                         kColorlessCombatPool.end(), id) !=
               kColorlessCombatPool.end();
    };
    EXPECT_FALSE(colorless_has(CardId::BANDAGE_UP))
        << "the HEALING tag excludes Bandage Up";
    for (CardId id : {CardId::DARK_SHACKLES, CardId::DISCOVERY,
                      CardId::ENLIGHTENMENT, CardId::JACK_OF_ALL_TRADES}) {
        EXPECT_TRUE(colorless_has(id));
    }
    for (CardId id : kColorlessCombatPool) {
        EXPECT_NE(card_def(id), nullptr);
    }

    auto red_combat_has = [](CardId id) {
        return std::find(kIroncladCombatPool.begin(),
                         kIroncladCombatPool.end(), id) !=
               kIroncladCombatPool.end();
    };
    EXPECT_TRUE(red_combat_has(CardId::ANGER));
    EXPECT_TRUE(red_combat_has(CardId::SHRUG_IT_OFF));
    EXPECT_TRUE(red_combat_has(CardId::BARRICADE));
    EXPECT_FALSE(red_combat_has(CardId::STRIKE))
        << "BASIC cards never enter srcCommon/srcUncommon/srcRare";
    EXPECT_FALSE(red_combat_has(CardId::FEED));
    EXPECT_FALSE(red_combat_has(CardId::REAPER));
}

// ===========================================================================
// Bandage Up -- heal 4/6, exhausts
// ===========================================================================

TEST(CardColorlessUncommonsBandageUp, HealsAndExhausts) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        s.player_hp = 50;
        const CardPoolIndex pi = AddHand(s, CardId::BANDAGE_UP, up);
        Play(s, 0);
        EXPECT_EQ(s.player_hp, up == 0 ? 54 : 56);
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, pi));
    }
}

TEST(CardColorlessUncommonsBandageUp, HealClampsAtMaxHp) {
    CombatState s = MakeCombat();
    s.player_hp = 78;  // heal 4 would overshoot 80
    AddHand(s, CardId::BANDAGE_UP);
    Play(s, 0);
    EXPECT_EQ(s.player_hp, 80);
}

// ===========================================================================
// Blind / Trip -- single-target base, whole-group upgraded
// ===========================================================================

TEST(CardColorlessUncommonsBlind, WeakOneTargetBaseAllLiveUpgraded) {
    {
        CombatState s = MakeThree();
        AddHand(s, CardId::BLIND);
        Play(s, 0, /*target=*/1);
        EXPECT_EQ(FindPower(s, 0, PowerId::WEAK), nullptr);
        ASSERT_NE(FindPower(s, 1, PowerId::WEAK), nullptr);
        EXPECT_EQ(FindPower(s, 1, PowerId::WEAK)->amount, 2);
        EXPECT_EQ(FindPower(s, 2, PowerId::WEAK), nullptr);
    }
    {
        CombatState s = MakeThree();
        s.monsters[2].hp = 0;  // ApplyPowerAction skips a dead target
        AddHand(s, CardId::BLIND, /*upgrade=*/1);
        Play(s, 0, /*target=*/0);
        for (uint8_t m = 0; m < 2; ++m) {
            ASSERT_NE(FindPower(s, m, PowerId::WEAK), nullptr) << "slot " << m;
            EXPECT_EQ(FindPower(s, m, PowerId::WEAK)->amount, 2)
                << "upgrade() changes only the TARGET -- there is no "
                   "upgradeMagicNumber (Blind.java:44-52)";
        }
        EXPECT_EQ(FindPower(s, 2, PowerId::WEAK), nullptr);
    }
}

TEST(CardColorlessUncommonsTrip, VulnerableOneTargetBaseAllLiveUpgraded) {
    {
        CombatState s = MakeThree();
        AddHand(s, CardId::TRIP);
        Play(s, 0, /*target=*/2);
        EXPECT_EQ(FindPower(s, 0, PowerId::VULNERABLE), nullptr);
        EXPECT_EQ(FindPower(s, 1, PowerId::VULNERABLE), nullptr);
        ASSERT_NE(FindPower(s, 2, PowerId::VULNERABLE), nullptr);
        EXPECT_EQ(FindPower(s, 2, PowerId::VULNERABLE)->amount, 2);
    }
    {
        CombatState s = MakeThree();
        AddHand(s, CardId::TRIP, /*upgrade=*/1);
        Play(s, 0, /*target=*/0);
        for (uint8_t m = 0; m < 3; ++m) {
            ASSERT_NE(FindPower(s, m, PowerId::VULNERABLE), nullptr) << "slot " << m;
            EXPECT_EQ(FindPower(s, m, PowerId::VULNERABLE)->amount, 2)
                << "Trip.upgrade (:49-57) changes only the target";
        }
    }
}

// ===========================================================================
// Deep Breath -- the FUSED double shuffle, then the draw
// ===========================================================================

TEST(CardColorlessUncommonsDeepBreath, TwoShuffleRngDrawsAndTheExactPermutation) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        // Three in the draw pile, four in the discard: the draw pile is NOT empty,
        // so a plain shuffle_discard_into_draw could not produce this answer -- the
        // SECOND shuffle is over the combined pile.
        std::vector<CardPoolIndex> draw_before;
        for (const CardId c : {CardId::STRIKE, CardId::DEFEND, CardId::BASH}) {
            draw_before.push_back(AddDrawTop(s, c));
        }
        std::vector<CardPoolIndex> discard_before;
        for (const CardId c : {CardId::STRIKE, CardId::STRIKE, CardId::DEFEND,
                               CardId::SHRUG_IT_OFF}) {
            discard_before.push_back(AddDiscard(s, c));
        }
        const CardPoolIndex deep = AddHand(s, CardId::DEEP_BREATH, up);
        s.shuffle_rng = from_seed(1234);

        // Independent oracle, built from the golden-tested JDK primitives:
        // EmptyDeckShuffleAction shuffles the DISCARD with one shuffleRng.randomLong
        // and appends it onto the draw pile's tail (addToTop == end of the list),
        // then ShuffleAction shuffles the WHOLE draw pile with a second one. The
        // Deep Breath itself is NOT in the shuffled set -- it is in limbo
        // (cardInUse) for the whole of both actions.
        RngStream probe = from_seed(1234);
        std::vector<CardPoolIndex> expect_draw = draw_before;
        std::vector<CardPoolIndex> shuffled_discard = discard_before;
        {
            JdkRandom r1(random_long(probe));
            jdk_shuffle(std::span<CardPoolIndex>(shuffled_discard), r1);
        }
        expect_draw.insert(expect_draw.end(), shuffled_discard.begin(),
                           shuffled_discard.end());
        {
            JdkRandom r2(random_long(probe));
            jdk_shuffle(std::span<CardPoolIndex>(expect_draw), r2);
        }
        const int32_t before = s.shuffle_rng.counter;

        Play(s, 0);

        EXPECT_EQ(s.shuffle_rng.counter, before + 2)
            << "EmptyDeckShuffleAction AND ShuffleAction(drawPile) -- TWO "
               "shuffleRng.randomLong() draws, not one";
        ASSERT_EQ(s.discard_count, 1);
        EXPECT_EQ(s.discard[0], deep)
            << "the played card was in limbo during both shuffles and is filed to "
               "the discard afterwards (UseCardAction.java:126)";
        // The draw then takes 1 (base) or 2 (upgraded) off the top == the tail.
        const int drawn = up == 0 ? 1 : 2;
        ASSERT_EQ(s.draw_count, static_cast<int>(expect_draw.size()) - drawn);
        for (uint8_t i = 0; i < s.draw_count; ++i) {
            EXPECT_EQ(s.draw[i], expect_draw[i]) << "draw slot " << int{i};
        }
        EXPECT_EQ(s.hand_count, drawn);
        for (int k = 0; k < drawn; ++k) {
            EXPECT_EQ(s.hand[k],
                      expect_draw[expect_draw.size() - 1 - static_cast<size_t>(k)]);
        }
    }
}

TEST(CardColorlessUncommonsDeepBreath, EmptyDiscardDrawsNoShuffleRngAtAll) {
    // The guard's input is the discard pile as DeepBreath.use sees it -- which
    // never contains the Deep Breath, because useCard queues the card's actions
    // before the UseCardAction that files it away. So an "empty" discard means
    // empty apart from the card being played, and the whole pair is skipped.
    CombatState s = MakeCombat();
    AddDrawTop(s, CardId::STRIKE);
    AddDrawTop(s, CardId::DEFEND);
    const CardPoolIndex deep = AddHand(s, CardId::DEEP_BREATH);
    s.shuffle_rng = from_seed(99);
    const int32_t before = s.shuffle_rng.counter;
    const CardPoolIndex old_top = s.draw[s.draw_count - 1];

    Play(s, 0);

    EXPECT_EQ(s.shuffle_rng.counter, before)
        << "both actions share ONE `discardPile.size() > 0` guard "
           "(DeepBreath.java:34-38): an empty discard skips BOTH, so ZERO draws";
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], old_top) << "the draw pile was not reordered";
    EXPECT_EQ(s.draw_count, 1);
    ASSERT_EQ(s.discard_count, 1);
    EXPECT_EQ(s.discard[0], deep);
}

TEST(CardColorlessUncommonsDeepBreath, ThePlayedCardIsNeverShuffledIntoTheDrawPile) {
    // The sharpest form of the limbo rule: with a discard of exactly one OTHER
    // card, the game shuffles that one card in and leaves the Deep Breath behind.
    CombatState s = MakeCombat();
    const CardPoolIndex only_discard = AddDiscard(s, CardId::STRIKE);
    const CardPoolIndex deep = AddHand(s, CardId::DEEP_BREATH);
    s.shuffle_rng = from_seed(4242);
    Play(s, 0);
    EXPECT_FALSE(PileHas(s.draw, s.draw_count, deep))
        << "the card whose use() queued the reshuffle is in limbo throughout it";
    ASSERT_EQ(s.discard_count, 1);
    EXPECT_EQ(s.discard[0], deep);
    // The one shuffled-in card was drawn straight back by the DrawCardAction(1).
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], only_discard);
}

// ===========================================================================
// Dramatic Entrance / Flash of Steel / Swift Strike / Finesse / Good Instincts
// ===========================================================================

TEST(CardColorlessUncommonsDramaticEntrance, HitsEveryLiveEnemyAndExhausts) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeThree();
        s.monsters[2].hp = 0;
        const CardPoolIndex pi = AddHand(s, CardId::DRAMATIC_ENTRANCE, up);
        Play(s, 0);
        const int dmg = up == 0 ? 8 : 12;
        EXPECT_EQ(s.monsters[0].hp, 100 - dmg);
        EXPECT_EQ(s.monsters[1].hp, 100 - dmg);
        EXPECT_EQ(s.monsters[2].hp, 0);
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, pi));
    }
}

TEST(CardColorlessUncommonsFlashOfSteel, DamageThenDrawOne) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddDrawTop(s, CardId::STRIKE);
        AddDrawTop(s, CardId::DEFEND);
        AddHand(s, CardId::FLASH_OF_STEEL, up);
        Play(s, 0);
        EXPECT_EQ(s.monsters[0].hp, up == 0 ? 97 : 94);
        EXPECT_EQ(s.hand_count, 1) << "DrawCardAction(p, 1) -- a literal 1 at "
                                      "both upgrade levels";
        EXPECT_EQ(s.draw_count, 1);
    }
}

TEST(CardColorlessUncommonsSwiftStrike, DamageOnly) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::SWIFT_STRIKE, up);
        Play(s, 0);
        EXPECT_EQ(s.monsters[0].hp, up == 0 ? 93 : 90);
        EXPECT_EQ(s.hand_count, 0);
    }
}

TEST(CardColorlessUncommonsFinesse, BlockThenDrawOne) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddDrawTop(s, CardId::STRIKE);
        AddHand(s, CardId::FINESSE, up);
        Play(s, 0);
        EXPECT_EQ(s.player_block, up == 0 ? 2 : 4);
        EXPECT_EQ(s.hand_count, 1);
    }
}

TEST(CardColorlessUncommonsGoodInstincts, BlockOnly) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::GOOD_INSTINCTS, up);
        Play(s, 0);
        EXPECT_EQ(s.player_block, up == 0 ? 6 : 9);
    }
}

// ===========================================================================
// Impatience -- ConditionalDrawAction
// ===========================================================================

TEST(CardColorlessUncommonsImpatience, DrawsOnlyWithNoAttackInHand) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        for (int i = 0; i < 4; ++i) {
            AddDrawTop(s, CardId::DEFEND);
        }
        AddHand(s, CardId::IMPATIENCE, up);
        AddHand(s, CardId::GOOD_INSTINCTS);  // a SKILL, so the condition holds
        Play(s, 0);
        const int drawn = up == 0 ? 2 : 3;
        EXPECT_EQ(s.hand_count, 1 + drawn)
            << "the surviving Good Instincts plus the drawn cards";
        EXPECT_EQ(s.draw_count, 4 - drawn);
    }
}

TEST(CardColorlessUncommonsImpatience, AnAttackInHandCancelsTheDrawEntirely) {
    CombatState s = MakeCombat();
    for (int i = 0; i < 4; ++i) {
        AddDrawTop(s, CardId::DEFEND);
    }
    AddHand(s, CardId::IMPATIENCE);
    AddHand(s, CardId::SWIFT_STRIKE);  // an ATTACK -- checkCondition() is false
    Play(s, 0);
    EXPECT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.draw_count, 4) << "nothing is queued at all "
                                  "(ConditionalDrawAction.java:32-35)";
}

TEST(CardColorlessUncommonsImpatience, DrawIsQueuedSoNoDrawStillGatesIt) {
    // The draw goes through the DRAW opcode, so DrawCardAction's No Draw early-out
    // (DrawCardAction.java:69-73) still applies -- which it would not if the body
    // moved cards directly.
    CombatState s = MakeCombat();
    for (int i = 0; i < 4; ++i) {
        AddDrawTop(s, CardId::DEFEND);
    }
    AddPower(s, kActorPlayer, PowerId::NO_DRAW, -1);
    AddHand(s, CardId::IMPATIENCE);
    Play(s, 0);
    EXPECT_EQ(s.hand_count, 0);
    EXPECT_EQ(s.draw_count, 4);
}

// ===========================================================================
// Mind Blast -- base damage == the draw pile size
// ===========================================================================

TEST(CardColorlessUncommonsMindBlast, BaseIsTheDrawPileSize) {
    for (const int pile : {0, 1, 7}) {
        CombatState s = MakeCombat();
        for (int i = 0; i < pile; ++i) {
            AddDrawTop(s, CardId::DEFEND);
        }
        AddHand(s, CardId::MIND_BLAST);
        Play(s, 0);
        EXPECT_EQ(s.monsters[0].hp, 100 - pile)
            << "MindBlast.applyPowers:48 baseDamage = drawPile.size()";
        EXPECT_EQ(s.player_energy, 4) << "cost 2";
    }
}

TEST(CardColorlessUncommonsMindBlast, RunsTheOrdinaryDamagePipeline) {
    // baseDamage is replaced, but super.applyPowers() still runs -- so Strength
    // and Vulnerable scale it exactly as any other attack.
    CombatState s = MakeCombat();
    for (int i = 0; i < 4; ++i) {
        AddDrawTop(s, CardId::DEFEND);
    }
    AddPower(s, kActorPlayer, PowerId::STRENGTH, 3);
    AddPower(s, 0, PowerId::VULNERABLE, 2);
    AddHand(s, CardId::MIND_BLAST, /*upgrade=*/1);
    Play(s, 0);
    EXPECT_EQ(s.monsters[0].hp, 100 - compute_damage(s, kActorPlayer, 0, 4));
    EXPECT_EQ(s.player_energy, 5) << "upgradeBaseCost(1) (MindBlast.java:65)";
}

// ===========================================================================
// Panacea
// ===========================================================================

TEST(CardColorlessUncommonsPanacea, GrantsArtifactAndExhausts) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        const CardPoolIndex pi = AddHand(s, CardId::PANACEA, up);
        Play(s, 0);
        ASSERT_NE(FindPower(s, kActorPlayer, PowerId::ARTIFACT), nullptr);
        EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::ARTIFACT)->amount,
                  up == 0 ? 1 : 2);
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, pi));
    }
}

// ===========================================================================
// Panic Button + No Block
// ===========================================================================

TEST(CardColorlessUncommonsPanicButton, BlockLandsBeforeItsOwnNoBlock) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        const CardPoolIndex pi = AddHand(s, CardId::PANIC_BUTTON, up);
        Play(s, 0);
        EXPECT_EQ(s.player_block, up == 0 ? 30 : 40)
            << "the GainBlockAction is addToBot FIRST (PanicButton.java:36), so "
               "the card's own block is gained before No Block exists";
        ASSERT_NE(FindPower(s, kActorPlayer, PowerId::NO_BLOCK), nullptr);
        EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::NO_BLOCK)->amount, 2)
            << "upgradeBlock(10) does not move the magicNumber";
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, pi));
    }
}

TEST(CardColorlessUncommonsPanicButton, NoBlockZeroesLaterCardBlock) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::PANIC_BUTTON);
    AddHand(s, CardId::GOOD_INSTINCTS);
    Play(s, 0);
    EXPECT_EQ(s.player_block, 30);
    Play(s, 0);  // Good Instincts: 6 card block, discarded by modifyBlockLast
    EXPECT_EQ(s.player_block, 30) << "NoBlockPower.modifyBlockLast returns 0.0f";
}

TEST(CardColorlessUncommonsPanicButton, NoBlockDecrementsAtEndOfRoundAndExpires) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::PANIC_BUTTON);
    Play(s, 0);
    ASSERT_NE(FindPower(s, kActorPlayer, PowerId::NO_BLOCK), nullptr);
    EndTurn(s);
    ASSERT_NE(FindPower(s, kActorPlayer, PowerId::NO_BLOCK), nullptr);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::NO_BLOCK)->amount, 1)
        << "ReducePowerAction(owner, owner, POWER_ID, 1) (NoBlockPower.java:48)";
    EndTurn(s);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::NO_BLOCK), nullptr)
        << "op_reduce_power removes the slot at <= 0";
}

TEST(CardColorlessUncommonsPanicButton, NoBlockLeavesDirectBlockAlone) {
    // modifyBlockLast lives in AbstractCard.applyPowersToBlock, so it only touches
    // CARD block. A direct GainBlockAction (kBlockNoPowers) runs neither pass.
    CombatState s = MakeCombat();
    AddPower(s, kActorPlayer, PowerId::NO_BLOCK, 2);
    ActionQueueItem blk{};
    blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
    blk.src = kActorPlayer;
    blk.tgt = kActorPlayer;
    blk.amount = 7;
    blk.flags = kBlockNoPowers;
    execute_opcode(s, blk);
    EXPECT_EQ(s.player_block, 7);
}

// ===========================================================================
// The two-pass block order -- the reason No Block is correct
// ===========================================================================
//
// AbstractCard.applyPowersToBlock (AbstractCard.java:2291-2307) runs modifyBlock
// over the WHOLE power list and only THEN modifyBlockLast over the whole list.
// These three cases together pin the ordering as the CAUSE, not a coincidence:
//   * Dexterity alone proves the first pass is really contributing 3;
//   * [No Block, Dexterity] is the case a single fused pass gets WRONG -- it
//     would zero first and then let Dexterity add 3 back, yielding 3;
//   * [Dexterity, No Block] yields 0 under either implementation, and is here to
//     show the answer does not depend on list order, which is exactly what having
//     a genuine second pass buys.

TEST(CardColorlessUncommonsBlockPasses, DexterityAloneAddsToCardBlock) {
    CombatState s = MakeCombat();
    AddPower(s, kActorPlayer, PowerId::DEXTERITY, 3);
    AddHand(s, CardId::GOOD_INSTINCTS);
    Play(s, 0);
    EXPECT_EQ(s.player_block, 9) << "6 + Dexterity 3, in the modifyBlock pass";
}

TEST(CardColorlessUncommonsBlockPasses,
     ModifyBlockLastBeatsADexterityLaterInTheList) {
    CombatState s = MakeCombat();
    AddPower(s, kActorPlayer, PowerId::NO_BLOCK, 2);   // FIRST in the list
    AddPower(s, kActorPlayer, PowerId::DEXTERITY, 3);  // AFTER it
    ASSERT_EQ(s.player_powers[0].power_id, static_cast<uint16_t>(PowerId::NO_BLOCK));
    ASSERT_EQ(s.player_powers[1].power_id, static_cast<uint16_t>(PowerId::DEXTERITY));
    AddHand(s, CardId::GOOD_INSTINCTS);
    Play(s, 0);
    EXPECT_EQ(s.player_block, 0)
        << "A SINGLE pass over this list would yield 3: No Block zeroes 6, then "
           "Dexterity adds 3 to the zero. The game runs modifyBlock over every "
           "power FIRST (6 -> 9) and modifyBlockLast over every power SECOND "
           "(9 -> 0), so the answer is 0. This assertion fails if the second "
           "pass is ever folded back into the first.";
}

TEST(CardColorlessUncommonsBlockPasses,
     ModifyBlockLastIsOrderIndependentAcrossTheList) {
    CombatState s = MakeCombat();
    AddPower(s, kActorPlayer, PowerId::DEXTERITY, 3);  // FIRST this time
    AddPower(s, kActorPlayer, PowerId::NO_BLOCK, 2);
    AddHand(s, CardId::GOOD_INSTINCTS);
    Play(s, 0);
    EXPECT_EQ(s.player_block, 0)
        << "the same answer with the list reversed -- a second pass cannot be "
           "outrun by a later modifyBlock contributor";
}

TEST(CardColorlessUncommonsBlockPasses, FrailStillScalesUnderTheFirstPass) {
    // The first pass is untouched by the split: Frail's 0.75f multiply and the
    // single floor after BOTH passes still produce the game's number.
    CombatState s = MakeCombat();
    AddPower(s, kActorPlayer, PowerId::FRAIL, 1);
    AddHand(s, CardId::GOOD_INSTINCTS);
    Play(s, 0);
    EXPECT_EQ(s.player_block, 4) << "floor(6 * 0.75f) == 4";
}

// ===========================================================================
// Madness -- cardRandomRng rejection sampling
// ===========================================================================

TEST(CardColorlessUncommonsMadness, NoEligibleCardConsumesNoRng) {
    CombatState s = MakeCombat();
    // Two permanently-free cards: neither costForTurn nor cost is > 0, so
    // MadnessAction never calls findAndModifyCard (MadnessAction.java:39).
    const CardPoolIndex a = AddHand(s, CardId::STRIKE);
    const CardPoolIndex b = AddHand(s, CardId::DEFEND);
    s.card_pool[a].cost_now = 0;
    s.card_pool[b].cost_now = 0;
    AddHand(s, CardId::MADNESS);
    s.card_random_rng = from_seed(7);
    const int32_t before = s.card_random_rng.counter;
    Play(s, 2);
    EXPECT_EQ(s.card_random_rng.counter, before)
        << "the guard fails, so getRandomCard is never reached";
}

TEST(CardColorlessUncommonsMadness, DrawCountIsTheRejectionSamplingAttemptCount) {
    // Slot 0 is permanently free (ineligible); slots 1 and 2 cost 1 and 2. Each
    // failed attempt is a WHOLE extra cardRandomRng draw, because
    // findAndModifyCard recurses rather than re-picking from a filtered list.
    int seeds_needing_a_retry = 0;
    for (const int64_t seed : {1, 2, 3, 4, 5, 6, 7, 8}) {
        CombatState s = MakeCombat();
        const CardPoolIndex free_slot = AddHand(s, CardId::STRIKE);
        s.card_pool[free_slot].cost_now = 0;  // permanent zero: no FOR_TURN flag
        const CardPoolIndex one = AddHand(s, CardId::DEFEND);      // cost 1
        const CardPoolIndex two = AddHand(s, CardId::BASH);        // cost 2
        AddHand(s, CardId::MADNESS);
        ASSERT_EQ(s.card_pool[one].cost_now, 1);
        ASSERT_EQ(s.card_pool[two].cost_now, 2);
        s.card_random_rng = from_seed(seed);

        // Independent oracle: hand.getRandomCard is group.get(rng.random(size-1))
        // over the hand AFTER Madness has left it, i.e. size 3.
        RngStream probe = from_seed(seed);
        int expected_draws = 0;
        int32_t pick = 0;
        do {
            pick = random(probe, 2);
            ++expected_draws;
        } while (pick == 0);  // slot 0 is the ineligible one
        if (expected_draws > 1) {
            ++seeds_needing_a_retry;
        }
        const int32_t before = s.card_random_rng.counter;

        Play(s, 3);  // Madness is hand slot 3

        EXPECT_EQ(s.card_random_rng.counter, before + expected_draws)
            << "seed " << seed << ": one draw per attempt";
        const CardPoolIndex hit = pick == 1 ? one : two;
        const CardPoolIndex missed = pick == 1 ? two : one;
        EXPECT_EQ(s.card_pool[hit].cost_now, 0) << "seed " << seed;
        EXPECT_NE(s.card_pool[missed].cost_now, 0) << "seed " << seed;
        EXPECT_EQ(s.card_pool[free_slot].cost_now, 0);
    }
    EXPECT_GT(seeds_needing_a_retry, 0)
        << "the seed set must actually exercise a rejection, or this test would "
           "only ever pin the one-draw case";
}

TEST(CardColorlessUncommonsMadness, BetterBranchIgnoresACostForTurnZeroCard) {
    // A card whose costForTurn was zeroed FOR THE TURN still has cost > 0, so it
    // sets neither betterPossible (costForTurn is not > 0) nor -- while a genuine
    // costForTurn > 0 card exists -- the branch findAndModifyCard tests. It must
    // never be the card Madness hits.
    CombatState s = MakeCombat();
    const CardPoolIndex temp_free = AddHand(s, CardId::BASH);
    s.card_pool[temp_free].cost_now = 0;
    s.card_pool[temp_free].flags = static_cast<uint16_t>(
        s.card_pool[temp_free].flags | card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN));
    const CardPoolIndex real = AddHand(s, CardId::DEFEND);  // costForTurn 1
    AddHand(s, CardId::MADNESS);
    s.card_random_rng = from_seed(11);
    Play(s, 2);
    EXPECT_EQ(s.card_pool[real].cost_now, 0) << "the betterPossible branch";
    EXPECT_TRUE(has_card_flag(s.card_pool[temp_free].flags,
                              CardFlag::COST_MODIFIED_FOR_TURN))
        << "the this-turn-only card was not touched";
}

TEST(CardColorlessUncommonsMadness, PossibleBranchZeroesPermanently) {
    // No card has costForTurn > 0, so betterPossible is false and the `cost > 0`
    // branch runs. The zeroing writes BOTH fields (MadnessAction.java:58-60), so
    // the this-turn marker must be CLEARED -- otherwise the end-of-turn sweep
    // would restore the registry cost.
    CombatState s = MakeCombat();
    const CardPoolIndex only = AddHand(s, CardId::BASH);  // registry cost 2
    s.card_pool[only].cost_now = 0;
    s.card_pool[only].flags = static_cast<uint16_t>(
        s.card_pool[only].flags | card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN));
    AddHand(s, CardId::MADNESS);
    s.card_random_rng = from_seed(3);
    const int32_t before = s.card_random_rng.counter;

    Play(s, 1);

    EXPECT_EQ(s.card_random_rng.counter, before + 1) << "one card, one attempt";
    EXPECT_EQ(s.card_pool[only].cost_now, 0);
    EXPECT_FALSE(has_card_flag(s.card_pool[only].flags,
                               CardFlag::COST_MODIFIED_FOR_TURN));
    EndTurn(s);
    EXPECT_EQ(s.card_pool[only].cost_now, 0)
        << "permanent for the combat: the end-turn sweep must not restore 2";
}

TEST(CardColorlessUncommonsMadness, ExhaustsAndCostsOneUntilUpgraded) {
    CombatState s = MakeCombat(/*energy=*/1);
    const CardPoolIndex pi = AddHand(s, CardId::MADNESS);
    AddHand(s, CardId::DEFEND);
    s.card_random_rng = from_seed(5);
    Play(s, 0);
    EXPECT_EQ(s.player_energy, 0);
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, pi));

    CombatState u = MakeCombat(/*energy=*/1);
    AddHand(u, CardId::MADNESS, /*upgrade=*/1);
    AddHand(u, CardId::DEFEND);
    u.card_random_rng = from_seed(5);
    Play(u, 0);
    EXPECT_EQ(u.player_energy, 1) << "upgradeBaseCost(0) (Madness.java:38)";
}

// ===========================================================================
// Dark Shackles -- pre-resolution Artifact gate + Shackled restoration
// ===========================================================================

TEST(CardColorlessUncommonsDarkShackles,
     LosesNineOrFifteenThenRestoresAtEndOfTurn) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        const int amount = up == 0 ? 9 : 15;
        const CardPoolIndex dark = AddHand(s, CardId::DARK_SHACKLES, up);
        Play(s, 0, 0);
        ASSERT_NE(FindPower(s, 0, PowerId::STRENGTH), nullptr);
        EXPECT_EQ(FindPower(s, 0, PowerId::STRENGTH)->amount, -amount);
        ASSERT_NE(FindPower(s, 0, PowerId::SHACKLED), nullptr);
        EXPECT_EQ(FindPower(s, 0, PowerId::SHACKLED)->amount, amount);
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, dark));

        EndTurn(s);
        EXPECT_EQ(FindPower(s, 0, PowerId::STRENGTH), nullptr)
            << "Strength(-N) + Strength(+N) queues the zero-stack removal";
        EXPECT_EQ(FindPower(s, 0, PowerId::SHACKLED), nullptr);
    }
}

TEST(CardColorlessUncommonsDarkShackles,
     ArtifactBlocksStrengthAndPreventsShackledFromBeingQueued) {
    CombatState s = MakeCombat();
    AddPower(s, 0, PowerId::ARTIFACT, 1);
    AddHand(s, CardId::DARK_SHACKLES);
    Play(s, 0, 0);

    EXPECT_EQ(FindPower(s, 0, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(FindPower(s, 0, PowerId::SHACKLED), nullptr)
        << "the hasPower(Artifact) test happened before Strength(-9) resolved";
    ASSERT_NE(FindPower(s, 0, PowerId::ARTIFACT), nullptr);
    EXPECT_EQ(FindPower(s, 0, PowerId::ARTIFACT)->amount, 0)
        << "the Strength debuff consumed the one Artifact charge";
}

TEST(CardColorlessUncommonsDarkShackles, ExistingStrengthReturnsExactly) {
    CombatState s = MakeCombat();
    AddPower(s, 0, PowerId::STRENGTH, 3);
    AddHand(s, CardId::DARK_SHACKLES);
    Play(s, 0, 0);
    ASSERT_NE(FindPower(s, 0, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(FindPower(s, 0, PowerId::STRENGTH)->amount, -6);
    EndTurn(s);
    ASSERT_NE(FindPower(s, 0, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(FindPower(s, 0, PowerId::STRENGTH)->amount, 3);
    EXPECT_EQ(FindPower(s, 0, PowerId::SHACKLED), nullptr);
}

// ===========================================================================
// Discovery -- generated choice + exact rejection-sampling accounting
// ===========================================================================

TEST(CardColorlessUncommonsDiscovery,
     OfferIsThreeDistinctRedCardsWithExactRngAccounting) {
    int seeds_with_duplicate_retry = 0;
    for (int64_t seed = 1; seed <= 64; ++seed) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::DISCOVERY);
        s.card_random_rng = from_seed(seed);

        RngStream probe = from_seed(seed);
        CardId expected[kDiscoveryChoiceCount]{};
        uint8_t n = 0;
        int draws = 0;
        while (n < kDiscoveryChoiceCount) {
            const int32_t pick =
                random(probe, static_cast<int32_t>(kIroncladCombatPoolCount) - 1);
            ++draws;
            const CardId id =
                kIroncladCombatPool[static_cast<unsigned>(pick)];
            bool dupe = false;
            for (uint8_t i = 0; i < n; ++i) {
                dupe = dupe || expected[i] == id;
            }
            if (!dupe) {
                expected[n++] = id;
            }
        }
        if (draws > kDiscoveryChoiceCount) {
            ++seeds_with_duplicate_retry;
        }

        Play(s, 0);
        ASSERT_EQ(s.action_count, 2);
        const ActionQueueItem& offer = s.action_queue[s.action_head];
        ASSERT_EQ(static_cast<Opcode>(offer.opcode), Opcode::DISCOVERY);
        EXPECT_EQ(s.card_random_rng.counter, probe.counter)
            << "seed " << seed << ": duplicates consume full retry draws";
        for (uint8_t i = 0; i < kDiscoveryChoiceCount; ++i) {
            EXPECT_EQ(discovery_choice_card(offer, i), expected[i])
                << "seed " << seed << ", offer slot " << int{i};
        }
        ActionMask mask{};
        legal_actions(s, mask);
        EXPECT_TRUE(mask.choice_pending);
        EXPECT_TRUE(mask.choice_from_generated);
        EXPECT_FALSE(mask.choice_from_discard);
        EXPECT_FALSE(mask.choice_from_exhaust);
        EXPECT_TRUE(mask.can_choose[0]);
        EXPECT_TRUE(mask.can_choose[1]);
        EXPECT_TRUE(mask.can_choose[2]);
        EXPECT_FALSE(mask.can_choose[3]);
    }
    EXPECT_GT(seeds_with_duplicate_retry, 0)
        << "the seed battery must cover at least one rejected duplicate";
}

TEST(CardColorlessUncommonsDiscovery,
     ChosenBaseCopyIsFreeThisTurnAndUpgradeDoesNotExhaust) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat(/*energy=*/3);
        const CardPoolIndex discovery = AddHand(s, CardId::DISCOVERY, up);
        s.card_random_rng = from_seed(123 + up);
        Play(s, 0);
        const ActionQueueItem offer = s.action_queue[s.action_head];

        uint8_t selected = 0;
        for (uint8_t i = 0; i < kDiscoveryChoiceCount; ++i) {
            const CardDef* d = card_def(discovery_choice_card(offer, i));
            ASSERT_NE(d, nullptr);
            if (d->base_cost > 0) {
                selected = i;
                break;
            }
        }
        const CardId expected = discovery_choice_card(offer, selected);
        Step(s, make_action(ActionVerb::CHOOSE, selected));

        ASSERT_EQ(s.hand_count, 1);
        const CardInstance& made = s.card_pool[s.hand[0]];
        EXPECT_EQ(made.card_id, static_cast<uint16_t>(expected));
        EXPECT_EQ(made.upgrade, 0);
        EXPECT_EQ(made.cost_now, 0);
        const CardDef* made_def = card_def(expected);
        ASSERT_NE(made_def, nullptr);
        EXPECT_EQ(has_card_flag(made.flags, CardFlag::COST_MODIFIED_FOR_TURN),
                  made_def->base_cost != 0);
        EXPECT_EQ(s.player_energy, up == 0 ? 2 : 2)
            << "Discovery costs 1 at both upgrade levels";
        EXPECT_EQ(PileHas(s.exhaust, s.exhaust_count, discovery), up == 0);
        EXPECT_EQ(PileHas(s.discard, s.discard_count, discovery), up != 0);
    }
}

// ===========================================================================
// Enlightenment -- temporary vs rest-of-combat cost base
// ===========================================================================

TEST(CardColorlessUncommonsEnlightenment, BaseCapsForTurnThenRestores) {
    CombatState s = MakeCombat();
    const CardPoolIndex bash = AddHand(s, CardId::BASH);
    AddHand(s, CardId::ENLIGHTENMENT);
    Play(s, 1);
    EXPECT_EQ(s.card_pool[bash].cost_now, 1);
    EXPECT_TRUE(has_card_flag(s.card_pool[bash].flags,
                              CardFlag::COST_MODIFIED_FOR_TURN));
    EndTurn(s);
    EXPECT_EQ(s.card_pool[bash].cost_now, 2);
}

TEST(CardColorlessUncommonsEnlightenment,
     UpgradedCapsBaseForTheRestOfCombat) {
    CombatState s = MakeCombat();
    const CardPoolIndex bash = AddHand(s, CardId::BASH);
    AddHand(s, CardId::ENLIGHTENMENT, /*upgrade=*/1);
    Play(s, 1);
    EXPECT_EQ(s.card_pool[bash].cost_now, 1);
    EXPECT_FALSE(has_card_flag(s.card_pool[bash].flags,
                               CardFlag::COST_MODIFIED_FOR_TURN));
    EndTurn(s);
    EXPECT_EQ(s.card_pool[bash].cost_now, 1);
}

TEST(CardColorlessUncommonsEnlightenment,
     TemporaryCapRestoresAConfusionModifiedBase) {
    CombatState s = MakeCombat();
    const CardPoolIndex defend = AddHand(s, CardId::DEFEND);
    s.card_pool[defend].cost_now = 3;  // Confusion writes cost and costForTurn
    AddHand(s, CardId::ENLIGHTENMENT);
    Play(s, 1);
    EXPECT_EQ(s.card_pool[defend].cost_now, 1);
    EXPECT_TRUE(has_card_flag(s.card_pool[defend].flags,
                              CardFlag::SAVED_BASE_COST));
    EXPECT_EQ(saved_base_cost(s.card_pool[defend].flags), 3);
    EndTurn(s);
    EXPECT_EQ(s.card_pool[defend].cost_now, 3)
        << "resetAttributes restores AbstractCard.cost, not the registry cost";
}

TEST(CardColorlessUncommonsEnlightenment,
     UpgradedPreservesACheaperCostForTurnThenRestoresItsNewBase) {
    CombatState s = MakeCombat();
    const CardPoolIndex bash = AddHand(s, CardId::BASH);
    s.card_pool[bash].cost_now = 0;
    s.card_pool[bash].flags = static_cast<uint16_t>(
        s.card_pool[bash].flags |
        card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN));
    AddHand(s, CardId::ENLIGHTENMENT, /*upgrade=*/1);

    Play(s, 1);
    EXPECT_EQ(s.card_pool[bash].cost_now, 0)
        << "Enlightenment+ writes cost=1 but leaves a cheaper costForTurn";
    EXPECT_TRUE(has_card_flag(s.card_pool[bash].flags,
                              CardFlag::COST_MODIFIED_FOR_TURN));
    EXPECT_EQ(saved_base_cost(s.card_pool[bash].flags), 1);

    EndTurn(s);
    EXPECT_EQ(s.card_pool[bash].cost_now, 1);
}

// ===========================================================================
// Jack of All Trades -- independent colorless-pool draws, duplicates allowed
// ===========================================================================

TEST(CardColorlessUncommonsJackOfAllTrades,
     OneOrTwoIndependentColorlessDrawsAndExhausts) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        const CardPoolIndex jack = AddHand(s, CardId::JACK_OF_ALL_TRADES, up);
        s.card_random_rng = from_seed(700 + up);
        RngStream probe = from_seed(700 + up);
        const int count = up == 0 ? 1 : 2;
        CardId expected[2]{};
        for (int i = 0; i < count; ++i) {
            const int32_t pick = random(
                probe, static_cast<int32_t>(kColorlessCombatPoolCount) - 1);
            expected[i] =
                kColorlessCombatPool[static_cast<unsigned>(pick)];
        }

        Play(s, 0);
        EXPECT_EQ(s.card_random_rng.counter, probe.counter);
        ASSERT_EQ(s.hand_count, count);
        for (int i = 0; i < count; ++i) {
            const CardInstance& made = s.card_pool[s.hand[i]];
            EXPECT_EQ(made.card_id, static_cast<uint16_t>(expected[i]));
            const CardDef* d = card_def(expected[i]);
            ASSERT_NE(d, nullptr);
            EXPECT_EQ(made.cost_now, d->base_cost);
            EXPECT_EQ(made.upgrade, 0);
        }
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, jack));
    }
}

// ===========================================================================
// Directed public-API script (advance() / legal_actions() only)
// ===========================================================================

TEST(CardColorlessUncommonsDirected, B310bGeneratedChoiceAndCostScript) {
    CombatState s = MakeCombat(/*energy=*/3, /*monster_hp=*/80);
    AddHand(s, CardId::DARK_SHACKLES);
    AddHand(s, CardId::DISCOVERY);
    AddHand(s, CardId::ENLIGHTENMENT);
    AddHand(s, CardId::JACK_OF_ALL_TRADES, /*upgrade=*/1);
    s.card_random_rng = from_seed(20260726);

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));
    ASSERT_NE(FindPower(s, 0, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(FindPower(s, 0, PowerId::STRENGTH)->amount, -9);

    int slot = FindHandSlot(s, CardId::DISCOVERY);
    ASSERT_GE(slot, 0);
    Step(s, make_action(ActionVerb::PLAY_CARD, static_cast<uint8_t>(slot)));
    ActionMask mask{};
    legal_actions(s, mask);
    ASSERT_TRUE(mask.choice_from_generated);
    Step(s, make_action(ActionVerb::CHOOSE, 0));

    slot = FindHandSlot(s, CardId::ENLIGHTENMENT);
    ASSERT_GE(slot, 0);
    Step(s, make_action(ActionVerb::PLAY_CARD, static_cast<uint8_t>(slot)));
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        EXPECT_LE(s.card_pool[s.hand[i]].cost_now, 1);
    }

    slot = FindHandSlot(s, CardId::JACK_OF_ALL_TRADES);
    ASSERT_GE(slot, 0);
    const uint8_t before = s.hand_count;
    Step(s, make_action(ActionVerb::PLAY_CARD, static_cast<uint8_t>(slot)));
    EXPECT_EQ(s.hand_count, static_cast<uint8_t>(before + 1))
        << "playing one hand card and creating two gives net +1";
}

TEST(CardColorlessUncommonsDirected, PanicButtonNoBlockDeepBreathScript) {
    CombatState s = MakeCombat(/*energy=*/3, /*monster_hp=*/60);
    s.player_hp = 40;
    // Draw pile: three cards. Discard: two, so Deep Breath's guard is satisfied.
    AddDrawTop(s, CardId::STRIKE);
    AddDrawTop(s, CardId::DEFEND);
    AddDrawTop(s, CardId::STRIKE);
    AddDiscard(s, CardId::DEFEND);
    AddDiscard(s, CardId::STRIKE);
    s.shuffle_rng = from_seed(2024);

    AddHand(s, CardId::PANIC_BUTTON);    // slot 0
    AddHand(s, CardId::GOOD_INSTINCTS);  // slot 1
    AddHand(s, CardId::BANDAGE_UP);      // slot 2
    AddHand(s, CardId::DEEP_BREATH);     // slot 3
    AddHand(s, CardId::SWIFT_STRIKE);    // slot 4

    ActionMask mask{};
    legal_actions(s, mask);
    ASSERT_TRUE(mask.can_play[0]);

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));   // Panic Button (free)
    EXPECT_EQ(s.player_energy, 3);
    EXPECT_EQ(s.player_block, 30);
    ASSERT_NE(FindPower(s, kActorPlayer, PowerId::NO_BLOCK), nullptr);

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));   // Good Instincts
    EXPECT_EQ(s.player_block, 30) << "6 card block discarded by modifyBlockLast";

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));   // Bandage Up: heal 4
    EXPECT_EQ(s.player_hp, 44) << "No Block touches BLOCK only, never healing";

    const int32_t shuffles_before = s.shuffle_rng.counter;
    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));   // Deep Breath
    EXPECT_EQ(s.shuffle_rng.counter, shuffles_before + 2);
    EXPECT_EQ(s.discard_count, 1)
        << "the two starting discards AND the Good Instincts played this turn were "
           "all shuffled into the draw pile; only the Deep Breath itself is left, "
           "because it was in limbo while its own reshuffle ran (Panic Button and "
           "Bandage Up exhausted)";
    ASSERT_EQ(s.hand_count, 2) << "Swift Strike plus the one drawn card";

    legal_actions(s, mask);
    // Swift Strike moved to slot 0 when Deep Breath left the hand.
    ASSERT_TRUE(mask.can_play[0]);
    ASSERT_TRUE(mask.can_play_target[0][0]);
    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));   // Swift Strike: 7
    EXPECT_EQ(s.monsters[0].hp, 53);
    EXPECT_EQ(s.player_energy, 3) << "every card played this turn was free";
}

}  // namespace
}  // namespace sts::engine
