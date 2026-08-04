// T0.5 -- the twin-fixture export (training-plan §2.6a: "the same utility
// generates fixtures the training repo consumes").
//
// Three things are tested, and the third is the one that keeps the file honest
// as the engine moves:
//
//   1. ROUND TRIP. write -> read -> byte-compare, on the committed cases
//      themselves, so the container is exercised with real payloads rather than
//      a hand-made one.
//   2. REFUSAL. A fixture whose stamps do not match the current build must be
//      REFUSED with a named reason, not reinterpreted (the refuse-on-mismatch
//      discipline plan T1.2 puts on every trajectory loader).
//   3. REPLAY. Every committed case is rebuilt from its recipe and must
//      reproduce the stored PublicView byte for byte -- and so must its twin.
//      That is what makes the fixture a leak-gate artifact and not just a blob:
//      if an engine change silently altered what is public, this fails here,
//      in this repo, instead of in the training repo months later.

#include "sts/twin/twin_fixture.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "sts/engine/public_view.hpp"
#include "sts/engine/run_advance.hpp"

namespace sts::twin {
namespace {

std::string golden_path() {
    return std::string(STS_TWIN_FIXTURE_DIR) + "/twins_v1.bin";
}

std::string scratch_path(const char* name) {
    return std::string(STS_TWIN_SCRATCH) + "/" + name;
}

std::vector<unsigned char> read_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in),
                                      std::istreambuf_iterator<char>());
}

bool load_golden(TwinFixtureHeader& h, std::vector<TwinCase>& cases,
                 std::string& error) {
    return read_twin_fixture(golden_path(), h, cases, error);
}

TEST(TwinFixture, CommittedFileLoadsWithCurrentStamps) {
    TwinFixtureHeader h{};
    std::vector<TwinCase> cases;
    std::string error;
    ASSERT_TRUE(load_golden(h, cases, error))
        << error
        << "\nIf the engine layout or PublicView moved, regenerate with "
           "`gen_twin_fixtures` and commit the result -- do not hand-edit it.";

    EXPECT_EQ(h.format_version, kTwinFixtureFormat);
    EXPECT_EQ(h.engine_schema_version, engine::SCHEMA_VERSION);
    EXPECT_EQ(h.public_view_version, engine::PUBLIC_VIEW_VERSION);
    EXPECT_EQ(h.public_view_size, sizeof(engine::PublicView));
    EXPECT_EQ(h.run_controller_size, sizeof(engine::RunController));
    EXPECT_EQ(h.case_count, cases.size());
    EXPECT_GT(cases.size(), 0u);

    // The export exists so the training repo can test invariance ACROSS PHASES;
    // a fixture that is all combat would not do that.
    std::map<uint8_t, int> per_phase;
    for (const TwinCase& c : cases) ++per_phase[c.run_phase];
    EXPECT_GE(per_phase.size(), 8u)
        << "the committed fixture covers only " << per_phase.size()
        << " run phases";
}

TEST(TwinFixture, ReplayingEveryCommittedCaseReproducesItsStoredView) {
    TwinFixtureHeader h{};
    std::vector<TwinCase> cases;
    std::string error;
    ASSERT_TRUE(load_golden(h, cases, error)) << error;

    std::string report;
    const std::size_t failed = verify_twin_fixture(cases, report);
    EXPECT_EQ(failed, 0u) << report;
}

TEST(TwinFixture, WriteReadRoundTripIsByteIdentical) {
    TwinFixtureHeader h{};
    std::vector<TwinCase> cases;
    std::string error;
    ASSERT_TRUE(load_golden(h, cases, error)) << error;

    const std::string out = scratch_path("roundtrip.bin");
    ASSERT_TRUE(write_twin_fixture(out, cases, error)) << error;

    TwinFixtureHeader h2{};
    std::vector<TwinCase> back;
    ASSERT_TRUE(read_twin_fixture(out, h2, back, error)) << error;

    ASSERT_EQ(back.size(), cases.size());
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const TwinCase& a = cases[i];
        const TwinCase& b = back[i];
        EXPECT_EQ(a.run_seed, b.run_seed) << "case " << i;
        EXPECT_EQ(a.policy_seed, b.policy_seed) << "case " << i;
        EXPECT_EQ(a.twin_seed, b.twin_seed) << "case " << i;
        EXPECT_EQ(a.step_index, b.step_index) << "case " << i;
        EXPECT_EQ(a.ascension, b.ascension) << "case " << i;
        EXPECT_EQ(a.policy, b.policy) << "case " << i;
        EXPECT_EQ(a.run_phase, b.run_phase) << "case " << i;
        EXPECT_EQ(a.actions, b.actions) << "case " << i;
        EXPECT_EQ(std::memcmp(&a.view, &b.view, sizeof(engine::PublicView)), 0)
            << "case " << i << ": the PublicView payload did not round-trip";
    }

    // The file itself, not just the decoded content: a writer that is not a
    // function of the cases alone would show up here and nowhere else.
    EXPECT_EQ(read_bytes(out), read_bytes(golden_path()))
        << "re-writing the committed cases produced different bytes";
}

TEST(TwinFixture, LoaderRefusesAStampMismatchWithANamedReason) {
    TwinFixtureHeader h{};
    std::vector<TwinCase> cases;
    std::string error;
    ASSERT_TRUE(load_golden(h, cases, error)) << error;

    const std::string out = scratch_path("stale.bin");
    ASSERT_TRUE(write_twin_fixture(out, cases, error)) << error;

    // Corrupt the stamps IN PLACE, one at a time, and require a refusal each
    // time. The offsets are the header's declared layout.
    struct Poke {
        std::size_t offset;
        const char* what;
    };
    const Poke pokes[] = {
        {0, "magic"},
        {offsetof(TwinFixtureHeader, format_version), "format version"},
        {offsetof(TwinFixtureHeader, engine_schema_version), "schema version"},
        {offsetof(TwinFixtureHeader, public_view_version), "PublicView version"},
        {offsetof(TwinFixtureHeader, public_view_size), "PublicView size"},
        {offsetof(TwinFixtureHeader, run_controller_size),
         "RunController size"},
    };

    const std::vector<unsigned char> good = read_bytes(out);
    ASSERT_FALSE(good.empty());

    for (const Poke& p : pokes) {
        std::vector<unsigned char> bad = good;
        bad[p.offset] = static_cast<unsigned char>(bad[p.offset] ^ 0x5A);
        const std::string path = scratch_path("poked.bin");
        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(bad.data()),
                    static_cast<std::streamsize>(bad.size()));
        }
        TwinFixtureHeader hh{};
        std::vector<TwinCase> cc;
        std::string err;
        EXPECT_FALSE(read_twin_fixture(path, hh, cc, err))
            << "a fixture with a corrupted " << p.what
            << " was accepted -- refuse-on-mismatch is not holding";
        EXPECT_FALSE(err.empty()) << "refusal without a named reason";
    }
}

TEST(TwinFixture, LoaderRefusesATruncatedAndAnOverlongFile) {
    TwinFixtureHeader h{};
    std::vector<TwinCase> cases;
    std::string error;
    ASSERT_TRUE(load_golden(h, cases, error)) << error;
    const std::string out = scratch_path("length.bin");
    ASSERT_TRUE(write_twin_fixture(out, cases, error)) << error;
    const std::vector<unsigned char> good = read_bytes(out);

    const auto write_and_expect_refusal = [](const std::string& path,
                                             const std::vector<unsigned char>& b,
                                             const char* what) {
        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(b.data()),
                    static_cast<std::streamsize>(b.size()));
        }
        TwinFixtureHeader hh{};
        std::vector<TwinCase> cc;
        std::string err;
        EXPECT_FALSE(read_twin_fixture(path, hh, cc, err))
            << what << " was accepted";
    };

    std::vector<unsigned char> truncated(good.begin(),
                                         good.begin() + static_cast<long>(
                                             good.size() / 2));
    write_and_expect_refusal(scratch_path("truncated.bin"), truncated,
                             "a truncated fixture");

    std::vector<unsigned char> overlong = good;
    overlong.push_back(0x00);
    write_and_expect_refusal(scratch_path("overlong.bin"), overlong,
                             "a fixture with trailing bytes");
}

}  // namespace
}  // namespace sts::twin
