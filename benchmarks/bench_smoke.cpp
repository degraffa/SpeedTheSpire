#include "sts/engine/version.hpp"

#include <benchmark/benchmark.h>

static void BM_VersionString(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(sts::engine::VersionString());
    }
}
BENCHMARK(BM_VersionString);

BENCHMARK_MAIN();
