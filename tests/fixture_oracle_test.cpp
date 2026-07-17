// The ~20 scripted Jaw Worm fights, replayed through the real engine and diffed
// against the INDEPENDENT reference-simulator fixtures (design doc §9).
//
// For each checked-in fixture trace (tests/golden/combat_fixtures/*.trace, written
// by tools/fixture_gen/gen_combat_fixtures.cpp -- a from-scratch reimplementation
// of the frozen spec, NOT the production engine):
//   1. read_trace -> (run_seed, expected states[0..N], the N actions).
//   2. replay the SAME (run_seed, actions) through the real engine via the
//      replay_skeleton driver (combat_begin + advance()).
//   3. diff_states the expected vs the live-replayed state after EVERY action
//      (initial + after each of the N actions); assert an EMPTY DiffReport at
//      every step. On any diff, print the named field diffs + the action index so
//      a human sees exactly what diverged.
//
// The reference simulator and the engine are two independent implementations of
// the same spec, so a zero-diff across all fixtures is real differential evidence.
//
// STS_COMBAT_FIXTURE_DIR is injected by tests/CMakeLists.txt (absolute path to
// tests/golden/combat_fixtures) so the binary finds the fixtures regardless of
// ctest's CWD.

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

#include "sts/diff/differ.hpp"
#include "sts/diff/replay.hpp"
#include "sts/diff/trace.hpp"

#ifndef STS_COMBAT_FIXTURE_DIR
#error "STS_COMBAT_FIXTURE_DIR must be defined by the build (see tests/CMakeLists.txt)"
#endif

namespace {

using sts::engine::Action;
using sts::engine::CombatState;

// Enumerate every *.trace fixture, sorted for stable test ordering.
std::vector<std::string> fixture_files() {
    std::vector<std::string> paths;
    const std::filesystem::path dir(STS_COMBAT_FIXTURE_DIR);
    if (std::filesystem::exists(dir)) {
        for (const auto& e : std::filesystem::directory_iterator(dir)) {
            if (e.is_regular_file() && e.path().extension() == ".trace") {
                paths.push_back(e.path().string());
            }
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

// Replay one fixture through the real engine and diff every state.
void CheckFixture(const std::string& path) {
    sts::diff::TraceHeader header{};
    std::vector<sts::diff::TraceRecord> records;
    ASSERT_TRUE(sts::diff::read_trace(path, header, records))
        << "read_trace failed (schema/magic/state_size mismatch?) for " << path;
    ASSERT_GE(records.size(), 1u) << path;

    // Reconstruct the action list from the records (record[k>=1].action bits).
    std::vector<Action> actions;
    actions.reserve(records.size() - 1);
    for (std::size_t k = 1; k < records.size(); ++k) {
        Action a{};
        a.bits = records[k].action;
        actions.push_back(a);
    }

    // Live replay through combat_begin + advance() (the replay_skeleton driver).
    std::vector<CombatState> actual;
    sts::diff::replay_skeleton(header.seed, std::span<const Action>(actions), actual);

    ASSERT_EQ(actual.size(), records.size())
        << "replay produced a different number of states for " << path;

    for (std::size_t k = 0; k < records.size(); ++k) {
        const sts::diff::DiffReport rep =
            sts::diff::diff_states(records[k].state, actual[k]);
        EXPECT_TRUE(rep.empty())
            << "DIFF in " << std::filesystem::path(path).filename().string()
            << " at state index " << k << " (0 = initial, k = after action k):\n"
            << rep.to_string();
    }
}

TEST(FixtureOracle, AllFixturesReplayWithZeroDiffs) {
    const std::vector<std::string> files = fixture_files();
    ASSERT_FALSE(files.empty())
        << "no *.trace fixtures found under " STS_COMBAT_FIXTURE_DIR
        << " -- run tools/fixture_gen/gen_combat_fixtures to (re)generate them";
    for (const std::string& f : files) {
        SCOPED_TRACE(f);
        CheckFixture(f);
    }
}

// A visible count so a fixture accidentally lost from disk is caught (the
// generator emits 20 fights; keep this in step when adding/removing fixtures).
TEST(FixtureOracle, FixtureCountIsAtLeastTwenty) {
    EXPECT_GE(fixture_files().size(), 20u)
        << "expected >= 20 scripted-fight fixtures under " STS_COMBAT_FIXTURE_DIR;
}

}  // namespace
