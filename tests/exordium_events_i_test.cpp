#include <array>
#include <cstdint>
#include <span>

#include <gtest/gtest.h>

#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"
#include "sts/engine/state_hash.hpp"

namespace {

using namespace sts::engine;

constexpr int64_t kSeed = 81726354;

RunController event_controller(EventId id, int ascension = 0) {
    RunController rc = run_begin(kSeed, static_cast<uint8_t>(ascension));
    rc.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
    rc.room_type = static_cast<uint8_t>(RoomType::Event);
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

void give_relic(RunState& rs, RelicId id) {
    ASSERT_LT(rs.relic_count, kRelicCap);
    rs.relics[rs.relic_count].relic_id = static_cast<uint16_t>(id);
    rs.relics[rs.relic_count].counter = -1;
    ++rs.relic_count;
}

TEST(BigFish, AllThreeOptionsApplyTheirExactImmediateMutations) {
    {
        RunController rc = event_controller(EventId::BIG_FISH);
        rc.run.hp = 20;
        rc.run.max_hp = 75;
        choose(rc, 0);
        EXPECT_EQ(rc.run.hp, 45);
        EXPECT_EQ(rc.run.max_hp, 75);
    }
    {
        RunController rc = event_controller(EventId::BIG_FISH);
        rc.run.hp = 20;
        rc.run.max_hp = 75;
        choose(rc, 1);
        EXPECT_EQ(rc.run.hp, 25);
        EXPECT_EQ(rc.run.max_hp, 80);
    }
    {
        RunController rc = event_controller(EventId::BIG_FISH);
        const uint16_t deck_before = rc.run.master_deck_count;
        const uint8_t relics_before = rc.run.relic_count;
        choose(rc, 2);
        ASSERT_EQ(rc.run.master_deck_count, deck_before + 1);
        EXPECT_EQ(rc.run.master_deck[deck_before].card_id,
                  static_cast<uint16_t>(CardId::REGRET));
        EXPECT_EQ(rc.run.relic_count, relics_before + 1);
    }
}

TEST(Cleric, AscensionCostHealAndPurgeGridShareTheMasterDeckDoor) {
    RunController low = event_controller(EventId::THE_CLERIC, 14);
    low.run.gold = 74;
    EXPECT_TRUE(menu(low).enabled[1]);

    RunController high = event_controller(EventId::THE_CLERIC, 15);
    high.run.gold = 74;
    EXPECT_FALSE(menu(high).enabled[1]);
    high.run.gold = 100;
    set_deck(high.run, {{CardId::ASCENDERS_BANE, 0}, {CardId::STRIKE, 0}});
    choose(high, 1);
    EXPECT_EQ(high.run.gold, 25);
    EXPECT_EQ(high.event.grid_kind,
              static_cast<uint8_t>(EventGridKind::PURGE));

    RunActionMask mask{};
    legal_actions(high, mask);
    EXPECT_FALSE(mask.can_choose_master_deck[0]);
    EXPECT_TRUE(mask.can_choose_master_deck[1]);
    choose(high, 1);
    ASSERT_EQ(high.run.master_deck_count, 1);
    EXPECT_EQ(high.run.master_deck[0].card_id,
              static_cast<uint16_t>(CardId::ASCENDERS_BANE));
    EXPECT_EQ(high.event.grid_kind,
              static_cast<uint8_t>(EventGridKind::NONE));

    RunController healed = event_controller(EventId::THE_CLERIC);
    healed.run.gold = 35;
    healed.run.hp = 10;
    healed.run.max_hp = 75;
    choose(healed, 0);
    EXPECT_EQ(healed.run.gold, 0);
    EXPECT_EQ(healed.run.hp, 28);
}

TEST(Cleric, NoPurgeableCardConsumesNoGold) {
    RunController rc = event_controller(EventId::THE_CLERIC, 20);
    rc.run.gold = 100;
    set_deck(rc.run, {{CardId::ASCENDERS_BANE, 0}});
    choose(rc, 1);
    EXPECT_EQ(rc.run.gold, 100);
    EXPECT_EQ(rc.event.grid_kind,
              static_cast<uint8_t>(EventGridKind::NONE));
}

TEST(DeadAdventurer, EntryConsumesShuffleSeedThenEnemyRoll) {
    RunController rc = run_begin(kSeed, 20);
    rc.combat.misc_rng = from_seed(9981);
    RngStream expected = rc.combat.misc_rng;
    std::array<uint8_t, 3> rewards = {0, 1, 2};
    JdkRandom jr(random_long(expected));
    jdk_shuffle(std::span<uint8_t>(rewards), jr);
    const int enemy = random(expected, 0, 2);

    rc.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
    rc.event.event_id = static_cast<uint16_t>(EventId::DEAD_ADVENTURER);
    event_dialog_impl(rc.event.event_id)->on_enter(rc, rc.event);

    EXPECT_EQ(rc.combat.misc_rng.counter, expected.counter);
    EXPECT_EQ(rc.event.scratch1, enemy);
    for (unsigned i = 0; i < rewards.size(); ++i) {
        EXPECT_EQ((static_cast<uint32_t>(
                       static_cast<uint16_t>(rc.event.scratch0)) >>
                   (2u * i)) &
                      3u,
                  rewards[i]);
    }
}

TEST(DeadAdventurer, FailedSearchSeedsRewardsThenEntersEventCombat) {
    RunController rc = event_controller(EventId::DEAD_ADVENTURER, 15);
    // Put every remaining reward in a known order and force the encounter roll
    // below the 35 percent starting threshold.
    rc.event.scratch0 = static_cast<int16_t>((0u << 0u) | (2u << 2u) |
                                             (1u << 4u));
    rc.event.scratch1 = 1;  // Gremlin Nob
    for (int64_t seed = 1;; ++seed) {
        RngStream probe = from_seed(seed);
        if (random(probe, 0, 99) < 35) {
            rc.combat.misc_rng = from_seed(seed);
            break;
        }
    }
    choose(rc, 0);
    ASSERT_EQ(rc.rewards.count, 1);
    EXPECT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::GOLD));
    EXPECT_GE(rc.rewards.items[0].gold, 25);
    EXPECT_LE(rc.rewards.items[0].gold, 35);

    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::Event));
    EXPECT_EQ(rc.combat.monsters[0].monster_id,
              static_cast<uint16_t>(MonsterId::GREMLIN_NOB));
    ASSERT_EQ(rc.rewards.count, 2);
    EXPECT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::GOLD));
    EXPECT_EQ(rc.rewards.items[1].kind,
              static_cast<uint8_t>(RewardItemKind::RELIC));
}

TEST(DeadAdventurer, SuccessfulSearchPaysEachShuffledRewardImmediately) {
    int64_t success_seed = 1;
    for (;; ++success_seed) {
        RngStream probe = from_seed(success_seed);
        if (random(probe, 0, 99) >= 25) {
            break;
        }
    }
    for (int reward = 0; reward < 3; ++reward) {
        RunController rc = event_controller(EventId::DEAD_ADVENTURER, 0);
        rc.event.scratch0 = static_cast<int16_t>(reward);
        rc.combat.misc_rng = from_seed(success_seed);
        const int gold_before = rc.run.gold;
        const uint8_t relics_before = rc.run.relic_count;
        choose(rc, 0);
        EXPECT_EQ((static_cast<uint16_t>(rc.event.scratch0) >> 6u) & 3u, 1u);
        EXPECT_EQ(rc.run.gold, gold_before + (reward == 0 ? 30 : 0));
        EXPECT_EQ(rc.run.relic_count,
                  static_cast<uint8_t>(relics_before + (reward == 2 ? 1 : 0)));
    }
}

TEST(DeadAdventurer, GoldRewardHelperMergesAndRecomputesGoldenIdolBonus) {
    RunController rc = run_begin(kSeed, 20);
    give_relic(rc.run, RelicId::GOLDEN_IDOL);
    ASSERT_TRUE(add_event_combat_gold_reward(rc.run, rc.rewards, 25));
    ASSERT_TRUE(add_event_combat_gold_reward(rc.run, rc.rewards, 30));
    ASSERT_EQ(rc.rewards.count, 1);
    EXPECT_EQ(rc.rewards.items[0].gold, 55);
    EXPECT_EQ(rc.rewards.items[0].bonus_gold, 14);
}

TEST(GoldenIdol, AscensionPenaltiesAndDuplicateRelicMatchTheDialogTree) {
    {
        RunController rc = event_controller(EventId::GOLDEN_IDOL, 14);
        rc.run.hp = 75;
        rc.run.max_hp = 75;
        choose(rc, 0);
        EXPECT_TRUE(owns(rc.run, RelicId::GOLDEN_IDOL));
        choose(rc, 1);
        EXPECT_EQ(rc.run.hp, 57);
    }
    {
        RunController rc = event_controller(EventId::GOLDEN_IDOL, 15);
        rc.run.hp = 75;
        rc.run.max_hp = 75;
        choose(rc, 0);
        choose(rc, 1);
        EXPECT_EQ(rc.run.hp, 49);
    }
    {
        RunController rc = event_controller(EventId::GOLDEN_IDOL, 15);
        choose(rc, 0);
        choose(rc, 2);
        EXPECT_EQ(rc.run.max_hp, 68);
        EXPECT_LE(rc.run.hp, 68);
    }
    {
        RunController rc = event_controller(EventId::GOLDEN_IDOL);
        const uint16_t before = rc.run.master_deck_count;
        choose(rc, 0);
        choose(rc, 0);
        ASSERT_EQ(rc.run.master_deck_count, before + 1);
        EXPECT_EQ(rc.run.master_deck[before].card_id,
                  static_cast<uint16_t>(CardId::INJURY));
    }
    {
        RunController rc = event_controller(EventId::GOLDEN_IDOL);
        give_relic(rc.run, RelicId::GOLDEN_IDOL);
        choose(rc, 0);
        EXPECT_TRUE(owns(rc.run, RelicId::CIRCLET));
    }
}

TEST(GoldenWing, DamageThresholdReadsTheUpgradedBaseProgram) {
    RunController rc = event_controller(EventId::GOLDEN_WING);
    set_deck(rc.run, {{CardId::BASH, 0}, {CardId::BODY_SLAM, 0}});
    EXPECT_FALSE(menu(rc).enabled[1]);
    rc.run.master_deck[0].upgrade = 1;
    EXPECT_TRUE(menu(rc).enabled[1]);

    rc.run.gold = 0;
    const int32_t before = rc.combat.misc_rng.counter;
    choose(rc, 1);
    EXPECT_GE(rc.run.gold, 50);
    EXPECT_LE(rc.run.gold, 80);
    EXPECT_EQ(rc.combat.misc_rng.counter, before + 1);
}

TEST(GoldenWing, DamageThenPromptThenPurgeIsAThreeStepFlow) {
    RunController rc = event_controller(EventId::GOLDEN_WING);
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::ASCENDERS_BANE, 0}});
    rc.run.hp = 30;
    choose(rc, 0);
    EXPECT_EQ(rc.run.hp, 23);
    EXPECT_EQ(rc.event.grid_kind,
              static_cast<uint8_t>(EventGridKind::NONE));
    choose(rc, 0);
    EXPECT_EQ(rc.event.grid_kind,
              static_cast<uint8_t>(EventGridKind::PURGE));
    choose(rc, 0);
    ASSERT_EQ(rc.run.master_deck_count, 1);
    EXPECT_EQ(rc.run.master_deck[0].card_id,
              static_cast<uint16_t>(CardId::ASCENDERS_BANE));
}

TEST(WorldOfGoop, EntryRangeClampsAndBothChoicesApplyInJavaOrder) {
    {
        RunController rc = event_controller(EventId::WORLD_OF_GOOP, 14);
        EXPECT_GE(rc.event.scratch0, 20);
        EXPECT_LE(rc.event.scratch0, 50);
        rc.run.hp = 30;
        const int gold = rc.run.gold;
        choose(rc, 0);
        EXPECT_EQ(rc.run.hp, 19);
        EXPECT_EQ(rc.run.gold, gold + 75);
    }
    {
        RunController rc = run_begin(kSeed, 15);
        rc.run.gold = 10;
        rc.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
        rc.event.event_id = static_cast<uint16_t>(EventId::WORLD_OF_GOOP);
        event_dialog_impl(rc.event.event_id)->on_enter(rc, rc.event);
        EXPECT_EQ(rc.event.scratch0, 10);
        choose(rc, 1);
        EXPECT_EQ(rc.run.gold, 0);
    }
    {
        RunController rc = event_controller(EventId::WORLD_OF_GOOP, 15);
        EXPECT_GE(rc.event.scratch0, 35);
        EXPECT_LE(rc.event.scratch0, 75);
    }
}

TEST(EventDialogs, LeaveBranchesOnlyAdvanceToTheirProceedScreen) {
    const struct {
        EventId id;
        uint8_t leave_option;
    } cases[] = {
        {EventId::THE_CLERIC, 2},
        {EventId::DEAD_ADVENTURER, 1},
        {EventId::GOLDEN_IDOL, 1},
        {EventId::GOLDEN_WING, 2},
    };
    for (const auto& tc : cases) {
        RunController rc = event_controller(tc.id, 20);
        const uint64_t before = hash_state(rc.run);
        choose(rc, tc.leave_option);
        EXPECT_EQ(hash_state(rc.run), before);
        const EventDialogMenu proceed = menu(rc);
        ASSERT_EQ(proceed.count, 1);
        EXPECT_TRUE(proceed.enabled[0]);
    }
}

TEST(EventCombat, PreservesAdvancedFloorStreamsAndPreseededRewardOrder) {
    RunController rc = run_begin(kSeed, 20);
    rc.run.floor = 9;
    rc.combat.monster_hp_rng = from_seed(10);
    rc.combat.ai_rng = from_seed(11);
    rc.combat.shuffle_rng = from_seed(12);
    rc.combat.card_random_rng = from_seed(13);
    rc.combat.misc_rng = from_seed(14);
    (void)random(rc.combat.monster_hp_rng, 99);
    (void)random(rc.combat.ai_rng, 99);
    (void)random(rc.combat.shuffle_rng, 99);
    (void)random(rc.combat.card_random_rng, 99);
    (void)random(rc.combat.misc_rng, 99);
    const int32_t hp_before = rc.combat.monster_hp_rng.counter;
    const int32_t ai_before = rc.combat.ai_rng.counter;
    const int32_t shuffle_before = rc.combat.shuffle_rng.counter;
    const int32_t card_before = rc.combat.card_random_rng.counter;
    const int32_t misc_before = rc.combat.misc_rng.counter;
    ASSERT_TRUE(add_event_combat_gold_reward(rc.run, rc.rewards, 30));

    ASSERT_TRUE(enter_event_combat(rc, "Gremlin Nob"));
    EXPECT_EQ(rc.combat.monster_hp_rng.counter, hp_before + 1);
    EXPECT_EQ(rc.combat.ai_rng.counter, ai_before + 1);
    EXPECT_EQ(rc.combat.shuffle_rng.counter, shuffle_before + 1);
    EXPECT_EQ(rc.combat.card_random_rng.counter, card_before);
    EXPECT_EQ(rc.combat.misc_rng.counter, misc_before);
    ASSERT_EQ(rc.rewards.count, 1);
    EXPECT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::GOLD));

    give_relic(rc.run, RelicId::WHITE_BEAST_STATUE);
    give_relic(rc.run, RelicId::PRAYER_WHEEL);
    rc.combat.phase = static_cast<uint8_t>(CombatPhase::COMBAT_OVER);
    rc.combat.player_hp = rc.run.hp;
    choose(rc, 0);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    ASSERT_EQ(rc.rewards.count, 3);
    EXPECT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::GOLD));
    EXPECT_EQ(rc.rewards.items[1].kind,
              static_cast<uint8_t>(RewardItemKind::POTION));
    EXPECT_EQ(rc.rewards.items[2].kind,
              static_cast<uint8_t>(RewardItemKind::CARDS));

    const uint8_t cursor_before = rc.monster_cursor;
    rc.cur_x = 0;
    next_room_transition(rc, 0, /*to_boss=*/false);
    EXPECT_EQ(rc.monster_cursor, cursor_before);
}

TEST(EventCombat, LagavulinVariantStartsAwake) {
    RunController rc = run_begin(kSeed, 20);
    ASSERT_TRUE(enter_event_combat(
        rc, "Lagavulin", EventCombatVariant::LAGAVULIN_AWAKE));
    ASSERT_EQ(rc.combat.monster_count, 1);
    EXPECT_EQ(rc.combat.monsters[0].monster_id,
              static_cast<uint16_t>(MonsterId::LAGAVULIN));
    EXPECT_NE(rc.combat.monsters[0].intent, 0);
}

}  // namespace
