// B4.2 map room-type assignment suite. Verifies sts::engine::map_rooms against
// the LIVE-GAME oracle: for the 20-seed A20-Ironclad campaign corpus
// (b13_on20b, the same golden B4.1 uses), the assigned room grid must match the
// game's dumped map SYMBOL-for-symbol, and the mapRng state at the END of
// generateMap must match the oracle's post-generateMap {counter, s0, s1}
// triple. That triple is the strongest possible proof of the two RNG-bearing
// steps this task owns:
//   * the TRAP-12 direct-XS128 Collections.shuffle (advances s0/s1 by size-1
//     next_long draws, counter unchanged), and
//   * the setEmeraldElite draw (+1 wrapper draw AFTER assignment, the B4.1
//     live-oracle finding).
// A wrong shuffle length/bound or a missing/extra emerald draw breaks the
// triple; a wrong permutation or rule breaks the symbol match. Together they
// pin the whole B4.2 contract to the live game.
//
// Golden: tests/golden/map_paths/oracle_maps.txt -- per-seed the header line
// carries the POST-generateMap {counter, s0, s1} (B4.1 verified counter is
// path-gen + 1; here we land the full three-field match) and each node carries
// its room `symbol`.
//
// STS_GOLDEN_DIR is injected by tests/CMakeLists.txt as an absolute path.

#include "sts/engine/map_rooms.hpp"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "sts/engine/map_gen.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/rng_xs128.hpp"
#include "sts/engine/run_state.hpp"

#ifndef STS_GOLDEN_DIR
#error "STS_GOLDEN_DIR must be defined by the build (see tests/CMakeLists.txt)"
#endif

namespace {

using sts::engine::assign_room_types;
using sts::engine::generate_map;
using sts::engine::GeneratedMap;
using sts::engine::RoomAssignment;
using sts::engine::RoomType;
using sts::engine::room_symbol;

constexpr int kA20 = 20;

std::string GoldenPath(const std::string& relative) {
    return std::string(STS_GOLDEN_DIR) + "/" + relative;
}

struct OracleNode {
    int x;
    int y;
    std::string symbol;
};

struct OracleSeed {
    int64_t seed = 0;
    int counter = 0;
    uint64_t s0 = 0;
    uint64_t s1 = 0;
    std::vector<OracleNode> nodes;
};

// Parses the whitespace-delimited golden (format documented in its header).
// Children coords are read past but not retained (B4.1 verifies edges; here we
// need only the per-node symbol and the header RNG triple).
std::vector<OracleSeed> LoadOracle() {
    std::vector<OracleSeed> seeds;
    std::ifstream in(GoldenPath("map_paths/oracle_maps.txt"));
    if (!in.is_open()) return seeds;

    auto next = [&](std::string& out) -> bool {
        while (in >> out) {
            if (!out.empty() && out[0] == '#') {
                std::string rest;
                std::getline(in, rest);
                continue;
            }
            return true;
        }
        return false;
    };

    std::string tok;
    if (!next(tok) || tok != "SEEDS") return seeds;
    int n_seeds = 0;
    in >> n_seeds;
    for (int s = 0; s < n_seeds; ++s) {
        std::string kw;
        if (!next(kw) || kw != "SEED") break;
        OracleSeed os;
        int n_nodes = 0;
        long long s0_signed = 0, s1_signed = 0;
        in >> os.seed >> os.counter >> s0_signed >> s1_signed >> n_nodes;
        os.s0 = static_cast<uint64_t>(s0_signed);
        os.s1 = static_cast<uint64_t>(s1_signed);
        for (int i = 0; i < n_nodes; ++i) {
            OracleNode nd;
            int nch = 0;
            in >> nd.x >> nd.y >> nd.symbol >> nch;
            for (int c = 0; c < nch; ++c) {
                int cx = 0, cy = 0;
                in >> cx >> cy;
            }
            os.nodes.push_back(nd);
        }
        seeds.push_back(std::move(os));
    }
    return seeds;
}

TEST(MapRooms, OracleCorpusLoads) {
    auto oracle = LoadOracle();
    ASSERT_EQ(oracle.size(), 20u) << "expected 20 seeds in the b13_on20b golden";
    for (const auto& os : oracle) {
        EXPECT_GT(os.nodes.size(), 0u);
        EXPECT_GT(os.counter, 0);
    }
}

// PRIMARY ACCEPTANCE (1): assigned room symbols match the oracle dump for every
// edge node, all 20 corpus seeds. The oracle dumps only nodes with edges, which
// is exactly the set assign_room_types fills; every dumped node must carry the
// symbol we assign.
TEST(MapRooms, RoomSymbolsMatchOracleAllSeeds) {
    auto oracle = LoadOracle();
    ASSERT_EQ(oracle.size(), 20u);
    for (const auto& os : oracle) {
        GeneratedMap g = generate_map(os.seed, 1);
        RoomAssignment ra = assign_room_types(g, kA20);
        for (const auto& onode : os.nodes) {
            const char got = room_symbol(ra.at(onode.x, onode.y));
            ASSERT_EQ(onode.symbol.size(), 1u);
            EXPECT_EQ(got, onode.symbol[0])
                << "room symbol mismatch at (" << onode.x << "," << onode.y
                << ") seed " << os.seed << ": got '" << got << "' want '"
                << onode.symbol << "'";
        }
    }
}

// PRIMARY ACCEPTANCE (2): the FULL post-generateMap {counter, s0, s1} triple
// matches the live oracle for all 20 seeds. This proves, together, the trap-12
// shuffle (raw-state advance, no counter advance) and the setEmeraldElite draw
// (+1 wrapper draw). No component of the tail is left unverified.
TEST(MapRooms, PostGenerateMapRngTripleMatchesOracle) {
    auto oracle = LoadOracle();
    ASSERT_EQ(oracle.size(), 20u);
    for (const auto& os : oracle) {
        GeneratedMap g = generate_map(os.seed, 1);
        RoomAssignment ra = assign_room_types(g, kA20);
        EXPECT_EQ(ra.rng.counter, os.counter)
            << "post-generateMap counter mismatch, seed " << os.seed;
        EXPECT_EQ(ra.rng.s0, os.s0)
            << "post-generateMap s0 mismatch, seed " << os.seed;
        EXPECT_EQ(ra.rng.s1, os.s1)
            << "post-generateMap s1 mismatch, seed " << os.seed;
    }
}

// The counter advance across the whole tail is exactly +1 (the setEmeraldElite
// wrapper draw); the trap-12 shuffle contributes 0. Cross-checks B4.1's
// finding directly on the B4.2 side.
TEST(MapRooms, TailCounterAdvanceIsExactlyEmeraldDraw) {
    auto oracle = LoadOracle();
    ASSERT_EQ(oracle.size(), 20u);
    for (const auto& os : oracle) {
        GeneratedMap g = generate_map(os.seed, 1);
        const int path_gen_counter = g.rng.counter;
        RoomAssignment ra = assign_room_types(g, kA20);
        EXPECT_EQ(ra.rng.counter, path_gen_counter + 1)
            << "tail counter advance != 1 (emerald draw), seed " << os.seed;
        EXPECT_GE(ra.elite_node_count, 1)
            << "no elite nodes -> setEmeraldElite would not fire, seed " << os.seed;
    }
}

// TRAP-12 NAMED TEST. Two facts, isolated from full generation:
//   (a) the direct-XS128 shuffle leaves the wrapper `counter` UNCHANGED;
//   (b) it advances the raw (s0,s1) by EXACTLY (size-1) next_long draws -- the
//       Fisher-Yates draw count -- which we confirm by stepping an independent
//       RandomXS128 (size-1) times from the same start state. (For these small
//       bounds RandomXS128.nextInt never hits the ~2^-63 rejection retry, so one
//       next_int == one next_long of state advance.)
TEST(MapRooms, Trap12ShuffleAdvancesRawStateNotCounter) {
    using sts::engine::from_seed;
    using sts::engine::RandomXS128;
    using sts::engine::RngStream;
    using sts::engine::xs128_room_shuffle;

    for (int size : {2, 5, 17, 42, 105}) {
        std::vector<RoomType> list(static_cast<size_t>(size), RoomType::Monster);
        for (int i = 0; i < size; ++i)
            list[static_cast<size_t>(i)] = static_cast<RoomType>((i % 6) + 1);

        RngStream rng = from_seed(1790050543751LL + size);
        rng.counter = 137;  // arbitrary non-zero, to prove it is preserved
        const int32_t counter_before = rng.counter;
        const uint64_t s0_before = rng.s0, s1_before = rng.s1;

        xs128_room_shuffle(rng, list.data(), size);

        // (a) counter unchanged.
        EXPECT_EQ(rng.counter, counter_before) << "size " << size;

        // (b) raw state == (size-1) next_long steps from the start state.
        RandomXS128 ref(s0_before, s1_before);
        for (int k = 0; k < size - 1; ++k) (void)ref.next_long();
        EXPECT_EQ(rng.s0, ref.state0()) << "size " << size;
        EXPECT_EQ(rng.s1, ref.state1()) << "size " << size;
    }
}

// The shuffle permutation is deterministic and a genuine permutation (multiset
// preserved). Determinism is what makes the oracle triple + symbol match a
// stable acceptance; the multiset check guards against an index bug corrupting
// the list.
TEST(MapRooms, Trap12ShuffleIsDeterministicPermutation) {
    using sts::engine::from_seed;
    using sts::engine::RngStream;
    using sts::engine::xs128_room_shuffle;

    constexpr int kSize = 60;
    auto make = []() {
        std::vector<RoomType> v(kSize);
        for (int i = 0; i < kSize; ++i)
            v[static_cast<size_t>(i)] = static_cast<RoomType>((i % 6) + 1);
        return v;
    };
    std::vector<RoomType> a = make(), b = make();
    RngStream ra = from_seed(424242), rb = from_seed(424242);
    xs128_room_shuffle(ra, a.data(), kSize);
    xs128_room_shuffle(rb, b.data(), kSize);
    EXPECT_EQ(a, b) << "shuffle not deterministic for a fixed start state";

    // multiset preserved: each of the 6 kinds keeps its count (10 each here).
    int before[7] = {0}, after[7] = {0};
    std::vector<RoomType> orig = make();
    for (auto r : orig) ++before[static_cast<int>(r)];
    for (auto r : a) ++after[static_cast<int>(r)];
    for (int k = 1; k <= 6; ++k)
        EXPECT_EQ(before[k], after[k]) << "kind " << k << " count changed";
}

// QUOTA TABLE: generateRoomTypes counts at A0 vs A20. The only ascension effect
// is the elite x1.6 branch (AbstractDungeon.java:571-575); everything else is
// identical. Hand-computed via the same float Math.round the game uses.
TEST(MapRooms, QuotaTableA0VsA20) {
    using sts::engine::compute_room_quota;

    // Hand-derived from Math.round((float)count * chance):
    //  shop=round(.05c) rest=round(.12c) event=round(.22c)
    //  elite(A0)=round(.08c)  elite(A20)=round(.08c*1.6)=round(.128c)
    struct Case { int count, shop, rest, event, elite0, elite20; };
    const Case cases[] = {
        // count=50: shop 2.5->3? Math.round(2.5f)=3 (half-up). rest 6.0->6.
        //   event 11.0->11. elite0 4.0->4. elite20 6.4->6.
        {50, 3, 6, 11, 4, 6},
        // count=62 (a typical corpus count): shop 3.1->3, rest 7.44->7,
        //   event 13.64->14, elite0 4.96->5, elite20 7.936->8.
        {62, 3, 7, 14, 5, 8},
        // count=40: shop 2.0->2, rest 4.8->5, event 8.8->9, elite0 3.2->3,
        //   elite20 5.12->5.
        {40, 2, 5, 9, 3, 5},
        // count=10: shop 0.5->1 (half-up), rest 1.2->1, event 2.2->2,
        //   elite0 0.8->1, elite20 1.28->1.
        {10, 1, 1, 2, 1, 1},
    };
    for (const auto& c : cases) {
        auto q0 = compute_room_quota(c.count, 0);
        auto q20 = compute_room_quota(c.count, kA20);
        EXPECT_EQ(q0.shop, c.shop) << "count " << c.count;
        EXPECT_EQ(q0.rest, c.rest) << "count " << c.count;
        EXPECT_EQ(q0.event, c.event) << "count " << c.count;
        EXPECT_EQ(q0.treasure, 0) << "count " << c.count;
        EXPECT_EQ(q0.elite, c.elite0) << "A0 elite, count " << c.count;
        EXPECT_EQ(q20.elite, c.elite20) << "A20 elite, count " << c.count;
        // Non-elite quotas identical across ascension.
        EXPECT_EQ(q0.shop, q20.shop);
        EXPECT_EQ(q0.rest, q20.rest);
        EXPECT_EQ(q0.event, q20.event);
        // A20 elite >= A0 elite (x1.6).
        EXPECT_GE(q20.elite, q0.elite) << "count " << c.count;
    }
}

// Java 8 Math.round(float) half-up on exact half-integers (the parity hazard
// H2). round(0.5)=1, round(2.5)=3, round(3.5)=4; and a plain non-half value.
TEST(MapRooms, JavaRoundFloatHalfUp) {
    using sts::engine::java_round_f;
    EXPECT_EQ(java_round_f(0.5f), 1);
    EXPECT_EQ(java_round_f(1.5f), 2);
    EXPECT_EQ(java_round_f(2.5f), 3);
    EXPECT_EQ(java_round_f(3.5f), 4);
    EXPECT_EQ(java_round_f(2.49f), 2);
    EXPECT_EQ(java_round_f(2.51f), 3);
    EXPECT_EQ(java_round_f(0.0f), 0);
    EXPECT_EQ(java_round_f(7.0f), 7);
}

// Structural rule checks against the oracle: fixed rows, no-rest-row-13,
// no-elite/rest on rows 0-4, treasure only on row 8, rest only rows 5-12+14.
// Proven directly from the assigned grid over all 20 seeds.
TEST(MapRooms, StructuralRoomRulesHoldAllSeeds) {
    using namespace sts::engine;
    auto oracle = LoadOracle();
    ASSERT_EQ(oracle.size(), 20u);
    for (const auto& os : oracle) {
        GeneratedMap g = generate_map(os.seed, 1);
        RoomAssignment ra = assign_room_types(g, kA20);
        for (int y = 0; y < kGameMapFloors; ++y) {
            for (int x = 0; x < kGameMapCols; ++x) {
                if (!g.at(x, y).has_edges()) continue;
                const RoomType r = ra.at(x, y);
                // Fixed rows.
                if (y == 0) { EXPECT_EQ(r, RoomType::Monster) << os.seed; }
                if (y == 8) { EXPECT_EQ(r, RoomType::Treasure) << os.seed; }
                if (y == 14) { EXPECT_EQ(r, RoomType::Rest) << os.seed; }
                // Rest never on rows 0-4 or 13.
                if (y <= 4 || y == 13) {
                    EXPECT_NE(r, RoomType::Rest) << "(" << x << "," << y << ") " << os.seed;
                }
                // Elite never on rows 0-4.
                if (y <= 4) {
                    EXPECT_NE(r, RoomType::Elite) << "(" << x << "," << y << ") " << os.seed;
                }
                // Treasure only on the fixed row 8.
                if (y != 8) {
                    EXPECT_NE(r, RoomType::Treasure) << "(" << x << "," << y << ") " << os.seed;
                }
                // Every connected node ends assigned (no None left).
                EXPECT_NE(r, RoomType::None) << "(" << x << "," << y << ") " << os.seed;
            }
        }
    }
}

// The RunState room_type encoding round-trips the assigned grid, and the stored
// map_rng equals the end-of-generateMap state. (B4.3 owns full RunState
// population; this mirrors B4.1's edge-encoding continuity.)
TEST(MapRooms, RunStateRoomEncodingRoundTrips) {
    using namespace sts::engine;
    auto oracle = LoadOracle();
    ASSERT_EQ(oracle.size(), 20u);
    for (const auto& os : oracle) {
        GeneratedMap g = generate_map(os.seed, 1);
        RoomAssignment ra = assign_room_types(g, kA20);
        RunState rs{};
        encode_rooms_into_run_state(ra, rs);
        EXPECT_EQ(rs.map_rng.counter, ra.rng.counter) << os.seed;
        EXPECT_EQ(rs.map_rng.s0, ra.rng.s0);
        EXPECT_EQ(rs.map_rng.s1, ra.rng.s1);
        for (int y = 0; y < kGameMapFloors; ++y) {
            for (int x = 0; x < kGameMapCols; ++x) {
                EXPECT_EQ(rs.map[run_state_map_index(x, y)].room_type,
                          static_cast<uint8_t>(ra.at(x, y)))
                    << "(" << x << "," << y << ") " << os.seed;
            }
        }
    }
}

}  // namespace
