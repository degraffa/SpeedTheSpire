// B4.5 -- combat rewards: assembly (gold / elite relic / potion / cards),
// pity dynamics, stream attribution (traps 13 and 18), the Smoke Bomb
// consumes-but-offers-nothing path, and the claim flow (gold door, potion
// slots + Sozu, relic acquisition, the master-deck door incl. the Ceramic Fish
// guard, Singing Bowl, skip, Question Card / Busted Crown / White Beast Statue
// / Golden Idol / Prayer Wheel / Black Star).
//
// Hand-derivations here recompute expectations from rng_stream primitives, the
// generated pool tables and the cited constants ONLY -- never by calling the
// production assembly on the same stream.

#include "sts/engine/combat_rewards.hpp"

#include <cstring>
#include <initializer_list>
#include <span>

#include <gtest/gtest.h>

#include "sts/engine/cards.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/potions.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/relics.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kA20 = 20;

bool streams_equal(const RngStream& a, const RngStream& b) noexcept {
    return a.s0 == b.s0 && a.s1 == b.s1 && a.counter == b.counter;
}

void give_relics(RunState& rs, std::initializer_list<RelicId> ids) {
    uint8_t i = rs.relic_count;
    for (const RelicId id : ids) {
        rs.relics[i] = RelicSlot{static_cast<uint16_t>(id), 0};
        ++i;
    }
    rs.relic_count = i;
}

// A populated post-run_begin RunState (pools shuffled, pity at +5, potion
// slots per ascension) positioned on floor 1.
RunState make_run(int64_t seed, uint8_t ascension = kA20) {
    RunState rs = run_begin(seed, ascension).run;
    rs.floor = 1;
    return rs;
}

int count_kind(const RewardScreen& s, RewardItemKind k) {
    int n = 0;
    for (uint8_t i = 0; i < s.count; ++i) {
        n += s.items[i].kind == static_cast<uint8_t>(k) ? 1 : 0;
    }
    return n;
}

int find_kind(const RewardScreen& s, RewardItemKind k) {
    for (uint8_t i = 0; i < s.count; ++i) {
        if (s.items[i].kind == static_cast<uint8_t>(k)) {
            return i;
        }
    }
    return -1;
}

// --- independent recomputations ---------------------------------------------

// AbstractDungeon.returnRandomPotion(false): one tier roll, then one draw per
// rejection-sampling attempt until the rarity matches (no Fruit-Juice clause
// when not limited).
PotionId hand_unlimited_potion_roll(RngStream& rng) {
    const PotionRarity tier = potion_tier_for_roll(random(rng, 0, 99));
    auto draw = [&rng]() {
        return static_cast<PotionId>(random(rng, kPotionPoolSize - 1) + 1);
    };
    PotionId candidate = draw();
    while (potion_def(candidate)->rarity != tier) {
        candidate = draw();
    }
    return candidate;
}

CardId hand_pool_draw(RngStream& rng, RewardCardRarity r) {
    switch (r) {
        case RewardCardRarity::RARE:
            return kIroncladRarePool[static_cast<unsigned>(
                random(rng, kIroncladRarePoolCount - 1))];
        case RewardCardRarity::UNCOMMON:
            return kIroncladUncommonPool[static_cast<unsigned>(
                random(rng, kIroncladUncommonPoolCount - 1))];
        case RewardCardRarity::COMMON:
        default:
            return kIroncladCommonPool[static_cast<unsigned>(
                random(rng, kIroncladCommonPoolCount - 1))];
    }
}

struct HandCardReward {
    uint16_t ids[kRewardCardCap] = {};
    RewardCardRarity rarities[kRewardCardCap] = {};
    int count = 0;
    int dupe_rerolls = 0;
    int upgrade_booleans = 0;
};

// AbstractDungeon.getRewardCards for `num` cards: rarity roll (+pity update),
// no-dupe re-roll, then one randomBoolean per non-RARE card. Mutates `rng` and
// `pity` exactly as the game mutates cardRng / cardBlizzRandomizer.
HandCardReward hand_roll_cards(RngStream& rng, int16_t& pity, int num,
                               RoomType room) {
    HandCardReward out{};
    for (int i = 0; i < num; ++i) {
        const int roll = static_cast<int>(random(rng, 99)) +
                         static_cast<int>(pity);
        const RewardCardRarity rarity = reward_card_rarity(roll, room);
        if (rarity == RewardCardRarity::RARE) {
            pity = static_cast<int16_t>(kCardBlizzStartOffset);
        } else if (rarity == RewardCardRarity::COMMON) {
            int v = pity - kCardBlizzGrowth;
            if (v <= kCardBlizzMaxOffset) {
                v = kCardBlizzMaxOffset;
            }
            pity = static_cast<int16_t>(v);
        }
        for (;;) {
            const CardId id = hand_pool_draw(rng, rarity);
            bool dupe = false;
            for (int j = 0; j < i; ++j) {
                dupe = dupe || out.ids[j] == static_cast<uint16_t>(id);
            }
            if (!dupe) {
                out.ids[i] = static_cast<uint16_t>(id);
                break;
            }
            ++out.dupe_rerolls;
        }
        out.rarities[i] = rarity;
    }
    for (int i = 0; i < num; ++i) {
        if (out.rarities[i] != RewardCardRarity::RARE) {
            (void)random_boolean(rng, kExordiumCardUpgradedChance);
            ++out.upgrade_booleans;
        }
    }
    out.count = num;
    return out;
}

// =============================================================================
// Rarity thresholds (the widths-vs-thresholds trap) and pools
// =============================================================================

TEST(RewardRarity, ThresholdsAreThreeAndFortyNotTheWidths) {
    // Normal rooms: `< 3` RARE, `< 40` UNCOMMON (widths 3/37,
    // AbstractRoom.java:108-109,158,167).
    EXPECT_EQ(reward_card_rarity(-45, RoomType::Monster), RewardCardRarity::RARE);
    EXPECT_EQ(reward_card_rarity(2, RoomType::Monster), RewardCardRarity::RARE);
    EXPECT_EQ(reward_card_rarity(3, RoomType::Monster),
              RewardCardRarity::UNCOMMON);
    EXPECT_EQ(reward_card_rarity(39, RoomType::Monster),
              RewardCardRarity::UNCOMMON);
    EXPECT_EQ(reward_card_rarity(40, RoomType::Monster),
              RewardCardRarity::COMMON);
    EXPECT_EQ(reward_card_rarity(104, RoomType::Monster),
              RewardCardRarity::COMMON);
}

TEST(RewardRarity, EliteWidensToTenFiftyAndBossIsAlwaysRare) {
    // MonsterRoomElite.java:34-35 (widths 10/40 -> thresholds < 10 / < 50);
    // MonsterRoomBoss.java:40-42 ignores the roll.
    EXPECT_EQ(reward_card_rarity(9, RoomType::Elite), RewardCardRarity::RARE);
    EXPECT_EQ(reward_card_rarity(10, RoomType::Elite),
              RewardCardRarity::UNCOMMON);
    EXPECT_EQ(reward_card_rarity(49, RoomType::Elite),
              RewardCardRarity::UNCOMMON);
    EXPECT_EQ(reward_card_rarity(50, RoomType::Elite), RewardCardRarity::COMMON);
    EXPECT_EQ(reward_card_rarity(99, RoomType::Boss), RewardCardRarity::RARE);
    EXPECT_EQ(reward_card_rarity(0, RoomType::Boss), RewardCardRarity::RARE);
}

TEST(RewardPools, RedOnlySplitTwentyThirtySixSixteen) {
    // The 72 red non-basics split 20/36/16 (design §5.1); colorless is
    // unreachable from a combat reward (Ironclad.getCardPool -> addRedCards
    // only, Ironclad.java:138-150 / CardLibrary.java:1152-1161; the sole
    // AbstractDungeon.getColorlessRewardCards caller is RewardItem(CardColor)
    // -> SensoryStone.java:121, an Act-3 event). Membership pins: no BASIC
    // starter, no curse, and every id resolves to a registry row.
    EXPECT_EQ(kIroncladCommonPoolCount, 20);
    EXPECT_EQ(kIroncladUncommonPoolCount, 36);
    EXPECT_EQ(kIroncladRarePoolCount, 16);
    auto check = [](const CardId* pool, int n) {
        for (int i = 0; i < n; ++i) {
            const CardId id = pool[i];
            EXPECT_NE(id, CardId::STRIKE);
            EXPECT_NE(id, CardId::DEFEND);
            EXPECT_NE(id, CardId::BASH);
            EXPECT_NE(id, CardId::ASCENDERS_BANE);
            const CardDef* def = card_def(id);
            ASSERT_NE(def, nullptr);
            EXPECT_NE(def->type, CardType::CURSE);
            EXPECT_NE(def->type, CardType::STATUS);
        }
    };
    check(kIroncladCommonPool.data(), kIroncladCommonPoolCount);
    check(kIroncladUncommonPool.data(), kIroncladUncommonPoolCount);
    check(kIroncladRarePool.data(), kIroncladRarePoolCount);
}

// =============================================================================
// Gold (trap 18) + Golden Idol
// =============================================================================

TEST(RewardGold, Trap18BossIsMiscRngEliteAndNormalAreTreasureRng) {
    for (int64_t seed : {11LL, 222LL, 3333LL}) {
        // Normal room: one treasureRng draw, miscRng untouched.
        {
            RunState rs = make_run(seed);
            RngStream misc = from_seed(seed + 900);
            RngStream treasure_copy = rs.treasure_rng;
            const int expected =
                static_cast<int>(random(treasure_copy, 10, 20));
            RewardScreen s{};
            assemble_combat_rewards(rs, misc, RoomType::Monster,
                                RewardOutcome::KILLED, s);
            const int gi = find_kind(s, RewardItemKind::GOLD);
            ASSERT_GE(gi, 0);
            EXPECT_EQ(s.items[gi].gold, expected);
            EXPECT_EQ(rs.treasure_rng.counter, treasure_copy.counter);
            EXPECT_EQ(misc.counter, 0) << "normal gold must not touch miscRng";
        }
        // Elite: one treasureRng draw for gold (plus relicRng for the relic).
        {
            RunState rs = make_run(seed);
            RngStream misc = from_seed(seed + 900);
            RngStream treasure_copy = rs.treasure_rng;
            const int expected =
                static_cast<int>(random(treasure_copy, 25, 35));
            RewardScreen s{};
            assemble_combat_rewards(rs, misc, RoomType::Elite,
                                RewardOutcome::KILLED, s);
            const int gi = find_kind(s, RewardItemKind::GOLD);
            ASSERT_GE(gi, 0);
            EXPECT_EQ(s.items[gi].gold, expected);
            EXPECT_EQ(misc.counter, 0) << "elite gold must not touch miscRng";
        }
        // Boss: one miscRng draw, treasureRng untouched.
        {
            RunState rs = make_run(seed);
            RngStream misc = from_seed(seed + 900);
            RngStream misc_copy = misc;
            const int tmp = 100 + static_cast<int>(random(misc_copy, -5, 5));
            const int expected =
                mathutils_round(static_cast<float>(tmp) * 0.75f);  // A20 >= 13
            const int32_t treasure_before = rs.treasure_rng.counter;
            RewardScreen s{};
            assemble_combat_rewards(rs, misc, RoomType::Boss,
                            RewardOutcome::KILLED, s);
            const int gi = find_kind(s, RewardItemKind::GOLD);
            ASSERT_GE(gi, 0);
            EXPECT_EQ(s.items[gi].gold, expected);
            EXPECT_EQ(misc.counter, 1);
            EXPECT_EQ(rs.treasure_rng.counter, treasure_before)
                << "boss gold must not touch treasureRng";
        }
    }
}

TEST(RewardGold, BossBelowA13SkipsTheRound) {
    RngStream misc = from_seed(42);
    RngStream copy = misc;
    const int tmp = 100 + static_cast<int>(random(copy, -5, 5));
    EXPECT_EQ(roll_boss_gold(misc, 0), tmp);
    EXPECT_EQ(misc.counter, 1);
}

TEST(RewardGold, GoldenIdolAddsRoundedQuarterAtAssemblyAndClaim) {
    RunState rs = make_run(77);
    give_relics(rs, {RelicId::GOLDEN_IDOL});
    RngStream misc = from_seed(1);
    RewardScreen s{};
    assemble_combat_rewards(rs, misc, RoomType::Monster,
                                RewardOutcome::KILLED, s);
    const int gi = find_kind(s, RewardItemKind::GOLD);
    ASSERT_GE(gi, 0);
    const RunRewardItem item = s.items[gi];
    EXPECT_EQ(item.bonus_gold,
              mathutils_round(static_cast<float>(item.gold) * 0.25f));
    const int32_t before = rs.gold;
    ASSERT_TRUE(claim_reward(rs, misc, s, static_cast<uint8_t>(gi)));
    EXPECT_EQ(rs.gold, before + item.gold + item.bonus_gold);
}

TEST(RewardGold, EctoplasmSuppressesTheClaimThroughTheGoldDoor) {
    // gain_gold is the single run-layer gold door; Ectoplasm returns before
    // the += (AbstractPlayer.gainGold via relics/relic_pickup.hpp). A direct
    // rs.gold write in the claim path would fail this.
    RunState rs = make_run(77);
    give_relics(rs, {RelicId::ECTOPLASM});
    RngStream misc = from_seed(1);
    RewardScreen s{};
    assemble_combat_rewards(rs, misc, RoomType::Monster,
                                RewardOutcome::KILLED, s);
    const int gi = find_kind(s, RewardItemKind::GOLD);
    ASSERT_GE(gi, 0);
    const int32_t before = rs.gold;
    ASSERT_TRUE(claim_reward(rs, misc, s, static_cast<uint8_t>(gi)));
    EXPECT_EQ(rs.gold, before);
}

// =============================================================================
// Potion drop + ratchet
// =============================================================================

TEST(RewardPotion, RollIsUnconditionalAndRatchetMovesBothWays) {
    // Sweep seeds until both outcomes are seen; each assembly must consume at
    // least the one unconditional potionRng roll and move blizzardPotionMod by
    // exactly +/-10 (AbstractRoom.java:601-607).
    bool saw_drop = false;
    bool saw_miss = false;
    for (int64_t seed = 1; seed < 200 && !(saw_drop && saw_miss); ++seed) {
        RunState rs = make_run(seed);
        const int16_t mod_before = rs.blizzard_potion_mod;
        RngStream potion_copy = rs.potion_rng;
        const int roll = static_cast<int>(random(potion_copy, 0, 99));
        const bool expect_drop = roll < 40;  // mod_before == 0 at run start
        const PotionId expect_id =
            expect_drop ? hand_unlimited_potion_roll(potion_copy)
                        : PotionId::NONE;
        RngStream misc = from_seed(seed);
        RewardScreen s{};
        assemble_combat_rewards(rs, misc, RoomType::Monster,
                                RewardOutcome::KILLED, s);
        if (expect_drop) {
            saw_drop = true;
            const int pi = find_kind(s, RewardItemKind::POTION);
            ASSERT_GE(pi, 0);
            EXPECT_EQ(s.items[pi].id, static_cast<uint16_t>(expect_id));
            EXPECT_EQ(rs.blizzard_potion_mod, mod_before - 10);
            EXPECT_TRUE(streams_equal(rs.potion_rng, potion_copy));
        } else {
            saw_miss = true;
            EXPECT_EQ(find_kind(s, RewardItemKind::POTION), -1);
            EXPECT_EQ(rs.blizzard_potion_mod, mod_before + 10);
            EXPECT_EQ(rs.potion_rng.counter, 1)
                << "the miss still consumed the unconditional roll";
        }
    }
    EXPECT_TRUE(saw_drop);
    EXPECT_TRUE(saw_miss);
}

TEST(RewardPotion, WhiteBeastStatueForcesTheDrop) {
    for (int64_t seed : {5LL, 50LL, 500LL, 5000LL, 50000LL}) {
        RunState rs = make_run(seed);
        give_relics(rs, {RelicId::WHITE_BEAST_STATUE});
        RngStream misc = from_seed(seed);
        RewardScreen s{};
        assemble_combat_rewards(rs, misc, RoomType::Monster,
                                RewardOutcome::KILLED, s);
        EXPECT_GE(find_kind(s, RewardItemKind::POTION), 0) << "seed " << seed;
        EXPECT_EQ(rs.blizzard_potion_mod, -10);
    }
}

// =============================================================================
// Elite relic drops
// =============================================================================

TEST(RewardRelic, EliteRollsTierThenPopsThePoolAtAssembly) {
    for (int64_t seed : {3LL, 33LL, 333LL}) {
        RunState rs = make_run(seed);
        // Derive the expectation on an independent COPY of the run: the tier
        // roll first (relicRng d100 through the 50/33/17 gate ==
        // MonsterRoomElite.returnRandomRelicTier), then the front pop with the
        // same spawn context assemble builds. The pop/canSpawn machinery is
        // B4.6's (already pinned there); what this pins is B4.5's ORDER and
        // context construction.
        RunState derive = rs;
        const RelicTier tier = return_random_relic_tier(derive);
        RelicSpawnContext ctx{};
        ctx.floor = derive.floor;
        fill_deck_spawn_gates(derive, ctx);
        fill_campfire_relic_count(derive, ctx);
        fill_boss_spawn_gates(derive, ctx);
        const RelicId expected = return_random_relic_key(derive, tier, ctx);
        const int32_t relic_draws_expected = derive.relic_rng.counter;

        RngStream misc = from_seed(seed);
        RewardScreen s{};
        assemble_combat_rewards(rs, misc, RoomType::Elite,
                                RewardOutcome::KILLED, s);
        const int ri = find_kind(s, RewardItemKind::RELIC);
        ASSERT_GE(ri, 0);
        EXPECT_EQ(s.items[ri].id, static_cast<uint16_t>(expected))
            << "seed " << seed;
        EXPECT_EQ(rs.relic_rng.counter, relic_draws_expected);
        for (int p = 0; p < kRelicTierCount; ++p) {
            EXPECT_EQ(rs.relic_pool_count[p], derive.relic_pool_count[p]);
        }

        // Claiming appends in acquisition order (after Burning Blood).
        const uint8_t relics_before = rs.relic_count;
        ASSERT_TRUE(claim_reward(rs, misc, s, static_cast<uint8_t>(ri)));
        EXPECT_EQ(rs.relic_count, relics_before + 1);
        EXPECT_EQ(rs.relics[relics_before].relic_id,
                  static_cast<uint16_t>(expected));
    }
}

TEST(RewardRelic, BlackStarAddsASecondNonCampfireRelic) {
    for (int64_t seed = 1; seed < 40; ++seed) {
        RunState rs = make_run(seed);
        give_relics(rs, {RelicId::BLACK_STAR});
        const int32_t relic_draws_before = rs.relic_rng.counter;
        RngStream misc = from_seed(seed);
        RewardScreen s{};
        assemble_combat_rewards(rs, misc, RoomType::Elite,
                                RewardOutcome::KILLED, s);
        EXPECT_EQ(count_kind(s, RewardItemKind::RELIC), 2) << "seed " << seed;
        EXPECT_GE(rs.relic_rng.counter, relic_draws_before + 2)
            << "two tier rolls";
        // The second draw is returnRandomNonCampfireRelic: never Peace Pipe /
        // Shovel / Girya (AbstractDungeon.java:690-697).
        int seen = 0;
        for (uint8_t i = 0; i < s.count; ++i) {
            if (s.items[i].kind != static_cast<uint8_t>(RewardItemKind::RELIC)) {
                continue;
            }
            if (++seen == 2) {
                const auto id = static_cast<RelicId>(s.items[i].id);
                EXPECT_NE(id, RelicId::PEACE_PIPE);
                EXPECT_NE(id, RelicId::SHOVEL);
                EXPECT_NE(id, RelicId::GIRYA);
            }
        }
    }
}

// =============================================================================
// Card rewards: pity dynamics, dupe re-roll, the Act-1 upgrade draw (trap 13)
// =============================================================================

TEST(RewardCards, PityAcrossScriptedSequenceMatchesHandDerivation) {
    // Acceptance: pity dynamics across scripted reward sequences match
    // hand-derivation. Drive eight consecutive normal-room assemblies per seed
    // and require the offer ids, the post-sequence cardRng state AND the
    // cardBlizzRandomizer trajectory to match an independent recomputation
    // built from rng_stream primitives + the generated pools + the cited
    // constants. Verifies COMMON's -1 step and UNCOMMON's no-op across real
    // sequences (RARE reset has its own named test below).
    for (int64_t seed : {7LL, 1234LL, 98765LL}) {
        RunState rs = make_run(seed);
        RngStream hand_rng = rs.card_rng;
        int16_t hand_pity = rs.card_blizz_randomizer;
        for (int fight = 0; fight < 8; ++fight) {
            const HandCardReward expect =
                hand_roll_cards(hand_rng, hand_pity, 3, RoomType::Monster);
            RngStream misc = from_seed(seed + fight);
            RewardScreen s{};
            assemble_combat_rewards(rs, misc, RoomType::Monster,
                                RewardOutcome::KILLED, s);
            const int ci = find_kind(s, RewardItemKind::CARDS);
            ASSERT_GE(ci, 0);
            const RunRewardItem& item = s.items[ci];
            ASSERT_EQ(item.card_count, 3);
            for (int j = 0; j < 3; ++j) {
                EXPECT_EQ(item.card_ids[j], expect.ids[j])
                    << "seed " << seed << " fight " << fight << " card " << j;
                EXPECT_EQ(item.card_upgrades[j], 0)
                    << "Act-1 upgrade chance is 0.0f";
            }
            EXPECT_EQ(rs.card_blizz_randomizer, hand_pity)
                << "seed " << seed << " fight " << fight;
            EXPECT_TRUE(streams_equal(rs.card_rng, hand_rng))
                << "seed " << seed << " fight " << fight;
        }
        // The trajectory must actually have moved (commons are frequent).
        EXPECT_NE(rs.card_blizz_randomizer, 5);
    }
}

TEST(RewardCards, RareResetsPityToPlusFiveBossEveryCard) {
    // Boss rewards return RARE unconditionally, and the pity switch runs on
    // the RETURNED rarity -- so a boss reward both consumes the three rarity
    // rolls and leaves cardBlizzRandomizer at +5 (AbstractDungeon.java:
    // 1435-1439; MonsterRoomBoss.java:40-42).
    RunState rs = make_run(101);
    rs.card_blizz_randomizer = -12;  // as if many commons had been drawn
    RngStream hand_rng = rs.card_rng;
    int16_t hand_pity = rs.card_blizz_randomizer;
    const HandCardReward expect =
        hand_roll_cards(hand_rng, hand_pity, 3, RoomType::Boss);
    RngStream misc = from_seed(101);
    RewardScreen s{};
    assemble_combat_rewards(rs, misc, RoomType::Boss,
                            RewardOutcome::KILLED, s);
    const int ci = find_kind(s, RewardItemKind::CARDS);
    ASSERT_GE(ci, 0);
    EXPECT_EQ(rs.card_blizz_randomizer, 5);
    EXPECT_EQ(hand_pity, 5);
    for (int j = 0; j < 3; ++j) {
        EXPECT_EQ(s.items[ci].card_ids[j], expect.ids[j]);
        // Every boss offer card is a rare-pool member.
        bool in_rare = false;
        for (int k = 0; k < kIroncladRarePoolCount; ++k) {
            in_rare = in_rare || kIroncladRarePool[static_cast<unsigned>(k)] ==
                                     static_cast<CardId>(s.items[ci].card_ids[j]);
        }
        EXPECT_TRUE(in_rare);
    }
    EXPECT_TRUE(streams_equal(rs.card_rng, hand_rng));
}

TEST(RewardCards, PityFloorsAtMinusForty) {
    RunState rs = make_run(55);
    rs.card_blizz_randomizer = -40;
    RngStream hand_rng = rs.card_rng;
    int16_t hand_pity = rs.card_blizz_randomizer;
    const HandCardReward expect =
        hand_roll_cards(hand_rng, hand_pity, 3, RoomType::Monster);
    RngStream misc = from_seed(55);
    RewardScreen s{};
    assemble_combat_rewards(rs, misc, RoomType::Monster,
                                RewardOutcome::KILLED, s);
    EXPECT_EQ(rs.card_blizz_randomizer, hand_pity);
    EXPECT_GE(rs.card_blizz_randomizer, -40);
    (void)expect;
}

TEST(RewardCards, DupeRerollConsumesExtraCardRngDraws) {
    // Find real seeds whose first reward needs at least one dupe re-roll; the
    // stream-state equality in the pity test above already proves the extra
    // draws are counted, so here we prove the loop is actually EXERCISED and
    // that the final offer still holds no duplicate.
    int exercised = 0;
    for (int64_t seed = 1; seed < 400 && exercised < 3; ++seed) {
        RunState rs = make_run(seed);
        RngStream hand_rng = rs.card_rng;
        int16_t hand_pity = rs.card_blizz_randomizer;
        const HandCardReward expect =
            hand_roll_cards(hand_rng, hand_pity, 3, RoomType::Monster);
        if (expect.dupe_rerolls == 0) {
            continue;
        }
        ++exercised;
        RngStream misc = from_seed(seed);
        RewardScreen s{};
        assemble_combat_rewards(rs, misc, RoomType::Monster,
                                RewardOutcome::KILLED, s);
        const int ci = find_kind(s, RewardItemKind::CARDS);
        ASSERT_GE(ci, 0);
        const RunRewardItem& item = s.items[ci];
        EXPECT_NE(item.card_ids[0], item.card_ids[1]);
        EXPECT_NE(item.card_ids[0], item.card_ids[2]);
        EXPECT_NE(item.card_ids[1], item.card_ids[2]);
        EXPECT_TRUE(streams_equal(rs.card_rng, hand_rng)) << "seed " << seed;
    }
    EXPECT_EQ(exercised, 3) << "no dupe-exercising seed found in range";
}

TEST(RewardCards, Trap13AndUpgradeBooleanAccounting) {
    // Trap 13: rollRarity(Random rng) IGNORES its parameter -- every reward
    // draw is cardRng (AbstractDungeon.java:1597-1598). And the Act-1 upgrade
    // pass draws ONE randomBoolean per NON-RARE card even though the chance is
    // 0.0f (:1470; Random.java:83-86); RARE cards short-circuit it. Exact
    // counter accounting: 3 rarity rolls + 3 picks + dupe re-rolls +
    // (non-rare count) booleans -- and no other run stream moves.
    for (int64_t seed : {21LL, 210LL, 2100LL}) {
        RunState rs = make_run(seed);
        RngStream hand_rng = rs.card_rng;
        int16_t hand_pity = rs.card_blizz_randomizer;
        const HandCardReward expect =
            hand_roll_cards(hand_rng, hand_pity, 3, RoomType::Monster);
        const int32_t before = rs.card_rng.counter;
        const RngStream monster_before = rs.monster_rng;
        const RngStream event_before = rs.event_rng;
        const RngStream merchant_before = rs.merchant_rng;
        RngStream misc = from_seed(seed);
        RewardScreen s{};
        assemble_combat_rewards(rs, misc, RoomType::Monster,
                                RewardOutcome::KILLED, s);
        int non_rare = 0;
        for (int j = 0; j < 3; ++j) {
            non_rare += expect.rarities[j] != RewardCardRarity::RARE ? 1 : 0;
        }
        EXPECT_EQ(rs.card_rng.counter,
                  before + 3 + 3 + expect.dupe_rerolls + non_rare)
            << "seed " << seed;
        EXPECT_TRUE(streams_equal(rs.monster_rng, monster_before));
        EXPECT_TRUE(streams_equal(rs.event_rng, event_before));
        EXPECT_TRUE(streams_equal(rs.merchant_rng, merchant_before));
    }
}

TEST(RewardCards, BossSkipsTheUpgradeBooleanEntirely) {
    // All three boss cards are RARE, so the upgrade pass draws NOTHING:
    // counter delta is exactly 3 rarity + 3 picks + dupes.
    RunState rs = make_run(31);
    RngStream hand_rng = rs.card_rng;
    int16_t hand_pity = rs.card_blizz_randomizer;
    const HandCardReward expect =
        hand_roll_cards(hand_rng, hand_pity, 3, RoomType::Boss);
    EXPECT_EQ(expect.upgrade_booleans, 0);
    const int32_t before = rs.card_rng.counter;
    RngStream misc = from_seed(31);
    RewardScreen s{};
    assemble_combat_rewards(rs, misc, RoomType::Boss,
                            RewardOutcome::KILLED, s);
    EXPECT_EQ(rs.card_rng.counter, before + 3 + 3 + expect.dupe_rerolls);
}

TEST(RewardCards, QuestionCardAddsAFourthBustedCrownRemovesTwo) {
    struct Case {
        std::initializer_list<RelicId> relics;
        int expected;
    };
    const Case cases[] = {
        {{RelicId::QUESTION_CARD}, 4},
        {{RelicId::BUSTED_CROWN}, 1},
        {{RelicId::QUESTION_CARD, RelicId::BUSTED_CROWN}, 2},
    };
    for (const Case& c : cases) {
        RunState rs = make_run(13);
        give_relics(rs, c.relics);
        RngStream hand_rng = rs.card_rng;
        int16_t hand_pity = rs.card_blizz_randomizer;
        const HandCardReward expect = hand_roll_cards(
            hand_rng, hand_pity, c.expected, RoomType::Monster);
        RngStream misc = from_seed(13);
        RewardScreen s{};
        assemble_combat_rewards(rs, misc, RoomType::Monster,
                                RewardOutcome::KILLED, s);
        const int ci = find_kind(s, RewardItemKind::CARDS);
        ASSERT_GE(ci, 0);
        EXPECT_EQ(s.items[ci].card_count, c.expected);
        for (int j = 0; j < c.expected; ++j) {
            EXPECT_EQ(s.items[ci].card_ids[j], expect.ids[j]);
        }
        EXPECT_TRUE(streams_equal(rs.card_rng, hand_rng));
    }
}

TEST(RewardCards, PrayerWheelAddsASecondCardRewardInPlainRoomsOnly) {
    {
        RunState rs = make_run(17);
        give_relics(rs, {RelicId::PRAYER_WHEEL});
        RngStream hand_rng = rs.card_rng;
        int16_t hand_pity = rs.card_blizz_randomizer;
        const HandCardReward first =
            hand_roll_cards(hand_rng, hand_pity, 3, RoomType::Monster);
        const HandCardReward second =
            hand_roll_cards(hand_rng, hand_pity, 3, RoomType::Monster);
        RngStream misc = from_seed(17);
        RewardScreen s{};
        assemble_combat_rewards(rs, misc, RoomType::Monster,
                                RewardOutcome::KILLED, s);
        EXPECT_EQ(count_kind(s, RewardItemKind::CARDS), 2);
        int seen = 0;
        for (uint8_t i = 0; i < s.count; ++i) {
            if (s.items[i].kind != static_cast<uint8_t>(RewardItemKind::CARDS)) {
                continue;
            }
            const HandCardReward& expect = ++seen == 1 ? first : second;
            for (int j = 0; j < 3; ++j) {
                EXPECT_EQ(s.items[i].card_ids[j], expect.ids[j]);
            }
        }
        EXPECT_TRUE(streams_equal(rs.card_rng, hand_rng));
    }
    {
        RunState rs = make_run(17);
        give_relics(rs, {RelicId::PRAYER_WHEEL});
        RngStream misc = from_seed(17);
        RewardScreen s{};
        assemble_combat_rewards(rs, misc, RoomType::Elite,
                                RewardOutcome::KILLED, s);
        EXPECT_EQ(count_kind(s, RewardItemKind::CARDS), 1)
            << "elite rooms get no Prayer Wheel bonus";
    }
}

// =============================================================================
// Smoke Bomb: consumes the battle-over draws, offers nothing
// =============================================================================

TEST(RewardSmoke, SmokeBombConsumesGoldRelicPotionDrawsButOffersNothing) {
    // The battle-over block runs for a smoked escape too -- gold roll, elite
    // relic tier + pool pop, the unconditional potion roll and its ratchet --
    // but openCombat(label, true) never calls setupItemReward
    // (CombatRewardScreen.java:267-289): no card roll, nothing claimable.
    RunState rs = make_run(23);
    const int16_t mod_before = rs.blizzard_potion_mod;
    const int32_t card_before = rs.card_rng.counter;
    const int32_t relic_draws_before = rs.relic_rng.counter;
    uint8_t pool_before = 0;
    for (int p = 0; p < kRelicTierCount; ++p) {
        pool_before = static_cast<uint8_t>(pool_before +
                                           rs.relic_pool_count[p]);
    }
    RngStream misc = from_seed(23);
    RewardScreen s{};
    assemble_combat_rewards(rs, misc, RoomType::Elite,
                            RewardOutcome::PLAYER_ESCAPED, s);
    EXPECT_EQ(s.count, 0);
    EXPECT_EQ(rs.treasure_rng.counter, 1);   // elite gold roll happened
    EXPECT_GE(rs.relic_rng.counter, relic_draws_before + 1);  // tier roll happened
    uint8_t pool_after = 0;
    for (int p = 0; p < kRelicTierCount; ++p) {
        pool_after = static_cast<uint8_t>(pool_after + rs.relic_pool_count[p]);
    }
    EXPECT_LT(pool_after, pool_before);      // the relic is popped and LOST
    EXPECT_GE(rs.potion_rng.counter, 1);     // unconditional potion roll
    EXPECT_NE(rs.blizzard_potion_mod, mod_before);  // ratchet moved
    EXPECT_EQ(rs.card_rng.counter, card_before)
        << "no card is rolled for a smoked escape";
}

// =============================================================================
// Claim flow: potions, the master-deck door, Singing Bowl, skip
// =============================================================================

// Assemble a normal-room reward guaranteed to hold a CARDS item.
RewardScreen assemble_normal(RunState& rs, int64_t seed) {
    RngStream misc = from_seed(seed);
    RewardScreen s{};
    assemble_combat_rewards(rs, misc, RoomType::Monster,
                                RewardOutcome::KILLED, s);
    return s;
}

TEST(RewardClaim, PotionNeedsAFreeSlotUnlessSozuDiscardsIt) {
    // Find a seed that drops a potion.
    for (int64_t seed = 1; seed < 200; ++seed) {
        RunState rs = make_run(seed);
        RewardScreen s = assemble_normal(rs, seed);
        const int pi = find_kind(s, RewardItemKind::POTION);
        if (pi < 0) {
            continue;
        }
        const uint16_t potion_id = s.items[pi].id;
        // (a) free slot: claim fills the FIRST empty slot.
        {
            RunState r2 = rs;
            RewardScreen s2 = s;
            RngStream misc = from_seed(1);
            ASSERT_TRUE(reward_claim_legal(r2, s2, static_cast<uint8_t>(pi)));
            ASSERT_TRUE(claim_reward(r2, misc, s2, static_cast<uint8_t>(pi)));
            EXPECT_EQ(r2.potions[0], potion_id);
        }
        // (b) all slots full (A20 has 2): the claim is illegal and a forced
        // call is a no-op that keeps the item.
        {
            RunState r2 = rs;
            for (uint8_t i = 0; i < r2.potion_slots; ++i) {
                r2.potions[i] = static_cast<uint16_t>(PotionId::BLOOD_POTION);
            }
            RewardScreen s2 = s;
            RngStream misc = from_seed(1);
            EXPECT_FALSE(reward_claim_legal(r2, s2, static_cast<uint8_t>(pi)));
            EXPECT_FALSE(claim_reward(r2, misc, s2, static_cast<uint8_t>(pi)));
            EXPECT_EQ(s2.count, s.count);
        }
        // (c) Sozu: claimable regardless, potion discarded
        // (RewardItem.java:276-279).
        {
            RunState r2 = rs;
            give_relics(r2, {RelicId::SOZU});
            for (uint8_t i = 0; i < r2.potion_slots; ++i) {
                r2.potions[i] = static_cast<uint16_t>(PotionId::BLOOD_POTION);
            }
            RewardScreen s2 = s;
            RngStream misc = from_seed(1);
            EXPECT_TRUE(reward_claim_legal(r2, s2, static_cast<uint8_t>(pi)));
            ASSERT_TRUE(claim_reward(r2, misc, s2, static_cast<uint8_t>(pi)));
            EXPECT_EQ(s2.count, s.count - 1);
            for (uint8_t i = 0; i < kPotionCap; ++i) {
                EXPECT_NE(r2.potions[i], potion_id);
            }
        }
        return;
    }
    FAIL() << "no potion-dropping seed found in range";
}

TEST(RewardClaim, TakeCardWalksTheDoorCeramicFishPaysNine) {
    // THE cheapest guard against a direct rs.master_deck[] write: obtaining a
    // reward card with Ceramic Fish equipped must pay out 9 gold through
    // dispatch_relics_on_obtain_card (run_deck.hpp).
    RunState rs = make_run(29);
    give_relics(rs, {RelicId::CERAMIC_FISH});
    RewardScreen s = assemble_normal(rs, 29);
    const int ci = find_kind(s, RewardItemKind::CARDS);
    ASSERT_GE(ci, 0);
    const uint16_t chosen = s.items[ci].card_ids[1];
    const uint16_t deck_before = rs.master_deck_count;
    const int32_t gold_before = rs.gold;
    RngStream misc = from_seed(1);
    ASSERT_TRUE(claim_reward(rs, misc, s, static_cast<uint8_t>(ci)));
    EXPECT_EQ(s.open_card_item, static_cast<uint8_t>(ci));
    ASSERT_TRUE(reward_take_card(rs, s, 1));
    EXPECT_EQ(rs.master_deck_count, deck_before + 1);
    EXPECT_EQ(rs.master_deck[deck_before].card_id, chosen);
    EXPECT_EQ(rs.gold, gold_before + 9) << "Ceramic Fish did not fire: the "
                                           "card bypassed the master-deck door";
    EXPECT_EQ(s.open_card_item, kNoOpenCardReward);
    EXPECT_EQ(find_kind(s, RewardItemKind::CARDS), -1);
}

TEST(RewardClaim, SingingBowlTradesTheCardsForTwoMaxHp) {
    RunState rs = make_run(37);
    give_relics(rs, {RelicId::SINGING_BOWL});
    rs.hp = 40;
    const int16_t max_before = rs.max_hp;
    RewardScreen s = assemble_normal(rs, 37);
    const int ci = find_kind(s, RewardItemKind::CARDS);
    ASSERT_GE(ci, 0);
    const uint16_t deck_before = rs.master_deck_count;
    RngStream misc = from_seed(1);
    ASSERT_TRUE(claim_reward(rs, misc, s, static_cast<uint8_t>(ci)));
    ASSERT_TRUE(reward_sing(rs, s));
    EXPECT_EQ(rs.max_hp, max_before + 2);
    EXPECT_EQ(rs.hp, 42);  // increaseMaxHp(2, true) also heals 2
    EXPECT_EQ(rs.master_deck_count, deck_before);
    EXPECT_EQ(find_kind(s, RewardItemKind::CARDS), -1);
    EXPECT_EQ(s.open_card_item, kNoOpenCardReward);
}

TEST(RewardClaim, SingWithoutTheBowlIsIllegal) {
    RunState rs = make_run(37);
    RewardScreen s = assemble_normal(rs, 37);
    const int ci = find_kind(s, RewardItemKind::CARDS);
    ASSERT_GE(ci, 0);
    RngStream misc = from_seed(1);
    ASSERT_TRUE(claim_reward(rs, misc, s, static_cast<uint8_t>(ci)));
    const int16_t max_before = rs.max_hp;
    EXPECT_FALSE(reward_sing(rs, s));
    EXPECT_EQ(rs.max_hp, max_before);
    EXPECT_EQ(s.open_card_item, static_cast<uint8_t>(ci));
}

TEST(RewardClaim, SkipKeepsTheCardItemClaimable) {
    RunState rs = make_run(41);
    RewardScreen s = assemble_normal(rs, 41);
    const int ci = find_kind(s, RewardItemKind::CARDS);
    ASSERT_GE(ci, 0);
    RngStream misc = from_seed(1);
    ASSERT_TRUE(claim_reward(rs, misc, s, static_cast<uint8_t>(ci)));
    reward_skip_card(s);
    EXPECT_EQ(s.open_card_item, kNoOpenCardReward);
    EXPECT_GE(find_kind(s, RewardItemKind::CARDS), 0) << "the item survives";
    // Re-opening offers the SAME cards (no re-roll on reopen).
    const RunRewardItem before = s.items[ci];
    ASSERT_TRUE(claim_reward(rs, misc, s, static_cast<uint8_t>(ci)));
    EXPECT_EQ(std::memcmp(&before, &s.items[ci], sizeof(before)), 0);
}

// =============================================================================
// Escape shapes: MONSTERS_ESCAPED and the STOLEN_GOLD return
// =============================================================================

TEST(RewardEscape, MonstersEscapedSuppressesGoldAndZeroesThePotionChance) {
    // haveMonstersEscaped gates the plain-monster gold roll (AbstractRoom.
    // java:319) and zeroes the potion CHANCE while the roll and +10 ratchet
    // still run (:585-607); the cards are STILL rolled and offered (the mugged
    // openCombat calls setupItemReward, CombatRewardScreen.java:280-285).
    RunState rs = make_run(41);
    RngStream misc = from_seed(4100);
    const RngStream misc_before = misc;
    const RngStream treasure_before = rs.treasure_rng;

    // Hand-accounting: exactly ONE potionRng draw (a guaranteed miss at
    // chance 0), and the card procedure of an ordinary kill.
    RngStream potion_copy = rs.potion_rng;
    (void)random(potion_copy, 0, 99);
    RngStream card_copy = rs.card_rng;
    int16_t pity = rs.card_blizz_randomizer;
    const HandCardReward cards =
        hand_roll_cards(card_copy, pity, 3, RoomType::Monster);

    RewardScreen s{};
    assemble_combat_rewards(rs, misc, RoomType::Monster,
                            RewardOutcome::MONSTERS_ESCAPED, s);
    EXPECT_EQ(count_kind(s, RewardItemKind::GOLD), 0);
    EXPECT_EQ(count_kind(s, RewardItemKind::POTION), 0);
    EXPECT_EQ(count_kind(s, RewardItemKind::STOLEN_GOLD), 0);
    ASSERT_EQ(count_kind(s, RewardItemKind::CARDS), 1);
    EXPECT_TRUE(streams_equal(rs.treasure_rng, treasure_before))
        << "the suppressed gold roll must not consume treasureRng";
    EXPECT_TRUE(streams_equal(rs.potion_rng, potion_copy));
    EXPECT_TRUE(streams_equal(rs.card_rng, card_copy));
    EXPECT_TRUE(streams_equal(misc, misc_before));
    EXPECT_EQ(rs.blizzard_potion_mod, 10);
    EXPECT_EQ(rs.card_blizz_randomizer, pity);
    const int ci = find_kind(s, RewardItemKind::CARDS);
    ASSERT_GE(ci, 0);
    for (int i = 0; i < cards.count; ++i) {
        EXPECT_EQ(s.items[ci].card_ids[i], cards.ids[i]);
    }
}

TEST(RewardEscape, WhiteBeastStatueOverridesTheEscapeZeroedChance) {
    // AbstractRoom.addPotionToRewards:594-596 runs AFTER the escape gate, so
    // White Beast Statue's forced 100 beats the zero: an all-escaped combat
    // still drops its potion (and the ratchet steps down).
    RunState rs = make_run(42);
    give_relics(rs, {RelicId::WHITE_BEAST_STATUE});
    RngStream misc = from_seed(4200);
    RewardScreen s{};
    assemble_combat_rewards(rs, misc, RoomType::Monster,
                            RewardOutcome::MONSTERS_ESCAPED, s);
    EXPECT_EQ(count_kind(s, RewardItemKind::POTION), 1);
    EXPECT_EQ(count_kind(s, RewardItemKind::GOLD), 0);
    EXPECT_EQ(rs.blizzard_potion_mod, -10);
}

TEST(RewardStolenGold, ReturnPrecedesGoldAndTakesNoGoldenIdolBonus) {
    // Looter.die() ran during combat, so its item is FIRST in the list
    // (AbstractRoom.java:619-626), and RewardItem's theft branch skips the
    // Golden Idol bonus (applyGoldBonus, RewardItem.java:104-129) that the
    // ordinary gold item on the same screen still receives.
    RunState rs = make_run(43);
    give_relics(rs, {RelicId::GOLDEN_IDOL});
    RngStream misc = from_seed(4300);
    RewardScreen s{};
    assemble_combat_rewards(rs, misc, RoomType::Monster, RewardOutcome::KILLED,
                            s, /*stolen_gold_return=*/40);
    ASSERT_GE(s.count, 2);
    EXPECT_EQ(s.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::STOLEN_GOLD));
    EXPECT_EQ(s.items[0].gold, 40);
    EXPECT_EQ(s.items[0].bonus_gold, 0)
        << "applyGoldBonus's theft branch takes no Golden Idol bonus";
    const int gi = find_kind(s, RewardItemKind::GOLD);
    ASSERT_GE(gi, 1);
    EXPECT_GT(s.items[gi].bonus_gold, 0)
        << "the ordinary gold item still gets the +25%";

    // Claim goes through the same gainGold door as GOLD (RewardItem.java:
    // 255-273).
    const int32_t before = rs.gold;
    ASSERT_TRUE(claim_reward(rs, misc, s, 0));
    EXPECT_EQ(rs.gold, before + 40);
    EXPECT_EQ(count_kind(s, RewardItemKind::STOLEN_GOLD), 0);
}

TEST(RewardStolenGold, PlayerEscapeDiscardsTheReturnUnclaimed) {
    // Kill the thief, then smoke-bomb: the smoked screen never calls
    // setupItemReward, so the room's reward list -- returned stolen gold
    // included -- is never shown; the battle-over draws are still consumed.
    RunState rs = make_run(44);
    const RngStream treasure_before = rs.treasure_rng;
    RngStream misc = from_seed(4400);
    RewardScreen s{};
    assemble_combat_rewards(rs, misc, RoomType::Monster,
                            RewardOutcome::PLAYER_ESCAPED, s,
                            /*stolen_gold_return=*/40);
    EXPECT_EQ(s.count, 0);
    EXPECT_FALSE(streams_equal(rs.treasure_rng, treasure_before))
        << "the battle-over gold draw is still consumed";
}

// =============================================================================
// Run-level integration: the CHOOSE claim flow end to end
// =============================================================================

int64_t find_first_monster_seed() {
    // Any seed works -- floor-1 rooms are always Monster (fixed row 0) -- but
    // pick one whose first encounter is the single Jaw Worm so the fight is
    // short and always winnable by the greedy driver.
    for (int64_t s = 1; s < 4000; ++s) {
        RngStream m = from_seed(s);
        MonsterLists ml{};
        generate_monster_lists(1, m, ml);
        if (ml.monster_list_count > 0 && ml.monster_list[0] == "Jaw Worm") {
            return s;
        }
    }
    ADD_FAILURE() << "no Jaw-Worm-first seed found in range";
    return 1;
}

void step(RunController& rc, Action a) {
    StepResult res{};
    advance(std::span<RunController>(&rc, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&res, 1));
}

void play_out_combat(RunController& rc) {
    StepResult res{};
    for (int i = 0; i < 800; ++i) {
        if (rc.phase != static_cast<uint8_t>(RunPhase::COMBAT)) {
            return;
        }
        RunActionMask m{};
        legal_actions(rc, m);
        Action a = make_action(ActionVerb::END_TURN);
        bool played = false;
        for (int h = 0; h < kHandCap && !played; ++h) {
            for (int t = 0; t < kMonsterCap; ++t) {
                if (m.combat.can_play_target[h][t]) {
                    a = make_action(ActionVerb::PLAY_CARD,
                                    static_cast<uint8_t>(h),
                                    static_cast<uint8_t>(t));
                    played = true;
                    break;
                }
            }
        }
        advance(std::span<RunController>(&rc, 1),
                std::span<const Action>(&a, 1),
                std::span<StepResult>(&res, 1));
    }
    ADD_FAILURE() << "combat did not terminate within the step cap";
}

TEST(RewardFlow, ChooseClaimsGoldAndCardThenProceeds) {
    const int64_t seed = find_first_monster_seed();
    RunController rc = run_begin(seed, kA20);
    step(rc, make_action(ActionVerb::CHOOSE));  // Neow -> map
    RunActionMask m{};
    legal_actions(rc, m);
    uint8_t x = 0;
    for (uint8_t c = 0; c < kMapCols; ++c) {
        if (m.can_choose_node[c]) {
            x = c;
            break;
        }
    }
    step(rc, make_action(ActionVerb::CHOOSE, x));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    play_out_combat(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    ASSERT_EQ(rc.combat_outcome, static_cast<uint8_t>(RunCombatOutcome::KILLED));

    // Claim the gold item through CHOOSE.
    legal_actions(rc, m);
    EXPECT_TRUE(m.can_proceed);
    const int gi = find_kind(rc.rewards, RewardItemKind::GOLD);
    ASSERT_GE(gi, 0);
    ASSERT_TRUE(m.can_claim_reward[gi]);
    const int32_t gold_before = rc.run.gold;
    const int32_t gold_amount = rc.rewards.items[gi].gold;
    step(rc, make_action(ActionVerb::CHOOSE, static_cast<uint8_t>(gi)));
    EXPECT_EQ(rc.run.gold, gold_before + gold_amount);

    // Open the card reward, take card 0 through the door.
    const int ci = find_kind(rc.rewards, RewardItemKind::CARDS);
    ASSERT_GE(ci, 0);
    const uint16_t chosen = rc.rewards.items[ci].card_ids[0];
    const uint16_t deck_before = rc.run.master_deck_count;
    step(rc, make_action(ActionVerb::CHOOSE, static_cast<uint8_t>(ci)));
    legal_actions(rc, m);
    EXPECT_FALSE(m.can_proceed) << "proceed hides while the pick screen is up";
    ASSERT_TRUE(m.can_take_card[0]);
    EXPECT_TRUE(m.can_skip_card);
    EXPECT_FALSE(m.can_sing);
    step(rc, make_action(ActionVerb::CHOOSE, 0));
    EXPECT_EQ(rc.run.master_deck_count, deck_before + 1);
    EXPECT_EQ(rc.run.master_deck[deck_before].card_id, chosen);

    // Proceed; the screen clears and the map returns.
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(rc.rewards.count, 0);
}

}  // namespace
}  // namespace sts::engine
