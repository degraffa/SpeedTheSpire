// Rest-site tier-2 and directed flow tests.
//
// Every expectation is hand-derived from the cited Java rather than from a
// second engine helper: CampfireUI button insertion/order, the 30% float heal,
// Regal Pillow and Dream Catcher, the Smith/Toke grids, Girya's three-use
// counter, and Shovel's relicRng tier roll followed by a pool-front pop.

#include "sts/engine/run_advance.hpp"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <span>

#include <gtest/gtest.h>

#include "sts/engine/cards.hpp"
#include "sts/engine/map_rooms.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rest_sites.hpp"
#include "sts/engine/run_deck.hpp"
#include "sts/engine/state_hash.hpp"

namespace sts::engine {
namespace {

constexpr int64_t kSeed = 1790050543751LL;

void step(RunController& rc, Action action) {
    StepResult result{};
    advance(std::span<RunController>(&rc, 1),
            std::span<const Action>(&action, 1),
            std::span<StepResult>(&result, 1));
}

uint8_t first_start_column(const RunController& rc) {
    for (uint8_t x = 0; x < kMapCols; ++x) {
        if (rc.run.map[run_state_map_index(x, 0)].edges != 0) {
            return x;
        }
    }
    return 0xFF;
}

RunController enter_floor_one_rest(uint8_t ascension = 0) {
    RunController rc = run_begin(kSeed, ascension);
    step(rc, make_action(ActionVerb::CHOOSE));  // Neow -> map
    const uint8_t x = first_start_column(rc);
    EXPECT_NE(x, 0xFF);
    rc.run.map[run_state_map_index(x, 0)].room_type =
        static_cast<uint8_t>(RoomType::Rest);
    step(rc, make_action(ActionVerb::CHOOSE, x));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::REST_SITE));
    EXPECT_EQ(rc.rest.screen, static_cast<uint8_t>(RestScreen::MENU));
    EXPECT_EQ(rc.run.floor, 1);
    EXPECT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::Rest));
    return rc;
}

void set_relics(RunState& rs, std::initializer_list<RelicSlot> relics) {
    rs.relic_count = 0;
    for (const RelicSlot relic : relics) {
        rs.relics[rs.relic_count++] = relic;
    }
}

uint8_t option_index(const RunState& rs, RestOptionKind kind) {
    const RestMenu menu = build_rest_menu(rs);
    for (uint8_t i = 0; i < menu.count; ++i) {
        if (menu.entries[i].kind == static_cast<uint8_t>(kind)) {
            return i;
        }
    }
    return 0xFF;
}

// RestOption/CampfireSleepEffect use `(int)(maxHealth * 0.3f)`: float
// multiplication and truncation, with no ascension branch.  The table includes
// values where 30% is non-integral and the two Ironclad max-HP tiers.
TEST(RestSites, HealAmountIsJavaFloatFloorWithNoAscensionScaling) {
    struct Case {
        int16_t max_hp;
        int expected;
    };
    for (const Case c : {Case{1, 0}, Case{75, 22}, Case{79, 23},
                         Case{80, 24}, Case{99, 29}}) {
        RunState rs{};
        rs.max_hp = c.max_hp;
        EXPECT_EQ(rest_heal_amount(rs), c.expected) << c.max_hp;
    }

    RunController a0 = enter_floor_one_rest(0);
    RunController a20 = enter_floor_one_rest(20);
    a0.run.max_hp = 75;
    a20.run.max_hp = 75;
    a0.run.hp = a20.run.hp = 10;
    const uint8_t rest0 = option_index(a0.run, RestOptionKind::REST);
    const uint8_t rest20 = option_index(a20.run, RestOptionKind::REST);
    step(a0, make_action(ActionVerb::CHOOSE, rest0));
    step(a20, make_action(ActionVerb::CHOOSE, rest20));
    EXPECT_EQ(a0.run.hp, 32);
    EXPECT_EQ(a20.run.hp, 32);
    EXPECT_EQ(a0.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(a20.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(RestSites, RestHealSaturatesAtMaxHpAndConsumesNoRng) {
    RunController rc = enter_floor_one_rest();
    rc.run.hp = static_cast<int16_t>(rc.run.max_hp - 2);
    const RunState before = rc.run;
    const int32_t relic_counter = rc.run.relic_rng.counter;
    const int32_t card_counter = rc.run.card_rng.counter;
    const int32_t misc_counter = rc.combat.misc_rng.counter;

    step(rc, make_action(ActionVerb::CHOOSE,
                         option_index(rc.run, RestOptionKind::REST)));

    EXPECT_EQ(rc.run.hp, rc.run.max_hp);
    EXPECT_EQ(rc.run.relic_rng.counter, relic_counter);
    EXPECT_EQ(rc.run.card_rng.counter, card_counter);
    EXPECT_EQ(rc.combat.misc_rng.counter, misc_counter);
    EXPECT_EQ(rc.run.max_hp, before.max_hp);
}

// CampfireSleepEffect computes the base heal first, adds Regal Pillow's 15,
// heals, and only then checks Dream Catcher (lines 48-76).  Pillow has no RNG
// hook and does not change the frozen no-ascension rule.
TEST(RestSites, RegalPillowAddsFifteenToTheRestHeal) {
    RunController rc = enter_floor_one_rest(20);
    set_relics(rc.run,
               {RelicSlot{static_cast<uint16_t>(RelicId::REGAL_PILLOW), -1}});
    rc.run.max_hp = 75;
    rc.run.hp = 10;
    const int32_t card_counter = rc.run.card_rng.counter;

    EXPECT_EQ(rest_heal_amount(rc.run), 37);  // floor(75 * 0.3f) + 15
    step(rc, make_action(ActionVerb::CHOOSE,
                         option_index(rc.run, RestOptionKind::REST)));

    EXPECT_EQ(rc.run.hp, 47);
    EXPECT_EQ(rc.run.card_rng.counter, card_counter);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

// Dream Catcher calls getRewardCards after the heal and opens CardRewardScreen
// directly: there is no outer reward item to claim and no extra Proceed click.
TEST(RestSites, DreamCatcherOpensDirectCardPickAfterHealing) {
    RunController rc = enter_floor_one_rest();
    set_relics(rc.run,
               {RelicSlot{static_cast<uint16_t>(RelicId::DREAM_CATCHER), -1}});
    rc.run.hp = 10;
    const uint16_t deck_before = rc.run.master_deck_count;
    const int32_t card_counter_before = rc.run.card_rng.counter;

    step(rc, make_action(ActionVerb::CHOOSE,
                         option_index(rc.run, RestOptionKind::REST)));

    EXPECT_EQ(rc.run.hp, 34);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::REST_SITE));
    EXPECT_EQ(rc.rest.screen,
              static_cast<uint8_t>(RestScreen::DREAM_CATCHER));
    ASSERT_EQ(rc.rewards.count, 1);
    EXPECT_EQ(rc.rewards.open_card_item, 0);
    EXPECT_EQ(static_cast<RewardItemKind>(rc.rewards.items[0].kind),
              RewardItemKind::CARDS);
    EXPECT_EQ(rc.rewards.items[0].card_count, 3);
    EXPECT_EQ(static_cast<CardId>(rc.rewards.items[0].card_ids[0]),
              CardId::BLOOD_FOR_BLOOD);
    EXPECT_EQ(static_cast<CardId>(rc.rewards.items[0].card_ids[1]),
              CardId::CARNAGE);
    EXPECT_EQ(static_cast<CardId>(rc.rewards.items[0].card_ids[2]),
              CardId::PERFECTED_STRIKE);
    EXPECT_EQ(rc.run.card_rng.counter, 9);
    EXPECT_EQ(rc.run.card_blizz_randomizer, 4);
    EXPECT_GT(rc.run.card_rng.counter, card_counter_before);

    RunActionMask mask{};
    legal_actions(rc, mask);
    EXPECT_TRUE(mask.can_take_card[0]);
    EXPECT_TRUE(mask.can_take_card[1]);
    EXPECT_TRUE(mask.can_take_card[2]);
    EXPECT_TRUE(mask.can_skip_card);
    EXPECT_FALSE(mask.can_proceed);

    const CardId picked =
        static_cast<CardId>(rc.rewards.items[0].card_ids[1]);
    step(rc, make_action(ActionVerb::CHOOSE, 1));
    ASSERT_EQ(rc.run.master_deck_count, deck_before + 1);
    EXPECT_EQ(static_cast<CardId>(
                  rc.run.master_deck[rc.run.master_deck_count - 1].card_id),
              picked);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(rc.rewards.count, 0);
    EXPECT_EQ(rc.rewards.open_card_item, kNoOpenCardReward);
}

TEST(RestSites, DreamCatcherSkipFinishesTheRestSiteWithoutAddingACard) {
    RunController rc = enter_floor_one_rest();
    set_relics(rc.run,
               {RelicSlot{static_cast<uint16_t>(RelicId::DREAM_CATCHER), -1}});
    const uint16_t deck_before = rc.run.master_deck_count;
    step(rc, make_action(ActionVerb::CHOOSE,
                         option_index(rc.run, RestOptionKind::REST)));
    ASSERT_EQ(rc.rest.screen,
              static_cast<uint8_t>(RestScreen::DREAM_CATCHER));

    step(rc, make_action(ActionVerb::CHOOSE, kChooseSkipCard));

    EXPECT_EQ(rc.run.master_deck_count, deck_before);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(rc.rewards.count, 0);
}

TEST(RestSites, FullMasterDeckDreamCatcherOffersOnlySkipOrSingingBowl) {
    RunController skip = enter_floor_one_rest();
    set_relics(
        skip.run,
        {RelicSlot{static_cast<uint16_t>(RelicId::DREAM_CATCHER), -1}});
    for (uint16_t i = skip.run.master_deck_count; i < kMasterDeckCap; ++i) {
        skip.run.master_deck[i] =
            CardInstance{static_cast<uint16_t>(CardId::STRIKE), 0, 0, 0, 0};
    }
    skip.run.master_deck_count = kMasterDeckCap;
    step(skip, make_action(ActionVerb::CHOOSE,
                           option_index(skip.run, RestOptionKind::REST)));
    ASSERT_EQ(skip.rest.screen,
              static_cast<uint8_t>(RestScreen::DREAM_CATCHER));

    RunActionMask mask{};
    legal_actions(skip, mask);
    for (uint8_t i = 0; i < skip.rewards.items[0].card_count; ++i) {
        EXPECT_FALSE(mask.can_take_card[i]);
    }
    EXPECT_TRUE(mask.can_skip_card);
    EXPECT_FALSE(mask.can_sing);
    const RunController before = skip;
    step(skip, make_action(ActionVerb::CHOOSE, 0));
    EXPECT_EQ(std::memcmp(&skip, &before, sizeof(skip)), 0);
    step(skip, make_action(ActionVerb::CHOOSE, kChooseSkipCard));
    EXPECT_EQ(skip.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(skip.run.master_deck_count, kMasterDeckCap);

    RunController sing = enter_floor_one_rest();
    set_relics(
        sing.run,
        {RelicSlot{static_cast<uint16_t>(RelicId::DREAM_CATCHER), -1},
         RelicSlot{static_cast<uint16_t>(RelicId::SINGING_BOWL), -1}});
    for (uint16_t i = sing.run.master_deck_count; i < kMasterDeckCap; ++i) {
        sing.run.master_deck[i] =
            CardInstance{static_cast<uint16_t>(CardId::STRIKE), 0, 0, 0, 0};
    }
    sing.run.master_deck_count = kMasterDeckCap;
    sing.run.hp = 10;
    step(sing, make_action(ActionVerb::CHOOSE,
                           option_index(sing.run, RestOptionKind::REST)));
    legal_actions(sing, mask);
    EXPECT_TRUE(mask.can_skip_card);
    EXPECT_TRUE(mask.can_sing);
    const int16_t max_before_bowl = sing.run.max_hp;
    const int16_t hp_after_rest = sing.run.hp;
    step(sing, make_action(ActionVerb::CHOOSE, kChooseSing));
    EXPECT_EQ(sing.run.max_hp, max_before_bowl + 2);
    EXPECT_EQ(sing.run.hp, hp_after_rest + 2);
    EXPECT_EQ(sing.run.master_deck_count, kMasterDeckCap);
    EXPECT_EQ(sing.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(RestSites, MalformedDreamCatcherRewardIsInertAndAdvertisesNothing) {
    RunController rc = enter_floor_one_rest();
    set_relics(
        rc.run,
        {RelicSlot{static_cast<uint16_t>(RelicId::SINGING_BOWL), -1}});
    rc.rest.screen = static_cast<uint8_t>(RestScreen::DREAM_CATCHER);
    rc.rewards = RewardScreen{};
    rc.rewards.count = static_cast<uint8_t>(kRewardItemCap + 1);
    rc.rewards.open_card_item = static_cast<uint8_t>(kRewardItemCap);

    RunActionMask mask{};
    legal_actions(rc, mask);
    EXPECT_FALSE(mask.can_skip_card);
    EXPECT_FALSE(mask.can_sing);
    for (bool can_take : mask.can_take_card) {
        EXPECT_FALSE(can_take);
    }

    const Action forced[] = {
        make_action(ActionVerb::CHOOSE, 0),
        make_action(ActionVerb::CHOOSE, kChooseSkipCard),
        make_action(ActionVerb::CHOOSE, kChooseSing)};
    for (const Action action : forced) {
        RunController attempted = rc;
        step(attempted, action);
        EXPECT_EQ(std::memcmp(&attempted, &rc, sizeof(rc)), 0)
            << "malformed Dream Catcher state accepted forced CHOOSE "
            << static_cast<int>(action_arg0(action));
    }
}

// CampfireUI inserts Rest, Smith, then calls addCampfireOption on each relic in
// acquisition order.  Unusable buttons remain in the menu but are absent from
// legal actions.
TEST(RestSites, MenuOrderAndAvailabilityMatrixMatchCampfireUi) {
    RunController rc = enter_floor_one_rest();
    set_relics(rc.run,
               {RelicSlot{static_cast<uint16_t>(RelicId::SHOVEL), -1},
                RelicSlot{static_cast<uint16_t>(RelicId::GIRYA), 3},
                RelicSlot{static_cast<uint16_t>(RelicId::PEACE_PIPE), -1}});
    rc.run.master_deck_count = 1;
    rc.run.master_deck[0] =
        CardInstance{static_cast<uint16_t>(CardId::ASCENDERS_BANE), 0, 0, 0, 0};

    RestMenu menu = build_rest_menu(rc.run);
    ASSERT_EQ(menu.count, 5);
    EXPECT_EQ(static_cast<RestOptionKind>(menu.entries[0].kind),
              RestOptionKind::REST);
    EXPECT_EQ(static_cast<RestOptionKind>(menu.entries[1].kind),
              RestOptionKind::SMITH);
    EXPECT_EQ(static_cast<RestOptionKind>(menu.entries[2].kind),
              RestOptionKind::DIG);
    EXPECT_EQ(static_cast<RestOptionKind>(menu.entries[3].kind),
              RestOptionKind::LIFT);
    EXPECT_EQ(static_cast<RestOptionKind>(menu.entries[4].kind),
              RestOptionKind::TOKE);
    EXPECT_TRUE(menu.entries[0].usable);
    EXPECT_FALSE(menu.entries[1].usable);  // no upgradable cards
    EXPECT_TRUE(menu.entries[2].usable);
    EXPECT_FALSE(menu.entries[3].usable);  // Girya counter reached 3
    EXPECT_FALSE(menu.entries[4].usable);  // only unpurgeable Ascender's Bane

    RunActionMask mask{};
    legal_actions(rc, mask);
    EXPECT_TRUE(mask.can_choose_rest[0]);
    EXPECT_FALSE(mask.can_choose_rest[1]);
    EXPECT_TRUE(mask.can_choose_rest[2]);
    EXPECT_FALSE(mask.can_choose_rest[3]);
    EXPECT_FALSE(mask.can_choose_rest[4]);

    // One ordinary base card opens Smith and Toke, and lowering Girya opens
    // Lift; the menu shape/order does not move.
    rc.run.master_deck[0] =
        CardInstance{static_cast<uint16_t>(CardId::STRIKE), 0, 0, 0, 0};
    rc.run.relics[1].counter = 2;
    menu = build_rest_menu(rc.run);
    ASSERT_EQ(menu.count, 5);
    EXPECT_TRUE(menu.entries[1].usable);
    EXPECT_TRUE(menu.entries[3].usable);
    EXPECT_TRUE(menu.entries[4].usable);
}

TEST(RestSites, NoUpgradableCardsMeansNoLegalSmith) {
    RunController rc = enter_floor_one_rest();
    rc.run.master_deck_count = 3;
    rc.run.master_deck[0] =
        CardInstance{static_cast<uint16_t>(CardId::STRIKE), 1, 0, 0, 0};
    rc.run.master_deck[1] =
        CardInstance{static_cast<uint16_t>(CardId::WOUND), 0, 0, 0, 0};
    rc.run.master_deck[2] =
        CardInstance{static_cast<uint16_t>(CardId::ASCENDERS_BANE), 0, 0, 0, 0};

    const RestMenu menu = build_rest_menu(rc.run);
    ASSERT_GE(menu.count, 2);
    EXPECT_FALSE(menu.entries[1].usable);
    RunActionMask mask{};
    legal_actions(rc, mask);
    EXPECT_FALSE(mask.can_choose_rest[1]);

    const RunController before = rc;
    step(rc, make_action(ActionVerb::CHOOSE, 1));
    EXPECT_EQ(std::memcmp(&rc, &before, sizeof(rc)), 0)
        << "choosing a disabled Smith button is a non-corrupting no-op";
}

TEST(RestSites, SmithGridWritesExistingUpgradeCountByMasterDeckIndex) {
    RunController rc = enter_floor_one_rest();
    rc.run.master_deck_count = 4;
    rc.run.master_deck[0] =
        CardInstance{static_cast<uint16_t>(CardId::STRIKE), 0, 0, 0, 0};
    rc.run.master_deck[1] =
        CardInstance{static_cast<uint16_t>(CardId::DEFEND), 1, 0, 0, 0};
    rc.run.master_deck[2] =
        CardInstance{static_cast<uint16_t>(CardId::SEARING_BLOW), 4, 0, 0, 0};
    rc.run.master_deck[3] =
        CardInstance{static_cast<uint16_t>(CardId::WOUND), 0, 0, 0, 0};

    step(rc, make_action(ActionVerb::CHOOSE,
                         option_index(rc.run, RestOptionKind::SMITH)));
    ASSERT_EQ(rc.rest.screen, static_cast<uint8_t>(RestScreen::SMITH));
    RunActionMask mask{};
    legal_actions(rc, mask);
    EXPECT_TRUE(mask.can_choose_master_deck[0]);
    EXPECT_FALSE(mask.can_choose_master_deck[1]);
    EXPECT_TRUE(mask.can_choose_master_deck[2]);
    EXPECT_FALSE(mask.can_choose_master_deck[3]);

    step(rc, make_action(ActionVerb::CHOOSE, 2));
    EXPECT_EQ(rc.run.master_deck[2].upgrade, 5);
    EXPECT_EQ(rc.run.master_deck[0].upgrade, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(RestSites, PeacePipeTokeUsesPurgeableGridAndMasterDeckRemovalDoor) {
    RunController rc = enter_floor_one_rest(20);
    set_relics(rc.run,
               {RelicSlot{static_cast<uint16_t>(RelicId::PEACE_PIPE), -1},
                RelicSlot{static_cast<uint16_t>(RelicId::DU_VU_DOLL), 0}});
    rc.run.master_deck_count = 2;
    rc.run.master_deck[0] =
        CardInstance{static_cast<uint16_t>(CardId::ASCENDERS_BANE), 0, 0, 0, 0};
    rc.run.master_deck[1] =
        CardInstance{static_cast<uint16_t>(CardId::PARASITE), 0, 0, 0, 0};
    rc.run.hp = rc.run.max_hp = 75;

    step(rc, make_action(ActionVerb::CHOOSE,
                         option_index(rc.run, RestOptionKind::TOKE)));
    ASSERT_EQ(rc.rest.screen, static_cast<uint8_t>(RestScreen::TOKE));
    RunActionMask mask{};
    legal_actions(rc, mask);
    EXPECT_FALSE(mask.can_choose_master_deck[0]);
    EXPECT_TRUE(mask.can_choose_master_deck[1]);

    step(rc, make_action(ActionVerb::CHOOSE, 1));
    ASSERT_EQ(rc.run.master_deck_count, 1);
    EXPECT_EQ(static_cast<CardId>(rc.run.master_deck[0].card_id),
              CardId::ASCENDERS_BANE);
    EXPECT_EQ(rc.run.max_hp, 72);  // Parasite.onRemoveFromMasterDeck
    EXPECT_EQ(rc.run.hp, 72);
    EXPECT_EQ(rc.run.relics[1].counter, 1);  // Du-Vu Doll recomputed
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(RestSites, GiryaLiftStopsAtThreeAndPersistsInRelicSlot) {
    RunController rc = enter_floor_one_rest();
    set_relics(rc.run,
               {RelicSlot{static_cast<uint16_t>(RelicId::GIRYA), 2}});
    const uint8_t lift = option_index(rc.run, RestOptionKind::LIFT);
    ASSERT_NE(lift, 0xFF);

    step(rc, make_action(ActionVerb::CHOOSE, lift));
    EXPECT_EQ(rc.run.relics[0].counter, 3);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));

    rc.phase = static_cast<uint8_t>(RunPhase::REST_SITE);
    rc.rest.screen = static_cast<uint8_t>(RestScreen::MENU);
    const RestMenu capped = build_rest_menu(rc.run);
    ASSERT_LT(lift, capped.count);
    EXPECT_FALSE(capped.entries[lift].usable);
    const uint64_t hash_before = hash_state(rc.run);
    step(rc, make_action(ActionVerb::CHOOSE, lift));
    EXPECT_EQ(rc.run.relics[0].counter, 3);
    EXPECT_EQ(hash_state(rc.run), hash_before);
}

TEST(RestSites, ShovelConsumesTierRollThenFrontPopAndOpensRelicReward) {
    RunController rc = enter_floor_one_rest();
    set_relics(rc.run,
               {RelicSlot{static_cast<uint16_t>(RelicId::SHOVEL), -1}});
    RunState expected = rc.run;
    RelicSpawnContext ctx{};
    ctx.floor = expected.floor;
    fill_deck_spawn_gates(expected, ctx);
    fill_campfire_relic_count(expected, ctx);
    fill_boss_spawn_gates(expected, ctx);
    const RelicTier tier = return_random_relic_tier(expected);
    const RelicId relic = return_random_relic_key(expected, tier, ctx);

    const uint8_t dig = option_index(rc.run, RestOptionKind::DIG);
    ASSERT_NE(dig, 0xFF);
    step(rc, make_action(ActionVerb::CHOOSE, dig));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    ASSERT_EQ(rc.rewards.count, 1);
    EXPECT_EQ(static_cast<RewardItemKind>(rc.rewards.items[0].kind),
              RewardItemKind::RELIC);
    EXPECT_EQ(static_cast<RelicId>(rc.rewards.items[0].id), relic);
    EXPECT_EQ(rc.run.relic_rng.counter, expected.relic_rng.counter);
    EXPECT_EQ(hash_state(rc.run), hash_state(expected))
        << "tier draw and pool pop are the only immediate run mutations";
    EXPECT_EQ(rc.run.relic_count, 1)
        << "the dug relic is not owned until the reward is claimed";

    step(rc, make_action(ActionVerb::CHOOSE, 0));
    EXPECT_EQ(rc.run.relic_count, 2);
    EXPECT_EQ(static_cast<RelicId>(rc.run.relics[1].relic_id), relic);
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(RestSites, ShovelRelicAtFixedCapStaysUnclaimedAndCanBeSkipped) {
    RunController rc = enter_floor_one_rest();
    rc.run.relic_count = kRelicCap;
    rc.run.relics[0] =
        RelicSlot{static_cast<uint16_t>(RelicId::SHOVEL), -1};
    for (uint8_t i = 1; i < rc.run.relic_count; ++i) {
        rc.run.relics[i] =
            RelicSlot{static_cast<uint16_t>(RelicId::ANCHOR), -1};
    }

    step(rc, make_action(ActionVerb::CHOOSE,
                         option_index(rc.run, RestOptionKind::DIG)));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    ASSERT_EQ(rc.rewards.count, 1);
    ASSERT_EQ(static_cast<RewardItemKind>(rc.rewards.items[0].kind),
              RewardItemKind::RELIC);
    RunActionMask mask{};
    legal_actions(rc, mask);
    EXPECT_FALSE(mask.can_claim_reward[0]);
    EXPECT_TRUE(mask.can_proceed)
        << "the player can abandon the already-popped dug relic";

    const RunController before = rc;
    step(rc, make_action(ActionVerb::CHOOSE, 0));
    EXPECT_EQ(std::memcmp(&rc, &before, sizeof(rc)), 0)
        << "a forced claim must neither consume the item nor mutate the run";
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(rc.rewards.count, 0);
}

TEST(RestSites, CopiedControllersExposeIdenticalMasksAndTransitions) {
    RunController a = enter_floor_one_rest();
    set_relics(a.run,
               {RelicSlot{static_cast<uint16_t>(RelicId::GIRYA), 1}});
    RunController b = a;
    RunActionMask ma{};
    RunActionMask mb{};
    legal_actions(a, ma);
    legal_actions(b, mb);
    EXPECT_EQ(std::memcmp(&ma, &mb, sizeof(ma)), 0);

    const Action lift =
        make_action(ActionVerb::CHOOSE,
                    option_index(a.run, RestOptionKind::LIFT));
    step(a, lift);
    step(b, lift);
    EXPECT_EQ(std::memcmp(&a, &b, sizeof(a)), 0);
    EXPECT_EQ(hash_state(a.run), hash_state(b.run));
}

}  // namespace
}  // namespace sts::engine
