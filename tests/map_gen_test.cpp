// B4.1 map path-generation suite. Verifies sts::engine::map_gen against the
// LIVE-GAME oracle: for the 20-seed A20-Ironclad campaign corpus (b13_on20b),
// the generated node/edge graph must match the game's dumped map node-for-node,
// and the post-path mapRng draw count must equal the oracle's mapRng.counter.
//
// Why the counter (not s0/s1) is the RNG oracle here: the game's dumped mapRng
// state is captured AFTER full generateMap = path generation + the trap-12
// room-type Collections.shuffle + setEmeraldElite. The shuffle advances the raw
// xorshift state (s0/s1) but NOT the wrapper counter (scoping report §5 / trap
// 12); setEmeraldElite makes exactly ONE wrapper draw AFTER room assignment (see
// PathGenCounterMatchesOracleMinusEmerald for the full accounting and the
// live-oracle finding that overturns the report's H6/R3). So a correct B4.1
// path-generation counter equals the oracle counter minus 1 (all 20 seeds), and
// its post-path raw state is what the B4.2 shuffle + emerald draw advance from.
// B4.2 lands the full {counter, s0, s1} three-field match.
//
// Golden: tests/golden/map_paths/oracle_maps.txt, derived from campaign
// b13_on20b (fork 04477E4E). Curated golden vector (working-agreements
// exception), not a raw campaign artifact.
//
// STS_GOLDEN_DIR is injected by tests/CMakeLists.txt as an absolute path.

#include "sts/engine/map_gen.hpp"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_state.hpp"

#ifndef STS_GOLDEN_DIR
#error "STS_GOLDEN_DIR must be defined by the build (see tests/CMakeLists.txt)"
#endif

namespace {

using sts::engine::GeneratedMap;
using sts::engine::generate_map;

std::string GoldenPath(const std::string& relative) {
    return std::string(STS_GOLDEN_DIR) + "/" + relative;
}

struct Child {
    int x;
    int y;
};

struct OracleNode {
    int x;
    int y;
    std::string symbol;
    std::vector<Child> children;
};

struct OracleSeed {
    int64_t seed = 0;
    int counter = 0;
    uint64_t s0 = 0;
    uint64_t s1 = 0;
    std::vector<OracleNode> nodes;
};

// Parses the whitespace-delimited golden (format documented in its header).
std::vector<OracleSeed> LoadOracle() {
    std::vector<OracleSeed> seeds;
    std::ifstream in(GoldenPath("map_paths/oracle_maps.txt"));
    if (!in.is_open()) return seeds;

    std::string tok;
    auto next = [&](std::string& out) -> bool {
        while (in >> out) {
            if (!out.empty() && out[0] == '#') {  // comment line -> skip to EOL
                std::string rest;
                std::getline(in, rest);
                continue;
            }
            return true;
        }
        return false;
    };

    if (!next(tok) || tok != "SEEDS") return seeds;
    int n_seeds = 0;
    in >> n_seeds;
    for (int s = 0; s < n_seeds; ++s) {
        std::string kw;
        if (!next(kw) || kw != "SEED") break;
        OracleSeed os;
        int n_nodes = 0;
        // s0/s1 are signed 64-bit longs in the dump; read as signed then bitcast.
        long long s0_signed = 0, s1_signed = 0;
        in >> os.seed >> os.counter >> s0_signed >> s1_signed >> n_nodes;
        os.s0 = static_cast<uint64_t>(s0_signed);
        os.s1 = static_cast<uint64_t>(s1_signed);
        for (int i = 0; i < n_nodes; ++i) {
            OracleNode nd;
            int nch = 0;
            in >> nd.x >> nd.y >> nd.symbol >> nch;
            for (int c = 0; c < nch; ++c) {
                Child ch;
                in >> ch.x >> ch.y;
                nd.children.push_back(ch);
            }
            os.nodes.push_back(nd);
        }
        seeds.push_back(std::move(os));
    }
    return seeds;
}

// Collects the generated graph's nodes-with-edges into the same shape the
// oracle dump uses (only nodes with >=1 outgoing edge; children = edge targets).
std::vector<OracleNode> CollectGenerated(const GeneratedMap& g) {
    std::vector<OracleNode> out;
    for (int y = 0; y < sts::engine::kGameMapFloors; ++y) {
        for (int x = 0; x < sts::engine::kGameMapCols; ++x) {
            const sts::engine::GenNode& n = g.at(x, y);
            if (!n.has_edges()) continue;
            OracleNode nd;
            nd.x = x;
            nd.y = y;
            for (int i = 0; i < n.edge_count; ++i) {
                nd.children.push_back(Child{n.edges[i].dst_x, n.edges[i].dst_y});
            }
            out.push_back(nd);
        }
    }
    return out;
}

void SortChildren(std::vector<Child>& c) {
    std::sort(c.begin(), c.end(), [](const Child& a, const Child& b) {
        return a.x != b.x ? a.x < b.x : a.y < b.y;
    });
}

TEST(MapGen, OracleCorpusLoads) {
    auto oracle = LoadOracle();
    ASSERT_EQ(oracle.size(), 20u) << "expected 20 seeds in the b13_on20b golden";
    for (const auto& os : oracle) {
        EXPECT_GT(os.nodes.size(), 0u);
        EXPECT_GT(os.counter, 0);
    }
}

// Primary B4.1 acceptance: generated edges match the oracle's dumped map
// node-for-node, for ALL 20 corpus seeds (the ledger asks >= 3).
TEST(MapGen, EdgesMatchOracleNodeForNode) {
    auto oracle = LoadOracle();
    ASSERT_EQ(oracle.size(), 20u);
    for (const auto& os : oracle) {
        // Act 1: mapRng = from_seed(run_seed + 1).
        GeneratedMap g = generate_map(os.seed, 1);
        auto gen = CollectGenerated(g);

        ASSERT_EQ(gen.size(), os.nodes.size())
            << "node-with-edges count mismatch for seed " << os.seed;

        // Index oracle nodes by (x,y).
        auto find_oracle = [&](int x, int y) -> const OracleNode* {
            for (const auto& n : os.nodes) {
                if (n.x == x && n.y == y) return &n;
            }
            return nullptr;
        };
        for (auto& gnode : gen) {
            const OracleNode* onode = find_oracle(gnode.x, gnode.y);
            ASSERT_NE(onode, nullptr)
                << "generated node (" << gnode.x << "," << gnode.y
                << ") absent from oracle for seed " << os.seed;
            std::vector<Child> gc = gnode.children;
            std::vector<Child> oc = onode->children;
            SortChildren(gc);
            SortChildren(oc);
            ASSERT_EQ(gc.size(), oc.size())
                << "child-count mismatch at (" << gnode.x << "," << gnode.y
                << ") seed " << os.seed;
            for (size_t i = 0; i < gc.size(); ++i) {
                EXPECT_EQ(gc[i].x, oc[i].x)
                    << "child x mismatch at (" << gnode.x << "," << gnode.y
                    << ") seed " << os.seed;
                EXPECT_EQ(gc[i].y, oc[i].y)
                    << "child y mismatch at (" << gnode.x << "," << gnode.y
                    << ") seed " << os.seed;
            }
        }
    }
}

// The mapRng WRAPPER counter (design doc: one increment per wrapper draw). Path
// generation is B4.1's whole job and ends BEFORE room-type assignment. The
// oracle counter is captured after the FULL generateMap, which -- on the
// fully-unlocked A20 profile the corpus was recorded on -- makes exactly ONE
// more wrapper draw than path generation:
//
//   generateMap (AbstractDungeon.java:510-540):
//     1. generateDungeon (path gen)        -> B4.1: N wrapper draws  <-- HERE
//     2. generateRoomTypes                 -> no RNG
//     3. assignRowAsRoomType x3            -> no RNG
//     4. distributeRoomsAcrossMap: Collections.shuffle(roomList, rng.random)
//          (RoomTypeAssigner.java:135)     -> RAW xorshift only, counter += 0
//          + assignRoomsToNodes            -> no RNG
//     5. setEmeraldElite (AbstractDungeon.java:539/542-556)
//          mapRng.random(0, eliteNodes.size()-1) (:551)  -> +1 wrapper draw
//
// setEmeraldElite's guard is `Settings.isFinalActAvailable && !hasEmeraldKey`
// (:543). isFinalActAvailable = IRONCLAD_WIN && SILENT_WIN && DEFECT_WIN && ...
// (Settings.java:642), TRUE on the fully-unlocked profile B0.2 audited, and no
// emerald key exists at Act-1 start -> the guard PASSES and the draw fires.
// This is a LIVE-ORACLE finding that OVERTURNS the scoping report's H6/R3
// ("setEmeraldElite gated off in S1; do not consume its draw"). The +1 belongs
// to the post-assignment phase (B4.2 owns it), NOT to path generation. Proof:
// across all 20 seeds the oracle counter is EXACTLY path-gen-counter + 1,
// independent of the wildly varying re-roll counts (1..12) and walk-1 re-rolls.
inline constexpr int kSetEmeraldEliteDraws = 1;  // post-assignment, B4.2 models it

TEST(MapGen, PathGenCounterMatchesOracleMinusEmerald) {
    auto oracle = LoadOracle();
    ASSERT_EQ(oracle.size(), 20u);
    for (const auto& os : oracle) {
        GeneratedMap g = generate_map(os.seed, 1);
        EXPECT_EQ(g.rng.counter, os.counter - kSetEmeraldEliteDraws)
            << "path-gen mapRng.counter should be oracle(" << os.counter
            << ") minus the setEmeraldElite draw, seed " << os.seed;
    }
}

// The post-path raw state (s0/s1) is decoupled from the oracle's post-generateMap
// state: the oracle state has advanced further via the trap-12 raw shuffle AND
// the setEmeraldElite draw. This documents the boundary B4.2 closes (it must
// replay: path -> raw Fisher-Yates shuffle -> setEmeraldElite draw to land the
// full {counter, s0, s1} match). Here we assert the decoupling is real.
TEST(MapGen, PostPathRawStateIsBehindOracle) {
    auto oracle = LoadOracle();
    ASSERT_EQ(oracle.size(), 20u);
    for (const auto& os : oracle) {
        GeneratedMap g = generate_map(os.seed, 1);
        const bool raw_equal = (g.rng.s0 == os.s0 && g.rng.s1 == os.s1);
        EXPECT_FALSE(raw_equal)
            << "post-path raw state unexpectedly equals post-generateMap for seed "
            << os.seed << " (shuffle + emerald draw should have advanced s0/s1)";
    }
}

// Hand-derived PATH-GENERATION draw count for one seed (STS00001, run_seed
// 1790050543751). The path-gen count decomposes exactly as:
//   walk-seed draws (6 + walk-1 re-rolls) + primary draws (14/walk * 6 = 84)
//   + ancestor-gap re-roll draws.
// For STS00001: 6 + 84 + 3 = 93. The oracle's 94 = 93 + 1 (setEmeraldElite),
// see kSetEmeraldEliteDraws above.
TEST(MapGen, HandDerivedPathDrawCountSingleSeed) {
    using namespace sts::engine;
    const int64_t kSeed = 1790050543751LL;  // STS00001
    GenStats st;
    GeneratedMap g = generate_map(kSeed, 1, /*ancestor_bug=*/true, &st);

    // Decomposition, each independently derived from the algorithm structure.
    EXPECT_EQ(st.walk_seed_draws, 6);   // 6 walks, no walk-1 re-roll on this seed
    EXPECT_EQ(st.primary_draws, 84);    // 14 floor steps * 6 walks
    EXPECT_EQ(st.reroll_draws, 3);      // ancestor-gap re-rolls
    EXPECT_EQ(g.rng.counter, 93);       // 6 + 84 + 3
    EXPECT_EQ(g.rng.counter, st.walk_seed_draws + st.primary_draws + st.reroll_draws);

    // Cross-check: oracle counter is exactly path-gen + setEmeraldElite draw.
    auto oracle = LoadOracle();
    const OracleSeed* os = nullptr;
    for (const auto& o : oracle) {
        if (o.seed == kSeed) { os = &o; break; }
    }
    ASSERT_NE(os, nullptr);
    EXPECT_EQ(os->counter, g.rng.counter + kSetEmeraldEliteDraws);  // 94 == 93 + 1
}

// Named coverage for the getCommonAncestor :116 bug (H5). Two facts proven:
//   (1) the ancestor re-roll branch actually FIRES on the corpus (the machinery
//       is on the tested path, not dead code), and
//   (2) the bug is LOAD-BEARING: running with the bug "fixed" (node1.x < node2.x)
//       changes the generated map for at least one corpus seed, so replicating
//       the bug verbatim is required for the node-for-node oracle match.
TEST(MapGen, AncestorBugIsExercisedAndLoadBearing) {
    using namespace sts::engine;
    auto oracle = LoadOracle();
    ASSERT_EQ(oracle.size(), 20u);

    int seeds_with_reroll = 0;
    int seeds_where_bug_matters = 0;
    for (const auto& os : oracle) {
        // Production path (bug on) with instrumentation.
        GenStats stats;
        GeneratedMap g_bug = generate_map(os.seed, 1, /*ancestor_bug=*/true, &stats);
        if (stats.reroll_fires > 0) ++seeds_with_reroll;

        // Walk-seed accounting sanity: 6 seeds + walk-1 re-rolls; total counter
        // == walk_seed_draws + primary_draws + reroll_draws.
        EXPECT_GE(stats.walk_seed_draws, kMapPathDensity);
        EXPECT_EQ(g_bug.rng.counter,
                  stats.walk_seed_draws + stats.primary_draws + stats.reroll_draws)
            << "counter decomposition mismatch, seed " << os.seed;

        // Counterfactual: same seed with the bug "fixed".
        GeneratedMap g_fixed = generate_map(os.seed, 1, /*ancestor_bug=*/false);
        bool differs = (g_bug.rng.counter != g_fixed.rng.counter) ||
                       (g_bug.rng.s0 != g_fixed.rng.s0) ||
                       (g_bug.rng.s1 != g_fixed.rng.s1);
        if (!differs) {
            // Same RNG end-state -> compare topology directly.
            for (int y = 0; y < kGameMapFloors && !differs; ++y) {
                for (int x = 0; x < kGameMapCols && !differs; ++x) {
                    const GenNode& a = g_bug.at(x, y);
                    const GenNode& b = g_fixed.at(x, y);
                    if (a.edge_count != b.edge_count) { differs = true; break; }
                    for (int i = 0; i < a.edge_count; ++i) {
                        if (a.edges[i].dst_x != b.edges[i].dst_x ||
                            a.edges[i].dst_y != b.edges[i].dst_y) {
                            differs = true; break;
                        }
                    }
                }
            }
        }
        if (differs) ++seeds_where_bug_matters;
    }

    EXPECT_GT(seeds_with_reroll, 0)
        << "no corpus seed fired an ancestor re-roll -- machinery untested";
    EXPECT_GT(seeds_where_bug_matters, 0)
        << "'fixing' the :116 bug changed no seed -- bug not proven load-bearing";
}

// The RunState edge encoding round-trips the generated adjacency losslessly:
// every generated edge appears in the bitfield and no phantom edge is added.
TEST(MapGen, RunStateEncodingRoundTrips) {
    using namespace sts::engine;
    auto oracle = LoadOracle();
    ASSERT_EQ(oracle.size(), 20u);
    for (const auto& os : oracle) {
        RunState rs{};
        rs.run_seed = os.seed;
        rs.act = 1;
        GeneratedMap g = generate_map_paths(rs);

        // map_rng copied back.
        EXPECT_EQ(rs.map_rng.counter, g.rng.counter) << "seed " << os.seed;
        EXPECT_EQ(rs.map_rng.s0, g.rng.s0);
        EXPECT_EQ(rs.map_rng.s1, g.rng.s1);

        // Every generated edge is reflected in the bitfield, and vice versa.
        for (int y = 0; y < kGameMapFloors; ++y) {
            for (int x = 0; x < kGameMapCols; ++x) {
                const GenNode& n = g.at(x, y);
                const uint8_t bits = rs.map[run_state_map_index(x, y)].edges;
                uint8_t expect = 0;
                for (int i = 0; i < n.edge_count; ++i) {
                    const int dx = n.edges[i].dst_x, dy = n.edges[i].dst_y;
                    if (dy == kBossDstY) expect |= kEdgeBoss;
                    else if (dx == x - 1) expect |= kEdgeLeft;
                    else if (dx == x) expect |= kEdgeCenter;
                    else if (dx == x + 1) expect |= kEdgeRight;
                }
                EXPECT_EQ(bits, expect)
                    << "edge-bitfield mismatch at (" << x << "," << y
                    << ") seed " << os.seed;
            }
        }
    }
}

// Boss-edge edge case: every row-14 node that has edges must connect only to
// the boss (col 3, dstY 16), and the oracle dump must agree.
TEST(MapGen, BossEdgesFromRow14) {
    auto oracle = LoadOracle();
    ASSERT_EQ(oracle.size(), 20u);
    for (const auto& os : oracle) {
        GeneratedMap g = generate_map(os.seed, 1);
        for (int x = 0; x < sts::engine::kGameMapCols; ++x) {
            const sts::engine::GenNode& n = g.at(x, 14);
            if (!n.has_edges()) continue;
            ASSERT_EQ(n.edge_count, 1) << "row-14 node (" << x
                                       << ",14) should have exactly the boss edge";
            EXPECT_EQ(n.edges[0].dst_x, sts::engine::kBossCol);
            EXPECT_EQ(n.edges[0].dst_y, sts::engine::kBossDstY);
        }
        // Oracle cross-check: any dumped node at y==14 lists child (3,16).
        for (const auto& onode : os.nodes) {
            if (onode.y != 14) continue;
            ASSERT_EQ(onode.children.size(), 1u);
            EXPECT_EQ(onode.children[0].x, sts::engine::kBossCol);
            EXPECT_EQ(onode.children[0].y, sts::engine::kBossDstY);
        }
    }
}

}  // namespace
