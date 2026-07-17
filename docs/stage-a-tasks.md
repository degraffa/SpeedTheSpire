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

### A1.1 `[ ]` ∥ `RandomXS128` bit-exact
**Deps:** A0.1 · **Spec:** §3.1 · **Provenance:** RandomXS128.java
**Deliverables:** `include/sts/engine/rng_xs128.hpp` (header-only, constexpr-
friendly): `nextLong`, `nextInt(n)` with rejection loop, `nextDouble`,
`nextFloat` (double multiply then narrow), `nextBoolean`, murmur3 `setSeed`
with the seed-0→INT64_MIN rule, `setState/getState`.
**Acceptance:** gtest `rng_xs128_test` — byte-compare against golden set 1;
traps 4, 5, 11 from §10 each have a named test.
**Log:** —

### A1.2 `[ ]` ∥ JDK LCG + `Collections.shuffle`
**Deps:** A0.1 · **Spec:** §3.3 · **Provenance:** CardGroup.java:561-567
**Deliverables:** `include/sts/engine/rng_jdk.hpp`: 48-bit LCG (`nextInt`,
`nextInt(bound)` incl. power-of-two and rejection paths) + `jdk_shuffle(span)`
exact Fisher–Yates.
**Acceptance:** gtest `rng_jdk_test` — permutations match golden set 4
element-exactly.
**Log:** —

### A1.3 `[ ]` Game `Random` wrapper (`RngStream`)
**Deps:** A1.1 · **Spec:** §3.2, §3.6 · **Provenance:** Random.java
**Deliverables:** `include/sts/engine/rng_stream.hpp`: 24-byte POD
`RngStream {s0, s1, counter, pad}`; free functions for every wrapper method
(inclusive bounds!); `from_seed(seed)`, `from_seed_counter(seed, n)` (replay
`random(999)` ×n), floor/act derivation helpers
(`floor_stream(seed, floor)`, `map_stream(seed, act)`).
**Acceptance:** gtest `rng_stream_test` — golden sets 2, 3, 5; trap 3 and 7
named tests; `static_assert` trivially-copyable + size 24.
**Log:** —

### A1.4 `[ ]` ∥ `SeedHelper` conversion
**Deps:** A0.1 · **Spec:** §3.5 · **Provenance:** SeedHelper.java
**Deliverables:** `include/sts/engine/seed_string.hpp` — base-35 both
directions (unsigned out, wrapping in), O→0 sterilization.
**Acceptance:** gtest `seed_string_test` — golden set 6 round-trips; trap 6
named test.
**Log:** —

### G1 `[ ]` **Gate: tier-1 RNG suite green**
**Deps:** A1.1–A1.4
All four RNG test binaries pass in debug **and** asan presets; suite wired
into CI workflow so it gates every later commit. Nothing in Phase 2+ starts
before this. Then: tag `g1-rng-green`, update CLAUDE.md "Current state".
**Log:** —

---

## Phase 2 — State structs

### A2.1 `[ ]` Core ids and card instance types
**Deps:** G1 · **Spec:** §4.2, §6
**Deliverables:** `include/sts/engine/types.hpp` — `CardId/PowerId/MonsterId/
RelicId` (u16 enums generated later; hand-written for skeleton set),
`CardInstance {card_id, upgrade, cost_now, flags, misc}`, `PowerSlot
{power_id, amount}`, `Action {u32}` verb encoding from §7.
**Acceptance:** `static_assert` sizes; smoke test constructs each.
**Log:** —

### A2.2 `[ ]` `CombatState` / `RunState`
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
**Log:** —

---

## Phase 3 — Action-queue pump (no cards yet)

### A3.1 `[ ]` Queue structures + pump loop
**Deps:** A2.2 · **Spec:** §5.1–5.2 · **Provenance:** GameActionManager.java:185-367
**Deliverables:** `src/engine/action_queue.cpp` + header: the four fixed
rings, `add_to_top/bottom`, `add_to_turn_start`, card-queue index-1 insertion
rule, end-turn sentinel; `pump(CombatState&)` implementing the §5.2 priority
order incl. the full start-of-turn sequence and block decay
(Barricade/Blur/Calipers branches present but only default path exercised).
**Acceptance:** gtest `action_queue_test` — ordering unit tests: top-vs-bottom
interleave, preTurn after actions, sentinel triggers end-of-turn path,
turn counter increments; trap 9 named test.
**Log:** —

### A3.2 `[ ]` Jaw Worm AI + monster turn
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
**Log:** —

---

## Phase 4 — Effect interpreter + the five cards

### A4.1 `[ ]` Opcode interpreter + damage pipeline
**Deps:** A3.1 · **Spec:** §5.5, §6 · **Provenance:** DamageInfo.java:35-100
**Deliverables:** `src/engine/interp.cpp`: opcodes `DAMAGE, BLOCK, APPLY_POWER,
DRAW, GAIN_ENERGY, SHUFFLE_IN, EXHAUST, ROLL_MOVE`; damage in float, hook
order per §5.5; powers for skeleton: Strength, Vulnerable (player+monster
sides), Weak (cheap to add, needed by tests).
**Acceptance:** gtest `damage_pipeline_test` — table-driven cases with
Strength/Vulnerable/Weak stacking checked against hand-computed values from
the cited Java (incl. float-rounding edge case: base 7, Str 2, Vuln → 13 not
13.5-rounded); trap 1 named test.
**Log:** —

### A4.2 `[ ]` Draw/discard/reshuffle + energy
**Deps:** A4.1, A1.2 · **Spec:** §3.3, §9 · **Provenance:** CardGroup.java:561-567
**Deliverables:** pile ops on `CombatState` (draw with empty-pile reshuffle via
`shuffleRng.randomLong()` → JDK shuffle; discard; exhaust), energy
gain/spend, hand-size-10 overflow rule (drawn card goes to discard).
**Acceptance:** gtest `piles_test` — reshuffle permutation matches golden JDK
shuffle for known stream state; shuffleRng counter advances by exactly 1 per
shuffle; trap 2 named test.
**Log:** —

### A4.3 `[ ]` Five skeleton cards + card-play flow
**Deps:** A4.1, A4.2 · **Spec:** §5.3, §9
**Deliverables:** registry entries (hand-coded constexpr table for skeleton):
Strike, Defend, Bash (+Vulnerable 2, 2-cost), Shrug It Off (block 8, draw 1),
Pommel Strike (9 dmg, draw 1); PLAY_CARD action → cardQueue → §5.3 dequeue
flow (hook order stubs where no listeners exist yet, random-target roll at
dequeue present for trap 10 even though all five cards are fixed-target).
**Acceptance:** gtest `cards_test` — per-card table tests (tier-2 pattern:
constructed state → play → assert fields); full-turn integration: scripted
3-turn fight reaches exact expected state hash.
**Log:** —

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

### A5.2 `[ ]` ∥ Observation encoder stub
**Deps:** A2.2 · **Spec:** §7 (D0.3)
**Deliverables:** `encode_observation(const CombatState&, ObsBuffer&)` — flat
fixed layout (hp/energy/block, hand card ids+costs, monster hp/intent/powers);
layout documented in the header; version-stamped.
**Acceptance:** gtest round-trip spot checks; zero allocation (asan +
counting allocator in test).
**Log:** —

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
