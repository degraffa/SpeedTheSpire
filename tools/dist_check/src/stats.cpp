#include "sts/dist_check/stats.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace sts::dist_check {
namespace {

// Regularized upper incomplete gamma Q(a,x), using the series for P below
// x<a+1 and Lentz's continued fraction otherwise. This is the chi-square
// survival function Q(k/2, statistic/2); no external statistics dependency is
// needed. Convergence constants are intentionally much tighter than the
// significance thresholds used by the campaign.
double gamma_q(double a, double x) {
    if (!(a > 0.0) || x < 0.0 || std::isnan(x)) {
        throw std::invalid_argument("gamma_q domain");
    }
    if (x == 0.0) return 1.0;

    constexpr int kMaxIterations = 10000;
    constexpr double kEpsilon = 1.0e-14;
    constexpr double kFloor = std::numeric_limits<double>::min() / kEpsilon;
    const double scale = std::exp(-x + a * std::log(x) - std::lgamma(a));

    if (x < a + 1.0) {
        double ap = a;
        double term = 1.0 / a;
        double sum = term;
        for (int i = 1; i <= kMaxIterations; ++i) {
            ap += 1.0;
            term *= x / ap;
            sum += term;
            if (std::abs(term) <= std::abs(sum) * kEpsilon) {
                return std::clamp(1.0 - sum * scale, 0.0, 1.0);
            }
        }
        throw std::runtime_error("gamma series did not converge");
    }

    double b = x + 1.0 - a;
    double c = 1.0 / kFloor;
    double d = 1.0 / b;
    double h = d;
    for (int i = 1; i <= kMaxIterations; ++i) {
        const double fi = static_cast<double>(i);
        const double an = -fi * (fi - a);
        b += 2.0;
        d = an * d + b;
        if (std::abs(d) < kFloor) d = kFloor;
        c = b + an / c;
        if (std::abs(c) < kFloor) c = kFloor;
        d = 1.0 / d;
        const double delta = d * c;
        h *= delta;
        if (std::abs(delta - 1.0) <= kEpsilon) {
            return std::clamp(h * scale, 0.0, 1.0);
        }
    }
    throw std::runtime_error("gamma continued fraction did not converge");
}

}  // namespace

ChiSquareResult chi_square(std::string name,
                           const std::vector<uint64_t>& observed,
                           const std::vector<double>& probabilities) {
    if (observed.size() != probabilities.size() || observed.empty()) {
        throw std::invalid_argument("chi_square: shape mismatch or empty input");
    }
    const uint64_t total =
        std::accumulate(observed.begin(), observed.end(), uint64_t{0});
    if (total == 0) {
        throw std::invalid_argument("chi_square: empty sample");
    }
    double probability_sum = 0.0;
    for (double p : probabilities) {
        if (p < 0.0 || !std::isfinite(p)) {
            throw std::invalid_argument("chi_square: invalid probability");
        }
        probability_sum += p;
    }
    if (std::abs(probability_sum - 1.0) > 1.0e-10) {
        throw std::invalid_argument("chi_square " + name +
                                    ": probabilities do not sum to one (" +
                                    std::to_string(probability_sum) + ")");
    }

    double statistic = 0.0;
    int positive_cells = 0;
    for (std::size_t i = 0; i < observed.size(); ++i) {
        const double expected =
            static_cast<double>(total) * probabilities[i];
        if (expected == 0.0) {
            if (observed[i] != 0) {
                return ChiSquareResult{std::move(name),
                                       std::numeric_limits<double>::infinity(),
                                       0, 0.0, total};
            }
            continue;
        }
        ++positive_cells;
        const double delta = static_cast<double>(observed[i]) - expected;
        statistic += delta * delta / expected;
    }
    if (positive_cells < 2) {
        throw std::invalid_argument("chi_square: fewer than two supported cells");
    }
    const int dof = positive_cells - 1;
    const double p = gamma_q(static_cast<double>(dof) / 2.0, statistic / 2.0);
    return ChiSquareResult{std::move(name), statistic, dof, p, total};
}

std::vector<HolmDecision> holm_bonferroni(
    const std::vector<ChiSquareResult>& tests, double family_alpha) {
    if (!(family_alpha > 0.0 && family_alpha < 1.0)) {
        throw std::invalid_argument("holm_bonferroni: alpha must be in (0,1)");
    }
    std::vector<const ChiSquareResult*> ordered;
    ordered.reserve(tests.size());
    for (const ChiSquareResult& test : tests) ordered.push_back(&test);
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
        return a->p_value < b->p_value;
    });

    std::vector<HolmDecision> out;
    out.reserve(ordered.size());
    bool retain_rest = false;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const double threshold =
            family_alpha / static_cast<double>(ordered.size() - i);
        const bool rejected =
            !retain_rest && ordered[i]->p_value <= threshold;
        if (!rejected) retain_rest = true;
        out.push_back(HolmDecision{ordered[i]->name, ordered[i]->p_value,
                                   threshold, rejected});
    }
    return out;
}

}  // namespace sts::dist_check
