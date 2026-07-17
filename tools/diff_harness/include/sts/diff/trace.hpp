#pragma once

// Trajectory trace file format (design doc §8) -- the permanent Stage B
// artifact half of the diff harness (A6.1). A trace is the on-disk form of
// "(seed, action[]) -> state snapshot after every action" (design doc §9): it
// stores the whole trajectory of one fight so it can be replayed, diffed, or
// used as a fixture oracle (see oracle.hpp).
//
// FILE FORMAT (little-endian, single-platform offline tool -- this is a debug
// artifact, not a cross-platform wire format; producer and consumer are the
// same WSL x86-64 build):
//
//   TraceHeader (24 bytes, no padding):
//     char     magic[4]        = {'S','T','S','0'}   (design doc §8's 'STS0')
//     uint32_t schema_version  = SCHEMA_VERSION       (stamped; loaders REFUSE
//                                                      a mismatch -- hard check)
//     uint32_t state_size      = sizeof(CombatState)  (second safety check)
//     uint32_t record_count                           (number of records)
//     int64_t  seed                                   (the fight's run seed --
//                                                      an extension of §8's
//                                                      minimal container, needed
//                                                      because §9 defines a trace
//                                                      as "(seed, action[]) ->
//                                                      ..."; without it a trace
//                                                      is not self-replayable)
//
//   record[record_count], each:
//     CombatState state   (raw memcpy of sizeof(CombatState) bytes -- design
//                          doc §8: "Serialization is memcpy")
//     uint32_t    action  (the Action.bits that PRODUCED this state; 0 for the
//                          initial record -- see the record convention below)
//     uint32_t    aux     (bit 0 = terminal flag: the state's phase is
//                          COMBAT_OVER; bits 1..31 reserved 0. Design doc §8
//                          names "aux" without fixing contents; this is the
//                          documented choice -- a cheap terminal marker so a
//                          reader can find combat end without decoding the state)
//
// RECORD CONVENTION: records[0] is the INITIAL state (as returned by
// combat_begin, before any action) with action == 0; records[k] (k >= 1) is the
// state AFTER applying actions[k-1], with action == actions[k-1].bits. Hence a
// trace of N actions has N+1 records, and write_trace requires
// states.size() == actions.size() + 1 (states[0] == the initial state).

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::diff {

// On-disk header. Field order is chosen so the struct is naturally packed
// (4+4+4+4 then an 8-aligned int64) -> exactly 24 bytes, no padding, so writing
// it as raw bytes is deterministic.
struct TraceHeader {
    char magic[4];
    uint32_t schema_version;
    uint32_t state_size;
    uint32_t record_count;
    int64_t seed;
};

static_assert(sizeof(TraceHeader) == 24, "TraceHeader must be 24 bytes, no padding");

// One decoded trace record (in-memory; the file stores state/action/aux back to
// back without this struct's own layout mattering).
struct TraceRecord {
    engine::CombatState state;
    uint32_t action;  // Action.bits that produced `state` (0 for the initial record)
    uint32_t aux;     // bit 0 = terminal (phase == COMBAT_OVER)
};

// aux bit meanings.
inline constexpr uint32_t kAuxTerminal = 1u;  // bit 0

// Write a trace. `states` must have exactly `actions.size() + 1` entries, with
// states[0] the initial (pre-action) state and states[k] the state after
// actions[k-1] (see the RECORD CONVENTION above). Returns false on a bad
// argument shape or any I/O failure.
[[nodiscard]] bool write_trace(const std::string& path, int64_t seed,
                               std::span<const engine::Action> actions,
                               std::span<const engine::CombatState> states);

// Read a trace. On success fills `header_out` (including the stamped seed) and
// `records_out`. REFUSES (returns false, leaving outputs unspecified) a file
// whose magic, schema_version, or state_size does not match the current build --
// a controlled, clearly-diagnosable failure, never a crash or a garbage load.
[[nodiscard]] bool read_trace(const std::string& path, TraceHeader& header_out,
                              std::vector<TraceRecord>& records_out);

}  // namespace sts::diff
