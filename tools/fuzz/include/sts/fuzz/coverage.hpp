#pragma once

// COVERAGE ACCOUNTING for the fuzz soak.
//
// A soak that reports only "10,000,000 actions, 0 failures" is not evidence
// about the engine -- it is evidence about the number 10,000,000. The point of
// this header is that every claim in the final report is a counter that was
// incremented by a real step, and that the report can say what was NEVER
// reached as flatly as what was.
//
// So the interesting fields here are the zeros: room types never entered,
// move categories never legal, reward kinds never claimed, registry rows never
// instantiated. `Coverage::write_report` prints those explicitly under
// "NEVER REACHED" rather than leaving them to be inferred from a table.
//
// Everything is a plain counter so shards merge by addition (`merge`), which is
// what lets the overnight script sum N processes into one report.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "sts/engine/map_rooms.hpp"  // RoomType / kRoomTypeCount
#include "sts/engine/run_advance.hpp"  // kFinalAct (the per-act bucket count)
#include "sts/fuzz/policy.hpp"
#include "sts/registry/manifest.hpp"

namespace sts::fuzz {

// Why a run stopped. Only the first three are ordinary; the last three are
// findings and are reported separately from the totals.
enum class EndReason : uint8_t {
    RUN_OVER = 0,             // RunPhase::RUN_OVER -- terminal, expected
                              // (a death OR the S1 boss victory; the
                              // victories/deaths counters carry the split)
    ROOM_UNIMPLEMENTED = 1,   // parked on unmodelled room content -- expected today
    NO_LEGAL_MOVES = 2,       // mask empty in a non-terminal phase -- suspicious
    ACTION_CAP = 3,           // hit --max-actions; the run was still going
    LIVELOCK = 4,             // the run started revisiting states (see below)
    NO_PROGRESS = 5,          // a legal action was an immediate one-state cycle
    COUNT = 6,
};

// LIVELOCK is NOT reported as a failure by default, and the reason is a real
// one found by the first smoke run of this tool. The combat-reward screen has a
// legal 2-cycle: claiming a CARDS item opens the pick screen, Skip closes it,
// and (by design -- run_advance.hpp: "the CARD item stays claimable") the item
// is claimable again. A policy that prefers Skip therefore loops forever while
// the controller hash keeps changing, so a plain zero-delta detector never
// fires and the run burns its whole action budget on two states.
//
// That is faithful to the game, so calling it a bug would make every soak red.
// What the fuzzer owes instead is (a) not to waste the budget on it, and (b) to
// SAY how often it happened -- a soak whose actions all went into a 2-cycle has
// not tested anything, and that must be visible in the report rather than
// hidden inside a large total. `--fail-on-livelock` promotes it for anyone
// hunting a genuine engine livelock.

[[nodiscard]] const char* end_reason_name(EndReason r) noexcept;

// map_rooms.hpp's RoomType count. DERIVED from the enum, not re-spelled: this
// sizes room_entered[] / room_stalled[], which are indexed by rc.room_type, and
// as a hand-written 8 with nothing checking it, it would have silently
// under-covered the enum the moment a room kind was added. engine::
// kRoomTypeCount carries its own static_assert against the last enumerator, so
// the guard exists at both ends of the dependency.
inline constexpr int kRoomTypeCount = engine::kRoomTypeCount;
static_assert(
    kRoomTypeCount == static_cast<int>(engine::RoomType::Victory) + 1,
    "kRoomTypeCount must cover every RoomType enumerator");
// Same discipline as kRoomTypeCount above, and for the same reason: this was a
// hand-written 5 with nothing checking it, so it had ALREADY silently
// under-covered the enum -- `STOLEN_GOLD` (kind 5) was outside the array and
// `reward_kind_name` had no case for it. S3.11 added EMERALD_KEY (6) and
// SAPPHIRE_KEY (7), which would have been dropped the same way. It is now the
// engine's own count, which carries its own static_assert against the last
// enumerator, so the guard exists at both ends of the dependency.
inline constexpr int kRewardKindCount = engine::kRewardKindCount;
static_assert(kRewardKindCount ==
                  static_cast<int>(engine::RewardItemKind::SAPPHIRE_KEY) + 1,
              "kRewardKindCount must cover every RewardItemKind enumerator");
inline constexpr int kTurnBuckets = 8;        // 1,2,3,4,5-6,7-9,10-19,20+
inline constexpr int kFloorBuckets = 16;      // floor 0..14, then 15+

// PER-ACT BUCKETS, indexed by RunState::act DIRECTLY (1-based), so index 0 is a
// slot that can never be written and every table below reads act N at [N].
//
// It is sized from engine::kFinalAct rather than the literal 4, so a bucket
// array cannot silently DROP an act it was handed -- which would turn "we never
// got there" and "we got there and did not count it" into the same zero, the
// exact confusion the shop entry hole (fuzz_run.cpp) already cost this tool
// once.
//
// S3.32 MOVED kFinalAct 3 -> 4 AND THE `+ 2` WITH IT. The old spelling was
// `kFinalAct + 2` for a reason that has now expired: it bought one slot past
// the terminal act so Act 4 kept a bucket while the Ending was unmodelled. With
// kFinalAct == 4 that slot IS act 4, so the term is `+ 1` and the array is the
// same five entries -- the static_assert below is what caught the change, and
// keeping it at 5 is what says the width did not silently grow.
inline constexpr int kActBuckets = engine::kFinalAct + 1;  // 0 unused, 1..4
static_assert(kActBuckets == 5, "acts are 1..4; index 0 is the unused sentinel");

// A fixed-capacity "which registry rows did we ever see" bitset. Sized from the
// generated manifest so it cannot silently under-cover a growing registry.
template <std::size_t N>
struct SeenSet {
    static constexpr std::size_t kWords = (N + 63) / 64;
    uint64_t w[kWords]{};

    void set(std::size_t i) noexcept {
        if (i < N) w[i / 64] |= (1ull << (i % 64));
    }
    [[nodiscard]] bool test(std::size_t i) const noexcept {
        return i < N && (w[i / 64] & (1ull << (i % 64))) != 0;
    }
    [[nodiscard]] std::size_t count() const noexcept {
        std::size_t n = 0;
        for (std::size_t k = 0; k < kWords; ++k) {
            n += static_cast<std::size_t>(std::popcount(w[k]));
        }
        return n;
    }
    void merge(const SeenSet& o) noexcept {
        for (std::size_t k = 0; k < kWords; ++k) w[k] |= o.w[k];
    }
};

struct Coverage {
    // --- totals ---
    uint64_t cases = 0;      // (seed, ascension, policy, policy_seed) tuples run
    uint64_t runs = 0;       // engine runs executed (>= 2 per case: replay-twice)
    uint64_t actions = 0;    // actions counted ONCE per case (pass A)
    uint64_t actions_engine = 0;  // actions actually stepped, all passes

    uint64_t end_reason[static_cast<int>(EndReason::COUNT)]{};
    // Actions attributed to each end reason. "How many cases livelocked" is
    // much less useful than "how much of the action budget went into
    // livelocked runs" -- the second is what tells you whether a big total is
    // real coverage or a 2-cycle with good PR.
    uint64_t end_actions[static_cast<int>(EndReason::COUNT)]{};
    uint64_t per_policy_cases[static_cast<int>(PolicyKind::COUNT)]{};
    uint64_t per_policy_actions[static_cast<int>(PolicyKind::COUNT)]{};

    // --- structural coverage ---
    uint64_t room_entered[kRoomTypeCount]{};
    uint64_t room_stalled[kRoomTypeCount]{};   // parked at ROOM_UNIMPLEMENTED here
    uint64_t move_legal[static_cast<int>(MoveCat::COUNT)]{};  // times in the legal set
    uint64_t move_taken[static_cast<int>(MoveCat::COUNT)]{};
    uint64_t reward_claimed[kRewardKindCount]{};

    // --- PER-ACT structural coverage (S2.41) -------------------------------
    //
    // WHY THESE EXIST. The S2-G1 gate soak claims "three-act A20 runs". Before
    // this block the report could not tell a sweep that walked all three acts
    // from one that died on floor 6 every time: `max_floor` is a single number
    // one lucky case can carry, and the rooms table is act-blind. These four
    // tables make the claim a witness -- and, just as importantly, make its
    // ABSENCE a printed line rather than something a reader has to notice is
    // missing (report()'s NEVER REACHED block names every act that no case
    // stood in, and every act whose boss was never fought or never killed).
    //
    // The unit differs per table, deliberately:
    //   act_cases    -- CASES (idempotent per case: a run that spends 40 steps
    //                   in Act 2 counts once, so this reads as "how many runs
    //                   got there", which is the reach question).
    //   act_rooms    -- ROOM ENTRIES, the same event room_entered[] counts,
    //                   split by the act it happened in (they sum to it).
    //   act_boss_*   -- FIGHTS and KILLS, per act. A kill is the act's boss
    //                   combat leaving COMBAT with RunCombatOutcome::KILLED,
    //                   which is exact for all three acts -- the Act-3 boss
    //                   opens no reward screen and no chest at all
    //                   (run_advance.hpp), so a BOSS_TREASURE-based probe (the
    //                   one seed_scan uses for Acts 1-2) would read 0 there
    //                   forever. `victories` is the independent cross-check:
    //                   act_boss_kills[3] and victories count the same event
    //                   from two different sides.
    uint64_t act_cases[kActBuckets]{};
    uint64_t act_rooms[kActBuckets][kRoomTypeCount]{};
    uint64_t act_boss_fights[kActBuckets]{};
    uint64_t act_boss_kills[kActBuckets]{};

    uint64_t combats_entered = 0;
    uint64_t combats_killed = 0;
    uint64_t combats_smoked = 0;
    uint64_t deaths = 0;      // RUN_OVER terminals that were a death
    uint64_t victories = 0;   // RUN_OVER terminals that were the run's WIN
                              // (engine::run_is_victory) -- the two share one
                              // phase and one EndReason, so the split lives
                              // here, not in a new enumerator. The win moved
                              // with its producer at S2.12: it is now the
                              // ACT-3 BOSS kill, not the Act-1 boss chest's
                              // proceed.
                              //
                              // S2.41 re-measured this once the S2.2x/S2.3x
                              // content landed and the structural 0 was gone.
                              // It is no longer a content gap; what remains is
                              // a REACH result, and the honest reading is that
                              // the E0 heuristics of policy.hpp win a run so
                              // rarely that a soak can be large and still show
                              // 0 here. The per-act tables below are what say
                              // WHICH act a sweep actually got to, so a 0 in
                              // this field is attributable instead of merely
                              // disappointing. Cross-check: act_boss_kills[3]
                              // counts the same event from the combat side.
    uint64_t reward_screens = 0;
    uint64_t cards_taken = 0;
    uint64_t cards_skipped = 0;
    uint64_t potions_used = 0;
    uint64_t relics_gained = 0;
    uint64_t escapes = 0;              // smoke-bomb escapes (== combats_smoked)

    uint64_t turn_bucket[kTurnBuckets]{};   // per COMBAT, its max turn reached
    uint64_t floor_bucket[kFloorBuckets]{}; // per CASE, its final floor
    uint32_t max_turn = 0;
    uint32_t max_floor = 0;
    uint32_t max_actions_in_case = 0;
    uint32_t max_act = 0;   // merges by max, like the three above

    // --- registry-row sightings (the §7.4 "to-fuzz list" in miniature) ---
    // Sized from the generated manifest plus headroom: registry ids are
    // APPEND-ONLY and may carry gaps (design §4.4), so the largest id can
    // exceed the row count. The report walks ids rather than rows and skips
    // any id the registry has no game_id for, which is how a gap is told apart
    // from a row that was never exercised.
    static constexpr std::size_t kIdHeadroom = 64;
    SeenSet<registry::manifest::kCardsCount + kIdHeadroom> cards_played;
    SeenSet<registry::manifest::kMonstersCount + kIdHeadroom> monsters_fought;
    SeenSet<registry::manifest::kRelicsCount + kIdHeadroom> relics_owned;
    SeenSet<registry::manifest::kPotionsCount + kIdHeadroom> potions_held;
    SeenSet<registry::manifest::kPowersCount + kIdHeadroom> powers_applied;

    void merge(const Coverage& o) noexcept;
    // Merge only if every additive scalar fits. Summary ingestion uses this
    // strict form so individually-valid shards cannot wrap a campaign total.
    [[nodiscard]] bool merge_checked(const Coverage& o) noexcept;

    // Human-readable report. `elapsed_s` may be 0 when unknown.
    [[nodiscard]] std::string report(double elapsed_s) const;
    // Flat key=value form for shard merging / machine consumption.
    [[nodiscard]] std::string kv() const;
};

// Parse a `kv()` blob back into a Coverage (for merging shard summaries).
// Returns false on a malformed line, on an unknown key, and -- deliberately --
// on a MISSING one. Strictness is the point: a merge that silently dropped a
// field would understate a soak's totals.
[[nodiscard]] bool coverage_from_kv(const std::string& text, Coverage& out);

// The keys a summary written by an older `fuzz_soak` may legitimately lack, in
// the order they were added. Exposed so a test can prove the tolerance covers
// exactly this set and nothing more.
[[nodiscard]] const std::vector<std::string>& legacy_optional_kv_keys();

// The same parse, tolerating summaries written by an OLDER `fuzz_soak` whose
// counter set was smaller.
//
// WHY THIS EXISTS, and why it is not the default. Summaries written before the
// `victories` counter landed (pre-`6d7efc4`) do not carry that key, so
// `coverage_from_kv` rejects them -- correctly, because for a LIVE sweep a
// missing counter is drift. But an ARCHIVED campaign summary is a historical
// artifact: it cannot be rewritten, and regenerating it means re-running the
// whole sweep it summarises. This reads it, defaults every field that vintage
// did not have to 0, and reports WHICH ones through `defaulted` so the caller
// can say so out loud.
//
// A summary read this way is NOT equivalent to a native one: `victories` reads
// 0 whether the sweep won nothing or simply never counted, so any report built
// on it must carry that caveat rather than present the number as measured. That
// is why the flag is opt-in and why `--merge` prints a banner.
//
// Only keys in `legacy_optional_kv_keys()` may be absent. A missing key outside
// that set, an unknown key, or a malformed line still returns false -- the
// tolerance is for history, never for drift.
[[nodiscard]] bool coverage_from_kv_legacy(const std::string& text, Coverage& out,
                                           std::vector<std::string>& defaulted);

}  // namespace sts::fuzz
