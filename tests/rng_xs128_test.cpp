// Tier-1 RNG suite for task A1.1 (docs/stage-a-tasks.md). Byte-compares
// sts::engine::RandomXS128::next_long() against the JVM-captured golden
// vectors (tests/golden/xs128_<label>.bin, see
// tools/golden_capture/README.md for the binary format and
// tests/golden/seed_battery.txt for the label -> seed mapping), plus one
// named test per applicable docs/stage-a-design.md §10 trap (4, 5, 11).

#include "sts/engine/rng_xs128.hpp"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

// STS_GOLDEN_DIR is injected by tests/CMakeLists.txt as an absolute path to
// tests/golden/ (CTest/GoogleTest run with CWD = the build dir, not the
// source tree, so a relative path would not be robust).
#ifndef STS_GOLDEN_DIR
#error "STS_GOLDEN_DIR must be defined by the build (see tests/CMakeLists.txt)"
#endif

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

// The golden files are raw int64 big-endian (see tools/golden_capture/README.md
// "Binary format"), so bytes must be reassembled MSB-first regardless of host
// endianness.
int64_t ReadBigEndianI64(const unsigned char* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | static_cast<uint64_t>(p[i]);
    }
    return static_cast<int64_t>(v);
}

}  // namespace

// --- Golden set 1: RandomXS128 first 10k nextLong() per battery seed ------

TEST(RngXs128Golden, NextLongMatchesGoldenSet1ForEverySeedInBattery) {
    const std::vector<SeedEntry> battery = LoadSeedBattery();
    ASSERT_FALSE(battery.empty())
        << "failed to load " << GoldenPath("seed_battery.txt")
        << " (run tools/golden_capture/regen.ps1 on the Windows host first)";

    constexpr int kDrawCount = 10000;
    constexpr size_t kExpectedFileSize = static_cast<size_t>(kDrawCount) * 8;

    for (const SeedEntry& entry : battery) {
        SCOPED_TRACE("seed label = " + entry.label +
                     ", seed value = " + std::to_string(entry.seed));

        const std::string golden_path = GoldenPath("xs128_" + entry.label + ".bin");
        const std::vector<unsigned char> golden_bytes = ReadFileBytes(golden_path);
        ASSERT_EQ(golden_bytes.size(), kExpectedFileSize)
            << "golden file missing or wrong size: " << golden_path;

        sts::engine::RandomXS128 rng(entry.seed);
        for (int i = 0; i < kDrawCount; ++i) {
            const int64_t expected = ReadBigEndianI64(&golden_bytes[static_cast<size_t>(i) * 8]);
            const int64_t actual = rng.next_long();
            ASSERT_EQ(actual, expected)
                << "first divergence at draw index " << i << " for seed label "
                << entry.label;
        }
    }
}

// --- Trap 4: nextLong(n)'s rejection loop must be kept -----------------
//
// The reject branch fires when `bits - (bits % n) + (n - 1)` overflows a
// signed int64. For typical in-game bounds (tens to low thousands) that is
// astronomically rare (~1e-16 per docs/stage-a-design.md §10), but choosing
// n just above 2^62 makes the reject probability ~50% per draw (any draw
// whose top-bit-cleared value is >= n immediately overflows), which lets
// this test force and observe the branch deterministically and quickly
// instead of merely asserting the loop's source-level shape.

TEST(RngXs128Trap, RejectionLoopNotSimplifiedToModulo) {
    constexpr int64_t n = (int64_t{1} << 62) + 1;  // ~50% reject rate, see above.

    std::optional<int64_t> found_seed;
    int64_t naive_result = 0;
    for (int64_t seed = 1; seed <= 1000; ++seed) {
        sts::engine::RandomXS128 probe(seed);
        const uint64_t raw = static_cast<uint64_t>(probe.next_long());
        const uint64_t bits = raw >> 1;
        const int64_t value = static_cast<int64_t>(bits % static_cast<uint64_t>(n));
        const uint64_t check = bits - static_cast<uint64_t>(value) + static_cast<uint64_t>(n - 1);
        if (static_cast<int64_t>(check) < 0) {
            found_seed = seed;
            naive_result = value;
            break;
        }
    }

    ASSERT_TRUE(found_seed.has_value())
        << "could not find a seed whose first draw triggers nextLong(n)'s "
           "rejection branch within the search budget";

    // `naive_result` is exactly what a "simplified to modulo" implementation
    // (a single nextLong() draw, no reject/retry) would have returned.
    sts::engine::RandomXS128 real(*found_seed);
    const int64_t real_result = real.next_long(n);

    EXPECT_GE(real_result, 0);
    EXPECT_LT(real_result, n);

    // Because the correct implementation must reject the first draw and
    // consume a second (independent) nextLong(), its result generically
    // differs from the naive single-draw modulo shortcut. Equality here
    // would mean either the loop never rejected (a bug in this test's
    // search) or the implementation under test skips the retry (the
    // regression this test exists to catch).
    EXPECT_NE(real_result, naive_result)
        << "next_long(n) returned the same value a naive single-draw modulo "
           "would have, even though this seed/bound was chosen specifically "
           "to force the rejection branch — the retry loop may have been "
           "simplified away";
}

// --- Trap 5: seed 0 scrambles as INT64_MIN, not as 0 --------------------

TEST(RngXs128Trap, SeedZeroScramblesAsInt64Min) {
    sts::engine::RandomXS128 zero_seeded(0);
    sts::engine::RandomXS128 min_seeded(std::numeric_limits<int64_t>::min());

    // If seed 0 were scrambled as literal 0 (the bug this trap guards
    // against), zero_seeded's state would differ from min_seeded's.
    EXPECT_EQ(zero_seeded.get_state(), min_seeded.get_state());

    for (int i = 0; i < 256; ++i) {
        ASSERT_EQ(zero_seeded.next_long(), min_seeded.next_long())
            << "diverged at draw " << i;
    }
}

// --- Trap 11: nextFloat() narrows a double product, not a float one -----

TEST(RngXs128Trap, NextFloatNarrowsFromDouble) {
    // Part 1: next_float() must match a reference that mirrors the Java
    // source structure exactly -- multiply (nextLong() >>> 40) by NORM_FLOAT
    // in DOUBLE precision, then narrow the product to float as the very last
    // step. This is the assertion that would catch a regression to a wrong
    // shift amount, wrong constant, or wrong cast order.
    sts::engine::RandomXS128 rng(12345);
    sts::engine::RandomXS128 reference(12345);
    constexpr double kNormFloat = 5.960464477539063e-8;  // exactly 2^-24

    for (int i = 0; i < 5000; ++i) {
        const float actual = rng.next_float();

        const uint64_t raw = static_cast<uint64_t>(reference.next_long());
        const uint64_t bits40 = raw >> 40;
        const float expected =
            static_cast<float>(static_cast<double>(bits40) * kNormFloat);

        ASSERT_EQ(actual, expected)
            << "next_float() at draw " << i
            << " does not match a double-precision-multiply-then-narrow "
               "reference computation";
    }

    // Part 2: prove the "multiply in float instead" simplification is a real
    // hazard in general, not a theoretical one -- even though, for
    // next_float()'s own operand (nextLong() >>> 40 is always <= 24 bits,
    // and NORM_FLOAT above is an *exact* power of two), rounding commutes
    // with the scaling and the two techniques happen to always agree for
    // that specific combination. Using an operand wider than float's 24-bit
    // significand together with a multiplier that is NOT an exact power of
    // two (unlike NORM_FLOAT) reproduces the general failure mode this trap
    // guards against, independent of RandomXS128 itself.
    constexpr double kNonPowerOfTwoMultiplier = 0.1;
    bool general_hazard_confirmed = false;
    for (uint64_t operand = (uint64_t{1} << 24) + 1; operand < (uint64_t{1} << 24) + 5000;
         ++operand) {
        const float via_float_multiply =
            static_cast<float>(operand) * static_cast<float>(kNonPowerOfTwoMultiplier);
        const float via_double_multiply_then_narrow =
            static_cast<float>(static_cast<double>(operand) * kNonPowerOfTwoMultiplier);
        if (via_float_multiply != via_double_multiply_then_narrow) {
            general_hazard_confirmed = true;
            break;
        }
    }

    EXPECT_TRUE(general_hazard_confirmed)
        << "expected to find at least one operand where multiplying in float "
           "precision diverges from multiplying in double precision and "
           "narrowing only the final result -- this is why next_float() "
           "insists on the double intermediate even where, for its own "
           "particular operand width and power-of-two constant, the two "
           "approaches happen to coincide";
}
