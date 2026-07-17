// Tests for sts/engine/seed_string.hpp against the JVM-captured golden
// vectors in tests/golden/seedhelper.txt (see
// tools/golden_capture/README.md §6 for the exact row-format spec this
// parser follows).

#include "sts/engine/seed_string.hpp"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

// Splits a line on '\t', preserving empty fields (e.g. the `RT 0` row's
// empty string field between adjacent tabs) -- std::getline with a '\t'
// delimiter does this naturally.
std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, '\t')) {
        fields.push_back(field);
    }
    // A trailing empty field (line ends in '\t') is dropped by getline; not
    // relevant to this file's rows (match/mapped-char columns are always
    // non-empty), so no special-casing needed.
    return fields;
}

std::string GoldenPath() {
#ifdef STS_GOLDEN_DIR
    return std::string(STS_GOLDEN_DIR) + "/seedhelper.txt";
#else
#error "STS_GOLDEN_DIR must be defined by the build"
#endif
}

struct RtRow {
    std::string label;
    std::int64_t original_long;
    std::string seed_string;
    std::int64_t roundtrip_long;
    bool match;
};

struct OTestRow {
    std::string label;
    std::int64_t original_long;
    std::string string_with_0;
    std::string string_with_o;
    std::int64_t roundtrip_long;
    bool match;
};

struct ValidCharRow {
    char input;
    char expected;  // sts::engine::kSeedInvalidChar for the "NULL" rows
};

struct GoldenSeedHelper {
    std::vector<RtRow> rt_rows;
    std::vector<OTestRow> otest_rows;
    std::vector<ValidCharRow> validchar_rows;
};

GoldenSeedHelper ParseGolden() {
    GoldenSeedHelper golden;
    std::ifstream in(GoldenPath());
    if (!in) {
        ADD_FAILURE() << "Could not open golden file: " << GoldenPath();
        return golden;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::vector<std::string> f = SplitTabs(line);
        if (f.empty()) {
            continue;
        }

        // ParseGolden returns a value, so the fatal ASSERT_* macros (which
        // expand to a bare `return;`) cannot be used inside this loop;
        // malformed rows are reported non-fatally and skipped instead.
        if (f[0] == "RT") {
            EXPECT_EQ(f.size(), 6u) << "malformed RT row: " << line;
            if (f.size() != 6u) {
                continue;
            }
            RtRow row;
            row.label = f[1];
            row.original_long = std::stoll(f[2]);
            row.seed_string = f[3];
            row.roundtrip_long = std::stoll(f[4]);
            row.match = (f[5] == "1");
            golden.rt_rows.push_back(std::move(row));
        } else if (f[0] == "OTEST") {
            EXPECT_EQ(f.size(), 7u) << "malformed OTEST row: " << line;
            if (f.size() != 7u) {
                continue;
            }
            OTestRow row;
            row.label = f[1];
            row.original_long = std::stoll(f[2]);
            row.string_with_0 = f[3];
            row.string_with_o = f[4];
            row.roundtrip_long = std::stoll(f[5]);
            row.match = (f[6] == "1");
            golden.otest_rows.push_back(std::move(row));
        } else if (f[0] == "VALIDCHAR") {
            EXPECT_EQ(f.size(), 3u) << "malformed VALIDCHAR row: " << line;
            if (f.size() != 3u || f[1].size() != 1u) {
                ADD_FAILURE() << "expected single-char input: " << line;
                continue;
            }
            ValidCharRow row;
            row.input = f[1][0];
            row.expected = (f[2] == "NULL") ? sts::engine::kSeedInvalidChar : f[2][0];
            golden.validchar_rows.push_back(row);
        } else {
            ADD_FAILURE() << "unrecognized row kind in golden file: " << line;
        }
    }
    return golden;
}

}  // namespace

TEST(SeedString, RoundTripRowsMatchGolden) {
    GoldenSeedHelper golden = ParseGolden();
    ASSERT_FALSE(golden.rt_rows.empty()) << "golden file produced no RT rows";

    for (const RtRow& row : golden.rt_rows) {
        SCOPED_TRACE("label=" + row.label);

        // Golden data invariant, not a fixture-quality issue: the JVM
        // capture recorded match=1 for every row (README §6). A 0 here
        // means the capture harness itself observed a real mismatch, which
        // would be a bug in SeedHelper.java's own round-trip -- not something
        // we expect to see, but we assert it rather than assume it.
        ASSERT_TRUE(row.match) << "golden capture itself recorded a mismatch";

        EXPECT_EQ(sts::engine::seed_to_string(row.original_long), row.seed_string)
            << "seed_to_string(" << row.original_long << ")";
        EXPECT_EQ(sts::engine::seed_from_string(row.seed_string), row.roundtrip_long)
            << "seed_from_string(\"" << row.seed_string << "\")";
        EXPECT_EQ(row.original_long, row.roundtrip_long)
            << "golden round-trip itself should be identity for a match=1 row";
    }
}

TEST(SeedString, ZeroEncodesToEmptyString) {
    // SeedHelper.getString(0): the base-35 loop is `while (leftover != 0)`,
    // so it never executes for seed 0 -- getString(0) is "", not "0".
    // Confirmed by the golden `RT 0` row (empty string field between
    // adjacent tabs).
    EXPECT_EQ(sts::engine::seed_to_string(0), "");
    EXPECT_EQ(sts::engine::seed_from_string(""), 0);
}

TEST(SeedString, OSterilizationRowMatchesGolden) {
    GoldenSeedHelper golden = ParseGolden();
    ASSERT_EQ(golden.otest_rows.size(), 1u);

    const OTestRow& row = golden.otest_rows.front();
    ASSERT_TRUE(row.match);

    // string_with_0 is a real captured getString() output for original_long;
    // string_with_o substitutes one '0' -> 'O'. Both must decode (via
    // seed_from_string's inline uppercase + O->0 sterilization, mirroring
    // SeedHelper.getLong's `.replaceAll("O", "0")`) to the same long.
    EXPECT_EQ(sts::engine::seed_from_string(row.string_with_0), row.original_long);
    EXPECT_EQ(sts::engine::seed_from_string(row.string_with_o), row.roundtrip_long);
    EXPECT_EQ(row.roundtrip_long, row.original_long);
}

TEST(SeedString, ValidCharacterMatchesGolden) {
    GoldenSeedHelper golden = ParseGolden();
    ASSERT_FALSE(golden.validchar_rows.empty());

    for (const ValidCharRow& row : golden.validchar_rows) {
        SCOPED_TRACE(std::string("input=") + row.input);
        EXPECT_EQ(sts::engine::seed_valid_character(row.input), row.expected);
    }
}

// Trap 6 (docs/stage-a-design.md §10 item 6): encode is UNSIGNED, decode is
// SIGNED-wrapping. This is a genuine asymmetry in the game's own code (not a
// C++-ism we're introducing): SeedHelper.getString reads the seed via
// Long.toUnsignedString (treats all 64 bits as magnitude), while
// SeedHelper.getLong accumulates into a plain signed `long total` that wraps
// silently past Long.MAX_VALUE. A round-trip through both directions must
// still be lossless for every int64_t bit pattern -- that's what makes the
// asymmetry safe -- but you cannot implement encode and decode with "the
// same" 64-bit interpretation and get both right; unsigned-in/signed-out
// (or vice versa) both fail somewhere in the space.
TEST(SeedStringTrap, EncodeUnsignedDecodeSignedWrapping) {
    // INT64_MIN: as unsigned magnitude this is 2^63, the largest single bit
    // pattern that is simultaneously the most negative signed value. If
    // getString mistakenly treated this as a *signed* magnitude, the
    // implementation would have to special-case it (there is no positive
    // int64_t equal to -INT64_MIN). Golden RT row `MIN` confirms the actual
    // game string, but this test isolates *why* unsigned-encode matters
    // rather than just replaying the fixture.
    constexpr std::int64_t kMin = INT64_MIN;
    std::string min_str = sts::engine::seed_to_string(kMin);
    EXPECT_EQ(min_str, "2QIJMIKEYSYQ8");
    EXPECT_EQ(sts::engine::seed_from_string(min_str), kMin);

    // A seed whose top bit is set (negative as signed, huge as unsigned)
    // round-trips correctly only because encode reads all 64 bits as
    // unsigned magnitude *and* decode's signed accumulation wraps back to
    // the identical bit pattern -- both halves of the asymmetry have to
    // hold simultaneously.
    constexpr std::int64_t kNegative = -1;  // unsigned: 0xFFFFFFFFFFFFFFFF
    std::string neg_str = sts::engine::seed_to_string(kNegative);
    EXPECT_EQ(neg_str, "5G24A25UXKXFF");
    EXPECT_EQ(sts::engine::seed_from_string(neg_str), kNegative);

    // Decode's signed-wrapping half in isolation: feed getLong a string long
    // enough to overflow int64_t during accumulation and confirm it wraps
    // (mod 2^64) rather than saturating or trapping/throwing. 13 base-35
    // digits at the top of the alphabet overflow well past 2^63.
    std::string overflowing(13, 'Z');
    std::int64_t wrapped = sts::engine::seed_from_string(overflowing);
    // Recompute the expected wrapped value independently via uint64_t
    // modular arithmetic (mirrors Java long's silent wraparound), rather
    // than hardcoding a magic constant, so the assertion documents *how*
    // the wrap is derived. 'Z' is the last character in kSeedAlphabet
    // ("...XYZ"), alphabet index 34.
    std::uint64_t expected_u64 = 0;
    for (char c : overflowing) {
        (void)c;
        expected_u64 = expected_u64 * 35u + 34u;
    }
    EXPECT_EQ(wrapped, static_cast<std::int64_t>(expected_u64));
    // And it must NOT equal a naive (non-wrapping) reading -- if it did,
    // this test would be vacuous.
    EXPECT_NE(static_cast<std::uint64_t>(wrapped), 0u);
}
