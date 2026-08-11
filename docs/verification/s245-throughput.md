# S2.45 throughput re-baseline — measured report

Written by S2.45 (2026-08-10) on branch `s2.45`, base
`d57e077` (S2.49). Evidence for the S2.45 acceptance row in
[../s2-tasks.md](../s2-tasks.md) and for design §6 S2-G2 item 7
([../s2-design.md](../s2-design.md)). The methodology is B5.5's, unchanged —
its landed record is [stage-b-log.md §B5.5](../stage-b-log.md#b55), and
`benchmarks/README.md` carries the standing description of each measurement.

---

## 0. The headline, stated honestly

**All three frozen floors hold, by two to five orders of magnitude.** Nothing
in S2 — including S2.48's `sync_live_gold` on every in-combat advance and
S2.49's per-`DAMAGE`-item guard predicate — moved any floor.

**The premise of design §6 item 7 did not materialise, and that is the finding
of this report.** Item 7 expects the whole-run rate to drop *proportionally to
run length*, because "three-act runs are longer". Measured: the run length did
not change at all. The B5.5 corpus averages **47.04 actions per run** today
against **46.93** when B5.5 measured it (+0.2 %), because under the E0 random
policy **no run in that corpus leaves Act 1** — the benchmark now prints
`act2_runs=0 act3_runs=0` over ~35,000 measured runs, and an independent
instrument (§4) agrees that not one of the 1,000 cases so much as *reaches* the
Act-1 boss. So the number recorded below is a re-baseline against **three-act
content**, not against three-act **trajectories**. It is comparable to B5.5's,
and it should be read that way.

That is not a content gap. It is the same policy ceiling S2.41 measured and
S2.42 recorded: the E0 heuristics lose roughly ×30 per act, so a three-act
sim-side trajectory does not exist to be timed
([s242-deep-reach.md](s242-deep-reach.md) §11). **A three-act whole-run rate
cannot be measured with any weight-free policy in this repository**, which is
why §6 records the S3 baseline as a *pair* of numbers — a corpus-conditional
runs/sec and a length-independent run-steps/sec — rather than one runs/sec that
silently means "act-1 deaths per second".

---

## 1. What ran

| | |
|---|---|
| Host | 8-core / 16-thread 5800X3D, `Run on (16 X 3400.05 MHz CPU s)`, L3 98,304 KiB — the same host B5.5 measured on |
| Preset | `release`, configured `-DSTS_BUILD_BENCHMARKS=ON` |
| Command | `tools/wsl_run.sh --script benchmarks/run_throughput.sh` (Git-Bash on the Windows host) |
| Per-metric flags | `--benchmark_min_time=2s`, one metric per binary invocation, exactly one `items_per_second` result required per invocation — all of it the wrapper's own frozen discipline, unchanged from B5.5 |
| Repetitions | **3 invocations of the whole wrapper** on the committed harness (B5.5 recorded one; the extra two are this report's, and §3 states the spread they exposed) |
| Machine state | serial, foreground, nothing else scheduled on the box; two further invocations of the pre-extension harness were taken earlier in the session and are reported in §3 as spread, not as results |
| Workload | unchanged: the fixed 1,000-seed / policy-stream corpus accepted by the G6 random-policy soak, at A20 |

No engine, registry, fixture, golden file or schema was touched. The only
source change is the benchmark-side act-reach instrumentation of §5.

## 2. Floors — verdict

The floors are frozen in `benchmarks/run_throughput.sh`; the script exits
nonzero below any of them, and printed `PASS` for all three in **every one of
the five invocations taken this session**.

| Floor | Frozen value | Measured (median of 3) | Worst seen this session | Margin at worst | Verdict |
|---|---|---|---|---|---|
| combat steps/sec/core (`BM_AdvanceBatch`) | 50,000 | **13,517,300** | 12,175,500 | ×243 | **HOLD** |
| full combats/sec/core (`BM_RandomPolicyFullCombatPerCore`) | 300 | **60,323.0** | 49,780.2 | ×166 | **HOLD** |
| full A20 runs/sec whole-machine (`BM_RandomPolicyFullA20RunWholeMachine`) | 0.4 | **195,311** | 148,261 | ×370,653 | **HOLD** |

No floor regressed. No benchmark was skipped: a `ROOM_UNIMPLEMENTED` park, an
empty legal mask or the action cap would have called `SkipWithError` and left
the wrapper with no rate to check, and none did.

## 3. The measurements

Committed-harness invocations, in the order taken:

| Metric | F1 | F2 | F3 | median |
|---|---|---|---|---|
| `BM_AdvanceBatch` steps/sec/core | 13,499,300 | 13,525,000 | 13,517,300 | **13,517,300** |
| complete combats/sec/core | 59,610.1 | 60,323.0 | 60,542.0 | **60,323.0** |
| — its `combat_steps`/sec/core | 1,407,040 | 1,424,080 | 1,428,930 | 1,424,080 |
| complete A20 runs/sec whole-machine | 187,243 | 195,548 | 195,311 | **195,311** |
| — its `run_steps`/sec whole-machine | 8,808,690 | 9,199,380 | 9,185,780 | 9,185,780 |
| `runs_counted` | 34,704 | 33,776 | 35,376 | — |
| `terminal_act_sum` | 34,704 | 33,776 | 35,376 | — |
| `act2_runs` / `act3_runs` | 0 / 0 | 0 / 0 | 0 / 0 | — |

**Run-to-run spread is real and is reported rather than hidden.** Two earlier
invocations of the pre-extension harness read 13.848 M / 60,194 / 176,757 and
13.911 M / 55,492 / 169,643; three mid-session invocations of the extended
harness read as low as 12.176 M / 49,780 / 148,261 before settling into the F
series above. That is a spread of about ±12 % on the whole-run metric and ±10 %
on the other two, with **no clean covariate** — the slowest readings were taken
at the *lowest* reported load average, so the obvious "leftover load" and
"thermal drift" stories both fail on the data, and this report does not assert
a cause it did not isolate. What the spread does mean is stated in §2: the
floors are checked against the *worst* number seen, not the median, and they
hold there by ≥ 166×.

**Derived, and stable across every invocation:**

| Quantity | S2.45 | B5.5 |
|---|---|---|
| engine steps per complete combat | 23.60 – 23.61 | 23.62 |
| run-level actions per complete run | 47.03 – 47.04 | 46.93 |
| mean terminal act (`terminal_act_sum` / `runs_counted`) | **1.000** | not instrumented |

## 4. Act reach — why the "three-act" claim is printed, not inferred

`terminal_act_sum` equals `runs_counted` **exactly** in all three invocations.
That is the positive control: it says both that every measured run ended in
Act 1 *and* that the probe returned a real act value rather than zeros — a bare
`act2_runs=0` cannot tell those two apart, which is the whole reason the
counter exists (§5).

Cross-checked with a **different tool over exactly the same corpus**. The
benchmark's `fuzz_policy_seed_for(seed)` is `policy_seed_for(seed, RANDOM, 0)`
from `tools/fuzz/src/main_soak.cpp:176`, so this soak sweep is the benchmark's
case set, case for case:

```bash
build/release/tools/fuzz/fuzz_soak \
    --seed-start 1 --seeds 1000 --policies random --reps 1 \
    --ascension 20 --max-actions 20000 --threads 8
```

| Soak reading | Value |
|---|---|
| cases | 1,000 (100 % `run_over`) |
| counted actions | 47,050 → **47.05 actions/case** |
| `room_unimplemented` / `action_cap` / `livelock` / `no_progress` | 0 / 0 / 0 / 0 |
| act 1 / act 2 / act 3 cases | **1,000 / 0 / 0** |
| act-1 boss rooms entered | **0** |
| deepest act / deepest floor / deepest turn | 1 / 13 (of 16) / 13 |

Two independent instruments, one shared corpus: 47.05 actions/case against the
benchmark's 47.03–47.04 `run_steps`/`runs_counted`, and act-2 reach 0 against
`act2_runs` 0. The corpus does not merely fail to finish three acts — **it
never fights the Act-1 boss**, which is well inside the 0.12 % sim-side
Act-1-kill rate S2.42 measured over 50,000 rows.

## 5. Harness extension

`benchmarks/bench_throughput.cpp` gained four counters on
`BM_RandomPolicyFullA20RunWholeMachine`, and nothing else changed. The terminal
act is read **once per run** from `RunController::run.act` after `RUN_OVER` —
the act counter only ever increments, so one read is the run's maximum and
costs nothing per step. `runs_counted` is the denominator the rate line cannot
supply; `terminal_act_sum` is the §4 positive control; `act2_runs` / `act3_runs`
are the reach witnesses. They are plain counters, so Google Benchmark sums them
across workers and divides them by nothing — deliberately *not* carrying the
worker-count correction the `kIsRate` counters need. No floor is attached to
any of them: `run_throughput.sh` is untouched, and still greps exactly one
`items_per_second` per invocation.

This was the minimum needed to keep the report honest. The whole-run benchmark
did **not** need a three-act variant: it runs to `RUN_OVER` and the engine has
been act-general since S2.12, so it already measures against three-act content.
What it could not do was *say* how far into that content it got.

## 6. Against B5.5 — what moved, and what this does not claim

| Metric | B5.5 (2026-07-29) | S2.45 (median) | Ratio |
|---|---|---|---|
| batch steps/sec/core | 27,163,500 | 13,517,300 | ×0.498 |
| complete combats/sec/core | 84,624.2 | 60,323.0 | ×0.713 |
| — `combat_steps`/sec/core | 1,998,800 | 1,424,080 | ×0.712 |
| complete A20 runs/sec whole-machine | 204,749 | 195,311 | ×0.954 |
| — `run_steps`/sec whole-machine | 9,608,930 | 9,185,780 | ×0.956 |

Because run *length* is unchanged (§3), every one of these ratios is a
per-step cost ratio, not a workload change. Read that way:

- **The run layer is within 5 % of B5.5.** `run_steps`/sec is ×0.956 — and the
  run-level step is exactly where S2.48 added `sync_live_gold` to every
  in-combat advance. At this resolution that addition has **no measurable
  cost**. This is the named risk in the S2.45 brief, and it is answered.
- **Combat stepping is ×0.71.** `combat_steps`/sec and complete-combats/sec
  moved together to three digits, so the cost is per step, spread evenly, not a
  changed combat shape. A whole content stage — plus S2.49's per-`DAMAGE`-item
  guard — sits between the two readings.
- **The 10,000-state batch is ×0.498, and the leading candidate is state size,
  not the interpreter.** `sizeof(CombatState)` is pinned at **8,088 B**
  (`include/sts/engine/combat_state.hpp:1229`); the B5.5-era header's last
  recorded value was **3,896 B** (comment, `a2c3a6e`, documented rather than
  asserted at that commit). The batch's working set therefore went ~38.96 MB →
  ~80.88 MB against a 96 MiB L3, and `BM_AdvanceBatch` is a bandwidth-bound
  sweep whose states go terminal early (its own header comment says so). A
  ×2.08 state and a ×0.498 rate is the shape of a memory-bound loop.

**None of that is attributed, and this report does not attribute it.** These
are absolute one-build floor checks; the repository's sanctioned instrument for
a *comparison* is the interleaved `tools/bench_ab.sh` over two binaries
([../conventions.md](../conventions.md)), and no A/B was run here — an A/B
against the B5.5 tree would compare two different content sets rather than two
implementations of one, and S2.45's scope is measurement, not optimisation.
The open, answerable question a follow-up can settle cheaply is narrow:
**A/B `d57e077` against `646bd18`** (the two commits either side of S2.48 +
S2.49) on `bench_advance_mask` and `bench_throughput`, which isolates exactly
those two changes and nothing else.

## 7. The S3 baseline, recorded

Design §6 item 7 asks for "a new three-act whole-run number with methodology as
the S3 baseline". What §0 establishes is that a single runs/sec cannot carry
that meaning today, so the baseline is recorded as a pair, both from the
release preset on the host in §1:

| S3 baseline quantity | Value | What it is good for |
|---|---|---|
| complete A20 runs/sec, whole-machine, frozen 1,000-case corpus | **195,311** (worst seen 148,261) | apples-to-apples against B5.5's 204,749. Corpus-conditional: mean run length 47.04 actions, mean terminal act 1.000 |
| run-level engine steps/sec, whole-machine | **9,185,780** | **length-independent.** Project any future run length through this: a trajectory of *N* run-level actions costs *N* / 9.19 M machine-seconds |
| engine steps/sec/core, single-threaded combat | 1,424,080 | per-core combat cost, free of the batch benchmark's memory effects |

A genuine three-act rate becomes measurable when a policy that survives two
boss fights exists — the S2.V2 sim-consulting driver, or a GT2 checkpoint.
Until then, projecting `run_steps`/sec through a measured three-act action
count is the honest construction, and quoting 195,311 as "three-act runs per
second" is not.

## 8. Reproduce

```bash
tools/wsl_run.sh release -DSTS_BUILD_BENCHMARKS=ON
tools/wsl_run.sh --script benchmarks/run_throughput.sh     # x3, serially
```

The act-reach cross-check is the `fuzz_soak` command in §4. Both are absolute
checks of one build: do not compare two sequential runs of either as if it were
an A/B (§6).
