# Sim-side self-replay fuzz soak (ledger B5.1; design §7.1(2), stage-a §2).
#
# Shaped like tools/diff_harness: a static library that is the tool's substance
# (policies, the replay-twice guard, coverage, the reproducer format) plus thin
# executables over it. The library is what the gtest links, so every claim the
# soak makes is testable without running a soak.
#
# Two executables, and the split is the point:
#   fuzz_soak   -- sweeps, threads, writes reproducers and in-flight journals.
#   fuzz_repro  -- replays ONE case. It links fuzz_core and the engine and
#                  nothing else, which is what makes a reproducer usable by
#                  someone who does not want to run a fuzzer.
#
# xxhash is linked directly (not reached through sts_engine) because the guard
# hashes the whole RunController, not just the two states engine::hash_state
# covers -- the transient screen-flow fields live only on the controller.
add_library(fuzz_core STATIC
    src/policy.cpp
    src/fuzz_run.cpp
    src/coverage.cpp
    src/repro.cpp
)
target_include_directories(fuzz_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(fuzz_core PUBLIC sts_engine registry_generated PRIVATE xxhash)
target_compile_features(fuzz_core PUBLIC cxx_std_20)
sts_set_warnings(fuzz_core)
sts_enable_sanitizers(fuzz_core)

add_executable(fuzz_soak src/main_soak.cpp)
target_link_libraries(fuzz_soak PRIVATE fuzz_core)
if(STS_ENABLE_SANITIZERS)
    set(STS_FUZZ_SANITIZER_ID "asan-ubsan")
else()
    set(STS_FUZZ_SANITIZER_ID "none")
endif()
target_compile_definitions(fuzz_soak PRIVATE
    STS_FUZZ_BUILD_ID="${CMAKE_BUILD_TYPE}-${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}-${STS_FUZZ_SANITIZER_ID}-schema5-b51fix2-cardgate1-rest1")
sts_set_warnings(fuzz_soak)
sts_enable_sanitizers(fuzz_soak)
find_package(Threads REQUIRED)
target_link_libraries(fuzz_soak PRIVATE Threads::Threads)

add_executable(fuzz_repro src/main_repro.cpp)
target_link_libraries(fuzz_repro PRIVATE fuzz_core)
sts_set_warnings(fuzz_repro)
sts_enable_sanitizers(fuzz_repro)
