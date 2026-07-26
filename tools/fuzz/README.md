# Sim self-replay fuzz soak

`fuzz_soak` runs a sequential seed sweep through the engine's legal-action
mask with five deterministic policies: uniform random-legal, greedy damage,
greedy block, hoard gold, and always event. Every case is regenerated from its
four-value identity and replayed; the per-step and final
`RunController` content hashes must agree. A sampled third pass replays the
literal action list used by the reproducer format.

The driver reports counted actions from pass A only. `actions stepped incl.
replay passes` is diagnostic and must not be used for an acceptance total. A
legal action that leaves the controller unchanged is an immediate
`NO_PROGRESS` failure; only genuine cycles of length two or greater use the
separate livelock accounting.

## Build and test

From the repository root:

```bash
tools/wsl_run.sh debug asan release
```

The `fuzz_test` target exercises clean replay, injected mismatch and abort
triage, case-id and literal-action reproduction, legal-move enumeration,
controller hashing, coverage persistence, seed sweeping, and shard-report
merging.

## Run an overnight campaign

`soak.sh` never builds. Give it already-tested release and sanitizer binaries:

```bash
tools/wsl_run.sh --script tools/fuzz/soak.sh \
  --main-bin build/release/tools/fuzz/fuzz_soak \
  --asan-bin build/asan/tools/fuzz/fuzz_soak \
  --seeds 10000 --reps 5 --asan-percent 1
```

The default artifact root is outside the repository at
`/mnt/d/STS_BG_Mod/SpeedTheSpire-campaigns/fuzz`. Each timestamped run
contains the exact commands, separate main and sanitizer logs, versioned
`*_summary_*.kv` files, human-readable reports, `MAIN_REPORT.txt`, and
`ASAN_REPORT.txt`. The sanitizer seed interval starts immediately after the
main interval, so the campaigns are disjoint and their reports remain separate.
Do not commit these campaign artifacts.

`--shard I/N` selects cases by stable global case index, so independent hosts
can run disjoint shards and merge their summaries. A summary records its build
identity (configuration, compiler, sanitizer mode, schema) and full sweep
configuration; merge refuses missing, duplicate, overlapping, incompatible, or
overflowing shards and propagates any shard failure:

```bash
fuzz_soak --merge shard0_summary.kv shard1_summary.kv
```

## Triage

A normal mismatch writes a `STSFUZZ v1` file containing both the case identity
and the minimal action prefix through the first divergent step:

```bash
fuzz_repro finding.repro --regen --verbose
```

An assert, sanitizer abort, or crash cannot write a normal reproducer. Before
each case starts, the driver flushes an `*_inflight_*.txt` journal containing a
copy/paste `fuzz_repro --seed ...` command. A clean worker removes its journal;
`soak.sh` treats any surviving journal as a failed campaign.

Both command-line and file parsers are strict: zero-work sweeps, overflow,
partial case identities, duplicate fields, malformed numeric tokens, and
incomplete summaries are usage errors rather than clean runs.
