#include <gtest/gtest.h>

#include <cmath>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include "sts/dist_check/s2_expect.hpp"
#include "sts/dist_check/stats.hpp"

namespace {
using sts::dist_check::ChiSquareResult;
using sts::dist_check::chi_square;
using sts::dist_check::holm_bonferroni;
using sts::registry::EncounterPool;
namespace s2 = sts::dist_check::s2;

TEST(DistCheckStats, ExactFitHasUnitSurvivalProbability) {
    const auto r = chi_square("fit", {25, 50, 25}, {0.25, 0.5, 0.25});
    EXPECT_DOUBLE_EQ(r.statistic, 0.0);
    EXPECT_DOUBLE_EQ(r.p_value, 1.0);
    EXPECT_EQ(r.degrees_of_freedom, 2);
    EXPECT_EQ(r.sample_count, 100u);
}

TEST(DistCheckStats, ChiSquareSurvivalMatchesKnownOneDegreeValue) {
    // Two supported cells give one degree of freedom. At x=4 the survival
    // probability is erfc(sqrt(2)).
    const auto r = chi_square("known", {40, 60}, {0.5, 0.5});
    EXPECT_NEAR(r.statistic, 4.0, 1.0e-12);
    EXPECT_NEAR(r.p_value, 0.0455002638963584, 1.0e-12);
}

TEST(DistCheckStats, ImpossibleObservationIsAnExactFailure) {
    const auto r = chi_square("support", {9, 1}, {1.0, 0.0});
    EXPECT_EQ(r.p_value, 0.0);
}

TEST(DistCheckStats, HolmIsStepDownAndRetainsEverythingAfterFirstRetention) {
    const std::vector<ChiSquareResult> tests = {
        {"a", 0.0, 1, 0.001, 100},
        {"b", 0.0, 1, 0.006, 100},
        {"c", 0.0, 1, 0.007, 100},
    };
    const auto decisions = holm_bonferroni(tests, 0.01);
    ASSERT_EQ(decisions.size(), 3u);
    EXPECT_TRUE(decisions[0].rejected);   // .001 <= .01/3
    EXPECT_FALSE(decisions[1].rejected);  // .006 > .01/2
    EXPECT_FALSE(decisions[2].rejected);  // retained by step-down rule
}

// --- S2.44: the pre-registered replicate-before-flagging rule ---------------
//
// The rule is protocol, so it is pinned here rather than merely printed by the
// campaign: a stage-one retention must be FINAL (and must not even consult the
// replicate), a stage-one rejection must survive a second rejection at the same
// per-row threshold to stand, and a replicate that retains must downgrade the
// verdict while keeping both p-values.

using sts::dist_check::HolmDecision;
using sts::dist_check::confirm_by_replicate;

TEST(HolmReplicate, Stage1RetentionIsFinalAndNeverRunsTheReplicate) {
    const std::vector<HolmDecision> stage_one = {
        {"retained", 0.4, 0.005, false},
    };
    int calls = 0;
    const auto verdicts = confirm_by_replicate(
        stage_one, [&](const std::string&) {
            ++calls;
            return 0.0;  // would reject if it were ever consulted
        });
    EXPECT_EQ(calls, 0);
    ASSERT_EQ(verdicts.size(), 1u);
    EXPECT_FALSE(verdicts[0].replicated);
    EXPECT_FALSE(verdicts[0].rejected);
    EXPECT_DOUBLE_EQ(verdicts[0].stage_one_p, 0.4);
}

TEST(HolmReplicate, Stage1RejectionRetainedByTheReplicateIsDowngraded) {
    // The 2026-08-10 act3_weak_pair shape: an alpha-tail stage-one hit that a
    // fresh seed block does not reproduce.
    const std::vector<HolmDecision> stage_one = {
        {"alpha_tail", 6.750359e-04, 7.692308e-04, true},
    };
    const auto verdicts = confirm_by_replicate(
        stage_one, [](const std::string&) { return 0.42; });
    ASSERT_EQ(verdicts.size(), 1u);
    EXPECT_TRUE(verdicts[0].replicated);
    EXPECT_FALSE(verdicts[0].rejected);
    EXPECT_DOUBLE_EQ(verdicts[0].stage_one_p, 6.750359e-04);
    EXPECT_DOUBLE_EQ(verdicts[0].replicate_p, 0.42);
    EXPECT_DOUBLE_EQ(verdicts[0].threshold, 7.692308e-04);
}

TEST(HolmReplicate, RejectionInBothStagesStaysRejected) {
    const std::vector<HolmDecision> stage_one = {
        {"real_bias", 1.0e-9, 7.692308e-04, true},
    };
    const auto verdicts = confirm_by_replicate(
        stage_one, [](const std::string&) { return 1.0e-11; });
    ASSERT_EQ(verdicts.size(), 1u);
    EXPECT_TRUE(verdicts[0].replicated);
    EXPECT_TRUE(verdicts[0].rejected);
}

TEST(HolmReplicate, TheReplicateIsJudgedAtTheSamePerRowThreshold) {
    // Exactly at the threshold rejects, one ulp above it does not -- the same
    // `<=` Holm itself uses, so the two stages cannot disagree on the boundary.
    const std::vector<HolmDecision> stage_one = {
        {"at_threshold", 1.0e-4, 5.0e-4, true},
        {"just_above", 1.0e-4, 5.0e-4, true},
    };
    const auto verdicts = confirm_by_replicate(
        stage_one, [](const std::string& name) {
            return name == "at_threshold" ? 5.0e-4
                                          : std::nextafter(5.0e-4, 1.0);
        });
    ASSERT_EQ(verdicts.size(), 2u);
    EXPECT_TRUE(verdicts[0].rejected);
    EXPECT_FALSE(verdicts[1].rejected);
    EXPECT_TRUE(verdicts[1].replicated);
}

TEST(HolmReplicate, OnlyTheRejectedRowsOfAMixedFamilyAreReplicated) {
    const std::vector<HolmDecision> stage_one = {
        {"a", 1.0e-6, 3.333e-03, true},
        {"b", 0.20, 5.000e-03, false},
        {"c", 1.0e-5, 1.000e-02, true},
    };
    std::vector<std::string> consulted;
    const auto verdicts = confirm_by_replicate(
        stage_one, [&](const std::string& name) {
            consulted.push_back(name);
            return 1.0e-8;
        });
    EXPECT_EQ(consulted, (std::vector<std::string>{"a", "c"}));
    ASSERT_EQ(verdicts.size(), 3u);
    EXPECT_TRUE(verdicts[0].rejected);
    EXPECT_FALSE(verdicts[1].rejected);
    EXPECT_FALSE(verdicts[1].replicated);
    EXPECT_TRUE(verdicts[2].rejected);
}

// --- S2.44: the tier-4 S2 family's analytic expectations --------------------
//
// A wrong EXPECTATION and an engine defect are indistinguishable on a campaign
// report line, so every law the S2 family scores against is pinned here against
// numbers derived by hand from the cited Java rules over the registry rows.

std::size_t key_index(const std::vector<s2::PoolRow>& rows,
                      std::string_view key) {
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].key == key) return i;
    }
    ADD_FAILURE() << "pool row not found: " << key;
    return 0;
}

double law_sum(const std::vector<double>& law) {
    return std::accumulate(law.begin(), law.end(), 0.0);
}

TEST(DistCheckS2Expect, ActTwoPoolRowsCarryTheCityWeights) {
    const auto weak = s2::pool_rows(2, EncounterPool::WEAK);
    ASSERT_EQ(weak.size(), 5u);
    for (const auto& row : weak) EXPECT_DOUBLE_EQ(row.weight, 2.0);

    const auto strong = s2::pool_rows(2, EncounterPool::STRONG);
    ASSERT_EQ(strong.size(), 8u);
    EXPECT_DOUBLE_EQ(law_sum({strong[0].weight, strong[1].weight,
                              strong[2].weight, strong[3].weight,
                              strong[4].weight, strong[5].weight,
                              strong[6].weight, strong[7].weight}),
                     29.0);
    EXPECT_DOUBLE_EQ(strong[key_index(strong, "Snake Plant")].weight, 6.0);
}

TEST(DistCheckS2Expect, PoolRollLawNormalizesOverTheSurvivors) {
    const auto strong = s2::pool_rows(2, EncounterPool::STRONG);
    const auto plain = s2::pool_roll_law(strong, {});
    EXPECT_DOUBLE_EQ(plain[key_index(strong, "Snake Plant")], 6.0 / 29.0);
    EXPECT_NEAR(law_sum(plain), 1.0, 1.0e-12);

    // TheCity.java:144-148 -- the game's only two-key exclusion.
    const std::vector<std::string_view> chosen =
        s2::weak_exclusions(2, "Chosen");
    ASSERT_EQ(chosen.size(), 2u);
    const auto conditioned = s2::pool_roll_law(strong, chosen);
    EXPECT_DOUBLE_EQ(conditioned[key_index(strong, "Chosen and Byrds")], 0.0);
    EXPECT_DOUBLE_EQ(conditioned[key_index(strong, "Cultist and Chosen")], 0.0);
    EXPECT_DOUBLE_EQ(conditioned[key_index(strong, "Snake Plant")],
                     6.0 / 24.0);
    EXPECT_NEAR(law_sum(conditioned), 1.0, 1.0e-12);
}

TEST(DistCheckS2Expect, ConsecutivePairLawForbidsTheImmediateRepeat) {
    const auto weak = s2::pool_rows(2, EncounterPool::WEAK);
    const auto law = s2::consecutive_pair_law(weak);
    ASSERT_EQ(law.size(), 25u);
    for (std::size_t i = 0; i < 5; ++i) {
        for (std::size_t j = 0; j < 5; ++j) {
            if (i == j) {
                EXPECT_DOUBLE_EQ(law[i * 5 + j], 0.0);
            } else {
                EXPECT_DOUBLE_EQ(law[i * 5 + j], 1.0 / 20.0);
            }
        }
    }
    EXPECT_NEAR(law_sum(law), 1.0, 1.0e-12);
}

TEST(DistCheckS2Expect, ActTwoFirstStrongJointZeroesTheExcludedPairs) {
    const auto weak = s2::pool_rows(2, EncounterPool::WEAK);
    const auto strong = s2::pool_rows(2, EncounterPool::STRONG);
    const auto law = s2::first_strong_joint_law(2);
    ASSERT_EQ(law.size(), weak.size() * strong.size());
    const std::size_t ns = strong.size();
    const auto cell = [&](std::string_view w, std::string_view s) {
        return law[key_index(weak, w) * ns + key_index(strong, s)];
    };
    EXPECT_DOUBLE_EQ(cell("Chosen", "Chosen and Byrds"), 0.0);
    EXPECT_DOUBLE_EQ(cell("Chosen", "Cultist and Chosen"), 0.0);
    EXPECT_DOUBLE_EQ(cell("Spheric Guardian", "Sentry and Sphere"), 0.0);
    EXPECT_DOUBLE_EQ(cell("3 Byrds", "Chosen and Byrds"), 0.0);
    // Chosen: 1/5 of the last-weak mass, renormalized over 29 - 2 - 3 = 24.
    EXPECT_DOUBLE_EQ(cell("Chosen", "Snake Plant"), 0.2 * 6.0 / 24.0);
    // Shell Parasite excludes nothing, so the full 29 stands.
    EXPECT_DOUBLE_EQ(cell("Shell Parasite", "Snake Plant"), 0.2 * 6.0 / 29.0);
    EXPECT_NEAR(law_sum(law), 1.0, 1.0e-12);
}

TEST(DistCheckS2Expect, ActThreeFirstStrongHandlesSelfExclusionAndTheInertOne) {
    const auto weak = s2::pool_rows(3, EncounterPool::WEAK);
    const auto strong = s2::pool_rows(3, EncounterPool::STRONG);
    ASSERT_EQ(weak.size(), 3u);
    ASSERT_EQ(strong.size(), 8u);
    const auto law = s2::first_strong_joint_law(3);
    const std::size_t ns = strong.size();
    const auto cell = [&](std::string_view w, std::string_view s) {
        return law[key_index(weak, w) * ns + key_index(strong, s)];
    };
    // TheBeyond.java:131-134 -- "3 Darklings" is in BOTH pools and excludes
    // itself, so the pair is impossible and the rest renormalize over 7.
    EXPECT_DOUBLE_EQ(cell("3 Darklings", "3 Darklings"), 0.0);
    EXPECT_DOUBLE_EQ(cell("3 Darklings", "Maw"), (1.0 / 3.0) * (1.0 / 7.0));
    EXPECT_DOUBLE_EQ(cell("3 Shapes", "4 Shapes"), 0.0);
    // TheBeyond.java:135-138 -- "Orb Walker" excludes its OWN weak-only key,
    // which the first-strong loop can never roll: the exclusion is INERT and
    // its row must stay a full uniform eighth.
    EXPECT_DOUBLE_EQ(cell("Orb Walker", "Maw"), (1.0 / 3.0) * (1.0 / 8.0));
    EXPECT_DOUBLE_EQ(cell("Orb Walker", "3 Darklings"),
                     (1.0 / 3.0) * (1.0 / 8.0));
    EXPECT_NEAR(law_sum(law), 1.0, 1.0e-12);
}

TEST(DistCheckS2Expect, FrontScanLawMatchesTheHandCountedBossChestCase) {
    // The act-2 boss chest with Burning Blood held: 22 boss rows, 2 canSpawn-
    // gated, 3 offers popped from the front. The 20 spawnable rows leave 21
    // gaps; the two blocked rows land in a uniform multiset of them, and k are
    // consumed exactly when k fall in the first three gaps -- 171 / 54 / 6 of
    // the 231 multisets.
    const auto law = s2::front_scan_blocked_law(20, 2, 3);
    ASSERT_EQ(law.size(), 3u);
    EXPECT_DOUBLE_EQ(law[0], 171.0 / 231.0);
    EXPECT_DOUBLE_EQ(law[1], 54.0 / 231.0);
    EXPECT_DOUBLE_EQ(law[2], 6.0 / 231.0);
    EXPECT_NEAR(law_sum(law), 1.0, 1.0e-12);
}

TEST(DistCheckS2Expect, FrontScanLawIsDegenerateWithNothingBlocked) {
    const auto law = s2::front_scan_blocked_law(22, 0, 3);
    ASSERT_EQ(law.size(), 1u);
    EXPECT_DOUBLE_EQ(law[0], 1.0);
}

}  // namespace
