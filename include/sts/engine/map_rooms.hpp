#pragma once

// Act-map ROOM-TYPE assignment. Re-expresses the tail of
// AbstractDungeon.generateMap that runs AFTER path generation
// (map_gen.hpp): the room-type quotas, the trap-12 direct-XS128
// Collections.shuffle of the room list, the placement rules (row gates,
// parent/sibling exclusions, monster fill), the three fixed rows (0/8/14), and
// the post-assignment setEmeraldElite draw. It consumes a fully path-generated
// GeneratedMap (whose g.rng is at the exact mapRng state the game reaches just
// before RoomTypeAssigner runs) and produces the per-node room grid plus the
// mapRng state the game reaches at the END of generateMap.
//
// Provenance (read in full from D:\STS_BG_Mod\SlayTheSpireDecompiled before
// coding; every citation resolves there):
//   - AbstractDungeon.generateMap          (AbstractDungeon.java:510-540)
//   - AbstractDungeon.generateRoomTypes     (AbstractDungeon.java:558-594)
//       elite x1.6 at ascension>=1          (AbstractDungeon.java:571-573)
//   - AbstractDungeon.setEmeraldElite       (AbstractDungeon.java:542-556)
//   - room chances (0.05/0.12/0.0/0.22/0.08)(Exordium.java:95-99)
//   - RoomTypeAssigner.assignRowAsRoomType  (RoomTypeAssigner.java:30-40)
//   - RoomTypeAssigner.getConnectedNonAssignedNodeCount (:42-51)
//   - RoomTypeAssigner.getSiblings          (:53-63)
//   - RoomTypeAssigner.ruleSiblingMatches   (:65-72)
//   - RoomTypeAssigner.ruleParentMatches    (:74-82)
//   - RoomTypeAssigner.ruleAssignableToRow  (:84-91)
//   - RoomTypeAssigner.getNextRoomTypeAccordingToRules (:93-105)
//   - RoomTypeAssigner.lastMinuteNodeChecker(:107-115)
//   - RoomTypeAssigner.assignRoomsToNodes   (:117-125)
//   - RoomTypeAssigner.distributeRoomsAcrossMap (:127-143)  [TRAP 12 @:135]
//   - Collections.shuffle (JDK, RandomAccess path) -> Fisher-Yates
//   - room mapSymbols M/?/E/R/$/T/B (MonsterRoom.java:28, EventRoom.java:20,
//       MonsterRoomElite.java:30, RestRoom.java:27, ShopRoom.java:32,
//       TreasureRoom.java:31, MonsterRoomBoss.java:23)
//
// ORIENTATION matches map_gen.hpp: GAME semantics, 15 floors (rows y=0..14) x 7
// columns (x=0..6), iterated row-major (y outer, x inner) exactly as
// AbstractDungeon's map ArrayList<ArrayList<>> is iterated.
//
// KEY HAZARDS honoured:
//   TRAP 12 (design doc 10.12): RoomTypeAssigner.java:135 passes the RAW
//     RandomXS128 (Random.random, a field that EXTENDS java.util.Random) straight
//     to Collections.shuffle, so the Fisher-Yates uses RandomXS128's overridden
//     nextInt(bound) -> the raw (s0,s1) advance by exactly (size-1) next_long
//     draws but the wrapper `counter` does NOT advance. This is the OPPOSITE of
//     deck/pool shuffles (JDK-LCG, rng_jdk.hpp). See xs128_room_shuffle below.
//   setEmeraldElite (a live-oracle finding, not read off the decompiled source):
//     on the fully-unlocked A20 profile the guard
//     `Settings.isFinalActAvailable && !hasEmeraldKey` (AbstractDungeon.java:543)
//     PASSES **while the run holds no emerald key**, so mapRng.random(0,
//     eliteNodes.size()-1) (:551) FIRES exactly once after room assignment ->
//     +1 wrapper draw. This overturns the scoping report's H6/R3 ("gated off in
//     S1"); the live oracle settled it. S3.11 made the SECOND conjunct live:
//     the draw is skipped, and no node marked, on every act generated after the
//     key is taken (the `has_emerald_key` parameter of assign_room_types).
//   H2 float Math.round: quotas are Math.round((float)count * chance), the
//     multiply in float precision. Java 8 Math.round(float) is replicated
//     bit-for-bit in java_round_f below (not a naive double round -- half-integer
//     products would diverge).
//   H7 row-13 quota exclusion: the quota base `count` excludes row 13
//     (map.size()-2), AbstractDungeon.java:520; but nodeCount (padding target)
//     does NOT -- row 13 fills from padding (monster-heavy).

#include <bit>
#include <cstdint>
#include <type_traits>

#include "sts/engine/map_gen.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/rng_xs128.hpp"
#include "sts/engine/run_state.hpp"

namespace sts::engine {

// Room-kind id written into RunState.MapNode.room_type (a u8). None(0) is the
// value-initialized "unassigned / no room" state (matches Java room==null).
// FINALIZED (schema v3): these are THE RunState.MapNode.room_type
// encoding, append-only (never renumber; new room kinds get the next value).
enum class RoomType : uint8_t {
    None = 0,
    Monster = 1,   // "M"  MonsterRoom
    Event = 2,     // "?"  EventRoom
    Elite = 3,     // "E"  MonsterRoomElite
    Rest = 4,      // "R"  RestRoom
    Shop = 5,      // "$"  ShopRoom
    Treasure = 6,  // "T"  TreasureRoom
    // "B"  MonsterRoomBoss. In Acts 1-3 it is NEVER placed on a grid node --
    // DungeonMap.update builds a synthetic MapRoomNode(-1, 15) for it
    // (DungeonMap.java:77-79) and no RoomTypeAssigner path emits the value.
    // ACT 4 IS THE EXCEPTION, AND ONLY ON PAPER (S3.32): TheEnding
    // .generateSpecialMap really does put a MonsterRoomBoss in the map array
    // at (3,3) (TheEnding.java:82-83), so the engine mirrors it there -- but
    // the node is still never ENTERED through the map, because the boss
    // button at the Act-4 elite runs that same synthetic-node path
    // (DungeonMap.java:68 extends the gate to `TheEnding && y == 2`).
    Boss = 7,
    // TreasureRoomBoss -- the post-boss chest (TreasureRoomBoss.java:22-36).
    // Like Boss it is NEVER written into a grid node: ProceedButton
    // .goToTreasureRoom (ProceedButton.java:179-187) builds a synthetic
    // MapRoomNode(-1, 15) for it, so no RoomTypeAssigner path can produce this
    // value in RunState.map. It is the RESOLVED room kind the controller
    // carries (RunController.room_type) while RunPhase::BOSS_TREASURE is up.
    // Value 8 is the S2 Wave-2 allocation (docs/stage-b-tasks.md "S2 Wave-2
    // allocations"); values are append-only and never renumbered.
    TreasureBoss = 8,
    // VictoryRoom(EventType.HEART) -- the room the last Act-3 boss's proceed
    // builds (ProceedButton.goToVictoryRoomOrTheDoor, ProceedButton.java:
    // 199-208) as another synthetic off-grid MapRoomNode(-1, 15), so like Boss
    // and TreasureBoss it can never be written into a grid node. It is a REAL
    // FLOOR (a full nextRoomTransition: ++floorNum, the five-stream reseed, the
    // relic onEnterRoom fan-outs) whose phase is EVENT (VictoryRoom.java:21-33)
    // and whose event is the `Spire Heart` dialog; it grants nothing.
    // Value 9 is the S3 Wave-1 allocation (docs/s3-tasks.md "Registry id blocks
    // granted to Wave 1"). Values are append-only and never renumbered.
    Victory = 9,
    // TrueVictoryRoom -- the run's REAL terminal, and the one off-map-looking
    // room that is actually ON the grid. ProceedButton.goToTrueVictoryRoom
    // (ProceedButton.java:189-197) builds `new MapRoomNode(3, 4)` -- NOT the
    // `MapRoomNode(-1, 15)` its three siblings (goToTreasureRoom :181,
    // goToVictoryRoomOrTheDoor :202, goToDoubleBoss :214) build -- and the
    // Act-4 map really does hold a TrueVictoryRoom at (3,4)
    // (TheEnding.java:84-85, :124), so unlike Boss/TreasureBoss/Victory this
    // value IS written into a grid node. The coordinates are not a coincidence:
    // the Act-4 boss sits at floor act4_floor_base + 4 (row 3) and the
    // transition's ++floorNum lands the player on row 4, which is (3,4).
    //
    // It is still never ENTERED through the map -- the victory node has no
    // inbound edge (:86-88 connect rest->shop->elite->boss and stop), so no
    // mask can offer it; the room arrives only through the Act-4 boss's proceed.
    //
    // The room is NO_INTERACT (TrueVictoryRoom.java:26-32: isScreenUp, the
    // proceed button hidden, `screen = NO_INTERACT`) and the cutscene behind it
    // is presentation, so the engine's terminal IS this room's entry --
    // RunPhase::RUN_OVER with RunVictoryKind::HEART. Value 10 is the S3 Wave-1
    // allocation, spent by S3.33.
    TrueVictory = 10,
};

// One past the last RoomType enumerator, for consumers that index an array by
// room kind (tools/fuzz/include/sts/fuzz/coverage.hpp's kRoomTypeCount). It is
// declared HERE, beside the enum, and static_asserted against it, so a new room
// kind cannot silently leave a downstream array one short -- which is exactly
// what the fuzz coverage table had no guard against before TreasureBoss landed.
inline constexpr int kRoomTypeCount = 11;
static_assert(static_cast<int>(RoomType::TrueVictory) + 1 == kRoomTypeCount,
              "kRoomTypeCount must be one past the last RoomType enumerator");

// The game's mapSymbol for a room (RoomTypeAssigner dump / oracle golden).
[[nodiscard]] constexpr char room_symbol(RoomType r) noexcept {
    switch (r) {
        case RoomType::Monster:  return 'M';
        case RoomType::Event:    return '?';
        case RoomType::Elite:    return 'E';
        case RoomType::Rest:     return 'R';
        case RoomType::Shop:     return '$';
        case RoomType::Treasure: return 'T';
        case RoomType::Boss:     return 'B';
        // TreasureRoomBoss's constructor (TreasureRoomBoss.java:31-36) never
        // assigns `mapSymbol`, so the room carries AbstractRoom's default and
        // no map dump can contain it -- the room is off-grid. 'T' is returned
        // rather than '.' so a dump that somehow held one is readable as a
        // chest; a map-generation test can never produce this case (the
        // RoomTypeAssigner writes only values 1..6).
        case RoomType::TreasureBoss: return 'T';
        // VictoryRoom's constructor (VictoryRoom.java:21-24) assigns no
        // `mapSymbol` either, and the room is off-grid for the same reason, so
        // no map dump can contain this value. 'V' is returned rather than '.'
        // on the same terms as TreasureBoss's 'T': a dump that somehow held one
        // should be readable, and the RoomTypeAssigner writes only 1..6.
        case RoomType::Victory:  return 'V';
        // TrueVictoryRoom's constructor (TrueVictoryRoom.java:20-24) assigns no
        // `mapSymbol` either, so `getMapSymbol()` returns null and the game's
        // own dump of the Act-4 map prints "null" at (3,4) rather than a
        // character. 'V' is returned on the same terms as Victory's and
        // TreasureBoss's: readable, and unreachable from any RoomTypeAssigner
        // (which writes only 1..6). NOTE that this is the one of the three
        // that a map dump CAN contain, because unlike them the room is in the
        // grid -- the replay differ neutralizes `map[]` on both sides
        // (replay/src/main.cpp, neutralize_incomparable), so the symbol has no
        // comparison consumer; S3.62 owns the Act-4 map capture.
        case RoomType::TrueVictory: return 'V';
        case RoomType::None:     return '.';
    }
    return '.';
}

// --- Room chances (Exordium.java:95-99; identical in every act) --------------
// Declared as float literals in the game; kept as float so the quota multiply
// happens in float precision exactly like Java's `(float)count * chance`.
inline constexpr float kShopRoomChance = 0.05f;
inline constexpr float kRestRoomChance = 0.12f;
inline constexpr float kTreasureRoomChance = 0.0f;
inline constexpr float kEventRoomChance = 0.22f;
inline constexpr float kEliteRoomChance = 0.08f;
inline constexpr float kEliteAscensionMult = 1.6f;  // AbstractDungeon.java:572 (A1+)

// Fixed rows (AbstractDungeon.java:525-531).
inline constexpr int kRestRow = 14;      // map.size()-1
inline constexpr int kMonsterRow = 0;
inline constexpr int kTreasureRow = 8;
inline constexpr int kQuotaExcludedRow = 13;  // map.size()-2 (H7)

// --- Java 8 Math.round(float) -----------------------------------------------
// Bit-for-bit replica of java.lang.Math.round(float) as shipped in JDK 8 (the
// game's runtime): the significand-shift algorithm, NOT the pre-Java-7
// (int)floor(a+0.5f) form. For the positive, small products used by the quotas
// both agree, but this is exact for every finite input (H2).
[[nodiscard]] constexpr int32_t java_round_f(float a) noexcept {
    constexpr int32_t kSignificandWidth = 24;
    constexpr int32_t kExpBias = 127;
    constexpr int32_t kExpBitMask = 0x7F800000;
    constexpr int32_t kSignifBitMask = 0x007FFFFF;
    const int32_t int_bits = std::bit_cast<int32_t>(a);
    const int32_t biased_exp =
        (int_bits & kExpBitMask) >> (kSignificandWidth - 1);
    const int32_t shift =
        (kSignificandWidth - 2 + kExpBias) - biased_exp;  // 149 - biased_exp
    if ((shift & -32) == 0) {  // pow(2,-32) <= ulp(a) < 1
        int32_t r = (int_bits & kSignifBitMask) | (kSignifBitMask + 1);
        if (int_bits < 0) r = -r;
        return ((r >> shift) + 1) >> 1;
    }
    return static_cast<int32_t>(static_cast<double>(a) + 0.5);
}

// --- Room quotas (generateRoomTypes, AbstractDungeon.java:558-594) -----------

struct RoomQuota {
    int shop = 0;
    int rest = 0;
    int treasure = 0;  // always 0 (treasureRoomChance == 0)
    int elite = 0;
    int event = 0;
    int monster = 0;   // remainder (LOG-ONLY in the game; never added to roomList)
};

// generateRoomTypes counts (AbstractDungeon.java:562-580). `ascension` gates the
// A1+ elite x1.6 branch (:571-573). Only shop/rest/elite/event are pushed to the
// room list (:582-593); treasure/monster here are logging-only.
[[nodiscard]] constexpr RoomQuota compute_room_quota(int count, int ascension) noexcept {
    RoomQuota q;
    q.shop = java_round_f(static_cast<float>(count) * kShopRoomChance);
    q.rest = java_round_f(static_cast<float>(count) * kRestRoomChance);
    q.treasure = java_round_f(static_cast<float>(count) * kTreasureRoomChance);
    if (ascension >= 1) {
        // Note the exact float assoc: ((count*0.08f)*1.6f) (:572).
        q.elite = java_round_f(static_cast<float>(count) * kEliteRoomChance *
                               kEliteAscensionMult);
    } else {
        q.elite = java_round_f(static_cast<float>(count) * kEliteRoomChance);
    }
    q.event = java_round_f(static_cast<float>(count) * kEventRoomChance);
    q.monster = count - q.shop - q.rest - q.treasure - q.elite - q.event;
    return q;
}

// --- Trap-12 direct-XS128 Collections.shuffle --------------------------------
// java.util.Collections.shuffle(list, rnd) on a RandomAccess list (ArrayList):
//   for (int i = size; i > 1; i--) swap(list, i-1, rnd.nextInt(i));
// Here rnd IS the raw RandomXS128 (RoomTypeAssigner.java:135), so each nextInt(i)
// is RandomXS128.nextInt(i) = (int)nextLong(i): ONE next_long draw, advancing the
// raw (s0,s1) but NOT the wrapper counter. This routes through the direct-XS128
// path, never the JDK-LCG path (rng_jdk.hpp) that deck/pool shuffles use.
template <typename T>
constexpr void xs128_room_shuffle(RngStream& map_rng, T* list, int size) noexcept {
    if (size < 2) return;
    RandomXS128 r(map_rng.s0, map_rng.s1);
    for (int i = size - 1; i > 0; --i) {  // Java i=size..2 with swap index i-1
        const int j = r.next_int(i + 1);   // == nextInt(i) in Java's loop var
        const T tmp = list[i];
        list[i] = list[j];
        list[j] = tmp;
    }
    map_rng.s0 = r.state0();
    map_rng.s1 = r.state1();
    // counter intentionally untouched (TRAP 12).
}

// --- Assignment result -------------------------------------------------------

struct RoomAssignment {
    // Room grid in GAME orientation [floor y][col x]; None for no-edge/unfilled.
    RoomType rooms[kGameMapFloors][kGameMapCols];
    // mapRng at the END of generateMap (after the trap-12 shuffle AND the
    // setEmeraldElite draw). Matches the oracle's post-generateMap {counter,s0,s1}.
    RngStream rng;

    int available_room_count = 0;  // `count` quota base (excludes row 13)
    int node_count = 0;            // nodeCount padding target (all edge/unassigned)
    int elite_node_count = 0;      // # Elite rooms placed (setEmeraldElite bound)
    RoomQuota quota;               // pre-padding quota breakdown

    // The node setEmeraldElite's draw CHOSE (AbstractDungeon.java:551-552):
    // eliteNodes is collected row-major (i = row, j = col, :545-550, the same
    // orientation this header iterates), so the drawn index k names the k-th
    // Elite in row-major order. Recorded because the ENTRY roll
    // (MonsterRoomElite.applyEmeraldEliteBuff, MonsterRoomElite.java:39-68)
    // fires only on this one node -- the placement draw itself was already
    // modelled; the coordinates were simply discarded. -1/-1 when no Elite was
    // placed (then no draw fired either).
    int8_t emerald_x = -1;         // burning-elite column, or -1
    int8_t emerald_y = -1;         // burning-elite row (floor - 1), or -1

    [[nodiscard]] constexpr RoomType at(int x, int y) const noexcept {
        return rooms[y][x];
    }
};

namespace detail {

// ruleAssignableToRow (RoomTypeAssigner.java:84-91). rows y<=4 reject Rest+Elite;
// rows y>=13 reject Rest.
[[nodiscard]] constexpr bool rule_assignable_to_row(int y, RoomType room) noexcept {
    if (y <= 4 && (room == RoomType::Rest || room == RoomType::Elite)) return false;
    return y < 13 || room != RoomType::Rest;
}

// applicableRooms membership for ruleParentMatches (Rest/Treasure/Shop/Elite).
[[nodiscard]] constexpr bool parent_rule_applies(RoomType room) noexcept {
    return room == RoomType::Rest || room == RoomType::Treasure ||
           room == RoomType::Shop || room == RoomType::Elite;
}

// applicableRooms membership for ruleSiblingMatches (Rest/Monster/Event/Elite/Shop).
[[nodiscard]] constexpr bool sibling_rule_applies(RoomType room) noexcept {
    return room == RoomType::Rest || room == RoomType::Monster ||
           room == RoomType::Event || room == RoomType::Elite ||
           room == RoomType::Shop;
}

// ruleParentMatches (RoomTypeAssigner.java:74-82): true if some PARENT already
// holds the same class AND the class is parent-applicable.
[[nodiscard]] constexpr bool rule_parent_matches(const GeneratedMap& g,
                                                const RoomAssignment& ra,
                                                const GenNode& n,
                                                RoomType room) noexcept {
    if (!parent_rule_applies(room)) return false;
    for (int i = 0; i < n.parent_count; ++i) {
        const RoomType pr = ra.rooms[n.parent_y[i]][n.parent_x[i]];
        if (pr == room) return true;
    }
    (void)g;
    return false;
}

// ruleSiblingMatches over getSiblings (RoomTypeAssigner.java:53-72): true if any
// SIBLING (another child of a shared parent, != n) already holds the same
// class AND the class is sibling-applicable. Sibling room lookups see the
// current grid, so only already-assigned (earlier row-major) siblings count --
// exactly matching the game's in-order assignment.
[[nodiscard]] constexpr bool rule_sibling_matches(const GeneratedMap& g,
                                                 const RoomAssignment& ra,
                                                 int nx, int ny,
                                                 const GenNode& n,
                                                 RoomType room) noexcept {
    if (!sibling_rule_applies(room)) return false;
    for (int pi = 0; pi < n.parent_count; ++pi) {
        const GenNode& parent = g.at(n.parent_x[pi], n.parent_y[pi]);
        for (int ei = 0; ei < parent.edge_count; ++ei) {
            const int sx = parent.edges[ei].dst_x;
            const int sy = parent.edges[ei].dst_y;
            if (sx == nx && sy == ny) continue;      // siblingNode == n -> skip
            if (sy >= kGameMapFloors) continue;      // boss edge (never here); guard
            const RoomType sr = ra.rooms[sy][sx];
            if (sr == RoomType::None) continue;       // getRoom()==null -> skip
            if (sr == room) return true;
        }
    }
    return false;
}

}  // namespace detail

// Full generateMap tail over a path-generated graph (AbstractDungeon.java:
// 517-540 + RoomTypeAssigner). `g.rng` must be at the post-path mapRng state
// (as produced by map_gen.hpp generate_map / generate_map_graph). Returns the
// room grid and the END-of-generateMap mapRng state.
//
// `has_emerald_key` is `Settings.hasEmeraldKey` AT THE MOMENT THIS ACT IS
// GENERATED -- the run's `rs.keys & kKeyEmerald`. It is the second conjunct of
// setEmeraldElite's guard (AbstractDungeon.java:543), and that guard wraps the
// WHOLE body: once the run holds the key, every subsequently generated act
// skips the `mapRng.random(0, eliteNodes.size() - 1)` draw entirely and marks
// no node, so its end-of-generateMap mapRng {counter, s0, s1} -- and therefore
// every map generated after it -- differs from a run that never took the key.
// This is s3-design §5 trap 1, the single highest-risk change in S3, and the
// reason the keys are a run-layer feature rather than three booleans.
[[nodiscard]] constexpr RoomAssignment assign_room_types(
    const GeneratedMap& g, int ascension,
    bool has_emerald_key = false) noexcept {
    RoomAssignment ra{};
    ra.rng = g.rng;  // continue on the exact post-path mapRng.

    // (1) availableRoomCount (AbstractDungeon.java:517-523): edge nodes, y != 13.
    int count = 0;
    for (int y = 0; y < kGameMapFloors; ++y) {
        for (int x = 0; x < kGameMapCols; ++x) {
            if (!g.at(x, y).has_edges()) continue;
            if (y == kQuotaExcludedRow) continue;  // map.size()-2
            ++count;
        }
    }
    ra.available_room_count = count;

    // (2) generateRoomTypes -> roomList = [Shop.., Rest.., Elite.., Event..]
    //     (AbstractDungeon.java:582-593). Order matters: it is the pre-shuffle
    //     list order (padding appends Monster to the END later).
    const RoomQuota q = compute_room_quota(count, ascension);
    ra.quota = q;

    // roomList backing store: at most one entry per fillable node (<= 105).
    constexpr int kRoomListCap = kGameMapFloors * kGameMapCols;
    RoomType room_list[kRoomListCap];
    int room_list_size = 0;
    auto push = [&](RoomType r, int n) {
        for (int i = 0; i < n; ++i) room_list[room_list_size++] = r;
    };
    push(RoomType::Shop, q.shop);
    push(RoomType::Rest, q.rest);
    push(RoomType::Elite, q.elite);
    push(RoomType::Event, q.event);

    // (3) Fixed rows (AbstractDungeon.java:525-531). assignRowAsRoomType sets
    //     EVERY still-null node in the row (edge or not); we set the whole row.
    for (int x = 0; x < kGameMapCols; ++x) {
        ra.rooms[kRestRow][x] = RoomType::Rest;
        ra.rooms[kMonsterRow][x] = RoomType::Monster;
        ra.rooms[kTreasureRow][x] = RoomType::Treasure;
    }

    // (4) distributeRoomsAcrossMap (RoomTypeAssigner.java:127-143).
    //   (4a) nodeCount = connected & room==null (all rows, INCLUDING row 13).
    int node_count = 0;
    for (int y = 0; y < kGameMapFloors; ++y) {
        for (int x = 0; x < kGameMapCols; ++x) {
            if (!g.at(x, y).has_edges()) continue;
            if (ra.rooms[y][x] != RoomType::None) continue;  // fixed rows excluded
            ++node_count;
        }
    }
    ra.node_count = node_count;

    //   (4b) pad with MonsterRoom to reach nodeCount (:129-131).
    while (room_list_size < node_count) room_list[room_list_size++] = RoomType::Monster;

    //   (4c) TRAP 12 shuffle (:135): raw XS128, counter unchanged.
    xs128_room_shuffle(ra.rng, room_list, room_list_size);

    //   (4d) assignRoomsToNodes (:117-125): row-major; first rule-passing room
    //        from the (post-shuffle) list; remove it from the list.
    for (int y = 0; y < kGameMapFloors; ++y) {
        for (int x = 0; x < kGameMapCols; ++x) {
            const GenNode& n = g.at(x, y);
            if (!n.has_edges() || ra.rooms[y][x] != RoomType::None) continue;
            // getNextRoomTypeAccordingToRules (:93-105).
            int chosen = -1;
            for (int i = 0; i < room_list_size; ++i) {
                const RoomType r = room_list[i];
                if (!detail::rule_assignable_to_row(y, r)) continue;
                const bool blocked =
                    detail::rule_parent_matches(g, ra, n, r) ||
                    detail::rule_sibling_matches(g, ra, x, y, n, r);
                if (!blocked) { chosen = i; break; }
                if (y != 0) continue;
                chosen = i; break;  // row-0 override (:101-102)
            }
            if (chosen < 0) continue;  // null -> lastMinuteNodeChecker later
            ra.rooms[y][x] = room_list[chosen];
            // roomList.remove(index): shift the tail down.
            for (int i = chosen; i < room_list_size - 1; ++i)
                room_list[i] = room_list[i + 1];
            --room_list_size;
        }
    }

    //   (4e) lastMinuteNodeChecker (:107-115): any leftover edge node -> Monster.
    for (int y = 0; y < kGameMapFloors; ++y) {
        for (int x = 0; x < kGameMapCols; ++x) {
            if (!g.at(x, y).has_edges()) continue;
            if (ra.rooms[y][x] == RoomType::None) ra.rooms[y][x] = RoomType::Monster;
        }
    }

    // (5) setEmeraldElite (AbstractDungeon.java:539,542-556). On the fully-
    //     unlocked A20 profile the guard passes -> one mapRng.random(0,
    //     eliteNodes.size()-1) draw fires (:551), advancing counter + state. The
    //     CHOSEN node is recorded (row-major k-th Elite, matching the game's
    //     row-major eliteNodes collection, :545-550): the emerald-key ENTRY
    //     roll (MonsterRoomElite.java:39-68) is gated on exactly this node's
    //     hasEmeraldKey flag, so the coordinates are load-bearing for the run
    //     layer, not just the draw.
    int elite_nodes = 0;
    for (int y = 0; y < kGameMapFloors; ++y)
        for (int x = 0; x < kGameMapCols; ++x)
            if (ra.rooms[y][x] == RoomType::Elite) ++elite_nodes;
    ra.elite_node_count = elite_nodes;
    if (kFinalActAvailable && !has_emerald_key && elite_nodes >= 1) {
        const int32_t chosen = random(ra.rng, 0, elite_nodes - 1);  // counter += 1
        int seen = 0;
        for (int y = 0; y < kGameMapFloors && ra.emerald_x < 0; ++y) {
            for (int x = 0; x < kGameMapCols; ++x) {
                if (ra.rooms[y][x] != RoomType::Elite) continue;
                if (seen == chosen) {
                    ra.emerald_x = static_cast<int8_t>(x);
                    ra.emerald_y = static_cast<int8_t>(y);
                    break;
                }
                ++seen;
            }
        }
    }
    return ra;
}

// Convenience: path generation (map_gen.hpp) + room assignment from a run seed.
// act_num is 1..4 (S1 = act 1); ascension gates the elite x1.6 branch (A20 = 20).
[[nodiscard]] constexpr RoomAssignment generate_map_rooms(
    int64_t run_seed, int act_num, int ascension,
    bool has_emerald_key = false) noexcept {
    GeneratedMap g = generate_map(run_seed, act_num);
    return assign_room_types(g, ascension, has_emerald_key);
}

// Encode an assigned room grid into RunState.MapNode.room_type (u8) and store the
// end-of-generateMap mapRng. Uses map_gen.hpp's game-oriented index convention.
// (run_advance.cpp owns the final RunState population; this mirror exists for
// continuity with map_gen.hpp's edge encoding.)
constexpr void encode_rooms_into_run_state(const RoomAssignment& ra,
                                           RunState& rs) noexcept {
    for (int y = 0; y < kGameMapFloors; ++y) {
        for (int x = 0; x < kGameMapCols; ++x) {
            rs.map[run_state_map_index(x, y)].room_type =
                static_cast<uint8_t>(ra.rooms[y][x]);
        }
    }
    rs.map_rng = ra.rng;
}

static_assert(std::is_trivially_copyable_v<RoomAssignment>);

}  // namespace sts::engine
