#pragma once

// S2.44 -- the ANALYTIC half of the tier-4 S2 family (s2-design §6, S2-G2 item
// 6). Every function here derives an expected distribution from the cited Java
// rules over registry DATA; nothing here samples, and nothing here calls the
// code under test. The observed half lives in src/s2_main.cpp and drives the
// engine's own entry points.
//
// The split exists so the expectations can be unit-tested (tests/
// dist_check_test.cpp, `DistCheckS2Expect.*`) against hand-derived numbers,
// which is the only way a chi-square campaign can be trusted: a wrong
// expectation is indistinguishable from an engine defect at the report line.
//
// Provenance for the rules encoded here (read in full from
// D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * MonsterInfo.normalizeWeights / roll        MonsterInfo.java:27-52
//   * AbstractDungeon.populateMonsterList        AbstractDungeon.java:1064-1095
//   * AbstractDungeon.populateFirstStrongEnemy   AbstractDungeon.java:1057-1062
//   * TheCity.generateExclusions                 TheCity.java:136-148
//   * TheBeyond.generateExclusions               TheBeyond.java:126-138
//   * AbstractDungeon.returnRandomRelicKey /
//     returnEndRandomRelicKey                    AbstractDungeon.java:704-819
//   * BossChest.<init>                           BossChest.java:35-39

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "sts/registry/encounter_table.hpp"

namespace sts::dist_check::s2 {

// One pool row exactly as the registry holds it: the game's encounter key and
// its MonsterInfo weight, in registry id order (== the game's ArrayList add
// order). Deliberately NOT normalized and NOT sorted -- normalization is a
// property of the law below, and the ascending-weight sort is a float-tie
// concern of the engine's roll, not of the probability.
struct PoolRow {
    std::string_view key;
    double weight = 0.0;
};

// The (act, pool) rows, in registry id order.
[[nodiscard]] std::vector<PoolRow> pool_rows(int act,
                                             sts::registry::EncounterPool pool);

// The WEAK row's `excludes` list for one weak key in one act, i.e. what
// generateExclusions hands populateFirstStrongEnemy when `key` is the LAST weak
// entry. Empty for a key with no exclusions.
[[nodiscard]] std::vector<std::string_view> weak_exclusions(
    int act, std::string_view key);

// MonsterInfo.roll over `rows` with `excluded` keys rejected and re-rolled: the
// weights of the surviving rows, renormalized. Returns one probability per row
// in `rows` order; an excluded row gets exactly 0.0. `excluded` keys that are
// not in `rows` are inert (TheBeyond's "Orb Walker" self-exclusion is a real
// instance of that -- it is a weak-only key the first-strong loop can never
// match, and the registry models it verbatim).
[[nodiscard]] std::vector<double> pool_roll_law(
    std::span<const PoolRow> rows,
    std::span<const std::string_view> excluded);

// The joint law of TWO CONSECUTIVE populateMonsterList entries drawn from one
// pool at list indices 0 and 1: index 0 is the bare pool roll, index 1 rejects
// an immediate repeat and re-rolls from the same normalized pool, so its
// conditional is weight/(1 - weight(prev)). The A-B-A rule needs index-2 and
// therefore cannot bite at index 1 -- which is why this one function serves the
// Act-2/3 WEAK segment (two entries; weak_segment_for_act) and both acts' ELITE
// segment (no A-B-A rule at all).
//
// Cell (i, j) is at index i * rows.size() + j. The diagonal is exactly 0.
[[nodiscard]] std::vector<double> consecutive_pair_law(
    std::span<const PoolRow> rows);

// The joint law of (LAST weak entry, FIRST strong entry) for an act whose weak
// segment is two entries (Acts 2 and 3). The marginal of the last weak entry is
// the index-1 marginal of consecutive_pair_law; the conditional of the first
// strong entry is pool_roll_law over the STRONG rows with that weak key's
// exclusions rejected.
//
// Cell (w, s) is at index w * strong_count + s. An excluded (w, s) pair is
// exactly 0 -- which is what makes the exclusion an EXACT support assertion
// rather than a soft frequency claim.
[[nodiscard]] std::vector<double> first_strong_joint_law(int act);

// The front-scan consumption law of a canSpawn-gated pool pop
// (returnRandomRelicKey -> returnEndRandomRelicKey, both `remove(0)` for BOSS
// tier). A uniformly shuffled pool holds `allowed` spawnable and `blocked`
// blocked entries; the caller pops until `wanted` spawnable keys have been
// yielded, and every blocked entry it walks over is PERMANENTLY CONSUMED.
//
// Returns P(k blocked entries consumed) for k = 0 .. blocked, i.e. the pool
// lost `wanted + k` entries. Derivation: the `allowed` spawnable entries define
// allowed+1 gaps; the blocked entries land in a uniformly random multiset of
// those gaps, and k of them are consumed exactly when k fall in the first
// `wanted` gaps. Hence the negative-hypergeometric
//     P(k) = C(wanted+k-1, k) * C(gaps-wanted+blocked-k-1, blocked-k)
//            / C(gaps+blocked-1, blocked),  gaps = allowed + 1.
[[nodiscard]] std::vector<double> front_scan_blocked_law(int allowed,
                                                         int blocked,
                                                         int wanted);

}  // namespace sts::dist_check::s2
