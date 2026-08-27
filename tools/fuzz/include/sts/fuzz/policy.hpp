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
    // S2.V2: the sim-consulting scripted driver (s2-design §6's sanctioned
    // escalation). Deterministic and weight-free like the E0 policies, but a
    // different CLASS of generator: combat decisions run a bounded turn-local
    // search over engine snapshots (copy the controller, advance the copy,
    // score the resulting state -- policy_search.cpp), and run-layer decisions
    // are the b1.7.0 survival heuristics (greedy_policy.py R1-R4 +
    // ACT_PROFILES) ported onto the sim's structs. The ONLY stochastic input
    // is the shared one-draw tie-break from PolicyRng, so a case is still a
    // pure function of its CaseId. The _SKIP variant differs in exactly one
    // rule: R4's boss-relic pick answers SKIP (the S2-G2 item-2 skip-cohort
    // identity, the sim-side mirror of policy_bossrelic_skip.json).
    SIM_SEARCH = 5,
    SIM_SEARCH_SKIP = 6,
    COUNT = 7,
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
    REST = 14,
    SMITH = 15,
    LIFT = 16,
    TOKE = 17,
    DIG = 18,
    SMITH_CARD = 19,
    TOKE_CARD = 20,
    TREASURE_OPEN = 21,
    TREASURE_SKIP = 22,
    EVENT_OPTION = 23,
    EVENT_GRID = 24,
    // The optional hand-select screen's confirm button (ActionVerb::CONFIRM).
    // It gets its own category rather than sharing COMBAT_CHOOSE because the
    // whole point of the soak here is to know that the EMPTY confirm -- the
    // legal move a count-driven screen has no spelling for -- is being reached,
    // and a shared counter could not tell a confirm from a toggle.
    CHOICE_CONFIRM = 25,
    // Every shop move: buying a card / relic / potion, opening the removal
    // grid, confirming a card in it, and the Proceed that leaves. ONE bucket
    // for what is really two screens and four item kinds, for the reason the
    // NEOW_PROCEED comment in policy.cpp gives -- MoveCat is a SHARED,
    // append-only namespace (docs/stage-b-tasks.md's shared-namespace table),
    // so splitting it finer means the orchestrator ALLOCATING values rather
    // than a task taking them. What is lost is coverage resolution inside one
    // shop; legality and enumeration are exact either way.
    SHOP = 26,
    // The campfire's Ruby Key button (RestOptionKind::RECALL): taking it spends
    // the whole campfire action and flips the run's kKeyRuby bit, so the soak
    // needs to know it is reached -- and reached SEPARATELY from REST, whose
    // ordinal it can share a menu with.
    RECALL = 27,
    // The boss chest (RunPhase::BOSS_TREASURE). Four categories rather than one,
    // because unlike the shop's or Neow's several screens these four moves have
    // DIFFERENT and individually load-bearing reachability -- the whole point of
    // trap 3 is that the three relics burn at room entry whether or not the
    // chest is ever opened, so a soak must be able to say separately how often
    // it opened, picked, skipped, and walked past. The S2 Wave-2 grant
    // (docs/stage-b-tasks.md) allocated 28-31 for exactly this.
    BOSS_CHEST_OPEN = 28,   // click the chest -> bossRelicScreen
    BOSS_CHEST_PICK = 29,   // take one of the three (also the equip sub-screens:
                            // a picked relic's onEquip grid / reward screen is
                            // part of the same pick, not a fifth move kind)
    BOSS_CHEST_SKIP = 30,   // the screen's cancel button -- REVERSIBLE
    BOSS_CHEST_PROCEED = 31,  // leave the room (the noPick path when unpicked)
    COUNT = 32,
};

[[nodiscard]] const char* move_cat_name(MoveCat c) noexcept;

struct Move {
    engine::Action action{};
    MoveCat cat = MoveCat::END_TURN;
};

// Proven upper bound on simultaneously-legal moves. A combat CHOOSE may source
// the 128-slot discard/exhaust/draw pile (not merely the ten-slot hand), while
// each potion slot contributes either one non-target move or at most
// kMonsterCap targeted moves. Those two sets may coexist while a choice is open.
inline constexpr size_t kMoveCap =
    static_cast<size_t>(engine::kDiscardCap) +
    static_cast<size_t>(engine::kPotionCap * engine::kMonsterCap);
static_assert(engine::kDiscardCap >= engine::kExhaustCap);
static_assert(engine::kDiscardCap >= engine::kDrawCap);
static_assert(engine::kDiscardCap >= engine::kMasterDeckCap);
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

// The SIM_SEARCH / SIM_SEARCH_SKIP decision body (policy_search.cpp).
// policy_pick dispatches here; exposed so the planner's tests can drive it
// directly. Same purity contract as policy_pick: deterministic given
// (kind, rc, moves, rng state), exactly one rng draw per call.
[[nodiscard]] size_t sim_search_pick(PolicyKind kind,
                                     const engine::RunController& rc,
                                     const Move* moves, size_t n,
                                     PolicyRng& rng) noexcept;

}  // namespace sts::fuzz
