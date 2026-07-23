#pragma once

// Act-map PATH generation (Stage B, task B4.1). Re-expresses the game's
// MapGenerator random walk, bit-for-bit on the `mapRng` stream, producing the
// node/edge graph of one act's map. Room-type ASSIGNMENT (the quotas + the
// trap-12 Collections.shuffle + the placement rules) is a SEPARATE task (B4.2)
// and is deliberately NOT done here; this file stops after path generation, at
// the exact `mapRng` state the game reaches before RoomTypeAssigner runs.
//
// Provenance (read in full from D:\STS_BG_Mod\SlayTheSpireDecompiled before
// coding; every citation resolves there):
//   - MapGenerator.generateDungeon        (MapGenerator.java:23-28)
//   - MapGenerator.createNodes            (MapGenerator.java:50-60)   [no RNG]
//   - MapGenerator.createPaths            (MapGenerator.java:62-77)   [6 walks]
//   - MapGenerator._createPaths           (MapGenerator.java:133-211) [the walk]
//   - MapGenerator.getCommonAncestor      (MapGenerator.java:111-131) [BUG @116]
//   - MapGenerator.getNodeWithMaxX/MinX   (MapGenerator.java:91-109)
//   - MapGenerator.getMaxEdge/getMinEdge  (MapGenerator.java:79-89)
//   - MapGenerator.filterRedundantEdges.. (MapGenerator.java:30-48)   [row 0]
//   - MapGenerator.randRange              (MapGenerator.java:270-276)
//   - MapRoomNode.addEdge/addParent       (MapRoomNode.java:75-84,149-151)
//   - MapEdge.compareTo (EdgeComparator)  (MapEdge.java:71-90)
//   - AbstractDungeon.generateMap         (AbstractDungeon.java:510-540)
//   - mapRng seeding = Random(seed+actNum) (Exordium.java:56 -> act1 = seed+1)
//
// ORIENTATION (see docs scoping report §0). This file works entirely in the
// GAME's map semantics: 15 floors (rows, y = 0..14) x 7 columns (x = 0..6),
// `MAP_HEIGHT=15 / MAP_WIDTH=7` (AbstractDungeon.java:210-211). The current
// RunState storage names its dims transposed (kMapRows=7 / kMapCols=15); the
// schema-v2 rename/reorientation is B4.3's job. To avoid perpetuating the
// confusion, path data is written into RunState via one documented index
// convention (`run_state_map_index` below): after B4.3 flips the kMapRows/
// kMapCols names to 15x7 that convention becomes the natural row-major index
// and the call sites need no logic change.
//
// HAZARDS honoured (scoping report §6):
//   H1  MapRoomNode/MapEdge ctors call libgdx MathUtils (global static RNG),
//       NOT mapRng -- offsets/angles/dot-spacing are cosmetic and are NOT
//       modelled here (they would phantom-consume draws).
//   H4  edge lists are kept sorted by MapEdge.compareTo (dstX then dstY),
//       STABLE, so ties keep insertion order.
//   H5  getCommonAncestor compares `node1.x < node2.y` (MapGenerator.java:116);
//       this is a real upstream off-by-one (should be node2.x). It is
//       REPLICATED verbatim -- it changes which re-rolls fire, hence the draw
//       count. See k_ancestor_bug_line below.
//   Parents are appended WITHOUT dedup (MapRoomNode.addParent) and addParent is
//   called even when addEdge de-duped the edge (MapGenerator.java:207-209), so
//   a node's parent list may contain duplicates -- this is preserved because
//   the ancestor re-roll loop iterates that list and its length affects draws.

#include <cstdint>
#include <type_traits>

#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_state.hpp"

namespace sts::engine {

// --- Game map constants (AbstractDungeon.java:210-213, 512-514) --------------

inline constexpr int kGameMapFloors = 15;   // MAP_HEIGHT: rows y = 0..14
inline constexpr int kGameMapCols = 7;      // MAP_WIDTH:  cols x = 0..6
inline constexpr int kMapPathDensity = 6;   // MAP_PATH_DENSITY (6 walks)
inline constexpr int kBossCol = 3;          // hardcoded boss column (:140)
inline constexpr int kBossDstY = 16;        // synthetic boss edge dstY (dstY+2)
inline constexpr int kMinAncestorGap = 3;   // _createPaths :160
inline constexpr int kMaxAncestorGap = 5;   // _createPaths :161

// The replicated upstream bug lives at MapGenerator.java:116 (`node1.x <
// node2.y`). Named here so the intent is unmistakable at the use site.
inline constexpr int k_ancestor_bug_line = 116;

// Edge-bitfield encoding written into RunState.MapNode.edges (a u8). A grid
// node at (x, y<14) only ever connects to the next row at column x-1/x/x+1
// (proven: the primary/re-roll draws move +/-1 and the sibling clamp only pulls
// toward an existing neighbour edge, staying within [x-1, x+1]); row-14 nodes
// connect only to the synthetic boss. 4 bits suffice:
inline constexpr uint8_t kEdgeLeft = 0x1;    // -> (x-1, y+1)
inline constexpr uint8_t kEdgeCenter = 0x2;  // -> (x,   y+1)
inline constexpr uint8_t kEdgeRight = 0x4;   // -> (x+1, y+1)
inline constexpr uint8_t kEdgeBoss = 0x8;    // -> boss (row 14 only)

// RunState.map[] index for GAME node (x = col 0..6, y = floor 0..14). The
// backing array is kMapRows*kMapCols = 105 entries either way; this convention
// is game-oriented (floor-major). B4.3's schema-v2 rename (kMapRows->15,
// kMapCols->7) turns this into the plain row-major `y*width + x`.
[[nodiscard]] constexpr int run_state_map_index(int x, int y) noexcept {
    return y * kGameMapCols + x;
}

// --- Working generation structures (game semantics) --------------------------
// Not part of RunState; a scratch graph the generator builds, then encodes into
// RunState. Fixed-capacity, pointer-free (same discipline as the engine PODs).

struct GenEdge {
    int8_t dst_x;
    int8_t dst_y;
};

struct GenNode {
    // A node has at most 3 next-row edges; row-14 nodes have exactly 1 boss
    // edge. 4 gives margin. Edges kept sorted by (dst_x, dst_y) (H4).
    static constexpr int kMaxEdges = 4;
    // A target gains at most one parent per walk (a walk visits each floor once
    // as a source), so <= kMapPathDensity distinct additions; kept WITH
    // duplicates. 8 gives margin.
    static constexpr int kMaxParents = 8;

    GenEdge edges[kMaxEdges];
    int edge_count;
    int8_t parent_x[kMaxParents];
    int8_t parent_y[kMaxParents];
    int parent_count;

    [[nodiscard]] constexpr bool has_edges() const noexcept { return edge_count > 0; }
};

// Optional instrumentation, filled when a non-null pointer is threaded through
// generation. Lets tests prove the ancestor re-roll path (and the :116 bug) is
// load-bearing without perturbing the production path.
struct GenStats {
    int walk_seed_draws = 0;   // createPaths seed draws (6 + walk-1 re-rolls)
    int primary_draws = 0;     // per-floor-step primary randRange draws
    int reroll_draws = 0;      // conditional ancestor-gap re-roll draws
    int reroll_fires = 0;      // # times an ancestor-gap re-roll branch fired
};

struct GeneratedMap {
    GenNode nodes[kGameMapFloors][kGameMapCols];
    RngStream rng;  // mapRng, at the state reached AFTER path generation.

    [[nodiscard]] constexpr GenNode& at(int x, int y) noexcept { return nodes[y][x]; }
    [[nodiscard]] constexpr const GenNode& at(int x, int y) const noexcept {
        return nodes[y][x];
    }

    // True iff node (x,y) has an outgoing edge to (dst_x, dst_y). For B4.1
    // verification against the oracle's dumped "children" list.
    [[nodiscard]] constexpr bool has_child(int x, int y, int dst_x, int dst_y) const noexcept {
        const GenNode& n = at(x, y);
        for (int i = 0; i < n.edge_count; ++i) {
            if (n.edges[i].dst_x == dst_x && n.edges[i].dst_y == dst_y) return true;
        }
        return false;
    }
};

namespace detail {

// randRange(rng, min, max) == rng.random(max-min) + min  (MapGenerator.java:
// 270-276). One mapRng draw. random(int) is TRAP-3 inclusive, so this yields
// min..max inclusive.
[[nodiscard]] constexpr int map_rand_range(RngStream& rng, int min, int max) noexcept {
    return random(rng, max - min) + min;
}

// MapRoomNode.addEdge (MapRoomNode.java:75-84): add only if no existing edge is
// EQUAL under MapEdge.compareTo (same dst_x AND dst_y); then keep the list
// sorted by (dst_x, dst_y) (MapGenerator.java:208 Collections.sort). Stable
// insertion keeps ties in insertion order (H4).
constexpr void node_add_edge_sorted(GenNode& n, int8_t dst_x, int8_t dst_y) noexcept {
    for (int i = 0; i < n.edge_count; ++i) {
        if (n.edges[i].dst_x == dst_x && n.edges[i].dst_y == dst_y) return;  // dup
    }
    // stable insert at first position whose (dst_x,dst_y) is strictly greater.
    int pos = n.edge_count;
    for (int i = 0; i < n.edge_count; ++i) {
        const bool greater = (n.edges[i].dst_x > dst_x) ||
                             (n.edges[i].dst_x == dst_x && n.edges[i].dst_y > dst_y);
        if (greater) { pos = i; break; }
    }
    for (int i = n.edge_count; i > pos; --i) n.edges[i] = n.edges[i - 1];
    n.edges[pos] = GenEdge{dst_x, dst_y};
    ++n.edge_count;
}

// MapRoomNode.addParent (MapRoomNode.java:149-151): append, NO dedup. Called
// unconditionally at commit (MapGenerator.java:209), even when addEdge de-duped.
constexpr void node_add_parent(GenNode& n, int8_t px, int8_t py) noexcept {
    n.parent_x[n.parent_count] = px;
    n.parent_y[n.parent_count] = py;
    ++n.parent_count;
}

// getNodeWithMaxX (MapGenerator.java:91-99): the parent with strictly-greatest
// x; on ties keeps the FIRST occurrence (`if (node.x <= max.x) continue`). All
// parents share a row, so only x is compared. Writes the chosen (x,y) out.
constexpr void node_with_max_x(const GenNode& n, int8_t& ox, int8_t& oy) noexcept {
    ox = n.parent_x[0];
    oy = n.parent_y[0];
    for (int i = 1; i < n.parent_count; ++i) {
        if (n.parent_x[i] <= ox) continue;
        ox = n.parent_x[i];
        oy = n.parent_y[i];
    }
}

// getNodeWithMinX (MapGenerator.java:101-109): strictly-least x, first tie kept.
constexpr void node_with_min_x(const GenNode& n, int8_t& ox, int8_t& oy) noexcept {
    ox = n.parent_x[0];
    oy = n.parent_y[0];
    for (int i = 1; i < n.parent_count; ++i) {
        if (n.parent_x[i] >= ox) continue;
        ox = n.parent_x[i];
        oy = n.parent_y[i];
    }
}

// getCommonAncestor (MapGenerator.java:111-131). node1=(n1x,n1y),
// node2=(n2x,n2y) with n1y==n2y. Returns whether a common ancestor within
// max_depth exists and, if so, its y (the only field the caller reads:
// ancestor_gap = newEdgeY - ancestor.y). REPLICATES THE :116 BUG verbatim:
// `node1.x < node2.y`.
[[nodiscard]] constexpr bool common_ancestor_y(const GeneratedMap& g,
                                               int n1x, int n1y, int n2x, int n2y,
                                               int max_depth, int& ancestor_y,
                                               bool ancestor_bug = true) noexcept {
    int lx, ly, rx, ry;
    // BUG (H5): the game compares node1.x against node2.y (should be node2.x).
    // `ancestor_bug` is true on the production path; the false branch exists
    // ONLY so a test can prove the bug is load-bearing (map_gen_test).
    const bool left_is_node1 = ancestor_bug ? (n1x < n2y) : (n1x < n2x);
    if (left_is_node1) {
        lx = n1x; ly = n1y; rx = n2x; ry = n2y;
    } else {
        lx = n2x; ly = n2y; rx = n1x; ry = n1y;
    }
    for (int cy = n1y; cy >= 0 && cy >= n1y - max_depth; --cy) {
        const GenNode& ln = g.at(lx, ly);
        const GenNode& rn = g.at(rx, ry);
        if (ln.parent_count == 0 || rn.parent_count == 0) return false;
        int8_t nlx, nly, nrx, nry;
        node_with_max_x(ln, nlx, nly);
        node_with_min_x(rn, nrx, nry);
        lx = nlx; ly = nly; rx = nrx; ry = nry;
        if (lx != rx || ly != ry) continue;
        ancestor_y = ly;
        return true;
    }
    return false;
}

// _createPaths (MapGenerator.java:133-211), expressed as a loop over the tail
// recursion. `start_x` is the walk's starting column; the initial edge is
// MapEdge(start_x, -1, start_x, 0) so the first current node is (start_x, 0).
constexpr void create_one_path(GeneratedMap& g, RngStream& rng, int start_x,
                               bool ancestor_bug, GenStats* stats) noexcept {
    int edge_dst_x = start_x;
    int edge_dst_y = 0;
    const int row_end_node = kGameMapCols - 1;  // 6

    for (;;) {
        const int cur_x = edge_dst_x;
        const int cur_y = edge_dst_y;
        GenNode& current = g.at(cur_x, cur_y);

        // Top-of-map / boss terminator (:139-144). No mapRng draw.
        if (cur_y + 1 >= kGameMapFloors) {  // cur_y == 14
            node_add_edge_sorted(current, static_cast<int8_t>(kBossCol),
                                 static_cast<int8_t>(kBossDstY));
            return;
        }

        // Column clamp bounds (:145-156).
        int min, max;
        if (edge_dst_x == 0) {
            min = 0; max = 1;
        } else if (edge_dst_x == row_end_node) {
            min = -1; max = 0;
        } else {
            min = -1; max = 1;
        }

        // Primary step draw (:157).
        int new_edge_x = edge_dst_x + map_rand_range(rng, min, max);
        if (stats) ++stats->primary_draws;
        const int new_edge_y = cur_y + 1;

        // Ancestor-gap re-roll loop (:160-192). `parents` is bound ONCE to the
        // ORIGINAL candidate's parent list (Java captures the reference; the
        // list is not mutated inside the loop). targetNodeCandidate.x is
        // tracked separately in `cand_x` because it is reassigned on re-roll.
        const GenNode& orig_cand = g.at(new_edge_x, new_edge_y);
        int cand_x = new_edge_x;  // targetNodeCandidate.x
        const int orig_parent_count = orig_cand.parent_count;
        for (int pi = 0; pi < orig_parent_count; ++pi) {
            const int px = orig_cand.parent_x[pi];
            const int py = orig_cand.parent_y[pi];
            // `parent == currentNode` guard (:166).
            if (px == cur_x && py == cur_y) continue;
            int anc_y = 0;
            if (!common_ancestor_y(g, px, py, cur_x, cur_y, kMaxAncestorGap, anc_y,
                                   ancestor_bug)) {
                continue;  // ancestor == null (:166)
            }
            const int ancestor_gap = new_edge_y - anc_y;
            if (ancestor_gap < kMinAncestorGap) {
                if (cand_x > cur_x) {  // target right of current (:169-173)
                    new_edge_x = edge_dst_x + map_rand_range(rng, -1, 0);
                    if (new_edge_x < 0) new_edge_x = edge_dst_x;
                } else if (cand_x == cur_x) {  // target == current (:174-180)
                    new_edge_x = edge_dst_x + map_rand_range(rng, -1, 1);
                    if (new_edge_x > row_end_node) {
                        new_edge_x = edge_dst_x - 1;
                    } else if (new_edge_x < 0) {
                        new_edge_x = edge_dst_x + 1;
                    }
                } else {  // target left of current (:181-186)
                    new_edge_x = edge_dst_x + map_rand_range(rng, 0, 1);
                    if (new_edge_x > row_end_node) new_edge_x = edge_dst_x;
                }
                if (stats) { ++stats->reroll_draws; ++stats->reroll_fires; }
                cand_x = new_edge_x;  // targetNodeCandidate = getNode(newEdgeX,..)
                continue;
            }
            // if (ancestor_gap < max_ancestor_gap) continue;  -- else no-op (:190)
        }

        // Sibling clamp (:193-204). No RNG. getMaxEdge/getMinEdge just read the
        // sorted neighbour edge list (first = min dst_x, last = max dst_x).
        if (edge_dst_x != 0) {
            const GenNode& left_node = g.at(edge_dst_x - 1, cur_y);
            if (left_node.has_edges()) {
                const int right_edge_of_left = left_node.edges[left_node.edge_count - 1].dst_x;
                if (right_edge_of_left > new_edge_x) new_edge_x = right_edge_of_left;
            }
        }
        if (edge_dst_x < row_end_node) {
            const GenNode& right_node = g.at(edge_dst_x + 1, cur_y);
            if (right_node.has_edges()) {
                const int left_edge_of_right = right_node.edges[0].dst_x;
                if (left_edge_of_right < new_edge_x) new_edge_x = left_edge_of_right;
            }
        }

        // Commit (:205-210): addEdge (deduped, sorted), addParent (no dedup,
        // unconditional), recurse on the new edge.
        node_add_edge_sorted(current, static_cast<int8_t>(new_edge_x),
                             static_cast<int8_t>(new_edge_y));
        node_add_parent(g.at(new_edge_x, new_edge_y), static_cast<int8_t>(cur_x),
                        static_cast<int8_t>(cur_y));
        edge_dst_x = new_edge_x;
        edge_dst_y = new_edge_y;
    }
}

// filterRedundantEdgesFromRow (MapGenerator.java:30-48): across ROW-0 nodes in
// column order, an edge to a (dst_x,dst_y) already emitted by an earlier row-0
// node is deleted from the later node. Only row 0 is touched.
constexpr void filter_redundant_row0_edges(GeneratedMap& g) noexcept {
    // existing (dst_x,dst_y) pairs seen so far.
    int8_t seen_x[kGameMapCols * GenNode::kMaxEdges];
    int8_t seen_y[kGameMapCols * GenNode::kMaxEdges];
    int seen_count = 0;
    for (int x = 0; x < kGameMapCols; ++x) {
        GenNode& n = g.at(x, 0);
        if (!n.has_edges()) continue;
        // Determine which of this node's edges are redundant, then delete them.
        bool del[GenNode::kMaxEdges] = {false, false, false, false};
        for (int i = 0; i < n.edge_count; ++i) {
            const int8_t dx = n.edges[i].dst_x;
            const int8_t dy = n.edges[i].dst_y;
            for (int s = 0; s < seen_count; ++s) {
                if (seen_x[s] == dx && seen_y[s] == dy) { del[i] = true; break; }
            }
            seen_x[seen_count] = dx;
            seen_y[seen_count] = dy;
            ++seen_count;
        }
        int w = 0;
        for (int i = 0; i < n.edge_count; ++i) {
            if (del[i]) continue;
            n.edges[w++] = n.edges[i];
        }
        n.edge_count = w;
    }
}

}  // namespace detail

// generateDungeon -> createPaths (the 6 walks) -> filterRedundantEdgesFromRow
// (MapGenerator.java:23-28, 62-77). `g.rng` must already be seeded to the
// act-scoped mapRng (see generate_map_paths). pathDensity is fixed at 6 (the
// "Uncertain Future" density-1 mod is not S1). On return `g.rng` holds the
// mapRng state the game reaches immediately BEFORE the room-type shuffle.
constexpr void generate_map_graph(GeneratedMap& g, bool ancestor_bug = true,
                                  GenStats* stats = nullptr) noexcept {
    // createNodes: no RNG (createPaths only; the nodes come value-initialized).
    const int row_size = kGameMapCols - 1;  // nodes.get(0).size()-1 == 6 (:64)
    int first_starting_node = -1;
    for (int i = 0; i < kMapPathDensity; ++i) {
        int starting_node = detail::map_rand_range(g.rng, 0, row_size);  // :67
        if (stats) ++stats->walk_seed_draws;
        if (i == 0) first_starting_node = starting_node;                 // :68-70
        while (starting_node == first_starting_node && i == 1) {         // :71-73
            starting_node = detail::map_rand_range(g.rng, 0, row_size);
            if (stats) ++stats->walk_seed_draws;
        }
        detail::create_one_path(g, g.rng, starting_node, ancestor_bug, stats);  // :74
    }
    detail::filter_redundant_row0_edges(g);
}

// Full B4.1 entry point over game semantics: seed mapRng for the act and run
// path generation. act_num is 1..4 (S1 = act 1). Returns the working graph;
// callers that only need RunState use generate_map_paths below.
[[nodiscard]] constexpr GeneratedMap generate_map(int64_t run_seed, int act_num,
                                                 bool ancestor_bug = true,
                                                 GenStats* stats = nullptr) noexcept {
    GeneratedMap g{};
    g.rng = map_stream(run_seed, act_num);  // Exordium.java:56 (act1 = seed+1)
    generate_map_graph(g, ancestor_bug, stats);
    return g;
}

// Encode a generated graph's edges into RunState.map[] and store the post-path
// mapRng. `room_type` is left as-is (B4.2 assigns it). Uses the documented
// game-oriented index convention (run_state_map_index) so B4.3's rename is
// mechanical.
constexpr void encode_paths_into_run_state(const GeneratedMap& g, RunState& rs) noexcept {
    for (int y = 0; y < kGameMapFloors; ++y) {
        for (int x = 0; x < kGameMapCols; ++x) {
            const GenNode& n = g.at(x, y);
            uint8_t bits = 0;
            for (int i = 0; i < n.edge_count; ++i) {
                const int dx = n.edges[i].dst_x;
                const int dy = n.edges[i].dst_y;
                if (dy == kBossDstY) {
                    bits |= kEdgeBoss;
                } else if (dx == x - 1) {
                    bits |= kEdgeLeft;
                } else if (dx == x) {
                    bits |= kEdgeCenter;
                } else if (dx == x + 1) {
                    bits |= kEdgeRight;
                }
            }
            rs.map[run_state_map_index(x, y)].edges = bits;
        }
    }
    rs.map_rng = g.rng;
}

// B4.1 deliverable: generate the act map's paths into RunState. Reads
// rs.run_seed / rs.act, seeds mapRng, runs path generation, writes the edge
// adjacency + the post-path mapRng back into rs. Room types remain untouched
// (B4.2). Returns the working graph for callers/tests that want the rich form.
constexpr GeneratedMap generate_map_paths(RunState& rs) noexcept {
    GeneratedMap g = generate_map(rs.run_seed, static_cast<int>(rs.act));
    encode_paths_into_run_state(g, rs);
    return g;
}

static_assert(std::is_trivially_copyable_v<GenNode>);
static_assert(std::is_trivially_copyable_v<GeneratedMap>);

}  // namespace sts::engine
