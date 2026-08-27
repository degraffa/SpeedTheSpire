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

The no-argument defaults name today's cohorts under five **roles**, one flag
per role: `--breadth` (the three breadth groups `s243_breadth_rand` / `_take`
/ `_skip` plus the `s243_breadth_top2` top-up), `--recapture` (the four
escape-window recapture groups `s243_recap_*` / `s243_recap2_*`), `--depth`
(the fourteen S2.V2 depth cohorts carrying items 2 and 3 plus the Mind Bloom
directed captures), `--iteration` (the twenty-two divergence-stopped waves
that drove the day's engine and emitter fixes) and `--preflight` (the two
fork-pin preflights). `s243_resweep7.log` is the retest sweep and the
`s243_prep` seed-scan summary the sim-side rare-event census, both under the
fixed `D:\STS_BG_Mod\_oracle_data` root; `--retest-log`, `--prep-census`,
`--dispositions`, `--registry`, `--artifact-root` and `--out-dir` move the
rest. Cohort membership comes from each group's own `parallel_group.json`,
never from a directory glob.

The roles are not decoration. Only `breadth` runs count toward item 1's 2,000;
only a `breadth` or `preflight` cohort is required to have finished its seed
list; only an `iteration` cohort may carry a campaign-level disposition, and
every one of them must; and a `preflight` cohort may carry no disposition at
all — every one of its runs must read clean both as captured and today.

A worker the driver stopped mid-campaign never reaches the postprocess that
writes `report.json`. Its run rows are read from `campaign_progress.json`'s
`seeds_done` block instead (the same additive reach fields), which file spoke
is recorded per cohort, and its classification can only come from the retest
sweep — an artifact neither source classifies aborts the report. The artifact
set is enumerated from the worker **directory**, so the capture the driver
died on is inventoried, hashed and classified like any other, with no
completed run row and therefore no reach claims.

**What it will not do.** It never infers a bar. Every shortfall column is
unchanged from the run that printed four UNMETs — the B5.4 "literal
shortfall" tradition — and only the numbers moved. Nothing is read off a
convenient field either: `boss_kill_acts` is a *set* of act numbers, and the
captures show the Act-2 boss chest's trailing MAP record already carrying
`act: 3`, so both halves of item 3 are read artifact-side from the ordered
Act-3 `act_boss` identities (every identity but the last is witnessed killed;
the last only on a victory), and only a double-boss run that ends in victory
counts toward the bar. It also fails loudly rather than bucketing: an artifact
whose hash drifts, whose header does not identify it as this campaign's A20
Ironclad run, whose records disagree about their own `act`, whose
classification or campaign status is outside the known vocabulary, or whose
`event_id` is neither a registry row nor one of the explicitly allowlisted
non-registry ids, aborts the report.

Dispositions live in `s243_dispositions.json` (format
`STS-S2-DIVERGENCE-DISPOSITIONS v1`) and are exact, never wildcards. Three
lists, because three different things are dispositioned: `items` keys a run
finding by (campaign_id, seed, classification); `event_rows` keys design §6
item 4's per-row alternative to a sighting by a registry `game_id`; and
`campaign_rows` keys an `iteration` cohort by its group id, because what an
iteration wave lost is a whole cohort **seat**, not one finding. The run-level
`superseded-by-recapture` status must name the replacement capture, which the
tool then verifies against its own evidence set — same seed, present, and its
own current classification clean — and whose replay-recognized capture-race
count it prints rather than assumes; the campaign-level one must name a
consumed cohort, other than itself, every one of whose captures reads clean
today. The alternative, `resolved`, names the landed fix instead.

Four rules differ from `generate_report.py`, deliberately:

1. **Classification is layered.** A worker `report.json` carries the
   capture-time verdict; a retest sweep log carries the current one for the
   runs it covers. The sweep wins where it speaks. Both totals are rendered,
   so a triage wave's effect is visible rather than overwritten.
2. **Provenance is per cohort.** Worker provenance must agree *within* a
   group; the groups' fork pins are expected to differ, because the
   escape-window class was closed over two successive fork holds and the
   SecretPortal playtime pin came later still — collapsing the pins would
   hide that.
3. **A non-gameplay terminal is excluded, not fatal.** B5.4 raises; this tool
   drops the run from the full-run count, lists it by seed and outcome, and
   keeps going — a dashboard whose job is printing shortfalls cannot abort on
   one.
4. **A stale disposition is reported, not fatal.** The dashboard must
   regenerate over improving inputs: when a sibling task makes a
   dispositioned run replay clean, the output is a rendered "no longer
   exercised" row. An *untriaged* finding still counts against the bar, which
   is the half that protects the gate. A retest verdict naming a campaign
   outside the selected cohorts is reported the same way — selecting a subset
   must not slander the rest — but a verdict for a campaign that WAS read and
   a seed that was not is a missing artifact, and is fatal.

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

## Compressed CI corpus, Acts 1–3 (S2.46)

`build_ci_corpus.py --three-act` builds the second committed corpus,
`tests/golden/oracle_corpus/three_act_a20_5.{tar.gz,manifest.json}`, in format
`STS-ORACLE-CI-CORPUS v2`. Five whole-run Acts 1–3 A20 captures, pinned one by
one in `DEFAULT_THREE_ACT_PICKS` with the reason each is there — the same
independence discipline as the B5.4 list above, for the same reason.

Three departures from v1, each forced by what the S2 depth evidence is:

* **Provenance is per entry.** The depth cohorts ran under two fork pins on
  purpose; one aggregate pin would hide that. `pipeline_version` is null exactly
  when the driver stopped a campaign before postprocess, and the smoke rejects a
  null on a `complete` campaign.
* **The translated trace is optional**, with a recorded `trace_absent_reason`.
  One entry's translation aborted on an Act-4 `Spire Heart` event id; one wave
  never reached postprocess at all. Both captures replay clean, which is the
  assertion the corpus exists to make.
* **The stored classification is not a selection input.** Two picks were
  classified non-clean by the postprocess that ran on capture day and are clean
  on the landed engine — they are the captures whose divergences drove the day's
  fixes. So `--three-act` requires `--replay-bin` and **re-replays every pick**,
  refusing anything that is not zero-diff to its run terminal with zero
  capture-race records.

`ci_corpus_smoke.py` reads both formats and takes `--expect-entries`, which is
asserted rather than read off the manifest it is checking. On a v2 corpus it
also enforces the corpus's own contract — every entry an act-3 capture, at least
one *completed* double-boss run, both boss-relic policy axes present — because a
curated corpus that quietly lost its double-boss run would still replay green
and would no longer be the evidence
[docs/verification/s2-verification.md](../../docs/verification/s2-verification.md)
cites. `OracleCorpusReplay.ThreeActCorpusReplaysZeroDiff` and its injected
sibling run in every preset.

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

S2.46 extended the manifest to **21 families / 101 named regressions** — the six
G7-era families unchanged, plus fifteen S2-campaign-discovered ones (the
replay-copy shared `misc`, the spawn pre-pass, the combat-terminal
adjudication, card cost state across pile moves and upgrades, the egg reward
OFFER preview, the three liveness senses, the double-boss handoff, the
unmodelled wall clock, the cross-act capture derivations, the live run-state
projections, the deferred hook boundaries, positional monster identity, screen
arm shape and ownership, emitter index-space identity, and the Act-2/3 replay
seams). Design §6 S2-G2 item 5 is that audit's exit code; the per-family
rationale is in
[docs/verification/s2-verification.md](../../docs/verification/s2-verification.md).
