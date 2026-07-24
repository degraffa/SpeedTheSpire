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

# B3.13: Cultist AI + monster turn. Same hand-derived-oracle pattern as
# jaw_worm_test (tests/fixtures/cultist_fixture.tsv, gen by gen_cultist_fixture.py),
# plus a pump-driven check of the monster Ritual Strength ramp (skipFirst).
add_executable(cultist_test
    cultist_test.cpp
)

target_link_libraries(cultist_test PRIVATE sts_engine GTest::gtest_main)
target_compile_definitions(cultist_test PRIVATE STS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
sts_set_warnings(cultist_test)
sts_enable_sanitizers(cultist_test)

gtest_discover_tests(cultist_test)

# B3.13: Louse (Normal + Defensive) AI + monster turn + curl-up. Hand-derived
# oracle (louse_{normal,defensive}_fixture.tsv, gen by gen_louse_fixture.py),
# plus the Curl Up on-first-attack block trigger acceptance test.
add_executable(louse_test
    louse_test.cpp
)

target_link_libraries(louse_test PRIVATE sts_engine GTest::gtest_main)
target_compile_definitions(louse_test PRIVATE STS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
sts_set_warnings(louse_test)
sts_enable_sanitizers(louse_test)

gtest_discover_tests(louse_test)

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

# B3.12: encounter framework -- composition resolution (miscRng), spawn HP-roll
# order (monsterHpRng), multi-monster legal-action target grid, monsterRng pool
# draw.
add_executable(encounters_test
    encounters_test.cpp
)

target_link_libraries(encounters_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(encounters_test)
sts_enable_sanitizers(encounters_test)

gtest_discover_tests(encounters_test)

# B4.4: run-level advance + room lifecycle -- run_begin Neow-pending stream state,
# trap-7 floor reseed, run-combat entry == combat_begin, full floor cycle, map
# choice legality + non-combat routing + heterogeneous batch advance.
add_executable(run_advance_test
    run_advance_test.cpp
)

target_link_libraries(run_advance_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(run_advance_test)
sts_enable_sanitizers(run_advance_test)

gtest_discover_tests(run_advance_test)

# A6.1: diff harness -- trace format + field-by-field differ + reproducer +
# fixture oracle (design doc §8, §9). Links diff_harness (tools/diff_harness),
# which PUBLIC-links sts_engine, so this test reaches both the harness API and
# the engine. Acceptance: synthetic divergence in each field group is caught and
# named; reproducer file replays to the same diff; trace round-trip + schema
# rejection.
add_executable(differ_test
    differ_test.cpp
)

target_link_libraries(differ_test PRIVATE diff_harness GTest::gtest_main)
sts_set_warnings(differ_test)
sts_enable_sanitizers(differ_test)

gtest_discover_tests(differ_test)

# A6.2: fixture oracle -- the ~20 scripted Jaw Worm fights (M1 acceptance). Each
# checked-in trace (tests/golden/combat_fixtures/*.trace, written by the
# independent reference simulator tools/fixture_gen/gen_combat_fixtures) is
# replayed through the real engine (combat_begin + advance via A6.1's replay)
# and diffed field-by-field after every action -- acceptance is zero diffs on
# all fixtures. Links diff_harness (read_trace / replay_skeleton / diff_states).
add_executable(fixture_oracle_test
    fixture_oracle_test.cpp
)

target_link_libraries(fixture_oracle_test PRIVATE diff_harness GTest::gtest_main)
target_compile_definitions(fixture_oracle_test PRIVATE
    STS_COMBAT_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/golden/combat_fixtures")
sts_set_warnings(fixture_oracle_test)
sts_enable_sanitizers(fixture_oracle_test)

gtest_discover_tests(fixture_oracle_test)

# B2.1: registry codegen acceptance -- determinism (generate twice -> byte-
# identical), duplicate-id rejection (clear error + non-zero exit), and
# equivalence of the generated tables to the hand-written engine tables
# (types.hpp enums / cards.hpp CardDef) so the later migration is a zero-change
# swap. registry_gen_standalone.cpp includes ONLY the generated headers, proving
# they compile standalone; the main TU also links sts_engine as the oracle.
add_executable(registry_gen_test
    registry_gen_test.cpp
    registry_gen_standalone.cpp
)

add_dependencies(registry_gen_test registry_gen_headers)
target_link_libraries(registry_gen_test PRIVATE
    sts_engine registry_generated GTest::gtest_main)
target_compile_definitions(registry_gen_test PRIVATE
    STS_PYTHON_EXECUTABLE="${STS_PYTHON_EXECUTABLE}"
    STS_REGISTRY_GEN_PY="${STS_REGISTRY_GEN_PY}"
    STS_REGISTRY_DIR="${STS_REGISTRY_DIR}"
    STS_GEN_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/registry_gen_scratch")
sts_set_warnings(registry_gen_test)
sts_enable_sanitizers(registry_gen_test)

gtest_discover_tests(registry_gen_test)

# B1.5: translator acceptance -- the §2.5 oracle fields land in the right schema
# field bit-for-bit; an unknown field is refused; round-trip stability (translate
# twice -> identical structs + identical emitted combat trace). Runs on the
# committed curated golden sample tests/golden/oracle_corpus/skeleton_sample.jsonl.
# Links oracle_translator (which PRIVATE-links nlohmann); the test itself never
# sees the JSON header, proving the tools-only grant.
add_executable(translator_test
    translator_test.cpp
)

target_link_libraries(translator_test PRIVATE oracle_translator GTest::gtest_main)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/translator_scratch")
target_compile_definitions(translator_test PRIVATE
    STS_ORACLE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/tests/golden/oracle_corpus"
    STS_TRANSLATOR_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/translator_scratch")
sts_set_warnings(translator_test)
sts_enable_sanitizers(translator_test)

gtest_discover_tests(translator_test)

# B1.6: the CommunicationModOracleAdapter answers query()/query_run() over a
# TRANSLATED campaign sample. Translates the committed golden sample through the
# real B1.5 translator, packs it into a v2 trace container (write_trace_v2), and
# exercises the file-based adapter for both state kinds + unknown-seed/prefix.
# Links oracle_translator (which PUBLIC-links diff_harness, so the v2 trace API
# and the adapter are reachable); nlohmann stays PRIVATE to the translator.
add_executable(oracle_adapter_test
    oracle_adapter_test.cpp
)

target_link_libraries(oracle_adapter_test PRIVATE oracle_translator GTest::gtest_main)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/translator_scratch")
target_compile_definitions(oracle_adapter_test PRIVATE
    STS_ORACLE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/tests/golden/oracle_corpus"
    STS_TRANSLATOR_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/translator_scratch")
sts_set_warnings(oracle_adapter_test)
sts_enable_sanitizers(oracle_adapter_test)

gtest_discover_tests(oracle_adapter_test)

# B4.1: map path generation (MapGenerator re-expression) vs the live-game oracle
# corpus (tests/golden/map_paths/oracle_maps.txt, derived from campaign
# b13_on20b). Header-only engine code, so it links sts_engine like the other
# tier-2 suites.
add_executable(map_gen_test
    map_gen_test.cpp
)

target_link_libraries(map_gen_test PRIVATE sts_engine GTest::gtest_main)
target_compile_definitions(map_gen_test PRIVATE STS_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden")
sts_set_warnings(map_gen_test)
sts_enable_sanitizers(map_gen_test)

gtest_discover_tests(map_gen_test)

# B3.1: interpreter / card-mechanics extensions (X-cost, ALL_ENEMY / RANDOM_ENEMY
# targeting, exhaust/unplayable flag bits, MAKE_CARD/SET_COST opcodes, upgrade
# plumbing). Tier-2 tests over constructed states, hand-computed from the cited
# Java. Header-only engine surface, links sts_engine like the other tier-2 suites.
add_executable(card_ext_test
    card_ext_test.cpp
)

target_link_libraries(card_ext_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(card_ext_test)
sts_enable_sanitizers(card_ext_test)

gtest_discover_tests(card_ext_test)

# B4.2: map room-type assignment (generateRoomTypes quotas, trap-12 direct-XS128
# Collections.shuffle, placement rules, fixed rows 0/8/14, setEmeraldElite draw)
# vs the live-game oracle corpus (tests/golden/map_paths/oracle_maps.txt): room
# symbol match per node + the full post-generateMap {counter,s0,s1} triple, all
# 20 seeds. Header-only engine code (map_rooms.hpp), links sts_engine like the
# other tier-2 suites.
add_executable(map_rooms_test
    map_rooms_test.cpp
)

target_link_libraries(map_rooms_test PRIVATE sts_engine GTest::gtest_main)
target_compile_definitions(map_rooms_test PRIVATE STS_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden")
sts_set_warnings(map_rooms_test)
sts_enable_sanitizers(map_rooms_test)

gtest_discover_tests(map_rooms_test)

# B3.2: power-hook framework -- the frozen §5.2-5.5 dispatch order, the data-bound
# + native-escape-hatch mechanisms, and the five hook-order stress cases
# (onExhaust list order, onUseCard/Corruption redirect, atEndOfTurn stack,
# APPLY_POWER Artifact/Sadistic interception, wasHPLost/Rupture attribution).
# Engine surface + power_hooks.cpp (in sts_engine); links sts_engine like the
# other tier-2 suites.
add_executable(power_hooks_test
    power_hooks_test.cpp
)

target_link_libraries(power_hooks_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(power_hooks_test)
sts_enable_sanitizers(power_hooks_test)

gtest_discover_tests(power_hooks_test)

# B3.23 potions: each Ironclad potion's USE effect + potency (S1 A20), the
# trap-14 rejection-sampling identity roll, and the A11 potion-slot count.
# Engine surface + potions.cpp (in sts_engine).
add_executable(potions_test
    potions_test.cpp
)

target_link_libraries(potions_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(potions_test)
sts_enable_sanitizers(potions_test)

gtest_discover_tests(potions_test)

# B3.24 relic-hook framework tier-2 suite: acquisition-order dispatch + per-relic
# combat triggers (starter Burning Blood + common pool) + counter persistence
# (Nunchaku/Pen Nib). Engine surface + relic_hooks.cpp (in sts_engine).
add_executable(relic_hooks_test
    relic_hooks_test.cpp
)

target_link_libraries(relic_hooks_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(relic_hooks_test)
sts_enable_sanitizers(relic_hooks_test)

gtest_discover_tests(relic_hooks_test)

# B3.4 red common skills + CHOOSE-in-combat plumbing.
add_executable(card_skills_test
    card_skills_test.cpp
)
target_link_libraries(card_skills_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(card_skills_test)
sts_enable_sanitizers(card_skills_test)
gtest_discover_tests(card_skills_test)

# B3.3 red common attacks (block/Strength/Strike-scaled damage, AoE + random
# multi-hit, MAKE_CARD self-copy/Wound, Headbutt discard->draw-top choice, Clash
# canUse). Tier-2 per card (both upgrade rows), hand-computed from the cited use().
add_executable(card_attacks_test
    card_attacks_test.cpp
)
target_link_libraries(card_attacks_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(card_attacks_test)
sts_enable_sanitizers(card_attacks_test)
gtest_discover_tests(card_attacks_test)

# B3.5: the exact 11 Ironclad uncommon attacks. Per-card base/upgraded tier-2
# behavior plus Blood for Blood cost hooks, Rampage misc scaling, Searing Blow
# multi-upgrade counts, Sever Soul filtering, and Whirlwind X-cost/AOE.
add_executable(card_uncommon_attacks_test
    card_uncommon_attacks_test.cpp
)

target_link_libraries(card_uncommon_attacks_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(card_uncommon_attacks_test)
sts_enable_sanitizers(card_uncommon_attacks_test)
gtest_discover_tests(card_uncommon_attacks_test)

# B3.9 statuses + curses (unplayable gating, end-of-turn triggers vs. §5.4 order,
# on-draw energy loss, on-other-card-played HP loss, ethereal exhaust, Normality
# play-cap, Slimed exhaust-on-play). Tier-2 per entry, hand-computed from the Java.
add_executable(status_curse_test
    status_curse_test.cpp
)
target_link_libraries(status_curse_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(status_curse_test)
sts_enable_sanitizers(status_curse_test)
gtest_discover_tests(status_curse_test)
