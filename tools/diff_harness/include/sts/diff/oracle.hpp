#pragma once

// Oracle adapter interface (design doc §8 / ledger A6.1: "oracle adapter
// interface -- fixture-file impl now, CommunicationMod impl in Stage B"). The
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
};

// Fixture-file oracle: answers query() from checked-in trace files (the fixture
// file format IS the trace format, trace.hpp -- so A6.2 produces fixtures with
// write_trace and this consumes them; no separate format to maintain). Load one
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

// STAGE B EXTENSION POINT (out of scope for A6.1 -- documented seam only, like
// A4.1's SHUFFLE_IN/ROLL_MOVE stubs). A live oracle plugs in here as another
// OracleAdapter subclass without changing the differ or trace format:
//
//   class CommunicationModOracleAdapter final : public OracleAdapter {
//       // query() drives a running Slay the Spire + CommunicationMod process
//       // to (seed, prefix) and reads back its state as a CombatState.
//       bool query(int64_t, std::span<const engine::Action>,
//                  engine::CombatState&) const override;
//   };
//
// It is NOT implemented in A6.1 (Stage B, tools/oracle_bridge/).

}  // namespace sts::diff
