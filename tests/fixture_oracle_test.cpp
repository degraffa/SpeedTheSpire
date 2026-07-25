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
#include <cstdint>
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

// --- Terminal-outcome coverage ----------------------------------------------
//
// The corpus is meant to cover BOTH ways a fight can end. Nothing above checks
// that it does, and the gap is not theoretical: fixt16_r29_monster_death spent a
// long time named for monster death while leaving the monster alive on 17/43.
// Its final PLAY named hand slot 3 of a three-card hand, `queue_card_play`
// refused the index, `advance()` no-opped the action -- and the reference
// simulator no-opped it too, so AllFixturesReplayWithZeroDiffs stayed green.
//
// That is the shape of defect worth pinning. A zero-diff check compares two
// implementations against each other; it is silent whenever they AGREE, and two
// implementations agree readily about an action that does nothing. The claim
// "this corpus exercises monster death" lived only in a file name and a row of a
// markdown table, and both went stale without anything turning red.
//
// So the guarantee is asserted here, and asserted from the REPLAYED TERMINAL
// STATE rather than from any name: a fixture counts as covering monster death
// only when the real engine, driven through that fixture's own recorded actions,
// actually finishes at COMBAT_OVER with every monster at 0 HP. Renaming a file
// cannot satisfy this test, and neutering a script cannot pass it.
//
// The two classifications are deliberately independent rather than a chain of
// else-ifs: "ended with the monster dead" and "ended with the player dead" are
// separate questions about the same final state, and collapsing them would
// quietly stop checking one if a fixture ever satisfied both.

std::string join_names(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& n : names) {
        if (!out.empty()) {
            out += ", ";
        }
        out += n;
    }
    return out.empty() ? std::string("<none>") : out;
}

struct Terminal {
    bool combat_over = false;
    bool monster_dead = false;  // COMBAT_OVER and every monster at 0 HP
    bool player_dead = false;   // COMBAT_OVER and the player at 0 HP
};

// Replay one fixture through the real engine and report where it ended. Uses
// fatal assertions, so it returns void and fills `out` (gtest's ASSERT_ rule).
void ReplayTerminal(const std::string& path, Terminal& out) {
    sts::diff::TraceHeader header{};
    std::vector<sts::diff::TraceRecord> records;
    ASSERT_TRUE(sts::diff::read_trace(path, header, records)) << path;
    ASSERT_GE(records.size(), 1u) << path;

    std::vector<Action> actions;
    actions.reserve(records.size() - 1);
    for (std::size_t k = 1; k < records.size(); ++k) {
        Action a{};
        a.bits = records[k].action;
        actions.push_back(a);
    }

    std::vector<CombatState> states;
    sts::diff::replay_skeleton(header.seed, std::span<const Action>(actions), states);
    ASSERT_FALSE(states.empty()) << path;

    const CombatState& end = states.back();
    out.combat_over = end.phase == static_cast<std::uint8_t>(
                                       sts::engine::CombatPhase::COMBAT_OVER);
    if (!out.combat_over) {
        return;  // mid-combat by design: most fixtures stop at a chosen state
    }
    out.player_dead = end.player_hp <= 0;
    // monster_count == 0 would make "every monster is dead" vacuously true, so
    // require that there was a monster there to die.
    bool every_monster_dead = end.monster_count > 0;
    for (std::uint8_t m = 0; m < end.monster_count; ++m) {
        if (end.monsters[m].hp > 0) {
            every_monster_dead = false;
        }
    }
    out.monster_dead = every_monster_dead;
}

TEST(FixtureOracle, CorpusCoversBothTerminalOutcomes) {
    const std::vector<std::string> files = fixture_files();
    ASSERT_FALSE(files.empty())
        << "no *.trace fixtures found under " STS_COMBAT_FIXTURE_DIR;

    std::vector<std::string> monster_death, player_death, mid_combat;

    for (const std::string& path : files) {
        SCOPED_TRACE(path);
        Terminal t;
        ASSERT_NO_FATAL_FAILURE(ReplayTerminal(path, t));

        const std::string name = std::filesystem::path(path).filename().string();
        if (!t.combat_over) {
            mid_combat.push_back(name);
        }
        if (t.monster_dead) {
            monster_death.push_back(name);
        }
        if (t.player_dead) {
            player_death.push_back(name);
        }
    }

    const std::string inventory =
        "\n  ended with the monster dead: " + join_names(monster_death) +
        "\n  ended with the player dead:  " + join_names(player_death) +
        "\n  still mid-combat (fine -- most are, by design): " +
        join_names(mid_combat) +
        "\n"
        "\n  These come from the state the ENGINE reaches when replayed through"
        "\n  each fixture's own recorded actions. A file named *_monster_death"
        "\n  contributes nothing here unless the fight really ends that way.\n";

    EXPECT_FALSE(monster_death.empty())
        << "no fixture ends with the monster dead, so the corpus never exercises"
           " the monster-death path through advance()/pump.\n"
           "  Note that a fixture whose last action is inert reaches no terminal"
           " state while still looking correct: the reference simulator no-ops it"
           " too, so the zero-diff check above passes. Re-author the intended"
           " fixture against 'gen_combat_fixtures --dump <name>' until its final"
           " state is COMBAT_OVER with the monster at 0 HP -- and keep the killing"
           " blow a single-effect card (Strike), or the pump halts with that card's"
           " later effect still queued."
        << inventory;

    EXPECT_FALSE(player_death.empty())
        << "no fixture ends with the player dead, so the corpus never exercises"
           " the player-death path: the pump halting at COMBAT_OVER during the"
           " MONSTER's turn, with start-of-turn never running.\n"
           "  Arrange the lethal blow to be a single-effect move (Chomp), or the"
           " monster's trailing queued effects are left unapplied and the fixture"
           " diffs."
        << inventory;
}

}  // namespace
