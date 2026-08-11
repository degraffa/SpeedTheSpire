# Throughput benchmark methodology

Build the release preset with benchmarks enabled:

```bash
tools/wsl_run.sh release -DSTS_BUILD_BENCHMARKS=ON
build/release/benchmarks/bench_advance_mask --benchmark_min_time=2s
build/release/benchmarks/bench_throughput --benchmark_min_time=2s
```

From the Windows host, the reproducible floor check is:

```bash
tools/wsl_run.sh --script benchmarks/run_throughput.sh
```

It runs each metric separately against the release binaries, requires exactly
one `items_per_second` result, normalizes the SI suffix, and exits nonzero when
any frozen floor is missed. This is an absolute check of one build; comparisons
between builds use the interleaved helper described below.

`bench_advance_mask` is the combat-step measurement. It uses the public
mask-reusing batch `advance()` overload over 10,000 heterogeneous states and
reports `items_per_second` as steps/sec/core. Policy selection and legality
enumeration remain inside the timed loop.

`BM_RandomPolicyFullCombatPerCore` constructs a fresh walking-skeleton combat,
then repeatedly builds the legal mask, selects a uniformly random legal move,
and advances until win or loss. Construction, policy selection, and every
engine step are timed. `items_per_second` is completed combats/sec/core; the
`combat_steps` rate is a cross-check that the benchmark is doing active work.

`BM_RandomPolicyFullA20RunWholeMachine` runs at ascension 20 with one Google
Benchmark worker per logical CPU reported by `std::thread::hardware_concurrency`.
It reuses the fuzz soak's exact random-legal move enumeration and policy RNG,
and includes `run_begin`, legal-mask construction, policy selection, and every
run-level step through `RUN_OVER`. Both victory and death are complete terminal
trajectories; an unimplemented-room stall, empty legal set, or action cap fails
the benchmark instead of counting. `items_per_second` is raw completed
random-policy runs/sec across the machine. Workers cycle over the same fixed
1,000-seed/policy-stream corpus accepted by the G6 random-policy soak, so this
is a stable performance workload rather than a new content sweep.

Since S2.45 the same benchmark also reports four act-reach counters:
`runs_counted`, `terminal_act_sum`, `act2_runs` and `act3_runs`. The terminal
act is read once per run from `RunController::run.act` (which only ever
increments) after `RUN_OVER`. They are plain counters, not rates — Google
Benchmark sums them across workers and divides them by nothing, so they print
as whole-machine totals over the measurement, and `runs_counted` is the
denominator the rate line cannot supply.

They exist because the S2 re-baseline claims a *three-act* rate while the E0
random policy loses roughly ×30 per act: how far into the three acts this fixed
corpus actually gets has to be a printed number, not an inference.
`terminal_act_sum` is the positive control — `terminal_act_sum ==
runs_counted` says both that every run ended in act 1 *and* that the probe
returned a real act rather than zeros, which a bare `act2_runs=0` cannot
distinguish. Interpretation belongs to the report, not the floor script: no
floor is attached to any of them.

Google Benchmark sums both counters and elapsed time across worker states
before applying a rate (`src/benchmark_runner.cc` and `src/counter.cc` in its
fetched source), which makes an unadjusted multithread rate a per-worker
average. The benchmark multiplies each worker's completed-run and step counts
by the registered worker count before setting the rate counters; after the
library's aggregation, `items_per_second` and `run_steps` are therefore
whole-machine totals rather than mislabeled per-worker rates.

The planned Stage C workload is a 25-simulation MCTS plus neural-network
inference, but neither search nor inference exists in this simulator repository.
The frozen Stage B exit bar therefore names raw random-policy full runs as its
temporary stand-in. The benchmark follows that literal rule: it does **not**
divide the measured rate by 25 or claim that one random trajectory reproduces
search-tree reuse, branching, snapshot traffic, or NN cost. “25-sim
MCTS-equivalent” identifies the future workload this surrogate protects; the
real harness must replace this measurement in Stage C.

These are absolute exit-bar measurements, not comparisons. If a future change
is compared with a baseline, build both binaries and use the repository's
interleaved `tools/bench_ab.sh`; do not compare two sequential benchmark runs.
