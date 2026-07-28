# `seed_scan` — the capture planner

The oracle capture campaigns walk `STS%05d` sequentially, so what a campaign
sees is whatever those seeds happen to contain. A rare target — one specific
shrine, a treasure floor, a run that reaches the boss — is then a lottery, and
nobody knows whether the list contains it until the game has been driven for
hours.

`seed_scan` answers that question in the simulator first. Event selection is a
pure function of `RunState` (`generate_event`, `src/engine/event_framework.cpp`),
so a full A20 run costs microseconds and a five-thousand-seed sweep costs
seconds. Point the capture at seeds already known to contain the target.

## Build and run

Built with the tests (it links `fuzz_core`), so any preset produces it:

```bash
tools/wsl_run.sh release
build/release/tools/oracle_bridge/planner/seed_scan --help
```

Use the **release** preset for real scans; debug and asan are 1–2 orders of
magnitude slower and the tool is throughput-bound.

## The two output files, and why there are two

```bash
seed_scan --seeds STS00100-STS05099 \
          --policies random,greedy_damage --policy-seeds 0,1 \
          --out scan.tsv \
          --need-event "Match and Keep!" --min-hit-count 2 \
          --seed-list seeds_match_and_keep.txt
```

* `--out` is the **evidence**: one row per (seed, policy, policy_seed), hit or
  not, with the decoded event flags, the max floor, whether a treasure room was
  entered and whether the boss was reached. A later question can be answered by
  re-filtering this file instead of rescanning.
* `--seed-list` is the **artifact a campaign consumes**: nothing but seed
  strings, with a `#` header recording the scan that produced it. A campaign
  driver reading it never has to parse a verdict.

Artifacts belong under the non-repo data root (`docs/stage-b-design.md` §7.3),
not in the tree.

## `--min-hit-count` is the whole point

What a run encounters depends on the path taken through the map, which is the
*policy's* choice, not the seed's — and the capture will be driven by a
**different** policy from the scan. A seed whose target was found by exactly one
scanned combination says the target is *reachable*, not that it will be
*reached*.

So the qualifying rule counts hits across combinations. Measured on
`STS00100-STS05099` × {random, greedy_damage} × {0, 1} at the commit that
introduced this tool, `Match and Keep!` qualified 152 seeds at
`--min-hit-count 1` and 91 at `--min-hit-count 2` — the 61 seeds that dropped
out are precisely the ones a capture would have been sent to for nothing.
**Use 2 or more.** (Re-derive the figures with the command above rather than
quoting these; they move with the engine.)

## Determinism

Every stochastic decision in a scanned run comes from `fuzz::PolicyRng`, a
private splitmix64 seeded from `policy_seed` — no engine stream is touched — so
a row is a pure function of its case. `--verify-determinism` scans every case
twice and exits 1 on any difference; the same scan run as two separate processes
is byte-identical, which `seed_scan_test` also pins.

## Naming events

`--need-event` takes either spelling, case-insensitively: the enum symbol
(`MATCH_AND_KEEP`) or the game id (`Match and Keep!`). `--list-events` prints
the table. Multiple `--need-event` flags are an AND *within one run*, not an OR
across runs.

## Relationship to `tools/fuzz`

The run loop is `sts::fuzz::run_case`, not a copy of it. This directory adds a
`fuzz::StepObserver` over pass A and nothing else — the scan's value is that its
verdict matches what a capture will see, and a forked policy loop is a loop that
drifts away from that.
