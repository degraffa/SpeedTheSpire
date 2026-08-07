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
//
// ---------------------------------------------------------------------------
// S2.42 -- PER-ACT DEPTH, AND WHY THE COHORT ARTIFACT IS A TRIPLE
// ---------------------------------------------------------------------------
//
// The S1 vocabulary above is act-agnostic on purpose: nothing past Act 1
// existed. `boss_reached` is one bool, there is no boss-KILL observation at
// all, and the seed list holds seed strings. Every design 6 S2-G2 depth bar is
// stated PER ACT and several are stated as KILLS, so S2.42 adds:
//
//   * `max_act`, `boss_reached_acts`, `boss_killed_acts` (bitmasks, bit act-1),
//     `victory`, and `boss_ids[]` -- the act's boss ENCOUNTER identity, which
//     is what "over >= 2 distinct first-boss identities" (G2-3) filters on.
//   * `--cohort-list`, a third output file holding (seed, policy, policy_seed)
//     TRIPLES rather than seeds.
//
// THE BOSS-KILL PROBE IS EXACT, NOT INFERRED. The boss chest is entered ONLY
// through the boss reward's proceed (ProceedButton.goToTreasureRoom, a full
// room transition), and Acts 1 and 2 both end in one, while Act 3's boss opens
// no reward screen and no chest at all (run_advance.hpp:70-76). So:
//
//     act-N boss KILLED (N in 1,2)  <=>  RunPhase::BOSS_TREASURE seen at act N
//     act-3 boss KILLED             <=>  run_is_victory(rc)
//     act-N boss REACHED            <=>  RoomType::Boss seen at act N
//
// The PHASE is the probe rather than RoomType::TreasureBoss (which is never
// written into a grid node and is only the resolved room type while the chest
// is up, map_rooms.hpp:85-92), because the phase is what the fuzz MoveCat 28-31
// buckets key off and the two instruments should agree. The live capture driver
// uses the same pair of probes on the protocol dump
// (campaign_driver.py `_observe_reach`), deliberately.
//
// `boss_reached` is KEPT and is exactly `boss_reached_acts != 0`. Redefining it
// would silently change every `--need-boss` filter already in someone's shell
// history; the new columns are APPENDED after `fail_kind` so a naive `cut -f10`
// over an old script still selects the same column.
//
// WHY A TRIPLE, AND WHY `--min-hit-count` INVERTS FOR DEPTH. The hit-count rule
// above exists because the capture is driven by a DIFFERENT policy from the
// scan, so a one-hit seed is near-worthless. A DEPTH cohort is the opposite
// case: the point of design 6's sanction ("the sim pre-scan chooses (seed,
// policy, policy-seed) triples whose scripted line reaches the target") is that
// a deep line is FRAGILE -- the property is not "this seed can be won" but
// "this exact line wins this seed". A one-hit triple is therefore a perfectly
// good cohort member, and raising --min-hit-count would throw most of an Act-3
// cohort away. See the README section, which says so where a reader will meet
// it.
//
// AND THE HONEST CAVEAT, WHICH THE REPORT MUST CARRY. `fuzz::PolicyKind` (the
// sim's five) and the driver's policy names (`random-legal` / `greedy` /
// `script` / `external`+config) are DIFFERENT FAMILIES. A triple naming
// `greedy_damage` is a SIM policy; the oracle campaign cannot literally run it.
// The triple selects a SEED that a scripted line of that shape reaches depth
// on, and the policy/policy_seed columns are provenance for that claim -- they
// are not an executable instruction to the capture. S2.42 adopts that reading
// deliberately rather than building a correspondence between the two families
// (which would mean a new PolicyKind, in the one file S2.41 is concurrently
// editing).

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
// the EventId 1..31 layout of event_framework.hpp:164-169. The word is a
// uint32_t, so S2.02's Act-2/3 ids 32..51 have no bit and always read false;
// S2.13, which makes them drawable, owns widening the storage.
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

// --- Relic targeting ---------------------------------------------------------

// A relic the scan WATCHES, step by step, over the whole run. Three
// observations per target, each latched (idempotent OR, per the StepObserver
// contract):
//
//   offered           the relic was visible as an OFFER: a RELIC row on the
//                     live reward screen (rc.rewards -- elites, chests, event
//                     combat rewards) or a shelf slot of the live merchant
//                     (rc.shop.relics). Offers depend on the policy's path
//                     (pool pops are path-dependent), which is exactly why this
//                     is scanned rather than computed from the seed alone.
//   reward_offered    the REWARD-ROW half of `offered` alone. The distinction
//                     is load-bearing for capture planning: a reward-row
//                     offer is CLAIMABLE FOR FREE, while a shelf offer must
//                     be BOUGHT -- and an A20 shop lists only AFFORDABLE rows
//                     in its choice list, so an uncommon relic (250 base,
//                     ~265+ at A20) on a floor-3 shelf against ~130 gold is
//                     an offer no live policy can accept. The first bottle
//                     scan conflated the two and selected 14 seeds whose
//                     bottles all sat on unaffordable early shelves.
//   acquired          the relic is in RunState.relics -- the policy claimed or
//                     bought it.
//   shop_while_owned  a merchant floor (RunPhase::SHOP) was live while the
//                     relic was owned. This is The Courier's whole question:
//                     its restock behaviour only exists in a shop entered
//                     AFTER it was picked up elsewhere (its canSpawn ANDs
//                     `!(getCurrRoom() instanceof ShopRoom)`, Courier.java:
//                     41-43, so it can never be bought in one).
//
// Names are joined through the registry's exact `game_id` strings
// (sts/registry/game_ids.hpp), the same join key the capture artifacts carry.
struct RelicObs {
    registry::RelicId id = registry::RelicId::NONE;
    bool offered = false;         // reward row OR merchant shelf
    bool reward_offered = false;  // reward row only (claimable for free)
    bool acquired = false;
    bool shop_while_owned = false;
};

// --- Act depth ---------------------------------------------------------------

// Acts a run can be in. `run_advance.hpp` kFinalAct is 3; the fourth act (the
// Ending) is out of the S2 model entirely, so three bits is the whole space and
// a uint8_t mask is not a premature narrowing.
inline constexpr int kMaxActs = 3;

// Bit for one act in the `boss_reached_acts` / `boss_killed_acts` masks. Acts
// are 1-based; act 0 (before the dungeon exists) has no bit and never sets one.
[[nodiscard]] constexpr uint8_t act_bit(unsigned act) noexcept {
    return (act >= 1u && act <= static_cast<unsigned>(kMaxActs))
               ? static_cast<uint8_t>(1u << (act - 1u))
               : uint8_t{0};
}

[[nodiscard]] constexpr bool act_bit_set(uint8_t mask, unsigned act) noexcept {
    const uint8_t b = act_bit(act);
    return b != 0 && (mask & b) != 0;
}

// '|'-joined "act<N>=<encounter game id>" for the acts whose boss identity was
// observed, in ascending act order; "" when none was. The separator rationale
// is the events column's (no encounter game id contains '|' or a tab).
[[nodiscard]] std::string boss_ids_text(const uint16_t (&boss_ids)[kMaxActs]);

// The registry `game_id` for an EncounterDef id, or "" for 0 / unknown. A
// linear scan of the generated kEncounters table -- the generator emits no
// id->name lookup for encounters (they have no enum, tools/registry_gen
// vocab.py DOMAINS), and 61 rows scanned once per observed boss is not a cost
// worth a second table to get wrong.
[[nodiscard]] std::string_view encounter_game_id_from_id(uint16_t id);

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
    // One entry per tracked relic, in the order the targets were given to
    // scan_case. Empty when nothing was tracked -- the TSV column is then
    // empty and the row shape is unchanged.
    std::vector<RelicObs> relic_obs;
    uint64_t final_hash = 0;
    // run_case's own pass-A/pass-B/pass-C verdict. "none" on a clean case; any
    // other value is a fuzz finding that happens to have surfaced during a
    // scan, and is reported rather than swallowed.
    std::string fail_kind = "none";

    // --- S2.42 per-act depth, appended after fail_kind in the TSV -----------
    uint8_t max_act = 0;            // highest RunState::act observed
    uint8_t boss_reached_acts = 0;  // bit (act-1) per act whose boss room was entered
    uint8_t boss_killed_acts = 0;   // bit (act-1) per act whose boss was KILLED
    bool victory = false;           // run_is_victory(rc): the act-3 kill
    // The act's boss ENCOUNTER id (RunState::boss_ids), 0 where unobserved.
    // G2-3's ">= 2 distinct first-boss identities" filters on this.
    uint16_t boss_ids[kMaxActs]{};
};

struct ScanLimits {
    // S2.42 raised this from 4000. 4000 was an ACT-1 budget: the tool was
    // written when no run could leave Act 1, and a run that hits the cap ends
    // as EndReason::ACTION_CAP -- which in a DEPTH scan reads as a policy
    // failure while actually being the tool's own truncation. A three-act A20
    // run is roughly three times the actions, so 12000 restores the same
    // headroom-per-act the S1 number had. `ScanSummary::text()` prints the
    // ACTION_CAP count next to the reach numbers so a truncation artifact is
    // visible rather than inferred.
    uint32_t max_actions = 12000;
    uint32_t revisit_limit = 64;
};

// Run one case to terminal and summarise it. Deterministic given `c` and
// `relic_targets` (the targets only ADD observation; no engine stream or
// policy decision reads them). `relic_targets` become row.relic_obs in the
// same order.
[[nodiscard]] ScanRow scan_case(
    const ScanCase& c, const ScanLimits& lim = {},
    const std::vector<registry::RelicId>& relic_targets = {});

// --- Filtering ---------------------------------------------------------------

struct Filter {
    // ALL listed events must have fired in the same run (AND, not OR): the
    // caller asking for two targets wants one capture that contains both.
    std::vector<registry::EventId> need_events;
    bool need_treasure = false;
    bool need_boss = false;
    uint32_t min_floor = 0;

    // --- S2.42 depth clauses ------------------------------------------------
    // 0 means "don't care" for the two act clauses; acts are 1-based, so 0 is
    // not a legal act and cannot collide with a real request.
    uint8_t need_boss_reached_act = 0;
    uint8_t need_boss_killed_act = 0;
    bool need_victory = false;
    uint32_t min_act = 0;
    // ANY-OF, like the relic clauses and for the same reason: the motivating
    // query is "an Act-2 boss cohort covering EITHER of two identities", which
    // an all-of reading could never satisfy (one run fights one boss per act).
    std::vector<uint16_t> need_boss_ids;

    // Relic clauses. UNLIKE need_events, each list is an ANY-OF within its
    // clause: the motivating query is "an early source offered ANY of the
    // three Bottled relics", which an all-of reading could never satisfy (one
    // run rarely offers two bottles). The clauses still AND with each other
    // and with everything above. A row that did not TRACK a named relic (no
    // matching relic_obs entry) never hits -- filtering on an untracked relic
    // is a caller bug the row must not paper over, and the CLI always tracks
    // the union of every filter's relics.
    std::vector<registry::RelicId> need_relic_offered;
    std::vector<registry::RelicId> need_relic_reward_offered;
    std::vector<registry::RelicId> need_relic_acquired;
    std::vector<registry::RelicId> need_shop_after_relic;

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

// One qualifying (seed, policy, policy_seed) for the --cohort-list artifact.
// See the header's note on why this is a triple and what the policy columns do
// and do not mean.
struct CohortTriple {
    std::string seed;
    fuzz::PolicyKind policy = fuzz::PolicyKind::RANDOM;
    uint64_t policy_seed = 0;
    uint8_t boss_reached_acts = 0;
    uint8_t boss_killed_acts = 0;
    uint16_t boss_ids[kMaxActs]{};
};

[[nodiscard]] CohortTriple cohort_triple(const ScanRow& row);
// One TSV line: seed, policy, policy_seed, boss_reached_acts, boss_killed_acts,
// boss_ids. No verdict -- a consumer must never have to parse one.
[[nodiscard]] std::string cohort_triple_to_tsv(const CohortTriple& t);
[[nodiscard]] std::string_view cohort_tsv_header();

// Per-act depth counters, kept once for the whole scan and once per policy --
// "per-act boss-fight and boss-kill rates PER POLICY at scanned scale" is the
// S2.42 Acceptance sentence verbatim, and ScanSummary had no per-policy
// dimension at all before it.
struct ActDepth {
    uint64_t rows = 0;
    uint64_t boss_reached[kMaxActs]{};
    uint64_t boss_killed[kMaxActs]{};
    uint64_t victories = 0;
    uint64_t action_cap = 0;  // the truncation witness -- see ScanLimits

    void add(const ScanRow& row);
};

struct ScanSummary {
    uint64_t rows = 0;
    uint64_t seeds = 0;
    uint64_t treasure_rows = 0;
    uint64_t boss_rows = 0;
    ActDepth depth;
    ActDepth per_policy[static_cast<int>(fuzz::PolicyKind::COUNT)]{};
    uint64_t event_rows[32]{};  // index by EventId (1..31); [0] unused
    // Per tracked relic (find-or-insert on first sighting in add()): rows in
    // which it was offered / acquired / shop-while-owned.
    struct RelicRows {
        registry::RelicId id;
        uint64_t offered = 0;
        uint64_t reward_offered = 0;
        uint64_t acquired = 0;
        uint64_t shop_while_owned = 0;
    };
    std::vector<RelicRows> relic_rows;
    uint64_t floor_hist[kFloorHistogramBuckets]{};
    uint64_t end_reason[static_cast<int>(fuzz::EndReason::COUNT)]{};
    uint64_t failures = 0;
    uint32_t max_floor = 0;
    uint64_t actions = 0;

    void add(const ScanRow& row);
    [[nodiscard]] std::string text() const;
};

}  // namespace sts::planner
