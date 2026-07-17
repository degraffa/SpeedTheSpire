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
