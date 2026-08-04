#pragma once

// TWIN FIXTURES -- the T0.5 export the training repo consumes (training-plan
// §2.6a: "the same utility generates fixtures the training repo consumes";
// T1.6 gates policy-logit and search-statistic invariance on them).
//
// WHAT A FIXTURE IS, AND WHY IT IS NOT A STATE DUMP. A twin fixture case is a
// RECIPE plus a PAYLOAD:
//
//   recipe  : (run_seed, ascension, policy, policy_seed, action prefix,
//              twin_seed) -- everything needed to rebuild the TRUE state by
//              replay and its twin by `make_hidden_twin`.
//   payload : the `PublicView` both of them encode to, stored verbatim.
//
// It is a recipe because `RunController` CANNOT be written to disk as bytes:
// `MonsterLists` holds `std::string_view` encounter keys, so a controller's
// representation contains POINTERS whose values move with ASLR (the same reason
// the fuzz harness content-hashes a controller rather than byte-hashing it,
// fuzz_run.hpp). Replay is also the doctrine the training plan already commits
// to for restricted sidecars (plan §5: keyframes + action logs, reconstruction
// by replay), so the fixture format and the shard format agree.
//
// The payload is what makes the file self-checking: a reader rebuilds the state
// and must reproduce the stored view BYTE FOR BYTE. That catches an engine
// change the version stamps miss, and it is what `verify` below reports.
//
// FILE FORMAT (little-endian; producer and consumer are the same build family,
// exactly as the trace format's note says -- tools/diff_harness/.../trace.hpp):
//
//   TwinFixtureHeader (40 bytes, no padding):
//     char     magic[8]              = "STSTWIN1"
//     uint32_t format_version        = kTwinFixtureFormat
//     uint32_t engine_schema_version = engine::SCHEMA_VERSION
//     uint32_t public_view_version   = engine::PUBLIC_VIEW_VERSION
//     uint32_t public_view_size      = sizeof(engine::PublicView)
//     uint32_t run_controller_size   = sizeof(engine::RunController)
//     uint32_t case_count
//     uint64_t reserved              = 0
//
//   case[case_count], each:
//     int64_t  run_seed
//     uint64_t policy_seed
//     int64_t  twin_seed
//     uint32_t step_index            (how many actions the prefix holds)
//     uint32_t action_count          (== step_index; stored so the record is
//                                     self-delimiting without trusting it)
//     uint8_t  ascension
//     uint8_t  policy                (fuzz::PolicyKind, provenance only)
//     uint8_t  run_phase             (RunPhase of the rebuilt state)
//     uint8_t  pad[5]                = 0
//     uint32_t actions[action_count] (Action.bits, in order)
//     PublicView view                (the shared public view of both twins)
//
// The reader REFUSES a file whose magic, format version, schema version,
// PublicView version or either struct size does not match the current build --
// a controlled, diagnosable failure rather than a garbage load, and the same
// refuse-on-mismatch discipline the trajectory loaders use (plan T1.2).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "sts/engine/public_view.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/schema.hpp"

namespace sts::twin {

inline constexpr uint32_t kTwinFixtureFormat = 1;
inline constexpr char kTwinFixtureMagic[8] = {'S', 'T', 'S', 'T',
                                              'W', 'I', 'N', '1'};

struct TwinFixtureHeader {
    char magic[8];
    uint32_t format_version;
    uint32_t engine_schema_version;
    uint32_t public_view_version;
    uint32_t public_view_size;
    uint32_t run_controller_size;
    uint32_t case_count;
    uint64_t reserved;
};

static_assert(sizeof(TwinFixtureHeader) == 40,
              "TwinFixtureHeader must be 40 bytes with no padding");

struct TwinCase {
    int64_t run_seed = 0;
    uint64_t policy_seed = 0;
    int64_t twin_seed = 0;
    uint32_t step_index = 0;
    uint8_t ascension = 20;
    uint8_t policy = 0;
    uint8_t run_phase = 0;
    std::vector<uint32_t> actions;  // Action.bits prefix, length == step_index
    engine::PublicView view{};      // what both twins encode to
};

// Fill `header` with the stamps of the CURRENT build.
[[nodiscard]] TwinFixtureHeader current_header(uint32_t case_count) noexcept;

// Write / read. Both return false (with `error` set) on any refusal or I/O
// failure; the outputs are unspecified on failure.
[[nodiscard]] bool write_twin_fixture(const std::string& path,
                                      const std::vector<TwinCase>& cases,
                                      std::string& error);

[[nodiscard]] bool read_twin_fixture(const std::string& path,
                                     TwinFixtureHeader& header,
                                     std::vector<TwinCase>& cases,
                                     std::string& error);

// Rebuild one case: `run_begin(run_seed, ascension)` then the recorded action
// prefix, giving the TRUE state; `make_hidden_twin(truth, twin_seed)` gives its
// twin. Returns false only if the case is malformed.
[[nodiscard]] bool rebuild_twin_case(const TwinCase& c,
                                     engine::RunController& truth,
                                     engine::RunController& twin) noexcept;

// The self-check every consumer should run once: rebuild each case and require
// (a) the rebuilt phase to match, (b) the rebuilt TRUE state to encode to the
// stored view byte for byte, and (c) its twin to encode to the same bytes.
// Returns the number of cases that failed and appends one line per failure to
// `report`.
[[nodiscard]] std::size_t verify_twin_fixture(const std::vector<TwinCase>& cases,
                                              std::string& report);

}  // namespace sts::twin
