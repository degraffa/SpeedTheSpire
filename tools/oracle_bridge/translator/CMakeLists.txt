# B1.5 translator (Stage B design §2.6): campaign JSONL -> RunState/CombatState.
#
# nlohmann/json is granted here as a tools-ONLY dependency (design §2.6 / the
# working agreements). It is fetched header-only and exposed as an INTERFACE
# target (mirroring the xxHash pattern in the root CMakeLists) so there is NO
# sub-build, and it is linked PRIVATE into oracle_translator only. sts_engine
# links neither oracle_translator nor nlohmann_json, so the JSON header can never
# reach the engine / hot path -- the grant's core constraint.
#
# This directory is added under STS_BUILD_TESTS (next to diff_harness): the
# translator's consumer today is the WSL gtest translator_test.

include(FetchContent)
# Populate the source only (no add_subdirectory of nlohmann's own CMake), then
# expose its single-header include dir as an INTERFACE target -- same "no
# sub-build, include-only" shape the root uses for xxHash.
#
# xxHash gets that for free: its repo has no top-level CMakeLists.txt, so plain
# FetchContent_MakeAvailable() populates and stops. nlohmann/json *does* have
# one, so MakeAvailable would add_subdirectory() it and give us the sub-build
# this grant is explicitly shaped to avoid (it would also give the dependency a
# build tree, which is what disqualifies a dependency from the root's shared
# checkout cache -- see STS_SHARED_DEP_CACHE).
#
# SOURCE_SUBDIR is the supported way to suppress that: MakeAvailable only calls
# add_subdirectory() when <src>/<SOURCE_SUBDIR>/CMakeLists.txt exists, and the
# docs (CMake >= 3.18) explicitly bless pointing it at a directory that does not
# exist "to avoid adding a project that contains a CMakeLists.txt file in its
# top directory". SOURCE_SUBDIR is inert in the ExternalProject sub-build the
# populate step runs, so it changes nothing about what is checked out.
#
# This replaces the deprecated single-argument FetchContent_Populate(), which
# policy CMP0169 (CMake 3.30) deprecates and which is slated to become an error.
# The dev env is pinned at 3.28 where the old form still works, but the new form
# works there too -- it is the same populate, just spelled the supported way.
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    SOURCE_SUBDIR  sts-populate-only-do-not-add-subdirectory
)
FetchContent_MakeAvailable(nlohmann_json)
add_library(nlohmann_json INTERFACE)
# SYSTEM so the third-party header does not trip our -Wconversion/-Wshadow wall.
target_include_directories(nlohmann_json SYSTEM INTERFACE "${nlohmann_json_SOURCE_DIR}/single_include")

# The translator library: pure JSON->schema translation + the disposition table.
add_library(oracle_translator STATIC
    src/translate.cpp
)
target_include_directories(oracle_translator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# diff_harness (PUBLIC-links sts_engine) gives write_trace + the engine's POD
# schema headers + (transitively) the generated registry include dir; registry_
# generated is linked explicitly for the game_ids.hpp join tables the engine
# itself does not include. nlohmann is PRIVATE (grant compliance).
target_link_libraries(oracle_translator
    PUBLIC  diff_harness
    PUBLIC  registry_generated
    PRIVATE nlohmann_json)
add_dependencies(oracle_translator registry_gen_headers)
target_compile_features(oracle_translator PUBLIC cxx_std_20)

sts_set_warnings(oracle_translator)
sts_enable_sanitizers(oracle_translator)

# CLI front-end: run the translator over real campaign artifacts and print
# per-run disposition stats (used for the B1.5 corpus report; not a CI test).
add_executable(translate_cli src/main.cpp)
target_link_libraries(translate_cli PRIVATE oracle_translator)
sts_set_warnings(translate_cli)
sts_enable_sanitizers(translate_cli)

# G4 gate verifier: presence + RNG cross-check of campaign artifacts against the
# engine's tier-1-tested floor_stream/map_stream. Links diff_harness for the
# engine's POD RNG headers and nlohmann for JSONL parsing (tools-only grant).
# Reads the uncommitted §7.3 corpus at runtime; not a CI test.
add_executable(oracle_gate_check src/oracle_gate_check.cpp)
target_link_libraries(oracle_gate_check PRIVATE diff_harness nlohmann_json)
add_dependencies(oracle_gate_check registry_gen_headers)
sts_set_warnings(oracle_gate_check)
sts_enable_sanitizers(oracle_gate_check)
