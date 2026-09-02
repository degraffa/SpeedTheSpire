# Twin fixtures (ledger T0.5): the container the training repo consumes, plus
# the generator that writes tests/golden/twin_fixtures/twins_v1.bin.
#
# Shaped like tools/diff_harness and tools/fuzz: a static library holding the
# substance (the format, its refusal rules, replay-based rebuild and the
# self-check) plus one thin executable over it. The library is what
# tests/twin_fixture_test links, so every claim the format makes is testable
# without running the generator.
add_library(twin_fixture STATIC src/twin_fixture.cpp)
target_include_directories(twin_fixture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(twin_fixture PUBLIC sts_engine)
target_compile_features(twin_fixture PUBLIC cxx_std_20)
sts_set_warnings(twin_fixture)
sts_enable_sanitizers(twin_fixture)

# The generator is run by hand to regenerate the committed fixture, exactly like
# tools/fixture_gen/gen_combat_fixtures. It links fuzz_core for the run loop
# rather than forking the policy loop (fuzz_run.hpp).
add_executable(gen_twin_fixtures src/gen_twin_fixtures.cpp)
target_link_libraries(gen_twin_fixtures PRIVATE twin_fixture fuzz_core)
target_compile_definitions(gen_twin_fixtures PRIVATE
    STS_TWIN_FIXTURE_OUT="${PROJECT_SOURCE_DIR}/tests/golden/twin_fixtures")
sts_set_warnings(gen_twin_fixtures)
sts_enable_sanitizers(gen_twin_fixtures)
