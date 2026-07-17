// Smoke benchmark for the batch API (A5.1, design doc §7): advance() throughput
// over a 10k-state batch under a random-legal policy. No perf target at M1 (the
// ledger records the number as a baseline only). This is a smoke benchmark, not
// a rigorous perf suite -- perf hardening is Stage C.
//
// The action-selection RNG here (std::mt19937) is BENCHMARK-HARNESS RNG only:
// it picks which legal action to feed each state. It is NOT gameplay RNG (that
// is the bit-exact RngStream inside each CombatState) and carries no
// bit-exactness requirement.

#include <array>
#include <cstdint>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

#include "sts/engine/advance.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace {

using namespace sts::engine;

// The M1 skeleton deck (design doc §9): 5x Strike, 4x Defend, 1x Bash,
// 1x Shrug It Off, 1x Pommel Strike.
std::vector<CardId> SkeletonDeck() {
    std::vector<CardId> deck;
    for (int i = 0; i < 5; ++i) deck.push_back(CardId::STRIKE);
    for (int i = 0; i < 4; ++i) deck.push_back(CardId::DEFEND);
    deck.push_back(CardId::BASH);
    deck.push_back(CardId::SHRUG_IT_OFF);
    deck.push_back(CardId::POMMEL_STRIKE);
    return deck;
}

constexpr std::size_t kBatch = 10000;

// Pick a random legal Action for `s` using `rng` (harness RNG). Collects the
// legal choices (each playable hand slot + END_TURN) and picks one uniformly.
// If the state is terminal / has no legal play, returns END_TURN (a harmless
// no-op-ish action that keeps the loop uniform).
Action RandomLegalAction(const CombatState& s, std::mt19937& rng) {
    ActionMask mask{};
    legal_actions(s, mask);

    // At most kHandCap plays + 1 end-turn.
    std::array<Action, kHandCap + 1> choices{};
    int count = 0;
    for (int i = 0; i < kHandCap; ++i) {
        if (mask.can_play[i]) {
            // Single monster in the skeleton -> target slot 0.
            choices[count++] =
                make_action(ActionVerb::PLAY_CARD, static_cast<uint8_t>(i), 0, 0);
        }
    }
    if (mask.can_end_turn) {
        choices[count++] = make_action(ActionVerb::END_TURN);
    }
    if (count == 0) {
        return make_action(ActionVerb::END_TURN);
    }
    std::uniform_int_distribution<int> pick(0, count - 1);
    return choices[pick(rng)];
}

void BM_AdvanceBatch(benchmark::State& state) {
    const std::vector<CardId> deck = SkeletonDeck();

    // 10k distinct combats (vary the seed per state so they are not identical).
    std::vector<CombatState> states;
    states.reserve(kBatch);
    for (std::size_t i = 0; i < kBatch; ++i) {
        states.push_back(combat_begin(static_cast<int64_t>(1000 + i), /*floor=*/1,
                                      std::span<const CardId>(deck)));
    }

    std::vector<Action> actions(kBatch);
    std::vector<StepResult> results(kBatch);
    std::mt19937 rng(0xC0FFEE);

    std::size_t steps = 0;
    for (auto _ : state) {
        // Choose a random legal action per state (not timed-out of the loop --
        // this IS part of a realistic step, kept simple).
        for (std::size_t i = 0; i < kBatch; ++i) {
            actions[i] = RandomLegalAction(states[i], rng);
        }
        advance(std::span<CombatState>(states), std::span<const Action>(actions),
                std::span<StepResult>(results));
        benchmark::DoNotOptimize(results.data());
        benchmark::ClobberMemory();
        steps += kBatch;
    }

    // steps/sec via the framework's counters (rate counter).
    state.counters["steps"] =
        benchmark::Counter(static_cast<double>(steps), benchmark::Counter::kIsRate);
    state.SetItemsProcessed(static_cast<int64_t>(steps));
}

BENCHMARK(BM_AdvanceBatch)->Unit(benchmark::kMillisecond);

}  // namespace

BENCHMARK_MAIN();
