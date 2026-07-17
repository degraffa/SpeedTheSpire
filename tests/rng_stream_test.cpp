// Tier-1 RNG suite for task A1.3 (docs/stage-a-tasks.md). Byte-compares the
// game's Random-wrapper re-expression in sts::engine (rng_stream.hpp) against
// the JVM-captured golden vectors:
//   - golden set 2: wrapper_<seed>.bin        (1000 draws per method/overload)
//   - golden set 3: counter_restore_<seed>.bin (137-draw restore equivalence)
//   - golden set 5: floor_derive_<seed>.bin    (floor/act stream derivation)
// plus named tests for docs/stage-a-design.md §10 traps 3 and 7.
//
// Binary format: tools/golden_capture/README.md ("Binary format" + sections
// 2, 3, 5). All multi-byte values are big-endian.
//
// STS_GOLDEN_DIR is injected by tests/CMakeLists.txt as an absolute path to
// tests/golden, so this binary resolves golden files regardless of the CWD
// ctest launches it from.

#include "sts/engine/rng_stream.hpp"

#include <bit>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#ifndef STS_GOLDEN_DIR
#error "STS_GOLDEN_DIR must be defined by the build (see tests/CMakeLists.txt)"
#endif

// Regression guard duplicating the header's load-bearing asserts (design doc
// §3.6/§4.1; ledger acceptance line "static_assert trivially-copyable + size
// 24").
static_assert(std::is_trivially_copyable_v<sts::engine::RngStream>);
static_assert(sizeof(sts::engine::RngStream) == 24);

namespace {

using sts::engine::RngStream;

std::string GoldenPath(const std::string& relative) {
    return std::string(STS_GOLDEN_DIR) + "/" + relative;
}

struct SeedEntry {
    std::string label;
    int64_t seed = 0;
};

// Parses tests/golden/seed_battery.txt: "# label\tvalue" header, then one
// "<label>\t<signed decimal long>" row per battery seed.
std::vector<SeedEntry> LoadSeedBattery() {
    std::vector<SeedEntry> entries;
    std::ifstream in(GoldenPath("seed_battery.txt"));
    if (!in.is_open()) {
        return entries;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        SeedEntry entry;
        entry.label = line.substr(0, tab);
        entry.seed = std::stoll(line.substr(tab + 1));
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<unsigned char> ReadFileBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    in.unsetf(std::ios::skipws);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in),
                                      std::istreambuf_iterator<char>());
}

// Golden files store all multi-byte values big-endian (MSB first).
int64_t ReadBigEndianI64(const unsigned char* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | static_cast<uint64_t>(p[i]);
    }
    return static_cast<int64_t>(v);
}

int32_t ReadBigEndianI32(const unsigned char* p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v = (v << 8) | static_cast<uint32_t>(p[i]);
    }
    return static_cast<int32_t>(v);
}

// Draws one value of the given wrapper method-id from `s`, returning its
// `result_bits` in the exact encoding the golden capture stored (see README
// section 2): int methods sign-extend to int64; long/randomLong verbatim;
// boolean 0/1; float methods store the raw IEEE-754 bit pattern zero-extended
// into the int64 slot. Method ids and their canonical fixed args mirror the
// README's method-id table exactly.
uint64_t DrawOnceBits(RngStream& s, int method_id) {
    using namespace sts::engine;
    switch (method_id) {
        case 0:  // random(int range=999)
            return static_cast<uint64_t>(static_cast<int64_t>(random(s, 999)));
        case 1:  // random(int start=-500, end=500)
            return static_cast<uint64_t>(
                static_cast<int64_t>(random(s, -500, 500)));
        case 2:  // random(long range=1e12)
            return static_cast<uint64_t>(
                random(s, static_cast<int64_t>(1000000000000LL)));
        case 3:  // random(long start=-5e11, end=5e11)
            return static_cast<uint64_t>(
                random(s, static_cast<int64_t>(-500000000000LL),
                       static_cast<int64_t>(500000000000LL)));
        case 4:  // randomLong()
            return static_cast<uint64_t>(random_long(s));
        case 5:  // randomBoolean()
            return random_boolean(s) ? 1u : 0u;
        case 6:  // randomBoolean(float chance=0.5f)
            return random_boolean(s, 0.5f) ? 1u : 0u;
        case 7:  // random()  -> float
            return static_cast<uint64_t>(std::bit_cast<uint32_t>(random(s)));
        case 8:  // random(float range=100.0f)
            return static_cast<uint64_t>(
                std::bit_cast<uint32_t>(random(s, 100.0f)));
        case 9:  // random(float start=-50.0f, end=50.0f)
            return static_cast<uint64_t>(
                std::bit_cast<uint32_t>(random(s, -50.0f, 50.0f)));
        default:
            ADD_FAILURE() << "unknown method id " << method_id;
            return 0;
    }
}

}  // namespace

// --- Golden set 2: wrapper methods, 1000 draws each -----------------------

TEST(RngStreamGolden, WrapperMethodsMatchGoldenSet2ForEverySeed) {
    const std::vector<SeedEntry> battery = LoadSeedBattery();
    ASSERT_FALSE(battery.empty())
        << "failed to load " << GoldenPath("seed_battery.txt");

    constexpr int kMethods = 10;
    constexpr int kDraws = 1000;
    // header {method_id:i32, arg0:i64, arg1:i64} = 20 bytes;
    // record {counter_after:i32, result_bits:i64} = 12 bytes.
    constexpr size_t kHeader = 4 + 8 + 8;
    constexpr size_t kRecord = 4 + 8;
    constexpr size_t kBlock = kHeader + static_cast<size_t>(kDraws) * kRecord;
    constexpr size_t kFileSize = static_cast<size_t>(kMethods) * kBlock;

    for (const SeedEntry& entry : battery) {
        SCOPED_TRACE("seed label = " + entry.label);
        const std::string path = GoldenPath("wrapper_" + entry.label + ".bin");
        const std::vector<unsigned char> bytes = ReadFileBytes(path);
        ASSERT_EQ(bytes.size(), kFileSize) << "missing/wrong size: " << path;

        for (int id = 0; id < kMethods; ++id) {
            const size_t block_off = static_cast<size_t>(id) * kBlock;
            const int32_t file_id = ReadBigEndianI32(&bytes[block_off]);
            ASSERT_EQ(file_id, id) << "block " << id << " has method_id " << file_id;

            // Each block replays from a FRESH stream (README section 2).
            RngStream s = sts::engine::from_seed(entry.seed);
            for (int i = 0; i < kDraws; ++i) {
                const size_t rec_off =
                    block_off + kHeader + static_cast<size_t>(i) * kRecord;
                const int32_t exp_counter = ReadBigEndianI32(&bytes[rec_off]);
                const uint64_t exp_bits =
                    static_cast<uint64_t>(ReadBigEndianI64(&bytes[rec_off + 4]));

                const uint64_t got_bits = DrawOnceBits(s, id);
                ASSERT_EQ(s.counter, exp_counter)
                    << "counter mismatch, method " << id << " draw " << i;
                ASSERT_EQ(got_bits, exp_bits)
                    << "result mismatch, method " << id << " draw " << i;
            }
        }
    }
}

// --- Golden set 3: counter-restore equivalence (N = 137) ------------------

TEST(RngStreamGolden, CounterRestoreMatchesGoldenSet3) {
    const std::vector<SeedEntry> battery = LoadSeedBattery();
    ASSERT_FALSE(battery.empty());

    constexpr int32_t kN = 137;
    // {N:i32} then three {s0:i64, s1:i64, counter:i32} = 4 + 3*20 = 64 bytes.
    constexpr size_t kFileSize = 4 + 3 * (8 + 8 + 4);

    for (const SeedEntry& entry : battery) {
        SCOPED_TRACE("seed label = " + entry.label);
        const std::string path =
            GoldenPath("counter_restore_" + entry.label + ".bin");
        const std::vector<unsigned char> bytes = ReadFileBytes(path);
        ASSERT_EQ(bytes.size(), kFileSize) << "missing/wrong size: " << path;

        const int32_t file_n = ReadBigEndianI32(&bytes[0]);
        ASSERT_EQ(file_n, kN);

        auto read_block = [&](size_t off) {
            RngStream s{};
            s.s0 = static_cast<uint64_t>(ReadBigEndianI64(&bytes[off]));
            s.s1 = static_cast<uint64_t>(ReadBigEndianI64(&bytes[off + 8]));
            s.counter = ReadBigEndianI32(&bytes[off + 16]);
            return s;
        };
        const RngStream golden_direct = read_block(4);
        const RngStream golden_ctor = read_block(4 + 20);
        const RngStream golden_setc = read_block(4 + 40);

        // Block 1: 137 mixed drawOnce() calls, method ids 0..9 cycling.
        RngStream direct = sts::engine::from_seed(entry.seed);
        for (int32_t i = 0; i < kN; ++i) {
            (void)DrawOnceBits(direct, i % 10);
        }
        EXPECT_EQ(direct.s0, golden_direct.s0);
        EXPECT_EQ(direct.s1, golden_direct.s1);
        EXPECT_EQ(direct.counter, golden_direct.counter);

        // Block 2: new Random(seed, 137) -- 137 x random(999) replay.
        const RngStream ctor = sts::engine::from_seed_counter(entry.seed, kN);
        EXPECT_EQ(ctor.s0, golden_ctor.s0);
        EXPECT_EQ(ctor.s1, golden_ctor.s1);
        EXPECT_EQ(ctor.counter, golden_ctor.counter);

        // Block 3: new Random(seed).setCounter(137) -- 137 x randomBoolean().
        const RngStream setc = sts::engine::from_seed_set_counter(entry.seed, kN);
        EXPECT_EQ(setc.s0, golden_setc.s0);
        EXPECT_EQ(setc.s1, golden_setc.s1);
        EXPECT_EQ(setc.counter, golden_setc.counter);

        // The one-draw invariant: all three replay paths -- despite drawing
        // different VALUES -- land on the identical engine state after N draws.
        EXPECT_EQ(direct.s0, ctor.s0);
        EXPECT_EQ(direct.s1, ctor.s1);
        EXPECT_EQ(direct.s0, setc.s0);
        EXPECT_EQ(direct.s1, setc.s1);
    }
}

// --- Golden set 5: floor / act stream derivation --------------------------

TEST(RngStreamGolden, FloorAndActDerivationMatchGoldenSet5) {
    const std::vector<SeedEntry> battery = LoadSeedBattery();
    ASSERT_FALSE(battery.empty());

    constexpr int kFloors = 55;
    constexpr int kStreamsPerFloor = 5;
    constexpr int kActs = 4;
    constexpr size_t kRec = 16;  // {s0:i64, s1:i64}
    constexpr size_t kFileSize =
        (static_cast<size_t>(kFloors) * kStreamsPerFloor + kActs) * kRec;

    // mapRng seed offsets for acts 1..4 (README section 5): +1, +200, +600, +1200.
    constexpr int64_t kActOffset[kActs] = {1, 200, 600, 1200};

    for (const SeedEntry& entry : battery) {
        SCOPED_TRACE("seed label = " + entry.label);
        const std::string path =
            GoldenPath("floor_derive_" + entry.label + ".bin");
        const std::vector<unsigned char> bytes = ReadFileBytes(path);
        ASSERT_EQ(bytes.size(), kFileSize) << "missing/wrong size: " << path;

        // Floors 1..55: all 5 stream slots per floor share one seed (seed+floor).
        for (int floor = 1; floor <= kFloors; ++floor) {
            const RngStream got = sts::engine::floor_stream(entry.seed, floor);
            EXPECT_EQ(got.counter, 0);
            for (int slot = 0; slot < kStreamsPerFloor; ++slot) {
                const size_t off =
                    (static_cast<size_t>(floor - 1) * kStreamsPerFloor + slot) * kRec;
                const uint64_t exp_s0 =
                    static_cast<uint64_t>(ReadBigEndianI64(&bytes[off]));
                const uint64_t exp_s1 =
                    static_cast<uint64_t>(ReadBigEndianI64(&bytes[off + 8]));
                ASSERT_EQ(got.s0, exp_s0)
                    << "floor " << floor << " slot " << slot << " s0";
                ASSERT_EQ(got.s1, exp_s1)
                    << "floor " << floor << " slot " << slot << " s1";
            }
        }

        // Trailing 4 records: mapRng for acts 1..4.
        for (int act = 1; act <= kActs; ++act) {
            const RngStream got = sts::engine::map_stream(entry.seed, act);
            EXPECT_EQ(got.counter, 0);
            const size_t off =
                (static_cast<size_t>(kFloors) * kStreamsPerFloor +
                 static_cast<size_t>(act - 1)) * kRec;
            const uint64_t exp_s0 =
                static_cast<uint64_t>(ReadBigEndianI64(&bytes[off]));
            const uint64_t exp_s1 =
                static_cast<uint64_t>(ReadBigEndianI64(&bytes[off + 8]));
            ASSERT_EQ(got.s0, exp_s0) << "act " << act << " s0";
            ASSERT_EQ(got.s1, exp_s1) << "act " << act << " s1";

            // Cross-check the header's offset arithmetic independently.
            const RngStream ref =
                sts::engine::from_seed(entry.seed + kActOffset[act - 1]);
            EXPECT_EQ(got.s0, ref.s0) << "act " << act << " offset arithmetic";
            EXPECT_EQ(got.s1, ref.s1) << "act " << act << " offset arithmetic";
        }
    }
}

// --- Trap 3: random(int) bounds are inclusive -----------------------------

TEST(RngStreamTrap, RandomIntBoundsAreInclusive) {
    // random(range) == nextInt(range + 1), so over a fresh stream every draw
    // stays within [0, range] and the top of the range (range itself) is
    // reachable -- unreachable if the bound were treated as exclusive.
    RngStream s = sts::engine::from_seed(42);
    bool saw_top = false;
    constexpr int32_t kRange = 5;  // small bound so the ceiling is hit quickly
    for (int i = 0; i < 100000; ++i) {
        const int32_t v = sts::engine::random(s, kRange);
        ASSERT_GE(v, 0);
        ASSERT_LE(v, kRange) << "draw " << i << " exceeded inclusive bound";
        if (v == kRange) {
            saw_top = true;
        }
    }
    EXPECT_TRUE(saw_top)
        << "random(" << kRange << ") never returned " << kRange
        << " -- the inclusive upper bound (nextInt(range+1)) was not honored";

    // Direct structural check: random(range) must equal nextInt(range+1) on the
    // same underlying draw.
    RngStream a = sts::engine::from_seed(7);
    sts::engine::RandomXS128 ref(a.s0, a.s1);
    const int32_t via_wrapper = sts::engine::random(a, 999);
    const int32_t via_primitive = ref.next_int(1000);  // range + 1
    EXPECT_EQ(via_wrapper, via_primitive);
    EXPECT_EQ(a.counter, 1);
}

// --- Trap 7: floor reseed uses the floor number AFTER increment -----------

TEST(RngStreamTrap, FloorReseedUsesFloorNumberAfterIncrement) {
    constexpr int64_t kSeed = 123456789;

    // Entering floor N reseeds as Random(seed + N) using the POST-increment
    // floor number. So the stream for "arriving on floor N" must equal
    // from_seed(seed + N), NOT from_seed(seed + (N-1)) (the pre-increment
    // value, which is the adjacent-floor bug this trap guards against).
    for (int32_t floor = 1; floor <= 55; ++floor) {
        const RngStream got = sts::engine::floor_stream(kSeed, floor);
        const RngStream after_increment = sts::engine::from_seed(kSeed + floor);
        EXPECT_EQ(got.s0, after_increment.s0) << "floor " << floor;
        EXPECT_EQ(got.s1, after_increment.s1) << "floor " << floor;

        const RngStream before_increment = sts::engine::from_seed(kSeed + floor - 1);
        // Distinct seeds give distinct scrambled state, so the off-by-one
        // (pre-increment) seeding is observably wrong.
        EXPECT_NE(got.s0, before_increment.s0)
            << "floor " << floor << " matched the pre-increment seed";
    }
}
