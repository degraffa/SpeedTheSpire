// Merchant tier-2 suite: stock generation draw order, the price pipeline, the
// purge ramp, the shop-relic hooks, and the whole CHOOSE flow through the run
// layer.
//
// TWO KINDS OF EXPECTATION LIVE HERE, and the distinction is deliberate.
//
//   STREAM EXACTNESS -- how many draws, off which stream, in which order -- is
//   pinned HARD, by replaying the streams beside the engine and comparing
//   draw for draw. That is the invariant the shop exists to get right.
//
//   IDENTITY expectations are written AGAINST THE GENERATED POOL ARRAYS
//   (kIronclad*Pool, kColorless*Pool), never as hand-copied card ids. Four of
//   those arrays carry a documented interim ORDER deviation (registry-id order
//   until an oracle capture pins CardLibrary's HashMap order), so a hand-copied
//   id would break on a pool reorder that changes nothing about the shop. The
//   nine type-filtered views this task added do NOT deviate -- CardGroup sorts
//   them -- but they are indexed the same way for uniformity.
//
// The one exception is ShopCapture below, which reproduces a REAL captured A20
// merchant and therefore names its cards, relics and potions outright: that is
// the point of the vector.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include "sts/engine/cards.hpp"
#include "sts/engine/potions.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rest_sites.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/shop.hpp"

using namespace sts::engine;

namespace {

constexpr uint8_t kA20 = 20;

// The libGDX MathUtils.round the price pipeline uses, spelled out here so the
// expectations below are independent of the engine helper they check.
[[nodiscard]] int mround(float v) noexcept {
    return static_cast<int>(static_cast<double>(v) + 16384.5) - 16384;
}

[[nodiscard]] bool streams_equal(const RngStream& a, const RngStream& b) noexcept {
    return a.s0 == b.s0 && a.s1 == b.s1 && a.counter == b.counter;
}

void give_relic(RunState& rs, RelicId id) {
    rs.relics[rs.relic_count].relic_id = static_cast<uint16_t>(id);
    rs.relics[rs.relic_count].counter = 0;
    ++rs.relic_count;
}

void fill_pool(RunState& rs, RelicPool tier, std::initializer_list<RelicId> ids) {
    const auto t = static_cast<std::size_t>(tier);
    uint8_t n = 0;
    for (RelicId id : ids) {
        rs.relic_pools[t][n++] = static_cast<uint16_t>(id);
    }
    rs.relic_pool_count[t] = n;
}

// A RunState with just enough shape to build a merchant: streams seeded from a
// fixed seed, A20, a purge cost at its base, and small relic pools whose ends
// are unambiguous.
[[nodiscard]] RunState bare_run(int64_t seed) {
    RunState rs{};
    rs.run_seed = seed;
    rs.ascension = kA20;
    rs.act = 1;
    rs.floor = 5;
    rs.hp = 60;
    rs.max_hp = 75;
    rs.gold = 999;
    rs.purge_cost = kPurgeCostBase;
    rs.potion_slots = 2;
    rs.card_blizz_randomizer = 5;
    rs.card_rng = from_seed(seed);
    rs.merchant_rng = from_seed(seed);
    rs.potion_rng = from_seed(seed);
    // Pool ends chosen to have no canSpawn gate, so an end-pop takes exactly
    // one entry and the draw accounting is unambiguous.
    fill_pool(rs, RelicPool::COMMON,
              {RelicId::AKABEKO, RelicId::VAJRA, RelicId::BLOOD_VIAL});
    fill_pool(rs, RelicPool::UNCOMMON,
              {RelicId::SHURIKEN, RelicId::PEAR, RelicId::QUESTION_CARD});
    fill_pool(rs, RelicPool::RARE,
              {RelicId::CALIPERS, RelicId::TURNIP, RelicId::MANGO});
    fill_pool(rs, RelicPool::SHOP,
              {RelicId::TOOLBOX, RelicId::LEES_WAFFLE, RelicId::MEDICAL_KIT});
    return rs;
}

// A pool-array lookup so identity expectations never hard-code an id.
[[nodiscard]] bool in_pool(const CardId* pool, int n, CardId id) {
    for (int i = 0; i < n; ++i) {
        if (pool[i] == id) return true;
    }
    return false;
}

}  // namespace

// =============================================================================
// The draw-order pin
// =============================================================================

// THE acceptance test: for one fixed merchantRng state, the complete stock and
// every price match a hand-derivation performed draw for draw beside the
// engine. The derivation is the code below plus this table -- one row per
// merchantRng draw, in the order ShopRoom.onPlayerEntry produces them:
//
//   #   consumer                              wrapper call
//   --  ------------------------------------  --------------------------------
//   1   coloredCards[0] price jitter          random(0.9f, 1.1f)
//   2   coloredCards[1] price jitter          random(0.9f, 1.1f)
//   3   coloredCards[2] price jitter          random(0.9f, 1.1f)
//   4   coloredCards[3] price jitter          random(0.9f, 1.1f)
//   5   coloredCards[4] price jitter          random(0.9f, 1.1f)
//   6   colorlessCards[0] price jitter        random(0.9f, 1.1f)
//   7   colorlessCards[1] price jitter        random(0.9f, 1.1f)
//   8   the sale slot                         random(0, 4)
//   9   relic slot 0 tier                     random(99)
//   10  relic slot 0 price jitter             random(0.95f, 1.05f)
//   11  relic slot 1 tier                     random(99)
//   12  relic slot 1 price jitter             random(0.95f, 1.05f)
//   13  relic slot 2 price jitter             random(0.95f, 1.05f)   <- NO tier
//   14  potion slot 0 price jitter            random(0.95f, 1.05f)
//   15  potion slot 1 price jitter            random(0.95f, 1.05f)
//   16  potion slot 2 price jitter            random(0.95f, 1.05f)
//
// Note draw 13: the third relic slot is ALWAYS the SHOP tier and rolls no tier
// (ShopScreen.java:365), which is why a shop is 16 draws and not 17. Note also
// that the relic and potion IDENTITIES cost merchantRng nothing -- the pools
// are end-popped and the potion identity comes off potionRng.
TEST(ShopDrawOrder, SixteenMerchantDrawsInTheJavaOrderForAFixedState) {
    RunState rs = bare_run(0x5EED'1234'5678'9ABCLL);
    // Replay stream, positioned exactly where the engine's will start.
    RngStream m = rs.merchant_rng;
    RngStream c = rs.card_rng;
    RngStream p = rs.potion_rng;

    const ShopState shop = generate_shop(rs);

    // --- the hand-derivation, draw by draw ------------------------------------
    // Cards first: five colored identities off cardRng (rarity roll + pool
    // index each), then the two colourless. Re-derived with the same helpers
    // the engine uses, so this pins the ORDER and the STREAM, not the table.
    auto roll_rarity = [&](RngStream& s) {
        return shop_card_rarity_for_roll(static_cast<int>(random(s, 99)) +
                                         rs.card_blizz_randomizer);
    };
    RewardCardRarity rarity[kShopColoredCount]{};
    CardId ids[kShopColoredCount]{};
    const CardType types[kShopColoredCount] = {CardType::ATTACK, CardType::ATTACK,
                                               CardType::SKILL, CardType::SKILL,
                                               CardType::POWER};
    for (int i = 0; i < kShopColoredCount; ++i) {
        ids[i] = shop_card_from_pool(c, roll_rarity(c), types[i], &rarity[i]);
        // Slots 1 and 3 re-roll while they repeat the slot before them.
        while ((i == 1 && ids[i] == ids[0]) || (i == 3 && ids[i] == ids[2])) {
            ids[i] = shop_card_from_pool(c, roll_rarity(c), types[i], &rarity[i]);
        }
    }
    const CardId cl0 =
        draw_colorless_card_from_pool(c, RewardCardRarity::UNCOMMON);
    const CardId cl1 = draw_colorless_card_from_pool(c, RewardCardRarity::RARE);

    // Draws 1-5: colored prices, truncated, no colourless bump.
    int expect_colored[kShopColoredCount]{};
    for (int i = 0; i < kShopColoredCount; ++i) {
        expect_colored[i] = static_cast<int>(
            static_cast<float>(card_base_price(rarity[i])) *
            random(m, 0.9f, 1.1f));
    }
    // Draws 6-7: colourless prices, x1.2 INSIDE the truncation.
    const int expect_cl0 = static_cast<int>(
        static_cast<float>(card_base_price(RewardCardRarity::UNCOMMON)) *
        random(m, 0.9f, 1.1f) * 1.2f);
    const int expect_cl1 = static_cast<int>(
        static_cast<float>(card_base_price(RewardCardRarity::RARE)) *
        random(m, 0.9f, 1.1f) * 1.2f);
    // Draw 8: the sale slot, then the integer halving.
    const int expect_sale = random(m, 0, 4);
    expect_colored[expect_sale] /= 2;
    // Draws 9-13: two rolled tiers + three jittered relic prices.
    const RelicTier t0 = shop_relic_tier_for_roll(random(m, 99));
    const int relic0_price = mround(static_cast<float>(relic_base_price(t0)) *
                                    random(m, 0.95f, 1.05f));
    const RelicTier t1 = shop_relic_tier_for_roll(random(m, 99));
    const int relic1_price = mround(static_cast<float>(relic_base_price(t1)) *
                                    random(m, 0.95f, 1.05f));
    const int relic2_price =
        mround(static_cast<float>(relic_base_price(RelicTier::SHOP)) *
               random(m, 0.95f, 1.05f));
    // Draws 14-16: three potion identities off potionRng, each priced off the
    // next merchantRng draw.
    int potion_price[kShopPotionCount]{};
    PotionId potion_id[kShopPotionCount]{};
    for (int i = 0; i < kShopPotionCount; ++i) {
        potion_id[i] = return_random_potion(p);
        const PotionDef* def = potion_def(potion_id[i]);
        ASSERT_NE(def, nullptr);
        potion_price[i] = mround(
            static_cast<float>(potion_base_price(def->rarity)) *
            random(m, 0.95f, 1.05f));
    }
    // A20 >= 16: everything x1.1, rounded, in list order (relics, potions,
    // colored, colorless). The purge cost is NOT touched.
    for (int i = 0; i < kShopColoredCount; ++i) {
        expect_colored[i] = mround(static_cast<float>(expect_colored[i]) * 1.1f);
    }
    const int expect_cl0_a16 = mround(static_cast<float>(expect_cl0) * 1.1f);
    const int expect_cl1_a16 = mround(static_cast<float>(expect_cl1) * 1.1f);
    const int relic0_a16 = mround(static_cast<float>(relic0_price) * 1.1f);
    const int relic1_a16 = mround(static_cast<float>(relic1_price) * 1.1f);
    const int relic2_a16 = mround(static_cast<float>(relic2_price) * 1.1f);

    // --- the comparison --------------------------------------------------------
    EXPECT_EQ(rs.merchant_rng.counter, 16) << "a fresh shop is exactly 16 draws";
    EXPECT_TRUE(streams_equal(rs.merchant_rng, m))
        << "the replay must land on the same merchantRng state, draw for draw";
    EXPECT_TRUE(streams_equal(rs.card_rng, c));
    EXPECT_TRUE(streams_equal(rs.potion_rng, p));

    for (int i = 0; i < kShopColoredCount; ++i) {
        EXPECT_EQ(shop.colored[i].id, static_cast<uint16_t>(ids[i])) << i;
        EXPECT_EQ(shop.colored[i].price, expect_colored[i]) << i;
    }
    EXPECT_EQ(shop.sale_index, expect_sale);
    EXPECT_EQ(shop.colorless[0].id, static_cast<uint16_t>(cl0));
    EXPECT_EQ(shop.colorless[1].id, static_cast<uint16_t>(cl1));
    EXPECT_EQ(shop.colorless[0].price, expect_cl0_a16);
    EXPECT_EQ(shop.colorless[1].price, expect_cl1_a16);
    EXPECT_EQ(shop.relics[0].price, relic0_a16);
    EXPECT_EQ(shop.relics[1].price, relic1_a16);
    EXPECT_EQ(shop.relics[2].price, relic2_a16);
    for (int i = 0; i < kShopPotionCount; ++i) {
        EXPECT_EQ(shop.potions[i].id, static_cast<uint16_t>(potion_id[i])) << i;
        EXPECT_EQ(shop.potions[i].price,
                  mround(static_cast<float>(potion_price[i]) * 1.1f))
            << i;
    }
    // Slot 2 is the SHOP-tier slot and takes the end of the shop pool.
    EXPECT_EQ(shop.relics[2].id, static_cast<uint16_t>(RelicId::MEDICAL_KIT));
    EXPECT_EQ(shop.purge_available, 1);
    EXPECT_EQ(shop.actual_purge_cost, kPurgeCostBase);
}

TEST(ShopDrawOrder, StockCameFromTheGeneratedPoolsAndTheRightTypes) {
    // Identity written against the pool ARRAYS: the five colored slots are
    // ATTACK, ATTACK, SKILL, SKILL, POWER, each from one of that type's three
    // rarity views; the two colourless come from the two sorted colourless
    // views. A pool reorder cannot break this.
    for (int64_t seed = 1; seed <= 40; ++seed) {
        RunState rs = bare_run(seed * 2654435761LL);
        const ShopState shop = generate_shop(rs);
        SCOPED_TRACE(seed);
        const CardType want[kShopColoredCount] = {
            CardType::ATTACK, CardType::ATTACK, CardType::SKILL,
            CardType::SKILL, CardType::POWER};
        for (int i = 0; i < kShopColoredCount; ++i) {
            const CardDef* def =
                card_def(static_cast<CardId>(shop.colored[i].id));
            ASSERT_NE(def, nullptr) << i;
            EXPECT_EQ(def->type, want[i]) << i;
        }
        // The two ATTACK slots and the two SKILL slots are deduped against each
        // other (Merchant.java:65-67, :75-77).
        EXPECT_NE(shop.colored[0].id, shop.colored[1].id);
        EXPECT_NE(shop.colored[2].id, shop.colored[3].id);
        EXPECT_TRUE(in_pool(kColorlessUncommonPool.data(),
                            kColorlessUncommonPoolCount,
                            static_cast<CardId>(shop.colorless[0].id)));
        EXPECT_TRUE(in_pool(kColorlessRarePool.data(), kColorlessRarePoolCount,
                            static_cast<CardId>(shop.colorless[1].id)));
    }
}

TEST(ShopDrawOrder, PowerSlotSkipsTheEmptyCommonViewWithoutSpendingADraw) {
    // getCardFromPool(COMMON, POWER, true) finds an empty type view and returns
    // getCardFromPool(UNCOMMON, POWER, true) -- ONE cardRng draw in total,
    // because CardGroup.getRandomCard returns null before it indexes anything.
    RngStream a = from_seed(777);
    RngStream b = a;
    RewardCardRarity drawn{};
    const CardId from_common =
        shop_card_from_pool(a, RewardCardRarity::COMMON, CardType::POWER, &drawn);
    const CardId from_uncommon =
        shop_card_from_pool(b, RewardCardRarity::UNCOMMON, CardType::POWER);
    EXPECT_EQ(a.counter, 1);
    EXPECT_EQ(from_common, from_uncommon);
    EXPECT_EQ(drawn, RewardCardRarity::UNCOMMON)
        << "the card's own rarity is UNCOMMON, which is what it is priced as";
    EXPECT_TRUE(in_pool(kIroncladUncommonPowerPool.data(),
                        kIroncladUncommonPowerPoolCount, from_common));
}

TEST(ShopDrawOrder, RarityTableIsTheShopRoomTableNotTheRewardOne) {
    // ShopRoom's constructor sets baseRareCardChance 9 / baseUncommonCardChance
    // 37 and its getCardRarity override passes useAlternation=false, so no
    // relic can move these boundaries.
    EXPECT_EQ(shop_card_rarity_for_roll(0), RewardCardRarity::RARE);
    EXPECT_EQ(shop_card_rarity_for_roll(8), RewardCardRarity::RARE);
    EXPECT_EQ(shop_card_rarity_for_roll(9), RewardCardRarity::UNCOMMON);
    EXPECT_EQ(shop_card_rarity_for_roll(45), RewardCardRarity::UNCOMMON);
    EXPECT_EQ(shop_card_rarity_for_roll(46), RewardCardRarity::COMMON);
    EXPECT_EQ(shop_card_rarity_for_roll(99), RewardCardRarity::COMMON);
}

TEST(ShopDrawOrder, RelicTierTableCoversAllHundredRolls) {
    for (int32_t r = 0; r < 48; ++r) {
        EXPECT_EQ(shop_relic_tier_for_roll(r), RelicTier::COMMON) << r;
    }
    for (int32_t r = 48; r < 82; ++r) {
        EXPECT_EQ(shop_relic_tier_for_roll(r), RelicTier::UNCOMMON) << r;
    }
    for (int32_t r = 82; r < 100; ++r) {
        EXPECT_EQ(shop_relic_tier_for_roll(r), RelicTier::RARE) << r;
    }
}

TEST(ShopDrawOrder, ShopRelicDrawsSeeTheInShopCanSpawnGate) {
    // Maw Bank / Smiling Mask / The Courier / Old Coin all AND their floor gate
    // with `!(getCurrRoom() instanceof ShopRoom)`, and the merchant runs after
    // setCurrMapNode -- so a shop can never stock them, and the end-pop skips
    // past them instead.
    RunState rs = bare_run(4242);
    fill_pool(rs, RelicPool::COMMON,
              {RelicId::AKABEKO, RelicId::BLOOD_VIAL, RelicId::MAW_BANK,
               RelicId::SMILING_MASK});
    fill_pool(rs, RelicPool::UNCOMMON,
              {RelicId::SHURIKEN, RelicId::PEAR, RelicId::THE_COURIER});
    const ShopState shop = generate_shop(rs);
    for (int i = 0; i < kShopRelicCount; ++i) {
        EXPECT_NE(shop.relics[i].id, static_cast<uint16_t>(RelicId::MAW_BANK));
        EXPECT_NE(shop.relics[i].id,
                  static_cast<uint16_t>(RelicId::SMILING_MASK));
        EXPECT_NE(shop.relics[i].id, static_cast<uint16_t>(RelicId::THE_COURIER));
    }
}

// =============================================================================
// Pricing
// =============================================================================

TEST(ShopPricing, BasePriceTables) {
    EXPECT_EQ(card_base_price(RewardCardRarity::COMMON), 50);
    EXPECT_EQ(card_base_price(RewardCardRarity::UNCOMMON), 75);
    EXPECT_EQ(card_base_price(RewardCardRarity::RARE), 150);
    EXPECT_EQ(relic_base_price(RelicTier::COMMON), 150);
    EXPECT_EQ(relic_base_price(RelicTier::UNCOMMON), 250);
    EXPECT_EQ(relic_base_price(RelicTier::RARE), 300);
    EXPECT_EQ(relic_base_price(RelicTier::SHOP), 150);
    EXPECT_EQ(potion_base_price(PotionRarity::COMMON), 50);
    EXPECT_EQ(potion_base_price(PotionRarity::UNCOMMON), 75);
    EXPECT_EQ(potion_base_price(PotionRarity::RARE), 100);
}

TEST(ShopPricing, AscensionSixteenIsTheOnlyAscensionEffectAndSparesThePurge) {
    // Same seed at A15 and A20: the A16 branch is the ONLY difference, and it
    // is a round of x1.1 over the already-truncated price. The purge cost is
    // untouched because applyDiscount is called with affectPurge=false.
    RunState lo = bare_run(31337);
    lo.ascension = 15;
    RunState hi = bare_run(31337);
    hi.ascension = 20;
    const ShopState a = generate_shop(lo);
    const ShopState b = generate_shop(hi);
    for (int i = 0; i < kShopColoredCount; ++i) {
        EXPECT_EQ(a.colored[i].id, b.colored[i].id) << i;
        EXPECT_EQ(b.colored[i].price,
                  mround(static_cast<float>(a.colored[i].price) * 1.1f))
            << i;
    }
    for (int i = 0; i < kShopColorlessCount; ++i) {
        EXPECT_EQ(b.colorless[i].price,
                  mround(static_cast<float>(a.colorless[i].price) * 1.1f));
    }
    for (int i = 0; i < kShopRelicCount; ++i) {
        EXPECT_EQ(b.relics[i].price,
                  mround(static_cast<float>(a.relics[i].price) * 1.1f));
    }
    for (int i = 0; i < kShopPotionCount; ++i) {
        EXPECT_EQ(b.potions[i].price,
                  mround(static_cast<float>(a.potions[i].price) * 1.1f));
    }
    EXPECT_EQ(a.actual_purge_cost, kPurgeCostBase);
    EXPECT_EQ(b.actual_purge_cost, kPurgeCostBase);
    EXPECT_EQ(lo.merchant_rng.counter, hi.merchant_rng.counter)
        << "the discount pass consumes no RNG";
}

TEST(ShopPricing, SaleSlotIsAlwaysColoredAndHalvedBeforeAnyDiscount) {
    for (int64_t seed = 1; seed <= 60; ++seed) {
        RunState a = bare_run(seed * 7919);
        a.ascension = 0;  // no A16, so the raw truncated price is visible
        const ShopState raw = generate_shop(a);
        SCOPED_TRACE(seed);
        ASSERT_LT(raw.sale_index, kShopColoredCount);
        // The halved slot cannot exceed half of its rarity's ceiling; the other
        // four cannot go below 90% of their base's floor. Comparing the sale
        // slot against a same-rarity sibling is the direct check when one
        // exists.
        const CardDef* def =
            card_def(static_cast<CardId>(raw.colored[raw.sale_index].id));
        ASSERT_NE(def, nullptr);
        EXPECT_LE(raw.colored[raw.sale_index].price, 150 / 2 + 8);
    }
}

TEST(ShopPricing, ColorlessBumpIsInsideTheTruncation) {
    // (int)(base * jitter * 1.2f) is NOT (int)(base * jitter) * 1.2f. Deriving
    // both ways from the same stream and requiring the engine to match the
    // first is what pins the order of the two operations.
    RunState rs = bare_run(0x1234'5678LL);
    rs.ascension = 0;
    RngStream m = rs.merchant_rng;
    const ShopState shop = generate_shop(rs);
    // Skip the five colored jitters to reach the colourless ones.
    for (int i = 0; i < kShopColoredCount; ++i) {
        (void)random(m, 0.9f, 1.1f);
    }
    const float j0 = random(m, 0.9f, 1.1f);
    const int inside = static_cast<int>(75.0f * j0 * 1.2f);
    const int outside = static_cast<int>(static_cast<float>(
        static_cast<int>(75.0f * j0)) * 1.2f);
    EXPECT_EQ(shop.colorless[0].price, inside);
    if (inside != outside) {
        EXPECT_NE(shop.colorless[0].price, outside);
    }
}

// =============================================================================
// Purge: cost, ramp, persistence
// =============================================================================

TEST(ShopPurge, RampPersistsAcrossTwoShops) {
    // ShopScreen.purgeCost is a RUN-persistent field: +25 per removal, and the
    // NEXT shop opens at the ramped price.
    RunState rs = bare_run(90210);
    rs.master_deck_count = 3;
    for (int i = 0; i < 3; ++i) {
        rs.master_deck[i].card_id = static_cast<uint16_t>(kIroncladCommonPool[0]);
    }
    rs.gold = 1000;

    ShopState first = generate_shop(rs);
    EXPECT_EQ(first.actual_purge_cost, 75);
    ASSERT_TRUE(shop_purge_card(rs, first, 0));
    EXPECT_EQ(rs.gold, 1000 - 75);
    EXPECT_EQ(rs.purge_cost, 100);
    EXPECT_EQ(first.actual_purge_cost, 100);
    EXPECT_EQ(first.purge_available, 0) << "one removal per shop visit";
    EXPECT_EQ(rs.master_deck_count, 2);

    // A second merchant, later in the same run.
    ShopState second = generate_shop(rs);
    EXPECT_EQ(second.actual_purge_cost, 100);
    EXPECT_EQ(second.purge_available, 1);
    ASSERT_TRUE(shop_purge_card(rs, second, 0));
    EXPECT_EQ(rs.gold, 1000 - 75 - 100);
    EXPECT_EQ(rs.purge_cost, 125);
    EXPECT_EQ(second.actual_purge_cost, 125);
    EXPECT_EQ(rs.master_deck_count, 1);
}

TEST(ShopPurge, SmilingMaskPinsFiftyAtInitAndAfterTheRamp) {
    RunState rs = bare_run(4711);
    give_relic(rs, RelicId::SMILING_MASK);
    rs.master_deck_count = 2;
    rs.master_deck[0].card_id = static_cast<uint16_t>(kIroncladCommonPool[0]);
    rs.master_deck[1].card_id = static_cast<uint16_t>(kIroncladCommonPool[1]);
    rs.gold = 500;
    ShopState shop = generate_shop(rs);
    EXPECT_EQ(shop.actual_purge_cost, 50);
    ASSERT_TRUE(shop_purge_card(rs, shop, 0));
    EXPECT_EQ(rs.gold, 450);
    EXPECT_EQ(rs.purge_cost, 100) << "the BASE still ramps under the mask";
    EXPECT_EQ(shop.actual_purge_cost, 50);
}

TEST(ShopPurge, CourierAndMembershipDisagreeBetweenInitAndAfterAPurge) {
    // The game computes this cost in two places that do not agree, and both are
    // reproduced: at init each applyDiscount recomputes from `purgeCost`, so
    // Membership Card OVERWRITES The Courier's 0.8; purgeCard's tail spells the
    // product out. 75 -> init round(75*0.5) = 38; after the ramp to 100 ->
    // round(100*0.8*0.5) = 40.
    RunState rs = bare_run(1000003);
    give_relic(rs, RelicId::THE_COURIER);
    give_relic(rs, RelicId::MEMBERSHIP_CARD);
    rs.master_deck_count = 1;
    rs.master_deck[0].card_id = static_cast<uint16_t>(kIroncladCommonPool[0]);
    rs.gold = 500;
    EXPECT_EQ(shop_purge_cost_at_init(rs, 75), 38);
    EXPECT_EQ(shop_purge_cost_after_purge(rs, 100), 40);
    ShopState shop = generate_shop(rs);
    EXPECT_EQ(shop.actual_purge_cost, 38);
    ASSERT_TRUE(shop_purge_card(rs, shop, 0));
    EXPECT_EQ(rs.gold, 500 - 38);
    EXPECT_EQ(shop.actual_purge_cost, 40);
}

TEST(ShopPurge, CourierAndMembershipAlsoDiscountEveryPrice) {
    RunState plain = bare_run(2718281);
    RunState courier = bare_run(2718281);
    give_relic(courier, RelicId::THE_COURIER);
    RunState both = bare_run(2718281);
    give_relic(both, RelicId::THE_COURIER);
    give_relic(both, RelicId::MEMBERSHIP_CARD);

    const ShopState a = generate_shop(plain);
    const ShopState b = generate_shop(courier);
    const ShopState c = generate_shop(both);
    for (int i = 0; i < kShopRelicCount; ++i) {
        EXPECT_EQ(b.relics[i].price,
                  mround(static_cast<float>(a.relics[i].price) * 0.8f));
        EXPECT_EQ(c.relics[i].price,
                  mround(static_cast<float>(mround(
                             static_cast<float>(a.relics[i].price) * 0.8f)) *
                         0.5f))
            << "the multipliers are applied one after another, each rounded";
    }
}

TEST(ShopPurge, GridOnlyOffersPurgeableCards) {
    RunState rs = bare_run(13);
    rs.gold = 500;
    rs.master_deck_count = 2;
    rs.master_deck[0].card_id = static_cast<uint16_t>(kIroncladCommonPool[0]);
    rs.master_deck[1].card_id = static_cast<uint16_t>(CardId::ASCENDERS_BANE);
    const ShopState shop = generate_shop(rs);
    EXPECT_TRUE(shop_purge_card_legal(rs, shop, 0));
    EXPECT_FALSE(shop_purge_card_legal(rs, shop, 1))
        << "Ascender's Bane is not purgeable (CardGroup.getPurgeableCards)";
    EXPECT_FALSE(shop_purge_card_legal(rs, shop, 2));
}

TEST(ShopPurge, UnaffordableOrEmptyDeckClosesTheService) {
    RunState poor = bare_run(17);
    poor.master_deck_count = 1;
    poor.master_deck[0].card_id = static_cast<uint16_t>(kIroncladCommonPool[0]);
    poor.gold = 10;
    const ShopState a = generate_shop(poor);
    EXPECT_FALSE(shop_purge_legal(poor, a));

    RunState empty = bare_run(17);
    empty.gold = 500;
    empty.master_deck_count = 0;
    const ShopState b = generate_shop(empty);
    EXPECT_FALSE(shop_purge_legal(empty, b));
}

// =============================================================================
// Purchases + relic hooks
// =============================================================================

TEST(ShopPurchase, BuyingACardSpendsGoldAppendsTheDeckRowAndRetiresTheSlot) {
    RunState rs = bare_run(555);
    rs.gold = 999;
    const ShopState built = generate_shop(rs);
    ShopState shop = built;
    const int32_t before = rs.gold;
    const uint16_t deck_before = rs.master_deck_count;
    ASSERT_TRUE(shop_buy_card(rs, shop, 0, /*colorless=*/false));
    EXPECT_EQ(rs.gold, before - built.colored[0].price);
    EXPECT_EQ(rs.master_deck_count, deck_before + 1);
    EXPECT_EQ(rs.master_deck[deck_before].card_id, built.colored[0].id)
        << "masterDeck.addToTop is CardGroup's APPEND";
    EXPECT_EQ(shop.colored[0].sold, 1);
    EXPECT_FALSE(shop_buy_card(rs, shop, 0, /*colorless=*/false))
        << "a sold row cannot be bought twice";
}

TEST(ShopPurchase, InsufficientGoldIsAByteStableRefusal) {
    RunState rs = bare_run(556);
    ShopState shop = generate_shop(rs);
    rs.gold = 0;
    const RunState before = rs;
    const ShopState shop_before = shop;
    EXPECT_FALSE(shop_buy_card(rs, shop, 0, false));
    EXPECT_FALSE(shop_buy_relic(rs, rs.merchant_rng, shop, 0));
    EXPECT_FALSE(shop_buy_potion(rs, shop, 0));
    EXPECT_EQ(rs.gold, before.gold);
    EXPECT_EQ(rs.master_deck_count, before.master_deck_count);
    EXPECT_EQ(rs.relic_count, before.relic_count);
    EXPECT_EQ(shop.colored[0].sold, shop_before.colored[0].sold);
    EXPECT_EQ(shop.relics[0].sold, shop_before.relics[0].sold);
}

TEST(ShopPurchase, MawBankIsUsedUpByTheFirstCoinSpentInAShop) {
    // AbstractPlayer.loseGold fires every relic's onSpendGold when the current
    // room is a ShopRoom, and MawBank.onSpendGold sets its counter to -2.
    RunState rs = bare_run(606);
    give_relic(rs, RelicId::MAW_BANK);
    rs.gold = 999;
    ShopState shop = generate_shop(rs);
    ASSERT_EQ(rs.relics[0].counter, 0);
    ASSERT_TRUE(shop_buy_card(rs, shop, 0, false));
    EXPECT_EQ(rs.relics[0].counter, -2) << "used up on the first shop spend";
}

TEST(ShopPurchase, BuyingMembershipCardDiscountsTheRestOfItsOwnShop) {
    // StoreRelic.purchaseRelic obtains the relic and THEN calls
    // applyDiscount(0.5f, true), so the shop the card was bought in is itself
    // re-priced.
    RunState rs = bare_run(31415);
    rs.gold = 5000;
    fill_pool(rs, RelicPool::SHOP,
              {RelicId::TOOLBOX, RelicId::LEES_WAFFLE,
               RelicId::MEMBERSHIP_CARD});
    ShopState shop = generate_shop(rs);
    ASSERT_EQ(shop.relics[2].id,
              static_cast<uint16_t>(RelicId::MEMBERSHIP_CARD));
    const int16_t card0 = shop.colored[0].price;
    const int16_t potion0 = shop.potions[0].price;
    ASSERT_TRUE(shop_buy_relic(rs, rs.merchant_rng, shop, 2));
    EXPECT_EQ(shop.colored[0].price, mround(static_cast<float>(card0) * 0.5f));
    EXPECT_EQ(shop.potions[0].price, mround(static_cast<float>(potion0) * 0.5f));
    EXPECT_EQ(shop.actual_purge_cost,
              mround(static_cast<float>(kPurgeCostBase) * 0.5f));
    EXPECT_EQ(shop.relics[2].sold, 1);
}

TEST(ShopPurchase, BuyingSmilingMaskPinsThePurgeCostImmediately) {
    RunState rs = bare_run(2024);
    rs.gold = 5000;
    fill_pool(rs, RelicPool::COMMON,
              {RelicId::AKABEKO, RelicId::VAJRA, RelicId::SMILING_MASK});
    // Smiling Mask cannot SPAWN in a shop, so it is placed on a slot by hand:
    // the only way it reaches a shop slot in a real run is the pool having been
    // shuffled with it elsewhere -- what is under test is the purchase hook.
    ShopState shop = generate_shop(rs);
    shop.relics[0].id = static_cast<uint16_t>(RelicId::SMILING_MASK);
    shop.relics[0].price = 100;
    ASSERT_TRUE(shop_buy_relic(rs, rs.merchant_rng, shop, 0));
    EXPECT_EQ(shop.actual_purge_cost, 50);
}

TEST(ShopPurchase, PotionsNeedAFreeSlotAndAreRefusedOutrightBySozu) {
    RunState rs = bare_run(808);
    rs.gold = 999;
    rs.potion_slots = 2;
    ShopState shop = generate_shop(rs);
    ASSERT_TRUE(shop_potion_slot_available(rs));
    ASSERT_TRUE(shop_buy_potion(rs, shop, 0));
    EXPECT_EQ(rs.potions[0], shop.potions[0].id);
    EXPECT_EQ(shop.potions[0].sold, 1);
    ASSERT_TRUE(shop_buy_potion(rs, shop, 1));
    EXPECT_EQ(rs.potions[1], shop.potions[1].id);
    // Belt full: the third is refused and costs nothing.
    const int32_t gold = rs.gold;
    EXPECT_FALSE(shop_buy_potion(rs, shop, 2));
    EXPECT_EQ(rs.gold, gold);

    RunState sozu = bare_run(808);
    sozu.gold = 999;
    give_relic(sozu, RelicId::SOZU);
    ShopState s2 = generate_shop(sozu);
    EXPECT_FALSE(shop_potion_slot_available(sozu));
    EXPECT_FALSE(shop_buy_potion(sozu, s2, 0));
}

TEST(ShopPurchase, EggsPreviewUpgradeTheStockedCardsTheyMatch) {
    // The eggs override onPreviewObtainCard, which ShopScreen.initCards runs
    // over every stocked card -- so the merchant DISPLAYS them upgraded and the
    // bought instance is the upgraded one.
    RunState plain = bare_run(99991);
    RunState egg = bare_run(99991);
    give_relic(egg, RelicId::MOLTEN_EGG);
    const ShopState a = generate_shop(plain);
    ShopState b = generate_shop(egg);
    for (int i = 0; i < kShopColoredCount; ++i) {
        ASSERT_EQ(a.colored[i].id, b.colored[i].id) << i;
        const CardDef* def = card_def(static_cast<CardId>(a.colored[i].id));
        ASSERT_NE(def, nullptr);
        EXPECT_EQ(a.colored[i].upgrade, 0) << i;
        EXPECT_EQ(b.colored[i].upgrade, def->type == CardType::ATTACK ? 1 : 0)
            << i;
    }
    // And the bought card lands upgraded.
    RunState& rs = egg;
    rs.gold = 999;
    ASSERT_TRUE(shop_buy_card(rs, b, 0, false));
    EXPECT_EQ(rs.master_deck[rs.master_deck_count - 1].upgrade, 1);
}

// =============================================================================
// The Courier's restock (the deferred row's restock half)
// =============================================================================
//
// Provenance: ShopScreen.purchaseCard :598-643 (cards, incl. the unseeded
// useRng=false colored identity at :615-617 and setPrice :660-673),
// StoreRelic.purchaseRelic :105-112, StorePotion.purchasePotion :86-89,
// ShopScreen.getNewPrice :386-411. Stream expectations are replayed beside the
// engine draw for draw, exactly like the draw-order pin above. The live-game
// stream measurement backing these numbers is the wave2cap_courier_* capture
// (tools/oracle_bridge/driver/wave2cap_capture_runbook.md).

TEST(CourierRestock, ColoredPurchaseSpendsOneCardRngOneMerchantRngAndRefusesTheSlot) {
    RunState rs = bare_run(4242);
    give_relic(rs, RelicId::THE_COURIER);
    rs.gold = 9999;
    ShopState shop = generate_shop(rs);

    // Replay streams beside the purchase: the restock is ONE cardRng draw
    // (rollRarity through the ShopRoom 9/37 table, blizz READ not written)
    // and ONE merchantRng draw (setPrice's 0.9-1.1 jitter). The identity draw
    // is MathUtils.random and costs the seeded streams NOTHING.
    RngStream card_replay = rs.card_rng;
    RngStream merchant_replay = rs.merchant_rng;
    const RngStream potion_before = rs.potion_rng;
    const auto blizz_before = rs.card_blizz_randomizer;

    ASSERT_TRUE(shop_buy_card(rs, shop, 0, /*colorless=*/false));

    const int roll = static_cast<int>(random(card_replay, 99)) +
                     static_cast<int>(blizz_before);
    const RewardCardRarity rolled = shop_card_rarity_for_roll(roll);
    // Slot 0 is an ATTACK slot and every RED ATTACK view is non-empty, so the
    // drawn pool is the rolled pool.
    const float jitter = random(merchant_replay, 0.9f, 1.1f);
    const auto expected_price = static_cast<int16_t>(static_cast<int>(
        static_cast<float>(card_base_price(rolled)) * jitter * 0.8f));

    EXPECT_TRUE(streams_equal(rs.card_rng, card_replay))
        << "the colored restock is exactly ONE cardRng draw";
    EXPECT_TRUE(streams_equal(rs.merchant_rng, merchant_replay))
        << "the colored restock is exactly ONE merchantRng draw";
    EXPECT_TRUE(streams_equal(rs.potion_rng, potion_before));
    EXPECT_EQ(rs.card_blizz_randomizer, blizz_before)
        << "rollRarity in a ShopRoom READS the blizz counter, never writes it";

    EXPECT_EQ(shop.colored[0].id, kShopRestockedUnknownCard);
    EXPECT_EQ(shop.colored[0].sold, 0) << "the slot exists; it is not sold";
    EXPECT_EQ(shop.colored[0].price, expected_price)
        << "setPrice: one float product (base x jitter x 0.8 Courier), one "
           "truncation -- NO A16 x1.1 and no sale halving";

    // The named deviation's guard: off the mask, and a byte-stable refusal.
    EXPECT_FALSE(shop_buy_card_legal(rs, shop, 0, false));
    const RunState rs_before = rs;
    const ShopState shop_before = shop;
    EXPECT_FALSE(shop_buy_card(rs, shop, 0, false));
    EXPECT_EQ(std::memcmp(&rs, &rs_before, sizeof rs), 0);
    EXPECT_EQ(std::memcmp(&shop, &shop_before, sizeof shop), 0);
}

TEST(CourierRestock, ColorlessRestockIsFullySeededAndPurchasable) {
    RunState rs = bare_run(4343);
    give_relic(rs, RelicId::THE_COURIER);
    rs.gold = 9999;
    ShopState shop = generate_shop(rs);

    RngStream card_replay = rs.card_rng;
    RngStream merchant_replay = rs.merchant_rng;

    ASSERT_TRUE(shop_buy_card(rs, shop, 0, /*colorless=*/true));

    // purchaseCard's colorless branch (:600-613): merchantRng.random() against
    // colorlessRareChance, ONE cardRng draw on the rarity view, then setPrice
    // (a SECOND merchantRng draw) with the 1.2f colourless bump inside the
    // truncation.
    const RewardCardRarity rarity =
        random(merchant_replay) < kColorlessRareChance
            ? RewardCardRarity::RARE
            : RewardCardRarity::UNCOMMON;
    const CardId expected_id =
        draw_colorless_card_from_pool(card_replay, rarity);
    const float jitter = random(merchant_replay, 0.9f, 1.1f);
    const auto expected_price = static_cast<int16_t>(
        static_cast<int>(static_cast<float>(card_base_price(rarity)) * jitter *
                         1.2f * 0.8f));

    EXPECT_TRUE(streams_equal(rs.card_rng, card_replay));
    EXPECT_TRUE(streams_equal(rs.merchant_rng, merchant_replay));
    EXPECT_EQ(shop.colorless[0].id, static_cast<uint16_t>(expected_id))
        << "the colourless replacement is SEEDED (cardRng), unlike the "
           "colored one";
    EXPECT_EQ(shop.colorless[0].sold, 0);
    EXPECT_EQ(shop.colorless[0].price, expected_price);

    // A seeded restock is purchasable -- and buying it restocks AGAIN.
    ASSERT_TRUE(shop_buy_card_legal(rs, shop, 0, true));
    const uint16_t deck_before = rs.master_deck_count;
    ASSERT_TRUE(shop_buy_card(rs, shop, 0, true));
    EXPECT_EQ(rs.master_deck_count, deck_before + 1);
    EXPECT_EQ(rs.master_deck[deck_before].card_id,
              static_cast<uint16_t>(expected_id));
    EXPECT_EQ(shop.colorless[0].sold, 0) << "restocked a second time";
}

TEST(CourierRestock, RelicRestockRollsOneTierThenEndPopsAndReprices) {
    RunState rs = bare_run(4444);
    give_relic(rs, RelicId::THE_COURIER);
    rs.gold = 9999;
    ShopState shop = generate_shop(rs);

    // Snapshot the pools AFTER generation so the restock's end-pop is
    // unambiguous against them.
    uint16_t pool_ends[4];
    uint8_t pool_counts[4];
    for (int t = 0; t < 4; ++t) {
        pool_counts[t] = rs.relic_pool_count[t];
        pool_ends[t] = pool_counts[t] > 0
                           ? rs.relic_pools[t][pool_counts[t] - 1]
                           : 0;
    }

    RngStream merchant_replay = rs.merchant_rng;
    const RngStream card_before = rs.card_rng;
    const RngStream potion_before = rs.potion_rng;
    const uint16_t old_id = shop.relics[0].id;

    RngStream misc = from_seed(4444);
    ASSERT_TRUE(shop_buy_relic(rs, misc, shop, 0));

    // StoreRelic.purchaseRelic :105-112: ONE rollRelicTier
    // (merchantRng.random(99)) -> END-pop of that tier (no further draw; the
    // instanceof loop is dead in a ShopRoom, see shop.hpp) -> getNewPrice
    // (a SECOND merchantRng draw, then a separate Courier round).
    const RelicTier tier =
        shop_relic_tier_for_roll(random(merchant_replay, 99));
    const auto tier_idx = static_cast<int>(
        tier == RelicTier::COMMON
            ? RelicPool::COMMON
            : (tier == RelicTier::UNCOMMON ? RelicPool::UNCOMMON
                                           : RelicPool::RARE));
    const uint16_t expected_id = pool_ends[tier_idx];
    const RelicDef* def = relic_def(static_cast<RelicId>(expected_id));
    ASSERT_NE(def, nullptr);
    int expected_price = mround(static_cast<float>(relic_base_price(def->tier)) *
                                random(merchant_replay, 0.95f, 1.05f));
    expected_price = mround(static_cast<float>(expected_price) * 0.8f);

    EXPECT_TRUE(streams_equal(rs.merchant_rng, merchant_replay))
        << "relic restock = tier roll + price jitter, TWO merchantRng draws";
    EXPECT_TRUE(streams_equal(rs.card_rng, card_before));
    EXPECT_TRUE(streams_equal(rs.potion_rng, potion_before));

    EXPECT_NE(shop.relics[0].id, old_id);
    EXPECT_EQ(shop.relics[0].id, expected_id)
        << "returnRandomRelicEnd is an END-pop of the rolled tier";
    EXPECT_EQ(shop.relics[0].sold, 0) << "the shelf never empties";
    EXPECT_EQ(shop.relics[0].price, expected_price)
        << "getNewPrice: round(base x 0.95-1.05), then a separate Courier "
           "round -- no A16 pass";
    EXPECT_EQ(rs.relic_pool_count[tier_idx], pool_counts[tier_idx] - 1)
        << "the restock consumed the pool end";
}

TEST(CourierRestock, PotionRestockDrawsSeededTiersAndRestocksPurchasable) {
    RunState rs = bare_run(4545);
    give_relic(rs, RelicId::THE_COURIER);
    rs.gold = 9999;
    rs.potion_slots = 3;
    ShopState shop = generate_shop(rs);

    RngStream potion_replay = rs.potion_rng;
    RngStream merchant_replay = rs.merchant_rng;
    const RngStream card_before = rs.card_rng;

    ASSERT_TRUE(shop_buy_potion(rs, shop, 0));

    // StorePotion.purchasePotion :86-89: returnRandomPotion() on potionRng
    // (one tier roll + trap-14 rejection sampling -- a VARIABLE draw count the
    // replay reproduces exactly), then getNewPrice's ONE merchantRng draw.
    const PotionId expected_id = return_random_potion(potion_replay);
    const PotionDef* def = potion_def(expected_id);
    ASSERT_NE(def, nullptr);
    int expected_price =
        mround(static_cast<float>(potion_base_price(def->rarity)) *
               random(merchant_replay, 0.95f, 1.05f));
    expected_price = mround(static_cast<float>(expected_price) * 0.8f);

    EXPECT_TRUE(streams_equal(rs.potion_rng, potion_replay));
    EXPECT_TRUE(streams_equal(rs.merchant_rng, merchant_replay));
    EXPECT_TRUE(streams_equal(rs.card_rng, card_before));
    EXPECT_EQ(shop.potions[0].id, static_cast<uint16_t>(expected_id));
    EXPECT_EQ(shop.potions[0].sold, 0);
    EXPECT_EQ(shop.potions[0].price, expected_price);
    EXPECT_TRUE(shop_buy_potion_legal(rs, shop, 0))
        << "a seeded restock is purchasable";
}

TEST(CourierRestock, WithoutTheCourierEverySlotRetiresExactlyAsBefore) {
    // The regression guard for every purchase above: no Courier, no restock,
    // and -- the stream half -- no post-purchase draw on ANY stream.
    RunState rs = bare_run(4646);
    rs.gold = 9999;
    rs.potion_slots = 3;
    ShopState shop = generate_shop(rs);
    const RngStream card_before = rs.card_rng;
    const RngStream merchant_before = rs.merchant_rng;
    const RngStream potion_before = rs.potion_rng;
    RngStream misc = from_seed(4646);

    ASSERT_TRUE(shop_buy_card(rs, shop, 0, false));
    ASSERT_TRUE(shop_buy_card(rs, shop, 0, true));
    ASSERT_TRUE(shop_buy_relic(rs, misc, shop, 0));
    ASSERT_TRUE(shop_buy_potion(rs, shop, 0));

    EXPECT_TRUE(streams_equal(rs.card_rng, card_before));
    EXPECT_TRUE(streams_equal(rs.merchant_rng, merchant_before));
    EXPECT_TRUE(streams_equal(rs.potion_rng, potion_before));
    EXPECT_EQ(shop.colored[0].sold, 1);
    EXPECT_EQ(shop.colorless[0].sold, 1);
    EXPECT_EQ(shop.relics[0].sold, 1);
    EXPECT_EQ(shop.potions[0].sold, 1);
}

TEST(CourierRestock, MembershipBoughtUnderTheCourierRestocksItsOwnSlotAtBothDiscounts) {
    // Order inside purchaseRelic: obtain -> Membership applyDiscount(0.5f)
    // over the SHELF -> the Courier restock, whose getNewPrice then sees BOTH
    // discount relics owned (0.8f round, then 0.5f round).
    RunState rs = bare_run(4747);
    give_relic(rs, RelicId::THE_COURIER);
    rs.gold = 9999;
    fill_pool(rs, RelicPool::SHOP,
              {RelicId::TOOLBOX, RelicId::LEES_WAFFLE,
               RelicId::MEMBERSHIP_CARD});
    ShopState shop = generate_shop(rs);
    ASSERT_EQ(shop.relics[2].id,
              static_cast<uint16_t>(RelicId::MEMBERSHIP_CARD));
    const int16_t card0 = shop.colored[0].price;

    RngStream merchant_replay = rs.merchant_rng;
    RngStream misc = from_seed(4747);
    ASSERT_TRUE(shop_buy_relic(rs, misc, shop, 2));

    // The shelf pass ran first (0.5 on the already-Courier-discounted shelf)...
    EXPECT_EQ(shop.colored[0].price, mround(static_cast<float>(card0) * 0.5f));
    // ...then the restock: tier roll, end-pop, and getNewPrice with both
    // rounds applied separately.
    const RelicTier tier =
        shop_relic_tier_for_roll(random(merchant_replay, 99));
    (void)tier;  // the end-pop identity is asserted through the actual slot
    const RelicDef* def = relic_def(static_cast<RelicId>(shop.relics[2].id));
    ASSERT_NE(def, nullptr);
    int expected_price = mround(static_cast<float>(relic_base_price(def->tier)) *
                                random(merchant_replay, 0.95f, 1.05f));
    expected_price = mround(static_cast<float>(expected_price) * 0.8f);
    expected_price = mround(static_cast<float>(expected_price) * 0.5f);
    EXPECT_EQ(shop.relics[2].sold, 0) << "restocked, not retired";
    EXPECT_NE(shop.relics[2].id,
              static_cast<uint16_t>(RelicId::MEMBERSHIP_CARD));
    EXPECT_EQ(shop.relics[2].price, expected_price);
}

TEST(CourierRestock, AnEggBoughtMidShopPreviewsTheRemainingShelf) {
    // StoreRelic.purchaseRelic :98-103: the JUST-BOUGHT relic previews every
    // stocked card. Placed on a slot by hand exactly like the Smiling Mask
    // test above: what is under test is the purchase hook, not the spawn.
    RunState rs = bare_run(4848);
    rs.gold = 9999;
    ShopState shop = generate_shop(rs);
    shop.relics[0].id = static_cast<uint16_t>(RelicId::MOLTEN_EGG);
    shop.relics[0].price = 100;
    RngStream misc = from_seed(4848);
    ASSERT_TRUE(shop_buy_relic(rs, misc, shop, 0));
    for (int i = 0; i < kShopColoredCount; ++i) {
        const CardDef* def = card_def(static_cast<CardId>(shop.colored[i].id));
        ASSERT_NE(def, nullptr) << i;
        EXPECT_EQ(shop.colored[i].upgrade,
                  def->type == CardType::ATTACK ? 1 : 0)
            << i;
    }
}

// =============================================================================
// Meal Ticket / room entry
// =============================================================================

TEST(ShopEntry, MealTicketHealsFifteenOnEnteringAShopAndNowhereElse) {
    RunState rs{};
    rs.hp = 40;
    rs.max_hp = 75;
    give_relic(rs, RelicId::MEAL_TICKET);
    dispatch_just_entered_room_relics(rs, /*in_shop=*/false);
    EXPECT_EQ(rs.hp, 40);
    dispatch_just_entered_room_relics(rs, /*in_shop=*/true);
    EXPECT_EQ(rs.hp, 55);
    // Clamped to max HP, never over.
    rs.hp = 70;
    dispatch_just_entered_room_relics(rs, /*in_shop=*/true);
    EXPECT_EQ(rs.hp, 75);
}

// =============================================================================
// The CHOOSE flow through the run layer
// =============================================================================

namespace {

void step(RunController& rc, Action action) {
    Action a[1] = {action};
    StepResult r[1];
    advance(std::span<RunController>(&rc, 1), std::span<const Action>(a, 1),
            std::span<StepResult>(r, 1));
}

// Skip the blessing without spending its payout screens: park Neow on its
// final dialog button and press it, which is the press that opens the map.
void leave_neow(RunController& rc) {
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::DONE);
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
}

// Walk from the map into the row-0 room, whichever start column is connected.
void take_first_start(RunController& rc) {
    RunActionMask m{};
    legal_actions(rc, m);
    for (uint8_t x = 0; x < kMapCols; ++x) {
        if (!m.can_choose_node[x]) continue;
        step(rc, make_action(ActionVerb::CHOOSE, x));
        return;
    }
}

// Drive a controller into a shop by forcing every row-0 node to a Shop.
[[nodiscard]] RunController enter_shop(int64_t seed) {
    RunController rc = run_begin(seed, kA20);
    for (int x = 0; x < kMapCols; ++x) {
        rc.run.map[run_state_map_index(x, 0)].room_type =
            static_cast<uint8_t>(RoomType::Shop);
    }
    leave_neow(rc);
    take_first_start(rc);
    return rc;
}

}  // namespace

TEST(ShopFlow, EnteringAShopOpensTheMenuAndOffersEveryAffordableRow) {
    RunController rc = enter_shop(0xB48'5407LL);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::SHOP));
    rc.run.gold = 5000;
    RunActionMask m{};
    legal_actions(rc, m);
    EXPECT_TRUE(m.can_proceed);
    for (int i = 0; i < kShopColoredCount; ++i) {
        EXPECT_TRUE(m.can_buy_shop_item[kChooseShopColoredBase + i]) << i;
    }
    for (int i = 0; i < kShopColorlessCount; ++i) {
        EXPECT_TRUE(m.can_buy_shop_item[kChooseShopColorlessBase + i]) << i;
    }
    for (int i = 0; i < kShopRelicCount; ++i) {
        EXPECT_TRUE(m.can_buy_shop_item[kChooseShopRelicBase + i]) << i;
    }
    for (int i = 0; i < kShopPotionCount; ++i) {
        EXPECT_TRUE(m.can_buy_shop_item[kChooseShopPotionBase + i]) << i;
    }
    EXPECT_TRUE(m.can_purge);
}

TEST(ShopFlow, BuyThroughChooseThenProceedToTheMap) {
    RunController rc = enter_shop(0xB48'5407LL);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::SHOP));
    rc.run.gold = 5000;
    const uint16_t want_card = rc.shop.colored[2].id;
    const uint16_t want_relic = rc.shop.relics[1].id;
    const uint16_t want_potion = rc.shop.potions[0].id;
    const uint8_t relics_before = rc.run.relic_count;

    step(rc, make_action(ActionVerb::CHOOSE,
                         static_cast<uint8_t>(kChooseShopColoredBase + 2)));
    EXPECT_EQ(rc.run.master_deck[rc.run.master_deck_count - 1].card_id,
              want_card);
    step(rc, make_action(ActionVerb::CHOOSE,
                         static_cast<uint8_t>(kChooseShopRelicBase + 1)));
    EXPECT_EQ(rc.run.relic_count, relics_before + 1);
    EXPECT_EQ(rc.run.relics[relics_before].relic_id, want_relic);
    step(rc, make_action(ActionVerb::CHOOSE, kChooseShopPotionBase));
    EXPECT_EQ(rc.run.potions[0], want_potion);

    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(rc.shop.screen, static_cast<uint8_t>(ShopScreenKind::NONE))
        << "the merchant does not survive leaving the room";
}

TEST(ShopFlow, PurgeOpensAModalGridAndConfirmingRemovesExactlyOneCard) {
    RunController rc = enter_shop(0xB48'5407LL);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::SHOP));
    rc.run.gold = 5000;
    const uint16_t deck_before = rc.run.master_deck_count;
    ASSERT_GT(deck_before, 1);

    step(rc, make_action(ActionVerb::CHOOSE, kChooseShopPurge));
    EXPECT_EQ(rc.shop.screen, static_cast<uint8_t>(ShopScreenKind::PURGE_GRID));
    RunActionMask m{};
    legal_actions(rc, m);
    EXPECT_FALSE(m.can_proceed) << "the grid is modal over the shop floor";
    EXPECT_FALSE(m.can_purge);
    for (int i = 0; i < kShopItemCount; ++i) {
        EXPECT_FALSE(m.can_buy_shop_item[i]) << i;
    }
    uint16_t pick = kMasterDeckCap;
    for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
        if (m.can_choose_master_deck[i]) {
            pick = i;
            break;
        }
    }
    ASSERT_LT(pick, kMasterDeckCap);
    const uint16_t removed = rc.run.master_deck[pick].card_id;
    step(rc, make_action(ActionVerb::CHOOSE, static_cast<uint8_t>(pick)));
    EXPECT_EQ(rc.run.master_deck_count, deck_before - 1);
    EXPECT_EQ(rc.shop.screen, static_cast<uint8_t>(ShopScreenKind::MENU));
    EXPECT_EQ(rc.run.purge_cost, kPurgeCostBase + kPurgeCostRamp);
    legal_actions(rc, m);
    EXPECT_FALSE(m.can_purge) << "one removal per visit";
    EXPECT_TRUE(m.can_proceed);
    (void)removed;
}

TEST(ShopPurge, TheGridAndTheServiceGateExcludeBottledCards) {
    // ShopScreen.java:973: the purge grid opens
    // getGroupWithoutBottledCards(getPurgeableCards()), so a bottled card is
    // not a purge row -- and a deck whose ONLY purgeable cards are bottled
    // has no live purge service at all (shop_purge_legal's card scan).
    RunState rs = bare_run(707);
    rs.gold = 999;
    rs.master_deck_count = 2;
    rs.master_deck[0].card_id = static_cast<uint16_t>(CardId::CLEAVE);
    rs.master_deck[0].flags = kMasterCardInBottleFlame;
    rs.master_deck[1].card_id = static_cast<uint16_t>(CardId::ARMAMENTS);
    ShopState shop = generate_shop(rs);

    ASSERT_TRUE(shop_purge_legal(rs, shop));
    EXPECT_FALSE(shop_purge_card_legal(rs, shop, 0))
        << "the bottled card is not on the purge grid";
    EXPECT_TRUE(shop_purge_card_legal(rs, shop, 1));

    // Bottle the other card too: every purgeable card is now bottled and the
    // service itself goes dark.
    rs.master_deck[1].flags = kMasterCardInBottleLightning;
    EXPECT_FALSE(shop_purge_legal(rs, shop));
}

TEST(ShopPurchase, PlainBuyOfABottleIsRefusedWholeWithNoGoldSpent) {
    // The context-less shop_buy_relic overload cannot present the bottle grid,
    // so it must refuse BEFORE the gold leaves and the slot is marked sold --
    // never a paid-for relic silently not granted (contract in shop.hpp).
    RunState rs = bare_run(606);
    rs.gold = 999;
    rs.master_deck_count = 1;
    rs.master_deck[0].card_id = static_cast<uint16_t>(CardId::CLEAVE);
    ShopState shop = generate_shop(rs);
    shop.relics[0].id = static_cast<uint16_t>(RelicId::BOTTLED_FLAME);
    shop.relics[0].price = 100;
    EXPECT_FALSE(shop_buy_relic(rs, rs.merchant_rng, shop, 0));
    EXPECT_EQ(rs.gold, 999);
    EXPECT_EQ(shop.relics[0].sold, 0);
    EXPECT_EQ(rs.relic_count, 0);
}

TEST(ShopFlow, BuyingABottleOpensTheOverlayOverTheShopFloor) {
    // StoreRelic.purchaseRelic -> instantObtain -> onEquip: the bottle grid
    // opens over the merchant (room INCOMPLETE) and the pick returns to the
    // shop menu. The tier-rolled stock CAN roll a bottle for real; the slot is
    // placed by hand here so the test does not depend on a stock-roll seed.
    RunController rc = enter_shop(0xB48'5407LL);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::SHOP));
    rc.run.gold = 5000;
    rc.shop.relics[1].id = static_cast<uint16_t>(RelicId::BOTTLED_LIGHTNING);
    rc.shop.relics[1].price = 200;
    const uint8_t relics_before = rc.run.relic_count;
    const int32_t gold_before = rc.run.gold;

    // The A20 deck holds Defends (SKILLs), so Lightning's grid is non-empty.
    step(rc, make_action(ActionVerb::CHOOSE,
                         static_cast<uint8_t>(kChooseShopRelicBase + 1)));
    ASSERT_EQ(rc.run.relic_count, relics_before + 1);
    EXPECT_EQ(rc.run.relics[relics_before].relic_id,
              static_cast<uint16_t>(RelicId::BOTTLED_LIGHTNING));
    EXPECT_EQ(rc.run.gold, gold_before - 200);
    EXPECT_EQ(rc.shop.relics[1].sold, 1);
    ASSERT_EQ(rc.pending_bottle,
              static_cast<uint8_t>(MasterBottleKind::LIGHTNING));

    RunActionMask m{};
    legal_actions(rc, m);
    EXPECT_FALSE(m.can_proceed) << "the bottle grid is modal over the shop";
    EXPECT_FALSE(m.can_purge);
    for (int i = 0; i < kShopItemCount; ++i) {
        EXPECT_FALSE(m.can_buy_shop_item[i]) << i;
    }
    uint16_t pick = kMasterDeckCap;
    for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
        if (m.can_choose_master_deck[i]) {
            pick = i;
            break;
        }
    }
    ASSERT_LT(pick, kMasterDeckCap);
    step(rc, make_action(ActionVerb::CHOOSE, static_cast<uint8_t>(pick)));
    EXPECT_EQ(rc.run.master_deck[pick].flags, kMasterCardInBottleLightning);
    EXPECT_EQ(rc.pending_bottle,
              static_cast<uint8_t>(MasterBottleKind::NONE));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::SHOP));
    legal_actions(rc, m);
    EXPECT_TRUE(m.can_proceed) << "back on the shop menu after the pick";
}

// --- the equip-screen trio (Orrery / Dolly's Mirror / Cauldron) --------------
//
// The three SHOP relics whose onEquip opens a screen at the purchase site. Two
// of them roll REWARD CARDS while the player is standing in the merchant's
// room, which is why the rarity table below is the shop's and not the base one.

TEST(ShopPurchase, PlainBuyOfAnEquipScreenShopRelicIsRefusedWholeWithNoGoldSpent) {
    // Same contract as the bottle above (shop.hpp): the context-less overload
    // cannot present a reward screen or a deck grid, so it must refuse BEFORE
    // the gold leaves and the slot is marked sold.
    for (const RelicId id : {RelicId::ORRERY, RelicId::DOLLYS_MIRROR,
                             RelicId::CAULDRON}) {
        RunState rs = bare_run(606);
        rs.gold = 999;
        rs.master_deck_count = 1;
        rs.master_deck[0].card_id = static_cast<uint16_t>(CardId::CLEAVE);
        ShopState shop = generate_shop(rs);
        shop.relics[0].id = static_cast<uint16_t>(id);
        shop.relics[0].price = 100;
        const RunState before = rs;
        EXPECT_FALSE(shop_buy_relic(rs, rs.merchant_rng, shop, 0))
            << static_cast<int>(id);
        EXPECT_EQ(std::memcmp(&rs, &before, sizeof rs), 0)
            << "a refused purchase is byte-stable";
        EXPECT_EQ(shop.relics[0].sold, 0);
    }
}

TEST(ShopFlow, CauldronBrewsFiveFlatPotionsAndBurnsTheDeletedCardRow) {
    // Cauldron.onEquip (Cauldron.java:30-45): five PotionHelper.getRandomPotion()
    // brews (the FLAT potionRng draw, one each -- PotionHelper.java:169-172),
    // then combatRewardScreen.open, whose setupItemReward appends a full
    // getRewardCards() row that :36-44 immediately deletes again. The cards are
    // never shown; the cardRng draws and the blizz step are still spent, and
    // skipping them desyncs the run (measured live, seed STS430130).
    RunController rc = enter_shop(0xB48'5407LL);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::SHOP));
    rc.run.gold = 5000;
    rc.run.potion_slots = 3;
    rc.shop.relics[0].id = static_cast<uint16_t>(RelicId::CAULDRON);
    rc.shop.relics[0].price = 300;

    // The mirror: the same two stream consumptions, in the Java's order, run
    // beside the engine off a copy of the pre-purchase run. The scratch run
    // does not own Cauldron, which is exact here because Cauldron overrides
    // neither changeNumberOfCardsInReward nor any rarity hook.
    RunState scratch = rc.run;
    PotionId expected_brew[5];
    for (int i = 0; i < 5; ++i) {
        expected_brew[i] = get_random_potion(scratch.potion_rng);
    }
    RewardScreen scratch_screen{};
    scratch_screen.open_card_item = kNoOpenCardReward;
    const int32_t card_counter_before = scratch.card_rng.counter;
    roll_setup_item_card_reward(scratch, RoomType::Shop, scratch_screen);
    EXPECT_GE(scratch.card_rng.counter - card_counter_before, 9)
        << "3 rollRarity + 3 pool picks + 3 upgrade randomBooleans, the last "
           "taken even at Act 1's 0.0f chance";

    step(rc, make_action(ActionVerb::CHOOSE,
                         static_cast<uint8_t>(kChooseShopRelicBase + 0)));

    EXPECT_TRUE(streams_equal(rc.run.potion_rng, scratch.potion_rng))
        << "exactly five flat potionRng draws";
    EXPECT_TRUE(streams_equal(rc.run.card_rng, scratch.card_rng))
        << "the deleted card row's draws are spent all the same";
    EXPECT_EQ(rc.run.card_blizz_randomizer, scratch.card_blizz_randomizer)
        << "and its pity bookkeeping lands too";

    // The screen: five potions, in brew order, and NO card row.
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    ASSERT_EQ(rc.rewards.count, 5);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(rc.rewards.items[i].kind,
                  static_cast<uint8_t>(RewardItemKind::POTION)) << i;
        EXPECT_EQ(rc.rewards.items[i].id,
                  static_cast<uint16_t>(expected_brew[i])) << i;
    }
    for (uint8_t i = 0; i < rc.rewards.count; ++i) {
        EXPECT_NE(rc.rewards.items[i].kind,
                  static_cast<uint8_t>(RewardItemKind::CARDS))
            << "the hidden row is deleted from the VISIBLE rewards (:36-44)";
    }

    // The merchant is gone: ProceedButton's COMBAT_REWARD arm for a non-boss
    // non-event room opens the MAP (ProceedButton.java:122-158), never the shop.
    RunActionMask m{};
    legal_actions(rc, m);
    EXPECT_TRUE(m.can_proceed);
    for (int i = 0; i < kShopItemCount; ++i) {
        EXPECT_FALSE(m.can_buy_shop_item[i]) << i;
    }
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(ShopFlow, CauldronsHiddenRowRollsTheMerchantRoomsRarityTable) {
    // rollRarity asks getCurrRoom().getCardRarity(roll) (AbstractDungeon.java:
    // 1597-1603), and ShopRoom overrides BOTH halves: baseRareCardChance = 9
    // (ShopRoom.java:35-36) and getCardRarity(roll) -> getCardRarity(roll,
    // false) (:52-55), i.e. no alterCardRarityProbabilities pass. So a shop
    // reward roll is 9/37 AND N'loth's Gift cannot move it -- unlike every
    // combat reward, where the relic triples the rare chance.
    EXPECT_EQ(reward_card_rarity(8, RoomType::Shop), RewardCardRarity::RARE);
    EXPECT_EQ(reward_card_rarity(9, RoomType::Shop),
              RewardCardRarity::UNCOMMON);
    EXPECT_EQ(reward_card_rarity(45, RoomType::Shop),
              RewardCardRarity::UNCOMMON);
    EXPECT_EQ(reward_card_rarity(46, RoomType::Shop),
              RewardCardRarity::COMMON);
    // The base table for the same rolls, so the divergence is on the record.
    EXPECT_EQ(reward_card_rarity(8, RoomType::Monster),
              RewardCardRarity::UNCOMMON);

    RunState rs = bare_run(909);
    give_relic(rs, RelicId::NLOTHS_GIFT);
    EXPECT_EQ(reward_card_rarity_with_relics(rs, 8, RoomType::Shop),
              RewardCardRarity::RARE)
        << "still the shop's own 9, not 9 x 3";
    EXPECT_EQ(reward_card_rarity_with_relics(rs, 26, RoomType::Shop),
              RewardCardRarity::UNCOMMON)
        << "useAlternation=false: the gift is inert in the merchant's room";
    EXPECT_EQ(reward_card_rarity_with_relics(rs, 8, RoomType::Monster),
              RewardCardRarity::RARE)
        << "the same roll IS rare in a monster room, through the x3 pass";
}

TEST(ShopFlow, OrreryOffersFiveCardRowsNotFour) {
    // Orrery.onEquip (Orrery.java:27-33) is FOUR addCardToRewards() calls, and
    // combatRewardScreen.open -> setupItemReward appends a FIFTH unconditional
    // card row (CombatRewardScreen.java:72-96) that nothing here deletes -- a
    // ShopRoom is neither a TreasureRoom nor a RestRoom and carries no event.
    RunController rc = enter_shop(0xB48'5407LL);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::SHOP));
    rc.run.gold = 5000;
    rc.shop.relics[2].id = static_cast<uint16_t>(RelicId::ORRERY);
    rc.shop.relics[2].price = 250;

    RunState scratch = rc.run;
    RewardScreen scratch_screen{};
    scratch_screen.open_card_item = kNoOpenCardReward;
    for (int i = 0; i < 5; ++i) {
        roll_setup_item_card_reward(scratch, RoomType::Shop, scratch_screen);
    }

    const RngStream potion_before = rc.run.potion_rng;
    step(rc, make_action(ActionVerb::CHOOSE,
                         static_cast<uint8_t>(kChooseShopRelicBase + 2)));

    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    ASSERT_EQ(rc.rewards.count, 5)
        << "four addCardToRewards plus setupItemReward's own row";
    for (uint8_t i = 0; i < rc.rewards.count; ++i) {
        EXPECT_EQ(rc.rewards.items[i].kind,
                  static_cast<uint8_t>(RewardItemKind::CARDS)) << i;
        EXPECT_EQ(rc.rewards.items[i].card_ids[0],
                  scratch_screen.items[i].card_ids[0]) << i;
        EXPECT_EQ(rc.rewards.items[i].card_count,
                  scratch_screen.items[i].card_count) << i;
    }
    EXPECT_TRUE(streams_equal(rc.run.card_rng, scratch.card_rng))
        << "five getRewardCards rolls, no more and no fewer";
    EXPECT_TRUE(streams_equal(rc.run.potion_rng, potion_before))
        << "Orrery draws no potions";

    // The offer is claimable: opening row 0's pick screen is legal.
    RunActionMask m{};
    legal_actions(rc, m);
    EXPECT_TRUE(m.can_claim_reward[4])
        << "the fifth row is a real, claimable offer";
}

TEST(ShopFlow, DollysMirrorGridIsUnfilteredAndDuplicatesThePick) {
    // DollysMirror.onEquip (DollysMirror.java:33-42) hands gridSelectScreen
    // `player.masterDeck` ITSELF -- no getPurgeableCards, no bottled exclusion
    // -- with numCards 1 and canCancel FALSE. update (:45-58) then obtains a
    // makeStatEquivalentCopy of the pick with the three bottle flags cleared on
    // the COPY. Zero RNG on every path.
    RunController rc = enter_shop(0xB48'5407LL);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::SHOP));
    rc.run.gold = 5000;

    // A deck whose rows the OTHER grids would all reject in some way: an
    // unpurgeable curse, a bottled attack, and an upgraded Searing Blow
    // carrying a misc value.
    rc.run.master_deck_count = 3;
    rc.run.master_deck[0] = CardInstance{};
    rc.run.master_deck[0].card_id =
        static_cast<uint16_t>(CardId::ASCENDERS_BANE);
    rc.run.master_deck[1] = CardInstance{};
    rc.run.master_deck[1].card_id = static_cast<uint16_t>(CardId::CLEAVE);
    rc.run.master_deck[1].flags = kMasterCardInBottleFlame;
    rc.run.master_deck[2] = CardInstance{};
    rc.run.master_deck[2].card_id =
        static_cast<uint16_t>(CardId::SEARING_BLOW);
    rc.run.master_deck[2].upgrade = 2;
    rc.run.master_deck[2].misc = 7;

    rc.shop.relics[1].id = static_cast<uint16_t>(RelicId::DOLLYS_MIRROR);
    rc.shop.relics[1].price = 250;
    const RunState streams_before = rc.run;
    const RngStream card_random_before = rc.combat.card_random_rng;
    const RngStream misc_before = rc.combat.misc_rng;

    step(rc, make_action(ActionVerb::CHOOSE,
                         static_cast<uint8_t>(kChooseShopRelicBase + 1)));
    ASSERT_EQ(rc.pending_deck_pick,
              static_cast<uint8_t>(EquipDeckPick::DOLLYS_MIRROR));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::SHOP))
        << "the grid is an overlay; previousScreen brings the player back";
    EXPECT_TRUE(streams_equal(rc.run.card_rng, streams_before.card_rng));
    EXPECT_TRUE(streams_equal(rc.run.potion_rng, streams_before.potion_rng));
    EXPECT_TRUE(streams_equal(rc.combat.misc_rng, misc_before));
    EXPECT_TRUE(
        streams_equal(rc.combat.card_random_rng, card_random_before));

    RunActionMask m{};
    legal_actions(rc, m);
    EXPECT_FALSE(m.can_proceed) << "the grid is modal over the shop";
    for (int i = 0; i < kShopItemCount; ++i) {
        EXPECT_FALSE(m.can_buy_shop_item[i]) << i;
    }
    for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
        EXPECT_TRUE(m.can_choose_master_deck[i])
            << "row " << i << " -- the raw master deck is offered whole";
    }
    EXPECT_FALSE(m.can_choose_master_deck[rc.run.master_deck_count])
        << "and nothing past its end";

    // Pick the bottled, upgraded, misc-carrying rows in turn is not possible --
    // one pick closes the grid -- so take the Searing Blow, which proves the
    // stat-equivalent half, and check the bottle clear on a second run below.
    step(rc, make_action(ActionVerb::CHOOSE, 2));
    EXPECT_EQ(rc.pending_deck_pick,
              static_cast<uint8_t>(EquipDeckPick::NONE));
    ASSERT_EQ(rc.run.master_deck_count, 4);
    EXPECT_EQ(rc.run.master_deck[3].card_id,
              static_cast<uint16_t>(CardId::SEARING_BLOW));
    EXPECT_EQ(rc.run.master_deck[3].upgrade, 2)
        << "makeStatEquivalentCopy carries timesUpgraded";
    EXPECT_EQ(rc.run.master_deck[3].misc, 7)
        << "and misc -- a grown Ritual Dagger duplicates grown";
    EXPECT_EQ(rc.run.master_deck[2].upgrade, 2) << "the original is untouched";
    legal_actions(rc, m);
    EXPECT_TRUE(m.can_proceed) << "back on the shop menu after the pick";
}

TEST(ShopFlow, DollysMirrorClearsTheBottleOnTheCopyOnly) {
    // DollysMirror.java:50-52 sets the three inBottle* flags false ON THE COPY;
    // the original keeps its bottle, so mirroring a bottled card yields one
    // bottled instance and one free duplicate.
    RunController rc = enter_shop(0xB48'5407LL);
    rc.run.gold = 5000;
    rc.run.master_deck_count = 1;
    rc.run.master_deck[0] = CardInstance{};
    rc.run.master_deck[0].card_id = static_cast<uint16_t>(CardId::CLEAVE);
    rc.run.master_deck[0].flags = kMasterCardInBottleFlame;
    rc.shop.relics[1].id = static_cast<uint16_t>(RelicId::DOLLYS_MIRROR);
    rc.shop.relics[1].price = 250;

    step(rc, make_action(ActionVerb::CHOOSE,
                         static_cast<uint8_t>(kChooseShopRelicBase + 1)));
    ASSERT_EQ(rc.pending_deck_pick,
              static_cast<uint8_t>(EquipDeckPick::DOLLYS_MIRROR));
    step(rc, make_action(ActionVerb::CHOOSE, 0));

    ASSERT_EQ(rc.run.master_deck_count, 2);
    EXPECT_EQ(rc.run.master_deck[0].flags, kMasterCardInBottleFlame)
        << "the original keeps its bottle";
    EXPECT_EQ(rc.run.master_deck[1].flags & kMasterCardBottleMask, 0u)
        << "the copy arrives unbottled (:50-52)";
    EXPECT_EQ(rc.run.master_deck[1].card_id,
              static_cast<uint16_t>(CardId::CLEAVE));
}

TEST(ShopFlow, MealTicketHealsOnAStaticShopRoomEntry) {
    RunController rc = run_begin(0xB48'5407LL, kA20);
    for (int x = 0; x < kMapCols; ++x) {
        rc.run.map[run_state_map_index(x, 0)].room_type =
            static_cast<uint8_t>(RoomType::Shop);
    }
    give_relic(rc.run, RelicId::MEAL_TICKET);
    leave_neow(rc);
    rc.run.hp = 40;
    const int16_t hp_before = rc.run.hp;
    take_first_start(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::SHOP));
    const int16_t expect =
        static_cast<int16_t>(hp_before + kMealTicketHeal > rc.run.max_hp
                                 ? rc.run.max_hp
                                 : hp_before + kMealTicketHeal);
    EXPECT_EQ(rc.run.hp, expect);
}

TEST(ShopFlow, MealTicketAlsoHealsWhenAQuestionMarkResolvesToAShop) {
    // The ?-roll replaces the room object BEFORE justEnteredRoom runs, so this
    // is the same fan-out the static node above gets -- which is precisely the
    // shared entry effect the event framework deferred here.
    //
    // Search for a seed whose floor-1 ? rolls SHOP: the roll is one eventRng
    // float against the 100-slot table, SHOP occupying [0.10, 0.13).
    for (int64_t seed = 1; seed < 4000; ++seed) {
        RunController probe = run_begin(seed, kA20);
        RngStream e = probe.run.event_rng;
        const float roll = random(e);
        if (roll < 0.10f || roll >= 0.13f) continue;

        RunController rc = run_begin(seed, kA20);
        for (int x = 0; x < kMapCols; ++x) {
            rc.run.map[run_state_map_index(x, 0)].room_type =
                static_cast<uint8_t>(RoomType::Event);
        }
        give_relic(rc.run, RelicId::MEAL_TICKET);
        leave_neow(rc);
        rc.run.hp = 40;
        take_first_start(rc);
        ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::SHOP)) << seed;
        EXPECT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::Shop));
        EXPECT_EQ(rc.run.hp, 55);
        EXPECT_EQ(rc.run.merchant_rng.counter, 16);
        return;
    }
    FAIL() << "no seed under 4000 rolls a floor-1 ?->Shop";
}

// =============================================================================
// The captured-merchant vector
// =============================================================================

// A REAL A20 Ironclad shop, reproduced entry for entry from a recorded oracle
// campaign: run STS00008 of the b13 twenty-seed sweep (seed 1790050543758,
// floor 3, that run's first shop). Everything below -- the pre-entry stream
// triples, the three relic pools, and every expected id and price -- is read
// out of that capture's `oracle` block and its SHOP_SCREEN `screen_state`; the
// raw JSONL lives under the campaign data root and is not committed.
//
// This is the strongest single check in the suite, because it fixes all three
// streams at once against ground truth:
//   cardRng     9 -> 21   (12 draws: 5 x (rarity roll + pool index) + 2 colourless)
//   merchantRng 0 -> 16   (the sixteen-draw table above)
//   potionRng   3 -> 10   (3 tier rolls + 4 trap-14 rejection draws)
// and every price then falls out of the base tables, the two jitter bands, the
// integer sale halving and the A16 x1.1 -- which is also what pins the base
// price tables the decompiled tree cannot show on its own (see shop.hpp's note
// on the nested enums CFR could not resolve).
//
// It is NOT the ledger's oracle spot-diff leg: that one wants a capture taken
// against THIS engine's shop on a floor the driver walks to on purpose. This is
// a pre-existing capture that happened to contain a shop screen.
TEST(ShopCapture, B13Seed1790050543758Floor3MatchesTheRecordedMerchant) {
    RunState rs{};
    rs.run_seed = 1790050543758LL;
    rs.ascension = 20;
    rs.act = 1;
    rs.floor = 3;
    rs.hp = 68;
    rs.max_hp = 75;
    rs.gold = 99;
    rs.purge_cost = 75;
    rs.potion_slots = 2;
    rs.card_blizz_randomizer = 3;
    rs.card_rng = RngStream{static_cast<uint64_t>(-8634793150538233879LL),
                            static_cast<uint64_t>(-7724231287808926833LL), 9, 0};
    rs.merchant_rng = RngStream{static_cast<uint64_t>(8651138774985718958LL),
                                static_cast<uint64_t>(6818110329401097880LL), 0,
                                0};
    rs.potion_rng = RngStream{static_cast<uint64_t>(-7739402006926083965LL),
                              static_cast<uint64_t>(7150405526191279069LL), 3,
                              0};
    give_relic(rs, RelicId::BURNING_BLOOD);

    // The three pools the merchant end-pops, in the capture's recorded order.
    fill_pool(
        rs, RelicPool::COMMON,
        {RelicId::ODDLY_SMOOTH_STONE, RelicId::AKABEKO, RelicId::REGAL_PILLOW,
         RelicId::JUZU_BRACELET, RelicId::BAG_OF_MARBLES, RelicId::ANCHOR,
         RelicId::PEN_NIB, RelicId::TOY_ORNITHOPTER, RelicId::ORICHALCUM,
         RelicId::BOOT, RelicId::LANTERN, RelicId::SMILING_MASK,
         RelicId::ART_OF_WAR, RelicId::DREAM_CATCHER, RelicId::OMAMORI,
         RelicId::CENTENNIAL_PUZZLE, RelicId::BAG_OF_PREPARATION,
         RelicId::MAW_BANK, RelicId::POTION_BELT, RelicId::ANCIENT_TEA_SET,
         RelicId::TINY_CHEST, RelicId::CERAMIC_FISH, RelicId::HAPPY_FLOWER,
         RelicId::STRAWBERRY, RelicId::WHETSTONE, RelicId::RED_SKULL,
         RelicId::NUNCHAKU, RelicId::WAR_PAINT, RelicId::BRONZE_SCALES,
         RelicId::PRESERVED_INSECT, RelicId::MEAL_TICKET, RelicId::VAJRA,
         RelicId::BLOOD_VIAL});
    fill_pool(
        rs, RelicPool::UNCOMMON,
        {RelicId::MERCURY_HOURGLASS, RelicId::ORNAMENTAL_FAN,
         RelicId::INK_BOTTLE, RelicId::WHITE_BEAST_STATUE, RelicId::THE_COURIER,
         RelicId::PAPER_PHROG, RelicId::DARKSTONE_PERIAPT,
         RelicId::MUMMIFIED_HAND, RelicId::SHURIKEN, RelicId::TOXIC_EGG,
         RelicId::ETERNAL_FEATHER, RelicId::MATRYOSHKA, RelicId::FROZEN_EGG,
         RelicId::STRIKE_DUMMY, RelicId::GREMLIN_HORN, RelicId::SINGING_BOWL,
         RelicId::PANTOGRAPH, RelicId::BOTTLED_TORNADO, RelicId::KUNAI,
         RelicId::BLUE_CANDLE, RelicId::SUNDIAL, RelicId::LETTER_OPENER,
         RelicId::HORN_CLEAT, RelicId::MOLTEN_EGG, RelicId::BOTTLED_FLAME,
         RelicId::MEAT_ON_THE_BONE, RelicId::SELF_FORMING_CLAY, RelicId::PEAR,
         RelicId::BOTTLED_LIGHTNING, RelicId::QUESTION_CARD});
    fill_pool(rs, RelicPool::SHOP,
              {RelicId::ORRERY, RelicId::THE_ABACUS, RelicId::PRISMATIC_SHARD,
               RelicId::HAND_DRILL, RelicId::SLING_OF_COURAGE, RelicId::CAULDRON,
               RelicId::ORANGE_PELLETS, RelicId::MEMBERSHIP_CARD,
               RelicId::CHEMICAL_X, RelicId::STRANGE_SPOON,
               RelicId::CLOCKWORK_SOUVENIR, RelicId::FROZEN_EYE,
               RelicId::DOLLYS_MIRROR, RelicId::TOOLBOX, RelicId::BRIMSTONE,
               RelicId::LEES_WAFFLE, RelicId::MEDICAL_KIT});

    const ShopState shop = generate_shop(rs);

    // --- the five colored slots ------------------------------------------------
    EXPECT_EQ(shop.colored[0].id, static_cast<uint16_t>(CardId::PUMMEL));
    EXPECT_EQ(shop.colored[1].id, static_cast<uint16_t>(CardId::IRON_WAVE));
    EXPECT_EQ(shop.colored[2].id, static_cast<uint16_t>(CardId::ARMAMENTS));
    EXPECT_EQ(shop.colored[3].id, static_cast<uint16_t>(CardId::RAGE));
    EXPECT_EQ(shop.colored[4].id, static_cast<uint16_t>(CardId::RUPTURE));
    EXPECT_EQ(shop.colored[0].price, 43);  // sale slot: 75 -> 79 -> 39 -> 43
    EXPECT_EQ(shop.colored[1].price, 59);
    EXPECT_EQ(shop.colored[2].price, 59);
    EXPECT_EQ(shop.colored[3].price, 89);
    EXPECT_EQ(shop.colored[4].price, 85);
    EXPECT_EQ(shop.sale_index, 0);

    // --- the two colourless slots ----------------------------------------------
    EXPECT_EQ(shop.colorless[0].id, static_cast<uint16_t>(CardId::FINESSE));
    EXPECT_EQ(shop.colorless[1].id,
              static_cast<uint16_t>(CardId::SECRET_WEAPON));
    EXPECT_EQ(shop.colorless[0].price, 99);
    EXPECT_EQ(shop.colorless[1].price, 206);

    // --- relics: one UNCOMMON roll, one COMMON roll, then the SHOP slot --------
    EXPECT_EQ(shop.relics[0].id, static_cast<uint16_t>(RelicId::QUESTION_CARD));
    EXPECT_EQ(shop.relics[1].id, static_cast<uint16_t>(RelicId::BLOOD_VIAL));
    EXPECT_EQ(shop.relics[2].id, static_cast<uint16_t>(RelicId::MEDICAL_KIT));
    EXPECT_EQ(shop.relics[0].price, 268);
    EXPECT_EQ(shop.relics[1].price, 172);
    EXPECT_EQ(shop.relics[2].price, 161);

    // --- potions -----------------------------------------------------------------
    EXPECT_EQ(shop.potions[0].id,
              static_cast<uint16_t>(PotionId::STRENGTH_POTION));
    EXPECT_EQ(shop.potions[1].id,
              static_cast<uint16_t>(PotionId::DUPLICATION_POTION));
    EXPECT_EQ(shop.potions[2].id,
              static_cast<uint16_t>(PotionId::STEROID_POTION));
    EXPECT_EQ(shop.potions[0].price, 54);
    EXPECT_EQ(shop.potions[1].price, 85);
    EXPECT_EQ(shop.potions[2].price, 55);

    // --- purge + the three post-build stream states ------------------------------
    EXPECT_EQ(shop.actual_purge_cost, 75);
    EXPECT_TRUE(shop.purge_available);
    EXPECT_EQ(rs.merchant_rng.counter, 16);
    EXPECT_EQ(rs.merchant_rng.s0, static_cast<uint64_t>(4705300860281014165LL));
    EXPECT_EQ(rs.merchant_rng.s1, static_cast<uint64_t>(4204028962181382309LL));
    EXPECT_EQ(rs.card_rng.counter, 21);
    EXPECT_EQ(rs.card_rng.s0, static_cast<uint64_t>(8458146799682358052LL));
    EXPECT_EQ(rs.card_rng.s1, static_cast<uint64_t>(3933347875675124368LL));
    EXPECT_EQ(rs.potion_rng.counter, 10);
    EXPECT_EQ(rs.potion_rng.s0, static_cast<uint64_t>(-7724231287808926833LL));
    EXPECT_EQ(rs.potion_rng.s1, static_cast<uint64_t>(-6008490890897573153LL));
    // The blizz counter is a REWARD-screen pity counter: the shop's rarity
    // rolls read it and never move it (the capture agrees, 3 before and after).
    EXPECT_EQ(rs.card_blizz_randomizer, 3);
}

// The B4.8 acceptance capture: run STS00074 of
// b47_treasure_oracle_20260727T204809Z_claude01, seed 1790050543999, the
// floor-3 merchant. Where the vector above proves a merchant BUILD, this one
// also proves what happens when the player spends: the driver's random-legal
// policy bought the third potion and then a colored card, and the capture
// carries the state after each. Everything below -- pre-entry streams, the four
// end-popped pools, the shelf, the prices and the two purchases -- is
// transcribed from that artifact.
TEST(ShopCapture, B47Seed1790050543999Floor3MatchesTheRecordedMerchantAndItsPurchases) {
    RunState rs{};
    rs.run_seed = 1790050543999LL;
    rs.ascension = 20;
    rs.act = 1;
    rs.floor = 3;
    rs.hp = 21;
    rs.max_hp = 75;
    rs.gold = 128;
    rs.purge_cost = 75;
    rs.potion_slots = 2;
    rs.card_blizz_randomizer = 1;
    rs.card_rng = RngStream{static_cast<uint64_t>(7407002651897338544LL),
                            static_cast<uint64_t>(5100492437124867082LL), 18, 0};
    rs.merchant_rng = RngStream{static_cast<uint64_t>(8925859659600152053LL),
                                static_cast<uint64_t>(597351705577579072LL), 0, 0};
    rs.potion_rng = RngStream{static_cast<uint64_t>(301082221787037864LL),
                              static_cast<uint64_t>(-560592513775719408LL), 6, 0};
    give_relic(rs, RelicId::BURNING_BLOOD);

    fill_pool(
        rs, RelicPool::COMMON,
        {RelicId::STRAWBERRY, RelicId::SMILING_MASK, RelicId::MAW_BANK,
         RelicId::AKABEKO, RelicId::CENTENNIAL_PUZZLE, RelicId::ART_OF_WAR,
         RelicId::TINY_CHEST, RelicId::WHETSTONE, RelicId::JUZU_BRACELET,
         RelicId::REGAL_PILLOW, RelicId::TOY_ORNITHOPTER,
         RelicId::ODDLY_SMOOTH_STONE, RelicId::WAR_PAINT, RelicId::VAJRA,
         RelicId::ORICHALCUM, RelicId::BOOT, RelicId::CERAMIC_FISH,
         RelicId::LANTERN, RelicId::OMAMORI, RelicId::NUNCHAKU,
         RelicId::HAPPY_FLOWER, RelicId::PRESERVED_INSECT, RelicId::POTION_BELT,
         RelicId::BLOOD_VIAL, RelicId::BAG_OF_PREPARATION, RelicId::ANCHOR,
         RelicId::MEAL_TICKET, RelicId::RED_SKULL, RelicId::ANCIENT_TEA_SET,
         RelicId::PEN_NIB, RelicId::DREAM_CATCHER, RelicId::BRONZE_SCALES,
         RelicId::BAG_OF_MARBLES});
    fill_pool(
        rs, RelicPool::UNCOMMON,
        {RelicId::QUESTION_CARD, RelicId::KUNAI, RelicId::SELF_FORMING_CLAY,
         RelicId::MUMMIFIED_HAND, RelicId::BLUE_CANDLE, RelicId::PANTOGRAPH,
         RelicId::INK_BOTTLE, RelicId::DARKSTONE_PERIAPT,
         RelicId::WHITE_BEAST_STATUE, RelicId::SINGING_BOWL,
         RelicId::BOTTLED_FLAME, RelicId::MOLTEN_EGG, RelicId::ETERNAL_FEATHER,
         RelicId::SUNDIAL, RelicId::PAPER_PHROG, RelicId::HORN_CLEAT,
         RelicId::BOTTLED_LIGHTNING, RelicId::MATRYOSHKA,
         RelicId::BOTTLED_TORNADO, RelicId::MEAT_ON_THE_BONE,
         RelicId::FROZEN_EGG, RelicId::MERCURY_HOURGLASS, RelicId::LETTER_OPENER,
         RelicId::SHURIKEN, RelicId::STRIKE_DUMMY, RelicId::ORNAMENTAL_FAN,
         RelicId::PEAR, RelicId::THE_COURIER, RelicId::GREMLIN_HORN,
         RelicId::TOXIC_EGG});
    fill_pool(
        rs, RelicPool::RARE,
        {RelicId::MANGO, RelicId::ICE_CREAM, RelicId::TUNGSTEN_ROD,
         RelicId::FOSSILIZED_HELIX, RelicId::DU_VU_DOLL, RelicId::POCKETWATCH,
         RelicId::PRAYER_WHEEL, RelicId::WING_BOOTS, RelicId::GINGER,
         RelicId::TORII, RelicId::CAPTAINS_WHEEL, RelicId::PEACE_PIPE,
         RelicId::OLD_COIN, RelicId::DEAD_BRANCH, RelicId::MAGIC_FLOWER,
         RelicId::THREAD_AND_NEEDLE, RelicId::TURNIP, RelicId::LIZARD_TAIL,
         RelicId::UNCEASING_TOP, RelicId::BIRD_FACED_URN, RelicId::GIRYA,
         RelicId::CHARONS_ASHES, RelicId::INCENSE_BURNER,
         RelicId::STONE_CALENDAR, RelicId::GAMBLING_CHIP, RelicId::SHOVEL,
         RelicId::CALIPERS, RelicId::CHAMPION_BELT});
    fill_pool(
        rs, RelicPool::SHOP,
        {RelicId::SLING_OF_COURAGE, RelicId::CHEMICAL_X, RelicId::ORANGE_PELLETS,
         RelicId::ORRERY, RelicId::FROZEN_EYE, RelicId::DOLLYS_MIRROR,
         RelicId::CAULDRON, RelicId::STRANGE_SPOON, RelicId::BRIMSTONE,
         RelicId::CLOCKWORK_SOUVENIR, RelicId::PRISMATIC_SHARD,
         RelicId::TOOLBOX, RelicId::LEES_WAFFLE, RelicId::THE_ABACUS,
         RelicId::HAND_DRILL, RelicId::MEMBERSHIP_CARD, RelicId::MEDICAL_KIT});

    ShopState shop = generate_shop(rs);

    // --- the shelf, id for id and price for price ------------------------------
    EXPECT_EQ(shop.colored[0].id, static_cast<uint16_t>(CardId::SEVER_SOUL));
    EXPECT_EQ(shop.colored[1].id, static_cast<uint16_t>(CardId::BODY_SLAM));
    EXPECT_EQ(shop.colored[2].id, static_cast<uint16_t>(CardId::SENTINEL));
    EXPECT_EQ(shop.colored[3].id, static_cast<uint16_t>(CardId::HAVOC));
    EXPECT_EQ(shop.colored[4].id, static_cast<uint16_t>(CardId::FEEL_NO_PAIN));
    EXPECT_EQ(shop.colored[0].price, 83);
    EXPECT_EQ(shop.colored[1].price, 58);
    EXPECT_EQ(shop.colored[2].price, 79);
    EXPECT_EQ(shop.colored[3].price, 54);
    EXPECT_EQ(shop.colored[4].price, 41);  // sale slot: an UNCOMMON at 41
    EXPECT_EQ(shop.sale_index, 4);
    EXPECT_EQ(shop.colorless[0].id, static_cast<uint16_t>(CardId::BANDAGE_UP));
    EXPECT_EQ(shop.colorless[1].id, static_cast<uint16_t>(CardId::METAMORPHOSIS));
    EXPECT_EQ(shop.colorless[0].price, 108);
    EXPECT_EQ(shop.colorless[1].price, 204);

    // Two COMMON tier rolls, then the always-SHOP third slot.
    EXPECT_EQ(shop.relics[0].id, static_cast<uint16_t>(RelicId::BAG_OF_MARBLES));
    EXPECT_EQ(shop.relics[1].id, static_cast<uint16_t>(RelicId::TOXIC_EGG));
    EXPECT_EQ(shop.relics[2].id, static_cast<uint16_t>(RelicId::MEDICAL_KIT));
    EXPECT_EQ(shop.relics[0].price, 171);
    EXPECT_EQ(shop.relics[1].price, 274);
    EXPECT_EQ(shop.relics[2].price, 172);

    EXPECT_EQ(shop.potions[0].id, static_cast<uint16_t>(PotionId::ENERGY_POTION));
    EXPECT_EQ(shop.potions[1].id, static_cast<uint16_t>(PotionId::BLOOD_POTION));
    EXPECT_EQ(shop.potions[2].id, static_cast<uint16_t>(PotionId::SKILL_POTION));
    EXPECT_EQ(shop.potions[0].price, 55);
    EXPECT_EQ(shop.potions[1].price, 57);
    EXPECT_EQ(shop.potions[2].price, 57);

    EXPECT_EQ(shop.actual_purge_cost, 75);
    EXPECT_TRUE(shop.purge_available);

    // --- the three post-build stream states ------------------------------------
    EXPECT_EQ(rs.merchant_rng.counter, 16);
    EXPECT_EQ(rs.merchant_rng.s0, static_cast<uint64_t>(6319263430244920371LL));
    EXPECT_EQ(rs.merchant_rng.s1, static_cast<uint64_t>(8973596775308567784LL));
    EXPECT_EQ(rs.card_rng.counter, 30);
    EXPECT_EQ(rs.card_rng.s0, static_cast<uint64_t>(-2483065224940134106LL));
    EXPECT_EQ(rs.card_rng.s1, static_cast<uint64_t>(-3978610613189658618LL));
    EXPECT_EQ(rs.potion_rng.counter, 13);
    EXPECT_EQ(rs.potion_rng.s0, static_cast<uint64_t>(-1231467067838940388LL));
    EXPECT_EQ(rs.potion_rng.s1, static_cast<uint64_t>(-827854966696020424LL));
    EXPECT_EQ(rs.card_blizz_randomizer, 1);

    // --- what the run then bought ----------------------------------------------
    // The Skill Potion off the third potion slot: 128 -> 71, into the first free
    // slot, and the shelf row retires.
    ASSERT_TRUE(shop_buy_potion(rs, shop, 2));
    EXPECT_EQ(rs.gold, 71);
    EXPECT_EQ(rs.potions[0], static_cast<uint16_t>(PotionId::SKILL_POTION));
    EXPECT_EQ(rs.potions[1], static_cast<uint16_t>(PotionId::NONE));
    EXPECT_TRUE(shop.potions[2].sold);

    // Havoc off the fourth colored slot: 71 -> 17, appended to the deck. (This
    // RunState carries no master deck -- the capture's thirteen rows are the
    // spot-diff harness's business, not this vector's -- so the append lands at
    // index 0.)
    ASSERT_TRUE(shop_buy_card(rs, shop, 3, /*colorless=*/false));
    EXPECT_EQ(rs.gold, 17);
    ASSERT_EQ(rs.master_deck_count, 1);
    EXPECT_EQ(rs.master_deck[0].card_id, static_cast<uint16_t>(CardId::HAVOC));
    EXPECT_TRUE(shop.colored[3].sold);

    // Neither purchase touches the removal service.
    EXPECT_EQ(shop.actual_purge_cost, 75);
    EXPECT_EQ(rs.purge_cost, 75);
}
