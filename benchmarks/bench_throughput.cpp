// B5.5 throughput floors: complete random-policy combats per core and complete
// A20 runs under the frozen random-policy stand-in for the future 25-sim MCTS.
//
// The action-selection RNG is benchmark-harness state only. Gameplay RNG stays
// inside CombatState / RunController and remains bit-exact. The full-run
// benchmark deliberately reuses sts::fuzz's canonical random-legal move
// enumerator: a benchmark-local approximation of RunActionMask would otherwise
// drift every time a screen or action kind is added.

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

#include <benchmark/benchmark.h>

#include "sts/engine/advance.hpp"
#include "sts/engine/index_cast.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/types.hpp"
#include "sts/fuzz/policy.hpp"

namespace {

using namespace sts::engine;

constexpr uint32_t kMaxCombatActions = 10000;
constexpr uint32_t kMaxRunActions = 20000;
constexpr uint64_t kCombatSeedBase = 0xB5500000ull;
constexpr uint64_t kPolicySeedBase = 0x25A20C0DEull;
constexpr uint64_t kSeedStride = 0x9E3779B97F4A7C15ull;
constexpr uint64_t kCompleteRunSeedCorpus = 1000;

std::vector<CardId> SkeletonDeck() {
    std::vector<CardId> deck;
    for (int i = 0; i < 5; ++i) deck.push_back(CardId::STRIKE);
    for (int i = 0; i < 4; ++i) deck.push_back(CardId::DEFEND);
    deck.push_back(CardId::BASH);
    deck.push_back(CardId::SHRUG_IT_OFF);
    deck.push_back(CardId::POMMEL_STRIKE);
    return deck;
}

bool pick_combat_action(const ActionMask& mask, sts::fuzz::PolicyRng& rng,
                        Action& out) noexcept {
    // The standalone combat is the one-monster walking-skeleton encounter and
    // its deck opens no choice screens. Keep target selection exact anyway:
    // targeted cards are enumerated through can_play_target, while untargeted
    // cards are enumerated through can_play only.
    std::array<Action, (kHandCap * kMonsterCap) + kHandCap + 1> moves{};
    int count = 0;
    for (int hand = 0; hand < kHandCap; ++hand) {
        bool targeted = false;
        for (int monster = 0; monster < kMonsterCap; ++monster) {
            if (mask.can_play_target[hand][monster]) {
                moves[as_index(count++)] =
                    make_action(ActionVerb::PLAY_CARD,
                                static_cast<uint8_t>(hand),
                                static_cast<uint8_t>(monster));
                targeted = true;
            }
        }
        if (mask.can_play[hand] && !targeted) {
            moves[as_index(count++)] =
                make_action(ActionVerb::PLAY_CARD,
                            static_cast<uint8_t>(hand));
        }
    }
    if (mask.can_end_turn) {
        moves[as_index(count++)] = make_action(ActionVerb::END_TURN);
    }
    if (count == 0) return false;
    out = moves[rng.below(static_cast<uint32_t>(count))];
    return true;
}

bool run_full_combat(int64_t seed, uint64_t policy_seed,
                     std::span<const CardId> deck,
                     uint64_t& steps) noexcept {
    CombatState combat = combat_begin(seed, 1, deck);
    sts::fuzz::PolicyRng rng(policy_seed);
    StepResult result{};

    for (uint32_t action_index = 0; action_index < kMaxCombatActions;
         ++action_index) {
        ActionMask mask{};
        legal_actions(combat, mask);
        Action action{};
        if (!pick_combat_action(mask, rng, action)) return false;

        advance(std::span<CombatState>(&combat, 1),
                std::span<const Action>(&action, 1),
                std::span<StepResult>(&result, 1));
        ++steps;
        if (result.terminal) return true;
    }
    return false;
}

// Same deterministic mapping used by fuzz_soak for its accepted 1,000-seed
// random-policy corpus. Reusing both the seed interval and its policy-stream
// mapping makes the throughput workload a fixed, already-proven set of
// complete terminal trajectories rather than an accidental content sweep.
uint64_t fuzz_policy_seed_for(int64_t seed) noexcept {
    uint64_t z = static_cast<uint64_t>(seed) * 0x9E3779B97F4A7C15ull;
    z ^= (static_cast<uint64_t>(sts::fuzz::PolicyKind::RANDOM) + 1) *
         0xD1B54A32D192ED03ull;
    z ^= 0xA24BAED4963EE407ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// S2.45: `terminal_act` receives the act the trajectory ENDED in. The act
// counter never decreases (run_advance.cpp's act transition only ever does
// ++act), so one read at the end is the run's maximum act -- no per-step cost,
// and no second traversal. It exists because the S2 re-baseline claims a
// THREE-ACT rate: with the E0 random policy losing roughly x30 per act
// (docs/s2-tasks.md, S2.41), act-2 and act-3 reach in this fixed corpus is a
// number the report has to print rather than assume.
bool run_full_a20(int64_t seed, uint64_t policy_seed,
                  uint64_t& steps, uint8_t& terminal_act) noexcept {
    RunController run = run_begin(seed, 20);
    sts::fuzz::PolicyRng rng(policy_seed);
    StepResult result{};
    sts::fuzz::Move moves[sts::fuzz::kMoveCap]{};

    for (uint32_t action_index = 0; action_index < kMaxRunActions;
         ++action_index) {
        const auto phase = static_cast<RunPhase>(run.phase);
        if (phase == RunPhase::RUN_OVER) {
            terminal_act = run.run.act;
            return true;
        }
        if (phase == RunPhase::ROOM_UNIMPLEMENTED) return false;

        RunActionMask mask{};
        legal_actions(run, mask);
        const std::size_t count =
            sts::fuzz::enumerate_moves(run, mask, moves,
                                      sts::fuzz::kMoveCap);
        if (count == 0) return false;
        const std::size_t pick =
            sts::fuzz::policy_pick(sts::fuzz::PolicyKind::RANDOM, run,
                                   moves, count, rng);
        const Action action = moves[pick].action;

        advance(std::span<RunController>(&run, 1),
                std::span<const Action>(&action, 1),
                std::span<StepResult>(&result, 1));
        ++steps;
    }
    return false;
}

const int kWholeMachineThreads = std::max(
    1, static_cast<int>(std::thread::hardware_concurrency()));

void BM_RandomPolicyFullCombatPerCore(benchmark::State& state) {
    const std::vector<CardId> deck = SkeletonDeck();
    uint64_t combats = 0;
    uint64_t steps = 0;

    for (auto _ : state) {
        const uint64_t sequence = combats + 1;
        const auto seed = static_cast<int64_t>(
            kCombatSeedBase + sequence * kSeedStride);
        const uint64_t policy_seed =
            kPolicySeedBase + sequence * kSeedStride;
        if (!run_full_combat(seed, policy_seed, deck, steps)) {
            state.SkipWithError(
                "random-policy combat failed to reach a terminal state");
            break;
        }
        ++combats;
        benchmark::DoNotOptimize(steps);
    }

    state.counters["combat_steps"] =
        benchmark::Counter(static_cast<double>(steps),
                           benchmark::Counter::kIsRate);
    state.SetItemsProcessed(static_cast<int64_t>(combats));
}

void BM_RandomPolicyFullA20RunWholeMachine(benchmark::State& state) {
    uint64_t random_runs = 0;
    uint64_t steps = 0;
    uint64_t act2_runs = 0;
    uint64_t act3_runs = 0;
    uint64_t terminal_act_sum = 0;
    const uint64_t thread =
        static_cast<uint64_t>(state.thread_index());

    for (auto _ : state) {
        const uint64_t sequence =
            thread + random_runs * static_cast<uint64_t>(kWholeMachineThreads);
        const auto seed = static_cast<int64_t>(
            (sequence % kCompleteRunSeedCorpus) + 1);
        const uint64_t policy_seed = fuzz_policy_seed_for(seed);
        uint8_t terminal_act = 0;
        if (!run_full_a20(seed, policy_seed, steps, terminal_act)) {
            state.SkipWithError(
                "random-policy A20 run failed to reach RUN_OVER");
            return;
        }
        terminal_act_sum += terminal_act;
        if (terminal_act >= 2) ++act2_runs;
        if (terminal_act >= 3) ++act3_runs;
        ++random_runs;
        benchmark::DoNotOptimize(steps);
    }

    // Google Benchmark sums every thread's counter but also sums every
    // thread's elapsed time before applying kIsRate. Its default multithread
    // rate is therefore per-worker throughput. Multiply each worker's count by
    // the worker count so the final quotient is the requested whole-machine
    // aggregate; the source-level aggregation rule is pinned in the README.
    const auto workers = static_cast<uint64_t>(state.threads());
    state.counters["run_steps"] =
        benchmark::Counter(static_cast<double>(steps * workers),
                           benchmark::Counter::kIsRate);
    // Act-reach witnesses, NOT rates: Google Benchmark sums a default-flagged
    // counter across workers and divides it by nothing, so these print as
    // whole-machine COUNTS over the measurement. No worker multiplication
    // here -- that correction exists only because kIsRate divides by the
    // summed per-worker time (see the note above).
    //
    // Four counters rather than two, because a bare `act2_runs=0` is exactly
    // as consistent with "no run left act 1" as with "the probe is dead", and
    // an acceptance report must not have to guess which it read:
    //   runs_counted      the DENOMINATOR. items_per_second is a rate, so the
    //                     printed line otherwise carries no run count to put
    //                     the reach counters over.
    //   terminal_act_sum  sum of terminal acts => mean terminal act. Exactly
    //                     1.000 x runs_counted says every run ended in act 1
    //                     AND that the probe returned a real act rather than
    //                     zeros -- the positive control the reach counters
    //                     lack on their own.
    //   act2_runs/act3_runs  the reach witnesses themselves.
    state.counters["runs_counted"] =
        benchmark::Counter(static_cast<double>(random_runs));
    state.counters["terminal_act_sum"] =
        benchmark::Counter(static_cast<double>(terminal_act_sum));
    state.counters["act2_runs"] =
        benchmark::Counter(static_cast<double>(act2_runs));
    state.counters["act3_runs"] =
        benchmark::Counter(static_cast<double>(act3_runs));
    state.SetItemsProcessed(
        static_cast<int64_t>(random_runs * workers));
}

BENCHMARK(BM_RandomPolicyFullCombatPerCore)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();
BENCHMARK(BM_RandomPolicyFullA20RunWholeMachine)
    ->Threads(kWholeMachineThreads)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

}  // namespace

BENCHMARK_MAIN();
