// S2.32's five act-gated one-timer bodies: Knowing Skull, The Joust, N'loth,
// Designer, Duplicator (src/engine/events/city_one_timers.cpp). The act/draw
// gates themselves are S2.13's and stay pinned in act_event_lists_test.cpp /
// one_time_specials_test.cpp; this file audits the option trees, the payouts
// and the A15 branches against the decompiled Java.

#include <array>
#include <cstdint>
#include <span>

#include <gtest/gtest.h>

#include "sts/engine/card_pools.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/potions.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"
#include "sts/registry/event_table.hpp"

namespace sts::engine {
namespace {

constexpr int64_t kSeed = 90311224;

RunController event_controller(EventId id, int ascension = 0) {
    RunController rc = run_begin(kSeed, static_cast<uint8_t>(ascension));
    rc.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
    rc.room_type = static_cast<uint8_t>(RoomType::Event);
    rc.run.act = 2;
    rc.run.floor = 20;
    rc.event = EventDialogState{};
    rc.event.event_id = static_cast<uint16_t>(id);
    rc.rewards = RewardScreen{};
    rc.rewards.open_card_item = kNoOpenCardReward;
    const EventDialogImpl* impl = event_dialog_impl(rc.event.event_id);
    EXPECT_NE(impl, nullptr);
    impl->on_enter(rc, rc.event);
    return rc;
}

void choose(RunController& rc, uint8_t option) {
    const Action a = make_action(ActionVerb::CHOOSE, option);
    StepResult result{};
    advance(std::span<RunController>(&rc, 1),
            std::span<const Action>(&a, 1),
            std::span<StepResult>(&result, 1));
}

EventDialogMenu menu(const RunController& rc) {
    EventDialogMenu out{};
    const EventDialogImpl* impl = event_dialog_impl(rc.event.event_id);
    EXPECT_NE(impl, nullptr);
    impl->build_menu(rc, rc.event, out);
    return out;
}

void set_deck(RunState& rs,
              std::initializer_list<std::pair<CardId, uint8_t>> cards) {
    rs.master_deck_count = 0;
    for (const auto& [id, upgrade] : cards) {
        ASSERT_TRUE(add_card_to_master_deck(rs, id, upgrade));
    }
}

bool owns(const RunState& rs, RelicId id) {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (rs.relics[i].relic_id == static_cast<uint16_t>(id)) {
            return true;
        }
    }
    return false;
}

void give_relic(RunState& rs, RelicId id, int16_t counter = -1) {
    ASSERT_LT(rs.relic_count, kRelicCap);
    rs.relics[rs.relic_count].relic_id = static_cast<uint16_t>(id);
    rs.relics[rs.relic_count].counter = counter;
    ++rs.relic_count;
}

int deck_count_of(const RunState& rs, CardId id) {
    int n = 0;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        n += rs.master_deck[i].card_id == static_cast<uint16_t>(id);
    }
    return n;
}

// =============================================================================
// Knowing Skull
// =============================================================================

TEST(KnowingSkull, CostsStartAtSixRampIndependentlyAndDamageIsPaidFirst) {
    RunController rc = event_controller(EventId::KNOWING_SKULL);
    rc.run.hp = 60;
    rc.run.gold = 0;
    choose(rc, 0);  // INTRO -> ASK
    ASSERT_EQ(rc.event.screen, 1);
    const EventDialogMenu m = menu(rc);
    ASSERT_EQ(m.count, 4);  // potion / gold / card / leave, never greyed out
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(m.enabled[i]);
    }

    choose(rc, 1);  // GOLD: pay 6, +90, goldCost -> 7
    EXPECT_EQ(rc.run.hp, 54);
    EXPECT_EQ(rc.run.gold, 90);
    choose(rc, 1);  // GOLD again: pay SEVEN -- only the bought cost ramps
    EXPECT_EQ(rc.run.hp, 47);
    EXPECT_EQ(rc.run.gold, 180);
    EXPECT_EQ(rc.event.scratch0, 6);  // potionCost untouched
    EXPECT_EQ(rc.event.scratch1, 8);  // goldCost after two buys
    EXPECT_EQ(rc.event.scratch2, 6);  // cardCost untouched

    choose(rc, 3);  // LEAVE pays the flat 6 (never ramped)
    EXPECT_EQ(rc.run.hp, 41);
    ASSERT_EQ(rc.event.screen, 2);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(KnowingSkull, PotionGoesToTheBeltAndSozuSkipsTheDrawEntirely) {
    RunController rc = event_controller(EventId::KNOWING_SKULL);
    rc.run.hp = 60;
    choose(rc, 0);
    const int32_t potion_counter = rc.run.potion_rng.counter;
    choose(rc, 0);  // A POTION
    EXPECT_EQ(rc.run.hp, 54);
    EXPECT_EQ(rc.run.potion_rng.counter, potion_counter + 1);
    EXPECT_NE(rc.run.potions[0], static_cast<uint16_t>(PotionId::NONE));

    RunController sozu = event_controller(EventId::KNOWING_SKULL);
    sozu.run.hp = 60;
    give_relic(sozu.run, RelicId::SOZU);
    choose(sozu, 0);
    const int32_t sozu_counter = sozu.run.potion_rng.counter;
    choose(sozu, 0);
    // Sozu short-circuits BEFORE getRandomPotion (KnowingSkull.java:132-135):
    // the damage and the ramp still land, the stream does not move.
    EXPECT_EQ(sozu.run.hp, 54);
    EXPECT_EQ(sozu.event.scratch0, 7);
    EXPECT_EQ(sozu.run.potion_rng.counter, sozu_counter);
    EXPECT_EQ(sozu.run.potions[0], static_cast<uint16_t>(PotionId::NONE));
}

TEST(KnowingSkull, AFullBeltLosesThePotionButSpendsTheDraw) {
    RunController rc = event_controller(EventId::KNOWING_SKULL);
    rc.run.hp = 60;
    const uint8_t slots = rc.run.potion_slots;
    for (uint8_t i = 0; i < slots && i < kPotionCap; ++i) {
        rc.run.potions[i] = static_cast<uint16_t>(PotionId::BLOCK_POTION);
    }
    choose(rc, 0);
    const int32_t counter = rc.run.potion_rng.counter;
    choose(rc, 0);
    EXPECT_EQ(rc.run.potion_rng.counter, counter + 1);
    for (uint8_t i = 0; i < slots && i < kPotionCap; ++i) {
        EXPECT_EQ(rc.run.potions[i],
                  static_cast<uint16_t>(PotionId::BLOCK_POTION));
    }
}

TEST(KnowingSkull, SecondCardBuyReadsTheFirstBuysPoolPermutation) {
    RunController rc = event_controller(EventId::KNOWING_SKULL);
    rc.run.hp = 60;
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    choose(rc, 0);

    // Derive both buys by replaying returnColorlessCard's IN-PLACE shuffle on
    // a model of the live pool: library order first, then the first shuffle's
    // permutation feeding the second (AbstractDungeon.java:1100-1113).
    std::array<uint16_t, static_cast<std::size_t>(kColorlessPoolCount)> pool{};
    for (int i = 0; i < kColorlessPoolCount; ++i) {
        pool[static_cast<std::size_t>(i)] = static_cast<uint16_t>(
            kColorlessPool[static_cast<std::size_t>(
                kColorlessPoolCount - 1 - i)]);
    }
    RngStream shuffle = rc.combat.shuffle_rng;
    std::array<CardId, 2> expected{};
    for (int buy = 0; buy < 2; ++buy) {
        JdkRandom jdk(random_long(shuffle));
        jdk_shuffle(std::span<uint16_t>(pool.data(), pool.size()), jdk);
        for (uint16_t id : pool) {
            if (sts::registry::event_card_rarity(static_cast<CardId>(id)) ==
                sts::registry::EventCardRarity::UNCOMMON) {
                expected[static_cast<std::size_t>(buy)] =
                    static_cast<CardId>(id);
                break;
            }
        }
    }

    choose(rc, 2);
    choose(rc, 2);
    ASSERT_EQ(rc.run.master_deck_count, 3);
    EXPECT_EQ(rc.run.master_deck[1].card_id,
              static_cast<uint16_t>(expected[0]));
    EXPECT_EQ(rc.run.master_deck[2].card_id,
              static_cast<uint16_t>(expected[1]));
}

TEST(KnowingSkull, ALethalPurchaseStillGrantsAndEndsTheRun) {
    RunController rc = event_controller(EventId::KNOWING_SKULL);
    rc.run.hp = 5;
    rc.run.gold = 0;
    choose(rc, 0);
    choose(rc, 1);  // GOLD for 6 damage at 5 HP: dead, but the 90 landed
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER));
    EXPECT_EQ(rc.run.gold, 90);
}

// =============================================================================
// The Joust
// =============================================================================

TEST(TheJoust, BetsPayFiftyUpFrontAndTheRollHappensOneScreenLater) {
    RunController rc = event_controller(EventId::THE_JOUST);
    rc.run.gold = 80;
    choose(rc, 0);  // HALT -> EXPLANATION
    ASSERT_EQ(menu(rc).count, 2);
    const int32_t misc_before = rc.combat.misc_rng.counter;
    choose(rc, 1);  // bet ON the owner
    EXPECT_EQ(rc.run.gold, 30);
    EXPECT_EQ(rc.combat.misc_rng.counter, misc_before)
        << "no roll at bet time (TheJoust.java:84-96)";
    RngStream probe = rc.combat.misc_rng;
    const bool owner_wins = random_boolean(probe, 0.3f);
    choose(rc, 0);  // PRE_JOUST continue: THE roll (:101)
    EXPECT_EQ(rc.combat.misc_rng.counter, misc_before + 1);
    choose(rc, 0);  // JOUST continue: the payout
    EXPECT_EQ(rc.run.gold, owner_wins ? 280 : 30);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(TheJoust, AWonMurdererBetPaysOneHundred) {
    // Scan for a misc seed whose 0.3f boolean is FALSE (the murderer wins).
    int64_t seed = 0;
    for (int64_t s = 1; s < 64; ++s) {
        RngStream probe = from_seed(s);
        if (!random_boolean(probe, 0.3f)) {
            seed = s;
            break;
        }
    }
    ASSERT_NE(seed, 0);
    RunController rc = event_controller(EventId::THE_JOUST);
    rc.run.gold = 50;
    rc.combat.misc_rng = from_seed(seed);
    choose(rc, 0);
    choose(rc, 0);  // bet on the murderer
    EXPECT_EQ(rc.run.gold, 0);
    choose(rc, 0);  // roll: murderer wins
    choose(rc, 0);  // payout
    EXPECT_EQ(rc.run.gold, 100);
}

// =============================================================================
// N'loth
// =============================================================================

TEST(Nloth, OffersTwoShuffledRelicsAndTradingLosesTheChosenOne) {
    RunController rc = event_controller(EventId::NLOTH);
    give_relic(rc.run, RelicId::VAJRA);
    give_relic(rc.run, RelicId::ANCHOR);

    // Re-derive the ctor's Collections.shuffle over the relic list
    // (Nloth.java:36-40) -- the on_enter already ran inside event_controller,
    // so replay it against the same pre-draw stream by rebuilding the state.
    RunController fresh = run_begin(kSeed, 0);
    fresh.run.act = 2;
    fresh.run.floor = 20;
    give_relic(fresh.run, RelicId::VAJRA);
    give_relic(fresh.run, RelicId::ANCHOR);
    std::array<uint16_t, kRelicCap> ids{};
    for (uint8_t i = 0; i < fresh.run.relic_count; ++i) {
        ids[i] = fresh.run.relics[i].relic_id;
    }
    RngStream misc = fresh.combat.misc_rng;
    JdkRandom jr(random_long(misc));
    jdk_shuffle(std::span<uint16_t>(ids.data(), fresh.run.relic_count), jr);

    fresh.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
    fresh.room_type = static_cast<uint8_t>(RoomType::Event);
    fresh.event = EventDialogState{};
    fresh.event.event_id = static_cast<uint16_t>(EventId::NLOTH);
    fresh.rewards = RewardScreen{};
    fresh.rewards.open_card_item = kNoOpenCardReward;
    event_dialog_impl(fresh.event.event_id)->on_enter(fresh, fresh.event);
    EXPECT_EQ(static_cast<uint16_t>(fresh.event.scratch0), ids[0]);
    EXPECT_EQ(static_cast<uint16_t>(fresh.event.scratch1), ids[1]);

    ASSERT_EQ(menu(fresh).count, 3);
    const RelicId chosen = static_cast<RelicId>(fresh.event.scratch0);
    choose(fresh, 0);
    EXPECT_FALSE(owns(fresh.run, chosen));
    EXPECT_TRUE(owns(fresh.run, RelicId::NLOTHS_GIFT));
    choose(fresh, 0);
    EXPECT_EQ(fresh.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(Nloth, LeaveKeepsEverything) {
    RunController rc = event_controller(EventId::NLOTH);
    give_relic(rc.run, RelicId::VAJRA);
    const uint8_t relics = rc.run.relic_count;
    choose(rc, 2);
    EXPECT_EQ(rc.run.relic_count, relics);
    EXPECT_FALSE(owns(rc.run, RelicId::NLOTHS_GIFT));
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(Nloth, AHeldGiftHandsACircletAndKeepsTheChosenRelic) {
    RunController rc = event_controller(EventId::NLOTH);
    give_relic(rc.run, RelicId::VAJRA);
    give_relic(rc.run, RelicId::NLOTHS_GIFT);
    const RelicId chosen = static_cast<RelicId>(rc.event.scratch0);
    choose(rc, 0);
    // The imported-state re-entry arm (Nloth.java:61-64): a Circlet, and the
    // chosen relic is NOT lost.
    EXPECT_TRUE(owns(rc.run, chosen));
    EXPECT_TRUE(owns(rc.run, RelicId::CIRCLET));
}

// =============================================================================
// Designer
// =============================================================================

// Find a misc seed whose two ctor booleans take the wanted values.
int64_t designer_seed(bool upgrades_one, bool removes_cards) {
    for (int64_t s = 1; s < 512; ++s) {
        RngStream probe = from_seed(s);
        if (random_boolean(probe) == upgrades_one &&
            random_boolean(probe) == removes_cards) {
            return s;
        }
    }
    return 0;
}

RunController designer_at_main(bool upgrades_one, bool removes_cards,
                               int ascension = 0) {
    RunController rc = event_controller(EventId::DESIGNER, ascension);
    const int64_t seed = designer_seed(upgrades_one, removes_cards);
    EXPECT_NE(seed, 0);
    rc.combat.misc_rng = from_seed(seed);
    event_dialog_impl(rc.event.event_id)->on_enter(rc, rc.event);
    EXPECT_EQ(rc.event.scratch0, upgrades_one ? 1 : 0);
    EXPECT_EQ(rc.event.scratch1, removes_cards ? 1 : 0);
    rc.run.gold = 500;
    choose(rc, 0);  // INTRO -> MAIN
    EXPECT_EQ(rc.event.screen, 1);
    return rc;
}

TEST(Designer, CtorDrawsTwoCoinsAndTheGatesReadGoldAndTheUnbottledDeck) {
    RunController rc = event_controller(EventId::DESIGNER);
    const int32_t misc = rc.combat.misc_rng.counter;
    event_dialog_impl(rc.event.event_id)->on_enter(rc, rc.event);
    EXPECT_EQ(rc.combat.misc_rng.counter, misc + 2);  // Designer.java:52-53

    RunController m = designer_at_main(true, true);
    set_deck(m.run, {{CardId::STRIKE, 0}});
    m.run.gold = 39;  // below every cost at A0 (40/60/90)
    const EventDialogMenu poor = menu(m);
    ASSERT_EQ(poor.count, 4);
    EXPECT_FALSE(poor.enabled[0]);
    EXPECT_FALSE(poor.enabled[1]);
    EXPECT_FALSE(poor.enabled[2]);
    EXPECT_TRUE(poor.enabled[3]);  // the punch is always on the table
}

TEST(Designer, AdjustmentsUpgradeOneOpensTheGridAndPaysForty) {
    RunController rc = designer_at_main(true, true);
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::BASH, 0}});
    choose(rc, 0);
    EXPECT_EQ(rc.run.gold, 460);
    ASSERT_EQ(rc.event.grid_kind, static_cast<uint8_t>(EventGridKind::UPGRADE));
    choose(rc, 1);  // upgrade the Bash
    EXPECT_EQ(rc.run.master_deck[1].upgrade, 1);
    EXPECT_EQ(rc.event.screen, 2);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(Designer, AdjustmentsUpgradeTwoShufflesOffOneRandomLong) {
    RunController rc = designer_at_main(false, true);
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::DEFEND, 0},
                      {CardId::BASH, 0}});
    // Derive which two cards the miscRng shuffle upgrades
    // (upgradeTwoRandomCards, Designer.java:230-258).
    std::array<uint16_t, 3> idx = {0, 1, 2};
    RngStream misc = rc.combat.misc_rng;
    JdkRandom jr(random_long(misc));
    jdk_shuffle(std::span<uint16_t>(idx.data(), idx.size()), jr);

    choose(rc, 0);
    EXPECT_EQ(rc.run.gold, 460);
    EXPECT_EQ(rc.event.screen, 2);
    int upgraded = 0;
    for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
        upgraded += rc.run.master_deck[i].upgrade;
    }
    EXPECT_EQ(upgraded, 2);
    EXPECT_EQ(rc.run.master_deck[idx[0]].upgrade, 1);
    EXPECT_EQ(rc.run.master_deck[idx[1]].upgrade, 1);
}

TEST(Designer, CleanUpRemoveOpensThePurgeGridAndPaysSixty) {
    RunController rc = designer_at_main(true, true);
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::BASH, 0}});
    choose(rc, 1);
    EXPECT_EQ(rc.run.gold, 440);
    ASSERT_EQ(rc.event.grid_kind, static_cast<uint8_t>(EventGridKind::PURGE));
    choose(rc, 0);
    EXPECT_EQ(rc.run.master_deck_count, 1);
    EXPECT_EQ(rc.run.master_deck[0].card_id,
              static_cast<uint16_t>(CardId::BASH));
    EXPECT_EQ(rc.event.screen, 2);
}

TEST(Designer, CleanUpTransformTakesTwoPicksInClickOrder) {
    RunController rc = designer_at_main(true, false);
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::DEFEND, 0},
                      {CardId::BASH, 0}});
    // Derive the two transform rolls: remove STRIKE (pick 0), roll; remove
    // BASH (pick 2), roll; append both (Designer.java:113-129).
    RngStream misc = rc.combat.misc_rng;
    const CardId t_a = transform_card(misc, CardId::STRIKE);
    const CardId t_b = transform_card(misc, CardId::BASH);

    choose(rc, 1);
    EXPECT_EQ(rc.run.gold, 440);
    ASSERT_EQ(rc.event.grid_kind,
              static_cast<uint8_t>(EventGridKind::TRANSFORMABLE));
    choose(rc, 0);  // first pick: the Strike
    ASSERT_EQ(rc.event.grid_kind,
              static_cast<uint8_t>(EventGridKind::TRANSFORM_PAIR_SECOND));
    // The first pick cannot be re-picked.
    RunActionMask mask{};
    legal_actions(rc, mask);
    EXPECT_FALSE(mask.can_choose_master_deck[0]);
    EXPECT_TRUE(mask.can_choose_master_deck[2]);
    choose(rc, 2);  // second pick: the Bash
    ASSERT_EQ(rc.run.master_deck_count, 3);
    EXPECT_EQ(rc.run.master_deck[0].card_id,
              static_cast<uint16_t>(CardId::DEFEND));
    EXPECT_EQ(rc.run.master_deck[1].card_id, static_cast<uint16_t>(t_a));
    EXPECT_EQ(rc.run.master_deck[2].card_id, static_cast<uint16_t>(t_b));
    EXPECT_EQ(rc.event.screen, 2);
}

TEST(Designer, FullServiceRemovesAPickThenUpgradesOneRandomCard) {
    RunController rc = designer_at_main(true, true);
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::DEFEND, 0},
                      {CardId::BASH, 0}});
    // After removing pick 0, the shuffle runs over the two survivors.
    RngStream misc = rc.combat.misc_rng;
    std::array<uint16_t, 2> idx = {0, 1};
    JdkRandom jr(random_long(misc));
    jdk_shuffle(std::span<uint16_t>(idx.data(), idx.size()), jr);

    choose(rc, 2);
    EXPECT_EQ(rc.run.gold, 410);
    ASSERT_EQ(rc.event.grid_kind, static_cast<uint8_t>(EventGridKind::PURGE));
    choose(rc, 0);  // remove the Strike
    ASSERT_EQ(rc.run.master_deck_count, 2);
    EXPECT_EQ(rc.run.master_deck[idx[0]].upgrade, 1);
    EXPECT_EQ(rc.run.master_deck[idx[1]].upgrade, 0);
    EXPECT_EQ(rc.event.screen, 2);
}

TEST(Designer, PunchCostsThreeOrFiveAtA15AndTheA15CostsApply) {
    RunController rc = designer_at_main(true, true);
    rc.run.hp = 40;
    choose(rc, 3);
    EXPECT_EQ(rc.run.hp, 37);
    EXPECT_EQ(rc.run.gold, 500);  // the punch is free

    RunController a15 = designer_at_main(true, true, 15);
    set_deck(a15.run, {{CardId::STRIKE, 0}});
    a15.run.hp = 40;
    // A15 costs 50/75/110 gate the buttons.
    a15.run.gold = 49;
    EXPECT_FALSE(menu(a15).enabled[0]);
    a15.run.gold = 500;
    choose(a15, 3);
    EXPECT_EQ(a15.run.hp, 35);
}

// =============================================================================
// Duplicator
// =============================================================================

TEST(Duplicator, CopiesAnyCardPreservingItsUpgradeCount) {
    RunController rc = event_controller(EventId::DUPLICATOR);
    set_deck(rc.run, {{CardId::SEARING_BLOW, 3}, {CardId::BASH, 0}});
    ASSERT_EQ(menu(rc).count, 2);
    choose(rc, 0);
    ASSERT_EQ(rc.event.grid_kind,
              static_cast<uint8_t>(EventGridKind::DUPLICATE));
    // The whole deck is legal -- no purgeable filter, no bottled exclusion.
    RunActionMask mask{};
    legal_actions(rc, mask);
    EXPECT_TRUE(mask.can_choose_master_deck[0]);
    EXPECT_TRUE(mask.can_choose_master_deck[1]);
    choose(rc, 0);
    ASSERT_EQ(rc.run.master_deck_count, 3);
    EXPECT_EQ(rc.run.master_deck[2].card_id,
              static_cast<uint16_t>(CardId::SEARING_BLOW));
    EXPECT_EQ(rc.run.master_deck[2].upgrade, 3);  // makeStatEquivalentCopy
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(Duplicator, EvenUnremovableCursesAreCopyableAndOmamoriEatsTheCopy) {
    RunController rc = event_controller(EventId::DUPLICATOR);
    set_deck(rc.run, {{CardId::ASCENDERS_BANE, 0}, {CardId::STRIKE, 0}});
    give_relic(rc.run, RelicId::OMAMORI, 1);
    choose(rc, 0);
    RunActionMask mask{};
    legal_actions(rc, mask);
    // gridSelectScreen.open(masterDeck, ...) has NO purgeable filter
    // (Duplicator.java:87): the Bane is a legal pick.
    EXPECT_TRUE(mask.can_choose_master_deck[0]);
    choose(rc, 0);
    // The copy is a CURSE obtain through the ordinary door: Omamori eats it.
    EXPECT_EQ(deck_count_of(rc.run, CardId::ASCENDERS_BANE), 1);
    for (uint8_t i = 0; i < rc.run.relic_count; ++i) {
        if (rc.run.relics[i].relic_id ==
            static_cast<uint16_t>(RelicId::OMAMORI)) {
            EXPECT_EQ(rc.run.relics[i].counter, 0);
        }
    }
    EXPECT_EQ(rc.event.screen, 1);
}

TEST(Duplicator, LeaveTouchesNothing) {
    RunController rc = event_controller(EventId::DUPLICATOR);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    choose(rc, 1);
    EXPECT_EQ(rc.run.master_deck_count, 1);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

}  // namespace
}  // namespace sts::engine
