#pragma once

// Oracle adapter interface (design doc §8): fixture-file impl now,
// CommunicationMod impl later. The
// oracle is the ONE provisional piece of the diff harness: it supplies the
// "expected" CombatState the differ compares the engine's "actual" against. In
// M1 that comes from checked-in fixture files (hand-derived / captured expected
// trajectories); in Stage B a live CommunicationMod bridge replaces it WITHOUT
// touching the differ or trace format (design doc §9).
//
// Unlike engine code (which is data-oriented and avoids virtual dispatch on the
// hot path), the oracle is offline test scaffolding, so a small abstract base
// class is the right tool: implementations are swapped by a test harness, never
// called in advance()'s loop.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/types.hpp"

#include "sts/diff/trace.hpp"

namespace sts::diff {

// Abstract oracle: given a seed and an action prefix, produce the expected
// CombatState after applying that prefix.
class OracleAdapter {
public:
    virtual ~OracleAdapter() = default;

    // Fill `out` with the oracle's expected state after applying `action_prefix`
    // (in order) from the fight identified by `seed`. Returns false if the oracle
    // has no data for this exact (seed, prefix) -- e.g. a fixture file only
    // covers specific scripted sequences, so an unknown seed or a prefix that
    // diverges from / runs past the recorded actions yields no answer.
    [[nodiscard]] virtual bool query(int64_t seed,
                                     std::span<const engine::Action> action_prefix,
                                     engine::CombatState& out) const = 0;

    // Run-level query (design §3.3: the oracle serves BOTH state kinds). Fill
    // `out` with the expected RunState after `action_prefix`. Default returns
    // false: an oracle that carries no run layer (the v1 FixtureFileOracleAdapter)
    // simply has no run answer. The Stage-B CommunicationModOracleAdapter, whose
    // translated v2 traces carry RunState records, overrides this.
    [[nodiscard]] virtual bool query_run(int64_t /*seed*/,
                                         std::span<const engine::Action> /*action_prefix*/,
                                         engine::RunState& /*out*/) const {
        return false;
    }
};

// Fixture-file oracle: answers query() from checked-in trace files (the fixture
// file format IS the trace format, trace.hpp -- so the fixture generator produces
// fixtures with write_trace and this consumes them; no separate format to
// maintain). Load one
// trace per scripted fight; each provides the expected state at every prefix
// length of its own action sequence.
class FixtureFileOracleAdapter final : public OracleAdapter {
public:
    // Load a fixture (== a trace file). Returns false if the file cannot be read
    // or is rejected by read_trace (bad magic / schema mismatch). Multiple
    // fixtures (different seeds) may be loaded into one adapter.
    [[nodiscard]] bool load_fixture(const std::string& path);

    // Number of loaded fixtures (one per scripted fight).
    [[nodiscard]] std::size_t fixture_count() const noexcept { return fixtures_.size(); }

    // OracleAdapter: look up the fixture whose seed matches, verify `action_prefix`
    // is a prefix of that fixture's recorded action sequence, and return the
    // recorded state after that prefix. False if no seed match, the prefix is
    // longer than the fixture, or the prefix diverges from the recorded actions.
    [[nodiscard]] bool query(int64_t seed,
                             std::span<const engine::Action> action_prefix,
                             engine::CombatState& out) const override;

private:
    struct Fixture {
        int64_t seed;
        std::vector<TraceRecord> records;  // records[k] = state after k actions
    };
    std::vector<Fixture> fixtures_;
};

// Stage-B live oracle (B1.6), FILE-BASED over TRANSLATED campaign traces
// (design §2.1 offline/batch, §3.3). The A6.1 seam anticipated a
// CommunicationModOracleAdapter that drives a live game; the frozen Stage-B
// architecture instead runs the game offline (the campaign driver produces JSONL
// artifacts, the translator converts them to v2 trace files), and this adapter
// answers query() / query_run() from those translated v2 traces -- both state
// kinds -- WITHOUT touching the differ or the v1 trace format. Keeping it
// file-based (not a live child process) is a deliberate §2.1 choice: the diff
// harness stays a pure WSL/CMake target with no game-process dependency.
//
// Grant note: this adapter reads pre-translated BINARY v2 traces
// (read_trace_v2). It never parses JSON, so it needs no nlohmann and stays on
// the engine-free diff_harness side of the §2.6 tools-only grant; the
// JSON->binary translation is the separate oracle_translator target.
class CommunicationModOracleAdapter final : public OracleAdapter {
public:
    // Load a translated campaign trace (a v2 trace file, or a v1 file via the
    // read_trace_v2 compatibility read). Returns false if the file cannot be read
    // or is refused (bad magic / unknown version / struct-size mismatch).
    // Multiple runs (different seeds) may be loaded into one adapter.
    [[nodiscard]] bool load_run_trace(const std::string& path);

    // Number of loaded runs (one per translated campaign file).
    [[nodiscard]] std::size_t run_count() const noexcept { return runs_.size(); }

    // Combat query: look up the run whose seed matches, verify `action_prefix`
    // is a prefix of its recorded action sequence, and return the recorded
    // CombatState after that prefix. False if no seed match, the prefix is longer
    // than the run, the prefix diverges from the recorded actions, or the record
    // at that prefix length is not a COMBAT record.
    [[nodiscard]] bool query(int64_t seed,
                             std::span<const engine::Action> action_prefix,
                             engine::CombatState& out) const override;

    // Run query: same prefix matching, but returns the recorded RunState and
    // requires the record at that prefix length to be a RUN record.
    [[nodiscard]] bool query_run(int64_t seed,
                                 std::span<const engine::Action> action_prefix,
                                 engine::RunState& out) const override;

private:
    struct Run {
        int64_t seed;
        std::vector<TraceRecordV2> records;  // records[k] = state after k actions
    };
    std::vector<Run> runs_;

    // Shared prefix resolution: find the run, validate the prefix, and return a
    // pointer to records[prefix.size()] (or nullptr on any mismatch).
    [[nodiscard]] const TraceRecordV2* resolve(
        int64_t seed, std::span<const engine::Action> action_prefix) const;
};

}  // namespace sts::diff
