#pragma once

// Trajectory trace file format (design doc §8) -- the permanent half of the
// diff harness. A trace is the on-disk form of
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
//                                                      needed because §9 defines
//                                                      a trace as "(seed,
//                                                      action[]) -> ..."; without
//                                                      it a trace is not
//                                                      self-replayable)
//
//   record[record_count], each:
//     CombatState state   (raw memcpy of sizeof(CombatState) bytes -- design
//                          doc §8: "Serialization is memcpy")
//     uint32_t    action  (the Action.bits that PRODUCED this state; 0 for the
//                          initial record -- see the record convention below)
//     uint32_t    aux     (design doc §8's "aux" field. bit 0 = terminal flag:
//                          the state's phase is COMBAT_OVER; bits 1..31 reserved
//                          0 -- a cheap terminal marker so a reader can find
//                          combat end without decoding the state)
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
#include "sts/engine/run_state.hpp"
#include "sts/engine/schema.hpp"
#include "sts/engine/types.hpp"

namespace sts::diff {

// On-disk container-format versions stamped into a trace header's
// `schema_version` field. v1 is the Stage-A CombatState-only container; v2 is
// the B1.6 mixed container with a per-record `state_kind` (see below). These are
// the ON-DISK format tags and are deliberately DECOUPLED from
// engine::SCHEMA_VERSION so the v1 writer/reader keep their exact Stage-A
// behavior (the 20 frozen v1 fixtures load unchanged) while SCHEMA_VERSION
// advances to the current format. kTraceFormatV2 tracks engine::SCHEMA_VERSION:
// B4.3 grew sizeof(RunState) and bumped SCHEMA_VERSION 2->3, so kTraceFormatV2
// follows to 3. The v2 CONTAINER format (state_kind + both struct sizes in the
// header) is unchanged; a stale-sized RunState trace is refused both by the
// stamped version and by the header's run_state_size check. No v2/RUN goldens
// are committed, so nothing on disk needs regeneration.
inline constexpr uint32_t kTraceFormatV1 = 1;
inline constexpr uint32_t kTraceFormatV2 = 3;
static_assert(kTraceFormatV2 == engine::SCHEMA_VERSION,
              "v2 trace format tag must equal the current engine::SCHEMA_VERSION");

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

// Read a v1 trace. On success fills `header_out` (including the stamped seed) and
// `records_out`. REFUSES (returns false, leaving outputs unspecified) a file
// whose magic, schema_version (must be kTraceFormatV1), or state_size does not
// match the current build -- a controlled, clearly-diagnosable failure, never a
// crash or a garbage load. This is the exact Stage-A reader; it accepts ONLY v1
// files (the 20 frozen combat fixtures). Read v2 (or v1-via-compat) with
// read_trace_v2 below.
[[nodiscard]] bool read_trace(const std::string& path, TraceHeader& header_out,
                              std::vector<TraceRecord>& records_out);

// =============================================================================
// Trace format v2 (design §3.3 run-level traces)
// =============================================================================
//
// v2 adds a per-record `state_kind` discriminator so a single container holds
// both CombatState and RunState records (a run header record + interleaved
// combat records). SCHEMA_VERSION bumps 1->2; loaders still refuse mismatches,
// with an explicit COMPATIBILITY READ of v1 files (every v1 record is a COMBAT
// record) so the frozen v1 fixtures load without regeneration.
//
// FILE FORMAT v2 (little-endian, same single-platform-offline caveat as v1):
//
//   TraceHeaderV2 (32 bytes, no padding):
//     char     magic[4]          = {'S','T','S','0'}
//     uint32_t schema_version    = kTraceFormatV2 (=3 as of B4.3)
//     uint32_t combat_state_size = sizeof(CombatState)   (refusal check)
//     uint32_t run_state_size    = sizeof(RunState)      (refusal check)
//     uint32_t record_count
//     uint32_t reserved          = 0                     (explicit 8-byte align)
//     int64_t  seed
//
//   record[record_count], each:
//     uint8_t  state_kind   (0 = COMBAT, 1 = RUN)
//     <state>  COMBAT -> sizeof(CombatState) bytes; RUN -> sizeof(RunState) bytes
//     uint32_t action       (Action.bits that produced this state; 0 for the
//                            initial record and for translated records whose
//                            sim_action_bits is null)
//     uint32_t aux          (bit 0 = terminal, as v1; run records carry 0)
//
// RECORD CONVENTION (as v1): records[0] is the initial state; records[k] is the
// state after k actions.

// Which struct a v2 record carries.
enum class StateKind : uint8_t {
    COMBAT = 0,
    RUN = 1,
};

// On-disk v2 header. Naturally packed: 4+4+4+4+4+4 = 24, then an 8-aligned
// int64 -> exactly 32 bytes, no padding, deterministic as raw bytes.
struct TraceHeaderV2 {
    char magic[4];
    uint32_t schema_version;    // kTraceFormatV2, or kTraceFormatV1 when read via compat
    uint32_t combat_state_size;
    uint32_t run_state_size;    // 0 when read via v1 compat (v1 had no run size)
    uint32_t record_count;
    uint32_t reserved;          // explicit padding / future use; written 0
    int64_t seed;
};

static_assert(sizeof(TraceHeaderV2) == 32, "TraceHeaderV2 must be 32 bytes, no padding");

// One decoded v2 record. Exactly one of `combat` / `run` is meaningful,
// selected by `kind`; the other stays value-init (harmless for a POD debug
// container). The file stores only the selected struct's bytes.
struct TraceRecordV2 {
    StateKind kind = StateKind::COMBAT;
    engine::CombatState combat{};  // meaningful iff kind == COMBAT
    engine::RunState run{};        // meaningful iff kind == RUN
    uint32_t action = 0;           // Action.bits that produced this state (0 = initial)
    uint32_t aux = 0;              // bit 0 = terminal (COMBAT_OVER); RUN carries 0
};

// Write a v2 trace. Each record is serialized with its `state_kind` byte, then
// the selected struct's bytes, then action+aux. Returns false on record count
// overflow or any I/O failure. (aux is taken verbatim from each record; the
// caller sets it -- e.g. kAuxTerminal for a COMBAT_OVER combat record.)
[[nodiscard]] bool write_trace_v2(const std::string& path, int64_t seed,
                                  std::span<const TraceRecordV2> records);

// Read a v2 trace, OR a v1 trace via a compatibility read (schema_version ==
// kTraceFormatV1 -> every record is decoded as a COMBAT TraceRecordV2, and
// header_out.run_state_size is reported 0). REFUSES (returns false) on: bad
// magic; a schema_version that is neither v1 nor v2; a state-size field that
// disagrees with the current build (combat size for v1/v2, run size for v2); an
// unknown per-record kind; or a short/truncated read. This is the loader the
// CommunicationModOracleAdapter uses, and the mechanism by which the frozen v1
// fixtures keep loading after the bump.
[[nodiscard]] bool read_trace_v2(const std::string& path, TraceHeaderV2& header_out,
                                 std::vector<TraceRecordV2>& records_out);

}  // namespace sts::diff
