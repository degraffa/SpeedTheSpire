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
    // S2.V2's Awakened One discharge experiment (2026-08-27). SIM_SEARCH plus
    // exactly ONE extra rule -- the CURIOSITY HOLD (see below and
    // policy_search.cpp): while a live monster owns PowerId::CURIOSITY, a
    // POWER-card play is priced at what the tax buys the boss beyond the
    // rollout's horizon, which in practice holds every Power until the Rebirth
    // purge lifts it. Everything else, R4's boss-relic identity included, is
    // SIM_SEARCH's.
    //
    // IT IS A SEPARATE KIND, NOT A CHANGE TO SIM_SEARCH, and that is the whole
    // point: the rule was MEASURED HARMFUL. On a paired 110-seed x 1,024
    // policy-seed grid (112,640 rows each, 1,929 Awakened One boss fights each)
    // SIM_SEARCH killed the boss 22 times and this kind 5 -- so the report's
    // §6 mechanism hypothesis is falsified, and the cohort schedules from
    // SIM_SEARCH. Keeping the rule as its own kind makes SIM_SEARCH provably
    // untouched (SimSearchCuriosityHold.LeavesSimSearchTrajectoriesIdentical)
    // and keeps the falsifying A/B a command anyone can re-run instead of a
    // number in a document. See docs/verification/s2v2-sim-reach.md §6.
    SIM_SEARCH_HOLD = 7,
    // S3.22's key-seeking variant (s3-design §6.1 step 1). SIM_SEARCH plus
    // exactly four run-layer rules, all of which name a key:
    //
    //   K1  a reward row of kind EMERALD_KEY or SAPPHIRE_KEY is claimed above
    //       every other row on its screen (the sapphire claim DESTROYS the
    //       chest relic -- RewardItem.java:317-326 -- which is the price this
    //       kind is willing to pay and SIM_SEARCH is not);
    //   K2  the campfire's RECALL button outranks rest/smith while HP allows;
    //   K3  a map candidate whose destination is the act's BURNING ELITE node
    //       (rc.emerald_x/emerald_y) gets a bounded appetite bonus while the
    //       emerald key is unheld and HP allows;
    //   K4  the same, smaller, for a Treasure node while the sapphire key is
    //       unheld and for a Rest node while the ruby key is unheld.
    //
    // IT IS A SEPARATE KIND, NOT A CHANGE TO SIM_SEARCH, for the reason
    // SIM_SEARCH_HOLD is: SIM_SEARCH is the cohort identity every S2-G2
    // schedulable triple was selected under, and its scan output must stay
    // byte-identical so those triples remain reproducible. Every rule above is
    // gated on `kind == SIM_SEARCH_KEYS` at exactly one site, and S3.22's
    // acceptance proves the invariance by sha256 of a fixed-range scan taken
    // before and after this value existed. Key-seeking is measured COSTLY --
    // see docs/verification/s3-22-key-reach.md for the paired table.
    SIM_SEARCH_KEYS = 8,
    // The information-limited twin of SIM_SEARCH (GT1/T2.2's fair scripted
    // baseline). T2.2's first combat expert-iteration run lost to SIM_SEARCH
    // at p=1.0, and nobody knew how much of that gap was SEARCH QUALITY versus
    // INFORMATION: SIM_SEARCH's rollout copy carries the TRUE controller,
    // hidden draw order and all, while a trained agent can only search over
    // `resample_hidden` particles (training-contract.md's declared hidden
    // set). This kind answers that by making exactly ONE substitution at
    // sim_search_pick's rollout-snapshot sites (policy_search.cpp): the
    // world every candidate is rolled out against is a `resample_hidden`
    // twin of `rc`, drawn ONCE per decision from a `SamplerRng` seeded off a
    // fresh `PolicyRng::next()` draw -- so every candidate in one decision is
    // compared under the SAME resampled world (common random numbers), and
    // the case stays a pure function of its CaseId (`--verify-determinism`
    // holds). Scorer, run-layer heuristics and the shared one-draw tie-break
    // are otherwise byte-identical to SIM_SEARCH -- every kind gate above
    // (`kind_holds_powers`, `kind_seeks_keys`, the `SIM_SEARCH_SKIP`
    // boss-relic answer) is an explicit `==` against ITS kind, so this value
    // falls through to SIM_SEARCH's behaviour in all of them by construction.
    // IT IS A SEPARATE KIND, NOT A CHANGE TO SIM_SEARCH, on the
    // SIM_SEARCH_HOLD/SIM_SEARCH_KEYS precedent: SIM_SEARCH's own output over
    // a fixed seed_scan range must stay byte-identical, which this kind's
    // acceptance proves by sha256. `SIM_SEARCH - SIM_SEARCH_BLIND` on paired
    // seeds is the measured information premium -- see
    // docs/verification/sim-search-blind.md.
    SIM_SEARCH_BLIND = 9,
    COUNT = 10,
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

// The SIM_SEARCH / SIM_SEARCH_SKIP / SIM_SEARCH_HOLD decision body
// (policy_search.cpp). policy_pick dispatches here; exposed so the planner's
// tests can drive it directly. Same purity contract as policy_pick:
// deterministic given (kind, rc, moves, rng state), exactly one rng draw per
// call.
[[nodiscard]] size_t sim_search_pick(PolicyKind kind,
                                     const engine::RunController& rc,
                                     const Move* moves, size_t n,
                                     PolicyRng& rng) noexcept;

// --- the Curiosity hold (S2.V2's Awakened One discharge, 2026-08-27) ---------
//
// SIM_SEARCH_HOLD's one extra combat rule, exposed so the directed tests can
// pin the CRITERION itself instead of inferring it from a decision. The
// derivation from the Java lives above the helpers in policy_search.cpp; the
// measured verdict on the rule is on PolicyKind::SIM_SEARCH_HOLD above.
//
// `sim_search_curiosity_tax(cs)` IS the trigger, and it is a property of the
// BOARD, not of the policy: the Strength that one POWER card play would hand a
// live monster right now -- the stack amount of PowerId::CURIOSITY on the first
// live monster that owns it, which is what CuriosityPower.onUseCard
// (CuriosityPower.java:42-47) applies. It returns 0, meaning the rule cannot
// fire, in every combat in the game except an Awakened One's phase 1: the
// power's only applier anywhere is AwakenedOne.usePreBattleAction
// (AwakenedOne.java:144-150, amount 2 from ascension 19), and the Rebirth purge
// removes it by name (:302-308).
[[nodiscard]] int sim_search_curiosity_tax(const engine::CombatState& cs) noexcept;

// The score the hold subtracts from candidate `m` in state `rc` UNDER
// SIM_SEARCH_HOLD: 0 unless the tax above is live AND `m` plays a POWER card
// out of hand, otherwise `tax * 4 (SS_AMT) * 20 (the rollout turn cap) * 300
// (one player HP)` -- one more rollout horizon of the damage the tax buys the
// boss. This function reports the rule's price for any state; only
// SIM_SEARCH_HOLD ever charges it.
[[nodiscard]] int64_t sim_search_curiosity_penalty(
    const engine::RunController& rc, const Move& m) noexcept;

}  // namespace sts::fuzz
