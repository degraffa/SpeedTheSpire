// T0.6 -- the belief sampler's DISTRIBUTIONAL suite (docs/training-plan.md
// §2.6d). Sibling of tools/dist_check: same chi-square + Holm-Bonferroni
// machinery (sts::dist_check), same pre-registration discipline, but the null
// hypotheses here are the CONTRACT's closed-form conditionals rather than
// aggregates of live simulator play.
//
// =============================================================================
// WHAT IS UNDER TEST -- THE CONTRACT, NOT THE MECHANIC
// =============================================================================
//
// resample_hidden samples from the belief declared by knowledge.hpp and
// resample.hpp, which is deliberately COARSER than the JDK mechanic in three
// recorded places (random insertion never displacing the top card; relic-pool
// membership under pop-time canSpawn; Match & Keep miss memory). Every null
// below is therefore derived from the DECLARED contract, enumerated in closed
// form in this file, and never from seed-filtered reality -- except for the one
// hypothesis (H9) whose row carries NO coarsening, where seed-filtered reality
// and the contract must agree and the agreement is worth checking. Testing the
// coarsened rows against seed-filtered reality would be a guaranteed and
// meaningless red: the sampler is *supposed* to be wider there.
//
// =============================================================================
// PRE-REGISTERED HYPOTHESIS FAMILY  (fixed before the first run; do not tune)
// =============================================================================
//
// Family-wise alpha:      ALPHA_FAMILY = 1.0e-3
// Correction:             Holm-Bonferroni step-down (dist_check::holm_bonferroni),
//                         chosen for the same reason tools/dist_check/README.md
//                         gives: strong FWER control under arbitrary dependence.
// Family size:            K = 9 (the nine rows below). The strictest Holm
//                         threshold is ALPHA_FAMILY/K = 1.11e-4; a hypothesis
//                         with p above that is retained at EVERY step, which is
//                         what the per-hypothesis assertions check.
// Statistic:              Pearson chi-square goodness of fit against the
//                         enumerated closed-form cell probabilities. A cell of
//                         probability 0 is an exact support assertion (observing
//                         one yields p = 0) -- so both the SUPPORT and the SHAPE
//                         of each conditional are under test.
// Determinism:            every hypothesis draws from ONE SamplerRng seeded with
//                         a pre-registered constant (kSeed* below), so the whole
//                         suite is a pure function of the build. A "flaky at
//                         alpha" suite would be a design failure; here a rerun
//                         reproduces every p-value bit for bit
//                         (SuiteIsDeterministicAcrossReruns pins that).
// Family-wise accounting: under a correct sampler the pre-registered DESIGN
//                         (before the seeds were fixed) flags with probability
//                         <= ALPHA_FAMILY = 1.0e-3 across the whole family;
//                         after seed fixing the realized suite is deterministic,
//                         so the observed outcome is a constant of the build and
//                         cannot flake between nights.
//
//   ID  NAME                                    CELLS  NIGHTLY N  CLOSED FORM
//   H1  draw.unconstrained_permutation_uniform     24     24,000  uniform on 4!
//   H2  draw.exact_prefix_conditional             120     24,000  1/3! on the 6
//                                                                 orders that keep
//                                                                 the known prefix;
//                                                                 0 on the other 114
//   H3  draw.relative_order_interleaving           24     24,000  1/12 on the 12
//                                                                 interleavings that
//                                                                 preserve the chain
//                                                                 order; 0 elsewhere
//   H4  encounter.weak_suffix_pair                 16     30,000  two-step chain
//                                                                 enumeration, WEAK
//                                                                 pool, no-immediate-
//                                                                 repeat + no-A-B-A
//   H5  encounter.strong_suffix_pair              m*m     90,000  same, STRONG pool
//                                                                 (NON-UNIFORM weights)
//   H6  encounter.elite_suffix_pair                 9     30,000  same, ELITE pool,
//                                                                 no-immediate-repeat
//                                                                 only
//   H7  relic.remainder_permutation_uniform        24     24,000  uniform on 4!
//   H8  relic.remainder_position_marginal          12     24,000  uniform position
//                                                                 marginal, window 12
//   H9  seedfilter.weak_second_encounter             4    200,000 seeds, filtered on a
//                                                                 one-entry public
//                                                                 prefix (acceptance
//                                                                 ~1/4): REAL
//                                                                 generate_monster_lists
//                                                                 output vs the same
//                                                                 closed form H4 uses
//
// H9 is the bounded seed-filtered sanity check the ledger asks for. It is
// bounded twice over: the filtered prefix is ONE entry (acceptance rate ~1/4,
// so ~50k accepted seeds out of 200k), and it is applied only to the encounter
// row, whose conditional law carries no declared coarsening. Chained with H4
// (sampler == contract) it gives sampler == reality on that prefix. There is
// deliberately NO seed-filtered draw-order check: that row IS coarsened, and a
// seed-filtered comparison there would fail by design.
//
// SMOKE vs NIGHTLY. The per-commit ctest runs this binary in SMOKE mode: the
// same statistics and the same alpha at N/10, with H9 (the seed sweep) skipped
// -- exactly the split tools/dist_check uses, where the per-commit test covers
// the machinery and the >=10k-seed sweep is an out-of-band campaign. The
// nightly entry point (tools/dist_check/sampler_dist.sh, wired in
// .github/workflows/nightly.yml) sets STS_SAMPLER_DIST_MODE=nightly and runs the
// full pre-registered N. Reducing N lowers POWER, never the level, so the smoke
// run is a valid (weaker) test at the same alpha; Holm is applied over the
// hypotheses actually executed.
//
// =============================================================================
// NEGATIVE CONTROL (the suite's power, asserted rather than assumed)
// =============================================================================
//
// Three deliberately-biased sampler MUTANTS are fed through the identical
// counting + chi-square + threshold path and must each be REJECTED at the
// strictest Holm threshold (p <= ALPHA_FAMILY/K). All three are
// support-complete -- they can produce every outcome the contract allows -- so
// rejection comes from the SHAPE of the distribution, not from a cheap support
// violation. Mutants always run at the nightly N (they cost microseconds), so
// the per-commit smoke run still proves the machinery has teeth.
//
//   M1  naive-swap shuffle (swap i with random(0, n-1)) replacing the draw-order
//       Fisher-Yates -- the classic non-uniform shuffle bug.
//   M2  the REAL relic-remainder permuter, called only 3 times in 4 -- an
//       early-return bug that leaves the true order intact a quarter of the time.
//   M3  an encounter continuation that respects the exclusion rules but ignores
//       the pool WEIGHTS (uniform over the allowed keys).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "sts/dist_check/stats.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/knowledge.hpp"
#include "sts/engine/resample.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_state.hpp"

namespace sts::engine {
namespace {

using sts::dist_check::ChiSquareResult;
using sts::dist_check::chi_square;
using sts::dist_check::holm_bonferroni;

// --- pre-registered constants -------------------------------------------------

constexpr double kAlphaFamily = 1.0e-3;
constexpr int kFamilySize = 9;
// The strictest Holm step. p above this is retained at every step of the
// procedure, so it is the per-hypothesis bar the individual assertions use.
constexpr double kHolmFloor = kAlphaFamily / static_cast<double>(kFamilySize);

// One fixed stream per hypothesis: distinct seeds so no two hypotheses share a
// realization, fixed values so the suite is deterministic.
constexpr int64_t kSeedDrawUnconstrained = 0xD1A70001LL;
constexpr int64_t kSeedDrawExactPrefix = 0xD1A70002LL;
constexpr int64_t kSeedDrawInterleave = 0xD1A70003LL;
constexpr int64_t kSeedEncounterWeak = 0xD1A70004LL;
constexpr int64_t kSeedEncounterStrong = 0xD1A70005LL;
constexpr int64_t kSeedEncounterElite = 0xD1A70006LL;
constexpr int64_t kSeedRelicPermutation = 0xD1A70007LL;
constexpr int64_t kSeedRelicMarginal = 0xD1A70008LL;
constexpr int64_t kSeedMutantDraw = 0xD1A71001LL;
constexpr int64_t kSeedMutantRelic = 0xD1A71002LL;
constexpr int64_t kSeedMutantEncounter = 0xD1A71003LL;

// The public prefixes the conditional hypotheses condition on. Any registry key
// would do; these are pinned so the closed form and the sample agree on which
// conditional is being tested.
constexpr std::string_view kWeakPrefix = "Cultist";
constexpr std::string_view kWeakSecond = "Jaw Worm";
constexpr std::string_view kWeakThird = "2 Louse";
constexpr std::string_view kStrongPrefix = "Blue Slaver";
constexpr std::string_view kElitePrefix = "Gremlin Nob";

struct Scale {
    bool nightly = false;
    uint64_t permutation_n = 2400;
    uint64_t encounter_n = 3000;
    uint64_t strong_n = 9000;
    uint64_t seedfilter_seeds = 0;  // 0 == H9 not executed (smoke)
};

// Mutants always run at the nightly N: they are local and cost microseconds,
// and a negative control that only has teeth at night is not a control.
constexpr uint64_t kMutantN = 24000;

// The mode switch. _dupenv_s rather than getenv under the MSVC CRT: clang-cl
// marks getenv deprecated there, and this project's warning gate is not the
// place to spend a suppression on one environment read.
[[nodiscard]] std::string env_mode() {
#if defined(_WIN32)
    char* buf = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&buf, &len, "STS_SAMPLER_DIST_MODE") != 0 || buf == nullptr) {
        return {};
    }
    std::string value(buf);
    std::free(buf);
    return value;
#else
    const char* m = std::getenv("STS_SAMPLER_DIST_MODE");
    return m == nullptr ? std::string{} : std::string(m);
#endif
}

[[nodiscard]] bool nightly_mode() { return env_mode() == "nightly"; }

[[nodiscard]] Scale scale() {
    Scale s;
    s.nightly = nightly_mode();
    if (s.nightly) {
        s.permutation_n = 24000;
        s.encounter_n = 30000;
        s.strong_n = 90000;
        s.seedfilter_seeds = 200000;
    }
    return s;
}

// --- permutation ranking (Lehmer code over the distinct labels) ---------------

[[nodiscard]] std::size_t factorial(std::size_t n) {
    std::size_t f = 1;
    for (std::size_t i = 2; i <= n; ++i) {
        f *= i;
    }
    return f;
}

// Rank of `order` among the permutations of its own (distinct) values, taken in
// ascending-value lexicographic order. Deterministic and total, so it indexes a
// cell array of size n!.
[[nodiscard]] std::size_t perm_rank(const std::vector<int>& order) {
    std::vector<int> pool = order;
    std::sort(pool.begin(), pool.end());
    std::size_t rank = 0;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const auto it = std::find(pool.begin(), pool.end(), order[i]);
        const std::size_t idx = static_cast<std::size_t>(it - pool.begin());
        rank += idx * factorial(order.size() - 1 - i);
        pool.erase(it);
    }
    return rank;
}

// The i-th permutation (same ranking) of `values`, for enumerating closed forms.
[[nodiscard]] std::vector<int> perm_at(std::vector<int> values,
                                       std::size_t rank) {
    std::sort(values.begin(), values.end());
    std::vector<int> out;
    while (!values.empty()) {
        const std::size_t f = factorial(values.size() - 1);
        const std::size_t idx = rank / f;
        rank %= f;
        out.push_back(values[idx]);
        values.erase(values.begin() + static_cast<std::ptrdiff_t>(idx));
    }
    return out;
}

// --- draw-pile helpers (same shape as resample_test.cpp) ----------------------

[[nodiscard]] CombatState pile_of(int n) {
    CombatState s{};
    s.draw_count = static_cast<uint8_t>(n);
    for (int i = 0; i < n; ++i) {
        s.draw[static_cast<std::size_t>(i)] = static_cast<CardPoolIndex>(i);
    }
    return s;
}

[[nodiscard]] std::vector<int> pile_top_first(const CombatState& s) {
    std::vector<int> out;
    for (int p = 0; p < static_cast<int>(s.draw_count); ++p) {
        out.push_back(static_cast<int>(
            s.draw[static_cast<std::size_t>(s.draw_count - 1 - p)]));
    }
    return out;
}

void restore_pile(CombatState& s, const CombatState& base) {
    for (int i = 0; i < static_cast<int>(base.draw_count); ++i) {
        s.draw[static_cast<std::size_t>(i)] = base.draw[static_cast<std::size_t>(i)];
    }
}

// --- encounter-pool closed form ----------------------------------------------
//
// Re-derived here from the registry table rather than reached into the engine's
// anonymous namespace: an independent re-expression of MonsterInfo.normalizeWeights
// + MonsterInfo.roll is the point of a distributional oracle. Both halves matter:
// the stable ASCENDING-weight sort before normalization (TRAP 1) and the float
// cumulative walk, whose last band absorbs the rounding remainder up to 1.0
// (roll_pool's fallback).

struct PoolCell {
    std::string_view key;
    double p = 0.0;
};

[[nodiscard]] std::vector<PoolCell> normalized_pool(
    int32_t act, sts::registry::EncounterPool pool) {
    std::vector<std::pair<std::string_view, float>> rows;
    for (const auto& e : sts::registry::kEncounters) {
        if (e.act == act && e.pool == pool) {
            rows.emplace_back(e.game_id, e.weight);
        }
    }
    std::stable_sort(rows.begin(), rows.end(),
                     [](const auto& a, const auto& b) { return a.second < b.second; });
    float total = 0.0f;
    for (const auto& r : rows) {
        total += r.second;
    }
    std::vector<PoolCell> out;
    float cur = 0.0f;
    double prev = 0.0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        cur += rows[i].second / total;
        const double c = (i + 1 == rows.size()) ? 1.0 : static_cast<double>(cur);
        out.push_back(PoolCell{rows[i].first, c - prev});
        prev = c;
    }
    return out;
}

[[nodiscard]] std::size_t pool_index(const std::vector<PoolCell>& pool,
                                     std::string_view key) {
    for (std::size_t i = 0; i < pool.size(); ++i) {
        if (pool[i].key == key) {
            return i;
        }
    }
    ADD_FAILURE() << "encounter key not in pool: " << key;
    return 0;
}

// AbstractDungeon.populateMonsterList's rejection loop is rejection sampling, so
// the accepted draw's law is the pool restricted to the allowed keys and
// renormalized. `forbid_a` is the entry immediately before the cursor,
// `forbid_b` the one before that (empty when the A-B-A rule does not apply --
// elites, or a cursor with only one entry behind it).
[[nodiscard]] std::vector<double> transition(const std::vector<PoolCell>& pool,
                                             std::string_view forbid_a,
                                             std::string_view forbid_b) {
    std::vector<double> p(pool.size(), 0.0);
    double mass = 0.0;
    for (std::size_t i = 0; i < pool.size(); ++i) {
        if (pool[i].key == forbid_a || (!forbid_b.empty() && pool[i].key == forbid_b)) {
            continue;
        }
        p[i] = pool[i].p;
        mass += pool[i].p;
    }
    for (double& x : p) {
        x /= mass;
    }
    return p;
}

// Brute-force enumeration of the two-step chain: every (first, second) pair,
// its probability the product of the two conditionals. Cell index is
// first * pool.size() + second.
[[nodiscard]] std::vector<double> two_step_joint(const std::vector<PoolCell>& pool,
                                                 std::string_view prev,
                                                 std::string_view prev2,
                                                 bool elites) {
    const std::size_t m = pool.size();
    std::vector<double> joint(m * m, 0.0);
    const std::vector<double> first = transition(pool, prev, elites ? "" : prev2);
    for (std::size_t i = 0; i < m; ++i) {
        if (first[i] == 0.0) {
            continue;
        }
        // At the second step the entry just written is `prev`, and the one
        // before it is the old `prev`.
        const std::vector<double> second =
            transition(pool, pool[i].key, elites ? "" : prev);
        for (std::size_t j = 0; j < m; ++j) {
            joint[i * m + j] = first[i] * second[j];
        }
    }
    return joint;
}

// =============================================================================
// H1-H3 -- draw-pile permutation under KnowledgeState constraints
// =============================================================================

[[nodiscard]] ChiSquareResult h1_draw_unconstrained(const Scale& sc) {
    constexpr int kN = 4;
    const CombatState base = pile_of(kN);
    CombatState s = base;
    SamplerRng rng = sampler_rng_from_seed(kSeedDrawUnconstrained);
    std::vector<uint64_t> obs(factorial(static_cast<std::size_t>(kN)), 0);
    for (uint64_t i = 0; i < sc.permutation_n; ++i) {
        restore_pile(s, base);
        resample_draw_order(s, KnowledgeState{}, rng);
        ++obs[perm_rank(pile_top_first(s))];
    }
    const std::vector<double> p(obs.size(), 1.0 / static_cast<double>(obs.size()));
    return chi_square("draw.unconstrained_permutation_uniform", obs, p);
}

[[nodiscard]] ChiSquareResult h2_draw_exact_prefix(const Scale& sc) {
    // Headbutt-style: the top two positions are exactly known, the other three
    // are uniform. 6 of the 120 orders are admissible; the other 114 are exact
    // support assertions.
    constexpr int kN = 5;
    const CombatState base = pile_of(kN);
    KnowledgeState k{};
    k.chain_count = 2;
    k.exact_prefix = 2;
    k.chain[0] = base.draw[4];  // top
    k.chain[1] = base.draw[3];

    CombatState s = base;
    SamplerRng rng = sampler_rng_from_seed(kSeedDrawExactPrefix);
    std::vector<uint64_t> obs(factorial(static_cast<std::size_t>(kN)), 0);
    for (uint64_t i = 0; i < sc.permutation_n; ++i) {
        restore_pile(s, base);
        resample_draw_order(s, k, rng);
        ++obs[perm_rank(pile_top_first(s))];
    }

    std::vector<int> labels;
    for (int i = 0; i < kN; ++i) {
        labels.push_back(i);
    }
    std::vector<double> p(obs.size(), 0.0);
    for (std::size_t r = 0; r < p.size(); ++r) {
        const std::vector<int> order = perm_at(labels, r);
        if (order[0] == static_cast<int>(k.chain[0]) &&
            order[1] == static_cast<int>(k.chain[1])) {
            p[r] = 1.0 / static_cast<double>(factorial(static_cast<std::size_t>(kN - 2)));
        }
    }
    return chi_square("draw.exact_prefix_conditional", obs, p);
}

[[nodiscard]] ChiSquareResult h3_draw_interleaving(const Scale& sc) {
    // The post-random-insertion contract: no absolute position survives, the
    // chain's RELATIVE order does, and the posterior is uniform over the
    // interleavings. 12 of the 24 orders of a 4-card pile keep chain[0] above
    // chain[1]; the other 12 are exact support assertions.
    constexpr int kN = 4;
    const CombatState base = pile_of(kN);
    KnowledgeState k{};
    k.chain_count = 2;
    k.exact_prefix = 0;
    k.chain[0] = base.draw[3];
    k.chain[1] = base.draw[1];

    CombatState s = base;
    SamplerRng rng = sampler_rng_from_seed(kSeedDrawInterleave);
    std::vector<uint64_t> obs(factorial(static_cast<std::size_t>(kN)), 0);
    for (uint64_t i = 0; i < sc.permutation_n; ++i) {
        restore_pile(s, base);
        resample_draw_order(s, k, rng);
        ++obs[perm_rank(pile_top_first(s))];
    }

    std::vector<int> labels;
    for (int i = 0; i < kN; ++i) {
        labels.push_back(i);
    }
    std::size_t admissible = 0;
    std::vector<bool> ok(obs.size(), false);
    for (std::size_t r = 0; r < obs.size(); ++r) {
        const std::vector<int> order = perm_at(labels, r);
        const auto pos_a = std::find(order.begin(), order.end(),
                                     static_cast<int>(k.chain[0]));
        const auto pos_b = std::find(order.begin(), order.end(),
                                     static_cast<int>(k.chain[1]));
        ok[r] = pos_a < pos_b;
        if (ok[r]) {
            ++admissible;
        }
    }
    std::vector<double> p(obs.size(), 0.0);
    for (std::size_t r = 0; r < obs.size(); ++r) {
        if (ok[r]) {
            p[r] = 1.0 / static_cast<double>(admissible);
        }
    }
    return chi_square("draw.relative_order_interleaving", obs, p);
}

// =============================================================================
// H4-H6 -- encounter-suffix continuation vs brute-force chain enumeration
// =============================================================================

// Continue a monster list whose first `keep` entries are the public prefix, and
// count the (next, next+1) pair. `target` is the list length the continuation
// must refill to.
[[nodiscard]] std::vector<uint64_t> sample_monster_pair(
    const std::vector<PoolCell>& pool, const MonsterLists& base, uint8_t keep,
    uint64_t n, int64_t seed) {
    const std::size_t m = pool.size();
    std::vector<uint64_t> obs(m * m, 0);
    SamplerRng rng = sampler_rng_from_seed(seed);
    for (uint64_t i = 0; i < n; ++i) {
        MonsterLists ml = base;
        continue_monster_lists(1, rng.stream, keep, 0, ml);
        const std::size_t a = pool_index(pool, ml.monster_list[keep]);
        const std::size_t b = pool_index(pool, ml.monster_list[keep + 1u]);
        ++obs[a * m + b];
    }
    return obs;
}

[[nodiscard]] ChiSquareResult h4_encounter_weak(const Scale& sc) {
    const std::vector<PoolCell> pool =
        normalized_pool(1, sts::registry::EncounterPool::WEAK);
    MonsterLists base{};
    base.monster_list_count = 3;  // weak segment only: no first-strong pass
    base.monster_list[0] = kWeakPrefix;
    const std::vector<uint64_t> obs =
        sample_monster_pair(pool, base, 1, sc.encounter_n, kSeedEncounterWeak);
    // Cursor at index 1: only one entry behind it, so the A-B-A rule is inert
    // for the first step and keys on kWeakPrefix for the second.
    const std::vector<double> p =
        two_step_joint(pool, kWeakPrefix, /*prev2=*/"", /*elites=*/false);
    return chi_square("encounter.weak_suffix_pair", obs, p);
}

[[nodiscard]] ChiSquareResult h5_encounter_strong(const Scale& sc) {
    const std::vector<PoolCell> pool =
        normalized_pool(1, sts::registry::EncounterPool::STRONG);
    MonsterLists base{};
    base.monster_list_count = 6;
    base.monster_list[0] = kWeakPrefix;
    base.monster_list[1] = kWeakSecond;
    base.monster_list[2] = kWeakThird;   // a WEAK key: never blocks a strong roll
    base.monster_list[3] = kStrongPrefix;
    const std::vector<uint64_t> obs =
        sample_monster_pair(pool, base, 4, sc.strong_n, kSeedEncounterStrong);
    const std::vector<double> p =
        two_step_joint(pool, kStrongPrefix, /*prev2=*/kWeakThird, /*elites=*/false);
    return chi_square("encounter.strong_suffix_pair", obs, p);
}

[[nodiscard]] ChiSquareResult h6_encounter_elite(const Scale& sc) {
    const std::vector<PoolCell> pool =
        normalized_pool(1, sts::registry::EncounterPool::ELITE);
    const std::size_t m = pool.size();
    MonsterLists base{};
    base.elite_list_count = 3;
    base.elite_list[0] = kElitePrefix;

    std::vector<uint64_t> obs(m * m, 0);
    SamplerRng rng = sampler_rng_from_seed(kSeedEncounterElite);
    for (uint64_t i = 0; i < sc.encounter_n; ++i) {
        MonsterLists ml = base;
        continue_monster_lists(1, rng.stream, 0, 1, ml);
        const std::size_t a = pool_index(pool, ml.elite_list[1]);
        const std::size_t b = pool_index(pool, ml.elite_list[2]);
        ++obs[a * m + b];
    }
    const std::vector<double> p =
        two_step_joint(pool, kElitePrefix, /*prev2=*/"", /*elites=*/true);
    return chi_square("encounter.elite_suffix_pair", obs, p);
}

// =============================================================================
// H7-H8 -- relic-pool remainder uniformity
// =============================================================================

[[nodiscard]] ChiSquareResult h7_relic_permutation(const Scale& sc) {
    constexpr int kWindow = 4;
    constexpr int kTier = 0;
    RunState rs{};
    rs.relic_pool_count[kTier] = static_cast<uint8_t>(kWindow);
    SamplerRng rng = sampler_rng_from_seed(kSeedRelicPermutation);
    std::vector<uint64_t> obs(factorial(static_cast<std::size_t>(kWindow)), 0);
    for (uint64_t i = 0; i < sc.permutation_n; ++i) {
        for (int j = 0; j < kWindow; ++j) {
            rs.relic_pools[kTier][j] = static_cast<uint16_t>(j + 1);
        }
        resample_relic_pool_remainders(rs, rng);
        std::vector<int> order;
        for (int j = 0; j < kWindow; ++j) {
            order.push_back(static_cast<int>(rs.relic_pools[kTier][j]));
        }
        ++obs[perm_rank(order)];
    }
    const std::vector<double> p(obs.size(), 1.0 / static_cast<double>(obs.size()));
    return chi_square("relic.remainder_permutation_uniform", obs, p);
}

[[nodiscard]] ChiSquareResult h8_relic_marginal(const Scale& sc) {
    // A window too large to enumerate: the tractable closed form is the
    // position marginal of one pinned relic, uniform over the window.
    constexpr int kWindow = 12;
    constexpr int kTier = 1;
    constexpr uint16_t kTracked = 1;
    RunState rs{};
    rs.relic_pool_count[kTier] = static_cast<uint8_t>(kWindow);
    SamplerRng rng = sampler_rng_from_seed(kSeedRelicMarginal);
    std::vector<uint64_t> obs(static_cast<std::size_t>(kWindow), 0);
    for (uint64_t i = 0; i < sc.permutation_n; ++i) {
        for (int j = 0; j < kWindow; ++j) {
            rs.relic_pools[kTier][j] = static_cast<uint16_t>(j + 1);
        }
        resample_relic_pool_remainders(rs, rng);
        for (int j = 0; j < kWindow; ++j) {
            if (rs.relic_pools[kTier][j] == kTracked) {
                ++obs[static_cast<std::size_t>(j)];
                break;
            }
        }
    }
    const std::vector<double> p(obs.size(), 1.0 / static_cast<double>(kWindow));
    return chi_square("relic.remainder_position_marginal", obs, p);
}

// =============================================================================
// H9 -- the bounded seed-filtered sanity check
// =============================================================================

[[nodiscard]] ChiSquareResult h9_seed_filtered(const Scale& sc) {
    const std::vector<PoolCell> pool =
        normalized_pool(1, sts::registry::EncounterPool::WEAK);
    std::vector<uint64_t> obs(pool.size(), 0);
    for (int64_t s = 1; s <= static_cast<int64_t>(sc.seedfilter_seeds); ++s) {
        RngStream m = from_seed(s);
        MonsterLists ml{};
        generate_monster_lists(1, m, ml);
        if (ml.monster_list_count < 2 || ml.monster_list[0] != kWeakPrefix) {
            continue;  // the public prefix filter
        }
        ++obs[pool_index(pool, ml.monster_list[1])];
    }
    const std::vector<double> p = transition(pool, kWeakPrefix, /*forbid_b=*/"");
    return chi_square("seedfilter.weak_second_encounter", obs, p);
}

// =============================================================================
// The family
// =============================================================================

[[nodiscard]] std::vector<ChiSquareResult> run_family(const Scale& sc) {
    std::vector<ChiSquareResult> out;
    out.push_back(h1_draw_unconstrained(sc));
    out.push_back(h2_draw_exact_prefix(sc));
    out.push_back(h3_draw_interleaving(sc));
    out.push_back(h4_encounter_weak(sc));
    out.push_back(h5_encounter_strong(sc));
    out.push_back(h6_encounter_elite(sc));
    out.push_back(h7_relic_permutation(sc));
    out.push_back(h8_relic_marginal(sc));
    if (sc.seedfilter_seeds > 0) {
        out.push_back(h9_seed_filtered(sc));
    }
    return out;
}

void report(const ChiSquareResult& r) {
    std::cout << "  " << r.name << ": chi2=" << r.statistic << " df="
              << r.degrees_of_freedom << " p=" << r.p_value << " n="
              << r.sample_count << "\n";
}

TEST(SamplerDistribution, PreRegisteredFamilyIsRetainedUnderHolm) {
    const Scale sc = scale();
    std::cout << "mode: " << (sc.nightly ? "nightly" : "smoke")
              << " (STS_SAMPLER_DIST_MODE)\n";
    const std::vector<ChiSquareResult> family = run_family(sc);
    ASSERT_EQ(family.size(), sc.nightly ? static_cast<std::size_t>(kFamilySize)
                                        : static_cast<std::size_t>(kFamilySize - 1));

    for (const ChiSquareResult& r : family) {
        report(r);
        EXPECT_GT(r.sample_count, 0u) << r.name << " observed nothing";
        // Every hypothesis above the strictest Holm threshold is retained at
        // every step of the step-down procedure.
        EXPECT_GT(r.p_value, kHolmFloor)
            << r.name << " diverges from its closed-form conditional "
            << "(chi2=" << r.statistic << ", df=" << r.degrees_of_freedom
            << ", n=" << r.sample_count << ")";
    }

    // The procedure itself, run as pre-registered, over the executed family.
    for (const auto& d : holm_bonferroni(family, kAlphaFamily)) {
        EXPECT_FALSE(d.rejected)
            << "Holm rejected " << d.name << " at family alpha " << kAlphaFamily
            << " (p=" << d.p_value << " <= " << d.threshold << ")";
    }
}

TEST(SamplerDistribution, SuiteIsDeterministicAcrossReruns) {
    // Fixed sampler seeds are what make "green on three consecutive nights" a
    // property of the sampler rather than of luck: two runs in one process must
    // agree to the last bit, and a fresh process starts from the same constants.
    const Scale sc = scale();
    const std::vector<ChiSquareResult> a = run_family(sc);
    const std::vector<ChiSquareResult> b = run_family(sc);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].name, b[i].name);
        EXPECT_EQ(a[i].statistic, b[i].statistic) << a[i].name;
        EXPECT_EQ(a[i].p_value, b[i].p_value) << a[i].name;
        EXPECT_EQ(a[i].sample_count, b[i].sample_count) << a[i].name;
    }
}

// =============================================================================
// Negative control -- the deliberately-biased mutants
// =============================================================================

// M1: the classic wrong shuffle. Every permutation stays reachable, so this is
// a pure shape failure, not a support failure.
void mutant_naive_shuffle(CombatState& s, SamplerRng& rng) {
    const int n = static_cast<int>(s.draw_count);
    for (int i = 0; i < n; ++i) {
        const int j = random(rng.stream, 0, n - 1);
        std::swap(s.draw[static_cast<std::size_t>(i)],
                  s.draw[static_cast<std::size_t>(j)]);
    }
}

TEST(SamplerDistributionNegativeControl, BiasedDrawOrderMutantIsRejected) {
    constexpr int kN = 4;
    const CombatState base = pile_of(kN);
    CombatState s = base;
    SamplerRng rng = sampler_rng_from_seed(kSeedMutantDraw);
    std::vector<uint64_t> obs(factorial(static_cast<std::size_t>(kN)), 0);
    for (uint64_t i = 0; i < kMutantN; ++i) {
        restore_pile(s, base);
        mutant_naive_shuffle(s, rng);
        ++obs[perm_rank(pile_top_first(s))];
    }
    const std::vector<double> p(obs.size(), 1.0 / static_cast<double>(obs.size()));
    const ChiSquareResult r =
        chi_square("mutant.draw_naive_shuffle", obs, p);
    report(r);
    EXPECT_LE(r.p_value, kHolmFloor)
        << "the suite failed to reject a known-biased shuffle -- it has no power";
}

TEST(SamplerDistributionNegativeControl, SkippingRelicRemainderMutantIsRejected) {
    // M2: the REAL permuter, wired through an early-return bug that leaves the
    // true order intact one time in four.
    constexpr int kWindow = 4;
    constexpr int kTier = 0;
    RunState rs{};
    rs.relic_pool_count[kTier] = static_cast<uint8_t>(kWindow);
    SamplerRng rng = sampler_rng_from_seed(kSeedMutantRelic);
    std::vector<uint64_t> obs(factorial(static_cast<std::size_t>(kWindow)), 0);
    for (uint64_t i = 0; i < kMutantN; ++i) {
        for (int j = 0; j < kWindow; ++j) {
            rs.relic_pools[kTier][j] = static_cast<uint16_t>(j + 1);
        }
        if (random(rng.stream, 0, 3) != 0) {
            resample_relic_pool_remainders(rs, rng);
        }
        std::vector<int> order;
        for (int j = 0; j < kWindow; ++j) {
            order.push_back(static_cast<int>(rs.relic_pools[kTier][j]));
        }
        ++obs[perm_rank(order)];
    }
    const std::vector<double> p(obs.size(), 1.0 / static_cast<double>(obs.size()));
    const ChiSquareResult r =
        chi_square("mutant.relic_remainder_early_return", obs, p);
    report(r);
    EXPECT_LE(r.p_value, kHolmFloor)
        << "the suite failed to reject a sampler that skips a quarter of its "
           "permutations";
}

TEST(SamplerDistributionNegativeControl, WeightIgnoringEncounterMutantIsRejected) {
    // M3: a continuation that honours the exclusion rules (so the support is
    // exactly right) but draws UNIFORMLY over the allowed keys instead of by
    // pool weight. Only the STRONG pool has non-uniform weights, which is why
    // this mutant is aimed at H5's conditional.
    const std::vector<PoolCell> pool =
        normalized_pool(1, sts::registry::EncounterPool::STRONG);
    const std::size_t m = pool.size();
    SamplerRng rng = sampler_rng_from_seed(kSeedMutantEncounter);

    const auto uniform_allowed = [&](std::string_view forbid_a,
                                     std::string_view forbid_b) {
        std::vector<std::size_t> allowed;
        for (std::size_t i = 0; i < m; ++i) {
            if (pool[i].key == forbid_a ||
                (!forbid_b.empty() && pool[i].key == forbid_b)) {
                continue;
            }
            allowed.push_back(i);
        }
        const int32_t pick = random(rng.stream, 0,
                                    static_cast<int32_t>(allowed.size()) - 1);
        return allowed[static_cast<std::size_t>(pick)];
    };

    std::vector<uint64_t> obs(m * m, 0);
    for (uint64_t i = 0; i < kMutantN; ++i) {
        const std::size_t a = uniform_allowed(kStrongPrefix, kWeakThird);
        const std::size_t b = uniform_allowed(pool[a].key, kStrongPrefix);
        ++obs[a * m + b];
    }
    const std::vector<double> p =
        two_step_joint(pool, kStrongPrefix, kWeakThird, /*elites=*/false);
    const ChiSquareResult r =
        chi_square("mutant.encounter_ignores_weights", obs, p);
    report(r);
    EXPECT_LE(r.p_value, kHolmFloor)
        << "the suite failed to reject a continuation that ignores the pool "
           "weights";
}

}  // namespace
}  // namespace sts::engine
