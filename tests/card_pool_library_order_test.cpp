// Card-pool LIBRARY ORDER: the order the game's card pools are actually in, and
// the live-capture evidence that pins it.
//
// The pools were emitted in registry-id order for a long time, as a documented
// interim deviation: the RNG draw COUNT and the pool MEMBERSHIP were Java-exact,
// but which id a given index named was not, because the game fills each pool by
// walking `CardLibrary.cards` -- a `HashMap<String, AbstractCard>` -- and no
// capture had pinned that walk.
//
// It turns out not to need pinning by observation at all: a Java HashMap's
// iteration order is a pure function of the keys, the final capacity and
// insertion order, so the generator computes it (see `library_order_key` in
// tools/registry_gen/stsgen/emit/cards.py). The oracle capture is what CHECKS
// that computation, and the checks live here.
//
// Two distinct orders come out of it, and mixing them up is the trap this file
// exists to catch:
//
//   * The REWARD pools (`commonCardPool` / `uncommonCardPool` / `rareCardPool`,
//     read by `getCard(rarity)`) are in plain library order. `addToTop` is
//     `group.add(c)` -- an APPEND, despite the name (CardGroup.java:455-457).
//   * The COMBAT pools (everything reading a `src*CardPool`) are the same
//     content REVERSED per rarity, because `initializeCardPools` copies each
//     pool into its `src*` twin with `addToBottom`, which is `group.add(0, c)`
//     -- a PREPEND (CardGroup.java:459-461) -- and then concatenates
//     common ++ uncommon ++ rare (AbstractDungeon.java:944-978).
//
// THE EVIDENCE. Campaigns `b45_rewards_oracle_20260727T204809Z_claude01` and
// `b45_rewards_oracle2_...` (seeds STS00042/43/48/49/51/52, A20 Ironclad)
// carry 9 CARD_REWARD screens whose offered card ids the game printed. Replaying
// each screen's assembly from the captured pre-battle-over RunState reproduces
// the simulator's own draw indices; those indices against these tables name the
// 27 cards below. Under the previous registry-id order, 0 of the 27 matched.
// The replay is `tools/oracle_bridge/replay/replay_run_diff`; these rows are its
// read-out, frozen so the tables cannot drift back.

#include <gtest/gtest.h>

#include "sts/engine/card_pools.hpp"
#include "sts/engine/cards.hpp"
#include "sts/registry/card_table.hpp"

namespace sts::engine {
namespace {

using sts::registry::kColorlessCombatPool;
using sts::registry::kColorlessCombatPoolCount;
using sts::registry::kIroncladAttackPool;
using sts::registry::kIroncladAttackPoolCount;
using sts::registry::kIroncladCombatPool;
using sts::registry::kIroncladCombatPoolCount;
using sts::registry::kIroncladCommonPool;
using sts::registry::kIroncladCommonPoolCount;
using sts::registry::kIroncladRarePool;
using sts::registry::kIroncladRarePoolCount;
using sts::registry::kIroncladSkillPool;
using sts::registry::kIroncladSkillPoolCount;
using sts::registry::kIroncladTrulyRandomPool;
using sts::registry::kIroncladTrulyRandomPoolCount;
using sts::registry::kIroncladUncommonPool;
using sts::registry::kIroncladUncommonPoolCount;
using sts::registry::kPoolableCurses;
using sts::registry::kPoolableCurseCount;

// One recovered (pool, index) -> identity constraint from a captured offer.
struct OfferPin {
    const char* screen;   // seed + floor the offer was captured on
    int index;            // the simulator's stream-exact draw index
    CardId expected;      // the id the live game offered at that index
};

TEST(CardPoolLibraryOrder, CapturedCommonOffersNameTheseIndices) {
    // Every COMMON identity across the nine captured CARD_REWARD screens.
    static constexpr OfferPin kPins[] = {
        {"STS00042 floor 1", 18, CardId::PERFECTED_STRIKE},
        {"STS00042 floor 1", 14, CardId::HAVOC},
        {"STS00042 floor 1", 10, CardId::POMMEL_STRIKE},
        {"STS00042 floor 4", 15, CardId::HEADBUTT},
        {"STS00042 floor 4", 2, CardId::WARCRY},
        {"STS00042 floor 5", 14, CardId::HAVOC},
        {"STS00042 floor 5", 2, CardId::WARCRY},
        {"STS00043 floor 1", 4, CardId::IRON_WAVE},
        {"STS00043 floor 1", 11, CardId::TWIN_STRIKE},
        {"STS00051 floor 1", 18, CardId::PERFECTED_STRIKE},
        {"STS00052 floor 1", 6, CardId::TRUE_GRIT},
        {"STS00052 floor 1", 15, CardId::HEADBUTT},
        {"STS00052 floor 4", 1, CardId::CLEAVE},
        {"STS00052 floor 4", 13, CardId::ARMAMENTS},
        {"STS00052 floor 4", 15, CardId::HEADBUTT},
    };
    for (const OfferPin& p : kPins) {
        ASSERT_LT(p.index, kIroncladCommonPoolCount) << p.screen;
        EXPECT_EQ(kIroncladCommonPool[static_cast<unsigned>(p.index)], p.expected)
            << "captured on " << p.screen << " at common-pool index " << p.index;
    }
}

TEST(CardPoolLibraryOrder, CapturedUncommonOffersNameTheseIndices) {
    static constexpr OfferPin kPins[] = {
        {"STS00042 floor 4", 28, CardId::RUPTURE},
        {"STS00042 floor 5", 23, CardId::ENTRENCH},
        {"STS00043 floor 1", 11, CardId::BURNING_PACT},
        {"STS00043 floor 2", 24, CardId::SENTINEL},
        {"STS00043 floor 2", 20, CardId::DISARM},
        {"STS00043 floor 2", 25, CardId::BATTLE_TRANCE},
        {"STS00049 floor 1", 14, CardId::RAMPAGE},
        {"STS00049 floor 1", 15, CardId::SEVER_SOUL},
        {"STS00049 floor 1", 9, CardId::FLAME_BARRIER},
        {"STS00051 floor 1", 5, CardId::RECKLESS_CHARGE},
        {"STS00051 floor 1", 25, CardId::BATTLE_TRANCE},
        {"STS00052 floor 1", 27, CardId::SECOND_WIND},
    };
    for (const OfferPin& p : kPins) {
        ASSERT_LT(p.index, kIroncladUncommonPoolCount) << p.screen;
        EXPECT_EQ(kIroncladUncommonPool[static_cast<unsigned>(p.index)], p.expected)
            << "captured on " << p.screen << " at uncommon-pool index " << p.index;
    }
}

// The one curse identity the capture pins. CardLibrary.getCurse
// (CardLibrary.java:1043-1050) walks the SEPARATE `curses` map, whose 14 entries
// settle at capacity 32 rather than the card map's 512 -- so this is an
// independent confirmation of the rule at a second capacity. STS00048's Neow
// "obtain a curse" drew index 2 and the game handed over Shame.
TEST(CardPoolLibraryOrder, CapturedCurseOfferNamesIndexTwo) {
    ASSERT_EQ(kPoolableCurseCount, 10);
    EXPECT_EQ(kPoolableCurses[2], CardId::SHAME);
}

// The head and tail of each reward pool, i.e. the ends of the raw HashMap walk.
// Cheap, and it fails loudly if the capacity constant or the tiebreak is ever
// disturbed without the offer pins happening to notice.
TEST(CardPoolLibraryOrder, RewardPoolEndpoints) {
    ASSERT_EQ(kIroncladCommonPoolCount, 20);
    EXPECT_EQ(kIroncladCommonPool[0], CardId::ANGER);
    EXPECT_EQ(kIroncladCommonPool[19], CardId::SWORD_BOOMERANG);
    ASSERT_EQ(kIroncladUncommonPoolCount, 36);
    EXPECT_EQ(kIroncladUncommonPool[0], CardId::SPOT_WEAKNESS);
    EXPECT_EQ(kIroncladUncommonPool[35], CardId::EVOLVE);
    ASSERT_EQ(kIroncladRarePoolCount, 16);
    EXPECT_EQ(kIroncladRarePool[0], CardId::IMMOLATE);
    EXPECT_EQ(kIroncladRarePool[15], CardId::DOUBLE_TAP);
}

// The src-pool relationship, asserted structurally rather than by transcribing
// a second list: the combat pool must be the three reward pools, each reversed,
// concatenated common-then-uncommon-then-rare, with the HEALING rows dropped.
TEST(CardPoolLibraryOrder, CombatPoolIsTheReversedRarityMajorConcatenation) {
    std::vector<CardId> want;
    const std::array<std::pair<const CardId*, int>, 3> tiers{{
        {kIroncladCommonPool.data(), kIroncladCommonPoolCount},
        {kIroncladUncommonPool.data(), kIroncladUncommonPoolCount},
        {kIroncladRarePool.data(), kIroncladRarePoolCount},
    }};
    for (const auto& [data, count] : tiers) {
        for (int i = count - 1; i >= 0; --i) {
            const CardId id = data[static_cast<unsigned>(i)];
            // returnTrulyRandomCardInCombat drops the two HEALING rows.
            if (id == CardId::FEED || id == CardId::REAPER) continue;
            want.push_back(id);
        }
    }
    ASSERT_EQ(static_cast<int>(want.size()), kIroncladCombatPoolCount);
    for (int i = 0; i < kIroncladCombatPoolCount; ++i) {
        EXPECT_EQ(kIroncladCombatPool[static_cast<unsigned>(i)],
                  want[static_cast<std::size_t>(i)])
            << "combat-pool position " << i;
    }
}

// Pandora's Box's list -- AbstractDungeon.returnTrulyRandomCard() (:936-942) --
// is the UNFILTERED src-pool concatenation: the same reversed rarity-major
// order as the combat pool, with NOTHING dropped. Asserted two ways so the two
// emitted lists cannot drift apart: (1) structurally against the three reward
// pools, exactly like the combat pool's own pin above; (2) as a containment --
// the combat pool must be precisely this list minus the two HEALING rows (Feed
// and Reaper, both RED RARE), in order.
TEST(CardPoolLibraryOrder, CombatPoolIsTheHealingFilteredTrulyRandomSubsequence) {
    std::vector<CardId> want;
    const std::array<std::pair<const CardId*, int>, 3> tiers{{
        {kIroncladCommonPool.data(), kIroncladCommonPoolCount},
        {kIroncladUncommonPool.data(), kIroncladUncommonPoolCount},
        {kIroncladRarePool.data(), kIroncladRarePoolCount},
    }};
    for (const auto& [data, count] : tiers) {
        for (int i = count - 1; i >= 0; --i) {
            want.push_back(data[static_cast<unsigned>(i)]);
        }
    }
    ASSERT_EQ(static_cast<int>(want.size()), kIroncladTrulyRandomPoolCount);
    for (int i = 0; i < kIroncladTrulyRandomPoolCount; ++i) {
        EXPECT_EQ(kIroncladTrulyRandomPool[static_cast<unsigned>(i)],
                  want[static_cast<std::size_t>(i)])
            << "truly-random-pool position " << i;
    }

    // 20 + 36 + 16 = 72; minus Feed and Reaper = 70 (dossier arithmetic pin).
    ASSERT_EQ(kIroncladTrulyRandomPoolCount, kIroncladCombatPoolCount + 2);
    int j = 0;
    for (int i = 0; i < kIroncladTrulyRandomPoolCount; ++i) {
        const CardId id = kIroncladTrulyRandomPool[static_cast<unsigned>(i)];
        if (id == CardId::FEED || id == CardId::REAPER) continue;
        ASSERT_LT(j, kIroncladCombatPoolCount);
        EXPECT_EQ(kIroncladCombatPool[static_cast<unsigned>(j)], id)
            << "combat-pool position " << j;
        ++j;
    }
    EXPECT_EQ(j, kIroncladCombatPoolCount);
}

// transformCard's list is a THIRD shape, and it is the one that mixes the two
// orders above -- the trap this file exists to catch, in its purest form.
// returnTrulyRandomCardFromAvailable (AbstractDungeon.java:1016-1045) reads
// `commonCardPool` -- the LIVE pool, plain library order -- and then
// `srcUncommonCardPool` and `srcRareCardPool`, the PREPEND-filled copies
// (:1180-1199, CardGroup.java:459-461), which hold their rarity's library order
// REVERSED. Walking all three forwards is right for the first block and wrong
// for the other two.
//
// Pinned at the three block boundaries, seed-free, against the raw emitted
// arrays -- the same shape as NeowGrid.TransformReadsTheSrcPoolsBackwards, but
// on the engine's own list builder rather than a test transcription of it. Both
// event grids (Living Wall, Transmorgrifier) and both Neow transform payouts go
// through this one function, so this is the only place the order is stated.
TEST(CardPoolLibraryOrder, TransformCardListIsCommonsThenBothSrcPoolsBackwards) {
    // DEFEND is BASIC: in none of the three pools, so nothing is excluded and
    // the list is the whole concatenation.
    CardId list[kTransformCardListCap]{};
    const int n = transform_card_list(CardId::DEFEND, list);
    ASSERT_EQ(n, kIroncladCommonPoolCount + kIroncladUncommonPoolCount +
                     kIroncladRarePoolCount);

    // Block 1 -- commonCardPool, FORWARDS.
    EXPECT_EQ(list[0], kIroncladCommonPool[0]);
    EXPECT_EQ(list[kIroncladCommonPoolCount - 1],
              kIroncladCommonPool[kIroncladCommonPoolCount - 1]);
    // Block 2 -- srcUncommonCardPool, BACKWARDS.
    EXPECT_EQ(list[kIroncladCommonPoolCount],
              kIroncladUncommonPool[kIroncladUncommonPoolCount - 1]);
    EXPECT_EQ(list[kIroncladCommonPoolCount + kIroncladUncommonPoolCount - 1],
              kIroncladUncommonPool[0]);
    // Block 3 -- srcRareCardPool, BACKWARDS.
    EXPECT_EQ(list[kIroncladCommonPoolCount + kIroncladUncommonPoolCount],
              kIroncladRarePool[kIroncladRarePoolCount - 1]);
    EXPECT_EQ(list[n - 1], kIroncladRarePool[0]);

    // ...and every position in between, so a same-endpoint reshuffle cannot
    // slip through.
    int at = 0;
    for (int i = 0; i < kIroncladCommonPoolCount; ++i, ++at) {
        EXPECT_EQ(list[at], kIroncladCommonPool[static_cast<unsigned>(i)])
            << "common block position " << i;
    }
    for (int i = kIroncladUncommonPoolCount - 1; i >= 0; --i, ++at) {
        EXPECT_EQ(list[at], kIroncladUncommonPool[static_cast<unsigned>(i)])
            << "uncommon block, src index " << i;
    }
    for (int i = kIroncladRarePoolCount - 1; i >= 0; --i, ++at) {
        EXPECT_EQ(list[at], kIroncladRarePool[static_cast<unsigned>(i)])
            << "rare block, src index " << i;
    }
}

// The B3.11 invariant, re-checked under the new order: the type-filtered pools
// are in-order subsequences of the full combat pool. This is what lets the
// generator emit them separately without them being able to drift apart.
TEST(CardPoolLibraryOrder, TypeFilteredPoolsRemainSubsequences) {
    for (const auto& [pool, count] :
         std::array<std::pair<const CardId*, int>, 2>{{
             {kIroncladAttackPool.data(), kIroncladAttackPoolCount},
             {kIroncladSkillPool.data(), kIroncladSkillPoolCount}}}) {
        int j = 0;
        for (int i = 0; i < count; ++i) {
            while (j < kIroncladCombatPoolCount &&
                   kIroncladCombatPool[static_cast<unsigned>(j)] !=
                       pool[static_cast<unsigned>(i)]) {
                ++j;
            }
            ASSERT_LT(j, kIroncladCombatPoolCount)
                << "filtered pool entry " << i << " is out of order or absent";
            ++j;
        }
    }
}

// Membership is what the interim order never put at risk, so it must not change
// now that the order has: every filtered pool is still a set-equal view of the
// combat pool restricted to its type, and the colorless combat pool still holds
// its own membership.
TEST(CardPoolLibraryOrder, MembershipIsUnchangedByTheReorder) {
    int attacks = 0;
    int skills = 0;
    for (int i = 0; i < kIroncladCombatPoolCount; ++i) {
        const CardDef* d = card_def(kIroncladCombatPool[static_cast<unsigned>(i)]);
        ASSERT_NE(d, nullptr);
        if (d->type == CardType::ATTACK) ++attacks;
        if (d->type == CardType::SKILL) ++skills;
    }
    EXPECT_EQ(attacks, kIroncladAttackPoolCount);
    EXPECT_EQ(skills, kIroncladSkillPoolCount);
    EXPECT_GT(kColorlessCombatPoolCount, 0);
    for (int i = 0; i < kColorlessCombatPoolCount; ++i) {
        EXPECT_NE(card_def(kColorlessCombatPool[static_cast<unsigned>(i)]), nullptr);
    }
}

}  // namespace
}  // namespace sts::engine
