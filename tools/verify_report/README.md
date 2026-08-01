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

The dashboard defaults name the final provenance-compatible G7 cohorts:
`g7_greedy_b153_20260729_200000_200011`,
`g7_random_b153_20260729_300000_324999`, and the simulator-planned late-Act-1
cohort `g7_late_act1_b153_20260801_boss_min4`, under the fixed
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

Only the last total drives the displayed G7 action shortfall. The report never
infers that captured actions were diff-clean. Likewise, “zero untriaged” means
every non-clean `(campaign, seed, classification)` has an exact reviewed row in
`divergence_dispositions.json`; an `open-*` disposition remains visibly open
and is not acceptance. The G7 volume verdict also requires at least two literal
campaign policy names; a million actions from one generator cannot satisfy the
frozen mixed-generator clause. Every hashed source artifact is reopened at its
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
