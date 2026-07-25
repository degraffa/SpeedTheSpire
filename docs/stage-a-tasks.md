# Stage A Task Ledger

Execution tracker for [stage-a-design.md](stage-a-design.md) (the frozen spec —
**this file never overrides it**; on any conflict, the design doc wins and this
file gets fixed). Target milestone: **M1** — walking skeleton passes the diff
harness on the 5-card micro-game.

**Stage A is complete.** All 18 task blocks are `[x]`; milestone **M1** and gate
**G3** landed at tag `m1-walking-skeleton` (131/131 in debug, asan and release,
20 combat fixtures zero-diff). Nothing here is open work — this file is the
index of what Stage A shipped, kept in the canonical reading order because the
mechanics it froze (RNG, state layout, the queue pump, the damage pipeline,
pile ops) are still in force for every Stage B task.

**The full Logs live verbatim in [stage-a-log.md](stage-a-log.md)** and appear
here as one-line index entries; each links to its anchor. Stage A's own
"Orchestrator protocol" and "Working agreements" sections moved with them, into
that file's historical appendix.

## Protocol

The rules every task in this repo obeys — statuses, git discipline, the
canonical reference reading order, the precedence chain, hygiene, and the build
commands — live once in **[conventions.md](conventions.md)**. It supersedes
Stage A's own wording of them (preserved unaltered in
[stage-a-log.md](stage-a-log.md#appendix--stage-as-own-working-agreements-historical-verbatim)).
The active ledger is [stage-b-tasks.md](stage-b-tasks.md).

---

## Phase 0 — Golden-vector capture (oracle for tier-1 tests)

- **A0.1** `[x]` JVM golden-capture harness — `tools/golden_capture/regen.ps1` captures all 6 golden categories over the 38-seed §3.7 battery → 306 files / ~7.6 MB under `tests/golden/`; two consecutive regen runs SHA-256-identical; recorded a §3.4 paraphrase correction (per-act-class multiplier 1/100/200/300, numeric result unchanged) · [log](stage-a-log.md#a01)

---

## Phase 1 — RNG trio (Gate G1)

- **A1.1** `[x]` ∥ `RandomXS128` bit-exact — header-only `rng_xs128.hpp`; first 10k `next_long` byte-match all 38 `xs128_*.bin`; traps 4 (rejection loop not modulo), 5 (seed 0 ≡ INT64_MIN), 11 (`nextFloat` narrows from double) named tests; 6/6 · [log](stage-a-log.md#a11)
- **A1.2** `[x]` ∥ JDK LCG + `Collections.shuffle` — `rng_jdk.hpp` 48-bit LCG + exact Fisher–Yates; 38 seeds × n ∈ {5,10,71,128} (152 golden files) element-exact, zero mismatches; trap 2 (JDK LCG, not xorshift) named; 4/4 · [log](stage-a-log.md#a12)
- **A1.3** `[x]` Game `Random` wrapper (`RngStream`) — 24-byte POD + one free function per `Random.java` wrapper, each enforcing the §3.2 one-draw invariant; `from_seed_counter`, `floor_stream` (trap 7), `map_stream` (+1/+200/+600/+1200); golden sets 2, 3 and 5 all match bit-for-bit; trap 3 (inclusive bounds); 18/18 · [log](stage-a-log.md#a13)
- **A1.4** `[x]` ∥ `SeedHelper` conversion — `seed_string.hpp` base-35 both directions: unsigned encode, O→0 sterilization, signed-wrapping decode; golden set 6 round-trips both ways; trap 6 named; 7/7 · [log](stage-a-log.md#a14)
- **G1** `[x]` **Gate: tier-1 RNG suite green** — tag `g1-rng-green` — all four RNG binaries 18/18 in debug **and** asan; traps 2, 3, 4, 5, 6, 7, 11 each a named passing test; the existing CI sanitize matrix already auto-discovers them, no workflow change needed · [log](stage-a-log.md#g1)

---

## Phase 2 — State structs

- **A2.1** `[x]` Core ids and card instance types — `types.hpp`: u16 `CardId`/`PowerId`/`MonsterId`/`RelicId` enums with `NONE = 0` as the empty-slot sentinel, `CardInstance` 8 B, `PowerSlot` 4 B, `Action` 4 B packing `verb|arg0|arg1|arg2` low-byte-first + `make_action`/accessor helpers; 25/25 · [log](stage-a-log.md#a21)
- **A2.2** `[x]` `CombatState` / `RunState` — `CombatState` **3312 B**, `RunState` **1648 B**, both plain aggregates (value-init zero-fills padding) with in-header trivially-copyable + size-ceiling asserts; single `SCHEMA_VERSION` (= 1) in `schema.hpp`; xxh3 `hash_state` over raw bytes (xxHash 0.8.2 header-only via `FetchContent`); **§3.4-vs-§4.3 stream-count conflict found and resolved** — 7 run-scoped streams + `map_rng`, §3.4 wins; 32/32 · [log](stage-a-log.md#a22)

---

## Phase 3 — Action-queue pump (no cards yet)

- **A3.1** `[x]` Queue structures + pump loop — the §5.1 rings + §5.2 `pump()`, factored as `pump_step()` so ordering is testable without an interpreter; end-turn sentinel = card-queue item with `card_index == 255`; block-decay branch structure present, default path live; `MonsterTurnFn` function-pointer seam for A3.2; **gap-fix**: the missing 4th (`pre_turn_actions`) ring added additively, `sizeof(CombatState)` 3312 → **3504**; **source-vs-recipe reconciliation**: `monsterAttacksQueued` is cleared at the end-turn sentinel, not in start-of-turn; trap 9; 40/40 · [log](stage-a-log.md#a31)
- **A3.2** `[x]` Jaw Worm AI + monster turn — A20 stats and move table with the forced first-move Chomp; draw accounting documented and tested (one `aiRng.random(99)` per decision plus a `randomBoolean` on tiebreak branches, so the counter advances by 1 or 2 per turn; `monsterHpRng` once at init); INDEPENDENT hand-derived oracle `tests/fixtures/jaw_worm_fixture.tsv` (32 seeds × 20 turns) from a from-scratch Python re-derivation, regen SHA-256-stable; 51/51 · [log](stage-a-log.md#a32)

---

## Phase 4 — Effect interpreter + the five cards

- **A4.1** `[x]` Opcode interpreter + damage pipeline — `interp.cpp`: §6's opcodes numbered 1..8 with `NOP = 0` reserved; both `DamageInfo.applyPowers` ownership branches exact, accumulated in `float` and floored ONCE via a bit-faithful `mathutils_floor`, clamped ≥ 0; Strength/Vulnerable/Weak hooks, `atDamageFinal*` and stance as documented identity stubs; APPLY_POWER encodes its `PowerId` in `flags`; trap 1 (float accumulation — 7/Str2/Vuln → 13, and base-5 Weak+Vuln where per-step integer math diverges); 62/62 · [log](stage-a-log.md#a41)
- **A4.2** `[x]` Draw/discard/reshuffle + energy — `piles.cpp` owns every pile mutation; **stop-the-line ledger correction**: the hand-size-10 rule is `amount = min(amount, 10 - hand.size())` applied ONCE up front (`DrawCardAction.java:92-97`), NOT draw-then-discard; reshuffle = exactly one `shuffleRng.randomLong()` seeding a JDK LCG over the discard list, appended front-to-back so `draw[count-1]` is the top card; the game's animation-paced draw/shuffle action split collapsed into one synchronous loop (identical piles, identical draw order and count); trap 2; 80/80 · [log](stage-a-log.md#a42)
- **A4.3** `[x]` Five skeleton cards + card-play flow — `constexpr` `CardDef` registry encoding §6's `{op, target, amount, flags}` effect programs (the exact shape Stage B codegen emits), each entry mirroring its `use()`'s `addToBot` order; `queue_card_play` + `resolve_card_play` wired into pump step 3; the two-cycle `useCard`/`UseCardAction` split collapsed with a documented justification (no skeleton listener has a real body); trap 10 (random target rolls at dequeue, never at enqueue); 3-turn hand-traced integration to an exact state hash (with a documented `NormalizeScratch` for stale bytes past `count`); **gap-fix**: `start_of_turn` never refilled energy — `kIroncladBaseEnergy = 3` added; 90/90 · [log](stage-a-log.md#a43)

---

## Phase 5 — Batch API + observation stub

- **A5.1** `[x]` `advance()` batch + legal-action mask — `combat_begin`/`advance`/`legal_actions`, heterogeneous and allocation-free; turn-1 setup reuses `pump()` rather than a hand-rolled path; **deck-shuffle provenance found and pinned**: `initializeDeck` uses the SAME one-`randomLong()`→JDK-LCG→Fisher-Yates mechanism as the in-combat reshuffle; `ActionMask` and `StepResult` shapes chosen (§7 left them unspecified), reward documented as a placeholder; **benchmark baseline ≈20.7 M steps/s** (release, 12-core 3.6 GHz, 10k-state batch, no target at M1); 96/96 · [log](stage-a-log.md#a51)
- **A5.2** `[x]` ∥ Observation encoder stub — `observation.hpp`: 188-byte trivially-copyable `ObsBuffer`, version-stamped with `SCHEMA_VERSION`, single linear pass, `noexcept`, fully overwrites its output; per-monster power cap 4 with a true `power_count` so truncation is detectable; zero-allocation proven by a TU-local `operator new`/`delete` counter (all forms incl. nothrow); 39/39 · [log](stage-a-log.md#a52)

---

## Phase 6 — Diff harness + M1 acceptance (Gate G3)

- **A6.1** `[x]` Trace format + differ — `tools/diff_harness/`: 24-byte `STS0` trace header with two documented §8 extensions (`record_count`, `seed`) and a hard `schema_version` refusal on read; SEMANTIC `diff_states` walking live `[0,count)` ranges with debuggable field names (`monsters[0].powers[1].amount`) and the 5 RNG streams compared individually; `STSREPRO v1` reproducer that replays to the same diff; `OracleAdapter` + `FixtureFileOracleAdapter` (fixture format IS the trace format), CommunicationMod impl left a documented Stage-B seam; 128/128 · [log](stage-a-log.md#a61)
- **A6.2** `[x]` Fixture oracle: 20 scripted Jaw Worm fights — 20 `.trace` fixtures built by an INDEPENDENT reference simulator (`tools/fixture_gen/`, reusing only the G1-golden RNG primitives), replayed through the real engine with **zero diffs across all 183 per-action state comparisons**; coverage confirmed for all 5 cards, reshuffle, player death, monster death, and concurrent Bellow-Strength + Bash-Vulnerable; **engine bug found and fixed (stop-the-line)**: `jaw_worm_take_turn` was still A3.2's no-op-EXECUTE stub and never enqueued its move effects, so a Jaw Worm dealt zero damage through `advance()`; 130/130 · [log](stage-a-log.md#a62)
- **G3** `[x]` **Gate: M1 exit** — tag `m1-walking-skeleton` — 131/131 in debug, asan **and** release; all 20 fixtures zero-diff; batch determinism green in all three presets; benchmark baseline recorded; **all 11 design-doc §10 traps now have a named passing test** — trap 8 (relic acquisition order) had none and got `RunStateTrap.RelicsPreserveAcquisitionOrder`, pinned at the level testable in a relic-less skeleton · [log](stage-a-log.md#g3)

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

- 2026-07-24 — **document restructure (no mechanic changed).** This ledger was
  973 lines and entirely complete, yet still carried every task's full Log
  inline while sitting in the canonical reading order for frozen Stage A
  mechanics. The 18 completed task blocks (58,299 B of text) moved
  byte-for-byte to the new [stage-a-log.md](stage-a-log.md) and are represented
  here by one-line index entries, matching the treatment
  [stage-b-tasks.md](stage-b-tasks.md) / [stage-b-log.md](stage-b-log.md)
  received the same day. Stage A's "Orchestrator protocol" and "Working
  agreements" sections moved verbatim into that archive's historical appendix
  and are superseded by [conventions.md](conventions.md), now the single
  authoritative copy. Byte-identity of every archived block was proven
  mechanically (sha256 over the concatenated blocks, extracted independently
  from `8174e32` and from the archive) before the commit. Stage A's Logs were
  also swept for deferred obligations; the findings are recorded in the sweep
  note below. No task's Deps / Deliverables / Acceptance / Log text was
  altered.
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
- 2026-07-17 — A6.2: engine gap-fix recorded in design-doc §12 change log
  ("A6.2 — Jaw Worm move effects never attached to the monster turn"). A3.2's
  `jaw_worm_take_turn` was a no-op-EXECUTE stub whose scope note deferred the real
  damage/block/Strength enqueues to "A4.x"; that hand-off was never taken, so a
  Jaw Worm dealt zero damage through `advance()`. A6.2 attaches the effects
  (JawWorm.takeTurn order, JawWorm.java:120-146), enabling the player-death and
  Bellow-Strength/Vulnerable coverage. Java (§1 precedence) is the source; not a
  frozen-mechanics change (the walking skeleton §9 always specified a fighting
  monster). `cards_test`/`jaw_worm_test` updated in the same commit.
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

## Deferred-obligation sweep (2026-07-24)

Every Stage A Log was swept at the archive pass for forward-looking obligations
(case-insensitive search for *deferred / defer / belongs to / revisit /
unreachable / placeholder / stub / pending / hand-off / not yet / Stage B*).
Each hit was then checked against [stage-b-log.md](stage-b-log.md), the
**Deferred obligations** table and open task entries in
[stage-b-tasks.md](stage-b-tasks.md), and the live source — not inferred.

**Discharged inside Stage A**

- A3.1's missing `pre_turn_actions` ring — fixed in A3.1 itself.
- A3.2's move-effect enqueues, deferred to "A4.x" and never taken — found and
  taken by **A6.2**.
- A4.1's `SHUFFLE_IN` / `op_draw` / `op_exhaust` dispatch stubs — taken by
  **A4.2**.
- A4.3's missing start-of-turn energy refill — fixed in A4.3.
- G1's "traps 1, 8, 9, 10 belong to later phases" — all 11 covered by **G3**.

**Discharged in Stage B (verified against the archive / source)**

- A4.1's `ROLL_MOVE` "reserved for future data-driven AI" — **B3.17**:
  "ROLL_MOVE (opcode 8) is now real", dispatching a per-monster queued roll.
- A4.1's Paper Frog ×1.75 "unreachable" Vulnerable branch — **B3.25** states
  the stage-a A4.1 inline note is "RETIRED in this commit". Its Odd Mushroom
  ×1.25 twin is already a table row (owner B3.26). Paper Crane is Silent-only
  and B3.25 records it as staying unreachable — no S1 owner needed.
- A5.1's "exact Ironclad A20 starting HP deferred to Stage B (design §11)" —
  **B4.4**: `run_advance.cpp` sets 80/80 with real provenance (`CharSelectInfo`,
  Ironclad.java:114). The residual A6 / A10 / A14 ascension modifiers are
  already a table row (owner B4.15).
- A5.1's single-monster `ActionMask` ("Stage B multi-monster needs real target
  enumeration") — **B3.12**: `ActionMask` gained
  `can_play_target[hand_slot][target]`.
- A5.2's observation stub sizing — **B3.12** grew `kObsMonsterCap` with
  `kMonsterCap` (`ObsBuffer` 188 → 240 B).
- A6.1's CommunicationMod `OracleAdapter` seam — **B1.6** landed
  `CommunicationModOracleAdapter`.
- A2.1's sentinel-only `RelicId` "pending Stage B's registry" — **B2.1/B2.2**
  (codegen) with the rows landing at B3.24/B3.25 (`registry/relics.yaml`).

**Still outstanding — already carried by Stage B before this sweep**

- A3.1's structural Barricade block-decay branch — table row, owner **B3.8**.
- A2.2's placeholder RunState boss ids / event+shop one-shot flags — the live
  consumers are the translator rows "real `act_boss`" (UNASSIGNED) and the
  `eventList`/`shrineList`/`specialOneTimeEventList` bitsets (owner B4.10).

**Still outstanding — ADDED to Stage B's Deferred obligations table by this
sweep** (both were named only inside open task **B3.26**'s Deliverables /
Acceptance text, never in the table, which the ledger calls "the live carrier")

- A3.1's **Calipers** block-decay branch (loses 15 instead of zeroing) —
  `action_queue.cpp`'s `has_calipers` is still a hard `false`.
- A4.3's **`EnergyManager.recharge()` SET-to-constant** simplification, exact
  only while no relic/power reaches the Ice Cream / Conserve branches — Ice
  Cream is the S1 consumer.

**No owner needed**

- A3.1's third block-decay branch, **Blur** — a Silent card, outside S1.
- A4.1's `atDamageFinal*` and player-stance identity stubs — S1 Ironclad is
  stanceless and no S1 content reaches them.
- A5.1's placeholder `StepResult` reward — reward shaping is explicitly
  training-loop scope, in a separate repo.
- The A2.2 §3.4-vs-§4.3 stream-count wording fix flagged for "a future
  design-doc v0.3" — the design docs are frozen; the change-log entry above is
  the durable record.
