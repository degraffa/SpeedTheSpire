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
#include "sts/engine/potions.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/state_hash.hpp"
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
    slots[*count] = PowerSlot{static_cast<uint16_t>(id), amount, 0, 0};
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

TEST(CardColorlessUncommonsRegistry, InteriorIdsAreFilledAndTheBlockIsComplete) {
    // 101 Forethought and 109 Purity were the two interior ids held open while
    // the engine had no OPTIONAL zero-to-N hand selection to express them with.
    // They are exactly the cards CardLibrary.addColorlessCards puts at those
    // alphabetical positions, and with them the colorless UNCOMMON block 92-111
    // is dense -- so this now pins completeness where it used to pin the holes.
    EXPECT_EQ(static_cast<int>(CardId::FORETHOUGHT), 101);
    EXPECT_EQ(static_cast<int>(CardId::PURITY), 109);
    for (int id = 92; id <= 111; ++id) {
        const CardDef* d = card_def(static_cast<CardId>(id));
        ASSERT_NE(d, nullptr) << "id " << id << " has no row";
        EXPECT_EQ(static_cast<int>(d->id), id);
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

// The upgraded-target STATE divergence, reproduced end-to-end. Blind.upgrade
// sets this.target = CardTarget.ALL_ENEMY (Blind.java:48; Trip.java:53), and
// GameActionManager's dequeue-time dead-target suppression applies ONLY to
// exact CardTarget.ENEMY (GameActionManager.java:264-283) -- so in the game an
// upgraded Blind whose QUEUED play outlives its selected monster still
// resolves and Weakens the survivors. The reachable route is Distilled Chaos:
// all three targets are rolled and BAKED at use time (DistilledChaosPotion.
// java:38-43, the ctor-argument getRandomMonster), so a Strike played first
// can kill the monster a queued Blind+ was aimed at. Before CardDef grew the
// upgraded-target column, the engine kept Blind+ at card-level ENEMY and both
// STATE-path reads of target_kind -- card_can_use's dead-target rejection and
// resolve_card_play's :264-283 suppression -- cancelled the play: no Weak
// anywhere, a real state divergence (the old ledger claim "ActionMask
// deviation, not a state one" was wrong).
TEST(CardColorlessUncommonsBlind, UpgradedPlaysThroughItsDeadBakedTarget) {
    // Find a card_random_rng seed whose first two live-monster rolls (over two
    // live monsters, random(rng, 1)) agree -- so the Strike and the Blind+ are
    // baked onto the SAME monster. Derived, not hardcoded, so the test cannot
    // rot if the rng implementation changes.
    int chosen = -1;
    uint8_t t01 = 0;
    for (int c = 1; c <= 64 && chosen < 0; ++c) {
        RngStream probe = from_seed(c);
        const uint8_t a = static_cast<uint8_t>(random(probe, 1));
        const uint8_t b = static_cast<uint8_t>(random(probe, 1));
        if (a == b) {
            chosen = c;
            t01 = a;
        }
    }
    ASSERT_GE(chosen, 1) << "no seed in 1..64 rolls a repeated target?";

    CombatState s = MakeCombat(/*energy=*/3, /*monster_hp=*/50);
    s.monster_count = 2;
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[1].hp = 50;
    s.monsters[1].max_hp = 50;
    s.monsters[t01].hp = 5;  // the doomed monster: one Strike (6) kills it
    s.card_random_rng = from_seed(chosen);

    // Draw pile bottom-to-top: [Defend, Blind+, Strike] -- PLAY_CARD FromDrawTop
    // pops the top, so the plays are Strike, then Blind+, then Defend.
    AddDrawTop(s, CardId::DEFEND);
    AddDrawTop(s, CardId::BLIND, /*upgrade=*/1);
    AddDrawTop(s, CardId::STRIKE);

    ASSERT_TRUE(use_potion(s, PotionId::DISTILLED_CHAOS, 0));
    pump(s);

    const uint8_t survivor = static_cast<uint8_t>(1 - t01);
    EXPECT_EQ(s.monsters[t01].hp, 0) << "the Strike killed its baked target";
    ASSERT_NE(FindPower(s, survivor, PowerId::WEAK), nullptr)
        << "Blind+ is CardTarget.ALL_ENEMY (Blind.java:48): the dead baked "
           "target must not cancel the play (GameActionManager.java:264-283 "
           "suppresses exact ENEMY only)";
    EXPECT_EQ(FindPower(s, survivor, PowerId::WEAK)->amount, 2);
    EXPECT_EQ(FindPower(s, t01, PowerId::WEAK), nullptr)
        << "ApplyPowerAction skips a dead target";
}

// The mask half of the same column: an upgraded Blind/Trip takes NO target in
// the game (CardTarget.ALL_ENEMY), so its ActionMask row must be all-false
// with can_play[i] carrying legality -- while the BASE card keeps its
// per-target row. (advance.cpp reads the upgrade-aware needs_target.)
TEST(CardColorlessUncommonsBlind, UpgradedTakesNoTargetInTheActionMask) {
    CombatState s = MakeThree();
    AddHand(s, CardId::BLIND);                 // slot 0: base -- targeted
    AddHand(s, CardId::BLIND, /*upgrade=*/1);  // slot 1: upgraded -- ALL_ENEMY
    AddHand(s, CardId::TRIP);                  // slot 2: base -- targeted
    AddHand(s, CardId::TRIP, /*upgrade=*/1);   // slot 3: upgraded -- ALL_ENEMY
    ActionMask m{};
    legal_actions(s, m);
    for (int slot : {0, 2}) {
        EXPECT_TRUE(m.can_play[slot]) << slot;
        bool any_target = false;
        for (int t = 0; t < kMonsterCap; ++t) {
            any_target = any_target || m.can_play_target[slot][t];
        }
        EXPECT_TRUE(any_target) << "base Blind/Trip is CardTarget.ENEMY";
    }
    for (int slot : {1, 3}) {
        EXPECT_TRUE(m.can_play[slot]) << slot;
        for (int t = 0; t < kMonsterCap; ++t) {
            EXPECT_FALSE(m.can_play_target[slot][t])
                << "upgraded Blind/Trip is CardTarget.ALL_ENEMY (Blind.java:48"
                   " / Trip.java:53): no target row";
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

// ===========================================================================
// OPTIONAL (zero-to-N) selection: Purity and upgraded Forethought
// ===========================================================================
//
// These two are the only cards whose hand-select screen is ended by a BUTTON
// rather than by a count, so everything below drives the public surface --
// legal_actions() to read the screen, advance(CHOOSE) to toggle, advance(
// CONFIRM) to press it -- and never reaches into the queue item.

// Open a screen by playing `card` and return the mask that describes it.
ActionMask OpenChoice(CombatState& s, uint8_t hand_slot) {
    StepResult r{};
    const Action a = make_action(ActionVerb::PLAY_CARD, hand_slot);
    advance(std::span<CombatState>(&s, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&r, 1));
    ActionMask m{};
    legal_actions(s, m);
    return m;
}

TEST(CardColorlessUncommonsRegistry, PurityAndForethoughtRows) {
    const CardDef* purity = card_def(CardId::PURITY);
    ASSERT_NE(purity, nullptr);
    EXPECT_EQ(purity->base_cost, 0);
    EXPECT_EQ(purity->upgraded_cost, 0);
    EXPECT_EQ(purity->type, CardType::SKILL);
    EXPECT_FALSE(purity->needs_target);
    EXPECT_EQ(purity->target_kind, CardTargetKind::NONE);
    EXPECT_TRUE(has_card_flag(purity->flags, CardFlag::EXHAUST));
    EXPECT_TRUE(has_card_flag(purity->upgraded_flags, CardFlag::EXHAUST));
    // magicNumber 3, upgradeMagicNumber(2) -> 5, carried as the step's amount.
    ASSERT_EQ(purity->step_count, 1);
    ASSERT_EQ(purity->upgraded_step_count, 1);
    EXPECT_EQ(static_cast<int>(purity->steps[0].op), static_cast<int>(Opcode::CHOOSE_CARD));
    EXPECT_EQ(purity->steps[0].amount, 3);
    EXPECT_EQ(purity->upgraded_steps[0].amount, 5);
    for (const CardEffectStep& st : {purity->steps[0], purity->upgraded_steps[0]}) {
        EXPECT_EQ(choose_kind_from_flags(st.extra), ChoiceKind::EXHAUST);
        EXPECT_TRUE(choose_is_optional(st.extra));
        EXPECT_FALSE(choose_is_random(st.extra))
            << "ExhaustAction(magic, false, true, true): isRandom is FALSE";
    }

    const CardDef* ft = card_def(CardId::FORETHOUGHT);
    ASSERT_NE(ft, nullptr);
    EXPECT_EQ(ft->base_cost, 0);
    EXPECT_EQ(ft->upgraded_cost, 0) << "upgrade() changes name and text only";
    EXPECT_EQ(ft->type, CardType::SKILL);
    EXPECT_FALSE(ft->needs_target);
    EXPECT_EQ(ft->flags, 0) << "Forethought does not exhaust";
    EXPECT_EQ(ft->upgraded_flags, 0);
    ASSERT_EQ(ft->step_count, 1);
    ASSERT_EQ(ft->upgraded_step_count, 1);
    // The ONLY difference between the rows is the selection mode.
    EXPECT_EQ(choose_kind_from_flags(ft->steps[0].extra),
              ChoiceKind::PUT_ON_DRAW_BOTTOM);
    EXPECT_EQ(choose_kind_from_flags(ft->upgraded_steps[0].extra),
              ChoiceKind::PUT_ON_DRAW_BOTTOM);
    EXPECT_FALSE(choose_is_optional(ft->steps[0].extra))
        << "base opens a MANDATORY exactly-one screen (open(msg, 1, false))";
    EXPECT_TRUE(choose_is_optional(ft->upgraded_steps[0].extra))
        << "upgraded opens open(msg, 99, true, true)";
    EXPECT_EQ(ft->steps[0].amount, 1);
    EXPECT_EQ(ft->upgraded_steps[0].amount, 99);
}

TEST(CardColorlessUncommonsRegistry, BothJoinTheColorlessCombatPool) {
    auto pool_has = [](CardId id) {
        return std::find(kColorlessCombatPool.begin(),
                         kColorlessCombatPool.end(),
                         id) != kColorlessCombatPool.end();
    };
    // COLORLESS, non-BASIC, and neither carries CardTags.HEALING (the one thing
    // returnTrulyRandomColorlessCardInCombat excludes) -- Purity.java:24-28 and
    // Forethought.java:24-26 add no tags at all.
    EXPECT_TRUE(pool_has(CardId::PURITY));
    EXPECT_TRUE(pool_has(CardId::FORETHOUGHT));
    // The colour-gated red pools are untouched by two COLORLESS SKILLs.
    for (const CardId id : kIroncladCombatPool) {
        EXPECT_NE(id, CardId::PURITY);
        EXPECT_NE(id, CardId::FORETHOUGHT);
    }
}

// --- Purity ---------------------------------------------------------------

TEST(CardColorlessUncommonsPurity, EmptyHandNeverOpensAScreen) {
    // ExhaustAction.java:76-79: hand.size() == 0 -> isDone, no screen. Purity is
    // the only card in hand, so by the time its action resolves the hand is
    // empty (the played card is in limbo).
    CombatState s = MakeCombat();
    const CardPoolIndex purity = AddHand(s, CardId::PURITY);
    const int32_t rng_before = s.card_random_rng.counter;

    ActionMask m = OpenChoice(s, 0);
    EXPECT_FALSE(m.choice_pending) << "nothing to show, so nothing to choose";
    EXPECT_TRUE(m.can_end_turn);
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, purity));
    EXPECT_EQ(s.card_random_rng.counter, rng_before)
        << "isRandom is false: Purity spends no RNG on any path";
}

TEST(CardColorlessUncommonsPurity, SmallHandStillOpensTheScreen) {
    // The branch that would auto-exhaust a hand of <= amount cards
    // (ExhaustAction.java:80-89) is guarded by `!this.anyNumber`, and Purity
    // passes anyNumber TRUE. So a 2-card hand under a 3-card Purity is NOT
    // silently exhausted -- the screen opens and zero is still a legal answer.
    CombatState s = MakeCombat();
    AddHand(s, CardId::PURITY);
    const CardPoolIndex a = AddHand(s, CardId::STRIKE);
    const CardPoolIndex b = AddHand(s, CardId::DEFEND);

    ActionMask m = OpenChoice(s, 0);
    ASSERT_TRUE(m.choice_pending);
    EXPECT_TRUE(m.choice_optional);
    EXPECT_TRUE(m.can_confirm_choice);
    EXPECT_EQ(m.choice_selected_count, 0);
    EXPECT_FALSE(m.can_end_turn);
    EXPECT_TRUE(m.can_choose[0]);
    EXPECT_TRUE(m.can_choose[1]);
    EXPECT_FALSE(m.can_choose[2]);
    EXPECT_EQ(s.hand_count, 2);
    EXPECT_TRUE(PileHas(s.hand, s.hand_count, a));
    EXPECT_TRUE(PileHas(s.hand, s.hand_count, b));
}

TEST(CardColorlessUncommonsPurity, ZeroCardConfirmExhaustsNothingButPurity) {
    CombatState s = MakeCombat();
    const CardPoolIndex purity = AddHand(s, CardId::PURITY);
    AddHand(s, CardId::STRIKE);
    AddHand(s, CardId::DEFEND);
    AddHand(s, CardId::BASH);

    ActionMask m = OpenChoice(s, 0);
    ASSERT_TRUE(m.choice_pending);
    ASSERT_TRUE(m.can_confirm_choice) << "canPickZero enables the button at open";

    Step(s, make_action(ActionVerb::CONFIRM));
    legal_actions(s, m);
    EXPECT_FALSE(m.choice_pending);
    EXPECT_EQ(s.hand_count, 3) << "the whole hand survived an empty confirm";
    EXPECT_EQ(s.exhaust_count, 1);
    EXPECT_EQ(s.exhaust[0], purity) << "only Purity's own exhaust=true fired";
    EXPECT_TRUE(m.can_end_turn) << "control is back with the player";
}

TEST(CardColorlessUncommonsPurity, PartialSelectionExhaustsInPickOrder) {
    // ExhaustAction.java:102-105 walks selectedCards.group, which the screen
    // built in PICK order (selectHoveredCard -> selectedCards.addToTop), so the
    // exhaust pile ends up in pick order too -- NOT hand order.
    CombatState s = MakeCombat();
    const CardPoolIndex purity = AddHand(s, CardId::PURITY);
    const CardPoolIndex strike = AddHand(s, CardId::STRIKE);
    const CardPoolIndex defend = AddHand(s, CardId::DEFEND);
    const CardPoolIndex bash = AddHand(s, CardId::BASH);

    ActionMask m = OpenChoice(s, 0);
    ASSERT_TRUE(m.choice_optional);
    // Pick the LAST hand card first, then the first: pick order reverses hand
    // order, which is the only way to tell the two apart.
    Step(s, make_action(ActionVerb::CHOOSE, 2));  // bash
    legal_actions(s, m);
    EXPECT_EQ(m.choice_selected_count, 1);
    EXPECT_EQ(s.hand[s.hand_count - 1], bash) << "the pick moved to the end";
    EXPECT_EQ(s.hand[0], strike) << "the rest of the hand kept its order";
    EXPECT_EQ(s.hand[1], defend);

    Step(s, make_action(ActionVerb::CHOOSE, 0));  // strike
    legal_actions(s, m);
    EXPECT_EQ(m.choice_selected_count, 2);
    ASSERT_EQ(s.hand_count, 3);
    EXPECT_EQ(s.hand[0], defend);
    EXPECT_EQ(s.hand[1], bash) << "picks stay in PICK order";
    EXPECT_EQ(s.hand[2], strike);

    Step(s, make_action(ActionVerb::CONFIRM));
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], defend);
    ASSERT_EQ(s.exhaust_count, 3);
    EXPECT_EQ(s.exhaust[0], bash) << "picked first, exhausted first";
    EXPECT_EQ(s.exhaust[1], strike);
    EXPECT_EQ(s.exhaust[2], purity) << "Purity's UseCardAction is queued LAST";
}

TEST(CardColorlessUncommonsPurity, DeselectAppendsToTheEndOfTheHand) {
    // updateSelectedCards (:441-443) puts a deselected card back with
    // hand.addToTop -- the END of the hand, not the slot it came from.
    CombatState s = MakeCombat();
    AddHand(s, CardId::PURITY);
    const CardPoolIndex strike = AddHand(s, CardId::STRIKE);
    const CardPoolIndex defend = AddHand(s, CardId::DEFEND);
    const CardPoolIndex bash = AddHand(s, CardId::BASH);

    ActionMask m = OpenChoice(s, 0);
    ASSERT_TRUE(m.choice_optional);
    Step(s, make_action(ActionVerb::CHOOSE, 0));  // pick strike
    legal_actions(s, m);
    // Hand is now [defend, bash | strike]; slot 2 is the pick and is toggleable.
    ASSERT_EQ(m.choice_selected_count, 1);
    EXPECT_TRUE(m.can_choose[2]);
    Step(s, make_action(ActionVerb::CHOOSE, 2));  // put strike back
    legal_actions(s, m);
    EXPECT_EQ(m.choice_selected_count, 0);
    ASSERT_EQ(s.hand_count, 3);
    EXPECT_EQ(s.hand[0], defend);
    EXPECT_EQ(s.hand[1], bash);
    EXPECT_EQ(s.hand[2], strike) << "back at the END, not at slot 0";

    Step(s, make_action(ActionVerb::CONFIRM));
    EXPECT_EQ(s.hand_count, 3) << "an empty confirm after a full round trip";
    EXPECT_EQ(s.exhaust_count, 1);
}

TEST(CardColorlessUncommonsPurity, MaxSelectionIsMagicNumberAndUpgradeMovesIt) {
    for (uint8_t up = 0; up < 2; ++up) {
        const int cap = up == 0 ? 3 : 5;
        CombatState s = MakeCombat();
        AddHand(s, CardId::PURITY, up);
        for (int i = 0; i < 6; ++i) {
            AddHand(s, CardId::STRIKE);
        }
        ActionMask m = OpenChoice(s, 0);
        ASSERT_TRUE(m.choice_optional);
        for (int i = 0; i < cap; ++i) {
            Step(s, make_action(ActionVerb::CHOOSE, 0));
        }
        legal_actions(s, m);
        EXPECT_EQ(m.choice_selected_count, cap);
        // The cap is reached: unpicked slots close, picked ones stay open
        // (selectHoveredCard's `numCardsToSelect > selectedCards.size()` gate).
        const uint8_t unpicked = static_cast<uint8_t>(s.hand_count - cap);
        for (uint8_t i = 0; i < s.hand_count; ++i) {
            EXPECT_EQ(m.can_choose[i], i >= unpicked)
                << "slot " << static_cast<int>(i) << " at cap " << cap;
        }
        EXPECT_TRUE(m.can_confirm_choice);

        Step(s, make_action(ActionVerb::CONFIRM));
        EXPECT_EQ(s.hand_count, static_cast<uint8_t>(6 - cap));
        EXPECT_EQ(s.exhaust_count, cap + 1) << "the picks plus Purity itself";
    }
}

// --- Forethought ----------------------------------------------------------

TEST(CardColorlessUncommonsForethought, BaseForcesTheSingleHandCardWithNoRng) {
    // ForethoughtAction.java:37-45: hand.size() == 1 && !chooseAny takes
    // getTopCard() -- the LAST hand entry -- with NO screen and, unlike
    // PutOnDeckAction's forced branch, NO cardRandomRng draw.
    CombatState s = MakeCombat();
    AddHand(s, CardId::FORETHOUGHT);
    const CardPoolIndex bash = AddHand(s, CardId::BASH);
    AddDrawTop(s, CardId::STRIKE);
    const int32_t rng_before = s.card_random_rng.counter;

    ActionMask m = OpenChoice(s, 0);
    EXPECT_FALSE(m.choice_pending) << "one eligible card is forced, not prompted";
    EXPECT_EQ(s.card_random_rng.counter, rng_before)
        << "ForethoughtAction bills no RNG on its forced path";
    EXPECT_EQ(s.hand_count, 0);
    ASSERT_EQ(s.draw_count, 2);
    EXPECT_EQ(s.draw[0], bash) << "addToBottom == draw index 0";
    EXPECT_TRUE(has_card_flag(s.card_pool[bash].flags,
                              CardFlag::FREE_TO_PLAY_ONCE))
        << "Bash costs 2 > 0";
}

TEST(CardColorlessUncommonsForethought, BasePromptsMandatoryOneWithNoConfirm) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::FORETHOUGHT);
    const CardPoolIndex strike = AddHand(s, CardId::STRIKE);
    const CardPoolIndex defend = AddHand(s, CardId::DEFEND);

    ActionMask m = OpenChoice(s, 0);
    ASSERT_TRUE(m.choice_pending);
    EXPECT_FALSE(m.choice_optional) << "open(msg, 1, false) is mandatory";
    EXPECT_FALSE(m.can_confirm_choice) << "a mandatory screen has no button move";
    EXPECT_EQ(m.choice_selected_count, 0);

    // CONFIRM is not legal here and must be a clean no-op.
    Step(s, make_action(ActionVerb::CONFIRM));
    legal_actions(s, m);
    EXPECT_TRUE(m.choice_pending) << "the screen is still open";
    EXPECT_EQ(s.hand_count, 2);

    Step(s, make_action(ActionVerb::CHOOSE, 1));  // defend
    EXPECT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], strike);
    ASSERT_EQ(s.draw_count, 1);
    EXPECT_EQ(s.draw[0], defend);
    EXPECT_TRUE(has_card_flag(s.card_pool[defend].flags,
                              CardFlag::FREE_TO_PLAY_ONCE));
}

TEST(CardColorlessUncommonsForethought, UpgradedZeroConfirmMovesNothing) {
    CombatState s = MakeCombat();
    const CardPoolIndex ft = AddHand(s, CardId::FORETHOUGHT, /*upgrade=*/1);
    AddHand(s, CardId::STRIKE);
    AddHand(s, CardId::DEFEND);

    ActionMask m = OpenChoice(s, 0);
    ASSERT_TRUE(m.choice_pending);
    EXPECT_TRUE(m.choice_optional);
    Step(s, make_action(ActionVerb::CONFIRM));

    EXPECT_EQ(s.hand_count, 2);
    EXPECT_EQ(s.draw_count, 0);
    EXPECT_EQ(s.exhaust_count, 0) << "Forethought does not exhaust";
    EXPECT_TRUE(PileHas(s.discard, s.discard_count, ft));
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        EXPECT_FALSE(has_card_flag(s.card_pool[s.hand[i]].flags,
                                   CardFlag::FREE_TO_PLAY_ONCE));
    }
}

TEST(CardColorlessUncommonsForethought, UpgradedMultiCardBottomOrder) {
    // Each moved card is addToBottom'd in pick order, and addToBottom inserts at
    // index 0 -- so the LAST pick ends up deepest and the FIRST pick is the one
    // drawn next of the three.
    CombatState s = MakeCombat();
    AddHand(s, CardId::FORETHOUGHT, /*upgrade=*/1);
    const CardPoolIndex strike = AddHand(s, CardId::STRIKE);
    const CardPoolIndex defend = AddHand(s, CardId::DEFEND);
    const CardPoolIndex bash = AddHand(s, CardId::BASH);
    const CardPoolIndex resting = AddDrawTop(s, CardId::STRIKE);

    ActionMask m = OpenChoice(s, 0);
    ASSERT_TRUE(m.choice_optional);
    Step(s, make_action(ActionVerb::CHOOSE, 0));  // pick strike  (1st)
    Step(s, make_action(ActionVerb::CHOOSE, 0));  // pick defend  (2nd)
    Step(s, make_action(ActionVerb::CHOOSE, 0));  // pick bash    (3rd)
    legal_actions(s, m);
    EXPECT_EQ(m.choice_selected_count, 3);
    EXPECT_EQ(m.can_confirm_choice, true);

    Step(s, make_action(ActionVerb::CONFIRM));
    EXPECT_EQ(s.hand_count, 0);
    ASSERT_EQ(s.draw_count, 4);
    EXPECT_EQ(s.draw[0], bash) << "last picked, deepest";
    EXPECT_EQ(s.draw[1], defend);
    EXPECT_EQ(s.draw[2], strike) << "first picked, nearest the top of the three";
    EXPECT_EQ(s.draw[3], resting) << "the pile that was already there is on top";
}

TEST(CardColorlessUncommonsForethought, FreeToPlayOnceOnlyForNonZeroCost) {
    // `if (c.cost > 0)` is strict and reads AbstractCard.cost -- the combat BASE
    // cost, which differs from costForTurn in both directions:
    //   Flash of Steel is a genuine 0-cost card             -> no grant
    //   Wound's Java cost is -2, the UNPLAYABLE sentinel    -> no grant
    //   Bash is 2                                           -> grant
    //   a Bash made free FOR THE TURN still has base cost 2 -> grant
    CombatState s = MakeCombat();
    AddHand(s, CardId::FORETHOUGHT, /*upgrade=*/1);
    const CardPoolIndex free_card = AddHand(s, CardId::FLASH_OF_STEEL);
    const CardPoolIndex clutter = AddHand(s, CardId::WOUND);
    const CardPoolIndex paid = AddHand(s, CardId::BASH);
    const CardPoolIndex temp_free = AddHand(s, CardId::BASH);
    s.card_pool[temp_free].cost_now = 0;
    s.card_pool[temp_free].flags = static_cast<uint16_t>(
        s.card_pool[temp_free].flags |
        card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN));

    ActionMask m = OpenChoice(s, 0);
    ASSERT_TRUE(m.choice_optional);
    for (int i = 0; i < 4; ++i) {
        Step(s, make_action(ActionVerb::CHOOSE, 0));
    }
    Step(s, make_action(ActionVerb::CONFIRM));
    ASSERT_EQ(s.draw_count, 4) << "an unplayable card is still selectable";

    EXPECT_FALSE(has_card_flag(s.card_pool[free_card].flags,
                               CardFlag::FREE_TO_PLAY_ONCE))
        << "a 0-cost card gains nothing";
    EXPECT_FALSE(has_card_flag(s.card_pool[clutter].flags,
                               CardFlag::FREE_TO_PLAY_ONCE))
        << "cost -2 is not > 0 either";
    EXPECT_TRUE(has_card_flag(s.card_pool[paid].flags,
                              CardFlag::FREE_TO_PLAY_ONCE));
    EXPECT_TRUE(has_card_flag(s.card_pool[temp_free].flags,
                              CardFlag::FREE_TO_PLAY_ONCE))
        << "setCostForTurn(0) does not move AbstractCard.cost";
}

TEST(CardColorlessUncommonsForethought, FreeToPlayOnceSurvivesOneCostZeroPlay) {
    // The whole lifetime, driven publicly: grant, draw it back, play it at ZERO
    // energy (hasEnoughEnergy admits it, useCard spends nothing), then see the
    // bit gone and the card payable again only at its real cost.
    CombatState s = MakeCombat(/*energy=*/1);
    AddHand(s, CardId::FORETHOUGHT, /*upgrade=*/1);
    const CardPoolIndex bash = AddHand(s, CardId::BASH);

    ActionMask m = OpenChoice(s, 0);
    ASSERT_TRUE(m.choice_optional);
    Step(s, make_action(ActionVerb::CHOOSE, 0));
    Step(s, make_action(ActionVerb::CONFIRM));
    ASSERT_EQ(s.draw_count, 1);
    ASSERT_TRUE(has_card_flag(s.card_pool[bash].flags,
                              CardFlag::FREE_TO_PLAY_ONCE));

    // Draw it back. Energy is 1; Bash costs 2, so only the grant makes it legal.
    draw_cards(s, 1);
    ASSERT_EQ(s.hand_count, 1);
    ASSERT_EQ(s.hand[0], bash);
    EXPECT_EQ(s.card_pool[bash].cost_now, 2) << "the grant is not a cost change";
    legal_actions(s, m);
    ASSERT_TRUE(m.can_play[0]);
    ASSERT_TRUE(m.can_play_target[0][0]);

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));
    EXPECT_EQ(s.player_energy, 1) << "freeToPlay() suppresses the spend";
    EXPECT_FALSE(has_card_flag(s.card_pool[bash].flags,
                               CardFlag::FREE_TO_PLAY_ONCE))
        << "UseCardAction.update:87 consumes it on the one play";
    ASSERT_TRUE(PileHas(s.discard, s.discard_count, bash));

    // Second time around it is an ordinary 2-cost card and 1 energy is not enough.
    s.discard_count = 0;
    s.hand[s.hand_count++] = bash;
    legal_actions(s, m);
    EXPECT_FALSE(m.can_play[0]) << "the free play was spent, the cost is back";
}

TEST(CardColorlessUncommonsForethought, FreeToPlayOnceDiesWithTheCombat) {
    // A card still in limbo when the combat ends is normalized by the terminal
    // flush, and the one-play bits do not outlive it -- the same discipline the
    // action-local exhaust bit follows there.
    CombatState s = MakeCombat(/*energy=*/3, /*monster_hp=*/1);
    AddHand(s, CardId::FORETHOUGHT, /*upgrade=*/1);
    const CardPoolIndex bash = AddHand(s, CardId::BASH);
    ActionMask m = OpenChoice(s, 0);
    ASSERT_TRUE(m.choice_optional);
    Step(s, make_action(ActionVerb::CHOOSE, 0));
    Step(s, make_action(ActionVerb::CONFIRM));
    ASSERT_TRUE(has_card_flag(s.card_pool[bash].flags,
                              CardFlag::FREE_TO_PLAY_ONCE));

    draw_cards(s, 1);
    legal_actions(s, m);
    ASSERT_TRUE(m.can_play_target[0][0]);
    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));  // Bash kills the monster
    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::COMBAT_OVER));
    EXPECT_FALSE(has_card_flag(s.card_pool[bash].flags,
                               CardFlag::FREE_TO_PLAY_ONCE));
}

// --- Shared surface -------------------------------------------------------

TEST(CardColorlessUncommonsOptionalChoice, IllegalMovesAreCleanNoOps) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::PURITY);
    AddHand(s, CardId::STRIKE);
    AddHand(s, CardId::DEFEND);

    ActionMask m = OpenChoice(s, 0);
    ASSERT_TRUE(m.choice_optional);
    const CombatState before = s;

    // Out-of-hand slot, a play and an end-turn while the screen is up.
    Step(s, make_action(ActionVerb::CHOOSE, 7));
    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));
    Step(s, make_action(ActionVerb::END_TURN));
    EXPECT_EQ(hash_state(s), hash_state(before))
        << "no illegal action may move the state";

    legal_actions(s, m);
    EXPECT_TRUE(m.choice_pending);
    EXPECT_EQ(m.choice_selected_count, 0);
    EXPECT_FALSE(m.can_end_turn);
    for (int i = 0; i < kHandCap; ++i) {
        EXPECT_FALSE(m.can_play[i]);
    }
}

TEST(CardColorlessUncommonsOptionalChoice, ConfirmIsIllegalWhenNoScreenIsOpen) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::STRIKE);
    ActionMask m{};
    legal_actions(s, m);
    ASSERT_FALSE(m.choice_pending);
    EXPECT_FALSE(m.can_confirm_choice);

    const CombatState before = s;
    Step(s, make_action(ActionVerb::CONFIRM));
    EXPECT_EQ(hash_state(s), hash_state(before));
}

TEST(CardColorlessUncommonsOptionalChoice, SuppliedMaskOverloadAgreesThroughout) {
    // The four-span overload's debug contract assert covers the three new mask
    // fields; drive a whole optional selection through it to prove they are part
    // of what "the mask matches the state" means.
    CombatState s = MakeCombat();
    AddHand(s, CardId::PURITY);
    AddHand(s, CardId::STRIKE);
    AddHand(s, CardId::DEFEND);

    const Action script[] = {
        make_action(ActionVerb::PLAY_CARD, 0),
        make_action(ActionVerb::CHOOSE, 1),
        make_action(ActionVerb::CHOOSE, 1),  // deselect it again
        make_action(ActionVerb::CHOOSE, 0),
        make_action(ActionVerb::CONFIRM),
    };
    for (const Action a : script) {
        ActionMask m{};
        legal_actions(s, m);
        StepResult r{};
        advance(std::span<CombatState>(&s, 1), std::span<const Action>(&a, 1),
                std::span<StepResult>(&r, 1), std::span<const ActionMask>(&m, 1));
    }
    EXPECT_EQ(s.exhaust_count, 2) << "one picked card plus Purity";
    EXPECT_EQ(s.hand_count, 1);
}

TEST(CardColorlessUncommonsDirected, PurityThenForethoughtScript) {
    // One turn, public API only: Purity takes two of four, upgraded Forethought
    // then tucks the survivors under the draw pile with a free play banked.
    CombatState s = MakeCombat(/*energy=*/3, /*monster_hp=*/80);
    AddHand(s, CardId::PURITY);                     // 0
    AddHand(s, CardId::FORETHOUGHT, /*upgrade=*/1); // 1
    AddHand(s, CardId::STRIKE);                     // 2
    AddHand(s, CardId::DEFEND);                     // 3
    const CardPoolIndex bash = AddHand(s, CardId::BASH);  // 4

    ActionMask m{};
    legal_actions(s, m);
    ASSERT_TRUE(m.can_play[0]);
    ASSERT_FALSE(m.choice_pending);

    Step(s, make_action(ActionVerb::PLAY_CARD, 0));  // Purity
    legal_actions(s, m);
    ASSERT_TRUE(m.choice_optional);
    ASSERT_EQ(s.hand_count, 4);
    // Exhaust Strike and Defend, leaving Forethought+ and Bash.
    Step(s, make_action(ActionVerb::CHOOSE,
                        static_cast<uint8_t>(FindHandSlot(s, CardId::STRIKE))));
    Step(s, make_action(ActionVerb::CHOOSE,
                        static_cast<uint8_t>(FindHandSlot(s, CardId::DEFEND))));
    Step(s, make_action(ActionVerb::CONFIRM));
    EXPECT_EQ(s.hand_count, 2);
    EXPECT_EQ(s.exhaust_count, 3) << "Strike, Defend, then Purity";
    EXPECT_EQ(s.player_energy, 3) << "Purity costs 0";

    const int ft_slot = FindHandSlot(s, CardId::FORETHOUGHT);
    ASSERT_GE(ft_slot, 0);
    Step(s, make_action(ActionVerb::PLAY_CARD, static_cast<uint8_t>(ft_slot)));
    legal_actions(s, m);
    ASSERT_TRUE(m.choice_optional);
    ASSERT_EQ(s.hand_count, 1) << "only Bash is left to offer";
    Step(s, make_action(ActionVerb::CHOOSE, 0));
    Step(s, make_action(ActionVerb::CONFIRM));

    EXPECT_EQ(s.hand_count, 0);
    ASSERT_EQ(s.draw_count, 1);
    EXPECT_EQ(s.draw[0], bash);
    EXPECT_TRUE(has_card_flag(s.card_pool[bash].flags,
                              CardFlag::FREE_TO_PLAY_ONCE));

    legal_actions(s, m);
    EXPECT_TRUE(m.can_end_turn);
    EXPECT_FALSE(m.choice_pending);
}

}  // namespace
}  // namespace sts::engine
