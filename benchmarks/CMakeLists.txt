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
# not need to build the fuzz executables (tools/fuzz is gated behind
# STS_BUILD_TESTS, so its fuzz_core library target may not exist here), while
# both consumers still exercise one canonical random-legal move enumerator.
#
# S3.64 finding: policy.cpp alone stopped being link-complete on its own once
# S2.V2 added PolicyKind::SIM_SEARCH -- policy_pick's dispatch switch calls
# sim_search_pick() (tools/fuzz/src/policy_search.cpp) unconditionally, a
# reference only -O3 interprocedural optimization proves dead (this
# benchmark's own corpus never selects SIM_SEARCH) and therefore only the
# release preset's LTO build was eliding. debug/asan (no IPO) failed to link
# `undefined reference to sts::fuzz::sim_search_pick` the first time either
# preset was built with -DSTS_BUILD_BENCHMARKS=ON after that policy landed --
# nothing had exercised the combination before. policy_search.cpp needs no
# link library beyond sts_engine (checked against its own #include list), so
# adding the one extra translation unit is the whole fix; it does not reach
# for fuzz_core, registry_generated or xxhash.
add_executable(bench_throughput
    bench_throughput.cpp
    ${PROJECT_SOURCE_DIR}/tools/fuzz/src/policy.cpp
    ${PROJECT_SOURCE_DIR}/tools/fuzz/src/policy_search.cpp
)

target_include_directories(bench_throughput PRIVATE
    ${PROJECT_SOURCE_DIR}/tools/fuzz/include
)
target_link_libraries(bench_throughput PRIVATE
    sts_engine
    benchmark::benchmark
)
sts_set_warnings(bench_throughput)

# S3.64: encode_public_view() + public_hash() per state on the v7 PublicView
# schema -- the training program's per-decision observation cost, not a floor.
# Same fixed random-policy corpus scheme as bench_throughput (own copy of the
# policy-seed mixing function, same reasoning: one source file per binary),
# and the same policy_search.cpp requirement as bench_throughput above for the
# same reason -- this target compiles policy.cpp fresh too.
add_executable(bench_public_view
    bench_public_view.cpp
    ${PROJECT_SOURCE_DIR}/tools/fuzz/src/policy.cpp
    ${PROJECT_SOURCE_DIR}/tools/fuzz/src/policy_search.cpp
)

target_include_directories(bench_public_view PRIVATE
    ${PROJECT_SOURCE_DIR}/tools/fuzz/include
)
target_link_libraries(bench_public_view PRIVATE
    sts_engine
    benchmark::benchmark
)
sts_set_warnings(bench_public_view)
