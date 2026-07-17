include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(sts_engine_tests
    test_smoke.cpp
)

target_link_libraries(sts_engine_tests PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(sts_engine_tests)
sts_enable_sanitizers(sts_engine_tests)

include(GoogleTest)
gtest_discover_tests(sts_engine_tests)

# A1.1: RandomXS128 (xorshift128+) bit-exactness suite. Separate executable
# target per the Stage A task ledger's acceptance line (gtest
# `rng_xs128_test`), not appended to sts_engine_tests.
add_executable(rng_xs128_test
    rng_xs128_test.cpp
)

target_link_libraries(rng_xs128_test PRIVATE sts_engine GTest::gtest_main)
target_compile_definitions(rng_xs128_test PRIVATE STS_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden")
sts_set_warnings(rng_xs128_test)
sts_enable_sanitizers(rng_xs128_test)

gtest_discover_tests(rng_xs128_test)

# A1.2: JDK LCG + Collections.shuffle bit-exactness suite.
add_executable(rng_jdk_test
    rng_jdk_test.cpp
)

target_link_libraries(rng_jdk_test PRIVATE sts_engine GTest::gtest_main)
target_compile_definitions(rng_jdk_test PRIVATE STS_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
sts_set_warnings(rng_jdk_test)
sts_enable_sanitizers(rng_jdk_test)

gtest_discover_tests(rng_jdk_test)

# A1.4: SeedHelper base-35 string<->long conversion suite.
add_executable(seed_string_test
    seed_string_test.cpp
)

target_link_libraries(seed_string_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(seed_string_test)
sts_enable_sanitizers(seed_string_test)
target_compile_definitions(seed_string_test PRIVATE STS_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden")

gtest_discover_tests(seed_string_test)

# A1.3: game Random wrapper (RngStream) bit-exactness suite -- golden sets 2, 3, 5.
add_executable(rng_stream_test
    rng_stream_test.cpp
)

target_link_libraries(rng_stream_test PRIVATE sts_engine GTest::gtest_main)
target_compile_definitions(rng_stream_test PRIVATE STS_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden")
sts_set_warnings(rng_stream_test)
sts_enable_sanitizers(rng_stream_test)

gtest_discover_tests(rng_stream_test)

# A2.1: core id enums + CardInstance/PowerSlot/Action scaffolding suite.
add_executable(types_test
    types_test.cpp
)

target_link_libraries(types_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(types_test)
sts_enable_sanitizers(types_test)

gtest_discover_tests(types_test)

# A2.2: CombatState/RunState layout + state-hash/snapshot suite. Links
# sts_engine, which now carries state_hash.cpp (and its private xxhash dep).
add_executable(state_test
    state_test.cpp
)

target_link_libraries(state_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(state_test)
sts_enable_sanitizers(state_test)

gtest_discover_tests(state_test)

# A5.2: observation encoder stub (ObsBuffer + encode_observation) round-trip
# spot checks + zero-allocation check (TU-local counting operator new/delete).
add_executable(observation_test
    observation_test.cpp
)

target_link_libraries(observation_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(observation_test)
sts_enable_sanitizers(observation_test)

gtest_discover_tests(observation_test)

# A3.1: action-queue pump + queue mechanics suite (design doc §5.1-§5.2, trap 9).
# Links sts_engine, which now carries action_queue.cpp.
add_executable(action_queue_test
    action_queue_test.cpp
)

target_link_libraries(action_queue_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(action_queue_test)
sts_enable_sanitizers(action_queue_test)

gtest_discover_tests(action_queue_test)

# A3.2: Jaw Worm AI + monster turn. Compares the engine against the hand-derived
# fixture (tests/fixtures/jaw_worm_fixture.tsv, gen by gen_jaw_worm_fixture.py).
# STS_FIXTURE_DIR points at tests/fixtures (distinct from tests/golden, which
# holds JVM-captured vectors -- this is a hand-derived oracle, not a golden file).
add_executable(jaw_worm_test
    jaw_worm_test.cpp
)

target_link_libraries(jaw_worm_test PRIVATE sts_engine GTest::gtest_main)
target_compile_definitions(jaw_worm_test PRIVATE STS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
sts_set_warnings(jaw_worm_test)
sts_enable_sanitizers(jaw_worm_test)

gtest_discover_tests(jaw_worm_test)

# A4.1: effect interpreter + damage pipeline (design doc §5.5, §6, §10 trap 1).
# Table-driven Strength/Vulnerable/Weak stacking vs. hand-computed values, the
# float-rounding trap 1 test, per-opcode direct checks, SHUFFLE_IN/ROLL_MOVE
# no-op checks, and a pump() wiring regression. Links sts_engine (interp.cpp).
add_executable(damage_pipeline_test
    damage_pipeline_test.cpp
)

target_link_libraries(damage_pipeline_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(damage_pipeline_test)
sts_enable_sanitizers(damage_pipeline_test)

gtest_discover_tests(damage_pipeline_test)

# A4.2: pile operations -- draw (with up-front hand-size cap + empty-pile
# reshuffle), reshuffle (shuffle_rng -> JDK LCG Fisher-Yates), exhaust, energy
# gain/spend (design doc §3.3, §9, §10 trap 2). The reshuffle permutation is
# checked against the golden-tested JdkRandom/jdk_shuffle primitives (A1.2/A1.3)
# as an independent oracle. Links sts_engine (piles.cpp).
add_executable(piles_test
    piles_test.cpp
)

target_link_libraries(piles_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(piles_test)
sts_enable_sanitizers(piles_test)

gtest_discover_tests(piles_test)

# A4.3: five skeleton cards + card-play flow (design doc §5.3, §9, §10 trap 10).
# Per-card table tests (constructed state -> queue_card_play + pump -> assert
# fields), the trap-10 dequeue-vs-enqueue random-target test, and a scripted
# 3-turn Jaw Worm fight whose hand-traced expected state is matched by hash.
# Links sts_engine (card_play.cpp + the A3.2 Jaw Worm module it drives).
add_executable(cards_test
    cards_test.cpp
)

target_link_libraries(cards_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(cards_test)
sts_enable_sanitizers(cards_test)

gtest_discover_tests(cards_test)

# A5.1: batch API (advance / legal_actions / combat_begin, design doc §7).
# Batch-of-128 independence (per-state hash == single-state reference run),
# determinism (same batch twice -> identical hashes), combat_begin invariants,
# and legal_actions affordability/phase gating. Links sts_engine (advance.cpp).
add_executable(advance_test
    advance_test.cpp
)

target_link_libraries(advance_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(advance_test)
sts_enable_sanitizers(advance_test)

gtest_discover_tests(advance_test)
