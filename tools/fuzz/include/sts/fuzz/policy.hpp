#pragma once

// Action-sequence GENERATORS for the sim-side fuzz soak (stage-b design §3.3's
// frozen substitution for "current agent policy": random-legal plus the E0
// heuristic baselines pulled forward from InitialPlan Part 2 -- greedy-damage,
// greedy-block, hoard-gold, always-event).
//
// Two hard properties, both load-bearing for B5.1:
//
//  1. NO ENGINE RNG IS TOUCHED. Every stochastic decision comes from PolicyRng,
//     a private splitmix64 seeded from the case's `policy_seed`. If a policy
//     drew from an engine stream it would perturb the very determinism the soak
//     is measuring, and a "nondeterminism" report would be the fuzzer's fault.
//
//  2. THE POLICY IS A PURE FUNCTION of (state, legal moves, PolicyRng state).
//     That is what makes a four-value CaseId a complete reproducer (case_id.hpp)
//     and lets a crash be replayed with no action log.
//
// Move enumeration is done ONCE, into a flat array, from RunActionMask. The
// engine's mask is the only source of legality -- the fuzzer never invents an
// action, and advance() re-checks anyway (advance.hpp), so an action the mask
// reports and advance() then ignores is itself a finding (see fuzz_run.hpp's
// no-progress detector).

#include <cstddef>
#include <cstdint>
#include <string>

#include "sts/engine/run_advance.hpp"
#include "sts/engine/types.hpp"

namespace sts::fuzz {

// --- PolicyRng ---------------------------------------------------------------

// splitmix64 (Steele/Lea/Flood). Deliberately NOT one of the engine's RNGs:
// this stream must be independent of everything the engine hashes.
class PolicyRng {
public:
    explicit PolicyRng(uint64_t seed) noexcept : state_(seed) {}

    [[nodiscard]] uint64_t next() noexcept {
        state_ += 0x9E3779B97F4A7C15ull;
        uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // Uniform in [0, n) via Lemire's multiply-shift (n > 0). Not rejection-
    // sampled: the modulo bias at n <= 256 against a 64-bit draw is ~2^-56,
    // which is irrelevant to a coverage generator and keeps the draw count
    // exactly one per decision (so `policy_seed` advances predictably).
    [[nodiscard]] uint32_t below(uint32_t n) noexcept {
        return static_cast<uint32_t>((next() >> 32) * static_cast<uint64_t>(n) >> 32);
    }

    [[nodiscard]] uint64_t state() const noexcept { return state_; }

private:
    uint64_t state_;
};

// --- Policies ----------------------------------------------------------------

enum class PolicyKind : uint8_t {
    RANDOM = 0,         // uniform over every legal move (design §3.3 "random-legal")
    GREEDY_DAMAGE = 1,  // E0: maximize this turn's damage; kill the weakest first
    GREEDY_BLOCK = 2,   // E0: maximize block, then damage
    HOARD_GOLD = 3,     // E0: claim gold, refuse cards, keep potions
    ALWAYS_EVENT = 4,   // E0: steer the map away from combat nodes
    COUNT = 5,
};

[[nodiscard]] const char* policy_name(PolicyKind k) noexcept;
[[nodiscard]] bool policy_from_name(const std::string& name, PolicyKind& out) noexcept;

// --- Move enumeration --------------------------------------------------------

// A coarse category per legal move. Its only job is COVERAGE ACCOUNTING: the
// soak reports how often each category was legal and how often it was taken, so
// "we never once used a potion" is a number rather than a guess.
enum class MoveCat : uint8_t {
    PLAY_CARD = 0,
    PLAY_CARD_TARGET = 1,
    END_TURN = 2,
    COMBAT_CHOOSE = 3,
    USE_POTION = 4,
    USE_POTION_TARGET = 5,
    NEOW_PROCEED = 6,
    MAP_NODE = 7,
    MAP_BOSS = 8,
    REWARD_CLAIM = 9,
    REWARD_TAKE_CARD = 10,
    REWARD_SKIP_CARD = 11,
    REWARD_SING = 12,
    REWARD_PROCEED = 13,
    TREASURE_OPEN = 14,
    TREASURE_SKIP = 15,
    COUNT = 16,
};

[[nodiscard]] const char* move_cat_name(MoveCat c) noexcept;

struct Move {
    engine::Action action{};
    MoveCat cat = MoveCat::END_TURN;
};

// Proven upper bound on simultaneously-legal moves. A combat CHOOSE may source
// the 128-slot discard/exhaust pile (not merely the ten-slot hand), while each
// potion slot contributes either one non-target move or at most kMonsterCap
// targeted moves. Those two sets may coexist while a choice is open.
inline constexpr size_t kMoveCap =
    static_cast<size_t>(engine::kDiscardCap) +
    static_cast<size_t>(engine::kPotionCap * engine::kMonsterCap);
static_assert(engine::kDiscardCap >= engine::kExhaustCap);
static_assert(kMoveCap >= 163);

// Enumerate every legal move for `rc`. `mask` must be the output of
// engine::legal_actions(rc, mask). Returns the move count (<= kMoveCap).
[[nodiscard]] size_t enumerate_moves(const engine::RunController& rc,
                                     const engine::RunActionMask& mask,
                                     Move* out, size_t cap) noexcept;

// Pick one of `moves[0..n)`. `n` must be > 0. Deterministic given
// (kind, rc, moves, rng state).
[[nodiscard]] size_t policy_pick(PolicyKind kind, const engine::RunController& rc,
                                 const Move* moves, size_t n, PolicyRng& rng) noexcept;

}  // namespace sts::fuzz
