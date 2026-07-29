#include <gtest/gtest.h>

#include <vector>

#include "sts/dist_check/stats.hpp"

namespace {
using sts::dist_check::ChiSquareResult;
using sts::dist_check::chi_square;
using sts::dist_check::holm_bonferroni;

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

}  // namespace
