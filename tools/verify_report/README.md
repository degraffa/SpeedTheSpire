# tools/verify_report — verification reporting

Home of the scripted checks behind the G6/G7 checklist lines and the B5.4
verification dashboard (design doc B §7.4–7.5).

## `generate_report.py` — campaign dashboard (B5.4)

Aggregates one or more B5.2 `report.json` files, verifies each included source
artifact against its recorded SHA-256, and writes deterministic Markdown,
JSON, and CSV under `docs/verification/`:

```bat
C:\Python39\python.exe tools\verify_report\generate_report.py
```

The dashboard defaults name the final provenance-compatible G7 cohorts: the
complete 3,000-run random-legal shard
`g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004`, and the
simulator-planned late-Act-1 greedy cohorts
`g7_late_act1_b153_20260801_boss_min4` and
`g7_late_act1_b153_20260801_boss_min4_scan14k`, under the fixed
`D:\STS_BG_Mod\_oracle_data\campaigns` root, and consume the fresh debug
`tier2_coverage.json`. Repeated `--campaign` flags select a different aggregate.
Every report row's local artifact is hash- and header-validated before any
de-duplication. An exact repeated seed is counted once only when its entire
report row agrees; a conflicting repeat is fatal.

The report keeps three action totals separate:

- `captured_actions`: every injected oracle action;
- `replay_clean_actions`: actions from runs classified wholly clean;
- `strict_zero_diff_actions`: the clean total excluding every replay-recognized
  capture-race family (currently obtain/Entropic, Smoke-Bomb escape
  settlement, and Living Wall's transform-preview cardRng burn). Current
  reports carry an all-family total plus a by-kind map;
  the reader remains backward-compatible with old obtain-only v1 reports.

The report never infers that captured actions were diff-clean, and all three
totals remain visible diagnostics. There is no G7 action-count quota after the
v0.1.11 design amendment: the oracle leg instead requires at least 2,000
distinct A20 seeds under at least two policies, no untriaged or open findings,
and an Act-1 boss-reward claim for every BOSS encounter row in the registry.
Likewise, “zero untriaged” means
every non-clean `(campaign, seed, classification)` has an exact reviewed row in
`divergence_dispositions.json`; an `open-*` disposition remains visibly open
and is not acceptance. Every hashed source artifact is reopened at its
header and must identify the selected campaign/seed as A20 Ironclad; joined
with complete named tier-2 coverage of the `a20.yaml` rows, that is the
dashboard's mechanical A20-modifier check. A run whose outcome is anything
other than death or an Act-1 boss-reward claim is rejected rather than counted
as a full-run attempt.

Oracle sightings are literal exact-string `game_id` occurrences recursively
found in campaign `state_json` objects. The same registry loader used by
codegen supplies the rows, and the join refuses a tier-2 report whose registry
shape has drifted. `a20.yaml` has no `game_id`, so those rows report `n/a`
rather than manufacturing a sighting.

## `generate_s2_report.py` — S2.43 dashboard (design §6 S2-G2 items 1–4)

The S2 sibling of the B5.4 dashboard. One deterministic no-argument
invocation reopens every artifact of the S2.43 oracle campaigns, applies the
retest classification sweep, joins the registry, and writes the committed
report under `docs/verification/`:

```bat
C:\Python39\python.exe tools\verify_report\generate_s2_report.py
```

Outputs: `s243-dashboard.md`, `s243-dashboard.json`,
`s243-event-coverage.csv` (the §7.4 event join) and
`s243-artifact-manifest.csv` (every consumed artifact's SHA-256). Re-running
over unchanged inputs rewrites all four byte for byte; the report also carries
a roll-up hash over the sorted (campaign, seed, sha256) triples, so a changed
artifact moves one visible number. The raw captures stay uncommitted (design
B §7.3) — the manifest is what pins them.

The no-argument defaults name today's cohorts: the three breadth groups
`s243_breadth_rand` / `_take` / `_skip`, the four escape-window recapture
groups `s243_recap_*` / `s243_recap2_*`, `s243_resweep5.log` as the retest
sweep, and the `s243_prep` seed-scan summary as the sim-side rare-event
census, all under the fixed `D:\STS_BG_Mod\_oracle_data` root. Repeated
`--breadth` / `--recapture` flags select a different aggregate;
`--retest-log`, `--prep-census`, `--dispositions`, `--registry`,
`--artifact-root` and `--out-dir` move the rest. Cohort membership comes from
each group's own `parallel_group.json`, never from a directory glob. The S2
depth cohorts join through the same flags when S2.V2's scan output schedules
them; nothing about the accounting changes but its numbers.

**What it will not do.** It never infers a bar. Items 2 and 3 have no cohort
yet, so they render as literal shortfalls naming the missing registry BOSS
rows — the B5.4 "literal shortfall" tradition, and the reason double-boss
detection is a real artifact-side detector (two distinct Act-3 `act_boss`
identities in one run) pinned by synthetic fixtures rather than a column
hard-wired false under a comment naming a future task. It also fails loudly
rather than bucketing: an artifact whose hash drifts, whose header does not
identify it as this campaign's A20 Ironclad run, whose records disagree about
their own `act`, whose classification is outside the known vocabulary, or
whose `event_id` is neither a registry row nor one of the explicitly
allowlisted non-registry ids, aborts the report.

Dispositions live in `s243_dispositions.json` (format
`STS-S2-DIVERGENCE-DISPOSITIONS v1`) and are exact, never wildcards. Two
lists, because two different things are dispositioned: `items` keys a run
finding by (campaign_id, seed, classification), and `event_rows` keys design
§6 item 4's per-row alternative to a sighting by a registry `game_id`. The
`superseded-by-recapture` status must name the replacement capture, which the
tool then verifies against its own evidence set — same seed, present, and its
own current classification clean — and whose replay-recognized capture-race
count it prints rather than assumes.

Four rules differ from `generate_report.py`, deliberately:

1. **Classification is layered.** A worker `report.json` carries the
   capture-time verdict; a retest sweep log carries the current one for the
   runs it covers. The sweep wins where it speaks. Both totals are rendered,
   so a triage wave's effect is visible rather than overwritten.
2. **Provenance is per cohort.** Worker provenance must agree *within* a
   group; the groups' fork pins are expected to differ, because the
   escape-window class was closed over two successive fork holds and
   collapsing the pins would hide that.
3. **A non-gameplay terminal is excluded, not fatal.** B5.4 raises; this tool
   drops the run from the full-run count, lists it by seed and outcome, and
   keeps going — a dashboard whose job is printing shortfalls cannot abort on
   one.
4. **A stale disposition is reported, not fatal.** The dashboard must
   regenerate over improving inputs: when a sibling task makes a
   dispositioned run replay clean, the output is a rendered "no longer
   exercised" row. An *untriaged* finding still counts against the bar, which
   is the half that protects the gate.

Unit tests: `test_s2_report.py`, registered as `verify_report_s2_python_test`.
Every fixture is a synthetic temp-directory campaign tree — the committed
suite never touches the oracle data root.

## Compressed CI corpus (B5.4)

`build_ci_corpus.py` deterministically curates exactly 50 distinct,
classification-clean, race-free runs. The committed
`tests/golden/oracle_corpus/act1_a20_50.tar.gz` contains each source JSONL
needed for whole-run replay and its B5.2 translated binary trace. Its adjacent
manifest binds every member hash, trace header, seed, campaign, action count,
and the archive hash.

`ci_corpus_smoke.py` verifies those hashes and safe archive paths, extracts to
the test target's scratch directory, and invokes the real `replay_run_diff
--replay --stop-on-diff` binary. `OracleCorpusReplay.FiftySeedCorpusReplaysZeroDiff`
is in every debug/ASan CI matrix. The sibling synthetic-divergence case mutates
only an extracted copy and requires replay to return nonzero.

The corpus builder's no-argument campaign set remains pinned to the original
B5.4 B5.2/B5.3 inputs recorded in its committed manifest. It is intentionally
independent of the moving dashboard defaults: regenerating a frozen CI corpus
must not silently select a different 50 runs merely because G7 added a larger
evidence cohort.

## `check_tier2_coverage.py` — tier-2 registry coverage (G6 leg 1, design §8(2))

Answers mechanically: **does every registry manifest row have at least one
named, registered, passing tier-2 test exercising it?**

```bash
tools/verify_report/check_tier2_coverage.sh            # debug preset, runs ctest
tools/verify_report/check_tier2_coverage.sh release
tools/verify_report/check_tier2_coverage.sh debug --use-last-log   # fast path
```

The `.sh` entry point works from Git-Bash, cmd/PowerShell and inside WSL; on
the Windows side it re-executes itself through the sanctioned
`tools/wsl_run.sh --script` bridge (conventions.md §6). The build tree for the
chosen preset must already exist (`tools/wsl_run.sh debug`).

Exit **0** iff every row is covered — the G6 bar. Exit 1 lists the uncovered
rows by name. Reports land in `<build>/verify_report/`:

* `tier2_coverage.md` — human-readable, deterministic, the shape B5.4 will
  commit under `docs/verification/`.
* `tier2_coverage.json` — full machine-readable row→test attribution.

### Where the rows come from

The same machinery the build uses, never an ad-hoc YAML parse:
`tools/registry_gen`'s `stsgen.loader` enumerates and validates the rows, and
the per-domain counts are **re-derived by running `gen.py` into a scratch
directory and reading the freshly generated `manifest.hpp`** (design §4.3: the
manifest is what the tier-2 coverage check consumes). A count quoted from any
document is never trusted; a loader/manifest disagreement is a hard error.

### The linking convention (discovered, not imposed)

Surveyed from the corpus as it stood at G6 time — these are the join keys the
existing tests already use, so adopting them required no test renames:

| domain | a test covers a row when… |
|---|---|
| cards, powers, monsters, relics, potions, events | its body references the row's generated enumerator (`CardId::STRIKE`, `RelicId::ANCHOR`, …) |
| encounters (no enum) | its body contains the row's `game_id` as an exact quoted string literal (`"Gremlin Gang"`) — how the corpus drives `resolve_encounter` |
| a20 (no enum, no game_id) | it matches the explicit `SWEEPS` allowlist: the `A20Manifest.*` sweeps the ledger itself names as the per-row machine-check (B4.15) |

Attribution tiers, strongest first; a row is covered by the strongest tier
holding at least one **registered, passing** test:

1. **direct** — reference inside the `TEST` body itself.
2. **file-scope** — reference in a test file's shared helpers/fixtures outside
   any `TEST` body; every test in that file is credited, since helpers only
   execute through the file's tests.
3. **sweep** — an allowlisted whole-domain sweep (see below).

References are found by a small C++ lexer that strips comments and, for brace
matching, string contents — a symbol in a comment covers nothing, and a brace
inside a string literal cannot desync the `TEST`-block parser. "Registered"
comes from `ctest -N`; "passing" from a fresh full `ctest` run (default,
~20 s parallel) or the existing `LastTest.log` under `--use-last-log`.

### Rules chosen where the line was murky (flagged per the G6 brief)

* **Deferred-with-asserted-inertness relics count as covered.** Dead Branch,
  Gambling Chip, the `energyMaster` ten, … (ledger *Deferred obligations*
  table) have tier-2 tests asserting their registry data and their inertness
  (`relic_rares_shop_test` etc.). Those are named passing tests exercising the
  row, so the row is covered; the *obligation* remains tracked by the ledger
  table, which is the right carrier for it. The checker deliberately does not
  try to distinguish "asserts behavior" from "asserts registry data +
  inertness" — that judgement lives in the ledger, not in a grep.
* **The generator's own tests grant no coverage.** `registry_gen_test`
  references every domain's manifest count, but it verifies the codegen
  machinery, not per-row rules content; crediting it would make the check
  vacuous. Hence the sweep **allowlist** is explicit data (test-name regex →
  domain → written justification) and starts with exactly one entry
  (`A20Manifest.*` → a20). Extending it is a reviewed edit.
* **`ctest -N` clobbers `LastTest.log`** (overwrites it with an empty
  start/end stanza — found the hard way). The checker snapshots and restores
  the log around its own `-N` call, and the default mode runs the suite itself
  and parses that run's own output, so recorded results are never silently
  destroyed or silently stale. A bare `ctest -N` typed by hand still clobbers
  the log; `--use-last-log` fails loud on such an empty log.

The dashboard extends rather than replaces this check: it consumes the JSON
row→test attribution and adds the campaign `game_id` sighting count. This
script's exit-code contract remains the G7 re-run of the tier-2 coverage bar.

## `check_g7_proactive_coverage.py` — historical-risk regression audit

The raw action quota was replaced with an executable audit of the defect
families the campaigns actually found: observable/private state, lifecycle and
queue ordering, RNG cardinality boundaries, filtered command identities,
nested screen transitions, and persistent-state/exclusion boundaries. The
checked-in `g7_proactive_manifest.json` maps every family to multiple focused
CTest names and a concrete witness statement.

```bash
tools/verify_report/check_g7_proactive_coverage.sh debug
tools/verify_report/check_g7_proactive_coverage.sh release --use-last-log
```

The checker fails if any required name is absent from CTest or did not pass.
It writes deterministic `g7_proactive_audit.{md,json}` beside the tier-2
report; the gate invocation commits the Markdown result under
`docs/verification/`.
