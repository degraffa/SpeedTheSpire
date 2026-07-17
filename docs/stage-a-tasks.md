# Stage A Task Ledger

Execution tracker for [stage-a-design.md](stage-a-design.md) (the frozen spec —
**this file never overrides it**; on any conflict, the design doc wins and this
file gets fixed). Target milestone: **M1** — walking skeleton passes the diff
harness on the 5-card micro-game.

## Orchestrator protocol

- Statuses: `[ ]` todo · `[~]` in progress · `[x]` done · `[!]` blocked.
  Update the checkbox and the **Log** line of a task when its state changes.
- A task is **done only when its Acceptance block passes** — run the commands,
  don't infer. Tests land in the same change as the code they verify.
- Respect `Deps:`. Tasks with disjoint deliverables and satisfied deps may run
  in parallel (parallel-safe groups are marked ∥).
- **Gates** (G1–G3) are stop-the-line: nothing past a gate starts until the
  gate task is `[x]`.
- Every commit builds and tests green: `cmake --preset debug && cmake --build
  --preset debug && ctest --preset debug` (WSL Ubuntu-2404). Run the `asan`
  preset before marking any task with new engine code done.
- Provenance: any behavior taken from the game cites
  `ClassName.method (File.java:line)` into `D:\STS_BG_Mod\SlayTheSpireDecompiled`
  (see design doc §1). If implementation contradicts the design doc's reading
  of the Java, stop and re-read the cited source before coding around it.
- New engine code layout: public headers `include/sts/engine/`, sources
  `src/engine/`, tests `tests/`, golden files `tests/golden/`, JVM harness
  `tools/golden_capture/`.

## Working agreements (enforced on every task)

### Git discipline

- Work on `master` (local repo, no remote; if a remote appears, push tags too).
- **One task = one commit**, made at task completion. Gates always get their
  own commit. The ledger's checkbox + Log update goes **in the same commit**
  as the task's code — history and ledger never disagree.
- Commit message: subject `A1.1: <what changed>`; body lists the acceptance
  evidence (test target names that ran green, incl. asan) and cites
  provenance for any behavior derived from the game. Claude-authored commits
  carry the `Co-Authored-By: Claude` trailer.
- Commit only from a green tree — debug **and** asan presets pass; release
  too at gates. Never `--no-verify`, never amend or rebase committed work;
  fix forward.
- Tag gates: `g1-rng-green` at G1, `m1-walking-skeleton` at G3.
- **Never commit:** decompiled Java or anything from `sts-classes.jar`
  (license hygiene — the repo re-expresses, never copies), scratch/temp
  files, `build/`. Golden vectors are the exception: they are *outputs* of
  the game's algorithms, small, and land under `tests/golden/` (marked
  `binary` in `.gitattributes`).

### Canonical references (reading order when picking up a task)

1. This ledger — the task's Deps/Deliverables/Acceptance.
2. [stage-a-design.md](stage-a-design.md), the cited §§ — the frozen spec.
3. The decompiled Java at `D:\STS_BG_Mod\SlayTheSpireDecompiled` — all
   `File.java:line` citations resolve here. Read the cited method *before*
   implementing it, even when the design doc paraphrases it.
4. [InitialPlan.md](../InitialPlan.md) — intent and rationale only, never
   mechanics.
5. `D:\STS_BG_Mod\sts-classes.jar` + Java 25 (Windows host, not WSL) — for
   regenerating golden vectors via `tools/golden_capture/`.

**Precedence on conflict:** decompiled Java > design doc > this ledger >
InitialPlan. Discovering a conflict is stop-the-line: fix the losing document
in the same change and record it in the task Log and the change log below.

### Hygiene

- **No rule without its test** — the test lands in the same commit as the
  behavior (this is the definition of implemented, per InitialPlan §B).
- Golden mismatch or fixture divergence = stop-the-line. Debugging step 1 is
  always re-reading the cited Java; root cause goes in the task Log.
- No new third-party dependencies beyond GoogleTest/Benchmark (+ xxh3 when
  A2.2 needs it) without a design-doc change-log entry.
- Session-start ritual for any executing agent: `git status` + `git log
  --oneline -5`; a dirty tree or a `[~]` task with an empty Log gets
  investigated before new work starts.
- At each gate: update CLAUDE.md "Current state" so a fresh session orients
  correctly without reading history.

---

## Phase 0 — Golden-vector capture (oracle for tier-1 tests)

### A0.1 `[x]` JVM golden-capture harness
**Deps:** none · **Spec:** §3.7
**Deliverables:** `tools/golden_capture/` — small Java program(s) run against
`D:\STS_BG_Mod\sts-classes.jar` (Java 25 on Windows host, not WSL) + a script
that regenerates everything under `tests/golden/`; a README documenting the
regen command. Emits:
1. `xs128_<seed>.bin` — first 10k `nextLong` for the §3.7 seed battery.
2. `wrapper_<seed>.bin` — 1k draws per wrapper method/overload, with counters.
3. `counter_restore_<seed>.bin` — state after N mixed draws vs `Random(seed,N)`.
4. `jdk_shuffle_<seed>_<n>.bin` — shuffled 1..N permutations, N ∈ {5,10,71,128}.
5. `floor_derive_<seed>.bin` — 5 floor streams × floors 1–55 + mapRng ×4 acts.
6. `seedhelper.txt` — string⇄long round-trip pairs incl. negatives and O→0.
**Acceptance:** regen script produces byte-identical output on a second run;
golden files checked in (small, binary via `.gitattributes`).
**Log:** `tools/golden_capture/regen.ps1` captured all 6 categories (38-seed
battery: 0, 1, -1, INT64_MIN, INT64_MAX, "SPIRE", 32 fixed randoms) → 306
files / ~7.6 MB under `tests/golden/`. Verified, not inferred: (1) two
consecutive regen runs produce SHA-256-identical output for all 306 files
(`Get-FileHash` diff = empty); (2) `counter_restore_*` — direct 137-mixed-draw
state, `Random(seed,137)` ctor replay, and `setCounter(137)` replay all land
on identical `(s0,s1,counter)` for every seed, confirming design doc §3.2's
replay-equivalence claim by inspection; (3) `floor_derive_*` — all 5
floor-scoped streams are bit-identical at floor entry for every
seed/floor, confirming they share one seed value per
`AbstractDungeon.java:1747-1751`; (4) `seedhelper.txt` — all 39 round-trip/
O-sterilization rows have `match=1`. One paraphrase correction recorded in
`tools/golden_capture/README.md`: design doc §3.4's "`seed + actNum ×
{1,100,200,300}`" is actually `actNum` (1/2/3/4, one per act) times a
**per-class** constant (1/100/200/300), confirmed by reading all four act
dungeon constructors, not a single lookup table — numeric result unchanged
(`seed+1`, `seed+200`, `seed+600`, `seed+1200`).

---

## Phase 1 — RNG trio (Gate G1)

### A1.1 `[x]` ∥ `RandomXS128` bit-exact
**Deps:** A0.1 · **Spec:** §3.1 · **Provenance:** RandomXS128.java
**Deliverables:** `include/sts/engine/rng_xs128.hpp` (header-only, constexpr-
friendly): `nextLong`, `nextInt(n)` with rejection loop, `nextDouble`,
`nextFloat` (double multiply then narrow), `nextBoolean`, murmur3 `setSeed`
with the seed-0→INT64_MIN rule, `setState/getState`.
**Acceptance:** gtest `rng_xs128_test` — byte-compare against golden set 1;
traps 4, 5, 11 from §10 each have a named test.
**Log:** `tests/rng_xs128_test.cpp` (new target, gtest `rng_xs128_test`):
`RngXs128Golden.NextLongMatchesGoldenSet1ForEverySeedInBattery` byte-compares
first 10k `next_long()` draws against all 38 `tests/golden/xs128_<label>.bin`
files (label→seed parsed at runtime from `seed_battery.txt`) — bit-exact.
Trap 4 (`RejectionLoopNotSimplifiedToModulo`) forces the reject branch with
`n = 2^62+1` (~50% reject rate vs. the ~1e-16 typical-bound rate) and proves
the returned value differs from the single-draw modulo shortcut. Trap 5
(`SeedZeroScramblesAsInt64Min`) proves seed 0 and seed `INT64_MIN` produce
identical state and draw sequences. Trap 11 (`NextFloatNarrowsFromDouble`)
checks `next_float()` against a double-multiply-then-narrow reference, plus a
standalone (non-power-of-two-multiplier) demonstration that float-space and
double-space multiplication are not generally interchangeable — note:
`nextFloat()`'s own operand/constant (24-bit value × exact `2^-24`) makes the
two techniques provably coincide always for that specific formula, since
rounding commutes with exact power-of-two scaling, so no in-situ divergence
is reachable; recorded here since it's a one-line proof but easy to miss.
Verified, not inferred: `ctest --preset debug` and `ctest --preset asan`
both 6/6 green (`Smoke.*` ×2 + `RngXs128Golden.*` + `RngXs128Trap.*` ×3),
WSL Ubuntu-2404, `cmake --preset {debug,asan}` → `cmake --build` → `ctest
--output-on-failure`.

### A1.2 `[x]` ∥ JDK LCG + `Collections.shuffle`
**Deps:** A0.1 · **Spec:** §3.3 · **Provenance:** CardGroup.java:561-567
**Deliverables:** `include/sts/engine/rng_jdk.hpp`: 48-bit LCG (`nextInt`,
`nextInt(bound)` incl. power-of-two and rejection paths) + `jdk_shuffle(span)`
exact Fisher–Yates.
**Acceptance:** gtest `rng_jdk_test` — permutations match golden set 4
element-exactly.
**Log:** `include/sts/engine/rng_jdk.hpp` (header-only `JdkRandom` LCG +
`jdk_shuffle<T>(span<T>, JdkRandom&)`) and `tests/rng_jdk_test.cpp` added;
`tests/CMakeLists.txt` gained an additive `rng_jdk_test` executable block
(`STS_GOLDEN_DIR` compile definition resolves `tests/golden` regardless of
ctest's CWD). Verified, not inferred: `ctest --preset debug` and `ctest
--preset asan` both 100% pass (4/4 tests) — `RngJdk.ShuffleMatchesGoldenSetFour`
byte/element-compares all 38 seed-battery entries × `n ∈ {5,10,71,128}` (152
files) against `tests/golden/jdk_shuffle_<label>_<n>.bin`, zero mismatches;
`RngJdkTrap.ShufflesRouteThroughJdkLcgNotXorshift` (trap 2) passes. No ASan/
UBSan findings.

### A1.3 `[x]` Game `Random` wrapper (`RngStream`)
**Deps:** A1.1 · **Spec:** §3.2, §3.6 · **Provenance:** Random.java
**Deliverables:** `include/sts/engine/rng_stream.hpp`: 24-byte POD
`RngStream {s0, s1, counter, pad}`; free functions for every wrapper method
(inclusive bounds!); `from_seed(seed)`, `from_seed_counter(seed, n)` (replay
`random(999)` ×n), floor/act derivation helpers
(`floor_stream(seed, floor)`, `map_stream(seed, act)`).
**Acceptance:** gtest `rng_stream_test` — golden sets 2, 3, 5; trap 3 and 7
named tests; `static_assert` trivially-copyable + size 24.
**Log:** `include/sts/engine/rng_stream.hpp` (header-only, builds on A1.1's
`RandomXS128`): 24-byte POD `RngStream {s0,s1,counter,pad}` with in-header
`static_assert` trivially-copyable + `sizeof==24`; a free function per
`Random.java` wrapper (`random(int)`/`random(int,int)`/`random(long)`/
`random(long,long)`/`random_long`/`random_boolean`/`random_boolean(float)`/
`random()`/`random(float)`/`random(float,float)`), each a thin rehydrate→one
`next_long`→commit(state+counter++) enforcing the §3.2 one-draw invariant;
`from_seed`, `from_seed_counter` (137×`random(999)` replay, oracle-diff-only —
copy is the real restore path, documented inline), `from_seed_set_counter`
(137×`randomBoolean` consistency check), `floor_stream(seed, floor)` =
`seed+floor` (trap 7, one helper covers all 5 floor streams), `map_stream(seed,
act)` = `seed + act*mult[act]` giving offsets +1/+200/+600/+1200. New gtest
binary `tests/rng_stream_test.cpp` (target `rng_stream_test`, additive
`tests/CMakeLists.txt` block, `STS_GOLDEN_DIR` compile def). Verified by
running, not inferred: `ctest --preset debug` and `ctest --preset asan` both
`100% tests passed, 0 failed out of 18` (WSL Ubuntu-2404), including
`rng_stream_test`'s 5 cases. Golden set 2 (`wrapper_<seed>.bin`): all 38 seeds
× 10 method blocks × 1000 draws byte-match `(counter_after, result_bits)`
(float results compared as zero-extended raw IEEE-754 bits). Golden set 3
(`counter_restore_<seed>.bin`, N=137): all 3 blocks — direct mixed draws
(ids 0..9 cycling `i%10`), `Random(seed,137)` ctor replay, `setCounter(137)`
replay — match golden `(s0,s1,counter)` and each other bit-for-bit, confirming
the one-draw-invariant replay-equivalence. Golden set 5
(`floor_derive_<seed>.bin`): all 38 seeds × floors 1-55 × 5 (identical) stream
slots + 4 mapRng acts match recorded pre-draw `(s0,s1)`. Trap 3
(`RandomIntBoundsAreInclusive`) and trap 7
(`FloorReseedUsesFloorNumberAfterIncrement`) are named passing tests. No
ASan/UBSan findings.

### A1.4 `[x]` ∥ `SeedHelper` conversion
**Deps:** A0.1 · **Spec:** §3.5 · **Provenance:** SeedHelper.java
**Deliverables:** `include/sts/engine/seed_string.hpp` — base-35 both
directions (unsigned out, wrapping in), O→0 sterilization.
**Acceptance:** gtest `seed_string_test` — golden set 6 round-trips; trap 6
named test.
**Log:** Implemented `seed_to_string`/`seed_from_string`/`seed_valid_character`
in `include/sts/engine/seed_string.hpp`, replicating `SeedHelper.getString`
(unsigned base-35 encode via `uint64_t`, empty string for seed 0 since the
encode loop is `while (leftover != 0)`), `getLong` (uppercase + inline O→0
sterilization, `uint64_t`-accumulated signed-wrapping decode, `static_cast`
to `int64_t` at the return — well-defined mod-2^64 reinterpretation per
C++20), and `getValidCharacter` (O/o→0, else membership test, `kSeedInvalidChar`
sentinel for no mapping). New gtest binary `tests/seed_string_test.cpp`
appended to `tests/CMakeLists.txt` (additive block, existing
`sts_engine_tests` target untouched) parses `tests/golden/seedhelper.txt`
(`STS_GOLDEN_DIR` compile definition) at runtime: all 32 `RT` rows
round-tripped both directions and cross-checked against the recorded `match`
flag, the `OTEST` row's O-substituted string decodes to the same long, all 4
`VALIDCHAR` rows match, plus a dedicated `SeedStringTrap.
EncodeUnsignedDecodeSignedWrapping` test (INT64_MIN/-1 unsigned-encode
round-trips + an independent-derivation overflow check for decode's
signed-wrapping accumulation). Verified by running, not inferred: `cmake
--preset debug && cmake --build --preset debug && ctest --preset debug` and
the same for `asan` (WSL Ubuntu-2404) — both presets report `100% tests
passed, 7 tests` (2 pre-existing smoke tests + 5 in `seed_string_test`,
including the trap test), `ctest` discovering `seed_string_test` correctly
via the golden-dir compile definition.

### G1 `[x]` **Gate: tier-1 RNG suite green**
**Deps:** A1.1–A1.4
All four RNG test binaries pass in debug **and** asan presets; suite wired
into CI workflow so it gates every later commit. Nothing in Phase 2+ starts
before this. Then: tag `g1-rng-green`, update CLAUDE.md "Current state".
**Log:** Verified by running (not inferred), WSL Ubuntu-2404: `ctest --preset
debug` and `ctest --preset asan` both `100% tests passed, 0 tests failed out
of 18` — all four RNG binaries (`rng_xs128_test`, `rng_jdk_test`,
`seed_string_test`, `rng_stream_test`) plus the pre-existing smoke test.
Phase-1 trap coverage confirmed by grep: traps 2, 3, 4, 5, 6, 7, 11 each have
a named passing test (traps 1, 8, 9, 10 belong to later phases, not yet due).
`.github/workflows/ci.yml` already runs a `{sanitize: false, true}` matrix
via `ctest --test-dir build`, which auto-discovers every `gtest_discover_tests`
target via `add_subdirectory(tests)` — no CI changes needed, the four new
binaries are already gated on every push/PR. Tagged `g1-rng-green`; CLAUDE.md
"Current state" updated in the same commit.

---

## Phase 2 — State structs

### A2.1 `[x]` Core ids and card instance types
**Deps:** G1 · **Spec:** §4.2, §6
**Deliverables:** `include/sts/engine/types.hpp` — `CardId/PowerId/MonsterId/
RelicId` (u16 enums generated later; hand-written for skeleton set),
`CardInstance {card_id, upgrade, cost_now, flags, misc}`, `PowerSlot
{power_id, amount}`, `Action {u32}` verb encoding from §7.
**Acceptance:** `static_assert` sizes; smoke test constructs each.
**Log:** `include/sts/engine/types.hpp` (header-only, `sts::engine`):
`CardId`/`PowerId`/`MonsterId`/`RelicId` u16 enums, each with `NONE = 0` as
the empty-slot sentinel convention (real ids start at 1), hand-written for
exactly the M1 skeleton set (§9) — `CardId` gets the five skeleton cards,
`PowerId` gets Strength/Vulnerable/Weak, `MonsterId` gets Jaw Worm, `RelicId`
is sentinel-only pending Stage B's registry. `CardInstance {card_id: u16,
upgrade: u8, cost_now: u8, flags: u16, misc: u16}` = 8 bytes (`flags`/`misc`
documented as reserved-for-future-use, unused by the skeleton set).
`PowerSlot {power_id: u16, amount: i16}` = 4 bytes. `Action {bits: uint32_t}`
= 4 bytes packing `verb:8 | arg0:8 | arg1:8 | arg2:8` low-byte-first (this
file's concrete choice for §7's field order, documented inline), plus
`ActionVerb` enum (`PLAY_CARD, END_TURN, USE_POTION, CHOOSE`) and
`make_action`/`action_verb`/`action_arg0/1/2` helpers so no caller hand-rolls
bit shifts. All three struct types carry in-header `static_assert`
trivially-copyable + exact-size checks. New gtest binary
`tests/types_test.cpp` (target `types_test`, additive `tests/CMakeLists.txt`
block matching the existing per-task pattern) re-asserts the same size/
copyability checks as a regression guard and smoke-constructs one of every
type, including an `Action` round-trip test with distinct non-overlapping
byte values in every arg lane to catch shift/mask mistakes. Verified by
running, not inferred: WSL Ubuntu-2404, `cmake --preset {debug,asan}` →
`cmake --build` → `ctest --output-on-failure` — both presets report `100%
tests passed, 0 tests failed out of 25` (the 18 pre-existing Phase-0/1 cases
plus 7 new `TypesSmoke.*` cases). No ASan/UBSan findings.

### A2.2 `[x]` `CombatState` / `RunState`
**Deps:** A2.1 · **Spec:** §4.2–4.4
**Deliverables:** `include/sts/engine/combat_state.hpp`, `run_state.hpp`:
fixed-capacity groups per the §4.2/§4.3 tables (card pool 160, piles
10/128/128/128/8, 5 monsters × 24 powers, queues 64/16/5, the right RNG
streams in each struct), `SCHEMA_VERSION` constant in one header.
**Deliverables:** `src/engine/state_hash.cpp` — xxh3 over struct bytes;
value-initialization helpers so padding hashes equal.
**Acceptance:** gtest `state_test` — `is_trivially_copyable`, `sizeof
CombatState ≤ 4096`, `sizeof RunState ≤ 8192` static asserts; snapshot =
memcpy round-trip hash-equal; two value-initialized states hash-equal.
**Log:** `include/sts/engine/schema.hpp` holds the single `SCHEMA_VERSION`
constant (= 1); both structs expose it as `static constexpr kSchemaVersion`
(compile-time, no per-instance storage — a stored stamp would be zeroed by the
value-init every state undergoes). `combat_state.hpp`: `CombatState` = **3312
bytes** (≤ 4096, matches the §4.2 ~3.3 KB estimate) — header (phase/turn/flags),
player (int16 hp/max_hp/block/energy + stance/counters + 24 `PowerSlot`), shared
160-row `CardInstance` pool, piles as `uint8_t` pool-index arrays 10/128/128/
128/8 + counts, 5 × `MonsterState` (`{monster_id,hp,max_hp,block,flags,
move_history[3],intent,power_count}` + 24 `PowerSlot` = 112 B each), action ring
64 × `ActionQueueItem{opcode:u16,src:u8,tgt:u8,amount:i32,flags:u32}` (12 B),
card queue 16, monster queue 5, and the 5 floor-scoped `RngStream`s
(monster_hp/ai/shuffle/card_random/misc). `run_state.hpp`: `RunState` = **1648
bytes** (≤ 8192) — run_seed, 128 × `CardInstance` master deck, int16 hp/max_hp +
gold/ascension/act/floor, 40 × `RelicSlot{relic_id:u16,counter:i16}`
(acquisition order = trigger order, trap 8), 5 potion slots, 7×15 `MapNode
{room_type:u8,edges:u8}` grid, placeholder boss ids/keys/event+shop one-shot
flags, `card_blizz_randomizer`/`blizzard_potion_mod` pity counters, and 8
`RngStream`s = the 7 run-scoped streams (§3.4) + `map_rng`. Both structs are
plain aggregates (no user ctors) so `T{}` zero-fills incl. padding; both carry
in-header `static_assert` trivially-copyable + size-ceiling. `src/engine/
state_hash.cpp` implements `hash_state(const CombatState&)` /
`hash_state(const RunState&)` via `XXH3_64bits` over the raw struct bytes; xxHash
v0.8.2 added header-only (`XXH_INLINE_ALL`) via top-level `FetchContent` → an
`INTERFACE` include target linked PRIVATE into `sts_engine` (no sub-build, no
link lib). New gtest binary `state_test` (6th block in `tests/CMakeLists.txt`,
links `sts_engine`). **§3.4 vs §4.3 discrepancy found & resolved** (see change
log): §4.3's "8 run-scoped streams + mapRng" over-counts §3.4's authoritative
7-row run-scoped inventory; RunState carries 7 run-scoped + mapRng = 8 stream
fields, citing §3.4. Verified by running, not inferred: WSL Ubuntu-2404,
`cmake --preset {debug,asan}` → build → `ctest --output-on-failure` — both
presets `100% tests passed, 0 failed out of 32` (25 pre-existing + 7 new:
`StateLayout.*` ×2, `StateHash.*` ×5). The "two value-initialized states
hash-equal" case (`StateHash.TwoValueInitialized{Combat,Run}StatesHashEqual`)
passes AND `memcmp == 0` under both GCC-13 debug and asan — padding is
deterministically zeroed by value-init on this toolchain, confirming §4.1's
premise. No ASan/UBSan findings.

---

## Phase 3 — Action-queue pump (no cards yet)

### A3.1 `[x]` Queue structures + pump loop
**Deps:** A2.2 · **Spec:** §5.1–5.2 · **Provenance:** GameActionManager.java:185-367
**Deliverables:** `src/engine/action_queue.cpp` + header: the four fixed
rings, `add_to_top/bottom`, `add_to_turn_start`, card-queue index-1 insertion
rule, end-turn sentinel; `pump(CombatState&)` implementing the §5.2 priority
order incl. the full start-of-turn sequence and block decay
(Barricade/Blur/Calipers branches present but only default path exercised).
**Acceptance:** gtest `action_queue_test` — ordering unit tests: top-vs-bottom
interleave, preTurn after actions, sentinel triggers end-of-turn path,
turn counter increments; trap 9 named test.
**Log:** `include/sts/engine/action_queue.hpp` + `src/engine/action_queue.cpp`
(namespace `sts::engine`, added to the `sts_engine` library) implement the
§5.1 queue primitives (`add_to_bottom`/`add_to_top` main ring,
`add_to_turn_start` pre-turn ring, `add_card_to_queue_bottom`/`_top`,
`make_end_turn_sentinel`/`is_end_turn_sentinel`, `pop_action_front`/
`pop_pre_turn_front`) and the §5.2 `pump()` priority loop, factored as
`pump_step()` returning a `PumpOutcome` so ordering is unit-testable without an
effect interpreter (A4.1) — "execute" of an action item is a pop; a non-sentinel
`card_queue` head is a documented pop-and-discard stub pending A4.3. End-turn
sentinel = a `card_queue` item with `card_index == 255` (out of the 0..159 pool
range, distinct from `CardId::NONE == 0` which is a valid slot); A4.3 builds it
via `make_end_turn_sentinel()`. Start-of-turn queues a placeholder `DRAW` opcode
(`kOpcodeDrawCard == 3`, matching §6's opcode-list order; A4.1 owns the real
table) and runs the Barricade/Blur/Calipers block-decay branch STRUCTURE with
only the default `player_block = 0` path live. **Monster-turn extension point
(the A3.1↔A3.2 seam):** a plain function pointer `using MonsterTurnFn =
void(*)(CombatState&, uint8_t monster_index)` passed to
`pump(CombatState&, MonsterTurnFn = default_monster_turn)` — `default_monster_turn`
is a no-op stub A3.1 tests use; A3.2 passes real Jaw Worm AI. No monster-specific
behavior is hard-coded in the pump. **preTurnActions gap-fix (stop-the-line):**
A2.2's `CombatState` had only 3 of §5.1's 4 queues; A3.1 added a
`pre_turn_actions` ring (`kPreTurnActionQueueCap == 16`, same `ActionQueueItem`
element type) + `pre_turn_head/tail/count` + a `turn_has_ended` flag, additively.
`sizeof(CombatState)` 3312 → **3504** (≤ 4096; +192 B ring, bookkeeping bytes
absorbed into former alignment padding); `static_assert` still passes and A2.2's
`state_test` is undisturbed. **Second source-vs-recipe reconciliation
(documented in code + design doc change log):** the decompiled
`GameActionManager.java` sets `monsterAttacksQueued = true` but never `= false`
anywhere in the tree (grep-confirmed), which taken literally would let monsters
act at most once. The ledger's step-6 recipe put the reset in the start-of-turn
sequence, but that leaves it false *during the player's turn*, mis-firing step 4
on the first A4.3 card-play pump. Per precedence (Java/design-doc > this
ledger's parenthetical) and design doc §5.2 step 6's actual enumeration
(cardsPlayedThisTurn/turnHasEnded, NOT monsterAttacksQueued;
GameActionManager.java:333,349-351), A3.1 clears the flag at the end-turn
sentinel (step 3) and leaves it set through the whole next player turn; step 6
does not touch it. Verified by running, not inferred: WSL Ubuntu-2404,
`cmake --preset {debug,asan}` → build → `ctest --output-on-failure` — both
presets `100% tests passed, 0 failed out of 40` (32 pre-existing + 8 new:
`ActionQueueLayout.*`, `ActionQueueOrdering.*` ×2, `ActionQueueSentinel.*` ×2,
`ActionQueueMonster.*` ×2, `ActionQueueTrap.CardQueueFrontInsertionIsIndexOneWhenBusy`).
No ASan/UBSan findings.

### A3.2 `[x]` Jaw Worm AI + monster turn
**Deps:** A3.1 · **Spec:** §9 · **Provenance:** JawWorm.java:121-184,
AbstractMonster.java:765-775
**Deliverables:** monster registry entry (hand-coded table for skeleton):
A20 stats, move table with thresholds/history rules, first-move-Chomp;
`roll_move` consuming `aiRng.random(99)` + tiebreak `randomBoolean(p)` draws
exactly as cited; HP roll from `monsterHpRng.random(42,46)`.
**Acceptance:** gtest `jaw_worm_test` — for 32 seeds × 20 turns with END_TURN
only: HP roll, move sequence, and aiRng/monsterHpRng counters match a fixture
generated by hand-executing the cited Java logic (fixture generator script
checked in, its output reviewed against JawWorm.java once).
**Log:** `include/sts/engine/monster_jaw_worm.hpp` + `src/engine/
monster_jaw_worm.cpp` (added to the `sts_engine` lib): A20 stat constants
(JawWorm.java ascension>=17 branch: chomp 12, thrash 7 +5 block, bellow +5
Str/9 block; setHp(42,46) from the >=7 branch), move-id constants
`kMoveChomp=1/kMoveBellow=2/kMoveThrash=3` (byte-identical to JawWorm.java:
65-67), a `MonsterIntent{NONE,ATTACK,DEFEND_BUFF,ATTACK_DEFEND}` enum, and
`jaw_worm_init`/`jaw_worm_take_turn`. `jaw_worm_take_turn` is MonsterTurnFn-
compatible and passed straight to A3.1's `pump()` as its step-5 hook. Move
history is the existing 3-slot `MonsterState.move_history` (most-recent-first,
0 = empty sentinel; no length field needed since no move id is 0), sufficient
because only lastMove/lastTwoMoves' top two are ever read. **Draw-counting
convention (chosen + documented in the header, the generator, and the test):**
`jaw_worm_init` performs decision #1 -- rollMove's forced-first-move Chomp,
which STILL consumes one `aiRng.random(99)` draw (rollMove always draws) but
ignores its value; each of the 20 `jaw_worm_take_turn` calls executes turn K
(no-op -- no effect interpreter until A4.1, per A3.1's boundary) then rolls
decision #(K+1): one `aiRng.random(99)` plus, on the tiebreak branches, one
`aiRng.randomBoolean` (0.5625f/0.357f/0.416f source literals) -- so the aiRng
counter advances by 1 OR 2 per turn (recorded per-turn). `monsterHpRng` is
drawn once, at init (counter 1). 20 take-turn calls => decisions #1..#21 (the
turn-21 roll is faithfully performed by turn 20's RollMoveAction but never
executed). **Fixture** lives at `tests/fixtures/jaw_worm_fixture.tsv`, an
INDEPENDENT hand-derived oracle generated by `tests/fixtures/
gen_jaw_worm_fixture.py` (a from-scratch Python re-derivation of RandomXS128 +
the Random wrapper + the decision tree, stdlib-only; regen command +
per-branch JawWorm.java/AbstractMonster.java review documented atop the
script; two consecutive runs are SHA-256-identical). Separate dir from
`tests/golden/` because this is hand-derived, not JVM-captured; new
`STS_FIXTURE_DIR` compile def. New gtest binary `jaw_worm_test` (8th block in
`tests/CMakeLists.txt`): `DirectLoopMatchesFixture` byte-compares HP roll +
per-turn move id + `aiRng`/`monsterHpRng` (s0,s1,counter) for all 32 seeds
(r0..r31 from `seed_battery.txt`) x 20 turns; `EndTurnDrivenMatchesFixture`
re-drives seed r0 through the real A3.1 `pump()` with END_TURN sentinels
(exercising the MonsterTurnFn wiring) to the same fixture bit-for-bit;
`ForcedFirstMoveAndOneDrawEachStream` + `LoadsThirtyTwoSeeds` are sanity nets.
Provenance verified by re-reading JawWorm.java:120-184 + AbstractMonster.java:
431-491,712-715,765-775 before coding -- all prompt claims confirmed against
source (A20 = ascension>=17 branch; setMove appends at decision time;
lastTwoMoves requires the TWO most-recent both == m). Verified by running, not
inferred: WSL Ubuntu-2404, `cmake --preset {debug,asan}` -> build -> `ctest
--output-on-failure` -- both presets `100% tests passed, 0 failed out of 51`
(47 pre-existing + 4 new). No ASan/UBSan findings.

---

## Phase 4 — Effect interpreter + the five cards

### A4.1 `[x]` Opcode interpreter + damage pipeline
**Deps:** A3.1 · **Spec:** §5.5, §6 · **Provenance:** DamageInfo.java:35-100
**Deliverables:** `src/engine/interp.cpp`: opcodes `DAMAGE, BLOCK, APPLY_POWER,
DRAW, GAIN_ENERGY, SHUFFLE_IN, EXHAUST, ROLL_MOVE`; damage in float, hook
order per §5.5; powers for skeleton: Strength, Vulnerable (player+monster
sides), Weak (cheap to add, needed by tests).
**Acceptance:** gtest `damage_pipeline_test` — table-driven cases with
Strength/Vulnerable/Weak stacking checked against hand-computed values from
the cited Java (incl. float-rounding edge case: base 7, Str 2, Vuln → 13 not
13.5-rounded); trap 1 named test.
**Log:** `include/sts/engine/interp.hpp` + `src/engine/interp.cpp` (added to the
`sts_engine` lib): `enum class Opcode : uint16_t` and
`execute_opcode(CombatState&, const ActionQueueItem&)` dispatch + a pure
`compute_damage(state, src, tgt, base)` returning the floored/clamped output.
Provenance re-read + confirmed before coding: DamageInfo.applyPowers
(DamageInfo.java:35-100) — both ownership branches implemented exactly (monster-
owned: owner `atDamageGive` → target `atDamageReceive` → player-stance
`atDamageReceive` → `atDamageFinalGive`/`atDamageFinalReceive`; player-owned:
owner `atDamageGive` → player-stance `atDamageGive` → target `atDamageReceive`
→ finals), accumulated in a local `float`, floored ONCE at the end via a
bit-faithful `mathutils_floor` (`(int)((double)v+16384.0)-16384`,
MathUtils.java:217 — a floor, not round), clamped ≥0. Skeleton power hooks:
Strength `+amount` (StrengthPower.java:96), Vulnerable `×1.5f`
(VulnerablePower.java:70, the Odd Mushroom/Paper Frog relic branches
unreachable), Weak `×0.75f` (WeakPower.java:67, Paper Crane branch
unreachable); `atDamageFinal*` + player-stance are documented identity stubs
(skeleton is stanceless, §4.2). BLOCK = straight `block += amount`
(AbstractCreature.addBlock — no Str/Vuln/Weak/relic hook on block gain in this
version; hook site commented). **Decisions (documented in interp.hpp):**
(1) **Actor sentinel** — reused A3.1's existing `kActorPlayer == 0xFF` (monsters
are slots 0..4); did NOT mint a new constant. (2) **APPLY_POWER encoding** —
`flags` low-16 carry the `PowerId` (via `make_apply_power_flags`/
`apply_power_id_from_flags`), `amount` is the stack count, `tgt` the recipient;
A4.3 builds items this way. (3) **Opcode numbering** — reserved `NOP == 0` as a
safe no-op because A3.1/A3.2 tests push value-initialized `ActionQueueItem`s
(opcode 0) through `pump_step`; real opcodes are §6's set numbered 1..8, which
shifts real `DRAW` to 4 — updated `action_queue.hpp`'s `kOpcodeDrawCard` 3→4 and
added a `static_assert(kOpcodeDrawCard == (uint16_t)Opcode::DRAW)` in
`action_queue.cpp` (design doc §6 assigns no numeric values, so this is an
implementation choice, not a doc conflict). (4) **SHUFFLE_IN stub** — dispatch
slot only; the discard→draw reshuffle (shuffle_rng + JDK shuffle) is A4.2's
deliverable. (5) **ROLL_MOVE stub** — A3.2 already rolls Jaw Worm's move inside
`jaw_worm_take_turn` (tested, 51/51); opcode reserved for future data-driven AI,
`monster_jaw_worm.*` untouched. (6) **DRAW** — minimal non-empty/non-overflow
slice (top of draw → hand); empty-pile reshuffle and hand-cap-10 overflow are
documented A4.2 hand-offs. (7) **EXHAUST** — `amount` carries the card-pool
index; card located in `hand`, moved to `exhaust`. **Wiring:** `pump_step`
steps 1 & 2 now call `execute_opcode` on every popped action/pre-turn item
(additive to `action_queue.cpp`, no rewrite); NOP/unrecognized-opcode no-op path
keeps A3.1/A3.2's placeholder-item tests green. New gtest `damage_pipeline_test`
(9th block in `tests/CMakeLists.txt`): 10-row hand-computed
Strength/Vulnerable/Weak table (incl. the 7/Str2/Vuln→13 edge and a monster-
owned branch), `DamagePipelineTrap.FloatAccumulationFlooredOnceNoIntegerShortcuts`
(the 7/2/→13 case PLUS base-5 Weak+Vuln where float-once=5 diverges from
integer-per-step=4), per-opcode direct checks (BLOCK/APPLY_POWER/GAIN_ENERGY/
DRAW/EXHAUST), SHUFFLE_IN/ROLL_MOVE/NOP/unrecognized memcmp no-op checks, and two
`pump()`-wiring regressions (queued DAMAGE lands on the target; pre-turn
GAIN_ENERGY applies). Verified by running, not inferred: WSL Ubuntu-2404,
`cmake --preset {debug,asan}` → build → `ctest --output-on-failure` — both
presets `100% tests passed, 0 failed out of 62` (51 pre-existing + 11 new);
`action_queue_test` and `jaw_worm_test` still green after the pump wiring. No
ASan/UBSan findings.

### A4.2 `[x]` Draw/discard/reshuffle + energy
**Deps:** A4.1, A1.2 · **Spec:** §3.3, §9 · **Provenance:** CardGroup.java:561-567
**Deliverables:** pile ops on `CombatState` (draw with empty-pile reshuffle via
`shuffleRng.randomLong()` → JDK shuffle; discard; exhaust), energy
gain/spend, hand-size-10 overflow rule (**CORRECTED** — see Log: the drawn card
is NOT discarded; the draw amount is capped up front).
**Acceptance:** gtest `piles_test` — reshuffle permutation matches golden JDK
shuffle for known stream state; shuffleRng counter advances by exactly 1 per
shuffle; trap 2 named test.
**Log:** `include/sts/engine/piles.hpp` + `src/engine/piles.cpp` (added to the
`sts_engine` lib): `shuffle_discard_into_draw`, `draw_cards`, `exhaust_card`
(the last relocated verbatim from A4.1's `interp.cpp` `op_exhaust` so all pile
mutations live in one module). `interp.cpp` now delegates `Opcode::DRAW` →
`draw_cards`, `Opcode::SHUFFLE_IN` → `shuffle_discard_into_draw`, `Opcode::EXHAUST`
→ `exhaust_card` (the A4.1 `op_draw`/`op_exhaust` stubs removed; SHUFFLE_IN is no
longer a no-op). **STOP-THE-LINE correction (decompiled Java > this ledger's own
A4.2 deliverable prose, per the precedence rule; recorded in the design-doc change
log):** the deliverable line above originally read "hand-size-10 overflow rule
(drawn card goes to discard)". That is NOT the game's behavior. `DrawCardAction.
update()` (DrawCardAction.java:92-97) computes, ONCE up front before any card is
drawn, `if (amount + hand.size() > 10) amount += 10 - (amount + hand.size())`,
which is algebraically **`amount = min(amount, 10 - hand.size())`** — overflowing
cards are simply never drawn; there is no draw-then-discard. (And `AbstractPlayer.
draw()`, :1657-1665, refuses entirely when `hand.size() == 10`, a degenerate case
of the same formula.) Implemented the actual cap-before-draw rule. **Reshuffle
card-ordering** verified by re-reading `EmptyDeckShuffleAction.update()`
(:42-64) + `CardGroup.shuffle(Random)` (:565-567) + `Soul.shuffle` (Soul.java:
90-102): exactly one `shuffleRng.randomLong()` seeds a `java.util.Random` whose
LCG drives `Collections.shuffle`'s Fisher-Yates over the discard list, then each
card is walked front-to-back and `addToTop`'d (appended to the END) onto the
empty draw pile — so the draw pile list becomes the shuffled discard array in the
SAME order, `getTopCard` (== last element) drawn first. Our `draw[draw_count-1]`
== top convention mirrors that list, so the engine `std::copy`s the shuffled
discard onto the `draw[]` tail (trap 2: JDK LCG, NOT xorshift128+). **Headless
timing simplification:** the game splits reshuffle-then-continue across animated
`AbstractGameAction`s (`DrawCardAction` re-queues itself around an
`EmptyDeckShuffleAction`) purely for frame pacing; `draw_cards` collapses this
into one synchronous loop (draw; if the draw pile empties with cards still owed,
reshuffle in place and continue) — observably identical (same pile contents, same
RNG draws in the same order/count), documented in `piles.hpp`. Energy spend is
`GAIN_ENERGY` with a negative `amount` (no new opcode; A4.1's no-clamp decision
holds); documented in `interp.hpp`. New gtest binary `piles_test` (10th block in
`tests/CMakeLists.txt`): reshuffle-matches-golden-JDK-shuffle (independent oracle
= A1.2/A1.3's golden-tested `random_long`/`JdkRandom`/`jdk_shuffle` off a copied
stream), `shuffle_rng.counter` +1-per-shuffle across discard sizes {2,5,10,50,128}
(and unchanged on empty discard), `PilesTrap.ShufflesRouteThroughJdkLcgNotXorshift`
(engine == JDK-LCG route, != a same-stream xorshift-driven Fisher-Yates),
corrected hand-size cap (returns capped count, discard untouched; full-hand draws
zero), reshuffle-mid-draw (exactly one reshuffle, fills the requested amount),
empty-both-piles (returns 0, no random_long drawn), exhaust, energy gain/spend,
and DRAW/SHUFFLE_IN/EXHAUST dispatch-wiring checks. A4.1's
`InterpStub.ShuffleInAndRollMoveDoNotMutateState` was narrowed to
`InterpStub.RollMoveDoesNotMutateState` (SHUFFLE_IN is now implemented; its
behavior moved to `piles_test`). Verified by running, not inferred: WSL
Ubuntu-2404, `cmake --preset {debug,asan}` → build → `ctest --output-on-failure`
— both presets `100% tests passed, 0 failed out of 80` (62 pre-existing A4.1-era
+ 18 new `Piles*`). No ASan/UBSan findings.

### A4.3 `[x]` Five skeleton cards + card-play flow
**Deps:** A4.1, A4.2 · **Spec:** §5.3, §9
**Deliverables:** registry entries (hand-coded constexpr table for skeleton):
Strike, Defend, Bash (+Vulnerable 2, 2-cost), Shrug It Off (block 8, draw 1),
Pommel Strike (9 dmg, draw 1); PLAY_CARD action → cardQueue → §5.3 dequeue
flow (hook order stubs where no listeners exist yet, random-target roll at
dequeue present for trap 10 even though all five cards are fixed-target).
**Acceptance:** gtest `cards_test` — per-card table tests (tier-2 pattern:
constructed state → play → assert fields); full-turn integration: scripted
3-turn fight reaches exact expected state hash.
**Log:** `include/sts/engine/cards.hpp` (header-only `constexpr` registry) +
`include/sts/engine/card_play.hpp` + `src/engine/card_play.cpp`. **Effect-program
representation:** a data table (design doc §6 "effect programs are sequences of
{op, target, amount, flags}") — each `CardDef` carries base cost, `CardType`
(Attack/Skill), `needs_target`/`random_target` flags, and up to `kMaxCardSteps`
(=2) `CardEffectStep{Opcode, amount, extra, StepTarget}` where `extra` packs the
`PowerId` for APPLY_POWER via A4.1's `make_apply_power_flags` and `StepTarget`
(SELF/CARD_TARGET) defers the concrete actor index to play time. Chosen over a
per-card dispatch function because it is the literal encoding of §6's frozen
decision and the exact shape Stage B codegen will emit, keeping the interpreter
effect-program-driven. Each entry mirrors its `use()`'s `addToBot` order
(provenance: Strike_Red/Defend_Red/Bash/ShrugItOff/PommelStrike `.java`, each
read in full). `queue_card_play` (PLAY_CARD → cardQueue via
`add_card_to_queue_bottom`, no resolution) and `resolve_card_play` (wired into
`action_queue.cpp` `pump_step` step 3, replacing the A3.1 pop-and-discard stub):
hook-order no-op stubs → `++cards_played_this_turn` → trap-10 target resolution →
queue effect items via `add_to_bottom` (NOT applied inline — matches
`AbstractCard.use()`'s addToBot) → move card hand→discard → deduct `cost_now`
AFTER queueing (useCard order). **Hook-order-collapse decision** (documented in
card_play.hpp, consistent with A4.2's precedent): the real game's two-cycle
`useCard()`+queued `UseCardAction` split is animation-pacing only; the skeleton
has zero relics/powers/blights/stances with a real onPlayCard/onUseCard body, so
resolution collapses into ONE synchronous step at dequeue — verified against
GameActionManager.getNextAction (:193-280) / AbstractPlayer.useCard (:1358-1384)
/ UseCardAction that the only cross-cycle interaction would be a listener with a
real body (none exist), so the observable end state and RNG draw sequence are
identical. **Trap 10:** `random_target` flag + `roll_random_target`
(getRandomMonster over live monsters via one `card_random_rng` draw) +
`resolve_play_target` dispatch site; false for all five cards but reachable —
`CardPlayTrap.RandomTargetRollsAtDequeueNotEnqueue` proves the roll is a
dequeue-only op (enqueue and fixed-target cards never touch `card_random_rng`;
the roll consumes exactly one draw and excludes dead monster slots). **Tests**
(`tests/cards_test.cpp`, 11th `tests/CMakeLists.txt` block, gtest `cards_test`):
8 per-card table tests (all 5 cards + Bash-damage-before-Vulnerable, Strike-into-
Vulnerable ×1.5, out-of-range-hand-index reject), the trap-10 test, and the
3-turn integration test. **Integration hand-trace** (seed r0, Jaw Worm HP 44,
fixed moves CHOMP/BELLOW/THRASH from A3.2's golden fixture; the production
`jaw_worm_take_turn` still defers real damage per its A3.2 scope note, so the
test supplies the A20 move effects locally and reuses `jaw_worm_take_turn` for
the move/aiRng progression): T1 Bash (8→monster 44→36) + Vuln 2, Strike (6×1.5=9,
36→27), CHOMP 12 → player 80→68; T2 Defend (block 5), Pommel (9×1.5=13, 27→14) +
draw, BELLOW +5 Str/+9 block; T3 Shrug (block 8) + draw, THRASH 7+Str5=12 vs
block 8 → player 68→64. Final: player hp 64/energy 3/block 0, monster hp
14/block 14/Vuln 2/Str 5, move_history [Thrash,Thrash,Bellow], hand 10/draw
8/discard 5, turn 4. Expected `CombatState` built independently from those
hand-traced values (RNG end states taken from the A3.2 fixture); hashes matched.
**First run FAILED** (field checks green, hash mismatch): the byte diff exposed
`draw[8..17]` holding the drawn-out indices 13..22 — the fixed-capacity piles (like
the queue rings) leave stale bytes beyond `count` on removal. Handled by a
documented `NormalizeScratch` that zeroes the four drained queue rings AND the
dead pile tails `[count, cap)` on BOTH states before hashing (asserting all queue
counts are 0 first, so no live state is dropped); this is drained scratch, not
gameplay state (rules only read `[0, count)`). **Post-commit gap-fix:** the
initial submission worked around a real missing engine feature by hand-scripting
`player_energy = 3` at each turn start in the test, since `start_of_turn` never
refilled energy (design doc §5.2's prose omits it — see the §12 change-log entry
"A4.3 — energy recharge missing from the §5.2 start-of-turn sequence"). Fixed:
`kIroncladBaseEnergy = 3` added to `action_queue.hpp`; `start_of_turn` now sets
`player_energy = kIroncladBaseEnergy` unconditionally every turn; the test
scaffolding was removed and the hand-traced expected final energy corrected
2 → 3 (the same `pump()` call ending turn 3 also drains through start-of-turn 4,
which refills again before the assertions run — not a new bug, a trace error
exposed by removing the workaround). Verified by running, not inferred: WSL
Ubuntu-2404, `cmake --preset {debug,asan}` → build → `ctest --output-on-failure`
— both presets `100% tests passed, 0 failed out of 90` (80 pre-existing A4.2-era
+ 10 new: 8 `CardTable.*`, `CardPlayTrap.*`, `CardIntegration.*`); no build
warnings; `action_queue_test`/`jaw_worm_test`/`damage_pipeline_test`/`piles_test`
still green after the step-3 dispatch change AND after the energy-refill fix.
No ASan/UBSan findings.

---

## Phase 5 — Batch API + observation stub

### A5.1 `[ ]` `advance()` batch + legal-action mask
**Deps:** A4.3, A3.2 · **Spec:** §7
**Deliverables:** `include/sts/engine/advance.hpp`, `src/engine/advance.cpp`:
`advance(span<CombatState>, span<const Action>, span<StepResult>)` —
heterogeneous, allocation-free; `legal_actions()`; combat-start construction
of `CombatState` from (seed, floor, deck) — `combat_begin()`.
**Deliverables:** `benchmarks/bench_advance.cpp` — 10k-state batch random-legal
policy steps/sec (recorded, no target at M1).
**Acceptance:** gtest `advance_test` — batch of 128 states with mixed actions
advances independently (per-state hash equals single-state reference run);
determinism: same batch twice → identical hashes; asan clean.
**Log:** —

### A5.2 `[x]` ∥ Observation encoder stub
**Deps:** A2.2 · **Spec:** §7 (D0.3)
**Deliverables:** `encode_observation(const CombatState&, ObsBuffer&)` — flat
fixed layout (hp/energy/block, hand card ids+costs, monster hp/intent/powers);
layout documented in the header; version-stamped.
**Acceptance:** gtest round-trip spot checks; zero allocation (asan +
counting allocator in test).
**Log:** `include/sts/engine/observation.hpp` (header-only, `inline`
`encode_observation`, matching the `rng_stream.hpp` style). `ObsBuffer` =
**188 bytes** POD, trivially-copyable (in-header `static_assert` on both):
`schema_version:u32` (reuses the single engine `SCHEMA_VERSION`, stored as a
real field per §8's version-stamped-record convention — documented rationale
in the header), player `hp/max_hp/block/energy` (int16 ×4 matching
`CombatState`'s own widths), `hand_count:u16` + fixed `hand_card_id[10]:u16` /
`hand_cost[10]:i16` (unused slots padded with `kObsEmptyCardId`=0 /
`kObsEmptyCost`=-1), `monster_count:u16` + `monsters[5]` of `ObsMonster`
(26 B each = `{monster_id:u16, hp:i16, max_hp:i16, intent:u8, occupied:u8,
power_count:u8, pad0, ObsPower powers[4]}`). Per-monster power cap chosen as
**4** (not `CombatState`'s 24) — skeleton Jaw Worm carries ≤3 live powers
(Str/Vuln/Weak, §9), so 4 gives headroom while keeping the stub compact;
`power_count` still reports the true count so a consumer detects truncation.
`encode_observation` is a single linear pass, no heap allocation, `noexcept`,
fully overwrites `out`. New gtest binary `observation_test` (7th block in
`tests/CMakeLists.txt`, links `sts_engine`). Zero-allocation check implemented
as a **TU-local** global `operator new`/`operator delete` override (all forms
incl. nothrow, routed through one malloc/free pair so asan sees no
alloc-dealloc mismatch — the nothrow-form omission was the first-cut bug,
fixed) that bumps a counter; the test snapshots the counter tightly around a
single warmed `encode_observation` call and asserts no increase. Overrides are
in the test TU only, never in engine headers/lib. Verified by running, not
inferred: WSL Ubuntu-2404, `cmake --preset {debug,asan}` → build → `ctest
--output-on-failure` — both presets `100% tests passed, 0 failed out of 39`
(32 pre-existing + 7 new: `Observation.*` — player/hand/monster round-trips,
hand+monster sentinel padding, power truncation, and `EncodeDoesNotAllocate`).
No ASan/UBSan findings.

---

## Phase 6 — Diff harness + M1 acceptance (Gate G3)

### A6.1 `[ ]` Trace format + differ
**Deps:** A5.1 · **Spec:** §8, §9
**Deliverables:** `tools/diff_harness/` (C++ target): trace writer
(`{magic, schema_version, records}` per §8), field-by-field state differ with
named-field output, `(seed, action-prefix)` reproducer emitter; oracle
adapter interface (`OracleAdapter` — fixture-file impl now, CommunicationMod
impl in Stage B).
**Acceptance:** gtest `differ_test` — synthetic divergence in each field group
is caught and named; reproducer file replays to the same diff.
**Log:** —

### A6.2 `[ ]` Fixture oracle: 20 scripted Jaw Worm fights
**Deps:** A6.1, A4.3 · **Spec:** §9
**Deliverables:** `tests/golden/combat_fixtures/` — ~20 scripted fights
(varied seeds, all five cards exercised, ≥1 reshuffle, ≥1 player death,
≥1 monster death, Bellow-Str + Vulnerable overlap) with expected per-action
state fields derived from the cited Java; generator/derivation notes checked
in for re-review.
**Acceptance:** diff harness runs all fixtures with **zero diffs**.
**Log:** —

### G3 `[ ]` **Gate: M1 exit**
**Deps:** G1, A6.2, A5.1
Checklist (all must hold, evidence linked in Log):
- [ ] Tier-1 RNG suite green across seed battery (CI-gated since G1).
- [ ] All fixture fights zero-diff through the harness.
- [ ] Batch `advance()` determinism test green in debug, release, asan.
- [ ] Benchmark number recorded in this file (baseline, no target).
- [ ] Design doc §10 trap list: every trap has a named, passing test
      (grep test names ↔ trap numbers).
Then: tag `m1-walking-skeleton`, update CLAUDE.md "Current state", and open
Stage B planning (oracle bridge first, per InitialPlan §B.1).
**Log:** —

---

## Parallelism map

```
A0.1 ──▶ A1.1 ─┬▶ A1.3 ─┐
     ├─▶ A1.2 ─┼────────┤            (A1.1, A1.2, A1.4 ∥)
     └─▶ A1.4 ─┴────────▶ G1
G1 ──▶ A2.1 ─▶ A2.2 ─┬─▶ A3.1 ─┬─▶ A3.2 ──────────┐
                     └─▶ A5.2  ├─▶ A4.1 ─▶ A4.2 ─▶ A4.3 ─▶ A5.1 ─▶ A6.1 ─▶ A6.2 ─▶ G3
                               │        (A5.2 ∥ with Phases 3–5)
```

## Change log

- 2026-07-16 — v0.1 created from stage-a-design.md v0.1 §9 build order.
- 2026-07-16 — v0.2 added Working agreements (git discipline, canonical
  references + precedence, hygiene); G1 gains tag + CLAUDE.md step;
  `tests/golden/** binary` added to `.gitattributes`.
- 2026-07-16 — A2.2: intra-design-doc conflict recorded. `stage-a-design.md`
  §4.3 prose says RunState holds "the 8 run-scoped streams + mapRng"; §3.4's
  provenance-cited stream inventory table lists only **seven** run-scoped
  streams (monsterRng, eventRng, merchantRng, cardRng, treasureRng, relicRng,
  potionRng), with §3.6 stating "RunState holds all run-scoped streams plus
  mapRng". §3.4 (the authoritative table) wins per the precedence rule:
  `RunState` carries 7 run-scoped `RngStream`s + `map_rng` = 8 stream fields.
  §4.3's "8 run-scoped" reads as an imprecise count of that 7+mapRng set; left
  as-is (not a mechanics change), flagged here for a future design-doc v0.3
  wording fix.
- 2026-07-17 — A4.2: ledger-prose correction (decompiled Java > this ledger).
  The A4.2 deliverable line read "hand-size-10 overflow rule (drawn card goes to
  discard)". `DrawCardAction.update()` (DrawCardAction.java:92-97) does NOT
  draw-then-discard: it caps the draw amount ONCE up front —
  `if (amount + hand.size() > 10) amount += 10 - (amount + hand.size())`, i.e.
  **`amount = min(amount, 10 - hand.size())`** — so overflowing cards are never
  drawn. (`AbstractPlayer.draw()`, :1657-1665, also refuses when `hand.size()
  == 10`.) A4.2 implements the actual cap-before-draw rule; the deliverable line
  is annotated "CORRECTED — see Log". The design doc's §9 skeleton-scope text
  does not state the wrong rule (only this ledger's deliverable prose did), so
  no design-doc mechanics change is needed; this change-log entry plus the A4.2
  Log are the durable record.
