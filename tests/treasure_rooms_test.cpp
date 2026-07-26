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
#include <cstring>
#include <initializer_list>
#include <type_traits>

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

// Bulk deck setup for capacity tests: raw rows on purpose (no obtain hooks),
// like run_begin's starter-deck write. Clears every row first so byte compares
// of RunState cannot see stale rows past the count.
void fill_master_deck(RunState& rs, int count) {
    for (CardInstance& c : rs.master_deck) {
        c = CardInstance{};
    }
    rs.master_deck_count = 0;
    for (int i = 0; i < count; ++i) {
        CardInstance& c = rs.master_deck[rs.master_deck_count++];
        c.card_id = static_cast<uint16_t>(CardId::STRIKE);
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

template <typename T>
void expect_byte_equal(const T& got, const T& want, const char* label) {
    static_assert(std::is_trivially_copyable_v<T>);
    EXPECT_EQ(std::memcmp(&got, &want, sizeof(T)), 0) << label;
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
    RewardScreen rewards{};
    ASSERT_TRUE(open_treasure_chest(rs, chest, rewards));

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
        RngStream misc = from_seed(2);  // claim-time stream only.
        ASSERT_TRUE(open_treasure_chest(rs, chest, rewards));
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
    ASSERT_TRUE(open_treasure_chest(rs, chest, rewards));

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
    ASSERT_TRUE(dispatch_relics_on_chest_open(rs, first, false));
    EXPECT_EQ(first.count, 1);
    EXPECT_EQ(rs.relics[0].counter, 1);
    EXPECT_EQ(rs.relic_rng.counter, 1);

    RewardScreen second{};
    ASSERT_TRUE(dispatch_relics_on_chest_open(rs, second, false));
    EXPECT_EQ(second.count, 1);
    EXPECT_EQ(rs.relics[0].counter, -2);
    EXPECT_EQ(rs.relic_rng.counter, 2);

    RewardScreen third{};
    ASSERT_TRUE(dispatch_relics_on_chest_open(rs, third, false));
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
    ASSERT_TRUE(dispatch_relics_on_chest_open(rs, rewards, false));
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
    ASSERT_TRUE(dispatch_relics_on_chest_open(rs, rewards, false));

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
    ASSERT_TRUE(dispatch_relics_on_chest_open(rs, rewards, false));
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
    ASSERT_TRUE(dispatch_relics_on_chest_open(rs, rewards, true));
    ASSERT_TRUE(dispatch_relics_on_chest_open_after(rs, rewards, true));
    EXPECT_EQ(rs.card_rng.counter, 0);
    EXPECT_EQ(rs.relic_rng.counter, 0);
    EXPECT_EQ(rs.master_deck_count, 0);
    EXPECT_EQ(rs.relics[0].counter, 2);
    EXPECT_EQ(rs.relics[2].counter, 1);
    ASSERT_EQ(rewards.count, 1);
    EXPECT_EQ(rewards.items[0].id,
              static_cast<uint16_t>(RelicId::ANCHOR));
}

TEST(TreasureCapacity, PublicBeforeHookRollsBackLateNearFullFailure) {
    RunState rs{};
    rs.floor = 9;
    rs.act = 1;
    rs.card_rng = from_seed(404);
    rs.relic_rng = from_seed(505);
    give_relics(rs, {{RelicId::CURSED_KEY, -1},
                     {RelicId::MATRYOSHKA, 1},
                     {RelicId::MATRYOSHKA, 1}});
    set_pool(rs, RelicTier::COMMON,
             {RelicId::ANCHOR, RelicId::VAJRA});
    set_pool(rs, RelicTier::UNCOMMON,
             {RelicId::PEAR, RelicId::HORN_CLEAT});

    RewardScreen rewards{};
    rewards.open_card_item = kNoOpenCardReward;
    rewards.count = static_cast<uint8_t>(kRewardItemCap - 1);
    for (uint8_t i = 0; i < rewards.count; ++i) {
        rewards.items[i].kind =
            static_cast<uint8_t>(RewardItemKind::GOLD);
        rewards.items[i].gold = static_cast<int16_t>(10 + i);
    }

    const RunState before_rs = rs;
    const RewardScreen before_rewards = rewards;
    EXPECT_FALSE(dispatch_relics_on_chest_open(rs, rewards, false));
    expect_byte_equal(rs, before_rs, "RunState transaction rollback");
    expect_byte_equal(
        rewards, before_rewards, "RewardScreen transaction rollback");
}

TEST(TreasureCapacity, CursedKeyAtFullMasterDeckRejectsOpenAtomically) {
    RunController rc{};
    rc.phase = static_cast<uint8_t>(RunPhase::TREASURE_ROOM);
    rc.run.floor = 9;
    rc.run.act = 1;
    rc.run.card_rng = from_seed(1201);
    rc.run.relic_rng = from_seed(1202);
    rc.run.treasure_rng = from_seed(1203);
    rc.combat.misc_rng = from_seed(1204);
    rc.treasure_chest = TreasureChest{
        static_cast<uint8_t>(ChestSize::SMALL),
        static_cast<uint8_t>(RelicTier::COMMON), 1, 0};
    give_relics(rc.run, {{RelicId::CURSED_KEY, -1}});
    set_pool(rc.run, RelicTier::COMMON, {RelicId::ANCHOR});
    fill_master_deck(rc.run, kMasterDeckCap);
    rc.rewards.open_card_item = kNoOpenCardReward;

    // The unblocked key needs a deck slot and none remain: the single
    // authority must already say no.
    RunActionMask mask{};
    legal_actions(rc, mask);
    EXPECT_FALSE(mask.can_open_chest);
    EXPECT_TRUE(mask.can_proceed);

    // Whole-controller byte tests: a forced step and a direct open are both
    // total no-ops -- no lost curse, no committed RNG/pool/reward changes.
    const RunController before = rc;
    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    expect_byte_equal(rc, before, "forced full-deck controller open");

    EXPECT_FALSE(open_treasure_chest(
        rc.run, rc.treasure_chest, rc.rewards));
    expect_byte_equal(rc, before, "direct full-deck open");
}

TEST(TreasureCapacity, BeforeHookRollsBackWhenCurseCannotJoinFullDeck) {
    // Matryoshka fires first (relicRng draw, pool pop, counter tick), THEN the
    // Cursed Key add fails at the full deck: the public seam must roll ALL of
    // it back, not just the deck write.
    RunState rs{};
    rs.floor = 9;
    rs.act = 1;
    rs.card_rng = from_seed(1301);
    rs.relic_rng = from_seed(1302);
    give_relics(rs, {{RelicId::MATRYOSHKA, 1},
                     {RelicId::CURSED_KEY, -1}});
    set_pool(rs, RelicTier::COMMON, {RelicId::ANCHOR});
    set_pool(rs, RelicTier::UNCOMMON, {RelicId::PEAR});
    fill_master_deck(rs, kMasterDeckCap);

    RewardScreen rewards{};
    rewards.open_card_item = kNoOpenCardReward;
    const RunState before_rs = rs;
    const RewardScreen before_rewards = rewards;
    EXPECT_FALSE(dispatch_relics_on_chest_open(rs, rewards, false));
    expect_byte_equal(rs, before_rs, "full-deck curse RunState rollback");
    expect_byte_equal(
        rewards, before_rewards, "full-deck curse RewardScreen rollback");
}

TEST(TreasureCapacity, OrderedCursedKeysPreflightAgainstRemainingSlots) {
    // Acquisition order: OMAMORI(1), KEY, KEY, OMAMORI(2), KEY. The FIRST
    // Omamori's single charge blocks key #1 and is spent; keys #2 and #3 are
    // unblocked because getRelic keeps returning that first (now empty)
    // Omamori -- the second one is never consulted. Two deck slots required.
    RunState rs{};
    rs.floor = 9;
    rs.act = 1;
    rs.card_rng = from_seed(1401);
    give_relics(rs, {{RelicId::OMAMORI, 1},
                     {RelicId::CURSED_KEY, -1},
                     {RelicId::CURSED_KEY, -1},
                     {RelicId::OMAMORI, 2},
                     {RelicId::CURSED_KEY, -1}});
    set_pool(rs, RelicTier::COMMON, {RelicId::ANCHOR});
    const TreasureChest chest{
        static_cast<uint8_t>(ChestSize::SMALL),
        static_cast<uint8_t>(RelicTier::COMMON), 0, 0};

    // One slot short of the two required: illegal, and the direct hook seam
    // rolls back byte-stably (identity draws included).
    fill_master_deck(rs, kMasterDeckCap - 1);
    EXPECT_FALSE(treasure_chest_open_legal(rs, chest));
    {
        RewardScreen rewards{};
        rewards.open_card_item = kNoOpenCardReward;
        const RunState before_rs = rs;
        const RewardScreen before_rewards = rewards;
        EXPECT_FALSE(dispatch_relics_on_chest_open(rs, rewards, false));
        expect_byte_equal(rs, before_rs, "one-slot-short RunState rollback");
        expect_byte_equal(rewards, before_rewards,
                          "one-slot-short RewardScreen rollback");
    }

    // Exactly two slots free: legal, and the open lands both curses, spends
    // the first Omamori's last charge, leaves the second untouched, and drew
    // one cardRng identity per key (blocked or not).
    fill_master_deck(rs, kMasterDeckCap - 2);
    EXPECT_TRUE(treasure_chest_open_legal(rs, chest));
    TreasureChest open_chest = chest;
    RewardScreen rewards{};
    ASSERT_TRUE(open_treasure_chest(rs, open_chest, rewards));
    EXPECT_EQ(rs.master_deck_count, kMasterDeckCap);
    EXPECT_EQ(rs.card_rng.counter, 3);
    EXPECT_EQ(rs.relics[0].counter, 0);
    EXPECT_EQ(rs.relics[3].counter, 2);
    EXPECT_EQ(open_chest.opened, 1);
}

TEST(TreasureCapacity, OmamoriChargesDepleteAcrossKeysInThePreflight) {
    // One Omamori with two charges ahead of three keys: it blocks keys #1 and
    // #2, then key #3 needs a slot -- the preflight must model the depletion,
    // not treat Omamori as an unconditional block.
    RunState rs{};
    rs.floor = 9;
    rs.act = 1;
    rs.card_rng = from_seed(1501);
    give_relics(rs, {{RelicId::OMAMORI, 2},
                     {RelicId::CURSED_KEY, -1},
                     {RelicId::CURSED_KEY, -1},
                     {RelicId::CURSED_KEY, -1}});
    set_pool(rs, RelicTier::COMMON, {RelicId::ANCHOR});
    const TreasureChest chest{
        static_cast<uint8_t>(ChestSize::SMALL),
        static_cast<uint8_t>(RelicTier::COMMON), 0, 0};

    fill_master_deck(rs, kMasterDeckCap);
    EXPECT_FALSE(treasure_chest_open_legal(rs, chest));

    fill_master_deck(rs, kMasterDeckCap - 1);
    EXPECT_TRUE(treasure_chest_open_legal(rs, chest));

    // A spent Omamori (counter 0, the value Omamori.use leaves behind) blocks
    // nothing: every key now needs a slot.
    rs.relics[0].counter = 0;
    EXPECT_FALSE(treasure_chest_open_legal(rs, chest));
    fill_master_deck(rs, kMasterDeckCap - 3);
    EXPECT_TRUE(treasure_chest_open_legal(rs, chest));
}

TEST(TreasureCapacity, SevenMatryoshkasRejectOpenWithoutAnyControllerChange) {
    RunController rc{};
    rc.phase = static_cast<uint8_t>(RunPhase::TREASURE_ROOM);
    rc.run.floor = 9;
    rc.run.act = 1;
    rc.run.card_rng = from_seed(606);
    rc.run.relic_rng = from_seed(707);
    rc.run.treasure_rng = from_seed(808);
    rc.combat.misc_rng = from_seed(909);
    rc.treasure_chest = TreasureChest{
        static_cast<uint8_t>(ChestSize::SMALL),
        static_cast<uint8_t>(RelicTier::COMMON), 1, 0};
    rc.run.relic_count = 7;
    for (uint8_t i = 0; i < rc.run.relic_count; ++i) {
        rc.run.relics[i].relic_id =
            static_cast<uint16_t>(RelicId::MATRYOSHKA);
        rc.run.relics[i].counter = 1;
    }
    set_pool(rc.run, RelicTier::COMMON,
             {RelicId::ANCHOR, RelicId::VAJRA, RelicId::BLOOD_VIAL,
              RelicId::BAG_OF_MARBLES, RelicId::BRONZE_SCALES,
              RelicId::CENTENNIAL_PUZZLE, RelicId::HAPPY_FLOWER,
              RelicId::JUZU_BRACELET});
    set_pool(rc.run, RelicTier::UNCOMMON,
             {RelicId::PEAR, RelicId::HORN_CLEAT, RelicId::PANTOGRAPH,
              RelicId::SINGING_BOWL, RelicId::MATRYOSHKA,
              RelicId::NLOTHS_MASK, RelicId::ETERNAL_FEATHER});
    rc.rewards.count = 1;
    rc.rewards.items[0].kind =
        static_cast<uint8_t>(RewardItemKind::GOLD);
    rc.rewards.items[0].gold = 123;

    RunActionMask mask{};
    legal_actions(rc, mask);
    EXPECT_FALSE(mask.can_open_chest);
    EXPECT_TRUE(mask.can_proceed);

    const RunController before = rc;
    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    expect_byte_equal(rc, before, "forced capacity-rejected controller open");

    EXPECT_FALSE(open_treasure_chest(
        rc.run, rc.treasure_chest, rc.rewards));
    expect_byte_equal(rc, before, "direct capacity-rejected open");
}

TEST(TreasureCapacity, ExactEightItemOpenStillSucceeds) {
    RunState rs{};
    rs.floor = 9;
    rs.act = 1;
    rs.relic_rng = from_seed(1001);
    rs.treasure_rng = from_seed(1002);
    give_relics(rs, {{RelicId::MATRYOSHKA, 1},
                     {RelicId::MATRYOSHKA, 1},
                     {RelicId::MATRYOSHKA, 1},
                     {RelicId::MATRYOSHKA, 1},
                     {RelicId::MATRYOSHKA, 1},
                     {RelicId::MATRYOSHKA, 1}});
    set_pool(rs, RelicTier::COMMON,
             {RelicId::ANCHOR, RelicId::VAJRA, RelicId::BLOOD_VIAL,
              RelicId::BAG_OF_MARBLES, RelicId::BRONZE_SCALES,
              RelicId::CENTENNIAL_PUZZLE, RelicId::HAPPY_FLOWER});
    set_pool(rs, RelicTier::UNCOMMON,
             {RelicId::PEAR, RelicId::HORN_CLEAT, RelicId::PANTOGRAPH,
              RelicId::SINGING_BOWL, RelicId::NLOTHS_MASK,
              RelicId::ETERNAL_FEATHER});
    TreasureChest chest{
        static_cast<uint8_t>(ChestSize::SMALL),
        static_cast<uint8_t>(RelicTier::COMMON), 1, 0};
    RewardScreen rewards{};

    ASSERT_TRUE(treasure_chest_open_legal(rs, chest));
    ASSERT_TRUE(open_treasure_chest(rs, chest, rewards));
    EXPECT_EQ(rewards.count, kRewardItemCap);
    EXPECT_EQ(chest.opened, 1);
}

TEST(TreasureMalformed, EveryInvalidDescriptorIsMaskAndStepAtomic) {
    constexpr std::array<TreasureChest, 8> invalid{{
        {static_cast<uint8_t>(ChestSize::NONE),
         static_cast<uint8_t>(RelicTier::COMMON), 0, 0},
        {4, static_cast<uint8_t>(RelicTier::COMMON), 0, 0},
        {static_cast<uint8_t>(ChestSize::SMALL),
         static_cast<uint8_t>(RelicTier::SHOP), 0, 0},
        // Size/tier pairs the generator can never emit: the tier switch is not
        // independent of the size switch.
        {static_cast<uint8_t>(ChestSize::SMALL),
         static_cast<uint8_t>(RelicTier::RARE), 0, 0},
        {static_cast<uint8_t>(ChestSize::LARGE),
         static_cast<uint8_t>(RelicTier::COMMON), 0, 0},
        {static_cast<uint8_t>(ChestSize::SMALL),
         static_cast<uint8_t>(RelicTier::COMMON), 2, 0},
        {static_cast<uint8_t>(ChestSize::SMALL),
         static_cast<uint8_t>(RelicTier::COMMON), 0, 1},
        {static_cast<uint8_t>(ChestSize::SMALL),
         static_cast<uint8_t>(RelicTier::COMMON), 0, 2},
    }};

    for (const TreasureChest& malformed : invalid) {
        RunController rc{};
        rc.phase = static_cast<uint8_t>(RunPhase::TREASURE_ROOM);
        rc.run.floor = 9;
        rc.run.act = 1;
        rc.run.relic_rng = from_seed(1111);
        rc.run.treasure_rng = from_seed(2222);
        rc.combat.misc_rng = from_seed(3333);
        rc.treasure_chest = malformed;
        rc.rewards.open_card_item = kNoOpenCardReward;
        rc.rewards.count = 1;
        rc.rewards.items[0].kind =
            static_cast<uint8_t>(RewardItemKind::GOLD);
        rc.rewards.items[0].gold = 77;

        RunActionMask mask{};
        legal_actions(rc, mask);
        EXPECT_FALSE(mask.can_open_chest);
        EXPECT_TRUE(mask.can_proceed);

        const RunController before = rc;
        step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
        expect_byte_equal(rc, before, "forced malformed controller open");

        EXPECT_FALSE(open_treasure_chest(
            rc.run, rc.treasure_chest, rc.rewards));
        expect_byte_equal(rc, before, "direct malformed open");
    }
}

TEST(TreasureMalformed, DescriptorDomainMatchesGeneratorExhaustively) {
    // Re-derive the constructible (size, tier) pairs from the generator, the
    // single authority, and require the legality predicate to agree on every
    // representable descriptor byte pair. has_gold stays a free 0/1 bit here,
    // matching the predicate's domain (it is not part of the pair table).
    bool constructible[4][8] = {};
    for (int32_t size_roll = 0; size_roll < 100; ++size_roll) {
        for (int32_t contents_roll = 0; contents_roll < 100; ++contents_roll) {
            const TreasureChest c =
                treasure_chest_for_rolls(size_roll, contents_roll);
            ASSERT_LT(c.size, 4);
            ASSERT_LT(c.relic_tier, 8);
            constructible[c.size][c.relic_tier] = true;
        }
    }

    for (uint8_t size = 0; size < 4; ++size) {
        for (uint8_t tier = 0; tier < 8; ++tier) {
            for (uint8_t gold = 0; gold <= 1; ++gold) {
                RunState rs{};
                rs.floor = 9;
                rs.act = 1;
                const TreasureChest chest{size, tier, gold, 0};
                EXPECT_EQ(treasure_chest_open_legal(rs, chest),
                          constructible[size][tier])
                    << "size=" << int{size} << " tier=" << int{tier}
                    << " gold=" << int{gold};
            }
        }
    }
}

TEST(TreasureLifecycle, FixedRowEntryOffersOpenAndSkipThenReusesRewardFlow) {
    // Both room phases are replay-visible, append-only values: rest sites
    // (B4.9) hold 7 and treasure rooms (B4.7) hold 8 — see the shared-namespace
    // allocation table in docs/stage-b-tasks.md.
    static_assert(static_cast<uint8_t>(RunPhase::TREASURE_ROOM) == 8);
    static_assert(static_cast<uint8_t>(RunPhase::REST_SITE) == 7);

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
