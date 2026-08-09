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

### Resuming an interrupted sweep

There is no checkpoint file, and there should not be one: a **shard is the
restartable unit**. A shard is a pure function of the sweep options and its own
index — not of the worker-thread count, not of what the other shards did, and
not of anything a previous attempt left on disk — so an interrupted sweep is
resumed by re-running the shards that did not finish. Re-running one shard
reproduces its `*_summary_s*.kv` byte for byte (`--threads` may differ), and the
merged report of an N-way split is byte-identical to the same sweep run in one
process. Both properties are tests, not conventions:
`FuzzDriver.AnInterruptedShardResumesToAByteIdenticalSummary` and
`FuzzDriver.ShardedSweepMergesToExactlyTheUnshardedSweep`.

A `*_inflight_*.txt` journal that survives a shard is the opposite signal: it
means a worker died mid-case, and the file is the crash reproducer. A clean
shard deletes its own.

## Per-act coverage

The report carries a **per-act** block — cases that stood in each act, boss
fights and boss kills per act, and the act-split of the rooms table (it is a
partition of the act-blind one, so the rows sum). It exists because the S2-G1
gate soak claims *three-act A20 runs*, and without it a sweep that died on
floor 6 every time and a sweep that walked three acts printed the same
`max_floor`-shaped evidence.

Read the zeros. `act never entered by any case: N` and `act N was entered but
its BOSS was never fought` are printed in the NEVER REACHED block, and
`act_boss_kills[3]` and `victories` are two independent probes of the same
event (the combat outcome vs `run_is_victory`) — the report shouts if they
disagree.

**What a soak's own numbers say about depth.** The E0 heuristics here are
coverage generators, not agents: measured over 100,000 cases (S2.41,
`build/release`, 20,000 seeds × 5 policies, A20), 3.2 % of cases reached the
Act-1 boss, 0.11 % crossed into Act 2, **no** case reached the Act-2 boss, and
`victories` was 0. Volume alone does not fix that. A soak is the right
instrument for *breadth* and for the determinism guard; a **directed cohort**
(`tools/oracle_bridge/planner/seed_scan --cohort-list`) or the live driver is
the right instrument for Act-2/3 *depth*.

### Merging an ARCHIVED campaign

Summaries written before the `victories` counter landed (pre-`6d7efc4`) do not
carry that key, and `--merge` rejects them — correctly, because for a live sweep
a missing counter is drift. They cannot be rewritten, and regenerating one
honestly means re-running the whole sweep it summarises.
`--allow-legacy-summaries` reads them anyway:

```bash
fuzz_soak --merge --allow-legacy-summaries old_shard0.kv old_shard1.kv
```

Every counter that vintage lacked reads **0**, which is not the same as having
measured zero. The flag is loud about it in two places, because either one can
be lost: a `LEGACY SUMMARY <file>: … NOT measured` line per file on **stderr**,
and a `provenance: REGENERATED FROM ARCHIVE` line in the **report** itself,
which is the part that gets pasted into a task log. Do not quote a defaulted
counter as a result. Only the keys in `legacy_optional_kv_keys()` may be absent;
any other missing key, unknown key or malformed line is still a hard failure,
with or without the flag.

## Triage

### Exit codes — the one that reads backwards

**`fuzz_repro` exits 1 on NOT REPRODUCED, and that is the PASS verdict on a
fixed build.** Its codes describe what the *replay* did, not whether you are
pleased about it: `0` replayed clean and matched, `1` a failure was reproduced
**or** the recorded one was not, `2` bad usage or an unreadable file. Re-run an
old reproducer against a build that has since been fixed and the recorded
failure no longer happens — the tool prints `RESULT: NOT REPRODUCED` and exits
**1**.

Any CI wiring that equates exit 0 with success therefore reads a **confirmed fix
as a failure** — and reads a reproducer that still fails on an unfixed build the
same way, so the two are indistinguishable by exit code alone. When the question
is "is this finding fixed?", gate on the `RESULT:` line, not on `$?`.

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
