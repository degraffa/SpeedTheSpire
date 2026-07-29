include(FetchContent)
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.1
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

add_executable(sts_engine_bench
    bench_smoke.cpp
)

target_link_libraries(sts_engine_bench PRIVATE sts_engine benchmark::benchmark_main)
sts_set_warnings(sts_engine_bench)

# A5.1: batch advance() throughput smoke benchmark (10k-state batch, random-legal
# policy). Own executable; bench_advance.cpp carries its own BENCHMARK_MAIN().
add_executable(bench_advance
    bench_advance.cpp
)

target_link_libraries(bench_advance PRIVATE sts_engine benchmark::benchmark)
sts_set_warnings(bench_advance)

# The B side of the advance() legality-guard A/B: the SAME benchmark source
# driving the mask-accepting advance() overload, so the policy's mask is built
# once per step instead of twice. Separate executable because tools/bench_ab.sh
# compares two binaries under one shared filter and needs exactly one
# items_per_second reading from each:
#   tools/wsl_run.sh --script tools/bench_ab.sh \
#       build/release/benchmarks/bench_advance \
#       build/release/benchmarks/bench_advance_mask
add_executable(bench_advance_mask
    bench_advance.cpp
)

target_link_libraries(bench_advance_mask PRIVATE sts_engine benchmark::benchmark)
target_compile_definitions(bench_advance_mask PRIVATE STS_BENCH_REUSE_MASK)
sts_set_warnings(bench_advance_mask)

# B5.5 throughput exit-bar benchmarks. The run benchmark compiles the fuzz
# tool's policy implementation directly so benchmark-only configurations do
# not need to build the fuzz executables, while both consumers still exercise
# one canonical random-legal move enumerator.
add_executable(bench_throughput
    bench_throughput.cpp
    ${PROJECT_SOURCE_DIR}/tools/fuzz/src/policy.cpp
)

target_include_directories(bench_throughput PRIVATE
    ${PROJECT_SOURCE_DIR}/tools/fuzz/include
)
target_link_libraries(bench_throughput PRIVATE
    sts_engine
    benchmark::benchmark
)
sts_set_warnings(bench_throughput)
