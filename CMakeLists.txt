cmake_minimum_required(VERSION 3.21)
project(SpeedTheSpire CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

option(STS_BUILD_TESTS "Build unit tests" ON)
option(STS_BUILD_BENCHMARKS "Build benchmarks" ON)
option(STS_ENABLE_SANITIZERS "Build with ASan/UBSan" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

# xxHash (A2.2): header-only XXH3 for state content hashing (design doc §4.1).
# Pre-approved as A2.2's sole new dependency by the Stage A working agreements.
# The repo's top-level has no CMakeLists, so FetchContent only populates the
# source tree; we expose it as an INTERFACE (include-only) target and let
# state_hash.cpp compile it with XXH_INLINE_ALL. No sub-build, no link step.
include(FetchContent)
FetchContent_Declare(
    xxhash
    GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
    GIT_TAG v0.8.2
)
FetchContent_MakeAvailable(xxhash)
add_library(xxhash INTERFACE)
target_include_directories(xxhash INTERFACE ${xxhash_SOURCE_DIR})

# Registry codegen (B2.1/B2.2). Runs gen.py at build time to emit the constexpr
# registry headers under <build>/generated (never committed). The engine's id
# enums and card/monster tables ARE these generated headers (the skeleton
# migration, Stage B design §4.4), so this must precede src/engine and is
# unconditional: every build of sts_engine needs the generator (Python 3 +
# PyYAML).
add_subdirectory(tools/registry_gen)

add_subdirectory(src/engine)

# A6.1: diff harness (trace format + differ + reproducer + fixture oracle). A
# normal C++ static-library target linked by the differ_test gtest binary (and a
# future CLI). Built whenever tests are, since that is its current consumer.
if(STS_BUILD_TESTS)
    add_subdirectory(tools/diff_harness)
    # A6.2: combat-fixture generator (independent reference simulator). Built with
    # the tests since it links diff_harness (write_trace) + sts_engine and is run
    # manually to regenerate tests/golden/combat_fixtures/*.trace.
    add_subdirectory(tools/fixture_gen)
    # B1.5: JSON->schema translator (campaign JSONL -> RunState/CombatState). Links
    # diff_harness (write_trace) + nlohmann/json (tools-only grant, PRIVATE); its
    # consumer is the WSL translator_test gtest.
    add_subdirectory(tools/oracle_bridge/translator)
endif()

if(STS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(STS_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
