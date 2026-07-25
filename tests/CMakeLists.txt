include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Build GoogleTest shared. Statically, every one of the 39 test executables
# links its own copy of the gtest archive; relinking all 39 measured ~33s
# against ~9.5s shared (3 reps each, same machine, same session). BUILD_SHARED_LIBS
# is a plain variable set only across the MakeAvailable call, so it applies to
# googletest's subdirectory and nothing else -- sts_engine and the tools stay
# static. The link mode is behaviour-neutral: it was verified to leave the
# debug, asan and release suites at a full pass with the pass COUNT unchanged.
# The count itself is deliberately NOT restated here -- it moves with every
# content task, and a number frozen in a comment is a liability (this line has
# already been wrong twice). `ctest --preset debug` is the live figure.
set(BUILD_SHARED_LIBS ON)
FetchContent_MakeAvailable(googletest)
unset(BUILD_SHARED_LIBS)

include(GoogleTest)

# Every target below is declared with sts_add_test (cmake/StsAddTest.cmake),
# which supplies the shape all 39 suites shared by hand: one executable, a
# GTest::gtest_main link, sts_set_warnings, sts_enable_sanitizers, and
# gtest_discover_tests. Defaults are SOURCES <name>.cpp and LIBS sts_engine, so
# a plain tier-2 suite over the engine is a single line.

sts_add_test(sts_engine_tests SOURCES test_smoke.cpp)

# A1.1: RandomXS128 (xorshift128+) bit-exactness suite. Separate executable
# target per the Stage A task ledger's acceptance line (gtest
# `rng_xs128_test`), not appended to sts_engine_tests.
sts_add_test(rng_xs128_test GOLDEN)

# A1.2: JDK LCG + Collections.shuffle bit-exactness suite.
sts_add_test(rng_jdk_test GOLDEN)

# A1.4: SeedHelper base-35 string<->long conversion suite.
sts_add_test(seed_string_test GOLDEN)

# A1.3: game Random wrapper (RngStream) bit-exactness suite -- golden sets 2, 3, 5.
sts_add_test(rng_stream_test GOLDEN)

# A2.1: core id enums + CardInstance/PowerSlot/Action scaffolding suite.
sts_add_test(types_test)

# A2.2: CombatState/RunState layout + state-hash/snapshot suite. Links
# sts_engine, which now carries state_hash.cpp (and its private xxhash dep).
sts_add_test(state_test)

# A5.2: observation encoder stub (ObsBuffer + encode_observation) round-trip
# spot checks + zero-allocation check (TU-local counting operator new/delete).
sts_add_test(observation_test)

# A3.1: action-queue pump + queue mechanics suite (design doc §5.1-§5.2, trap 9).
# Links sts_engine, which now carries action_queue.cpp.
sts_add_test(action_queue_test)

# A3.2: Jaw Worm AI + monster turn. Compares the engine against the hand-derived
# fixture (tests/fixtures/jaw_worm_fixture.tsv, gen by gen_jaw_worm_fixture.py).
# STS_FIXTURE_DIR points at tests/fixtures (distinct from tests/golden, which
# holds JVM-captured vectors -- this is a hand-derived oracle, not a golden file).
sts_add_test(jaw_worm_test FIXTURES)

# B3.13: Cultist AI + monster turn. Same hand-derived-oracle pattern as
# jaw_worm_test (tests/fixtures/cultist_fixture.tsv, gen by gen_cultist_fixture.py),
# plus a pump-driven check of the monster Ritual Strength ramp (skipFirst).
sts_add_test(cultist_test FIXTURES)

# B3.13: Louse (Normal + Defensive) AI + monster turn + curl-up. Hand-derived
# oracle (louse_{normal,defensive}_fixture.tsv, gen by gen_louse_fixture.py),
# plus the Curl Up on-first-attack block trigger acceptance test.
sts_add_test(louse_test FIXTURES)

# B3.14: Spike/Acid small+medium slime A20 AI, HP, effects, and encounter wiring.
# Independent 32-seed x 20-turn fixtures pin each class's aiRng draw sequence.
sts_add_test(slime_test FIXTURES)

# B3.17: large slimes + split framework. Independent 32-seed x 20-turn fixtures
# pin each class's aiRng draw sequence (incl. Acid L's nextFloat tiebreaks);
# focused tests pin the exact split threshold, child HP/slots, queue state after
# a split, both interrupt timings, and the cannotLose termination window.
sts_add_test(large_slime_test FIXTURES)

# B3.20: Slime Boss native AI, corrected Slimed-discard Goop Spray, exact
# split threshold/current-HP large children, full large->medium descendant
# chain positioning, boss terminal gate, and a public advance() directed script.
sts_add_test(slime_boss_test)

# Act-1 elites (Gremlin Nob + Sentry): registry ascension columns, Anger's
# SKILL-only trigger, Skull Bash's damage-then-Vulnerable order, the Sentry's
# pre-battle Artifact and its debuff nullify, Bolt's Dazed-into-discard count,
# and the 3-Sentry group from composition through spawn to alternating
# telegraphs. Three independent 32-seed x 20-turn fixtures (Nob, Sentry in an
# even and an odd slot) pin move history and the aiRng draw accounting.
sts_add_test(elite_test FIXTURES)

# B3.19: Lagavulin, the sleeping elite. Registry columns (HP A8 / attack A3 /
# debuff A18), the pre-battle armour that grows 8 a round until the shell opens,
# both wake paths (three idles, or the first hit that moves HP), the post-wake
# attack/attack/debuff cadence, the lethal-hit !isDying branch, and the awake
# `Lagavulin(false)` ctor variant.
sts_add_test(lagavulin_test)

# A4.1: effect interpreter + damage pipeline (design doc §5.5, §6, §10 trap 1).
# Table-driven Strength/Vulnerable/Weak stacking vs. hand-computed values, the
# float-rounding trap 1 test, per-opcode direct checks, SHUFFLE_IN/ROLL_MOVE
# no-op checks, and a pump() wiring regression. Links sts_engine (interp.cpp).
sts_add_test(damage_pipeline_test)

# A4.2: pile operations -- draw (with up-front hand-size cap + empty-pile
# reshuffle), reshuffle (shuffle_rng -> JDK LCG Fisher-Yates), exhaust, energy
# gain/spend (design doc §3.3, §9, §10 trap 2). The reshuffle permutation is
# checked against the golden-tested JdkRandom/jdk_shuffle primitives (A1.2/A1.3)
# as an independent oracle. Links sts_engine (piles.cpp).
sts_add_test(piles_test)

# A4.3: five skeleton cards + card-play flow (design doc §5.3, §9, §10 trap 10).
# Per-card table tests (constructed state -> queue_card_play + pump -> assert
# fields), the trap-10 dequeue-vs-enqueue random-target test, and a scripted
# 3-turn Jaw Worm fight whose hand-traced expected state is matched by hash.
# Links sts_engine (card_play.cpp + the A3.2 Jaw Worm module it drives).
sts_add_test(cards_test)

# A5.1: batch API (advance / legal_actions / combat_begin, design doc §7).
# Batch-of-128 independence (per-state hash == single-state reference run),
# determinism (same batch twice -> identical hashes), combat_begin invariants,
# and legal_actions affordability/phase gating. Links sts_engine (advance.cpp).
sts_add_test(advance_test)

# B3.12: encounter framework -- composition resolution (miscRng), spawn HP-roll
# order (monsterHpRng), multi-monster legal-action target grid, monsterRng pool
# draw.
sts_add_test(encounters_test)

# B4.4: run-level advance + room lifecycle -- run_begin Neow-pending stream state,
# trap-7 floor reseed, run-combat entry == combat_begin, full floor cycle, map
# choice legality + non-combat routing + heterogeneous batch advance.
sts_add_test(run_advance_test)

# A6.1: diff harness -- trace format + field-by-field differ + reproducer +
# fixture oracle (design doc §8, §9). Links diff_harness (tools/diff_harness),
# which PUBLIC-links sts_engine, so this test reaches both the harness API and
# the engine. Acceptance: synthetic divergence in each field group is caught and
# named; reproducer file replays to the same diff; trace round-trip + schema
# rejection.
sts_add_test(differ_test LIBS diff_harness)

# A6.2: fixture oracle -- the ~20 scripted Jaw Worm fights (M1 acceptance). Each
# checked-in trace (tests/golden/combat_fixtures/*.trace, written by the
# independent reference simulator tools/fixture_gen/gen_combat_fixtures) is
# replayed through the real engine (combat_begin + advance via A6.1's replay)
# and diffed field-by-field after every action -- acceptance is zero diffs on
# all fixtures. Links diff_harness (read_trace / replay_skeleton / diff_states).
sts_add_test(fixture_oracle_test
    LIBS diff_harness
    DEFS STS_COMBAT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden/combat_fixtures")

# B2.1: registry codegen acceptance -- determinism (generate twice -> byte-
# identical), duplicate-id rejection (clear error + non-zero exit), and
# equivalence of the generated tables to the hand-written engine tables
# (types.hpp enums / cards.hpp CardDef) so the later migration is a zero-change
# swap. registry_gen_standalone.cpp includes ONLY the generated headers, proving
# they compile standalone; the main TU also links sts_engine as the oracle.
#
# MAKE_DIRS creates the scratch root at CONFIGURE time. This target's 24 gtest
# cases become 24 independent ctest entries, so under -j they would otherwise
# all race to fs::create_directories() the same root on first use. Each case
# writes distinctly-named subdirectories under it (det_a/det_b, bad_registry,
# bad_out, upgrade_*, ... -- audited: no name is used by two cases), so the
# subdirectories themselves do not collide and the cases can run concurrently.
sts_add_test(registry_gen_test
    SOURCES registry_gen_test.cpp registry_gen_standalone.cpp
    LIBS sts_engine registry_generated
    DEPENDS registry_gen_headers
    MAKE_DIRS registry_gen_scratch
    DEFS
        STS_PYTHON_EXECUTABLE="${STS_PYTHON_EXECUTABLE}"
        STS_REGISTRY_GEN_PY="${STS_REGISTRY_GEN_PY}"
        STS_REGISTRY_DIR="${STS_REGISTRY_DIR}"
        STS_GEN_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/registry_gen_scratch")

# B1.5: translator acceptance -- the §2.5 oracle fields land in the right schema
# field bit-for-bit; an unknown field is refused; round-trip stability (translate
# twice -> identical structs + identical emitted combat trace). Runs on the
# committed curated golden sample tests/golden/oracle_corpus/skeleton_sample.jsonl.
# Links oracle_translator (which PRIVATE-links nlohmann); the test itself never
# sees the JSON header, proving the tools-only grant.
sts_add_test(translator_test
    LIBS oracle_translator
    MAKE_DIRS translator_scratch
    DEFS
        STS_ORACLE_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden/oracle_corpus"
        STS_TRANSLATOR_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/translator_scratch")

# B1.6: the CommunicationModOracleAdapter answers query()/query_run() over a
# TRANSLATED campaign sample. Translates the committed golden sample through the
# real B1.5 translator, packs it into a v2 trace container (write_trace_v2), and
# exercises the file-based adapter for both state kinds + unknown-seed/prefix.
# Links oracle_translator (which PUBLIC-links diff_harness, so the v2 trace API
# and the adapter are reachable); nlohmann stays PRIVATE to the translator.
#
# Scratch directory is oracle_adapter_scratch, NOT translator_scratch: this
# target and translator_test used to share one directory, which races once ctest
# runs in parallel (one ctest entry per gtest case => concurrent processes).
# sts_add_test now refuses a second claim on the same name at configure time.
sts_add_test(oracle_adapter_test
    LIBS oracle_translator
    MAKE_DIRS oracle_adapter_scratch
    DEFS
        STS_ORACLE_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden/oracle_corpus"
        STS_TRANSLATOR_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/oracle_adapter_scratch")

# B4.1: map path generation (MapGenerator re-expression) vs the live-game oracle
# corpus (tests/golden/map_paths/oracle_maps.txt, derived from campaign
# b13_on20b). Header-only engine code, so it links sts_engine like the other
# tier-2 suites.
sts_add_test(map_gen_test GOLDEN)

# B3.1: interpreter / card-mechanics extensions (X-cost, ALL_ENEMY / RANDOM_ENEMY
# targeting, exhaust/unplayable flag bits, MAKE_CARD/SET_COST opcodes, upgrade
# plumbing). Tier-2 tests over constructed states, hand-computed from the cited
# Java. Header-only engine surface, links sts_engine like the other tier-2 suites.
sts_add_test(card_ext_test)

# B4.2: map room-type assignment (generateRoomTypes quotas, trap-12 direct-XS128
# Collections.shuffle, placement rules, fixed rows 0/8/14, setEmeraldElite draw)
# vs the live-game oracle corpus (tests/golden/map_paths/oracle_maps.txt): room
# symbol match per node + the full post-generateMap {counter,s0,s1} triple, all
# 20 seeds. Header-only engine code (map_rooms.hpp), links sts_engine like the
# other tier-2 suites.
sts_add_test(map_rooms_test GOLDEN)

# B3.2: power-hook framework -- the frozen §5.2-5.5 dispatch order, the data-bound
# + native-escape-hatch mechanisms, and the five hook-order stress cases
# (onExhaust list order, onUseCard/Corruption redirect, atEndOfTurn stack,
# APPLY_POWER Artifact/Sadistic interception, wasHPLost/Rupture attribution).
# Engine surface + power_hooks.cpp (in sts_engine); links sts_engine like the
# other tier-2 suites.
sts_add_test(power_hooks_test)

# B3.23 potions: each Ironclad potion's USE effect + potency (S1 A20), the
# trap-14 rejection-sampling identity roll, and the A11 potion-slot count.
# Engine surface + potions.cpp (in sts_engine).
sts_add_test(potions_test)

# B3.24 relic-hook framework tier-2 suite: acquisition-order dispatch + per-relic
# combat triggers (starter Burning Blood + common pool) + counter persistence
# (Nunchaku/Pen Nib). Engine surface + relic_hooks.cpp (in sts_engine).
sts_add_test(relic_hooks_test)

# B4.6: five relic-pool JDK shuffles, front/end pop (trap 15), tier gate,
# canSpawn/fallback behavior, and RunState acquisition/on-pickup effects.
sts_add_test(relic_pools_test)

# B3.4 red common skills + CHOOSE-in-combat plumbing.
sts_add_test(card_skills_test)

# B3.3 red common attacks (block/Strength/Strike-scaled damage, AoE + random
# multi-hit, MAKE_CARD self-copy/Wound, Headbutt discard->draw-top choice, Clash
# canUse). Tier-2 per card (both upgrade rows), hand-computed from the cited use().
sts_add_test(card_attacks_test)

# B3.5: the exact 11 Ironclad uncommon attacks. Per-card base/upgraded tier-2
# behavior plus Blood for Blood cost hooks, Rampage misc scaling, Searing Blow
# multi-upgrade counts, Sever Soul filtering, and Whirlwind X-cost/AOE.
sts_add_test(card_uncommon_attacks_test)

# B3.6: the exact 17 Ironclad uncommon skills. Per-card base/upgraded tier-2
# behavior plus No Draw gating, Dual Wield DUPLICATE choice (forced/prompted/
# copies), Sentinel on-exhaust energy, Infernal Blade cardRandomRng accounting +
# cost-for-turn reset, Entrench block doubling, Flame Barrier thorns/removal,
# Second Wind exhaust-then-block ordering, Spot Weakness intent gating, and a
# directed public advance()/legal_actions() script.
sts_add_test(card_uncommon_skills_test)

# B3.9 statuses + curses (unplayable gating, end-of-turn triggers vs. §5.4 order,
# on-draw energy loss, on-other-card-played HP loss, ethereal exhaust, Normality
# play-cap, Slimed exhaust-on-play). Tier-2 per entry, hand-computed from the Java.
sts_add_test(status_curse_test)

# B3.7: exact RED/UNCOMMON/POWER roster. Per-power base/upgraded behavior,
# power-card no-discard path, draw/exhaust trigger ordering, and an advance() script.
sts_add_test(card_uncommon_powers_test)

# B4.15: ascension modifier table (design §6) -- the implemented run-setup + map
# rows, their application ORDER, and the frozen "no such modifier" negatives.
# Reads registry/a20.yaml directly (STS_REGISTRY_DIR) because that file is the
# rules-as-data home of the table and generates no C++ beyond a row count, so the
# negative freeze has to be pinned against the YAML itself.
sts_add_test(a20_modifiers_test DEFS STS_REGISTRY_DIR="${STS_REGISTRY_DIR}")

# Floating-point contract guard: proves cmake/StsFloatingPoint.cmake's
# -ffp-contract=off (and the MSVC/clang-cl equivalents) actually took effect on
# whatever toolchain is building, by computing a fused and an unfused form of
# `a + b * c` and asserting the compiler emitted the unfused one. Every case
# first asserts its inputs still distinguish the two forms, so the guard cannot
# decay into a vacuous pass. The damage pipeline (interp_damage.cpp:52) and
# rng_stream's random(start,end) both have the fusable shape, so a regression
# here moves committed fixtures and breaks cross-toolchain bit-identity.
sts_add_test(fp_contract_test)

# Hygiene guard: fails if an execution-plan task id (B3.6 / A4.1 shape)
# reappears in src/ or include/. Scans the SOURCE tree at run time rather than
# anything it links, so a new comment is caught without recompiling the engine;
# the allowlist mechanism and the reasoning live in the test file's header.
sts_add_test(no_task_ids_test DEFS STS_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
