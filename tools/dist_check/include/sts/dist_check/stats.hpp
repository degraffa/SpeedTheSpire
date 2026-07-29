#pragma once

#include <cstdint>
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

}  // namespace sts::dist_check
