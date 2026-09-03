# S3.64 throughput report -- the S2 attribution, and the S3 baseline

Written by S3.64 (2026-09-03) on branch `s364`, base `dedac58` (S3-G1).
Discharges the s2-tasks deferred-obligations row "Per-step throughput
attribution across S2" and the s3-tasks deferred-obligations row that
inherited it (both cite this file). Evidence for the S3.64 acceptance row in
[../s3-tasks.md](../s3-tasks.md). Methodology is B5.5's / S2.45's, unchanged
except where §2 says otherwise; S2.45's own record is
[s245-throughput.md](s245-throughput.md).

---

## 0. The headline, stated honestly

**Part (a), the S2.45 attribution: `RESULT: UNMEASURED`, on both flagged
benchmarks, at every sample size tried (n=5, 8, 12 interleaved pairs).** The
×0.712 combat-step / ×0.498 batch ratios stay unattributed. This is not a
methodology shortfall: the box was genuinely loud during the A/B windows
(host CPU 90-98%, driven by a concurrent WSL build under `vmmemWSL` -- see
§1), and the measured noise (sd 9-21% depending on round) swamps any signal
the size of the ratios being investigated. `tools/bench_ab.sh`'s own
calibration on this box is ±2.8%; this session's spread is four to eight
times that. Growing `-n` from 5 to 12 did not converge toward significance --
the mean stayed small and the spread stayed wide across all three rounds
(§3) -- which is itself informative: this is sustained contention, not a
transient spike a slightly longer run would average out.

**Part (b), the S3 baseline: all three B5.5/S2.45 floors HOLD**, by ≥ 98× at
the *worst* of five release-preset readings taken this session (§4). The
policy ceiling S2.41/S2.42/S2.45 measured is unchanged: `act2_runs=0
act3_runs=0` in every reading, so the corpus still never leaves Act 1 under
the weight-free E0 random policy, and "three-act runs/sec" remains
unquotable for the reason S2.45 gave. The S3 baseline is therefore recorded,
again, as the pair S2.45 established -- corpus-conditional runs/sec plus
length-independent run-steps/sec -- both re-measured fresh on the gated S3
tree (schema 9, `PUBLIC_VIEW_VERSION` 7, `sizeof(CombatState)` unchanged at
8,088 B).

**Part (c), the new number: `encode_public_view` + `public_hash` costs a
median 1.09 µs per state** (0.71 µs encode + 0.38 µs hash) over
`PublicView` v7 (8,992 B), measured by a new benchmark added for this task
(§5). This is the pair the training program pays on every decision; it is
comparable in shape, not value, to the T1.3 actor spike's 1.43 µs figure,
which was measured in the separate training repo against an earlier schema.

**A CMake finding, folded in per the coordinator's note**: building any
non-LTO preset (`debug`, `asan`) with `-DSTS_BUILD_BENCHMARKS=ON` failed to
link `bench_throughput` with `undefined reference to
sts::fuzz::sim_search_pick` before this task -- `benchmarks/CMakeLists.txt`
compiled `tools/fuzz/src/policy.cpp` without `tools/fuzz/src/policy_search.cpp`,
and only `release`'s LTO build proved able to dead-strip the now-unconditional
call `policy_pick()` makes into it. Fixed here by adding
`policy_search.cpp` to both `bench_throughput` and the new
`bench_public_view` (§6); confirmed by building both non-LTO WSL presets
clean. **CI dropped `-DSTS_BUILD_BENCHMARKS=ON` for this exact reason**
(S3.66, `3d1906c`, landed the same day, before this task read the tree) --
with the fix in, the flag can return to CI in a follow-up; this report does
not do that.

---

## 1. What ran, and the load conditions

| | |
|---|---|
| Host | 8-core / 16-thread 5800X3D, `Run on (16 X 3400.0x MHz CPU s)`, L3 98,304 KiB -- the same host B5.5/S2.45 measured on |
| Preset | `release`, configured `-DSTS_BUILD_BENCHMARKS=ON` (S3 gated tree, schema 9, `PUBLIC_VIEW_VERSION` 7) |
| Command (baseline) | `tools/wsl_run.sh --script benchmarks/run_throughput.sh`, x5 whole-wrapper invocations |
| Command (new number) | `bench_public_view --benchmark_min_time=2s`, x5 invocations |
| Command (A/B) | `tools/wsl_run.sh --script tools/bench_ab.sh -n {5,8,12} <A> <B>` -- two release+LTO binaries built from `git archive d57e077` / `git archive 646bd18` trees under the task scratchpad (worktree verbs are blocked by a hook, per the brief) |
| Per-metric flags | `--benchmark_min_time=2s`, one metric per binary invocation -- `run_throughput.sh`'s own frozen discipline, unchanged |

**Load, honestly, because it varied a lot across this session and that
variation is load-bearing for how part (a) reads:**

- Before starting: Windows host CPU 19-22% (three `Get-Counter` samples).
- During the 5 `run_throughput.sh` baseline invocations (§4): WSL-side
  Google Benchmark `Load Average` fell from 17-18 (out of 16 threads) on the
  first two invocations to 12-16 on the last three -- moderate, trending
  down, consistent with a sibling WSL build finishing partway through. The
  per-invocation numbers move with it (§4's table).
- During the two interleaved A/B measurements (§3): Windows host CPU **90-98%**
  across five `Get-Counter` samples, with `vmmemWSL` (the WSL2 VM process)
  and `MsMpEng` (Windows Defender) the top two non-system consumers by CPU
  time -- a concurrent WSL build (a sibling task worktree; `ccache`'s own
  cross-worktree cache hit during this session's `debug` rebuild independently
  confirms a sibling worktree, `_wt/s3g1`, was mid-build) plus AV scanning,
  neither of them this task's own process.
- During the 5 `bench_public_view` invocations (§5): WSL-side `Load Average`
  1.0-2.2 -- quiet. The sibling build(s) had finished by then.

This is exactly the situation the brief anticipated ("the box may be busy...
run the interleaved A/B only in quiet windows, or report the contention
honestly with the spread -- never a single run"): the loud window landed on
the A/B, not on the other two measurements, and it is reported as loud rather
than waited out indefinitely or silently averaged away.

No engine, registry, fixture, golden file or schema was touched by this task
beyond the two new source files in `benchmarks/` (§5, §6).

---

## 2. Part (a) -- what the A/B was for

s2-tasks.md's deferred row named a *specific, cheap, narrow* follow-up:
S2.45 found combat stepping at ×0.712 and the 10,000-state batch at ×0.498
against B5.5, both **absolute one-build measurements** that a comparison
instrument was never run against (S2.45's own scope was measurement, not
optimisation). It named the two commits either side of the two changes it
flagged as most likely to matter -- S2.48 (per-advance `sync_live_gold`) and
S2.49 (the multi-hit attacker-cancel DAMAGE-item guard) -- and the two
benchmarks whose ratios were unattributed:

```
tools/bench_ab.sh -n 5 <bench_advance_mask@d57e077> <bench_advance_mask@646bd18>
tools/bench_ab.sh -n 5 -f '^BM_RandomPolicyFullCombatPerCore' <bench_throughput@d57e077> <bench_throughput@646bd18>
```

`d57e077` is S2.49 itself (the later of the two flagged commits); `646bd18`
is the very next commit (a ledger-only update). So the A/B isolates
*exactly* S2.49's interp_damage change and nothing else -- the narrowest cut
available, as the brief specified. `BM_AdvanceBatch` (bench_advance_mask)
targets the ×0.498 batch ratio; `BM_RandomPolicyFullCombatPerCore`
(bench_throughput, filtered to exclude the multithreaded whole-run
benchmark, which was never part of the unattributed pair) targets the ×0.712
combat-step ratio.

Both trees were extracted with `git archive <sha> | tar -x` (not
`git worktree`, which a repo hook blocks) into the task scratchpad and built
independently, `release` preset, `-DSTS_BUILD_BENCHMARKS=ON`, each into its
own `build/release` -- so `STS_DEP_CACHE_DIR`'s per-tree partitioning
(`CMakeLists.txt`'s `PROJECT_SOURCE_DIR`-relative `.cache/deps`) kept the two
configures independent; neither tree is a git repository, so nothing in the
build depends on git metadata.

## 3. Part (a) -- the measured rounds

**`bench_advance_mask` / `BM_AdvanceBatch`** (the ×0.498 batch ratio):

| n (pairs) | A mean (M/s) | B mean (M/s) | delta mean | sd | sem | spread | Verdict |
|---|---|---|---|---|---|---|---|
| 5 | 7.697 | 7.609 | +1.37% | 29.54% | 13.21% | [-39.83%, +34.64%] | UNMEASURED |
| 8 | 8.893 | 8.855 | +0.43% | 13.77% | 4.87% | [-19.85%, +22.06%] | UNMEASURED |
| 12 | 7.145 | 7.317 | +3.12% | 20.81% | 6.01% | [-19.35%, +57.24%] | UNMEASURED |

**`bench_throughput` / `BM_RandomPolicyFullCombatPerCore`** (the ×0.712
combat-step ratio; note the raw items/sec here is ~1000× smaller than the
batch benchmark's -- it is complete-combats/sec, not steps/sec, and its own
`combat_steps` counter is not what `bench_ab.sh` parses):

| n (pairs) | A mean (M/s) | B mean (M/s) | delta mean | sd | sem | spread | Verdict |
|---|---|---|---|---|---|---|---|
| 8 | 0.046 | 0.048 | +6.19% | 10.72% | 3.79% | [-7.43%, +23.22%] | UNMEASURED |
| 12 | 0.052 | 0.051 | -0.36% | 9.29% | 2.68% | [-17.30%, +17.75%] | UNMEASURED |

Every round satisfies `|mean| <= 2*sem` -- `bench_ab.sh`'s own exit-3
condition -- so every round is `RESULT: UNMEASURED`, exactly as printed by
the sanctioned script; none of these numbers were rounded toward
significance or cherry-picked. **`RESULT: UNMEASURED` is this report's answer
for part (a).** The ×0.712 / ×0.498 ratios remain attributed only to the
leading candidate S2.45 named -- state size, not the interpreter
(`sizeof(CombatState)` 3,896 B pre-instanced-powers -> 8,088 B today, working
set ~39 MB -> ~81 MB against the 96 MiB L3, unmoved since S2.45) -- and that
candidate is still a hypothesis, not a measurement. A future re-run in a
genuinely quiet window (WSL-side `Load Average` well under core count, no
sibling `wsl_run.sh` active) is the way to settle it; this task did not get
one for the A/B specifically, though it did for §4 and §5.

## 4. Part (b) -- the S3 baseline, floors

Floors are frozen in `benchmarks/run_throughput.sh` (unchanged since B5.5;
S2.45 did not move them). Five release-preset invocations this session, in
order taken (§1 records the load trend across them):

| Invocation | steps/sec/core | combats/sec/core | combat_steps/sec/core | runs/sec whole-machine | run_steps/sec whole-machine | runs_counted |
|---|---|---|---|---|---|---|
| 1 | 10,224,700 | 38,772.2 | 915,967 | 126,034 | 5,929,760 | 16,000 |
| 2 | 7,356,180 | 29,457.2 | 695,880 | 118,807 | 5,589,730 | 16,000 |
| 3 | 7,856,180 | 35,779.2 | 845,554 | 105,452 | 4,961,400 | 16,000 |
| 4 | 10,349,000 | 49,214.5 | 1,162,530 | 136,690 | 6,427,480 | 24,496 |
| 5 | 10,686,000 | 46,417.5 | 1,096,650 | 141,549 | 6,659,720 | 22,880 |
| **median** | **10,224,700** | **38,772.2** | 915,967 | **126,034** | 5,929,760 | -- |
| **worst** | 7,356,180 | 29,457.2 | 695,880 | 105,452 | 4,961,400 | -- |

`act2_runs=0 act3_runs=0` in all five invocations; `terminal_act_sum ==
runs_counted` exactly in all five (the S2.45 positive control, still true:
the probe returns real data and every measured run ends in Act 1).

| Floor | Frozen value | Median (n=5) | Worst this session | Margin at worst | S2.45's worst | Verdict |
|---|---|---|---|---|---|---|
| combat steps/sec/core | 50,000 | 10,224,700 | 7,356,180 | ×147 | ×243 | **HOLD** |
| full combats/sec/core | 300 | 38,772.2 | 29,457.2 | ×98 | ×166 | **HOLD** |
| full A20 runs/sec whole-machine | 0.4 | 126,034 | 105,452 | ×263,630 | ×370,653 | **HOLD** |

Every floor holds by two to five orders of magnitude, same as S2.45. The
margins are smaller than S2.45's own worst-of-five because this session's
worst readings landed during the higher-load early invocations (§1) --
S2.45 measured on a quieter box. That is a load difference, not a regression:
against S2.45's *medians* (13,517,300 / 60,323.0 / 195,311), this session's
medians read ×0.76 / ×0.64 / ×0.65 -- directionally consistent with a busier
box across the board rather than one metric moving independently, which is
what a real engine-side regression would look like instead.

**Derived, and stable across all five invocations** (run_steps÷items_per_second,
combat_steps÷items_per_second):

| Quantity | This session | S2.45 |
|---|---|---|
| run-level actions per complete run | 47.03 -- 47.06 | 47.03 -- 47.04 |
| engine steps per complete combat | 23.62 -- 23.63 | 23.60 -- 23.61 |

Unchanged to two decimal places. The corpus, the policy, and the content it
reaches have not moved since S2.45; only the box's contemporaneous load has.

## 5. Part (c) -- `encode_public_view` + `public_hash` per state, v7

New benchmark, `benchmarks/bench_public_view.cpp` (`BM_EncodePublicView`,
`BM_PublicHash`), added by this task. It builds a bank of 4,096 real
`RunController` snapshots by stepping the same fixed 1,000-seed random-policy
corpus `bench_throughput.cpp` uses (same seed range, same policy-seed mixing
function, duplicated rather than shared per that file's own precedent),
snapshotting every 5 run-level actions so the bank mixes map/screen phases
with in-combat states. `BM_EncodePublicView` times `encode_public_view()`
alone over the whole bank per iteration; `BM_PublicHash` times
`public_hash(const PublicView&)` alone, over views already encoded once
outside the timed loop -- mirroring T1.3's own encode/hash split rather than
timing the combined `public_hash(const RunController&)` overload.
`PUBLIC_VIEW_VERSION` is 7, `sizeof(PublicView)` is 8,992 B
(`public_view.hpp` static_asserts).

Five release-preset invocations, `--benchmark_min_time=2s`, quiet window
(§1):

| Invocation | encode items/sec | encode us/state | hash items/sec | hash us/state |
|---|---|---|---|---|
| 1 | 988,172 | 1.012 | 801,754 | 1.247 |
| 2 | 1,725,910 | 0.579 | 2,818,830 | 0.355 |
| 3 | 1,409,380 | 0.709 | 2,622,810 | 0.381 |
| 4 | 1,845,280 | 0.542 | 2,902,910 | 0.345 |
| 5 | 785,542 | 1.273 | 2,119,330 | 0.472 |
| **median** | -- | **0.709** | -- | **0.381** |

Median combined: **1.09 us/state** (0.709 encode + 0.381 hash). Spread is
real here too -- roughly 0.54-1.27 us encode, 0.35-1.25 us hash -- despite the
quiet WSL-side load average (1.0-2.2); Google Benchmark's own iteration-count
adaptation (visible in the wide `Iterations` swing between invocations, not
reproduced above) is a more likely source than host contention for this
particular spread, since the load signal that explained §3/§4's noise is
absent here. Reported as a median with its spread rather than a single
number, on the same honesty principle as the rest of this report.

**Against T1.3's figure, with the caveat stated plainly**: T1.3 measured
"0.57 encode_public_view incl. the embedded mask + 0.40 public_hash" for a
combined 0.97 us, on an EARLIER `PUBLIC_VIEW_VERSION` ("the old pin", per the
brief) and a different machine (the training repo's own box, not
necessarily this host). This report's 0.71 + 0.38 = 1.09 us is close in
shape and comparable order of magnitude, but the two figures are not a valid
A/B -- different schema, different repo, different measurement box, no
interleaving. What this report adds that T1.3 could not: a number measured
*in this repo*, on *this schema* (v7, 8,992 B), reproducible by anyone who
builds the release preset with benchmarks on, independent of the training
repo's own toolchain.

## 6. Build matrix, and the CMake fix

All six presets build. WSL presets (`debug`, `asan`, `release`) were built
with `-DSTS_BUILD_BENCHMARKS=ON` explicitly, to prove the two new benchmark
translation units (`bench_public_view.cpp` and the `policy_search.cpp`
addition below) compile clean under GCC in every optimisation level, not
only under `release`'s LTO. `win-debug`/`win-asan`/`win-release` were built
at their standard settings (also with `-DSTS_BUILD_BENCHMARKS=ON`, to prove
the same on clang-cl); `/EHsc` was verified present in each `win-*`
`CMakeCache.txt` before building, per the conventions §6 trap. No ctest was
run for any preset, per this task's own brief (the 2026-09-03 owner
directive already retired ctest as acceptance evidence generally).

| Preset | Configure | Build |
|---|---|---|
| debug | OK | OK (after the fix below) |
| asan | OK | OK |
| release | OK | OK |
| win-debug | OK (`/EHsc` present) | OK |
| win-asan | OK (`/EHsc` present) | OK |
| win-release | OK (`/EHsc` present) | OK |

**The fix.** `benchmarks/CMakeLists.txt` compiled `tools/fuzz/src/policy.cpp`
directly into `bench_throughput` (deliberately, so a benchmarks-only
configuration -- `STS_BUILD_TESTS=OFF` -- need not build `fuzz_core`, which
lives behind that flag). `policy.cpp`'s dispatch, `policy_pick()`, calls
`sim_search_pick()` (`tools/fuzz/src/policy_search.cpp`) for
`PolicyKind::SIM_SEARCH` -- a kind this benchmark's corpus never selects, but
the call is unconditional in source, and only `release`'s interprocedural
optimisation was able to prove it dead and elide the reference.
`debug`/`asan` (no IPO) both failed to link `bench_throughput` with
`undefined reference to sts::fuzz::sim_search_pick` the first time this
task built either preset with `-DSTS_BUILD_BENCHMARKS=ON` -- nothing had
built that combination since SIM_SEARCH landed (S2.V2, after S2.45). The
same failure reproduced on the new `bench_public_view`, which compiles
`policy.cpp` for the same reason. Fix: add
`${PROJECT_SOURCE_DIR}/tools/fuzz/src/policy_search.cpp` as a second extra
source on both targets. `policy_search.cpp`'s own `#include` list reaches
only engine headers (no `registry_generated`, no `xxhash`), so no new link
library was needed -- confirmed by the clean `debug`/`asan` builds above,
not assumed.

**CI implication, not this task's to land.** CI already stopped passing
`-DSTS_BUILD_BENCHMARKS=ON` for exactly this reason: S3.66 (`3d1906c`,
"CI is build + oracle replay + leak-gate binaries ... no ctest") landed on
`master` the same day, ahead of this task reading the tree, and its own
commit body names the same undefined-reference failure this report found
independently. With the fix in this commit, a follow-up can re-add the flag
to CI; this report only states that the blocker this task hit is now gone,
not that CI has been changed.

## 7. The S3 baseline, declared for S4

Same shape S2.45 declared, re-measured on the gated S3 tree, both readings
from §4:

| S3 baseline quantity | Value (median, n=5) | Worst this session | What it is good for |
|---|---|---|---|
| complete A20 runs/sec, whole-machine, frozen 1,000-case corpus | **126,034** | 105,452 | apples-to-apples against S2.45's 195,311 / B5.5's 204,749. Corpus-conditional: mean run length 47.03-47.06 actions, mean terminal act 1.000 |
| run-level engine steps/sec, whole-machine | **5,929,760** | 4,961,400 | **length-independent.** Project any future run length through this: a trajectory of *N* run-level actions costs *N* / 5.93 M machine-seconds |
| engine steps/sec/core, single-threaded combat | 915,967 | 695,880 | per-core combat cost, free of the batch benchmark's memory effects |
| `encode_public_view` + `public_hash`, per state, `PublicView` v7 | **1.09 us** (0.71 + 0.38) | 1.72 us (worst: 1.27 + 0.47, not simultaneous) | the training program's per-decision observation cost on the gated tree |

The three frozen floors (§4) still hold by ≥ 98× at this session's worst
reading -- carry them forward unchanged into S4. The whole-run rate is still
a *pair*, for the same reason S2.45 gave: no weight-free policy in this
repository leaves Act 1 (`act2_runs=0 act3_runs=0`, every invocation, this
session and S2.45's both), so quoting either the runs/sec or the
run-steps/sec figure alone as "three-act throughput" would overstate what
was measured. A genuine three-act rate needs a policy that survives two
boss fights -- the S2.V2 sim-consulting driver or a GT2 checkpoint, same as
S2.45 named.

**Scope note, stated plainly rather than glossed over.** The S3.64 ledger
block's own prose frames part (b) as recording "the first whole-run rate
over a policy that actually finishes runs" -- and by 2026-09-03, S3 genuinely
has such a policy: SIM_SEARCH has driven live three-act campaigns to boss
kills (S2.V2, S3.44's Sharp Hide cohort, S3.53's sweep). This report does
**not** do that. This task's own delegated brief scoped §4's deliverable to
"every benchmark in `benchmarks/`" plus the one new `PublicView` number --
i.e. re-run the *existing* `bench_throughput` harness, which still drives
its fixed corpus with `PolicyKind::RANDOM` (`fuzz_policy_seed_for`,
unchanged since S2.45), not SIM_SEARCH. Building a SIM_SEARCH-driven
whole-machine benchmark is a real task -- it would need `fuzz_core`'s search
machinery linked into `benchmarks/` (today deliberately excluded, §6) or a
from-scratch port -- and is not something this report invents evidence for.
So: the corpus-conditional pair above is, honestly, the *same kind* of
number S2.45 recorded, re-measured on the S3 tree; a whole-run rate over a
policy that finishes three-act runs remains future work, named here rather
than assumed done.

## 8. Reproduce

```bash
# S3 baseline + the new number (quiet window recommended for a tight spread,
# not required -- the floors have enormous margin regardless):
tools/wsl_run.sh release -DSTS_BUILD_BENCHMARKS=ON
tools/wsl_run.sh --script benchmarks/run_throughput.sh     # x5, serially
tools/wsl_run.sh --script <wrapper invoking> \
    build/release/benchmarks/bench_public_view --benchmark_min_time=2s   # x5

# Part (a), the named A/B -- needs a genuinely quiet window, unlike the above:
git archive d57e077 | tar -x -C /path/to/A && cd /path/to/A && \
    tools/wsl_run.sh release -DSTS_BUILD_BENCHMARKS=ON
git archive 646bd18 | tar -x -C /path/to/B && cd /path/to/B && \
    tools/wsl_run.sh release -DSTS_BUILD_BENCHMARKS=ON
tools/wsl_run.sh --script tools/bench_ab.sh -n 12 \
    /path/to/A/build/release/benchmarks/bench_advance_mask \
    /path/to/B/build/release/benchmarks/bench_advance_mask
tools/wsl_run.sh --script tools/bench_ab.sh -n 12 -f '^BM_RandomPolicyFullCombatPerCore' \
    /path/to/A/build/release/benchmarks/bench_throughput \
    /path/to/B/build/release/benchmarks/bench_throughput
```

Both A/B commands are absolute checks against two builds; do not run either
sequentially against itself and read the two invocations as a comparison
(§6/§7's own rule, and conventions §8's).
