#pragma once

// SEED PRE-SCANNING -- picking capture seeds on evidence instead of blindly.
//
// THE PROBLEM. The oracle capture campaigns walk `STS%05d` sequentially, so
// which content a campaign sees is whatever those seeds happen to contain.
// Rare targets -- one specific shrine, a treasure floor, a run that reaches the
// boss -- are then a lottery: a 200-seed campaign that wants "Match and Keep!"
// gets it when it gets it, and nobody knows before the game has been driven for
// hours whether the list contains it at all.
//
// THE OBSERVATION THAT MAKES A PRE-SCAN POSSIBLE. Event selection is a pure
// function of RunState: generate_event (src/engine/event_framework.cpp:359-395)
// takes a THROWAWAY copy of rs.event_rng, draws from it, removes the drawn id
// from the committed pool bitset, and records the firing in
// `rs.event_flags |= 1u << (id - 1)` (:392). So the simulator can answer "does
// this seed contain that shrine" in microseconds, and the capture campaign only
// ever has to be pointed at seeds already known to contain the target.
//
// WHY THE ANSWER IS PER (SEED, POLICY, POLICY_SEED) AND NOT PER SEED. What a
// run encounters depends on the path taken through the map, which is the
// policy's choice, not the seed's. The capture will be driven by a DIFFERENT
// policy than the scan, so a seed whose target was hit by exactly one of the
// scanned combinations is near-worthless: it says the target is reachable, not
// that it will be reached. That is why the qualifying rule is a HIT COUNT
// (`min_hit_count`) over combinations rather than a boolean -- a seed that
// yields the target under many different policies is a seed whose target is
// robust to the one policy the capture actually uses.
//
// WHAT IS REUSED, AND WHY NOTHING IS FORKED. The run loop is
// sts::fuzz::run_case (tools/fuzz): run_begin -> legal_actions ->
// enumerate_moves -> policy_pick -> advance, with the livelock and no-progress
// detectors that keep a case from burning its whole action budget on the
// reward screen's legal 2-cycle. This file adds only an observer
// (fuzz::StepObserver) that watches pass A go past. Re-implementing the loop
// here would have produced a second copy to keep in step with the engine, and
// the scan's whole value is that its verdict matches what a capture will see.
//
// DETERMINISM. Every stochastic decision in a scanned run comes from
// fuzz::PolicyRng, a private splitmix64 seeded from `policy_seed`
// (tools/fuzz/include/sts/fuzz/policy.hpp:38-62) -- no engine stream is
// touched. Combined with run_case's own pass-A/pass-B comparison, a ScanRow is
// a pure function of its ScanCase, and scanning the same case twice is
// byte-identical. `seed_scan --verify-determinism` asserts exactly that.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "sts/engine/run_advance.hpp"
#include "sts/fuzz/case_id.hpp"
#include "sts/fuzz/coverage.hpp"
#include "sts/fuzz/fuzz_run.hpp"
#include "sts/fuzz/policy.hpp"
#include "sts/registry/ids.hpp"

namespace sts::planner {

// --- Event naming ------------------------------------------------------------

// EventId <-> names. Two spellings per event, because the two sides of this
// tool speak different ones: the C++ enum symbol (`MATCH_AND_KEEP`) is what a
// caller reading engine code will type, and the game id (`Match and Keep!`) is
// what registry/events.yaml, the translator join key and the capture artifacts
// all carry. `--need-event` accepts either.
//
// WHY THE STRINGS ARE HERE AND NOT GENERATED. registry/events.yaml carries
// `game_id` for every row, but tools/registry_gen emits only the enum
// (ids.hpp) and the metadata table (event_table.hpp) -- no name strings reach
// C++ today, and that generator is not this task's to change. So this table is
// a hand copy, guarded the only way a hand copy can honestly be guarded: a
// static_assert that its length equals the generated kEventTable's, so the day
// an event row is added, this file fails to compile rather than silently
// reporting the new event as unknown. tests/registry_gen_test.cpp keeps its own
// hand table of the same strings for the same reason.
struct EventName {
    registry::EventId id;
    std::string_view symbol;   // generated enum symbol (registry/events.yaml `name`)
    std::string_view game_id;  // the game's event id string (`game_id`)
};

[[nodiscard]] const std::vector<EventName>& event_names();

// Case-insensitive match against either spelling. Returns false if unknown.
[[nodiscard]] bool event_id_from_name(std::string_view name,
                                      registry::EventId& out);

// The `game_id` for an id, or "" if the id is not in the table.
[[nodiscard]] std::string_view event_game_id(registry::EventId id);

// Decode RunState::event_flags -- bit (id-1) per event_framework.cpp:392, with
// the EventId 1..31 layout of event_framework.hpp:164-169.
[[nodiscard]] bool event_flag_set(uint32_t flags, registry::EventId id);
[[nodiscard]] std::vector<registry::EventId> decode_event_flags(uint32_t flags);
// '|'-joined game ids, in ascending id order; "" for no flags. This is the
// results file's `events` column, so the separator is deliberately not a
// comma, tab or space -- every one of those occurs inside a game id.
[[nodiscard]] std::string event_flags_text(uint32_t flags);

// --- Seed identity -----------------------------------------------------------

// A seed as both spellings. `sim_seed_int == seed_to_long(game_seed_string)`
// is the contract the capture bridge joins on: the driver's Python port is
// tools/oracle_bridge/driver/campaign_driver.py:199-206 and the C++ codec is
// include/sts/engine/seed_string.hpp (stage-a design §3.5, trap 6).
struct Seed {
    std::string text;   // "STS00100" -- what a capture seed list holds
    int64_t value = 0;  // seed_from_string(text) -- what run_begin takes
};

// --- Scanning ----------------------------------------------------------------

struct ScanCase {
    Seed seed;
    uint8_t ascension = 20;
    fuzz::PolicyKind policy = fuzz::PolicyKind::RANDOM;
    uint64_t policy_seed = 0;

    [[nodiscard]] fuzz::CaseId case_id() const;
};

// One scanned (seed, policy, policy_seed). Every field is derived from the
// case alone; see the determinism note in the file header.
struct ScanRow {
    Seed seed;
    uint8_t ascension = 20;
    fuzz::PolicyKind policy = fuzz::PolicyKind::RANDOM;
    uint64_t policy_seed = 0;

    fuzz::EndReason end_reason = fuzz::EndReason::RUN_OVER;
    uint32_t actions = 0;
    uint32_t max_floor = 0;    // highest RunState::floor observed
    uint32_t event_flags = 0;  // terminal RunState::event_flags
    bool treasure_entered = false;  // RunPhase::TREASURE_ROOM was ever live
    bool boss_reached = false;      // RoomType::Boss was ever the current room
    uint64_t final_hash = 0;
    // run_case's own pass-A/pass-B/pass-C verdict. "none" on a clean case; any
    // other value is a fuzz finding that happens to have surfaced during a
    // scan, and is reported rather than swallowed.
    std::string fail_kind = "none";
};

struct ScanLimits {
    uint32_t max_actions = 4000;
    uint32_t revisit_limit = 64;
};

// Run one case to terminal and summarise it. Deterministic given `c`.
[[nodiscard]] ScanRow scan_case(const ScanCase& c, const ScanLimits& lim = {});

// --- Filtering ---------------------------------------------------------------

struct Filter {
    // ALL listed events must have fired in the same run (AND, not OR): the
    // caller asking for two targets wants one capture that contains both.
    std::vector<registry::EventId> need_events;
    bool need_treasure = false;
    bool need_boss = false;
    uint32_t min_floor = 0;

    // A seed qualifies only when at least this many of its scanned rows hit.
    // 1 means "reachable"; >= 2 means "reached by more than one policy /
    // policy seed", which is the property a capture on a THIRD policy needs.
    uint32_t min_hit_count = 1;

    [[nodiscard]] bool empty() const;
};

// Does this single row satisfy the per-row half of the filter?
[[nodiscard]] bool row_hits(const ScanRow& row, const Filter& f);

// The seed-level half. `rows` are the scanned combinations for ONE seed.
[[nodiscard]] uint32_t count_hits(const std::vector<ScanRow>& rows, const Filter& f);
[[nodiscard]] bool seed_qualifies(const std::vector<ScanRow>& rows, const Filter& f);

// --- Output ------------------------------------------------------------------

enum class Format : uint8_t { TSV = 0, JSONL = 1 };

[[nodiscard]] bool format_from_name(std::string_view name, Format& out);
[[nodiscard]] std::string_view tsv_header();
[[nodiscard]] std::string row_to_tsv(const ScanRow& row);
[[nodiscard]] std::string row_to_jsonl(const ScanRow& row);
[[nodiscard]] std::string row_to_text(const ScanRow& row, Format f);

// JSON string escaping for the few free-text fields (event game ids contain
// `'` and `!`, never a quote or backslash today -- escaped anyway so the
// writer stays correct if one ever does).
[[nodiscard]] std::string json_escape(std::string_view s);

// --- Aggregate report --------------------------------------------------------

inline constexpr int kFloorHistogramBuckets = 12;  // 0..10, then "11+"

struct ScanSummary {
    uint64_t rows = 0;
    uint64_t seeds = 0;
    uint64_t treasure_rows = 0;
    uint64_t boss_rows = 0;
    uint64_t event_rows[32]{};  // index by EventId (1..31); [0] unused
    uint64_t floor_hist[kFloorHistogramBuckets]{};
    uint64_t end_reason[static_cast<int>(fuzz::EndReason::COUNT)]{};
    uint64_t failures = 0;
    uint32_t max_floor = 0;
    uint64_t actions = 0;

    void add(const ScanRow& row);
    [[nodiscard]] std::string text() const;
};

}  // namespace sts::planner
