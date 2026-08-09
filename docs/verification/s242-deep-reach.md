# S2.42 deep-reach — measured reach report

Written by S2.42 (2026-08-07). Evidence accounting for the S2.42 acceptance
block in [../s2-tasks.md](../s2-tasks.md). The scan artifacts themselves stay
uncommitted under the design §7.3 data root, exactly as every prior campaign's
do; this report is the sanctioned exception
([conventions §2](../conventions.md#2-git-discipline)).

Its precedent is [te1-survival-cohort.md](te1-survival-cohort.md), whose 31.0 %
Act-1 driver reach is the number the driver half of S2.42 builds on.

> **UPDATED 2026-08-09 by S2.41.** §1's command was re-run on `master` +
> `s2.41` now that every S2.2x/S2.3x batch has landed, exactly as the deferred
> obligation row in [../s2-tasks.md](../s2-tasks.md) requires. **The Act-2/3
> cells below are no longer *pending content*: they are MEASURED, and what they
> measure is zero.** The structural wall is gone — no scanned row parks at
> `ROOM_UNIMPLEMENTED` any more — and what replaced it is a reach result. §11
> carries the re-run, its cross-check against the fuzz soak's new per-act
> coverage, and what the two together mean for the S2-G1 and S2-G2 bars.
> Everything above §11 that is *not* struck through is S2.42's record of what
> it measured on 2026-08-07 and is left as written.

---

## 0. The headline, stated honestly

**As S2.42 wrote it (2026-08-07):** Act-1 numbers in this report are measured.
Act-2 and Act-3 numbers are not measured, and they are not estimated either —
they are recorded as *pending content*. The reason was structural, not a
shortfall of the policy:

> the fuzz soak's `victories` counter now reads **0 for every soak until S2.28**
> lands the Act-3 bosses, because **no run can walk into an Act-2/3 room while
> its monsters are unimplemented**
> — S2.12 Log, [../s2-tasks.md](../s2-tasks.md)

An Act-2/3 combat room parked at `RunPhase::ROOM_UNIMPLEMENTED`
(`src/engine/run_advance.cpp`), and the first row of every act is a forced
Monster row (`map_rooms.hpp`) — so a policy-driven sim run could not take one
step into Act 2. Sim-side Act-2/3 reach was therefore **structurally 0**.

What S2.42 delivered was the **instrument and the report format**, plus the
Act-1 measurement, plus a live-game driver that plays all three acts. Nothing in
this document extrapolates a reach number.

**As S2.41 re-measured it (2026-08-09):** the wall is gone and the numbers did
not move. Act-2/3 reach is now **measured 0**, over the same 50,000 rows, with
`room_unimplemented` down from 8 rows to **0**. A scanned run now dies in Act 2
instead of parking in it. The honest reading is in §11: **sim-side policy reach
is the binding constraint on depth, and it is not a content problem.**

---

## 1. What ran

| | |
|---|---|
| Commit | branch `s242-deep-reach`, base `598667a` |
| Tool | `tools/oracle_bridge/planner/seed_scan`, **release** preset (the README warns debug/asan are 1–2 orders slower and the tool is throughput-bound) |
| Seeds | `STS00100`–`STS05099` — 5,000 sequential seeds |
| Policies | all five `fuzz::PolicyKind`: `random`, `greedy_damage`, `greedy_block`, `hoard_gold`, `always_event` |
| Policy seeds | `0, 1` |
| Rows | **50,000** = 5,000 × 5 × 2 |
| Ascension | 20 |
| `--max-actions` | **12,000** (raised from the Act-1-era 4,000 — §4) |
| Wall clock | 48.4 s, 1,033 rows/s, 96,235 actions/s (with `--verify-determinism`, so every case ran twice) |
| Artifacts | `D:\STS_BG_Mod\_oracle_data\s242\s242_scan.tsv`, `s242_summary.txt`, `s242_cohort_act1_kill.tsv` |

```bash
seed_scan --seeds STS00100-STS05099 \
          --policies random,greedy_damage,greedy_block,hoard_gold,always_event \
          --policy-seeds 0,1 \
          --verify-determinism \
          --out  $DATA/s242_scan.tsv \
          --summary $DATA/s242_summary.txt \
          --need-boss-kill-act 1 \
          --cohort-list $DATA/s242_cohort_act1_kill.tsv
```

---

## 2. Per-act boss-fight and boss-kill rates, per policy

The Acceptance sentence, verbatim. Denominator is **10,000 rows per policy**
(50,000 overall). "Fight" is `RoomType::Boss` observed at that act; "kill" is
`RunPhase::BOSS_TREASURE` observed at that act for Acts 1–2 and
`run_is_victory()` for Act 3 — see §3 for why those probes are exact.

| Policy | A1 fight | A1 kill | A2 fight | A2 kill | A3 fight | A3 kill | victories |
|---|---|---|---|---|---|---|---|
| `random` | 1 (0.01 %) | 0 (0.00 %) | 0 | 0 | 0 | 0 | 0 |
| `greedy_damage` | 101 (1.01 %) | 10 (0.10 %) | 0 | 0 | 0 | 0 | 0 |
| `greedy_block` | 61 (0.61 %) | 3 (0.03 %) | 0 | 0 | 0 | 0 | 0 |
| `hoard_gold` | 40 (0.40 %) | 1 (0.01 %) | 0 | 0 | 0 | 0 | 0 |
| `always_event` | 1,203 (12.03 %) | 45 (0.45 %) | 0 | 0 | 0 | 0 | 0 |
| **all** | **1,406 (2.81 %)** | **59 (0.12 %)** | **0** | **0** | **0** | **0** | **0** |

**The Act-2/3 columns were *pending content* when S2.42 wrote this table. S2.41
re-ran §1's command on 2026-08-09, after every S2.2x/S2.3x batch had landed, and
they are now MEASURED — at exactly the same values, 0.** The Act-1 columns
reproduced to the row (1,406 / 59), which is itself the check that the re-run
was the same experiment. Only the double-boss row is still blocked on content:

| Cell | State |
|---|---|
| Act-2 fight / kill | **measured 0** over 50,000 rows (was: blocked on S2.23/S2.24) |
| Act-3 fight / kill, victories | **measured 0** over 50,000 rows (was: blocked on S2.27/S2.28) |
| Double-boss runs (design §6 G2-3) | still unbuilt — needs the run-layer flag, see §7 and §10.2 |

The measured cell that is genuinely new: **nobody had measured sim-side boss
*kill* before.** TE.1 measured boss *reach* (0.80 % sim / 31 % driver); the
distance between 2.81 % reach and 0.12 % kill in the same 50,000 rows is the
number the S2-G2 depth bars actually live on, and it is 23× smaller.

The `always_event` outlier is not a surprise and is not a depth result: it
steers the map away from combat nodes, so it *arrives* at the boss far more
often on much less HP — 12.03 % fight against 0.45 % kill is a 27× gap, the
worst ratio of the five. `greedy_damage` has the best fight→kill conversion
(1.01 % → 0.10 %, 10×).

---

## 3. Why the two probes are exact rather than inferred

The boss chest is entered **only** through the boss reward's `proceed`
(`ProceedButton.goToTreasureRoom` — a full room transition), and Acts 1 and 2
both end in one, while Act 3's boss opens no reward screen and no chest at all
(`include/sts/engine/run_advance.hpp`, the Act-3 arm of
`finish_combat_after_action`). So:

```
act-N boss KILLED (N in 1,2)  <=>  RunPhase::BOSS_TREASURE seen at act N
act-3 boss KILLED             <=>  run_is_victory(rc)
act-N boss REACHED            <=>  RoomType::Boss seen at act N
```

The **phase** is the probe rather than `RoomType::TreasureBoss` (which is never
written into a grid node — it is only the resolved room type while the chest is
up, `map_rooms.hpp`), because the phase is what the fuzz `MoveCat` 28–31 buckets
key off and the two instruments should agree.

**The live capture driver runs the same pair of probes** against the protocol
dump (`campaign_driver.py::_observe_reach`: `room_type == "TreasureRoomBoss"` at
act N, and the GAME_OVER victory flag for act 3). That correspondence is
deliberate: a sim number and a driver number that disagreed would otherwise be
impossible to attribute.

`tests/seed_scan_test.cpp` pins that a kill never appears without its fight over
a real scanned sweep, and `APinnedDeepCaseExercisesEveryProbe` asserts all three
probes on one pinned row rather than waiting for a lottery (§6).

---

## 4. End-reason census, including the truncation witness

| End reason | Rows | % |
|---|---|---|
| `run_over` | 49,965 | 99.93 % |
| `room_unimplemented` | 8 | 0.02 % |
| `action_cap` | 27 | 0.05 % |

`action_cap` is reported deliberately. `ScanLimits::max_actions` was **4,000**,
an Act-1-era budget written when no run could leave Act 1; S2.42 raised it to
**12,000** because a three-act A20 run is roughly 3× the actions, and a
truncated deep run ends as `ACTION_CAP` — which in a *depth* scan reads as a
policy failure while actually being the tool's own truncation. That failure mode
is quiet, so the count now sits next to the reach numbers in every summary. At
12,000 it is 0.05 % overall and 0.26 % for `greedy_block` (the slowest policy),
i.e. not currently distorting anything.

The 8 `room_unimplemented` rows are the Act-2 wall: those runs killed the Act-1
boss, took the chest's `proceed` into Act 2, and parked on the first Act-2
monster room. They are the direct witness for §0.

**S2.41 re-run (2026-08-09): that row is now 0.** The census over the same
50,000 rows reads `run_over` 49,973 (99.95 %) / `action_cap` 27 (0.05 %) /
`room_unimplemented` **0**. The eight parked runs now fight their Act-2 rooms
and die in them, which is the cleanest single witness that the content wall is
gone — and, taken with §2's unchanged zeros, that removing it changed no reach
number.

---

## 5. Cohort schedulability — the triples

Acceptance: *"the S2-G2 depth cohorts are demonstrably schedulable from the scan
output."* The same scan emitted, from `--need-boss-kill-act 1 --cohort-list`:

```
# qualifying=59 triples over 55 distinct seeds
seed        policy          policy_seed  boss_reached_acts  boss_killed_acts  boss_ids
STS00243    always_event    0            1                  1                 act1=Slime Boss|act2=Automaton
STS00293    always_event    0            1                  1                 act1=Hexaghost|act2=Automaton
STS00345    greedy_damage   0            1                  1                 act1=Slime Boss|act2=Collector
…
```

**59 triples over 55 distinct seeds**, from 50,000 scanned rows.

Per-boss-identity breakdown of the Act-1-kill cohort — this is the column the
scheduler filters on for "every registry BOSS row" / "≥ 2 distinct identities":

| Act-1 boss | Cohort triples |
|---|---|
| Slime Boss | 29 |
| Hexaghost | 25 |
| The Guardian | 5 |

**All three Act-1 registry bosses are covered**, so the Act-1 half of a
per-boss-identity cohort is schedulable today. By source policy: `always_event`
45, `greedy_damage` 10, `greedy_block` 3, `hoard_gold` 1.

The Act-2 boss identity is *already* observable on these rows even though Act-2
content is not, because it is assigned at the act transition
(`run_advance.cpp`, `rs.boss_ids[next_act - 1]`) which these runs completed. All
three Act-2 bosses appear:

| Act-2 boss | Rows that crossed into Act 2 |
|---|---|
| Automaton | 24 |
| Champ | 18 |
| Collector | 17 |

So the *identity* dimension of the G2-2 Act-2 cohort is schedulable now; the
*reach* dimension is not, and is pending S2.23/S2.24.

### What the policy column does and does not mean

`fuzz::PolicyKind` (the sim's five) and the driver's `--policy` family
(`random-legal` / `greedy` / `script` / `external`+config) are **different
families**. A triple naming `greedy_damage` names a *sim* policy; the oracle
campaign cannot execute it. **S2.42 adopts the honest reading**: the triple
asserts that *a scripted line of that shape reaches the target on that seed*,
and the policy/policy-seed columns are **provenance for a reachability claim**,
not an instruction to the capture. The capture then confirms with its own
scripted policy.

The alternative — building a correspondence between the two policy families —
would have meant a new sim-side `PolicyKind` in
`tools/fuzz/include/sts/fuzz/policy.hpp`, the one file S2.41 is concurrently
editing. That collision was declined rather than negotiated. The caveat is
printed in the cohort file's own `#` header, so a consumer cannot pick the
artifact up without meeting it.

### `--min-hit-count` inverts here, and the README now says so

The seed-list rule (`--min-hit-count ≥ 2`) exists because a *content* capture
runs a **different** policy from the scan, so a one-hit seed says *reachable*,
not *reached*. A **depth** cohort is the opposite case: design §6 sanctions
triples "whose scripted line reaches the target" precisely because a deep line
is *fragile* — the property is not "this seed can be won" but "this exact line
wins this seed". A one-hit triple is a valid cohort member, and raising
`--min-hit-count` would discard most of an Act-3 cohort for a robustness
property its consumer does not use. `--cohort-list` therefore emits every
qualifying row and prints a note if `--min-hit-count > 1` is passed alongside it.

---

## 6. Determinism

- **`--verify-determinism` over the whole sweep: `determinism_mismatches=0`**
  across all 50,000 rows (each case scanned twice, serialized rows compared).
- `failures=0` — no scanned case surfaced a fuzz finding.
- `seed_scan_test` pins the same claim cross-process, and pins that the two
  boss-reach spellings (`boss` and `boss_reached_acts != 0`) can never disagree
  on a scanned run.
- **Driver-side replay determinism**: the Python driver/policy suite is green,
  including `SurvivalPolicyCmdTest.test_decisions_match_in_driver_greedy_from_the_same_seeds`
  (the external binary reproduces `--policy greedy` from `(policy_seed, seed)`
  alone) and the act-profile / boss-relic suites added by this task. Re-derive
  the count with `ctest -N | tail -1`; do not quote one from here.

---

## 7. Driver side — built and testable now, measured by S2.43

The driver runs against the **real game**, which has all three acts, so nothing
on the driver side is blocked by S2.2x. What blocked it was the driver's own
Act-1 terminal, and S2.42 removed it.

**Driver `b1.6.0` → `b1.7.0`** (pipeline `b5.3.0` → `b5.4.0`):

- `is_boss_combat_reward` no longer gates on `act == 1`, and is no longer a
  terminal. `_claim_boss_reward` claims the rows and then emits the `proceed`
  S1 refused, opening the boss chest.
- The run's terminal is now the game's own GAME_OVER — death or **victory**.
  A GAME_OVER screen can walk back to the menu, so deep runs no longer force an
  orchestrator relaunch.
- **R4, the boss-relic pick** (`greedy_policy._score_boss_reward`), with two
  SHA-pinned cohort configs — `policy_bossrelic_take.json` and
  `policy_bossrelic_skip.json` — so design §6 G2-2's "both a take and ≥ 1 skip
  witnessed" is a *named cohort identity*, not a probabilistic hope. Five BOSS
  relics are never taken, on a checkable criterion: each invalidates a rule
  `greedy_policy` itself owns (Sozu→R2, Runic Dome→R3, Snecko Eye→the
  cheap-utility term, Pandora's Box→R1, Calling Bell→the b1.5.3 modal-screen
  path).
- **`ACT_PROFILES`**, per-act overlays over the same ALL-CAPS constants
  (`MAP_ELITE`, `DECK_ATTACK_TARGET`/`DECK_SIZE_CAP`, `POTION_LOW_HP_FRACTION`,
  plus `POTION_HIGH_STAKES_FROM_ACT`). Act 1 is **byte-identical to b1.6.0 by
  construction** — there is no key `1` and an act-less dump resolves to 1 — so
  TE.1's 31.0 % stays reproducible in behaviour.
- `seeds_done` rows gained `boss_fight_acts` / `boss_kill_acts` /
  `boss_relic_acts` / `max_act` / `victory`, all additive and all outside
  `validate_artifacts.STRICT_DONE_KEYS`, so pre-b1.7.0 ledgers still validate.
  `campaign_pipeline` aggregates them into `boss_fight_by_act` /
  `boss_kill_by_act` / `boss_relic_pick_by_act` / `victories`.

**Not measured here, and why.** S2.42's brief forbids launching the game; a
live three-act capture cohort is S2.43's deliverable, run through
`campaign_pipeline.py`. Everything above is unit-tested (act profiles, the
two-screens invariant re-swept per act, the boss-relic cohorts, the boss-chest
reopen guard, the per-act reach block) but the **live** per-act driver reach
numbers are pending that capture.

### One trap worth naming: the boss-chest 2-cycle

A skipped boss-relic pick is a **reversible screen close** (`boss_chest.hpp`:
`relicSkipLogic` → `chest.close()`, which does not clear the three offers), and
`ChoiceScreenUtils.getChestRoomChoices` re-advertises `open` the instant
`isOpen` goes false. A stateless policy that both opens chests and skips picks
therefore has a legal open/skip 2-cycle whose signature alternates between two
screens — invisible to the driver's stuck detector, exactly the shape of the
b5.2 GRID-cancel trap. `CampaignDriver._boss_chest_reopen_filter` closes it by
dropping the second and later `open` of one boss chest; that costs nothing (a
reopened chest offers the same three relics) and cannot empty the candidate set
(`proceed` is always advertised in the room).

---

## 8. Escalation verdict

Design §6's driver-risk paragraph sanctions escalating to a **sim-consulting
scripted driver** (shallow rollout behind the same STS-POLICY-IO seam) *if*
measured TE.1-family reach is insufficient for the S2-G2 depth bars.

**Verdict: not yet decidable, and deliberately not pre-empted.** The number that
decides it is the *driver-side* Act-2/Act-3 reach under the b1.7.0 policy, and
that measurement is S2.43's live capture — the ledger row makes the escalation
conditional on a measurement, and pre-building it would spend the conditionality.

What the sim-side numbers do say is that the *sim* pre-scan alone cannot carry a
depth cohort: 0.12 % Act-1 kill over 50,000 rows is 59 triples, and Acts 2–3
are structurally 0. So the S2-G2 depth cohorts will be scheduled from the
driver's own reach plus the identity/seed dimensions of the scan, not from sim
reach.

The honest trigger to re-open this: if the b1.7.0 driver's live Act-2 boss-kill
rate is low enough that G2-2's "every Act-2 BOSS row, both a take and a skip"
cannot be filled at S2-G2 campaign scale, escalate. Record the number that
decided it, either way.

---

## 9. Cross-checks

- **Against TE.1's sim baseline.** [te1-survival-cohort.md](te1-survival-cohort.md)
  recorded the B5.1 E0 fuzz heuristics reaching the boss in **0.80 %** of
  240,000 scanned rows over `STS420000`–`STS479999`. This scan, over a
  different seed range and a different policy mix, puts the three comparable
  E0 heuristics at 1.01 % / 0.61 % / 0.40 % (`greedy_damage` / `greedy_block` /
  `hoard_gold`), i.e. **the same order of magnitude and bracketing 0.80 %**. The
  all-policy 2.81 % is *not* comparable — it is dominated by `always_event`,
  which the TE.1 mix did not weight the same way. No drift finding.
- **Boss identity distribution.** Act-1 bosses over all 50,000 rows: Hexaghost
  16,740 / Slime Boss 16,710 / The Guardian 16,550 — flat to within 0.6 %, as a
  uniform boss shuffle should be.
- **Legacy column meaning.** `boss` still equals `boss_reached_acts != 0` on
  every scanned row (pinned by `SeedScanActMask.BossReachedAgreesWithTheLegacyBool`),
  so every pre-S2.42 `--need-boss` filter still means what it meant, and the
  five new TSV columns are appended after `fail_kind` so positional scripts are
  unaffected.

---

## 10. Known limits of this instrument

Named here rather than left for the coverage join to discover:

1. ~~**Act-2/3 reach is 0 by construction** (§0). Re-run §1's command as
   S2.23/S2.24 and S2.27/S2.28 land; the report format does not change.~~
   **DISCHARGED by S2.41 (2026-08-09).** The command was re-run, the format did
   not change, and the cells are now measured rather than pending — §11. What
   the limit becomes is different in kind and is stated there: Act-2/3 reach is
   0 *by policy*, and no volume of this instrument fixes that.
2. **Double-boss detection does not exist yet.** Design §6 G2-3 wants ≥ 3
   completed double-boss runs over ≥ 2 distinct first-boss identities. The A20
   double boss is S2.28's engine work; the scan deliberately does **not** carry
   an inert `double_boss` column, because a field hard-wired false under a
   comment naming a future task is the exact shape
   [conventions §8](../conventions.md#8-traps-already-hit-verification-discipline)
   calls a bug signal. The probe should be whichever run-layer flag S2.28 lands,
   and S2.42 leaves it unbuilt rather than half-built.
3. **`event_flags` has no bits for the Act-2/3 events.** `RunState::event_flags`
   is a `uint32_t` with bit `(id-1)`, so S2.02's ids 32–51 always read false.
   **S2.13 owns widening the storage.** Until then the scan's event filters are
   Act-1-only, which directly limits how much of S2-G2 item 4 (event depth) is
   schedulable from the scan.
4. **`BOSS_REWARD.screen_state.relics` has no schema storage.** S2.42 promoted
   its PROTOCOL.md disposition from `I` (ignored-with-reason, never diffed —
   which made a *zero-diff* boss-relic pick unachievable) to a registry-joined
   **deferred** field, and left the storage to S2.43; see the Deferred
   obligations row in [../s2-tasks.md](../s2-tasks.md).
5. **The TE.1 policy binary's SHA-256 moved.** Any edit to `greedy_policy.py`
   changes it, so the hash pinned in TE.1's artifact headers
   (`BDF49784…`) is now **historical**. The TE.1 evidence doc stays valid — it
   records what ran — but its reproduction instructions need the b1.6.0 file,
   not today's. Act-1 *behaviour* is unchanged by construction (§7).

---

## 11. The S2.41 re-run (2026-08-09) — the cells are measured now

Owner: **S2.41**, discharging the "Act-2 / Act-3 **measured** sim-side reach
numbers" deferred obligation in [../s2-tasks.md](../s2-tasks.md), which names
S2.41 as the re-runner. Nothing about the instrument or the report format
changed; only the content underneath it did.

### 11.1 What ran

| | |
|---|---|
| Commit | branch `s2.41`, base `2f65a42` (all S2.2x monsters/bosses/elites and S2.3x events landed) |
| Tool | `tools/oracle_bridge/planner/seed_scan`, **release** preset — §1's command, verbatim |
| Rows | **50,000** = 5,000 seeds (`STS00100`–`STS05099`) × 5 policies × 2 policy seeds, A20, `--max-actions 12000` |
| Wall clock | 45.8 s, 1,093 rows/s, 101,835 actions/s, `--verify-determinism` on |
| Result | `failures=0`, **`determinism_mismatches=0`**, `actions=4,659,046`, `max_floor=31` |
| Artifacts | `D:\STS_BG_Mod\_oracle_data\s241\s241_scan.tsv`, `s241_summary.txt`, `s241_cohort_act1_kill.tsv` (uncommitted, per §7.3) |

Reproduced to the row against S2.42: A1 fight **1,406 (2.81 %)**, A1 kill
**59 (0.12 %)**, cohort **59 triples over 55 distinct seeds**, per-policy splits
identical. That equality is the control — it says the re-run is the same
experiment, so the Act-2/3 cells moved (or did not) for content reasons alone.

Changed, and only these: `room_unimplemented` 8 → **0** (§4), and four City
events now fire in the census (`Designer`, `Duplicator`, `Knowing Skull`,
`N'loth`, 1 row each) — the direct evidence that scanned runs really are inside
Act 2 rather than parked at its door.

### 11.2 The independent cross-check: the fuzz soak's per-act coverage

S2.41's other deliverable is a per-act breakout in the B5.1 soak's coverage
instrument, which is a *different* tool, a *different* seed range, and a
*different* probe for the same quantities (a boss KILL is the boss combat
leaving `RunPhase::COMBAT` with `RunCombatOutcome::KILLED`, not
`BOSS_TREASURE`). Its numbers are therefore worth quoting beside the scan's.

`build/release/tools/fuzz/fuzz_soak`, seeds 1–20,000 × 5 policies × 1 rep, A20,
`--max-actions 12000`, run as 4 shards and merged — **100,000 cases,
9,246,766 counted actions (18,527,368 stepped), 0 failures**:

| Act | Cases that stood in it | Boss fights | Boss kills | Rooms entered |
|---|---|---|---|---|
| 1 | 100,000 (100 %) | 3,155 (3.16 %) | 108 (0.11 %) | 608,505 |
| 2 | **108 (0.11 %)** | **0** | **0** | 438 |
| 3 | **0** | 0 | 0 | 0 |

`victories = 0`, and `act_boss_kills[3] == victories` (the report shouts if the
two probes ever disagree). `room_unimplemented` = 0 cases, `no_legal_moves` = 0,
`no_progress` = 0, `livelock` = 0.

The two instruments agree where they overlap: an Act-1 kill rate of 0.11–0.12 %,
an Act-2 boss fight count of 0, and no victory. The soak adds the number the
scan's boss-only columns cannot show — **Act 2 is entered 108 times and yields
438 room entries, i.e. about four rooms per crossing before the run dies.**

### 11.3 What this means, and what it does not

**It is not a content finding.** Every Act-2/3 room the scan and the soak touch
is live: 0 parked rows, 40 of 62 monster registry rows fought, City events
firing. The wall S2.42 documented is gone.

**It is a policy finding, and it binds.** The E0 heuristics in
`tools/fuzz/include/sts/fuzz/policy.hpp` are coverage generators — they were
chosen (design §3.3) to be cheap, deterministic and weight-free, not to survive
A20. Their measured ladder is roughly ×30 per act: 3.2 % reach the Act-1 boss,
0.11 % kill it, 0 of those 108 crossings reach the Act-2 boss. Extrapolating the
same ratio, an Act-2 boss *kill* would need order 10^7 cases and an Act-3
victory order 10^9 — against a gate soak sized at 10^7 *actions*. **No practical
soak volume witnesses a three-act run with these policies.**

Consequences, stated so nobody has to rediscover them:

1. **S2-G1's soak bar should be read as breadth plus determinism, not depth.**
   The soak's per-act block now *witnesses* whichever acts a sweep reached
   instead of leaving it to be inferred, and prints `act never entered by any
   case: N` when it did not. A gate run that shows Act 3 = 0 is reporting a
   policy fact honestly; it is not evidence of unimplemented content, and the
   `room_unimplemented` counter next to it is what distinguishes the two.
2. **S2-G2 depth cohorts cannot be scheduled from sim reach.** §8's verdict
   stands and is now measured on both sides of the act boundary rather than one:
   depth comes from the driver (b1.7.0, which plays the real game) plus the
   identity/seed dimensions of this scan.
3. **If a sim-side deep cohort is ever wanted**, the lever is a *different
   policy*, not more volume — design §6's sim-consulting scripted driver, or the
   sim-side equivalent of the driver's `ACT_PROFILES`. S2.41 deliberately did
   not build one: the ledger routes that decision through a measurement, and
   this is the measurement, not the decision.
