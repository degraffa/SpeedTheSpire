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
//   * The §2.5 oracle fields that have schema storage land bit-for-bit: the 7
//     run-scoped streams + mapRng + (B4.3) neowRng -> RunState, the 5 floor-
//     scoped streams -> CombatState, cardBlizzRandomizer / blizzardPotionMod
//     -> RunState. B4.3 (schema v3) additively grew RunState and UN-DEFERRED the
//     now-representable §2.5 items: neowRng (14th stream), the 3 event-pity
//     floats, purgeCost, and the potion-slot count now map. Of the id-LIST items,
//     the five relic-pool orders now map into relic_pools[5] / relic_pool_count:
//     they were blocked not on storage but on a COMPLETE relics.yaml, because
//     join_relic is fail-loud and ONE unregistered game_id in any of the five
//     arrays aborts the whole translation -- the boss and event-special tiers
//     were the last rows missing. B4.10 likewise un-deferred the
//     event/shrine/special remaining-list membership bitsets once events.yaml
//     supplied all 31 identities in canonical Java list order. Translation
//     validates each captured list as a removal-only subsequence before
//     collapsing it to bits, so duplicates/order drift fail loudly. Real
//     map/boss/event-shop-flag fields and per-monster move history beyond 3
//     remain deferred to their owning run-layer tasks. Deferred keys are still
//     STRUCTURALLY consumed (a new/renamed oracle key still trips the drift
//     error).
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
#include "sts/translate/combat_vitals.hpp"

namespace sts::translate {

// Translation options (design §2.6 / G4 gate tooling).
//
// `tolerate_unknown_ids` switches the translator from strict (default, B1.5
// contract: an unknown content id is a fatal drift) to an ACCOUNTING mode: an
// unknown content id (card/power/monster/relic/potion/event the registry does not
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
    // The index-normalised combat-vitals projection of the same dump
    // (combat_vitals.hpp), filled by the same walk that builds `combat`;
    // meaningful iff in_combat. `replay_run_diff --vitals` compares it against
    // `vitals_from_combat_state(<the live sim>)` at every in-combat record.
    CombatVitals vitals{};

    // `oracle.playtime` -- CardCrawlGame.playtime, wall-clock seconds
    // (s2-design §5 trap 5). NOT RunState: it is not save-parity state the
    // differ compares, and the disposition stays `oracle`. It is surfaced here
    // because it is an INPUT one rule needs -- SecretPortal's getShrine gate
    // (AbstractDungeon.java:1929-1933) -- and a missing SecretPortal shortens
    // getShrine's `tmp`, moving the drawn INDEX, so an Act-3 shrine draw past
    // 800 s cannot be replayed without it (S2.43, 2026-08-27).
    // `has_playtime` is false for pre-2026-08-26 captures, whose consumers
    // keep the engine's 0.0f (the trap-5 pin).
    float playtime = 0.0f;
    bool has_playtime = false;

    // S3.21 (a): true when THIS record's oracle block carried the three
    // `Settings.has*Key` booleans, i.e. it was captured by the 2026-09-03 jar
    // or later. `run.keys` is written only then; on an older capture it stays
    // a value-init 0, which is an absence of claim rather than a claim of "no
    // keys". The replay differ compares `keys` UNCONDITIONALLY (the
    // pre-redeploy corpora are zero-diff under that comparison because they
    // never claim a key), so nothing gates on this today -- it is the
    // artifact-level fact of which contract version a record was captured
    // under, and a triage reader's first question when `keys` ever REDs.
    bool has_keys = false;
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

    // The post-victory ending tail: an A20 double-boss victory walks the
    // "Spire Heart" dialog (four clicks, then the observed terminal) BEFORE
    // its victory terminal record.
    //
    // S2.43 COUNTED AND DROPPED these records, because "Spire Heart" had no
    // recognised event id and translating one would have aborted the whole
    // run. S3.21 gives the id its recognition (a non-pool sentinel, like
    // Neow's) and the records are now TRANSLATED like any other: they appear
    // in `records`, in order, at `first_post_victory_ending_record`. The
    // count survives as a labelled tally so the replay differ's summary can
    // say how many of them it actually reached and compared -- the tail is
    // still a distinct structural region, and how far into it a replay gets is
    // exactly the thing the Act-3-terminal work (S3.31) moves.
    //
    // Both fields are populated ONLY when the artifact's own terminal record
    // says victory; in any other artifact an unknown event id still aborts
    // loudly. `first_post_victory_ending_record` is -1 when there is no tail.
    int post_victory_ending_records = 0;
    int first_post_victory_ending_record = -1;
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
