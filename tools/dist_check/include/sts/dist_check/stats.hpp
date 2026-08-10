#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sts::dist_check {

struct ChiSquareResult {
    std::string name;
    double statistic = 0.0;
    int degrees_of_freedom = 0;
    double p_value = 1.0;
    uint64_t sample_count = 0;
};

// Pearson's chi-square goodness-of-fit test. Zero-probability cells are exact
// support assertions: observing one returns p=0. Other zero-expectation cells
// are omitted from the statistic and degrees of freedom.
[[nodiscard]] ChiSquareResult chi_square(
    std::string name, const std::vector<uint64_t>& observed,
    const std::vector<double>& probabilities);

struct HolmDecision {
    std::string name;
    double p_value = 1.0;
    double threshold = 0.0;
    bool rejected = false;
};

// Holm-Bonferroni step-down control of family-wise error. Results are returned
// in ascending p-value order. Once one hypothesis is retained, every later
// hypothesis is retained as required by the procedure.
[[nodiscard]] std::vector<HolmDecision> holm_bonferroni(
    const std::vector<ChiSquareResult>& tests, double family_alpha);

// --- Replicate-before-flagging (S2.44) --------------------------------------

struct ReplicateDecision {
    std::string name;
    double stage_one_p = 1.0;
    // Only meaningful when `replicated`; left at 1.0 otherwise so a reader who
    // ignores the flag cannot mistake an unrun stage for a significant one.
    double replicate_p = 1.0;
    double threshold = 0.0;   // the SAME per-row Holm threshold both stages use
    bool replicated = false;  // did the confirmatory stage run for this row?
    bool rejected = false;    // the FINAL verdict: rejected in BOTH stages
};

// THE PRE-REGISTERED TWO-STAGE RULE. A row RETAINED by Holm at stage one is
// final and its replicate is NEVER RUN (`replicate_p_value` is not called for
// it -- that is a contract, not an optimisation, and it is what keeps the rule
// from becoming a second look at rows that already passed). A row REJECTED at
// stage one triggers exactly ONE confirmatory replicate, judged at the SAME
// per-row threshold; it is finally rejected only if the replicate rejects too.
//
// The family-wise consequence, stated where the rule lives: under a true null a
// row must land in its own alpha tail TWICE on independent seed blocks, so the
// false-flag rate is ~alpha^2 per row rather than alpha, while power against a
// real effect is essentially unchanged -- a true bias rejects both stages.
//
// `replicate_p_value(name)` must return the p-value of THAT row recomputed on
// the pre-registered replicate seed block. Nothing here chooses that block.
[[nodiscard]] std::vector<ReplicateDecision> confirm_by_replicate(
    const std::vector<HolmDecision>& stage_one,
    const std::function<double(const std::string&)>& replicate_p_value);

}  // namespace sts::dist_check
