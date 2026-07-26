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

#include "sts/fuzz/policy.hpp"
#include "sts/registry/manifest.hpp"

namespace sts::fuzz {

// Why a run stopped. Only the first three are ordinary; the last two are
// FINDINGS and are reported separately from the totals.
enum class EndReason : uint8_t {
    RUN_OVER = 0,             // player died -- terminal, expected
    ROOM_UNIMPLEMENTED = 1,   // parked on unmodelled room content -- expected today
    NO_LEGAL_MOVES = 2,       // mask empty in a non-terminal phase -- suspicious
    ACTION_CAP = 3,           // hit --max-actions; the run was still going
    LIVELOCK = 4,             // the run started revisiting states (see below)
    COUNT = 5,
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

inline constexpr int kRoomTypeCount = 8;      // map_rooms.hpp RoomType
inline constexpr int kRewardKindCount = 5;    // combat_rewards.hpp RewardItemKind
inline constexpr int kTurnBuckets = 8;        // 1,2,3,4,5-6,7-9,10-19,20+
inline constexpr int kFloorBuckets = 16;      // floor 0..14, then 15+

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

    uint64_t combats_entered = 0;
    uint64_t combats_killed = 0;
    uint64_t combats_smoked = 0;
    uint64_t deaths = 0;
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

    // Human-readable report. `elapsed_s` may be 0 when unknown.
    [[nodiscard]] std::string report(double elapsed_s) const;
    // Flat key=value form for shard merging / machine consumption.
    [[nodiscard]] std::string kv() const;
};

// Parse a `kv()` blob back into a Coverage (for merging shard summaries).
// Returns false on a malformed line.
[[nodiscard]] bool coverage_from_kv(const std::string& text, Coverage& out);

}  // namespace sts::fuzz
