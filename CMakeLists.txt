cmake_minimum_required(VERSION 3.21)
project(SpeedTheSpire CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# Windows has no RPATH: the loader finds a DLL next to the .exe, on PATH, or not
# at all. googletest is deliberately built SHARED here (see tests/CMakeLists.txt
# -- it takes relinking ~39 test executables from ~33s to ~9.5s), so on Windows
# every test binary needs gtest.dll beside it. Without this, the executables link
# fine and then fail at startup with 0xc0000135 (STATUS_DLL_NOT_FOUND), which
# surfaces through ctest as the thoroughly unhelpful
#
#   discover_tests failed to run command: ...
#   execute_process Abnormal exit: Exit code 0xc0000135
#
# i.e. every test reported as a discovery failure rather than a missing DLL.
# Pointing all runtime artifacts at one directory is the standard fix and keeps
# gtest's shared-link benefit on both platforms.
#
# Scoped to WIN32 deliberately: on Linux the layout is load-bearing for tooling
# that addresses binaries by path (tools/fixture_gen/gen_combat_fixtures among
# them), and ELF RPATH already solves this, so there is nothing to fix there.
if(WIN32)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

option(STS_BUILD_TESTS "Build unit tests" ON)
# Default OFF: benchmarks are not part of any test run, but building them means
# fetching and building google-benchmark, which measured 50.9s vs 21.2s of
# configure time. Almost every local build is test-focused and pays that for
# nothing. Turn it on explicitly (-DSTS_BUILD_BENCHMARKS=ON) to build or run
# them. CI passes it explicitly on every job so the benchmark sources keep their
# compile coverage -- do not remove it there without replacing that coverage.
option(STS_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(STS_ENABLE_SANITIZERS "Build with ASan/UBSan" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)
include(StsAddTest)
include(StsFloatingPoint)
include(StsCompilerCache)

# Pin the floating-point contract BEFORE any add_subdirectory / FetchContent, so
# it covers every target in the tree including the header-only float code that
# is compiled with each consumer's flags. See cmake/StsFloatingPoint.cmake for
# why this one is directory-wide while sts_set_warnings is deliberately not, and
# tests/fp_contract_test.cpp for the control that proves it took effect.
sts_apply_fp_contract()

# Also before any target is created: the compiler launcher is read at
# target-creation time, so setting it here is what lets it reach the
# FetchContent dependencies (googletest) as well as our own targets.
sts_setup_compiler_cache()

# Windows sanitizer mechanics (/RTC1 strip + ASan runtime DLL staging). No-op on
# POSIX and when sanitizers are off. Must run after CMAKE_RUNTIME_OUTPUT_DIRECTORY
# is set above, since it copies the DLL there.
sts_prepare_sanitizer_runtime()

include(FetchContent)

# ---------------------------------------------------------------------------
# Shared FetchContent checkout cache
#
# Every preset configures into its own build tree, so by default each of debug,
# asan and release re-clones every FetchContent dependency into <build>/_deps --
# up to 12 clones for one full three-preset cycle, and the clones dominate a
# clean configure.
#
# FETCHCONTENT_BASE_DIR can move those checkouts to a shared location, but it
# moves the dependency's *build* tree with it: SOURCE_DIR and BINARY_DIR are
# both baked in at FetchContent_Declare() time from whatever FETCHCONTENT_BASE_DIR
# is in effect, so a single shared base dir puts <cache>/<dep>-build in every
# preset. Measured on CMake 3.28.3: an asan tree and a release tree configured
# with a common FETCHCONTENT_BASE_DIR both emit
#   <cache>/googletest-build/googletest/CMakeFiles/gtest.dir/src/gtest-all.cc.o
# and the same collision for benchmark -- i.e. an ASan-instrumented libgtest.so
# would land at the exact path the release link line reads. That is the one
# outcome a build cache must never have, and CMake offers no supported way to
# redirect only the source dir of a dependency declared in another directory.
#
# So the cache is scoped to the dependencies that have *no sub-build at all*:
# xxHash and nlohmann/json are populated only (never add_subdirectory'd) and are
# consumed as INTERFACE include directories, compiled by our own targets with
# our own flags. Sharing their checkouts therefore cannot carry a build artifact
# -- let alone a sanitizer-instrumented one -- between presets.
#
# Size note: this scoping used to also capture the bulk of the bytes (~290 MB of
# ~315 MB), because nlohmann/json was a 278 MB git clone. It is now fetched as a
# 293 KB release asset, so the shared pair is ~13 MB (xxHash 11 MB + nlohmann
# 1.9 MB) of ~38 MB total, against googletest 20 MB + benchmark 5.2 MB that stay
# per-build-tree. The cache is therefore worth much less in bytes than it was;
# what it still buys is not re-running git clone for xxHash once per preset.
#
# googletest and benchmark keep a private per-build-tree _deps. They are
# declared in tests/ and benchmarks/, which stay on the default base dir.
option(STS_SHARED_DEP_CACHE
    "Share the sub-build-free FetchContent checkouts (xxHash, nlohmann/json) across build trees" ON)
# The cache is partitioned by HOST SYSTEM, and that is not tidiness -- it is
# required for the WSL and native-Windows toolchains to coexist on one checkout.
#
# Both hosts reach the SAME physical directory under a different absolute-path
# spelling: WSL sees /mnt/d/STS_BG_Mod/... and Windows sees D:/STS_BG_Mod/...
# FetchContent's sub-build bakes its own path into the generated
# <dep>-subbuild/CMakeCache.txt, and CMake then refuses to reuse a cache
# directory whose recorded path is not the one it was invoked with:
#
#   CMake Error: The current CMakeCache.txt directory
#   D:/.../.cache/deps/xxhash-subbuild/CMakeCache.txt is different than the
#   directory /mnt/d/.../.cache/deps/xxhash-subbuild where CMakeCache.txt was
#   created.
#
# That is a hard configure failure, not a cache miss, so a single shared
# directory means whichever toolchain configured second simply cannot build.
# Splitting by CMAKE_HOST_SYSTEM_NAME keeps both sides cached and independent;
# the cost is one extra checkout per host, of a ~13 MB pair.
set(STS_DEP_CACHE_DIR "${CMAKE_SOURCE_DIR}/.cache/deps/${CMAKE_HOST_SYSTEM_NAME}"
    CACHE PATH
    "Location of the shared FetchContent checkout cache (see STS_SHARED_DEP_CACHE)")

if(STS_SHARED_DEP_CACHE)
    file(MAKE_DIRECTORY "${STS_DEP_CACHE_DIR}")
    # Two presets configured at the same time (an agent running debug and asan
    # in parallel, a CI matrix sharing a checkout) would otherwise both drive git
    # into the same working tree. GUARD PROCESS is an OS-level lock released when
    # this cmake process exits, so it cannot go stale; the loser simply waits.
    file(LOCK "${STS_DEP_CACHE_DIR}/.configure.lock"
        GUARD PROCESS TIMEOUT 900 RESULT_VARIABLE _sts_dep_cache_lock)
    if(NOT _sts_dep_cache_lock STREQUAL "0")
        message(WARNING
            "Could not lock the shared dependency cache at ${STS_DEP_CACHE_DIR} "
            "(${_sts_dep_cache_lock}); falling back to a private per-build-tree _deps.")
        set(STS_SHARED_DEP_CACHE OFF)
    endif()
endif()

# include(FetchContent) sets FETCHCONTENT_BASE_DIR as a cache variable pointing at
# ${CMAKE_BINARY_DIR}/_deps. Setting a *normal* variable of the same name shadows
# it here and in every add_subdirectory() below; unset() drops the shadow and
# reveals the per-build-tree default again. Both macros must bracket every
# declare/populate of a shared dependency -- nothing else.
macro(sts_shared_deps_begin)
    if(STS_SHARED_DEP_CACHE)
        set(FETCHCONTENT_BASE_DIR "${STS_DEP_CACHE_DIR}")
    endif()
endmacro()
macro(sts_shared_deps_end)
    unset(FETCHCONTENT_BASE_DIR)
endmacro()

# xxHash (A2.2): header-only XXH3 for state content hashing (design doc §4.1).
# Pre-approved as A2.2's sole new dependency by the Stage A working agreements.
# The repo's top-level has no CMakeLists, so FetchContent only populates the
# source tree; we expose it as an INTERFACE (include-only) target and let
# state_hash.cpp compile it with XXH_INLINE_ALL. No sub-build, no link step --
# which is exactly why its checkout is safe to share (see above).
sts_shared_deps_begin()
FetchContent_Declare(
    xxhash
    GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
    GIT_TAG v0.8.2
)
FetchContent_MakeAvailable(xxhash)
sts_shared_deps_end()
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
    # B5.1: sim-side self-replay fuzz soak -- policies, the replay-twice
    # determinism guard, coverage accounting, the STSFUZZ reproducer format,
    # and the fuzz_soak / fuzz_repro executables. Built with the tests because
    # fuzz_test links its static library; the soak itself is a campaign driven
    # by tools/fuzz/soak.sh, never by ctest.
    add_subdirectory(tools/fuzz)
    # B1.5: JSON->schema translator (campaign JSONL -> RunState/CombatState). Links
    # diff_harness (write_trace) + nlohmann/json (tools-only grant, PRIVATE); its
    # consumer is the WSL translator_test gtest.
    #
    # Bracketed by the shared-checkout macros because this directory's only
    # FetchContent use is nlohmann/json, which it populates *without* an
    # add_subdirectory (single-header, exposed as an INTERFACE include dir), so it
    # has no build tree to share. It used to be the single most expensive
    # dependency in the project at 278 MB of clone; it is now fetched as the
    # 293 KB upstream include.zip release asset instead (see that file).
    sts_shared_deps_begin()
    add_subdirectory(tools/oracle_bridge/translator)
    sts_shared_deps_end()
endif()

if(STS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(STS_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
