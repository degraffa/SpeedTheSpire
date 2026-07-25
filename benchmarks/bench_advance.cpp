// Smoke benchmark for the batch API (design doc §7): advance() throughput over a
// 10k-state batch under a random-legal policy. No perf target here -- this is a
// smoke benchmark, not a rigorous perf suite.
//
// The action-selection RNG here (std::mt19937) is BENCHMARK-HARNESS RNG only:
// it picks which legal action to feed each state. It is NOT gameplay RNG (that
// is the bit-exact RngStream inside each CombatState) and carries no
// bit-exactness requirement.
//
// TWO BINARIES FROM THIS ONE SOURCE (benchmarks/CMakeLists.txt):
//
//   bench_advance        the three-span advance(), which builds the legality
//                        mask itself -- so the mask is built TWICE per step,
//                        once by the policy to choose an action and once inside
//                        advance() to check it.
//   bench_advance_mask   built with -DSTS_BENCH_REUSE_MASK: the four-span
//                        overload, handed the very mask the policy just built.
//
// Two executables rather than two BENCHMARK() registrations in one, because
// tools/bench_ab.sh compares two binaries under a SHARED --benchmark_filter and
// requires exactly one items_per_second reading per run; one benchmark per
// binary is the only shape that satisfies both. The action-choosing policy
// (PickFromMask) is shared verbatim, so the single difference between A and B is
// who owns the mask -- which is exactly the thing being measured.

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
//
// When the mask offers nothing -- which is what a TERMINAL state looks like,
// and every state ends up there long before a 3s run is over -- this returns
// END_TURN and keeps feeding it. That is deliberate, and it is deliberately NOT
// compacted away: this is the batch-API usage pattern that matters (keep the
// batch uniform, keep stepping finished combats) and it is exactly the pattern
// that used to corrupt memory. advance() appended an end-turn sentinel to
// card_queue without checking legality, while pump_step short-circuits to
// COMBAT_OVER before it ever reaches the card-queue step, so nothing drained it:
// kCardQueueCap steps filled the array and the next one wrote past its end
// (assert in Debug, silent neighbour corruption in Release). advance() now
// rejects an action its own legal_actions() does not report, so feeding a
// terminal state is genuinely a no-op -- and this loop is the only place in the
// tree that exercises it at scale, since CI builds the benchmarks but never runs
// them. Keeping the terminal states in the batch is therefore the point of the
// harness, not an oversight: a re-broken guard aborts this binary.
//
// (The comment this replaces called it "a harmless no-op-ish action that keeps
// the loop uniform". It was neither harmless nor a no-op.)
//
// Split into "choose from a mask" and "build a mask, then choose" so both
// variants below run the IDENTICAL policy: the mask-reuse variant keeps the mask
// it built instead of dropping it, and nothing else about the loop changes.
Action PickFromMask(const ActionMask& mask, std::mt19937& rng) {
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

#ifndef STS_BENCH_REUSE_MASK
// Build-then-choose, dropping the mask on the floor -- which is precisely why
// the three-span advance() has to build its own. (Compiled out of the reuse
// variant so it is not an unused function there.)
Action RandomLegalAction(const CombatState& s, std::mt19937& rng) {
    ActionMask mask{};
    legal_actions(s, mask);
    return PickFromMask(mask, rng);
}
#endif

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
#ifdef STS_BENCH_REUSE_MASK
    // Allocated ONCE, outside the timed loop: a real search loop owns this
    // buffer for the life of the batch, and the thing under measurement is the
    // saved mask rebuild, not an allocation.
    std::vector<ActionMask> masks(kBatch);
#endif

    std::size_t steps = 0;
    for (auto _ : state) {
        // Choose a random legal action per state (not timed-out of the loop --
        // this IS part of a realistic step, kept simple).
        for (std::size_t i = 0; i < kBatch; ++i) {
#ifdef STS_BENCH_REUSE_MASK
            // The policy builds the mask and KEEPS it; advance() is then told
            // not to build its own.
            legal_actions(states[i], masks[i]);
            actions[i] = PickFromMask(masks[i], rng);
#else
            actions[i] = RandomLegalAction(states[i], rng);
#endif
        }
#ifdef STS_BENCH_REUSE_MASK
        advance(std::span<CombatState>(states), std::span<const Action>(actions),
                std::span<StepResult>(results),
                std::span<const ActionMask>(masks));
#else
        advance(std::span<CombatState>(states), std::span<const Action>(actions),
                std::span<StepResult>(results));
#endif
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
