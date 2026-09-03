// S3.63 -- the S3 tier-4 distributional family (s3-tasks.md S3.63, s3-design
// §5 traps 1/9 and the Act-4 monster getMove tables). ONE pre-registered
// Holm-Bonferroni family, sitting ALONGSIDE B5.3's and S2.44's rather than
// inside either (the same "separate family" discipline README.md states for
// S2.44: reopening a closed family to add rows retroactively changes every
// threshold it was judged against). The registration -- the hypothesis list,
// the family-wise alpha, the correction and the sample sizes -- is frozen
// BEFORE the first campaign run.
//
// The registered family is SIX hypotheses at family-wise alpha 0.01,
// Holm-Bonferroni corrected, with the S2.44 two-stage replicate-before-
// flagging rule applied uniformly to the family and to its negative controls:
//
//   1. s3.monster.spire_shield_case0_coin   -- SpireShield.getMove case 0
//   2. s3.monster.spire_spear_case2_coin    -- SpireSpear.getMove case 2
//   3. s3.monster.corrupt_heart_case0_coin  -- CorruptHeart.getMove phase 0
//   4. s3.map.emerald_gate_elite_index      -- setEmeraldElite's node choice
//   5. s3.relic.act4_shop_can_spawn_front_scan
//   6. s3.relic.act4_reward_can_spawn_front_scan
//
// Every OBSERVED count comes out of the engine's own entry points --
// spire_shield_init/_roll_move, spire_spear_init/_roll_move,
// corrupt_heart_init/_roll_move/_take_turn, assign_room_types (off
// generate_map), and return_random_relic_key -- driven from deterministic
// seeds, so a rerun of the same seed base reproduces every number byte for
// byte. Every EXPECTED distribution is either a closed-form 50/50 (a Java
// randomBoolean(), Random.java per header note (2) of each monster header), a
// stratified discrete-uniform conditioned on an ancillary statistic observed
// in the SAME sweep (the elite-node count, itself a structural fact of the
// map, not a tuned parameter), or dist_check's own S2.44 front-scan law
// (front_scan_blocked_law, s2_expect.hpp) reused UNMODIFIED: a canSpawn
// front-pop-then-end-scan over a uniformly shuffled pool is, by the
// permutation-symmetry argument below, the identical negative-hypergeometric
// distribution regardless of which fixed traversal order visits the pool.
//
// REUSING front_scan_blocked_law ACROSS FAMILIES, JUSTIFIED. Non-BOSS-tier
// canSpawn rejection (relic_pools.cpp:370-428) pops the FRONT once
// (return_random_relic_key) and then, on rejection, walks the END inward
// (return_end_random_relic_key), consuming every rejected entry permanently --
// unlike BOSS tier, which is a pure front-scan both times (S2.44's own
// registration). The traversal order this produces over pool POSITIONS is
// FIXED and OUTCOME-INDEPENDENT: position 0, then N-1, N-2, ..., 1. Because
// `initialize_relic_pools` shuffles the tier once uniformly at random
// (JdkRandom + Fisher-Yates), the sequence of allowed/blocked LABELS visited
// by ANY fixed, outcome-independent traversal order over a uniformly random
// permutation has the same distribution as visiting them in raw left-to-right
// order -- relabelling positions by a fixed bijection cannot bias a uniformly
// random permutation's label sequence. So "number of blocked entries consumed
// before the first allowed one" is exactly the SAME law under this
// front-then-end-inward traversal as under S2.44's pure front-scan, and
// front_scan_blocked_law(allowed, blocked, /*wanted=*/1) is the right
// expectation without new derivation. This is stated once, here, because both
// new hypotheses (5, 6) depend on it.
//
// REPLICATE BEFORE FLAGGING (S2.44's two-stage rule, dist_check/README.md,
// stats.hpp confirm_by_replicate): a row Holm-REJECTED at stage one triggers
// exactly ONE confirmatory replicate on the pre-registered replicate seed
// block (kReplicateSeedSalt), judged at the SAME per-row threshold; the row
// is finally rejected only if BOTH stages reject. A row retained at stage one
// is final and its replicate is never run. The rule applies uniformly to the
// six hypotheses and to the three negative controls below.
//
// POWER IS ASSERTED, NOT ASSUMED (T0.6 / S2.44 precedent): three
// deliberately-wrong mutants -- one per mechanism class this family tests --
// run through the identical chi-square machinery and MUST be rejected, in
// BOTH stages, at the family's strictest Holm threshold:
//
//   * mutant.spire_shield_case0_always_fortify  -- the case-0 coin never
//     fires; every seed reports Fortify. Represents "the randomBoolean draw
//     was dropped" for the coin-flip mechanism class (Shield/Spear/Heart
//     share one getMove shape at their respective coin arm).
//   * mutant.emerald_gate_always_first_node     -- the row-major rank is
//     hardcoded to 0 instead of read off mapRng's draw. Represents "the
//     draw's RESULT was ignored" for the map mechanism.
//   * mutant.act4_can_spawn_rejection_returns_relic -- a canSpawn rejection
//     puts the relic back instead of permanently consuming it (S2.44's own
//     M3, replayed against the Act-4 reward context). Represents "the
//     rejection reroute does not consume the pool" for the canSpawn
//     mechanism.
//
// EXACT SUPPORT / INVARIANT CHECKS, outside the Holm family (the B5.3/S2.44
// precedent: "exact support checks... are outside the stochastic family; any
// failure flags the campaign directly", dist_check/README.md):
//
//   * the emerald-gate TWO-ARM PAIRED comparison itself (trap 1's core
//     claim) -- on the SAME generated map, has_emerald_key=false versus
//     =true must produce byte-identical room grids, an identical elite-node
//     count, an mapRng counter that differs by exactly the one skipped draw,
//     and NO node ever marked while the key is held;
//   * the coin flips' "deterministic surround" -- every OTHER ai_rng draw
//     around each of the three coin flips (the rollMove `num` draw, and for
//     the Spear the two deterministic cycle arms leading up to case 2) costs
//     exactly the draws the header notes claim, on EVERY seed, regardless of
//     the coin's own outcome;
//   * the Heart's buffCount LADDER -- deterministic once GAIN_ONE_STRENGTH is
//     reached: Artifact(2) / Beat of Death(1) / Painful Stabs(-1) /
//     Strength(10) / Strength(50) forever, read off the QUEUED items
//     corrupt_heart_take_turn actually produces, with the 3-bit saturating
//     counter never exceeding rung 4 no matter how many times it is hit.
//
// A CONFIRMED rejection in the real family is a stop-the-line divergence,
// never a tuning signal: no alpha, no sample size, no seed block, no
// stratification and no expectation is adjusted after seeing a result.

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "sts/dist_check/s2_expect.hpp"  // front_scan_blocked_law (reused, see header)
#include "sts/dist_check/stats.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/map_gen.hpp"
#include "sts/engine/map_rooms.hpp"
#include "sts/engine/monster_corrupt_heart.hpp"
#include "sts/engine/monster_spire_shield.hpp"
#include "sts/engine/monster_spire_spear.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/registry/monster_table.hpp"

namespace {
using namespace sts::engine;
using sts::dist_check::ChiSquareResult;

// --- Pre-registered constants (frozen before the first campaign run) --------

constexpr int kMinimumSeeds = 10000;   // the B5.3/S2.44 floor, unchanged
constexpr int kDefaultSeeds = 20000;   // the B5.3/S2.44 acceptance scale
constexpr double kFamilyAlpha = 0.01;  // the B5.3/S2.44 family-wise alpha
constexpr int kFamilySize = 6;         // pre-registered hypothesis count

// Act-4 test floor: comfortably inside Act 4 (design §4.3's floor table puts
// every Act-4 room at 51 or later) and past every floor threshold in the
// canSpawn family this file exercises (35, 40, 48, 52), so the blocked set is
// stable across the A19/A20 floor-base shift.
constexpr uint16_t kAct4TestFloor = 55;

// THE CONFIRMATORY REPLICATE'S SEED SALT -- the S3.63 analogue of S2.44's
// kReplicateSeedSalt. FIXED AND DOCUMENTED BEFORE ANY REPLICATE WAS RUN, and
// DERIVED rather than chosen: the ASCII bytes 'S' '3' '6' '3' in the high
// word, so every sweep's replicate block is that sweep's own block with a
// constant offset in bits 32-63 and its low 32 bits untouched. Every
// stage-one base below is far short of 2^32 and no sweep is long enough to
// carry into bit 32, so the two stages' blocks are disjoint by construction,
// exactly as S2.44's are.
constexpr int64_t kReplicateSeedSalt = INT64_C(0x5333363300000000);

using CellTable = std::pair<std::vector<uint64_t>, std::vector<double>>;

struct Campaign {
    int seeds = kDefaultSeeds;
    int64_t salt = 0;  // 0 == stage one, kReplicateSeedSalt == the replicate
    std::map<std::string, CellTable> tables;         // name -> (observed, law)
    std::map<std::string, CellTable> mutant_tables;  // control name -> same
    std::vector<std::string> exact_failures;
};

void exact(Campaign& c, bool condition, std::string message) {
    if (!condition &&
        std::find(c.exact_failures.begin(), c.exact_failures.end(), message) ==
            c.exact_failures.end()) {
        c.exact_failures.push_back(std::move(message));
    }
}

void check_shape(std::string_view name, const std::vector<uint64_t>& observed,
                 const std::vector<double>& probabilities) {
    if (observed.size() == probabilities.size() && !observed.empty()) return;
    std::cerr << "dist_check_s3: " << name << " has " << observed.size()
              << " observed cells against " << probabilities.size()
              << " expected\n";
    throw std::invalid_argument("dist_check_s3: hypothesis table shape");
}

void add_test(Campaign& c, std::string name,
              const std::vector<uint64_t>& observed,
              const std::vector<double>& probabilities) {
    check_shape(name, observed, probabilities);
    c.tables.emplace(std::move(name), CellTable{observed, probabilities});
}

void add_mutant(Campaign& c, std::string name,
                const std::vector<uint64_t>& observed,
                const std::vector<double>& probabilities) {
    check_shape(name, observed, probabilities);
    c.mutant_tables.emplace(std::move(name), CellTable{observed, probabilities});
}

// --- H1..H3: the three Act-4 coin flips --------------------------------------
//
// Every one of these functions drives the REAL engine entry points
// (spire_shield_init/_roll_move, spire_spear_init/_roll_move,
// corrupt_heart_init/_roll_move) over a fresh CombatState per seed, with only
// `ai_rng` seeded -- the "deterministic surround" exact checks confirm every
// draw NOT under test costs exactly what the header notes say it costs,
// isolating the coin as the one degree of freedom.

constexpr uint8_t kShieldBash = sts::registry::kSpireShieldMoveBash;
constexpr uint8_t kShieldFortify = sts::registry::kSpireShieldMoveFortify;

constexpr uint8_t kSpearBurnStrike = sts::registry::kSpireSpearMoveBurnStrike;
constexpr uint8_t kSpearPiercer = sts::registry::kSpireSpearMovePiercer;
constexpr uint8_t kSpearSkewer = sts::registry::kSpireSpearMoveSkewer;

constexpr uint8_t kHeartBloodShots = sts::registry::kCorruptHeartMoveBloodShots;
constexpr uint8_t kHeartEchoAttack = sts::registry::kCorruptHeartMoveEchoAttack;
constexpr uint8_t kHeartDebilitate = sts::registry::kCorruptHeartMoveDebilitate;
constexpr uint8_t kHeartGainOneStrength =
    sts::registry::kCorruptHeartMoveGainOneStrength;

constexpr int64_t kShieldCoinSeedBase = INT64_C(0x53480001);   // "SH"...
constexpr int64_t kSpearCoinSeedBase = INT64_C(0x53500001);    // "SP"...
constexpr int64_t kHeartCoinSeedBase = INT64_C(0x48540001);    // "HT"...

// SpireShield.getMove case 0 (SpireShield.java:116-123): Fortify vs Bash,
// 50/50, off the very first decision (moveCount starts at 0). The
// "deterministic surround" here is the WHOLE init() call: exactly the
// rollMove `num` draw (spent, unread) plus the ONE case-0 randomBoolean --
// two ai_rng draws on every seed, regardless of which way the coin landed.
void spire_shield_case0_sweep(Campaign& c, uint64_t& fortify, uint64_t& bash,
                              uint64_t& mutant_fortify,
                              uint64_t& mutant_bash) {
    fortify = bash = mutant_fortify = mutant_bash = 0;
    auto cs = std::make_unique<CombatState>();
    for (int i = 0; i < c.seeds; ++i) {
        *cs = CombatState{};
        cs->monster_count = 1;
        cs->ai_rng = from_seed((kShieldCoinSeedBase ^ c.salt) + i);
        const int32_t before = cs->ai_rng.counter;
        spire_shield_init(*cs, 0);
        const int32_t after = cs->ai_rng.counter;
        exact(c, after - before == 2,
              "the Shield's opening decision did not spend exactly the "
              "rollMove draw plus its case-0 coin flip");
        const uint8_t move = cs->monsters[0].move_history[0];
        exact(c, move == kShieldBash || move == kShieldFortify,
              "the Shield's opening decision picked a move outside "
              "{Bash, Fortify}");
        if (move == kShieldFortify) {
            ++fortify;
        } else if (move == kShieldBash) {
            ++bash;
        }
        // MUTANT: the case-0 coin never fires -- always Fortify (the
        // "randomBoolean draw dropped" bug class).
        ++mutant_fortify;
    }
}

// SpireSpear.getMove case 2 (SpireSpear.java:131-137): Piercer vs BurnStrike,
// 50/50, reached only at moveCount == 2 -- the DEFAULT arm, unlike the
// Shield's case 0. Cases 0 and 1 are both deterministic (no coin), so the
// surrounding checks below cover the WHOLE three-arm cycle: init() (case 0,
// exactly one draw), one roll_move (case 1, exactly one draw), and the second
// roll_move (case 2, exactly two draws -- the coin).
void spire_spear_case2_sweep(Campaign& c, uint64_t& piercer,
                             uint64_t& burn_strike) {
    piercer = burn_strike = 0;
    auto cs = std::make_unique<CombatState>();
    for (int i = 0; i < c.seeds; ++i) {
        *cs = CombatState{};
        cs->monster_count = 1;
        cs->ai_rng = from_seed((kSpearCoinSeedBase ^ c.salt) + i);

        int32_t before = cs->ai_rng.counter;
        spire_spear_init(*cs, 0);
        exact(c, cs->ai_rng.counter - before == 1,
              "the Spear's case-0 opening decision spent more than the "
              "rollMove draw (case 0 has no coin)");
        exact(c, cs->monsters[0].move_history[0] == kSpearBurnStrike,
              "the Spear's very first decision was not BURN_STRIKE (case 0's "
              "lastMove(BURN_STRIKE) test must read false with no history)");

        before = cs->ai_rng.counter;
        spire_spear_roll_move(*cs, 0);
        exact(c, cs->ai_rng.counter - before == 1,
              "the Spear's case-1 decision spent more than the rollMove draw "
              "(case 1, SKEWER, is unconditional)");
        exact(c, cs->monsters[0].move_history[0] == kSpearSkewer,
              "the Spear's second decision was not SKEWER");

        before = cs->ai_rng.counter;
        spire_spear_roll_move(*cs, 0);
        exact(c, cs->ai_rng.counter - before == 2,
              "the Spear's case-2 decision did not spend exactly the "
              "rollMove draw plus its coin flip");
        const uint8_t move = cs->monsters[0].move_history[0];
        exact(c, move == kSpearPiercer || move == kSpearBurnStrike,
              "the Spear's third decision picked a move outside "
              "{Piercer, BurnStrike}");
        if (move == kSpearPiercer) {
            ++piercer;
        } else if (move == kSpearBurnStrike) {
            ++burn_strike;
        }
    }
}

// CorruptHeart.getMove phase 0 (CorruptHeart.java:179-186): BloodShots vs
// EchoAttack, 50/50, reached on the SECOND decision -- the opening DEBILITATE
// is an early return that does not advance moveCount (header note (1)), so
// the very next roll_move lands back on phase 0.
void corrupt_heart_case0_sweep(Campaign& c, uint64_t& blood_shots,
                               uint64_t& echo_attack) {
    blood_shots = echo_attack = 0;
    auto cs = std::make_unique<CombatState>();
    for (int i = 0; i < c.seeds; ++i) {
        *cs = CombatState{};
        cs->monster_count = 1;
        cs->ai_rng = from_seed((kHeartCoinSeedBase ^ c.salt) + i);

        int32_t before = cs->ai_rng.counter;
        corrupt_heart_init(*cs, 0);
        exact(c, cs->ai_rng.counter - before == 1,
              "the Heart's opening decision spent more than the rollMove "
              "draw (isFirstMove's DEBILITATE has no coin)");
        exact(c, cs->monsters[0].move_history[0] == kHeartDebilitate,
              "the Heart's very first decision was not DEBILITATE");

        before = cs->ai_rng.counter;
        corrupt_heart_roll_move(*cs, 0);
        exact(c, cs->ai_rng.counter - before == 2,
              "the Heart's phase-0 decision did not spend exactly the "
              "rollMove draw plus its coin flip");
        const uint8_t move = cs->monsters[0].move_history[0];
        exact(c, move == kHeartBloodShots || move == kHeartEchoAttack,
              "the Heart's second decision picked a move outside "
              "{BloodShots, EchoAttack}");
        if (move == kHeartBloodShots) {
            ++blood_shots;
        } else if (move == kHeartEchoAttack) {
            ++echo_attack;
        }
    }
}

// --- H4: the emerald-gate map divergence (trap 1) ----------------------------

constexpr int64_t kEmeraldSeedBase = INT64_C(0x454D001);  // "EM"...
constexpr int kEmeraldAscension = 20;
constexpr int kEmeraldAct = 2;  // any act after Act 1; the guard is act-blind

// The row-major rank setEmeraldElite's draw chose, re-derived from the
// (x, y) RoomAssignment recorded -- the same technique double_boss_sweep
// (s2_main.cpp) uses to recover a public-surface id's rank. -1 if no node was
// marked.
int elite_rank_of(const RoomAssignment& ra) {
    if (ra.emerald_x < 0 || ra.emerald_y < 0) return -1;
    int rank = 0;
    for (int y = 0; y < kGameMapFloors; ++y) {
        for (int x = 0; x < kGameMapCols; ++x) {
            if (ra.at(x, y) != RoomType::Elite) continue;
            if (x == ra.emerald_x && y == ra.emerald_y) return rank;
            ++rank;
        }
    }
    return -1;
}

// One stratified table: bucket k holds the observations from every seed whose
// arm-false map placed exactly k elite nodes, each bucket's own k-cell
// uniform law weighted by that bucket's OBSERVED share of the sweep. This
// conditions away the (structural, not tuned) elite-count nuisance variable
// and tests only the within-bucket uniformity of setEmeraldElite's draw --
// exactly the standard device of conditioning a chi-square test on an
// ancillary statistic, not "fitting the null to the data": the CONDITIONAL
// law (uniform 1/k) is the fixed, pre-registered claim; only which buckets
// exist is data-dependent, precisely as S2.44's can_spawn_stratum computes
// its pool size and blocked count from seed 0 rather than hardcoding them.
struct EmeraldSweepResult {
    std::map<int, std::vector<uint64_t>> buckets;       // k -> per-rank counts
    std::map<int, std::vector<uint64_t>> mutant_buckets;  // same shape, mutant
    std::map<int, uint64_t> bucket_totals;
};

void emerald_gate_sweep(Campaign& c, EmeraldSweepResult& out) {
    for (int i = 0; i < c.seeds; ++i) {
        const int64_t run_seed = (kEmeraldSeedBase ^ c.salt) + i;
        const GeneratedMap g = generate_map(run_seed, kEmeraldAct);
        const RoomAssignment ra_false =
            assign_room_types(g, kEmeraldAscension, /*has_emerald_key=*/false);
        const RoomAssignment ra_true =
            assign_room_types(g, kEmeraldAscension, /*has_emerald_key=*/true);

        // --- EXACT two-arm paired checks: trap 1's core claim ---------------
        exact(c, ra_true.emerald_x < 0 && ra_true.emerald_y < 0,
              "holding the emerald key still marked a burning elite node");
        exact(c, ra_true.elite_node_count == ra_false.elite_node_count,
              "elite PLACEMENT itself moved with the emerald key (only the "
              "post-placement marking draw should)");
        bool rooms_match = true;
        for (int y = 0; y < kGameMapFloors && rooms_match; ++y) {
            for (int x = 0; x < kGameMapCols; ++x) {
                if (ra_true.rooms[y][x] != ra_false.rooms[y][x]) {
                    rooms_match = false;
                    break;
                }
            }
        }
        exact(c, rooms_match,
              "the room grid differed between the two emerald-key arms of "
              "the SAME generated map");
        const int32_t expected_delta = ra_false.elite_node_count >= 1 ? 1 : 0;
        exact(c, ra_false.rng.counter - ra_true.rng.counter == expected_delta,
              "holding the emerald key did not remove exactly the one "
              "setEmeraldElite mapRng draw");

        if (ra_false.elite_node_count < 1) continue;
        const int k = ra_false.elite_node_count;
        exact(c, k >= 1 && k <= kGameMapFloors * kGameMapCols,
              "the placed elite-node count fell outside a sane range");
        const int rank = elite_rank_of(ra_false);
        exact(c, rank >= 0 && rank < k,
              "the chosen emerald-elite coordinates were not one of the "
              "placed elite nodes");
        if (rank < 0) continue;

        auto& bucket = out.buckets[k];
        if (bucket.empty()) bucket.assign(static_cast<std::size_t>(k), 0);
        ++bucket[static_cast<std::size_t>(rank)];
        auto& mbucket = out.mutant_buckets[k];
        if (mbucket.empty()) mbucket.assign(static_cast<std::size_t>(k), 0);
        ++mbucket[0];  // MUTANT: the rank is hardcoded to 0.
        ++out.bucket_totals[k];
    }
}

void build_emerald_table(const EmeraldSweepResult& r,
                         const std::map<int, std::vector<uint64_t>>& buckets,
                         std::vector<uint64_t>& observed,
                         std::vector<double>& law) {
    observed.clear();
    law.clear();
    uint64_t grand_total = 0;
    for (const auto& [k, n_k] : r.bucket_totals) grand_total += n_k;
    for (const auto& [k, counts] : buckets) {
        const uint64_t n_k = r.bucket_totals.at(k);
        const double per_cell =
            static_cast<double>(n_k) / (static_cast<double>(grand_total) *
                                        static_cast<double>(k));
        for (int idx = 0; idx < k; ++idx) {
            observed.push_back(counts[static_cast<std::size_t>(idx)]);
            law.push_back(per_cell);
        }
    }
}

// --- H5/H6: the Act-4 floor-gated canSpawn family ----------------------------
//
// Every relic in the family below gates on `ctx.floor` alone at a threshold
// (35, 40, 48 or 52) strictly under kAct4TestFloor -- read in full from
// src/engine/relics/relic_pickup_{common,uncommon,rare}.cpp (every
// `ctx.floor` occurrence in that directory is one of these rows or the
// unrelated act/campfire/deck gates isolated below): AncientTeaSet,
// CeramicFish, DreamCatcher, JuzuBracelet, MealTicket, Omamori, PotionBelt,
// RegalPillow, MawBank, SmilingMask, PreservedInsect, TinyChest (COMMON, 12
// of 33 rows); DarkstonePeriapt, FrozenEgg2, MeatOnTheBone, MoltenEgg2,
// QuestionCard, SingingBowl, ToxicEgg2, The Courier, Matryoshka (UNCOMMON, 9
// of 30 rows); PrayerWheel, WingBoots, OldCoin, Girya, PeacePipe, Shovel
// (RARE, 6 of 28 rows). At kAct4TestFloor every one of those thresholds has
// passed regardless of ascension band (design §4.3's 51/52 floor-base
// split), so the family is uniformly closed in Act 4 -- the `in_shop` clause
// on four of them (MawBank/SmilingMask/Courier/OldCoin) is already false by
// the FLOOR alone at this floor, whether or not the draw is a shop draw, so
// hypotheses 5 and 6 share one blocked-set derivation and differ only in
// which real call site (shop vs. elite combat reward) and which RNG lineage
// produced the draw.
constexpr uint16_t kBlockedCommon = 12;
constexpr uint16_t kBlockedUncommon = 9;
constexpr uint16_t kBlockedRare = 6;

RelicSpawnContext act4_relic_ctx(bool in_shop) {
    RelicSpawnContext ctx{};
    ctx.floor = kAct4TestFloor;
    ctx.act = 4;
    ctx.endless = false;
    ctx.in_shop = in_shop;
    // ISOLATE the floor-gated family: hold every OTHER canSpawn gate open so
    // a closed deck-content gate (the Bottled trio, UNCOMMON) cannot
    // masquerade as this family's signal. The campfire trio (Girya/Peace
    // Pipe/Shovel, RARE) and the two BOSS-only gates (Ectoplasm/Black Blood)
    // are unaffected either way -- the campfire trio's `floor >= 48` early
    // return already closes them before campfire_relic_count is read, and
    // Ectoplasm/Black Blood are not members of COMMON/UNCOMMON/RARE.
    ctx.deck_has_nonbasic_attack = true;
    ctx.deck_has_nonbasic_skill = true;
    ctx.deck_has_power = true;
    ctx.campfire_relic_count = 0;
    ctx.has_burning_blood = true;
    return ctx;
}

// One (tier, pool) stratum: `n` independent relicRng seeds, each a fresh
// initialize_relic_pools + one return_random_relic_key draw at
// kAct4TestFloor. `blocked` is the PRE-REGISTERED count above (asserted, not
// trusted); `allowed`/`size` are read back from the pool at seed 0, exactly
// as S2.44's can_spawn_stratum does.
void relic_tier_stratum(Campaign& c, int n, RelicTier tier, RelicPool pool,
                        bool in_shop, int64_t seed_base, int blocked,
                        std::vector<uint64_t>& consumed,
                        std::vector<uint64_t>& mutant_consumed,
                        int& allowed_out) {
    const int pidx = static_cast<int>(pool);
    consumed.assign(static_cast<std::size_t>(blocked) + 1, 0);
    mutant_consumed.assign(static_cast<std::size_t>(blocked) + 1, 0);
    int allowed = 0;
    for (int i = 0; i < n; ++i) {
        RunState rs{};
        rs.act = 4;
        rs.floor = kAct4TestFloor;
        rs.relic_rng = from_seed((seed_base ^ c.salt) + i);
        initialize_relic_pools(rs);
        const RelicSpawnContext ctx = act4_relic_ctx(in_shop);

        const int before = rs.relic_pool_count[pidx];
        if (i == 0) {
            int observed_blocked = 0;
            for (int k = 0; k < before; ++k) {
                const RelicId id = static_cast<RelicId>(rs.relic_pools[pidx][k]);
                if (!relic_can_spawn(id, ctx)) ++observed_blocked;
            }
            allowed = before - observed_blocked;
            exact(c, observed_blocked == blocked,
                  "the Act-4 floor-gated canSpawn family's blocked count "
                  "did not match the registered derivation for its tier");
        }
        exact(c, rs.relic_pool_count[pidx] == before,
              "the tier pool size varied between seeds");

        const RelicId id = return_random_relic_key(rs, tier, ctx);
        (void)id;
        const int after = rs.relic_pool_count[pidx];
        const int spent = before - after;
        exact(c, spent >= 1,
              "a relic draw consumed nothing from its pool");
        const std::size_t k = static_cast<std::size_t>(
            std::clamp(spent - 1, 0, blocked));
        ++consumed[k];
        // MUTANT: the canSpawn rejection puts the relic back instead of
        // permanently consuming it -- every observation lands at k == 0
        // (S2.44's M3, replayed here).
        ++mutant_consumed[0];
    }
    allowed_out = allowed;
}

constexpr int64_t kShopCommonSeedBase = INT64_C(0x53484F50);    // "SHOP"
constexpr int64_t kShopUncommonSeedBase = INT64_C(0x53484F51);
constexpr int64_t kShopRareSeedBase = INT64_C(0x53484F52);
constexpr int64_t kRewardCommonSeedBase = INT64_C(0x52455744);  // "REWD"
constexpr int64_t kRewardUncommonSeedBase = INT64_C(0x52455745);
constexpr int64_t kRewardRareSeedBase = INT64_C(0x52455746);

void relic_family_sweep(Campaign& c, bool in_shop, int64_t common_base,
                        int64_t uncommon_base, int64_t rare_base,
                        std::vector<uint64_t>& observed,
                        std::vector<uint64_t>& mutant_observed,
                        std::vector<double>& law) {
    observed.clear();
    mutant_observed.clear();
    law.clear();
    const int n = c.seeds / 3;
    struct Stratum {
        RelicTier tier;
        RelicPool pool;
        int64_t base;
        int blocked;
    };
    const Stratum strata[3] = {
        {RelicTier::COMMON, RelicPool::COMMON, common_base, kBlockedCommon},
        {RelicTier::UNCOMMON, RelicPool::UNCOMMON, uncommon_base,
         kBlockedUncommon},
        {RelicTier::RARE, RelicPool::RARE, rare_base, kBlockedRare},
    };
    for (const Stratum& s : strata) {
        std::vector<uint64_t> counts;
        std::vector<uint64_t> mutant_counts;
        int allowed = 0;
        relic_tier_stratum(c, n, s.tier, s.pool, in_shop, s.base, s.blocked,
                           counts, mutant_counts, allowed);
        const std::vector<double> stratum_law =
            sts::dist_check::s2::front_scan_blocked_law(allowed, s.blocked,
                                                         /*wanted=*/1);
        for (std::size_t k = 0; k < counts.size(); ++k) {
            observed.push_back(counts[k]);
            mutant_observed.push_back(mutant_counts[k]);
            law.push_back(stratum_law[k] / 3.0);
        }
    }
}

// --- The Heart's buffCount ladder (exact, outside the Holm family) ----------
//
// CorruptHeart.java:120-151, header note (4): buffCount is DETERMINISTIC once
// GAIN_ONE_STRENGTH is reached (phase 2 of the moveCount cycle, unconditional,
// no draw) -- 0 Artifact(2), 1 BeatOfDeath(1), 2 PainfulStabs(-1),
// 3 Strength(10), 4+ Strength(50) forever. This is a support/invariant claim,
// not a frequency one, so it is checked exactly rather than folded into the
// coin-flip hypothesis, over many independent seeds and many cycles per seed
// to also prove the 3-bit saturating counter never drifts past rung 4.
constexpr int64_t kHeartLadderSeedBase = INT64_C(0x4C414444);  // "LADD"
constexpr int kHeartLadderSeeds = 64;
constexpr int kHeartLadderCyclesPerSeed = 24;  // >= 8 GAIN_ONE_STRENGTH hits

void heart_ladder_sweep(Campaign& c) {
    auto cs = std::make_unique<CombatState>();
    for (int i = 0; i < kHeartLadderSeeds; ++i) {
        *cs = CombatState{};
        cs->monster_count = 1;
        cs->ai_rng = from_seed((kHeartLadderSeedBase ^ c.salt) + i);
        corrupt_heart_init(*cs, 0);  // DEBILITATE; buffCount stays 0
        uint32_t rung_expected = 0;
        for (int cycle = 0; cycle < kHeartLadderCyclesPerSeed; ++cycle) {
            corrupt_heart_roll_move(*cs, 0);
            if (cs->monsters[0].move_history[0] != kHeartGainOneStrength) {
                continue;
            }
            cs->action_head = cs->action_tail = cs->action_count = 0;
            corrupt_heart_take_turn(*cs, 0);
            exact(c, cs->action_count == 3,
                  "a GAIN_ONE_STRENGTH turn did not queue exactly "
                  "{Strength(+2), ladder rung, RollMove}");
            if (cs->action_count != 3) continue;
            const ActionQueueItem& strength_item = cs->action_queue[0];
            const ActionQueueItem& ladder_item = cs->action_queue[1];
            const ActionQueueItem& roll_item = cs->action_queue[2];
            exact(c,
                  strength_item.opcode ==
                          static_cast<uint16_t>(Opcode::APPLY_POWER) &&
                      apply_power_id_from_flags(strength_item.flags) ==
                          PowerId::STRENGTH &&
                      strength_item.amount == 2,
                  "the Heart's always-+2 Strength item was not queued first "
                  "or was not exactly +2 with no negated debuff present");
            exact(c, roll_item.opcode == static_cast<uint16_t>(Opcode::ROLL_MOVE),
                  "the Heart's GAIN_ONE_STRENGTH turn did not end in "
                  "ROLL_MOVE");

            const PowerId ladder_power =
                apply_power_id_from_flags(ladder_item.flags);
            const int32_t ladder_amount = ladder_item.amount;
            switch (rung_expected) {
                case 0:
                    exact(c,
                          ladder_power == PowerId::ARTIFACT &&
                              ladder_amount == kCorruptHeartArtifactAmount,
                          "buffCount rung 0 did not queue Artifact(2)");
                    break;
                case 1:
                    exact(c,
                          ladder_power == PowerId::BEAT_OF_DEATH &&
                              ladder_amount == kCorruptHeartLadderBeatAmount,
                          "buffCount rung 1 did not queue Beat of Death(1)");
                    break;
                case 2:
                    exact(c,
                          ladder_power == PowerId::PAINFUL_STABS &&
                              ladder_amount == -1,
                          "buffCount rung 2 did not queue Painful Stabs(-1)");
                    break;
                case 3:
                    exact(c,
                          ladder_power == PowerId::STRENGTH &&
                              ladder_amount == kCorruptHeartLadderStrength3,
                          "buffCount rung 3 did not queue Strength(10)");
                    break;
                default:
                    exact(c,
                          ladder_power == PowerId::STRENGTH &&
                              ladder_amount == kCorruptHeartLadderStrengthMax,
                          "buffCount rung 4+ did not queue Strength(50)");
                    break;
            }
            rung_expected = std::min<uint32_t>(rung_expected + 1,
                                               kCorruptHeartBuffCountSaturation);
            const uint32_t rung_after =
                corrupt_heart_buff_count(cs->monsters[0]);
            exact(c, rung_after == rung_expected,
                  "buffCount did not advance to the expected rung (or did "
                  "not saturate at 4)");
            cs->action_head = cs->action_tail = cs->action_count = 0;
        }
        exact(c, rung_expected == sts::engine::kCorruptHeartBuffCountSaturation,
              "a 24-cycle Heart sweep never reached buffCount saturation -- "
              "the cycle-length assumption in this file is stale");
    }
}

// --- Driver -------------------------------------------------------------------

void print_usage() { std::cerr << "usage: dist_check_s3 [--seeds N]\n"; }

bool parse_args(int argc, char** argv, Campaign& campaign) {
    bool saw_seeds = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--seeds" && i + 1 < argc) {
            if (saw_seeds) {
                std::cerr << "dist_check_s3: repeated --seeds\n";
                return false;
            }
            saw_seeds = true;
            const std::string_view value(argv[++i]);
            int parsed = 0;
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(),
                                parsed);
            if (error != std::errc{} || end != value.data() + value.size()) {
                std::cerr << "dist_check_s3: invalid --seeds value\n";
                return false;
            }
            campaign.seeds = parsed;
        } else {
            print_usage();
            return false;
        }
    }
    if (campaign.seeds < kMinimumSeeds) {
        std::cerr << "dist_check_s3: --seeds must be >= " << kMinimumSeeds
                  << " (tier-4 frozen scope)\n";
        return false;
    }
    return true;
}

void run_campaign(Campaign& campaign) {
    uint64_t shield_fortify = 0;
    uint64_t shield_bash = 0;
    uint64_t mutant_shield_fortify = 0;
    uint64_t mutant_shield_bash = 0;
    spire_shield_case0_sweep(campaign, shield_fortify, shield_bash,
                             mutant_shield_fortify, mutant_shield_bash);
    add_test(campaign, "s3.monster.spire_shield_case0_coin",
             {shield_fortify, shield_bash}, {0.5, 0.5});
    add_mutant(campaign, "mutant.spire_shield_case0_always_fortify",
               {mutant_shield_fortify, mutant_shield_bash}, {0.5, 0.5});

    uint64_t spear_piercer = 0;
    uint64_t spear_burn_strike = 0;
    spire_spear_case2_sweep(campaign, spear_piercer, spear_burn_strike);
    add_test(campaign, "s3.monster.spire_spear_case2_coin",
             {spear_piercer, spear_burn_strike}, {0.5, 0.5});

    uint64_t heart_blood_shots = 0;
    uint64_t heart_echo_attack = 0;
    corrupt_heart_case0_sweep(campaign, heart_blood_shots, heart_echo_attack);
    add_test(campaign, "s3.monster.corrupt_heart_case0_coin",
             {heart_blood_shots, heart_echo_attack}, {0.5, 0.5});

    EmeraldSweepResult emerald;
    emerald_gate_sweep(campaign, emerald);
    std::vector<uint64_t> emerald_observed;
    std::vector<double> emerald_law;
    build_emerald_table(emerald, emerald.buckets, emerald_observed,
                        emerald_law);
    add_test(campaign, "s3.map.emerald_gate_elite_index", emerald_observed,
             emerald_law);
    std::vector<uint64_t> emerald_mutant_observed;
    std::vector<double> emerald_mutant_law;  // identical shape/law to the real table
    build_emerald_table(emerald, emerald.mutant_buckets,
                        emerald_mutant_observed, emerald_mutant_law);
    add_mutant(campaign, "mutant.emerald_gate_always_first_node",
               emerald_mutant_observed, emerald_mutant_law);

    std::vector<uint64_t> shop_observed;
    // Only the reward context registers a canSpawn-family mutant (one
    // representative per mechanism class, S2.44's economy) -- the shop
    // context's mutant table is computed by the shared sweep function but
    // deliberately not registered.
    std::vector<uint64_t> shop_mutant_observed;
    std::vector<double> shop_law;
    relic_family_sweep(campaign, /*in_shop=*/true, kShopCommonSeedBase,
                       kShopUncommonSeedBase, kShopRareSeedBase, shop_observed,
                       shop_mutant_observed, shop_law);
    add_test(campaign, "s3.relic.act4_shop_can_spawn_front_scan",
             shop_observed, shop_law);

    std::vector<uint64_t> reward_observed;
    std::vector<uint64_t> reward_mutant_observed;
    std::vector<double> reward_law;
    relic_family_sweep(campaign, /*in_shop=*/false, kRewardCommonSeedBase,
                       kRewardUncommonSeedBase, kRewardRareSeedBase,
                       reward_observed, reward_mutant_observed, reward_law);
    add_test(campaign, "s3.relic.act4_reward_can_spawn_front_scan",
             reward_observed, reward_law);
    add_mutant(campaign, "mutant.act4_can_spawn_rejection_returns_relic",
               reward_mutant_observed, reward_law);

    heart_ladder_sweep(campaign);
}

}  // namespace

int main(int argc, char** argv) {
    Campaign stage_one;
    if (!parse_args(argc, argv, stage_one)) return 2;
    stage_one.salt = 0;
    run_campaign(stage_one);

    const auto score = [](const std::map<std::string, CellTable>& tables) {
        std::vector<ChiSquareResult> out;
        out.reserve(tables.size());
        for (const auto& [name, cells] : tables) {
            out.push_back(sts::dist_check::chi_square(name, cells.first,
                                                      cells.second));
        }
        return out;
    };
    const std::vector<ChiSquareResult> tests = score(stage_one.tables);
    const std::vector<ChiSquareResult> mutants = score(stage_one.mutant_tables);

    bool flagged = false;
    std::cout << "dist_check_s3 seeds=" << stage_one.seeds
              << " stochastic_hypotheses=" << tests.size()
              << " family_alpha=" << kFamilyAlpha
              << " correction=Holm-Bonferroni+replicate\n";
    if (tests.size() != static_cast<std::size_t>(kFamilySize)) {
        std::cout << "FLAG registration: the family is pre-registered at "
                  << kFamilySize << " hypotheses\n";
        flagged = true;
    }

    const auto stage_one_holm =
        sts::dist_check::holm_bonferroni(tests, kFamilyAlpha);
    const double strictest =
        kFamilyAlpha / static_cast<double>(tests.size());
    std::vector<sts::dist_check::HolmDecision> mutant_stage_one;
    mutant_stage_one.reserve(mutants.size());
    for (const ChiSquareResult& mutant : mutants) {
        mutant_stage_one.push_back(sts::dist_check::HolmDecision{
            mutant.name, mutant.p_value, strictest,
            mutant.p_value <= strictest});
    }

    const bool needs_replicate =
        std::any_of(stage_one_holm.begin(), stage_one_holm.end(),
                    [](const auto& d) { return d.rejected; }) ||
        std::any_of(mutant_stage_one.begin(), mutant_stage_one.end(),
                    [](const auto& d) { return d.rejected; });

    Campaign replicate;
    replicate.seeds = stage_one.seeds;
    replicate.salt = kReplicateSeedSalt;
    if (needs_replicate) {
        run_campaign(replicate);
        std::cout << "replicate stage ran: seed blocks are the stage-one "
                     "blocks XOR the pre-registered salt\n";
    }

    const auto replicate_p = [](const std::map<std::string, CellTable>* from) {
        return [from](const std::string& name) {
            const auto cells = from->find(name);
            if (cells == from->end()) {
                throw std::invalid_argument(
                    "dist_check_s3: no replicate table for " + name);
            }
            return sts::dist_check::chi_square(name, cells->second.first,
                                               cells->second.second)
                .p_value;
        };
    };
    const auto family_final = sts::dist_check::confirm_by_replicate(
        stage_one_holm, replicate_p(&replicate.tables));
    const auto mutant_final = sts::dist_check::confirm_by_replicate(
        mutant_stage_one, replicate_p(&replicate.mutant_tables));

    std::cout << std::scientific << std::setprecision(6);
    for (const auto& verdict : family_final) {
        const auto it =
            std::find_if(tests.begin(), tests.end(),
                         [&](const ChiSquareResult& test) {
                             return test.name == verdict.name;
                         });
        const char* label = verdict.rejected ? "FLAG "
                            : verdict.replicated ? "RETAINED-AFTER-REPLICATE "
                                                 : "PASS ";
        std::cout << label << verdict.name << " n=" << it->sample_count
                  << " chi2=" << it->statistic
                  << " df=" << it->degrees_of_freedom
                  << " p=" << verdict.stage_one_p;
        if (verdict.replicated) {
            std::cout << " replicate_p=" << verdict.replicate_p;
        }
        std::cout << " holm=" << verdict.threshold << '\n';
        flagged = flagged || verdict.rejected;
    }

    for (const auto& verdict : mutant_final) {
        std::cout << (verdict.rejected ? "CONTROL-REJECTED "
                                       : "CONTROL-SURVIVED ")
                  << verdict.name << " p=" << verdict.stage_one_p;
        if (verdict.replicated) {
            std::cout << " replicate_p=" << verdict.replicate_p;
        }
        std::cout << " holm=" << verdict.threshold << '\n';
        if (!verdict.rejected) {
            flagged = true;
            stage_one.exact_failures.push_back(
                "negative control survived the two-stage rule: " +
                verdict.name);
        }
    }

    for (const std::string& failure : stage_one.exact_failures) {
        std::cout << "FLAG exact: " << failure << '\n';
        flagged = true;
    }
    for (const std::string& failure : replicate.exact_failures) {
        std::cout << "FLAG exact (replicate stage): " << failure << '\n';
        flagged = true;
    }

    if (flagged) {
        std::cout << "RESULT FLAGGED -- stop-the-line divergence\n";
        return 1;
    }
    std::cout << "RESULT PASS\n";
    return 0;
}
