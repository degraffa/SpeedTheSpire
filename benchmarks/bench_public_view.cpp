// S3.64: encode_public_view + public_hash per state, on the v7 PublicView
// schema and the gated tree. Not a floor -- no threshold is attached anywhere
// in this file or in run_throughput.sh -- but the training program consumes
// this pair on every decision (docs/training-contract.md), and the only
// existing figure (T1.3's actor spike: 0.57 us encode + 0.40 us hash, "the old
// pin") was measured in the separate training repo against an EARLIER schema.
// This benchmark gives the engine repo its own, current, reproducible number.
//
// The state bank is drawn from real reachable RunController snapshots, not a
// synthetic one: it steps the SAME fixed 1,000-seed random-policy corpus
// bench_throughput.cpp uses (identical seed range, identical policy-seed
// mixing function -- duplicated here rather than shared, matching that file's
// own note on why the two throughput binaries do not share a header for it),
// snapshotting the controller every few run-level steps so the bank mixes
// map/screen phases with in-combat states rather than measuring only one.
//
// Two benchmarks, not one, because T1.3's own breakdown reports encode and
// hash as separate figures and this should stay comparable to that shape:
// BM_EncodePublicView times encode_public_view() alone; BM_PublicHash times
// public_hash(const PublicView&) alone, over views already encoded once
// outside the timed loop (mirroring the RunController overload's own two-step
// shape without re-paying the encode inside the hash timing).

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <benchmark/benchmark.h>

#include "sts/engine/advance.hpp"
#include "sts/engine/public_view.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/types.hpp"
#include "sts/fuzz/policy.hpp"

namespace {

using namespace sts::engine;

constexpr uint32_t kMaxRunActions = 20000;
constexpr uint64_t kSeedCorpus = 1000;
constexpr uint32_t kSnapshotEvery = 5;
constexpr std::size_t kBankTarget = 4096;

// Identical mixing function to bench_throughput.cpp's fuzz_policy_seed_for --
// duplicated, not shared, for the same reason that file gives: one source
// file per binary, no header either binary needs beyond the engine's own.
uint64_t fuzz_policy_seed_for(int64_t seed) noexcept {
    uint64_t z = static_cast<uint64_t>(seed) * 0x9E3779B97F4A7C15ull;
    z ^= (static_cast<uint64_t>(sts::fuzz::PolicyKind::RANDOM) + 1) *
         0xD1B54A32D192ED03ull;
    z ^= 0xA24BAED4963EE407ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Steps the fixed random-policy corpus, snapshotting the RunController every
// kSnapshotEvery run-level actions, until `target` snapshots are collected or
// the corpus is exhausted. Stops a trajectory at RUN_OVER or
// ROOM_UNIMPLEMENTED like bench_throughput's own run loop.
std::vector<RunController> BuildSnapshotBank(std::size_t target) {
    std::vector<RunController> bank;
    bank.reserve(target);
    sts::fuzz::Move moves[sts::fuzz::kMoveCap]{};

    for (uint64_t seed = 1; seed <= kSeedCorpus && bank.size() < target;
         ++seed) {
        RunController run = run_begin(static_cast<int64_t>(seed), 20);
        sts::fuzz::PolicyRng rng(
            fuzz_policy_seed_for(static_cast<int64_t>(seed)));
        StepResult result{};

        for (uint32_t step = 0;
             step < kMaxRunActions && bank.size() < target; ++step) {
            const auto phase = static_cast<RunPhase>(run.phase);
            if (phase == RunPhase::RUN_OVER ||
                phase == RunPhase::ROOM_UNIMPLEMENTED) {
                break;
            }
            if (step % kSnapshotEvery == 0) {
                bank.push_back(run);
            }

            RunActionMask mask{};
            legal_actions(run, mask);
            const std::size_t count =
                sts::fuzz::enumerate_moves(run, mask, moves,
                                           sts::fuzz::kMoveCap);
            if (count == 0) break;
            const std::size_t pick =
                sts::fuzz::policy_pick(sts::fuzz::PolicyKind::RANDOM, run,
                                       moves, count, rng);
            const Action action = moves[pick].action;

            advance(std::span<RunController>(&run, 1),
                    std::span<const Action>(&action, 1),
                    std::span<StepResult>(&result, 1));
        }
    }
    return bank;
}

const std::vector<RunController>& SnapshotBank() {
    static const std::vector<RunController> bank = BuildSnapshotBank(kBankTarget);
    return bank;
}

void BM_EncodePublicView(benchmark::State& state) {
    const std::vector<RunController>& bank = SnapshotBank();
    std::vector<PublicView> views(bank.size());
    std::size_t encoded = 0;

    for (auto _ : state) {
        for (std::size_t i = 0; i < bank.size(); ++i) {
            encode_public_view(bank[i], views[i]);
        }
        benchmark::DoNotOptimize(views.data());
        benchmark::ClobberMemory();
        encoded += bank.size();
    }

    state.SetItemsProcessed(static_cast<int64_t>(encoded));
}
BENCHMARK(BM_EncodePublicView)->Unit(benchmark::kMicrosecond);

void BM_PublicHash(benchmark::State& state) {
    const std::vector<RunController>& bank = SnapshotBank();
    std::vector<PublicView> views(bank.size());
    for (std::size_t i = 0; i < bank.size(); ++i) {
        encode_public_view(bank[i], views[i]);
    }

    uint64_t sink = 0;
    std::size_t hashed = 0;
    for (auto _ : state) {
        for (const PublicView& view : views) {
            sink ^= public_hash(view);
        }
        benchmark::DoNotOptimize(sink);
        hashed += views.size();
    }

    state.SetItemsProcessed(static_cast<int64_t>(hashed));
}
BENCHMARK(BM_PublicHash)->Unit(benchmark::kMicrosecond);

}  // namespace

BENCHMARK_MAIN();
