# SIM_SEARCH_BLIND — the information-limited twin, and the measured information premium

Written 2026-09-03. This is the acceptance evidence for `PolicyKind::SIM_SEARCH_BLIND`
(`tools/fuzz/include/sts/fuzz/policy.hpp`, `tools/fuzz/src/policy.cpp`,
`tools/fuzz/src/policy_search.cpp`): the substitution, the determinism and
invariance proofs, and the paired reach measurement the whole task exists to
produce. Scan artifacts (row TSVs) are regenerable from the commands quoted
here and stay out of the tree, under the design §7.3 data root
`D:\STS_BG_Mod\_oracle_data\` (this task's own outputs were kept in the
worktree's root during the run rather than under that path, and are not
committed either way — regenerate with the commands below).

## 0. Why, and the headline

The training repo's first combat expert-iteration run (T2.2) lost to
`SIM_SEARCH` with p=1.0. `SIM_SEARCH`'s combat decisions run a bounded
turn-local search over engine snapshots of the **true** controller — the
rollout sees the hidden draw order, the un-telegraphed monster rolls, every
future intent — while a trained agent under GT0's information contract can
only search over `resample_hidden` worlds. Nobody knew how much of the exit-V0
gap was *information* rather than *search quality*.

`SIM_SEARCH_BLIND` isolates the variable. It is `SIM_SEARCH` with exactly one
substitution: every rollout copy `sim_search_pick` builds is snapshotted from a
`resample_hidden` **twin** of the true state instead of the true state itself,
drawn once per decision so every candidate move in that decision is compared
under the same resampled world. Everything else — the scorer, the run-layer
heuristics, the shared one-draw tie-break — is byte-identical to `SIM_SEARCH`
by construction (every existing kind gate in `policy_search.cpp` is an
explicit `== SIM_SEARCH_HOLD` / `== SIM_SEARCH_KEYS` / `== SIM_SEARCH_SKIP`
check that `SIM_SEARCH_BLIND` simply does not match).

**The headline: the gap is large, and it is an information gap, not a
degenerate policy.** On a paired 2,500-seed fresh A20 grid (`STS700000`–
`STS702499`, one policy seed each, 5,000 rows):

* **Act-1 boss FIGHT rate falls 68.08 % → 38.56 %** (×0.57), **Act-1 boss
  KILL rate falls 34.56 % → 5.16 %** (×0.15).
* **Act-2 boss fight/kill both collapse further**: fight 3.88 % → 0.08 %,
  kill 0.28 % → 0.00 % (7 kills under `SIM_SEARCH`, 0 under
  `SIM_SEARCH_BLIND`).
* **Mean max floor falls 16.42 → 11.44**, and per-seed the paired comparison
  is one-sided: on 1,561 of 2,500 seeds `SIM_SEARCH` reaches a strictly
  deeper floor than `SIM_SEARCH_BLIND` on the *same* seed; the reverse holds
  on only 316; 623 tie.
* **Deaths shift earlier**: 95.76 % of `SIM_SEARCH` rows end in death against
  99.56 % for `SIM_SEARCH_BLIND` (neither policy wins on this grid — A20
  three-act survival is far outside either's reach), and the death floor
  distribution moves from "peaks in the 7–16 range with a real 17–33 tail"
  to "peaks in the 7–16 range with the 17–33 tail cut by 85 %".

Both kinds' output is otherwise governed by the same acceptance-of-invariance
and determinism bars every prior `SIM_SEARCH` variant carries: `SIM_SEARCH`'s
own scan output is proved byte-identical before/after this kind existed, and
`SIM_SEARCH_BLIND`'s scan output is proved deterministic (a case is a pure
function of its `CaseId`) over 500 seeds × 2 policy seeds, each scanned twice.

## 1. The substitution, precisely

`tools/fuzz/src/policy_search.cpp`, `sim_search_pick`, immediately after move
count clamping and before the per-candidate scoring loop:

```cpp
RunController rollout_world = rc;
if (kind == PolicyKind::SIM_SEARCH_BLIND) {
    SamplerRng sampler_rng = sampler_rng_from_seed(static_cast<int64_t>(rng.next()));
    resample_hidden(rollout_world, sampler_rng);
}
```

`rollout_world` then replaces `rc` at both of `sim_search_pick`'s rollout
snapshot sites — the in-combat 1-ply/2-ply candidate copy and the run-layer
(map-node / event-option) floor-rollout candidate copy — so every candidate
this decision is rolled out from the same particle. For every kind other than
`SIM_SEARCH_BLIND`, `rollout_world` **is** `rc` (the `if` never fires), so the
substitution is a compile-time no-op elsewhere; this is what the invariance
proof in §3 checks.

One snapshot site is **deliberately untouched**: the run-layer NO_PROGRESS
demotion guard (`RunController sim = rc;` a few lines later, inside the
`if (!in_combat)` tie-resolution loop) stays on the true `rc`. That check is
structural — "does the real game's `advance()` actually change state for this
action" (the Drug Dealer re-pick corner) — not part of the rollout scoring the
twin substitution covers, and using the twin there would answer a question
about the wrong world.

The sampler seed is a single fresh `PolicyRng::next()` draw, taken from `rng`
— the policy's own private stream, never an engine stream — so the draw is
**sampler-private** (resample.hpp's header note: `SamplerRng` is a distinct
type, un-assignable to any engine stream slot, precisely so this property is
greppable) and costs the engine zero draws. The case therefore stays a pure
function of its `CaseId`, which §4 proves at scale.

Every existing `sim_search`-family rule stays keyed to its own kind
(`kind_holds_powers` returns `kind == SIM_SEARCH_HOLD`; `kind_seeks_keys`
(S3.22) returns `kind == SIM_SEARCH_KEYS`; the boss-relic answer checks
`kind == SIM_SEARCH_SKIP` at its two sites), so `SIM_SEARCH_BLIND` falls
through to `SIM_SEARCH`'s values in all of them without a new clause.

## 2. What ran

Windows `win-release` (clang-cl), `seed_scan` at the standing defaults
(`--ascension 20`, `--max-actions 12000`).

| purpose | cases | rows |
|---|---|---|
| smoke (both kinds, sanity) | `STS600000`–`STS600009` × {`sim_search`, `sim_search_blind`} × ps{0,1} | 40 |
| `SIM_SEARCH` invariance (§3) | `STS500000`–`STS500199` × `sim_search` × ps{0,1}, built once before this change and once after | 400 × 2 builds |
| determinism sweep (§4) | `STS600000`–`STS600499` × `sim_search_blind` × ps{0,1}, `--verify-determinism` (every case scanned twice) | 1,000 base rows (2,000 evaluations) |
| paired reach measurement (§5) | `STS700000`–`STS702499` × {`sim_search`, `sim_search_blind`} × ps0 | 5,000 |

All four seed ranges are **fresh and disjoint** from every previously-quoted
campaign range: S2.41/42 `STS00100`–`STS05099`, S2.V2 `STS100000`–
`STS199999`, S2.V3 `STS200000`–`STS239999`, S2.43/S3.21 captures
`STS430000`–`STS431999`, S3.22/S3.23 `STS500000`–`STS509999` (per
[s3-22-key-reach.md](s3-22-key-reach.md) §1 and its own capture ledgers under
`D:\STS_BG_Mod\_oracle_data\s3\`). The one deliberate exception is the
invariance check in §3, which **reuses** `STS500000`–`STS500199` on purpose —
it is not a reach claim, it is a byte-for-byte regression check on
`SIM_SEARCH`'s pre-existing output, and S3.22 itself used that identical range
for the identical purpose when it added `SIM_SEARCH_KEYS`.

## 3. `SIM_SEARCH` byte-identical before/after

The same fixed scan (`STS500000`–`STS500199` × `sim_search` × ps{0,1}, 400
rows, `win-release`) run once on base `4b781fb` (this task's worktree base)
and once on this branch. Both files sha256:

```
9c86f1903adef7ba79c1f25d7dd0281a49c92cc3fef8b4c29547203c0deaa6b3
```

`cmp` clean. Since this task adds **zero** new TSV columns (unlike S3.22,
which appended three), the whole file matches byte-for-byte — every column,
not just the pre-existing ones.

## 4. Determinism sweep

```
build/win-release/bin/seed_scan.exe --seeds STS600000-STS600499 \
  --policies sim_search_blind --policy-seeds 0,1 --ascension 20 \
  --verify-determinism --format tsv --out determinism_blind.tsv \
  --summary determinism_blind_summary.txt
```

500 seeds × 2 policy seeds = 1,000 cases, each scanned twice by
`--verify-determinism` (2,000 evaluations, 1,086 actions/sec, 145.08 s wall):

```
determinism_mismatches=0
```

Exit 0. Every `SIM_SEARCH_BLIND` case is a pure function of its `CaseId`, as
the header comment on the substitution claims.

## 5. Corpus replay — a no-op, as it must be

This task touches no engine code (only `tools/fuzz/` and the planner's help
text), so the committed three-act oracle corpus must replay unaffected:

```
tools/wsl_run.sh --script tools/corpus_replay.sh
```

All three corpora (`act1_a20_50`, `three_act_a20_5`, `keys_a20_4`), all three
comparison modes (`--replay`, `--costs`, `--masks`), **ZERO-DIFF** in every
cell, and every injected-divergence negative control fails loud as required
(18 comparisons total, 9 clean + 9 controls).

## 6. The paired reach measurement

```
build/win-release/bin/seed_scan.exe --seeds STS700000-STS700624 \
  --policies sim_search,sim_search_blind --policy-seeds 0 --ascension 20 \
  --format tsv --out reach_chunk1.tsv --summary reach_chunk1_summary.txt
build/win-release/bin/seed_scan.exe --seeds STS700625-STS702499 \
  --policies sim_search,sim_search_blind --policy-seeds 0 --ascension 20 \
  --format tsv --out reach_chunk2.tsv --summary reach_chunk2_summary.txt
```

2,500 seeds, one policy seed, both kinds: 5,000 rows, 879,562 actions total,
322.97 s combined wall clock. Merged file sha256
`600ba1c3929a22f36249913c514de7f9ca6527778ffbbde6652491320245c300`.

### 6.1 Depth and boss reach

| metric | `sim_search` | `sim_search_blind` | ratio (blind / search) |
|---|---:|---:|---:|
| Act-1 boss FIGHT | 1,702 / 2,500 (68.08 %) | 964 / 2,500 (38.56 %) | ×0.57 |
| Act-1 boss KILL | 864 / 2,500 (34.56 %) | 129 / 2,500 (5.16 %) | ×0.15 |
| Act-2 boss FIGHT | 97 / 2,500 (3.88 %) | 2 / 2,500 (0.08 %) | ×0.02 |
| Act-2 boss KILL | 7 / 2,500 (0.28 %) | 0 / 2,500 (0.00 %) | ×0 |
| Three-act victory | 0 / 2,500 (0.00 %) | 0 / 2,500 (0.00 %) | — |
| mean max floor | 16.42 | 11.44 | — |
| max floor seen | 41 | 33 | — |

Victories are 0 for both, as expected — this is a 1-policy-seed A20 breadth
grid at the same scale S2/S3 breadth waves used to *measure* survival costs,
not to find wins; `SIM_SEARCH` itself did not clear an act boss beyond Act 1
in a comparable single-policy-seed breadth wave until deep re-seeding
(S3.22 §4).

### 6.2 Deaths (`end_reason == run_over && victory == 0`) by floor bucket

Buckets follow the Act boundaries: 1–6 (Act 1 early), 7–16 (Act 1 late /
Act-1 boss band), 17–33 (Act 2), 34+ (Act 3+).

| bucket | `sim_search` | `sim_search_blind` |
|---|---:|---:|
| floor 1–6 | 226 (9.5 % of deaths) | 532 (21.4 % of deaths) |
| floor 7–16 | 1,338 (55.9 % of deaths) | 1,837 (73.8 % of deaths) |
| floor 17–33 | 824 (34.4 % of deaths) | 120 (4.8 % of deaths) |
| floor 34+ | 6 (0.3 % of deaths) | 0 (0.0 %) |
| **total deaths** | **2,394 / 2,500 (95.76 %)** | **2,489 / 2,500 (99.56 %)** |

`SIM_SEARCH_BLIND` dies earlier and more often: its death mass is
concentrated in the floor 1–16 band (95.2 % of its deaths) where
`SIM_SEARCH`'s is spread with a real floor-17-33 tail (34.4 % of its deaths
reach Act 2 before dying). livelock (a non-death end reason, not counted
above) hits `SIM_SEARCH` **more** often (106 / 2,500, 4.2 %) than
`SIM_SEARCH_BLIND` (11 / 2,500, 0.4 %) — the coverage-accounting reading is
that `SIM_SEARCH` survives long enough to reach the states (deep combat-reward
loops) where the known livelock corner lives, and `SIM_SEARCH_BLIND` mostly
dies before it can.

### 6.3 Per-seed pairing

On the same 2,500 seeds, comparing the two policies' outcome on identical
seeds (paired, not independent):

* **Act-1 boss fight**: both fight it on 802 seeds, `SIM_SEARCH` fights it
  and `SIM_SEARCH_BLIND` does not on 900 seeds, the reverse on only 162,
  neither on 636. The asymmetry (900 vs 162, ×5.6) is the information premium
  stated per-seed rather than as a population average.
* **Max floor**: `SIM_SEARCH` reaches strictly deeper on 1,561 / 2,500 seeds
  (62.4 %), `SIM_SEARCH_BLIND` deeper on 316 / 2,500 (12.6 %), tied on 623
  (24.9 %).

## 7. The information premium — one paragraph

`SIM_SEARCH − SIM_SEARCH_BLIND` on this paired grid is large and one-sided at
every depth this measurement can see: knowing the true draw order and future
intents roughly doubles the Act-1 boss fight rate, more than sextuples the
Act-1 boss kill rate, and very nearly closes off Act-2 boss contact entirely
when it is missing (2 fights, 0 kills, against `SIM_SEARCH`'s already-thin 97
fights / 7 kills). Because both kinds share the identical scorer, run-layer
heuristics and search budget, and the ONLY thing that changes is which world
the rollout previews, this gap is a clean lower bound on the information
premium available to a search that can see hidden state versus one that
cannot — it is not a search-quality artifact, since search quality (ply depth,
candidate cap, tie-break) is held byte-identical between the two kinds. Read
against T2.2's finding that a trained agent (searching over `resample_hidden`
particles, same as `SIM_SEARCH_BLIND`) lost to `SIM_SEARCH` at p=1.0: this
report says a meaningful share of that margin is not a training deficiency at
all, it is the omniscience `SIM_SEARCH` is built on and `SIM_SEARCH_BLIND` is
built to remove — so `SIM_SEARCH_BLIND`, not `SIM_SEARCH`, is the scripted
baseline an information-limited agent should be compared against, and
`SIM_SEARCH` remains useful only as an upper bound / oracle-quality reference,
never as a fairness target.

## 8. Acceptance evidence (commands + verdicts)

**Six presets BUILD.** WSL (`tools/wsl_run.sh --script tools/build_presets.sh
debug asan release`): `PRESETS BUILT: debug asan release`, all three exit 0.
Windows (`win-debug` / `win-asan` / `win-release`, configured + built through
a vcvars64 + LLVM wrapper): all three exit 0, `/EHsc` present in each
`CMakeCache.txt` (`grep -c /EHsc build/win-*/CMakeCache.txt` → 1 each).

**`SIM_SEARCH` byte-identical before/after** (§3): sha256
`9c86f1903adef7ba79c1f25d7dd0281a49c92cc3fef8b4c29547203c0deaa6b3` on both
sides, `cmp` clean, whole file (no new columns to exclude).

**Determinism** (§4): `STS600000`–`STS600499` × `sim_search_blind` × ps{0,1},
`--verify-determinism` → `determinism_mismatches=0`, exit 0.

**Corpus replay** (§5): `tools/wsl_run.sh --script tools/corpus_replay.sh` →
all three corpora × all three modes ZERO-DIFF, all negative controls fail
loud, 18/18.

**Paired reach** (§6): `STS700000`–`STS702499` × both kinds × ps0, 5,000
rows / 879,562 actions, tables above.

**`check_stale_counts.sh`**: `check_stale_counts: clean`, exit 0.
**`check_doc_links.sh`**: run from Git Bash on the Windows host after this
report and the ledger row landed; expected `clean` (this file introduces no
counted pass ratios and its only links are the two above, both already
resolvable in-tree).

No ctest was run and no test was added, per the 2026-09-03 owner directive
(conventions.md §1).
