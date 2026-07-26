// Act-1 treasure rooms: exhaustive chest threshold tables, trap-16's shared
// contents roll, open/skip run lifecycle, and every registered chest hook.
//
// Source order under test (AbstractChest.java:62-102):
//   onChestOpen acquisition pass -> optional treasureRng gold float ->
//   base relic pool pop -> onChestOpenAfter acquisition pass.
// Cursed Key's returnRandomCurse() is evaluated before
// ShowCardAndObtainEffect's constructor checks Omamori
// (CursedKey.java:52-59; ShowCardAndObtainEffect.java:30-45), so cardRng always
// advances even when the curse is blocked.

#include <array>
#include <cstdint>
#include <initializer_list>

#include "gtest/gtest.h"

#include "sts/engine/card_pools.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/map_gen.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/treasure_rooms.hpp"

namespace sts::engine {
namespace {

constexpr int pool_index(RelicTier tier) {
    switch (tier) {
        case RelicTier::COMMON:
            return static_cast<int>(RelicPool::COMMON);
        case RelicTier::UNCOMMON:
            return static_cast<int>(RelicPool::UNCOMMON);
        case RelicTier::RARE:
            return static_cast<int>(RelicPool::RARE);
        case RelicTier::SHOP:
            return static_cast<int>(RelicPool::SHOP);
        case RelicTier::BOSS:
            return static_cast<int>(RelicPool::BOSS);
        default:
            return 0;
    }
}

void set_pool(RunState& rs, RelicTier tier,
              std::initializer_list<RelicId> ids) {
    const int p = pool_index(tier);
    rs.relic_pool_count[p] = static_cast<uint8_t>(ids.size());
    int i = 0;
    for (RelicId id : ids) {
        rs.relic_pools[p][i++] = static_cast<uint16_t>(id);
    }
}

void give_relics(RunState& rs,
                 std::initializer_list<std::pair<RelicId, int16_t>> ids) {
    rs.relic_count = 0;
    for (const auto& [id, counter] : ids) {
        RelicSlot& slot = rs.relics[rs.relic_count++];
        slot.relic_id = static_cast<uint16_t>(id);
        slot.counter = counter;
    }
}

int find_reward(const RewardScreen& s, RewardItemKind kind) {
    for (uint8_t i = 0; i < s.count; ++i) {
        if (static_cast<RewardItemKind>(s.items[i].kind) == kind) {
            return i;
        }
    }
    return -1;
}

void step(RunController& rc, Action action) {
    StepResult result{};
    advance(std::span<RunController>(&rc, 1),
            std::span<const Action>(&action, 1),
            std::span<StepResult>(&result, 1));
}

TEST(TreasureChestTable, ExactSizeAndContentsBoundaries) {
    EXPECT_EQ(static_cast<ChestSize>(treasure_chest_for_rolls(49, 0).size),
              ChestSize::SMALL);
    EXPECT_EQ(static_cast<ChestSize>(treasure_chest_for_rolls(50, 0).size),
              ChestSize::MEDIUM);
    EXPECT_EQ(static_cast<ChestSize>(treasure_chest_for_rolls(82, 0).size),
              ChestSize::MEDIUM);
    EXPECT_EQ(static_cast<ChestSize>(treasure_chest_for_rolls(83, 0).size),
              ChestSize::LARGE);

    struct Expected {
        int size_roll;
        int gold;
        int common;
        int uncommon;
        int rare;
    };
    constexpr Expected rows[] = {
        {0, 50, 75, 25, 0},
        {50, 35, 35, 50, 15},
        {83, 50, 0, 75, 25},
    };
    for (const Expected& e : rows) {
        int gold = 0;
        int common = 0;
        int uncommon = 0;
        int rare = 0;
        for (int roll = 0; roll < 100; ++roll) {
            const TreasureChest c =
                treasure_chest_for_rolls(e.size_roll, roll);
            gold += c.has_gold != 0;
            switch (static_cast<RelicTier>(c.relic_tier)) {
                case RelicTier::COMMON: ++common; break;
                case RelicTier::UNCOMMON: ++uncommon; break;
                case RelicTier::RARE: ++rare; break;
                default: FAIL() << "non-chest relic tier"; break;
            }
        }
        EXPECT_EQ(gold, e.gold);
        EXPECT_EQ(common, e.common);
        EXPECT_EQ(uncommon, e.uncommon);
        EXPECT_EQ(rare, e.rare);
    }
}

TEST(TreasureChestTable, Trap16UsesOneContentsRollForGoldAndTier) {
    for (int contents = 0; contents < 100; ++contents) {
        const TreasureChest small = treasure_chest_for_rolls(0, contents);
        EXPECT_EQ(small.has_gold != 0, contents < 50);
        EXPECT_EQ(static_cast<RelicTier>(small.relic_tier),
                  contents < 75 ? RelicTier::COMMON
                                : RelicTier::UNCOMMON);

        const TreasureChest medium = treasure_chest_for_rolls(50, contents);
        EXPECT_EQ(medium.has_gold != 0, contents < 35);
        EXPECT_EQ(static_cast<RelicTier>(medium.relic_tier),
                  contents < 35
                      ? RelicTier::COMMON
                      : (contents < 85 ? RelicTier::UNCOMMON
                                       : RelicTier::RARE));

        const TreasureChest large = treasure_chest_for_rolls(83, contents);
        EXPECT_EQ(large.has_gold != 0, contents < 50);
        EXPECT_EQ(static_cast<RelicTier>(large.relic_tier),
                  contents < 75 ? RelicTier::UNCOMMON : RelicTier::RARE);
    }

    RunState rs{};
    rs.treasure_rng = from_seed(9191);
    RngStream expected = rs.treasure_rng;
    const int size_roll = random(expected, 0, 99);
    const int contents_roll = random(expected, 0, 99);
    const TreasureChest got = roll_treasure_chest(rs);
    const TreasureChest want =
        treasure_chest_for_rolls(size_roll, contents_roll);
    EXPECT_EQ(got.size, want.size);
    EXPECT_EQ(got.has_gold, want.has_gold);
    EXPECT_EQ(got.relic_tier, want.relic_tier);
    EXPECT_EQ(rs.treasure_rng.counter, 2);
    EXPECT_EQ(rs.treasure_rng.s0, expected.s0);
    EXPECT_EQ(rs.treasure_rng.s1, expected.s1);
}

TEST(TreasureOpen, GoldUsesExactFloatRangeAndTreasureHasNoGoldenIdolBonus) {
    RunState rs{};
    rs.floor = 9;
    rs.act = 1;
    rs.treasure_rng = from_seed(717);
    give_relics(rs, {{RelicId::GOLDEN_IDOL, -1}});
    set_pool(rs, RelicTier::COMMON, {RelicId::ANCHOR});
    TreasureChest chest{
        static_cast<uint8_t>(ChestSize::SMALL),
        static_cast<uint8_t>(RelicTier::COMMON), 1, 0};
    RngStream expected = rs.treasure_rng;
    const int gold = mathutils_round(random(expected, 25.0f * 0.9f,
                                            25.0f * 1.1f));
    RngStream misc = from_seed(1);
    RewardScreen rewards{};
    open_treasure_chest(rs, misc, chest, rewards);

    ASSERT_EQ(rewards.count, 2);
    ASSERT_EQ(rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::GOLD));
    EXPECT_EQ(rewards.items[0].gold, gold);
    EXPECT_GE(rewards.items[0].gold, 23);
    EXPECT_LE(rewards.items[0].gold, 28);
    EXPECT_EQ(rewards.items[0].bonus_gold, 0);
    EXPECT_EQ(rs.treasure_rng.counter, 1);
    EXPECT_EQ(rs.treasure_rng.s0, expected.s0);
    EXPECT_EQ(rs.treasure_rng.s1, expected.s1);
    EXPECT_EQ(rewards.items[1].id,
              static_cast<uint16_t>(RelicId::ANCHOR));
}

TEST(TreasureOpen, BaseRelicUsesThePreRolledTierAndB46ClaimDoor) {
    struct Row {
        RelicTier tier;
        RelicId relic;
    };
    constexpr Row rows[] = {
        {RelicTier::COMMON, RelicId::ANCHOR},
        {RelicTier::UNCOMMON, RelicId::PEAR},
        {RelicTier::RARE, RelicId::MANGO},
    };
    for (const Row& row : rows) {
        RunState rs{};
        rs.floor = 9;
        rs.act = 1;
        rs.hp = 50;
        rs.max_hp = 80;
        set_pool(rs, row.tier, {row.relic});
        TreasureChest chest{
            static_cast<uint8_t>(ChestSize::MEDIUM),
            static_cast<uint8_t>(row.tier), 0, 0};
        RewardScreen rewards{};
        RngStream misc = from_seed(2);
        open_treasure_chest(rs, misc, chest, rewards);
        ASSERT_EQ(rewards.count, 1);
        EXPECT_EQ(rewards.items[0].id, static_cast<uint16_t>(row.relic));
        EXPECT_EQ(rs.relic_pool_count[pool_index(row.tier)], 0);
        EXPECT_TRUE(claim_reward(rs, misc, rewards, 0));
        ASSERT_EQ(rs.relic_count, 1);
        EXPECT_EQ(rs.relics[0].relic_id,
                  static_cast<uint16_t>(row.relic));
        EXPECT_EQ(rewards.count, 0);
    }
}

TEST(TreasureHooks, ExactBeforeBaseAfterOrderMakesNlothRemoveMatryoshkaBonus) {
    RunState rs{};
    rs.floor = 9;
    rs.act = 1;
    give_relics(rs, {{RelicId::NLOTHS_MASK, 1},
                     {RelicId::MATRYOSHKA, 2}});
    set_pool(rs, RelicTier::COMMON,
             {RelicId::ANCHOR, RelicId::VAJRA});
    set_pool(rs, RelicTier::UNCOMMON, {RelicId::PEAR});

    int64_t seed = 0;
    for (;; ++seed) {
        RngStream probe = from_seed(seed);
        if (random_boolean(probe, 0.75f)) break;
    }
    rs.relic_rng = from_seed(seed);  // force Matryoshka's COMMON branch.

    TreasureChest chest{
        static_cast<uint8_t>(ChestSize::SMALL),
        static_cast<uint8_t>(RelicTier::COMMON), 0, 0};
    RewardScreen rewards{};
    RngStream misc = from_seed(3);
    open_treasure_chest(rs, misc, chest, rewards);

    // Matryoshka popped/inserted Anchor before the base relic popped/inserted
    // Vajra. N'loth's after hook removes the first RELIC reward, not the last.
    ASSERT_EQ(rewards.count, 1);
    EXPECT_EQ(rewards.items[0].id,
              static_cast<uint16_t>(RelicId::VAJRA));
    EXPECT_EQ(rs.relic_pool_count[pool_index(RelicTier::COMMON)], 0);
    EXPECT_EQ(rs.relics[0].counter, -2);
    EXPECT_EQ(rs.relics[1].counter, 1);
    EXPECT_EQ(rs.relic_rng.counter, 1);
}

TEST(TreasureHooks, MatryoshkaHasTwoNonBossUsesThenStopsDrawing) {
    RunState rs{};
    rs.floor = 9;
    rs.act = 1;
    rs.relic_rng = from_seed(8);
    give_relics(rs, {{RelicId::MATRYOSHKA, 2}});
    set_pool(rs, RelicTier::COMMON,
             {RelicId::ANCHOR, RelicId::VAJRA, RelicId::BLOOD_VIAL});
    set_pool(rs, RelicTier::UNCOMMON,
             {RelicId::PEAR, RelicId::PANTOGRAPH, RelicId::HORN_CLEAT});

    RewardScreen first{};
    dispatch_relics_on_chest_open(rs, first, false);
    EXPECT_EQ(first.count, 1);
    EXPECT_EQ(rs.relics[0].counter, 1);
    EXPECT_EQ(rs.relic_rng.counter, 1);

    RewardScreen second{};
    dispatch_relics_on_chest_open(rs, second, false);
    EXPECT_EQ(second.count, 1);
    EXPECT_EQ(rs.relics[0].counter, -2);
    EXPECT_EQ(rs.relic_rng.counter, 2);

    RewardScreen third{};
    dispatch_relics_on_chest_open(rs, third, false);
    EXPECT_EQ(third.count, 0);
    EXPECT_EQ(rs.relics[0].counter, -2);
    EXPECT_EQ(rs.relic_rng.counter, 2);
}

TEST(TreasureHooks, CursedKeyAlwaysDrawsIdentityBeforeOmamoriBlocks) {
    RunState rs{};
    rs.card_rng = from_seed(111);
    rs.gold = 20;
    give_relics(rs, {{RelicId::CURSED_KEY, -1},
                     {RelicId::OMAMORI, 2},
                     {RelicId::CERAMIC_FISH, -1}});
    const RngStream before = rs.card_rng;
    RewardScreen rewards{};
    dispatch_relics_on_chest_open(rs, rewards, false);
    EXPECT_EQ(rs.card_rng.counter, before.counter + 1);
    EXPECT_EQ(rs.master_deck_count, 0);
    EXPECT_EQ(rs.relics[1].counter, 1);
    EXPECT_EQ(rs.gold, 20);  // no obtained card, so Ceramic Fish does not fire.
}

TEST(TreasureHooks, CursedKeyCurseUsesTheMasterDeckObtainHooks) {
    RunState rs{};
    rs.card_rng = from_seed(222);
    rs.gold = 20;
    rs.hp = 50;
    rs.max_hp = 80;
    give_relics(rs, {{RelicId::CURSED_KEY, -1},
                     {RelicId::CERAMIC_FISH, -1},
                     {RelicId::DARKSTONE_PERIAPT, -1},
                     {RelicId::DU_VU_DOLL, 0}});
    RngStream expected_rng = rs.card_rng;
    const CardId expected = return_random_curse(expected_rng);
    RewardScreen rewards{};
    dispatch_relics_on_chest_open(rs, rewards, false);

    ASSERT_EQ(rs.master_deck_count, 1);
    EXPECT_EQ(rs.master_deck[0].card_id,
              static_cast<uint16_t>(expected));
    EXPECT_EQ(rs.card_rng.s0, expected_rng.s0);
    EXPECT_EQ(rs.card_rng.s1, expected_rng.s1);
    EXPECT_EQ(rs.gold, 29);
    EXPECT_EQ(rs.hp, 56);
    EXPECT_EQ(rs.max_hp, 86);
    EXPECT_EQ(rs.relics[3].counter, 1);
}

TEST(TreasureHooks, MultipleCursedKeysConsumeOmamoriThenAcquireInOrder) {
    RunState rs{};
    rs.card_rng = from_seed(333);
    rs.gold = 0;
    give_relics(rs, {{RelicId::CURSED_KEY, -1},
                     {RelicId::OMAMORI, 1},
                     {RelicId::CURSED_KEY, -1},
                     {RelicId::CERAMIC_FISH, -1}});
    RewardScreen rewards{};
    dispatch_relics_on_chest_open(rs, rewards, false);
    EXPECT_EQ(rs.card_rng.counter, 2);
    EXPECT_EQ(rs.relics[1].counter, 0);
    EXPECT_EQ(rs.master_deck_count, 1);
    EXPECT_EQ(rs.gold, 9);
}

TEST(TreasureHooks, AllThreeHooksAreNoOpsForBossChest) {
    RunState rs{};
    rs.card_rng = from_seed(4);
    rs.relic_rng = from_seed(5);
    give_relics(rs, {{RelicId::MATRYOSHKA, 2},
                     {RelicId::CURSED_KEY, -1},
                     {RelicId::NLOTHS_MASK, 1}});
    RewardScreen rewards{};
    rewards.count = 1;
    rewards.items[0].kind = static_cast<uint8_t>(RewardItemKind::RELIC);
    rewards.items[0].id = static_cast<uint16_t>(RelicId::ANCHOR);
    dispatch_relics_on_chest_open(rs, rewards, true);
    dispatch_relics_on_chest_open_after(rs, rewards, true);
    EXPECT_EQ(rs.card_rng.counter, 0);
    EXPECT_EQ(rs.relic_rng.counter, 0);
    EXPECT_EQ(rs.master_deck_count, 0);
    EXPECT_EQ(rs.relics[0].counter, 2);
    EXPECT_EQ(rs.relics[2].counter, 1);
    ASSERT_EQ(rewards.count, 1);
    EXPECT_EQ(rewards.items[0].id,
              static_cast<uint16_t>(RelicId::ANCHOR));
}

TEST(TreasureLifecycle, FixedRowEntryOffersOpenAndSkipThenReusesRewardFlow) {
    static_assert(static_cast<uint8_t>(RunPhase::TREASURE_ROOM) == 8);
    static_assert(static_cast<uint8_t>(RunPhase::TREASURE_ROOM) !=
                  kReservedRestSiteRunPhaseValue);
    EXPECT_NE(static_cast<uint8_t>(RunPhase::TREASURE_ROOM),
              kReservedRestSiteRunPhaseValue);

    RunController rc = run_begin(8080, 20);
    for (int x = 0; x < kMapCols; ++x) {
        EXPECT_EQ(
            static_cast<RoomType>(
                rc.run.map[run_state_map_index(x, 8)].room_type),
            RoomType::Treasure);
    }

    rc.run.floor = 8;
    rc.cur_x = 0;
    rc.run.map[run_state_map_index(0, 7)].room_type =
        static_cast<uint8_t>(RoomType::Event);
    rc.run.map[run_state_map_index(0, 8)].room_type =
        static_cast<uint8_t>(RoomType::Treasure);
    RngStream expected = rc.run.treasure_rng;
    const int size_roll = random(expected, 0, 99);
    const int contents_roll = random(expected, 0, 99);
    const TreasureChest expected_chest =
        treasure_chest_for_rolls(size_roll, contents_roll);

    next_room_transition(rc, 0, false);
    EXPECT_EQ(rc.run.floor, 9);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::TREASURE_ROOM));
    EXPECT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::Treasure));
    EXPECT_EQ(rc.treasure_chest.size, expected_chest.size);
    EXPECT_EQ(rc.treasure_chest.relic_tier, expected_chest.relic_tier);
    EXPECT_EQ(rc.treasure_chest.has_gold, expected_chest.has_gold);
    EXPECT_EQ(rc.run.treasure_rng.counter, expected.counter);

    RunActionMask mask{};
    legal_actions(rc, mask);
    EXPECT_TRUE(mask.can_open_chest);
    EXPECT_TRUE(mask.can_proceed);
    EXPECT_EQ(mask.phase, static_cast<uint8_t>(RunPhase::TREASURE_ROOM));

    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    EXPECT_EQ(rc.treasure_chest.opened, 1);
    EXPECT_GE(find_reward(rc.rewards, RewardItemKind::RELIC), 0);
    legal_actions(rc, mask);
    EXPECT_TRUE(mask.can_proceed);
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(rc.treasure_chest.size,
              static_cast<uint8_t>(ChestSize::NONE));
}

TEST(TreasureLifecycle, SkipConsumesNoOpenTimeRngOrHooks) {
    RunController rc = run_begin(9090, 20);
    rc.run.floor = 8;
    rc.cur_x = 0;
    rc.run.map[run_state_map_index(0, 7)].room_type =
        static_cast<uint8_t>(RoomType::Event);
    rc.run.map[run_state_map_index(0, 8)].room_type =
        static_cast<uint8_t>(RoomType::Treasure);
    next_room_transition(rc, 0, false);
    const RngStream treasure_before = rc.run.treasure_rng;
    const RngStream relic_before = rc.run.relic_rng;
    const RngStream card_before = rc.run.card_rng;
    const uint8_t relic_count = rc.run.relic_count;

    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(rc.run.treasure_rng.counter, treasure_before.counter);
    EXPECT_EQ(rc.run.relic_rng.counter, relic_before.counter);
    EXPECT_EQ(rc.run.card_rng.counter, card_before.counter);
    EXPECT_EQ(rc.run.relic_count, relic_count);
    EXPECT_EQ(rc.rewards.count, 0);
}

}  // namespace
}  // namespace sts::engine
