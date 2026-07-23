#pragma once

// B1.5 translator (Stage B design §2.6): campaign JSONL (the driver's per-run
// artifact, design §2.7 / PROTOCOL.md) -> the frozen binary schema
// (RunState / CombatState) that the diff harness reads.
//
// SCOPE (frozen by the B1.5 ledger block, docs/stage-b-tasks.md):
//   * Full field-disposition table enforced (PROTOCOL.md §3 stock catalog + §5
//     oracle block). Every JSON field is mapped to a schema field, on an
//     explicit ignore-list with a reason, an oracle-advisory field, or
//     deferred to a later task with storage -- and a field that is in NONE of
//     those lists FAILS the translation (design §2.6 "fail loudly"). This is
//     realized as a typed recursive walker: each JSON object type has a parser
//     that consumes exactly its known keys; any leftover key throws.
//   * Id mapping via the generated registry tables (sts::registry::*_from_game_id,
//     B2.2). A non-empty content id the registry does not know is drift -> throw.
//   * The §2.5 oracle fields that have schema storage TODAY land bit-for-bit:
//     the 7 run-scoped streams + mapRng -> RunState, the 5 floor-scoped streams
//     -> CombatState, cardBlizzRandomizer / blizzardPotionMod -> RunState. The
//     §2.5 items with no storage yet (neowRng, event-pity floats, purgeCost,
//     event/shrine/special lists, relic-pool orders, per-monster move history
//     beyond 3, potion-slot count, real map/boss/event/shop fields) are known
//     to the walker (so they do not trip the drift error) but are NOT written
//     to the schema -- they are DEFERRED to B4.3, which adds their storage and
//     upgrades this translator to emit them (schema-version bump lives there).
//
// BOUNDARY vs B1.6: this task does NOT introduce the v2 trace container
// (state_kind discriminator, RunState-in-container, SCHEMA_VERSION bump) or the
// RunState differ / oracle adapter -- those are B1.6. The translated RunState is
// produced in memory (and its oracle fields verified bit-for-bit by the test);
// only the CombatState snapshots are persisted, via the existing v1 trace
// writer (write_combat_trace), which is exactly "the trace files the diff
// harness already reads" (design §2.6).
//
// Dependency grant: this target links nlohmann/json PRIVATE (tools-only per the
// §2.6 grant); sts_engine links neither the translator nor nlohmann.

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/run_state.hpp"

namespace sts::translate {

// Translation options (design §2.6 / G4 gate tooling).
//
// `tolerate_unknown_ids` switches the translator from strict (default, B1.5
// contract: an unknown content id is a fatal drift) to an ACCOUNTING mode: an
// unknown content id (card/power/monster/relic/potion the registry does not
// know) is TALLIED per-id and translated to NONE instead of aborting, and the
// affected record's remaining fields are STILL field-checked. Unknown *fields*,
// unknown RNG stream names, and oracle-anchor mismatches remain fatal in both
// modes -- id tolerance loosens only the id join, never the fail-loud field
// discipline. This is what lets a real A20 campaign (whose captures carry
// content ids the skeleton registry lacks pre-B3, e.g. AscendersBane / Burning
// Blood / Cultist) be checked for "zero unknown-FIELD errors" at G4 while the
// expected unknown-id set is reported rather than swallowed.
struct TranslateOptions {
    bool tolerate_unknown_ids = false;
};

// Thrown on any drift the frozen policy makes fatal: an unmapped JSON field, an
// unknown content id, an unknown RNG stream name, or a failed sanity-anchor
// cross-check. `what()` carries a self-describing message (source + record
// index + JSON path).
class TranslateError : public std::runtime_error {
public:
    explicit TranslateError(std::string msg) : std::runtime_error(std::move(msg)) {}
};

// Per-field disposition tallies (design §2.6 fail-loud accounting). Summed over
// every JSON field consumed across all records of a run.
struct DispositionStats {
    uint64_t mapped = 0;    // written into a RunState/CombatState schema field
    uint64_t ignored = 0;   // I-disposition (presentation / plumbing / S2 scope)
    uint64_t oracle = 0;    // O-disposition (stock value advisory; oracle authoritative)
    uint64_t deferred = 0;  // known S field with no schema storage yet (B4.3/B4.x)
};

// One translated action record. `run` always carries the run-level translation
// (streams, deck, hp/gold/..., pity). `combat` is present iff the dump was in a
// COMBAT room (game_state.combat_state present); it carries the combat schema
// fields + the 5 floor-scoped streams.
struct TranslatedRecord {
    int seq = 0;
    std::string action_command;    // the command issued from this state (advisory)
    bool ready_for_command = true;
    bool in_combat = false;
    engine::RunState run{};
    engine::CombatState combat{};  // meaningful iff in_combat
};

// A whole translated run (one JSONL file).
struct TranslatedRun {
    int64_t seed = 0;              // header seed long
    std::string seed_string;      // header seed base-35 string
    std::string character;
    uint32_t schema_version = 0;  // header schema_version (artifact format)
    std::vector<TranslatedRecord> records;
    DispositionStats stats;
    int combat_record_count = 0;

    // Unknown-content-id tally, populated ONLY under
    // TranslateOptions::tolerate_unknown_ids. Key is "<domain>:<game_id>" (e.g.
    // "card:AscendersBane"); value is the number of occurrences across the run.
    // Empty in strict mode (an unknown id throws before it could be tallied).
    // `sorted()` iteration order (std::map) keeps the report deterministic.
    std::map<std::string, uint64_t> unknown_ids;
    uint64_t unknown_id_hits = 0;  // sum over unknown_ids values
};

// Translate a JSONL file from disk. Throws TranslateError on drift; throws
// std::runtime_error on I/O or JSON-parse failure.
[[nodiscard]] TranslatedRun translate_file(const std::string& jsonl_path);
[[nodiscard]] TranslatedRun translate_file(const std::string& jsonl_path,
                                           const TranslateOptions& opts);

// Translate JSONL lines already in memory (one JSON object per element).
// `source_name` is used only in error messages.
[[nodiscard]] TranslatedRun translate_lines(const std::vector<std::string>& lines,
                                            const std::string& source_name);
[[nodiscard]] TranslatedRun translate_lines(const std::vector<std::string>& lines,
                                            const std::string& source_name,
                                            const TranslateOptions& opts);

// Persist the run's CombatState snapshots as a v1 trace (the format the diff
// harness reads, design §8 / sts/diff/trace.hpp). Snapshots are written in
// record order with the seed stamped from the run header; per-record action
// bits are 0 (the campaign driver leaves sim_action_bits null -- action->bits
// resolution is B1.6/B4.4). A run with no combat records writes a 1-record
// trace of a value-init CombatState is NOT done -- returns false instead so a
// caller can tell "no combat" apart from an I/O failure.
//
// Returns true on success (and when there was at least one combat record).
[[nodiscard]] bool write_combat_trace(const std::string& path,
                                      const TranslatedRun& run);

}  // namespace sts::translate
