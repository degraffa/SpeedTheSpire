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
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
# Populate the source only (no add_subdirectory of nlohmann's own CMake), then
# expose its single-header include dir as an INTERFACE target -- same "no
# sub-build, include-only" shape the root uses for xxHash.
FetchContent_GetProperties(nlohmann_json)
if(NOT nlohmann_json_POPULATED)
    FetchContent_Populate(nlohmann_json)
endif()
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
