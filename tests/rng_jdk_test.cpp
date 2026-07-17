#include "sts/engine/rng_jdk.hpp"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

// Byte-compares sts::engine::JdkRandom + jdk_shuffle against the JVM-captured
// golden set 4 (docs/stage-a-design.md §3.7 item 4; tools/golden_capture/
// README.md "4. jdk_shuffle_<seed>_<n>.bin").
//
// STS_GOLDEN_DIR is injected by tests/CMakeLists.txt as an absolute path to
// tests/golden, so this binary resolves golden files regardless of the CWD
// ctest launches it from.
#ifndef STS_GOLDEN_DIR
#error "STS_GOLDEN_DIR must be defined by the build (see tests/CMakeLists.txt)"
#endif

namespace {

struct SeedEntry {
    std::string label;
    int64_t seed;
};

// Parses tests/golden/seed_battery.txt: "# label\tvalue" header line, then
// one "<label>\t<signed-decimal-seed>" row per battery entry.
std::vector<SeedEntry> LoadSeedBattery() {
    const std::string path = std::string(STS_GOLDEN_DIR) + "/seed_battery.txt";
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "failed to open " << path;

    std::vector<SeedEntry> entries;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream iss(line);
        std::string label;
        int64_t value = 0;
        if (!(iss >> label >> value)) {
            continue;
        }
        entries.push_back({label, value});
    }
    return entries;
}

// Reads an n-element big-endian int32 array (see golden_capture/README.md
// "Binary format": all multi-byte values are big-endian).
std::vector<int32_t> ReadGoldenInts(const std::string& path, std::size_t n) {
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.good()) << "failed to open " << path;

    std::vector<int32_t> values(n);
    for (std::size_t i = 0; i < n; ++i) {
        unsigned char bytes[4] = {0, 0, 0, 0};
        in.read(reinterpret_cast<char*>(bytes), 4);
        EXPECT_TRUE(in.good()) << "short read from " << path;
        const uint32_t bits = (static_cast<uint32_t>(bytes[0]) << 24) |
                               (static_cast<uint32_t>(bytes[1]) << 16) |
                               (static_cast<uint32_t>(bytes[2]) << 8) |
                               static_cast<uint32_t>(bytes[3]);
        values[i] = static_cast<int32_t>(bits);
    }
    return values;
}

// Reproduces exactly what GoldenCapture.java does for category 4:
// Collections.shuffle(1..n, new java.util.Random(seed)).
std::vector<int32_t> ComputeShuffle(int64_t seed, std::size_t n) {
    std::vector<int32_t> values(n);
    for (std::size_t i = 0; i < n; ++i) {
        values[i] = static_cast<int32_t>(i + 1);
    }
    sts::engine::JdkRandom rng(seed);
    sts::engine::jdk_shuffle(std::span<int32_t>(values), rng);
    return values;
}

constexpr int kSizes[] = {5, 10, 71, 128};

}  // namespace

TEST(RngJdk, ShuffleMatchesGoldenSetFour) {
    const std::vector<SeedEntry> battery = LoadSeedBattery();
    ASSERT_FALSE(battery.empty());

    for (const SeedEntry& entry : battery) {
        for (const int n : kSizes) {
            SCOPED_TRACE("label=" + entry.label + " n=" + std::to_string(n));

            const std::string path = std::string(STS_GOLDEN_DIR) + "/jdk_shuffle_" +
                                      entry.label + "_" + std::to_string(n) + ".bin";
            const std::vector<int32_t> expected =
                ReadGoldenInts(path, static_cast<std::size_t>(n));
            const std::vector<int32_t> actual =
                ComputeShuffle(entry.seed, static_cast<std::size_t>(n));

            ASSERT_EQ(expected.size(), actual.size());
            for (std::size_t i = 0; i < expected.size(); ++i) {
                EXPECT_EQ(expected[i], actual[i]) << "mismatch at index " << i;
            }
        }
    }
}

// Trap 2 (docs/stage-a-design.md §10): "Shuffles route through
// java.util.Random, not xorshift128+." sts::engine::JdkRandom is a distinct
// type from RandomXS128 with its own 48-bit LCG state and its own
// nextInt(bound) algorithm -- jdk_shuffle only ever accepts a JdkRandom&, so
// there is no code path by which a shuffle could be driven by the xorshift
// engine (a structural/API-shape guarantee, checked at compile time by the
// call below). The concrete regression is a golden-vector match: seed 0,
// n=5 is a small, easy-to-eyeball case that would silently pass with a
// naive/incorrect RNG (e.g. one seeded and driven directly by xorshift128+)
// only by coincidence -- verified against the golden file, not hand-derived.
TEST(RngJdkTrap, ShufflesRouteThroughJdkLcgNotXorshift) {
    const std::vector<int32_t> expected =
        ReadGoldenInts(std::string(STS_GOLDEN_DIR) + "/jdk_shuffle_0_5.bin", 5);
    const std::vector<int32_t> actual = ComputeShuffle(/*seed=*/0, /*n=*/5);
    EXPECT_EQ(expected, actual);
}
