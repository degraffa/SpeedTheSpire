#include "sts/dist_check/s2_expect.hpp"

#include <algorithm>
#include <stdexcept>

namespace sts::dist_check::s2 {
namespace {

using sts::registry::EncounterDef;
using sts::registry::EncounterPool;
using sts::registry::kEncounters;

bool contains_key(std::span<const std::string_view> keys,
                  std::string_view key) {
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

// C(n, k) in double. n stays tiny here (pool sizes < 64), so the multiplicative
// form is exact well past the sizes this campaign uses.
double binomial(int n, int k) {
    if (k < 0 || n < 0 || k > n) return 0.0;
    double out = 1.0;
    const int m = std::min(k, n - k);
    for (int i = 0; i < m; ++i) {
        out = out * static_cast<double>(n - i) / static_cast<double>(i + 1);
    }
    return out;
}

}  // namespace

std::vector<PoolRow> pool_rows(int act, EncounterPool pool) {
    std::vector<PoolRow> out;
    for (const EncounterDef& e : kEncounters) {
        if (e.act == act && e.pool == pool) {
            out.push_back(PoolRow{e.game_id, static_cast<double>(e.weight)});
        }
    }
    return out;
}

std::vector<std::string_view> weak_exclusions(int act, std::string_view key) {
    for (const EncounterDef& e : kEncounters) {
        if (e.act != act || e.pool != EncounterPool::WEAK ||
            e.game_id != key) {
            continue;
        }
        std::vector<std::string_view> out;
        for (uint8_t i = 0; i < e.exclude_count; ++i) {
            out.push_back(e.excludes[i]);
        }
        return out;
    }
    return {};
}

std::vector<double> pool_roll_law(std::span<const PoolRow> rows,
                                  std::span<const std::string_view> excluded) {
    std::vector<double> out(rows.size(), 0.0);
    double total = 0.0;
    for (const PoolRow& row : rows) {
        if (!contains_key(excluded, row.key)) total += row.weight;
    }
    if (!(total > 0.0)) {
        throw std::invalid_argument("pool_roll_law: every row is excluded");
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!contains_key(excluded, rows[i].key)) {
            out[i] = rows[i].weight / total;
        }
    }
    return out;
}

std::vector<double> consecutive_pair_law(std::span<const PoolRow> rows) {
    const std::size_t n = rows.size();
    if (n < 2) {
        throw std::invalid_argument("consecutive_pair_law: pool below two rows");
    }
    const std::vector<double> first = pool_roll_law(rows, {});
    std::vector<double> out(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        const std::string_view repeat[1] = {rows[i].key};
        const std::vector<double> second = pool_roll_law(
            rows, std::span<const std::string_view>(repeat, 1));
        for (std::size_t j = 0; j < n; ++j) {
            out[i * n + j] = first[i] * second[j];
        }
    }
    return out;
}

std::vector<double> first_strong_joint_law(int act) {
    const std::vector<PoolRow> weak = pool_rows(act, EncounterPool::WEAK);
    const std::vector<PoolRow> strong = pool_rows(act, EncounterPool::STRONG);
    if (weak.size() < 2 || strong.empty()) {
        throw std::invalid_argument("first_strong_joint_law: empty act pools");
    }
    // The marginal of the LAST weak entry == the index-1 marginal of the pair
    // law (Acts 2-3 draw exactly two weak entries, weak_segment_for_act).
    const std::vector<double> pair = consecutive_pair_law(weak);
    std::vector<double> last_weak(weak.size(), 0.0);
    for (std::size_t i = 0; i < weak.size(); ++i) {
        for (std::size_t j = 0; j < weak.size(); ++j) {
            last_weak[j] += pair[i * weak.size() + j];
        }
    }

    std::vector<double> out(weak.size() * strong.size(), 0.0);
    for (std::size_t w = 0; w < weak.size(); ++w) {
        const std::vector<std::string_view> excl =
            weak_exclusions(act, weak[w].key);
        const std::vector<double> conditional = pool_roll_law(strong, excl);
        for (std::size_t s = 0; s < strong.size(); ++s) {
            out[w * strong.size() + s] = last_weak[w] * conditional[s];
        }
    }
    return out;
}

std::vector<double> front_scan_blocked_law(int allowed, int blocked,
                                           int wanted) {
    if (allowed < wanted || blocked < 0 || wanted < 1) {
        throw std::invalid_argument("front_scan_blocked_law: domain");
    }
    const int gaps = allowed + 1;
    const double denominator = binomial(gaps + blocked - 1, blocked);
    std::vector<double> out(static_cast<std::size_t>(blocked) + 1, 0.0);
    for (int k = 0; k <= blocked; ++k) {
        const double head = binomial(wanted + k - 1, k);
        const double tail =
            binomial(gaps - wanted + (blocked - k) - 1, blocked - k);
        out[static_cast<std::size_t>(k)] = head * tail / denominator;
    }
    return out;
}

}  // namespace sts::dist_check::s2
