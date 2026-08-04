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

# GoogleTest's headers are third-party and must not gate our build.
#
# sts_set_warnings lands PRIVATE, which correctly keeps our -Werror off
# googletest's own SOURCES -- but a header included by one of our test TUs is
# compiled with OUR flags, so gtest's header code is judged by our gate. Under
# clang-cl that is not theoretical: gtest 1.15.2 fails the build outright.
#
#   _deps/googletest-src/googletest/include/gtest/gtest-printers.h(524,35):
#   error: implicit conversion from 'char8_t' to 'char32_t' may change the
#   meaning of the represented code unit [-Werror,-Wcharacter-conversion]
#
# -Wcharacter-conversion is a recent addition to clang's -Wconversion family and
# is about character-encoding meaning, not the value-changing integer narrowing
# this project promoted to an error. Marking the include directories SYSTEM is
# the fix that costs nothing: it suppresses diagnostics from THEIR headers while
# leaving our gate over our own code exactly as strict. Suppressing the warning
# globally instead would have blunted -Wconversion everywhere to appease one
# line of someone else's code.
#
# Written as a property edit rather than FetchContent_Declare(... SYSTEM),
# because the latter needs CMake 3.25 and this project's floor is 3.21.
#
# The second half is googletest's OWN -WX (warnings-as-errors), which it applies
# to its own sources for MSVC-like compilers. clang-cl satisfies CMake's MSVC
# check, so gtest builds itself with `-W4 -WX` -- and under clang-cl /W4 means
# clang's -Wall -Wextra, which in clang 22 includes the new
# -Wcharacter-conversion. gtest 1.15.2 then fails to compile ITSELF:
#
#   gtest/gtest-printers.h(524,35): error: implicit conversion from 'char8_t'
#   to 'char32_t' ... [-Werror,-Wcharacter-conversion]
#
# gtest's own -wdNNNN suppression list cannot cover this: those are MSVC warning
# NUMBERS, which say nothing about a clang-native diagnostic.
#
# A VERSION BUMP DOES NOT FIX THIS -- tested, not assumed. Reproduced in an
# isolated project containing nothing but googletest and clang-cl 22.1.8:
#   v1.15.2 -> fails at gtest-printers.h:524
#   v1.17.0 -> fails at gtest-printers.h:528   (same defect, moved 4 lines)
# so this is a googletest x clang-22 incompatibility that upstream has not
# addressed, not a staleness problem in our pin.
#
# /WX- (disable warnings-as-errors) is scoped to the third-party targets only.
# It is deliberately NOT -Wno-character-conversion: silencing one named
# diagnostic would leave the next new clang warning to break the build the same
# way, and this says the accurate thing instead -- a dependency's opinion about
# its own warnings is not a gate on our build. Our own targets are untouched;
# sts_set_warnings still promotes the conversion pair to errors on every file we
# own. Nothing in googletest's source tree is patched.
foreach(_gt gtest gtest_main gmock gmock_main)
    if(TARGET ${_gt})
        get_target_property(_gt_inc ${_gt} INTERFACE_INCLUDE_DIRECTORIES)
        if(_gt_inc)
            set_target_properties(${_gt} PROPERTIES
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_gt_inc}")
        endif()
        if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            target_compile_options(${_gt} PRIVATE /WX-)
        endif()
    endif()
endforeach()

# A bash that can run an in-tree .sh script named by a HOST path.
#
# On Linux this is whatever `bash` is on PATH. On Windows it must NOT be:
# `bash` on the Windows PATH is C:\Windows\System32\bash.exe, the WSL launcher,
# which cannot execute a script named `D:\...` -- and CMake's find_program would
# happily hand it over. Git for Windows ships a real MSYS bash that resolves
# `D:/...` correctly, so it is searched for by its own install paths first and
# System32 is excluded outright.
#
# STS_TEST_BASH may end up empty. tests/host_shell.hpp turns that into a named
# GTEST_SKIP at the two call sites, not a failure -- a host without bash cannot
# run a bash script, and pretending otherwise would be the kind of red result
# that teaches people to ignore red results.
if(WIN32)
    find_program(STS_TEST_BASH
        NAMES bash.exe
        HINTS
            "$ENV{ProgramFiles}/Git/bin"
            "$ENV{ProgramFiles}/Git/usr/bin"
            "$ENV{ProgramW6432}/Git/bin"
            "$ENV{ProgramFiles\(x86\)}/Git/bin"
            "C:/Program Files/Git/bin"
            "C:/Program Files/Git/usr/bin"
        NO_DEFAULT_PATH)
else()
    find_program(STS_TEST_BASH NAMES bash)
endif()
if(STS_TEST_BASH)
    message(STATUS "Test bash: ${STS_TEST_BASH}")
else()
    message(STATUS "Test bash: none found -- the two soak-script tests will SKIP")
    set(STS_TEST_BASH "")
endif()

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

# A5.2: observation encoder stub (OmniscientObsBuffer + omniscient_encode_observation) round-trip
# spot checks + zero-allocation check (TU-local counting operator new/delete).
sts_add_test(observation_test)

# T0.1: PublicView combat block + v1 schema skeleton (public_view.hpp): layout
# walk (no implicit padding -- the struct is memcmp'd/hashed downstream),
# populated-combat round trip incl. the OmniscientObsBuffer stub's gaps,
# draw-multiset order invariance + hidden/transient non-leak checks, Runic Dome
# suppression parity with omniscient_encode_observation, full-overwrite +
# reserved-field zeroing, and the zero-allocation check. Field audit:
# docs/public-view-audit.md.
sts_add_test(public_view_test)

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

# B3.16: the five Act-1 gremlins. Independent 32-seed x 20-turn fixtures pin each
# class's monsterHpRng/aiRng draw sequence, plus a separate 4-gremlin battery for
# the Tsundere's aiRng block-target pick; focused tests cover Angry, the
# block-an-ally / aliveCount branches, Fat's queued RollMoveAction and the
# wizard's charge cycle.
sts_add_test(gremlin_test FIXTURES)

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

# B3.21: The Guardian -- the A9/A19 registry columns, the forced opening Charge
# Up, both move cycles, the mode flip at the exact cumulative-damage threshold
# INCLUDING its per-cycle growth, Sharp Hide on attack plays, and the Spot
# Weakness ATTACK_BUFF classification this task's Twin Slam telegraph uncovered.
sts_add_test(guardian_test)

# B3.22: Hexaghost -- the A9 HP column and the A4/A19 stat columns, the forced
# two-turn ACTIVATE/DIVIDER opener, Divider's player-HP-derived base pinned
# against the formula at Hexaghost.java:151, the 7-long orb-count cycle
# hand-derived across 16 turns, the orb count modelled in spare MonsterState
# flag bits, Inferno's BurnIncreaseAction (existing Burns upgraded in draw and
# discard, three more added) and the Sear Burn that is upgraded from the turn
# after the first Inferno. getMove never reads its rolled `num`, so the aiRng
# DRAW COUNT is what the RNG cases pin.
sts_add_test(hexaghost_test)

# The two Act-1 slavers -- the A7 HP column and the A2/A17 damage/debuff columns,
# the Blue Slaver's d40-crossed-with-history tree, the Red Slaver's forced
# opening Stab plus its once-per-combat Entangle latch, and Entangled itself:
# the ATTACK-only legality veto (AbstractCard.hasEnoughEnergy:872-875) and the
# end-of-player-turn self-removal. Two independent 32-seed x 20-turn fixtures pin
# the aiRng draw sequence AND the roll-driven move each turn, since both getMove
# overrides read the rolled num.
sts_add_test(slaver_test FIXTURES)

# The Fungi Beast -- the A7 HP column, the flat 6 Bite, the A17 Strength column,
# the pre-battle Spore Cloud, and the ON-DEATH TRIGGER ORDERING it introduces:
# with two beasts the first death releases 2 Vulnerable and the last releases
# none, because AbstractMonster.die sets isDying before walking the power list
# and SporeCloudPower.onDeath bails on isBattleEnding. One independent 32-seed x
# 20-turn fixture pins the roll-driven d60 move tree.
sts_add_test(fungi_beast_test FIXTURES)

# The Looter -- the A7 HP column, the A2 mug/lunge damage columns, the
# slashCount/steal accounting (pad0 doubles as the steal count), the Thievery
# marker power, and THE FIRST REACHABLE ESCAPE: the liveness predicate is now
# `dead or escaped` (MonsterGroup.java:90-95,117-122), so an escaped monster is
# alive, out of the fight, untargetable, skipped by AoE/random targeting and
# the power walks, and its departure ends a solo battle in a terminal state
# distinct from a kill (mugged room, positive HP, escaped flag). Also the
# Smoke Bomb potion's combat-layer body, blocked on the same predicate. One
# independent 32-seed fixture whose per-seed ROW COUNT (4 or 5 turns, to the
# escape) pins the Smoke-Bomb-or-Lunge coin.
sts_add_test(looter_test FIXTURES)

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

# Player-information layer (training-plan §2.2/§2.3): KnowledgeState draw-order
# constraints (Headbutt known-top, shuffle clear, random-insertion weakening,
# Frozen Eye full-order reveal), the Louse construction-roll reveal + Runic
# Dome suppression, the generated observability membership table, and the
# zero-perturbation guarantee (recording never touches CombatState or RNG).
# Links sts_engine (knowledge.cpp + the pile/interp hook sites).
sts_add_test(knowledge_test)

# T0.4: resample_hidden -- the belief sampler (training-plan §2.4). One test
# group per table row (preserve / condition / fresh), the Markov-continuation
# equivalence for the encounter suffix, public-quantity preservation via
# encode_public_view byte equality, sampler determinism, and the poisoned-seed
# canary proving a particle is a function of (public state, sampler seed) alone.
sts_add_test(resample_test)

# T0.7: public_hash -- the XXH3-64 identity of a public information set.
# Hidden twins (drawn by the T0.4 sampler) hash equal, public differences and a
# flipped mask bit do not, and an encode into a pre-dirtied buffer hashes the
# same as one into a clean buffer (the raw-byte hash's precondition).
sts_add_test(public_hash_test)

# T0.7: the omniscient-boundary check, tested as a TOOL rather than trusted.
# Runs tools/check_omniscient_boundary.sh in its git-free `--scan DIR` mode
# against three committed fixture directories and requires it to accept the
# PublicView-only actor, REJECT the one that reaches the full-state observation
# surface (the standing negative control), accept a hatched contrasting
# comment, and fail loudly on an unusable argument.
sts_add_test(omniscient_boundary_test
    MAKE_DIRS omniscient_boundary_scratch
    DEFS
        STS_TEST_BASH="${STS_TEST_BASH}"
        STS_OMNISCIENT_CHECK="${CMAKE_SOURCE_DIR}/tools/check_omniscient_boundary.sh"
        STS_OMNISCIENT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/omniscient_boundary"
        STS_OMNISCIENT_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/omniscient_boundary_scratch")

# T0.5: the hidden-twin leak gate (training-plan §2.6a/§2.6c). Twin PublicView +
# mask byte equality over >= 1,000 fuzz-harvested states with a per-phase count
# asserted for every reachable RunPhase, reveal-timing pairs (chest / Louse
# construction roll / Match-and-Keep board: masked-and-twin-varying before the
# reveal, carried-and-pinned after), direct mask invariance, and the
# characterisation test for the draw-source CHOOSE mask leak twin.hpp pins
# around. Links fuzz_core for the run loop -- a forked policy loop drifts
# (fuzz_run.hpp).
sts_add_test(twin_test LIBS sts_engine fuzz_core)

# T0.5: the total-byte classification tripwire (training-plan §2.6b). The
# runtime half of include/sts/engine/byte_class.hpp -- the message naming the
# byte range and the neighbouring members -- plus the negative controls that
# prove the mechanism can fail: a scratch field added to a shadow RunController,
# a removed classification row, an overlapping row, and a shrunken declared
# alignment gap.
sts_add_test(tripwire_test)

# T0.5: the twin-fixture export the training repo consumes (training-plan
# §2.6a). Round-trips the committed cases through the container, proves the
# loader REFUSES every stamp mismatch and both length errors with a named
# reason, and replays every committed case from its recipe -- the stored
# PublicView, and its twin's, must come back byte for byte.
sts_add_test(twin_fixture_test
    LIBS twin_fixture
    MAKE_DIRS twin_fixture_scratch
    DEFS
        STS_TWIN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden/twin_fixtures"
        STS_TWIN_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/twin_fixture_scratch")

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

# S2.01: the Act-2 (TheCity) / Act-3 (TheBeyond) encounter rows and the per-act
# pool tables codegen emits -- pool membership/order/weights, the exclusion sets
# (incl. Chosen's two-key set and the two Act-3 self-exclusions), the fixed
# compositions, and the miscRng programs (spawnGremlin x2 with replacement,
# spawnShapes' shared 6-slot pool at 3 and 4 draws, getAncientShape x2). Also
# pins the Act-1 groups unchanged by the act extension.
sts_add_test(encounters_s2_test)

# B4.4: run-level advance + room lifecycle -- run_begin Neow-pending stream state,
# trap-7 floor reseed, run-combat entry == combat_begin, full floor cycle, map
# choice legality + non-combat routing + heterogeneous batch advance.
sts_add_test(run_advance_test)

# B4.5: combat rewards -- rarity thresholds (< 3 / < 40; elite < 10 / < 50; boss
# always RARE), pity dynamics vs. independent recomputation, the no-dupe
# re-roll, the Act-1 zero-chance upgrade draws, stream attribution (traps 13 +
# 18), potion drop + blizzard ratchet, elite relic (+ Black Star), Smoke Bomb's
# consumes-but-offers-nothing path, and the claim flow (gold door, potion
# slots/Sozu, the master-deck door incl. the Ceramic Fish guard, Singing Bowl,
# skip, Question Card / Busted Crown / White Beast Statue / Golden Idol /
# Prayer Wheel).
sts_add_test(combat_rewards_test)

# Card-pool LIBRARY ORDER: the computed CardLibrary HashMap iteration order, the
# oracle-capture offer identities that check that computation, the reward-pool
# vs src-pool (reversed, rarity-major) split, and the membership invariants the
# reorder must not disturb.
sts_add_test(card_pool_library_order_test)

# Rest sites: Java-float 30% heal with the ascension-negative pinned, Regal
# Pillow/Dream Catcher, Smith and Peace Pipe grids, Girya/Shovel effects, menu
# availability/order, and deterministic RunController masks/transitions.
sts_add_test(rest_sites_test)

# Act-1 fixed-row treasure rooms: exact 50/33/17 chest size and shared
# contents-roll tables, open/skip lifecycle, 0.9/1.1 gold roll, B4.6 relic
# grant/claim, and Matryoshka/Cursed Key/N'loth's Mask hook ordering + gates.
sts_add_test(treasure_rooms_test)

# B4.8: the merchant. The sixteen-draw merchantRng stock/price sequence pinned
# by a beside-the-engine replay, the truncate/halve/round price pipeline with
# its ascension and shop-relic discounts, the run-persistent purge ramp across
# two shops, the Maw Bank / Meal Ticket / Membership Card / Smiling Mask hooks,
# the CHOOSE flow and its modal removal grid, plus a real captured A20 shop
# reproduced id-for-id and price-for-price.
sts_add_test(shop_test)

# B4.10: the ?-room roll table (incl. the asymmetric slot-99 clamps), the
# one-committed-draw eventRng contract, trap-19 pity float sequences vs
# bit-exact hand-derivation, Tiny Chest / Juzu / leaving-a-shop hooks,
# membership init + NFY gate + isCursed exclusion, filtered-pool gates and
# canonical order, generate_event vs hand-derivation with throwaway-stream
# discipline and pool-removal bookkeeping, and the EVENT_DIALOG phase through
# the synthetic proof body.
sts_add_test(event_framework_test)
sts_add_test(exordium_events_i_test)
sts_add_test(exordium_events_ii_test)
# The six shrine-list bodies, incl. Match and Keep's three-stream deal and the
# Wheel's six spin results; and the Act-1-reachable one-time specials together
# with the getShrine gate evidence for the six the filter excludes.
sts_add_test(shrines_test)
sts_add_test(one_time_specials_test)

# The floor-0 Neow blessing: the four-category option roll against a draw-by-
# draw hand derivation, trap 17 (a fresh Random(seed) that no other stream can
# move), the category tables incl. category 2's drawback-first roll order and
# its three conditional omissions, every payout's stream attribution -- the
# split-stream colorless offers, the three-potion blessing's rolled-then-
# discarded card row, and the boss swap's loseRelic-before-draw ordering -- the
# drawbacks incl. Omamori's interception, the master-deck grids, and the
# scripted walk from the blessing screen onto the first floor.
sts_add_test(neow_test)

# How a combat STARTS: the turn-1 block (begin_first_turn) must not run
# getNextAction step 6's applyEndOfTurnPowers pass, and both combat-construction
# paths (combat_begin, enter_combat) must keep going through it. The reproducer
# is a sleeping Lagavulin's Metallicize; the by-construction half scans the two
# source files, so it needs STS_SOURCE_ROOT like no_task_ids_test does.
sts_add_test(combat_start_test DEFS STS_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")

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
# MAKE_DIRS creates the scratch root at CONFIGURE time. Each of this target's
# gtest cases becomes an independent ctest entry, so under -j they would
# otherwise all race to fs::create_directories() the same root on first use.
# The case count is deliberately not written down here: it moves with every
# content batch and nothing re-derives it (conventions §8) -- use `ctest -N`.
# Each case writes distinctly-named subdirectories under the root (det_a/det_b,
# bad_registry, bad_out, upgrade_*, bad_event_* ... -- audited: no name is used
# by two cases), so the subdirectories themselves do not collide and the cases
# can run concurrently.
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

# B4.5 live-capture safety hardening. Stdlib-only synthetic tests exercise the
# driver/orchestrator process boundary and strict artifact preflight without
# launching the game. Unlike the live campaign, this belongs in every preset:
# it proves missing fork state stops before artifact/policy acceptance and
# cannot be turned into an orchestrator relaunch loop.
add_test(
    NAME oracle_campaign_python_test
    COMMAND "${STS_PYTHON_EXECUTABLE}" -m unittest -v test_oracle_campaign.py)
set_tests_properties(oracle_campaign_python_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/tools/oracle_bridge/driver")
add_test(
    NAME oracle_campaign_pipeline_python_test
    COMMAND "${STS_PYTHON_EXECUTABLE}" -m unittest -v test_campaign_pipeline.py)
set_tests_properties(oracle_campaign_pipeline_python_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/tools/oracle_bridge/driver")

# TE.1 standing-deviation shape checker (stdlib-only synthetic diffs).
add_test(
    NAME oracle_standing_triage_python_test
    COMMAND "${STS_PYTHON_EXECUTABLE}" -m unittest -v test_standing_triage.py)
set_tests_properties(oracle_standing_triage_python_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/tools/oracle_bridge/driver")

# B5.4 deterministic dashboard aggregation + compressed-corpus integrity.
add_test(
    NAME verify_report_python_test
    COMMAND "${STS_PYTHON_EXECUTABLE}" -m unittest -v test_verify_report.py)
set_tests_properties(verify_report_python_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/tools/verify_report")

# B5.4 pure sim-side CI replay over the committed compressed 50-seed corpus.
# The Python wrapper uses only stdlib gzip/tar support to verify hashes and
# extract safe members; the real replay_run_diff binary performs every state
# comparison. The second case mutates one extracted state and proves the same
# path fails loud without committing the mutation.
sts_add_test(oracle_corpus_replay_test
    DEPENDS replay_run_diff
    MAKE_DIRS oracle_corpus_scratch
    DEFS
        STS_PYTHON_EXECUTABLE="${STS_PYTHON_EXECUTABLE}"
        STS_CORPUS_SMOKE_PY="${CMAKE_SOURCE_DIR}/tools/verify_report/ci_corpus_smoke.py"
        STS_CORPUS_ARCHIVE="${CMAKE_CURRENT_SOURCE_DIR}/golden/oracle_corpus/act1_a20_50.tar.gz"
        STS_CORPUS_MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/golden/oracle_corpus/act1_a20_50.manifest.json"
        STS_REPLAY_BIN="$<TARGET_FILE:replay_run_diff>"
        STS_CORPUS_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/oracle_corpus_scratch")

# The replay harness's screen-relative command mapping
# (tools/oracle_bridge/replay/src/command_map.hpp). A wrong entry in that table
# is indistinguishable from an engine divergence when a whole artifact is
# replayed -- the sim quietly does the wrong thing and every later record
# disagrees -- so the table gets unit tests of its own. `replay_command_map` is
# the INTERFACE target the replay directory publishes for exactly this; it
# carries no JSON dependency, so no artifact and no data root is involved.
sts_add_test(replay_command_map_test LIBS sts_engine replay_command_map)

# The five SHAPE decisions the replay read-outs make
# (tools/oracle_bridge/replay/src/readout_shapes.hpp): chest reward-row shape,
# capture-id -> EventId joining, and the exact Smoke Bomb, Entropic Brew and
# transform-preview capture-race shapes. Each is a place a read-out can quietly
# call a real divergence benign -- a rule that elides too much simply reports
# zero-diff and nobody notices -- so they get unit tests rather than only a
# green campaign run. Same no-artifact, no-JSON contract as the command-map
# suite above.
sts_add_test(replay_readout_shapes_test LIBS sts_engine replay_readout_shapes)

# B4.13: the Match and Keep! board decode
# (tools/oracle_bridge/replay/src/mk_board.hpp). The capture never shows the
# dealt board. It shows a COMPACTED, screen-position-sorted list of the cards
# still face down, in an index space that is a PERMUTATION of the simulator's
# board slots -- and a card that has ever been flipped is labelled by identity
# with no position at all. Getting the permutation or the compaction wrong does
# not make the read-out fail loudly; it makes it compare the wrong twelve cards
# and report a clean deal. Same no-artifact, no-JSON contract as the two suites
# above, and no engine dependency either: the header is plain data and plain
# strings.
sts_add_test(replay_mk_board_test LIBS replay_mk_board)

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

# B3.26 RARE + SHOP relic tiers (+ the SPECIAL-tier Odd Mushroom): per-relic
# combat mechanics, the damage-pipeline and ApplyPowerAction interception
# relics, the pickup gates, and the asserted inertness of the rows whose bodies
# are deliberately deferred.
sts_add_test(relic_rares_shop_test)

# B3.27 BOSS relic pool (Neow's category-3 swap) + the Act-1-event-reachable
# SPECIAL tier: Snecko Eye's per-draw Confusion cost roll and its draw-order
# accounting, Velvet Choker's play limit, the direct-write relics (Philosopher's
# Stone, Neow's Lament), the Ectoplasm gold suppression and act gate, and the
# asserted inertness of every deliberate no-op in both tiers.
sts_add_test(relic_boss_special_test)

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

# B3.8: exact RED/RARE roster (16 cards). Per-card base/upgraded behaviour, the
# start-of-turn block persistence Barricade introduces, the general
# recursive-play verb (Double Tap's replay and its draw-top form), Exhume's
# exhaust-pile choice, Feed/Reaper healing through the relic seam, and an
# advance() script.
sts_add_test(card_rares_test)

# B3.10a: the fourteen colorless UNCOMMON cards that need no colorless card pool.
# Per-card base/upgraded behaviour, the six reserved interior CardId gaps, Deep
# Breath's fused double reshuffle and its two-draw shuffle_rng accounting,
# Impatience's conditional draw (including that it stays behind the No Draw
# gate), Mind Blast's draw-pile-sized base, Madness's cardRandomRng
# REJECTION-SAMPLING draw accounting, No Block, and the modifyBlock /
# modifyBlockLast TWO-PASS order No Block's correctness depends on.
sts_add_test(card_colorless_uncommons_test)

# B3.11 stage A: the four colorless RARE cards this stage owns (Apotheosis,
# Master of Strategy, Sadistic Nature, Thinking Ahead) and the UPGRADE_ALL
# opcode. Per-card base/upgraded tier-2 behaviour; the Sadistic native-power
# Shackled exclusion fix (B3.10b landed PowerId::SHACKLED, making the
# exclusion reachable); Thinking Ahead's queue-time hand.size() guard (the
# played card is already in limbo when it is read); UPGRADE_ALL's pile order
# and canUpgrade gate, including Searing Blow's always-upgradable override and
# the played Apotheosis's own limbo immunity.
sts_add_test(card_colorless_rares_test)

# The played-card limbo window (AbstractPlayer.useCard:1369-1375): while a
# card's own queued effects resolve, the card is cardInUse -- in NO pile --
# until the queued UseCardAction files it away. Pins the reshuffle-membership,
# discard/exhaust-order, Strange-Spoon RNG-position, Perfected-Strike
# self-count and choice-window consequences of that window.
sts_add_test(card_limbo_test)

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

# B5.1: sim-side fuzz soak. Tests the TOOL, not the engine: that an injected
# divergence is caught at the right step, that the emitted STSFUZZ reproducer
# replays it, that the four-value case id alone regenerates the identical
# trajectory, that every enumerated move is one advance() actually honours, and
# that coverage kv round-trips for shard merging. The soak itself is a campaign
# (tools/fuzz/soak.sh), never a ctest entry -- what is unit-tested here is the
# triage path, so it is exercised on every build rather than first tried on the
# day something breaks.
sts_add_test(fuzz_test
    LIBS fuzz_core
    MAKE_DIRS fuzz_scratch
    DEFS
        STS_FUZZ_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/fuzz_scratch"
        STS_FUZZ_SOAK_BIN="$<TARGET_FILE:fuzz_soak>"
        STS_FUZZ_REPRO_BIN="$<TARGET_FILE:fuzz_repro>"
        STS_TEST_BASH="${STS_TEST_BASH}"
        STS_FUZZ_SOAK_SCRIPT="${CMAKE_SOURCE_DIR}/tools/fuzz/soak.sh")

# The capture planner (tools/oracle_bridge/planner). Tests the TOOL: that the
# EventId <-> name table covers every generated event row, that the event_flags
# bit layout is read as (id-1), that a ScanRow is a byte-identical function of
# its ScanCase, and that the min-hit-count rule excludes the single-hit seeds it
# exists to exclude. Every one of those, wrong, produces a plausible-looking but
# WRONG capture seed list rather than an error -- which is why they are pinned
# here rather than noticed during a capture.
sts_add_test(seed_scan_test LIBS seed_scan_core)

# B5.3: the dependency-free chi-square survival function, impossible-support
# handling, and the pre-registered Holm-Bonferroni step-down procedure. The
# >=10k-seed analytic sweep itself is tools/dist_check/dist_check and remains a
# campaign rather than a per-commit ctest.
sts_add_test(dist_check_test LIBS dist_check_stats)

# T0.6: the belief sampler's DISTRIBUTIONAL suite. Same split as dist_check
# above -- the per-commit ctest runs it in SMOKE mode (the pre-registered
# statistics at N/10, seed sweep skipped, ~a second), and the nightly job
# (tools/dist_check/sampler_dist.sh, .github/workflows/nightly.yml) sets
# STS_SAMPLER_DIST_MODE=nightly for the full pre-registered sample sizes. The
# hypothesis family, its alpha and the family-wise accounting are pre-registered
# in the file's header comment; the negative-control mutants always run at the
# nightly N so the smoke run still proves the suite has power.
sts_add_test(sampler_dist_test LIBS sts_engine dist_check_stats)
add_test(
    NAME dist_check_spot_python_test
    COMMAND "${STS_PYTHON_EXECUTABLE}" -B -m unittest -v test_oracle_spot.py)
set_tests_properties(dist_check_spot_python_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/tools/dist_check")
