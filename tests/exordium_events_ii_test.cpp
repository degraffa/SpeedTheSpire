#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <utility>

#include <gtest/gtest.h>

#include "sts/engine/card_pools.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"
#include "sts/registry/event_table.hpp"

namespace {

using namespace sts::engine;

constexpr int64_t kSeed = 918273645;

RunController event_controller(EventId id, int ascension = 0) {
    RunController rc = run_begin(kSeed, static_cast<uint8_t>(ascension));
    rc.run.floor = 9;
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
    const Action action = make_action(ActionVerb::CHOOSE, option);
    StepResult result{};
    advance(std::span<RunController>(&rc, 1),
            std::span<const Action>(&action, 1),
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

void give_relic(RunState& rs, RelicId id, int16_t counter = -1) {
    ASSERT_LT(rs.relic_count, kRelicCap);
    rs.relics[rs.relic_count].relic_id = static_cast<uint16_t>(id);
    rs.relics[rs.relic_count].counter = counter;
    ++rs.relic_count;
}

int64_t seed_with_roll(int wanted) {
    for (int64_t seed = 1; seed < 100000; ++seed) {
        RngStream rng = from_seed(seed);
        if (random(rng, 0, 99) == wanted) {
            return seed;
        }
    }
    return 0;
}

TEST(LiarsGame, AgreePreservesBothPagesThenObtainsBeforeGoldDoors) {
    RunController rc = event_controller(EventId::LIARS_GAME);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    const int gold = rc.run.gold;
    choose(rc, 0);
    EXPECT_EQ(rc.event.screen, 1);
    EXPECT_EQ(rc.run.master_deck_count, 1);
    EXPECT_EQ(rc.run.gold, gold);

    choose(rc, 0);
    EXPECT_EQ(rc.event.screen, 2);
    ASSERT_EQ(rc.run.master_deck_count, 2);
    EXPECT_EQ(rc.run.master_deck[1].card_id,
              static_cast<uint16_t>(CardId::DOUBT));
    EXPECT_EQ(rc.run.gold, gold + 175);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));

    RunController doors = event_controller(EventId::LIARS_GAME, 15);
    set_deck(doors.run, {{CardId::STRIKE, 0}});
    give_relic(doors.run, RelicId::OMAMORI, 2);
    give_relic(doors.run, RelicId::ECTOPLASM);
    const int blocked_gold = doors.run.gold;
    choose(doors, 0);
    choose(doors, 0);
    EXPECT_EQ(doors.run.master_deck_count, 1);
    EXPECT_EQ(doors.run.relics[1].counter, 1);
    EXPECT_EQ(doors.run.gold, blocked_gold);

    RunController a15 = event_controller(EventId::LIARS_GAME, 15);
    const int a15_gold = a15.run.gold;
    choose(a15, 0);
    choose(a15, 0);
    EXPECT_EQ(a15.run.gold, a15_gold + 150);
}

TEST(LiarsGame, DisagreeIsAStateNeutralProceedPage) {
    RunController rc = event_controller(EventId::LIARS_GAME);
    const int gold = rc.run.gold;
    const uint16_t deck = rc.run.master_deck_count;
    choose(rc, 1);
    EXPECT_EQ(rc.event.screen, 3);
    EXPECT_EQ(rc.run.gold, gold);
    EXPECT_EQ(rc.run.master_deck_count, deck);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(LivingWall, ForgetGrowAndDisabledGrowUseArbitraryDeckGrid) {
    {
        RunController rc = event_controller(EventId::LIVING_WALL);
        set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::PARASITE, 0},
                          {CardId::DEFEND, 0}});
        choose(rc, 0);
        choose(rc, 1);
        ASSERT_EQ(rc.run.master_deck_count, 2);
        EXPECT_EQ(rc.run.master_deck[0].card_id,
                  static_cast<uint16_t>(CardId::STRIKE));
        EXPECT_EQ(rc.run.max_hp, 77);  // Parasite.onRemoveFromMasterDeck
        choose(rc, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
    {
        RunController rc = event_controller(EventId::LIVING_WALL);
        set_deck(rc.run, {{CardId::STRIKE, 1}, {CardId::DEFEND, 0}});
        choose(rc, 2);
        choose(rc, 1);
        EXPECT_EQ(rc.run.master_deck[1].upgrade, 1);
    }
    {
        RunController rc = event_controller(EventId::LIVING_WALL, 20);
        set_deck(rc.run, {{CardId::STRIKE, 1}, {CardId::DEFEND, 1}});
        const EventDialogMenu m = menu(rc);
        ASSERT_EQ(m.count, 3);
        EXPECT_FALSE(m.enabled[2]);
    }
    {
        RunController rc = event_controller(EventId::LIVING_WALL);
        set_deck(rc.run, {{CardId::ASCENDERS_BANE, 0}});
        choose(rc, 0);
        EXPECT_EQ(rc.event.grid_kind,
                  static_cast<uint8_t>(EventGridKind::NONE));
        choose(rc, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
}

// The event grid reaches AbstractDungeon.transformCard just as Neow's grid
// does, so the expectation is the SAME `transform_card` (card_pools.hpp) driven
// off the SAME stream the event uses -- miscRng, not neowRng. What this pins is
// the stream and the one-draw accounting; the LIST's order is pinned without a
// seed by CardPoolLibraryOrder.TransformCardListIsCommonsThenBothSrcPoolsBackwards
// and against the live game by LivingWallCapture.STS00051FloorTwoProducesHavoc.
TEST(LivingWall, ChangeRemovesFirstDrawsOnceAndObtainsFromSameColorPool) {
    RunController rc = event_controller(EventId::LIVING_WALL);
    set_deck(rc.run, {{CardId::ANGER, 0}, {CardId::DEFEND, 0}});
    give_relic(rc.run, RelicId::CERAMIC_FISH);
    const int gold = rc.run.gold;
    rc.combat.misc_rng = from_seed(4321);
    RngStream expected_rng = rc.combat.misc_rng;
    const CardId expected = transform_card(expected_rng, CardId::ANGER);

    choose(rc, 1);
    choose(rc, 0);
    ASSERT_EQ(rc.run.master_deck_count, 2);
    EXPECT_EQ(rc.run.master_deck[0].card_id,
              static_cast<uint16_t>(CardId::DEFEND));
    EXPECT_EQ(rc.run.master_deck[1].card_id,
              static_cast<uint16_t>(expected));
    EXPECT_EQ(rc.combat.misc_rng.counter, expected_rng.counter);
    EXPECT_EQ(rc.run.gold, gold + 9);  // Ceramic Fish onObtainCard
}

// Which of the three lists a source card reaches is its own COLOUR, and the
// generated pools still self-complete by it. There is no longer an
// `event_transform_color` table to ask: transformCard branches on the card
// itself (CURSE type, then colorless-pool membership, then the `default:` arm),
// so the branch is observable only through the list it produces.
TEST(LivingWall, TheTransformListFollowsTheSourceCardsOwnColour) {
    CardId list[kTransformCardListCap]{};

    // STRIKE is RED and BASIC: it takes the `default:` arm and is in none of
    // the three reward pools, so nothing is excluded and the list is the whole
    // RED concatenation.
    const int red = transform_card_list(CardId::STRIKE, list);
    EXPECT_EQ(red, kIroncladCommonPoolCount + kIroncladUncommonPoolCount +
                       kIroncladRarePoolCount);
    EXPECT_EQ(std::find(list, list + red, CardId::STRIKE), list + red);

    // WOUND is a COLORLESS status: also not in its pool, so also nothing
    // excluded -- but Blind, an ordinary colorless uncommon, is there.
    const int colorless = transform_card_list(CardId::BLIND, list);
    EXPECT_EQ(colorless, kColorlessPoolCount - 1);
    EXPECT_EQ(std::find(list, list + colorless, CardId::BLIND),
              list + colorless);

    // A CURSE reaches CardLibrary.getCurse, minus its own id.
    const int curse = transform_card_list(CardId::DOUBT, list);
    EXPECT_EQ(curse, kPoolableCurseCount - 1);
    EXPECT_EQ(std::find(list, list + curse, CardId::DOUBT), list + curse);
    EXPECT_NE(std::find(list, list + curse, CardId::SHAME), list + curse);
}

// =============================================================================
// Captured evidence -- the Living Wall transform the live game answered
// =============================================================================

// Run STS00051 of campaign `b45_rewards_oracle2_20260727T204809Z_claude01`,
// floor 2: a Living Wall took the Change option and transformed the grid's
// index 5, a Defend. The capture's own oracle block gives the whole vector, so
// nothing here is re-derived from the simulator:
//
//   * `miscRng` before the pick (record seq 21):
//     `{counter: 0, s0: 6415738780070550668, s1: 6791187303769805616}` -- the
//     fresh `Random(Settings.seed)` state, which is why `from_seed` reproduces
//     it and the ASSERTs below prove it rather than assuming it.
//   * `miscRng` after (seq 22): `counter: 1` -- exactly one draw.
//   * deck before: `... Defend_R, Defend_R, Defend_R, Defend_R, Bash,
//     Perfected Strike`; deck after: one Defend_R gone and **Havoc** appended.
//
// This is the vector that caught the bug: walking `cards.yaml` rows put Iron
// Wave at that index, and the whole-run `--replay` of STS00051 carried
// `master_deck[11].card_id: Havoc(8) -> Iron Wave(18)` for 19 consecutive
// records because of it.
TEST(LivingWallCapture, STS00051FloorTwoProducesHavoc) {
    // The capture's raw state, transcribed. It is also DERIVED, which is the
    // cross-check: miscRng is floor-scoped, reseeded as `Random(seed +
    // floorNum)` with the floor number AFTER its increment
    // (AbstractDungeon.java:1747-1751, stage-a trap 7), and floor_stream
    // reproduces the captured pair exactly on seed 1790050543926, floor 2.
    RngStream misc = floor_stream(1790050543926LL, 2);
    ASSERT_EQ(misc.s0, 6415738780070550668ULL);
    ASSERT_EQ(misc.s1, 6791187303769805616ULL);
    ASSERT_EQ(misc.counter, 0);

    EXPECT_EQ(transform_card(misc, CardId::DEFEND), CardId::HAVOC);
    EXPECT_EQ(misc.counter, 1);
}

TEST(Mushrooms, HealThenParasiteUsesTheNormalCurseObtainDoor) {
    RunController rc = event_controller(EventId::MUSHROOMS, 20);
    rc.run.hp = 20;
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    give_relic(rc.run, RelicId::OMAMORI, 1);
    choose(rc, 1);
    EXPECT_EQ(rc.run.hp, 38);
    EXPECT_EQ(rc.run.master_deck_count, 1);
    EXPECT_EQ(rc.run.relics[1].counter, 0);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(Mushrooms, PreseedsGoldAndRelicBeforeThreeFungiCombatAndRewardAppend) {
    RunController rc = event_controller(EventId::MUSHROOMS);
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::DEFEND, 0},
                      {CardId::BASH, 0}});
    give_relic(rc.run, RelicId::WHITE_BEAST_STATUE);
    rc.monster_cursor = 7;
    rc.combat.misc_rng = from_seed(8765);
    RngStream expected_rng = rc.combat.misc_rng;
    const int expected_gold = random(expected_rng, 20, 30);

    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::EVENT_DIALOG));
    choose(rc, 0);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(rc.combat.monster_count, 3);
    for (uint8_t i = 0; i < 3; ++i) {
        EXPECT_EQ(rc.combat.monsters[i].monster_id,
                  static_cast<uint16_t>(MonsterId::FUNGI_BEAST));
        rc.combat.monsters[i].hp = 0;
    }
    EXPECT_EQ(rc.monster_cursor, 7);
    EXPECT_EQ(rc.combat.misc_rng.counter, expected_rng.counter);
    ASSERT_EQ(rc.rewards.count, 2);
    EXPECT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::GOLD));
    EXPECT_EQ(rc.rewards.items[0].gold, expected_gold);
    EXPECT_EQ(rc.rewards.items[1].kind,
              static_cast<uint8_t>(RewardItemKind::RELIC));
    EXPECT_EQ(rc.rewards.items[1].id,
              static_cast<uint16_t>(RelicId::ODD_MUSHROOM));

    rc.combat.phase = static_cast<uint8_t>(CombatPhase::COMBAT_OVER);
    choose(rc, 0);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    ASSERT_EQ(rc.rewards.count, 4);
    EXPECT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::GOLD));
    EXPECT_EQ(rc.rewards.items[1].kind,
              static_cast<uint8_t>(RewardItemKind::RELIC));
    EXPECT_EQ(rc.rewards.items[2].kind,
              static_cast<uint8_t>(RewardItemKind::POTION));
    EXPECT_EQ(rc.rewards.items[3].kind,
              static_cast<uint8_t>(RewardItemKind::CARDS));
}

TEST(ScrapOoze, BoundaryRollPinsDisplayedTwentyFiveAsTwentySixOfHundred) {
    const int64_t fail_seed = seed_with_roll(73);
    const int64_t success_seed = seed_with_roll(74);
    ASSERT_NE(fail_seed, 0);
    ASSERT_NE(success_seed, 0);

    RunController fail = event_controller(EventId::SCRAP_OOZE);
    fail.run.hp = 30;
    fail.combat.misc_rng = from_seed(fail_seed);
    choose(fail, 0);
    EXPECT_EQ(fail.run.hp, 27);
    EXPECT_EQ(fail.event.scratch0, 35);
    EXPECT_EQ(fail.event.scratch1, 4);
    EXPECT_EQ(fail.run.relic_count, 1);  // Burning Blood only

    RunController success = event_controller(EventId::SCRAP_OOZE);
    success.run.hp = 30;
    success.combat.misc_rng = from_seed(success_seed);
    choose(success, 0);
    EXPECT_EQ(success.run.hp, 27);
    EXPECT_EQ(success.event.screen, 1);
    EXPECT_GT(success.run.relic_count, 1);
}

TEST(ScrapOoze, A15NormalDamagePrecedesRollAndLethalStillProcessesIt) {
    RunController rc = event_controller(EventId::SCRAP_OOZE, 15);
    rc.run.hp = 5;
    give_relic(rc.run, RelicId::TORII);
    rc.combat.misc_rng = from_seed(seed_with_roll(74));
    const int before = rc.combat.misc_rng.counter;
    const uint8_t relics_before = rc.run.relic_count;
    choose(rc, 0);
    EXPECT_EQ(rc.run.hp, 0);  // null owner: Torii does not reduce 5 to 1
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER));
    EXPECT_GE(rc.combat.misc_rng.counter, before + 1);
    EXPECT_GT(rc.run.relic_count, relics_before);

    RunController leave = event_controller(EventId::SCRAP_OOZE, 20);
    const int hp = leave.run.hp;
    choose(leave, 1);
    EXPECT_EQ(leave.run.hp, hp);
    choose(leave, 0);
    EXPECT_EQ(leave.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(ShiningLight, AcceptUsesPlayerNormalDamageAndJdkUpgradesFirstTwo) {
    RunController rc = event_controller(EventId::SHINING_LIGHT, 15);
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::DEFEND, 0},
                      {CardId::BASH, 0}, {CardId::DOUBT, 0}});
    give_relic(rc.run, RelicId::TORII);
    rc.combat.misc_rng = from_seed(2468);
    RngStream expected_rng = rc.combat.misc_rng;
    std::array<uint16_t, 3> expected{{0, 1, 2}};
    JdkRandom jdk(random_long(expected_rng));
    jdk_shuffle(std::span<uint16_t>(expected), jdk);

    choose(rc, 0);
    EXPECT_EQ(rc.run.hp, 45);  // A14 max 75; round(75 * .30) = 23
    EXPECT_EQ(rc.combat.misc_rng.counter, expected_rng.counter);
    for (uint16_t i = 0; i < 3; ++i) {
        const bool upgraded = i == expected[0] || i == expected[1];
        EXPECT_EQ(rc.run.master_deck[i].upgrade, upgraded ? 1 : 0);
    }
    EXPECT_EQ(rc.run.master_deck[3].upgrade, 0);
}

TEST(ShiningLight, ShuffleAndUpgradesStillRunAfterLethalDamage) {
    RunController rc = event_controller(EventId::SHINING_LIGHT);
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::DEFEND, 0}});
    rc.run.hp = 1;
    const int before = rc.combat.misc_rng.counter;
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER));
    EXPECT_EQ(rc.combat.misc_rng.counter, before + 1);
    EXPECT_EQ(rc.run.master_deck[0].upgrade, 1);
    EXPECT_EQ(rc.run.master_deck[1].upgrade, 1);

    RunController leave = event_controller(EventId::SHINING_LIGHT, 20);
    const int hp = leave.run.hp;
    choose(leave, 1);
    EXPECT_EQ(leave.run.hp, hp);
    choose(leave, 0);
    EXPECT_EQ(leave.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

}  // namespace
