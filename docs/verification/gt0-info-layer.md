# GT0 — sim-side information layer gate: evidence summary

Written by the GT0 gate task (2026-08-04). This is evidence accounting for the
GT0 checklist in [../training-tasks.md](../training-tasks.md); the gate
certifies the sim half of InitialPlan M6 per
[../training-plan.md](../training-plan.md) §2. Tag applied at landing:
`gt0-info-layer`.

**Tree under test:** branch `gt0-info-layer`, base
`e7512c8a89b61e9c6ca9847aa8ed86421d156b32` ("T0.5: leak-gate CI — hidden twins
+ total-byte tripwire"), which is the sha at which all seven T0 tasks are
landed. Every number below was produced on **this integrated tree**, not
carried forward from the individual task Logs — that re-run is the gate.

**Counts are cited, not quoted.** Per the CLAUDE.md stale-numbers rule, the
suite total is whatever `ctest --preset <p> -N | tail -1` reports; the tables
below record *outcomes* (`100% tests passed, 0 failed`) and the per-suite test
names that carry the acceptance, not a hardcoded repo-wide count.

---

## 1. Preset matrix (all six, once, on the integrated tree)

Configure + build + `ctest`, full matrix. WSL/GCC presets via
`tools/wsl_run.sh`; `win-*` presets natively with `clang-cl` from
`C:\Program Files\LLVM\bin`.

| Preset | Host / compiler | Configure | Build | `ctest` |
|---|---|---|---|---|
| `debug` | WSL Ubuntu-2404 / GCC 13 | ok | ok | **PASS** — 100 % passed, 0 failed |
| `asan` | WSL / GCC 13 + ASan/UBSan | ok | ok | **PASS** — 100 % passed, 0 failed |
| `release` | WSL / GCC 13, LTO | ok | ok | **PASS** — 100 % passed, 0 failed |
| `win-debug` | Windows / clang-cl | ok (100.8 s) | ok | **PASS** — 100 % passed (218.5 s) |
| `win-asan` | Windows / clang-cl + ASan/UBSan | ok | ok | **PASS** — 100 % passed (220.8 s) |
| `win-release` | Windows / clang-cl, LTO | ok | ok | **PASS** — 100 % passed (122.2 s) |

`ctest --preset win-debug -N | tail -1` for the suite count; the WSL and
Windows presets report the same total, which is the byte-identity property
CLAUDE.md records for this build.

## 2. Per-task acceptance re-runs

Each row re-runs the named acceptance from that task's block in
[../training-tasks.md](../training-tasks.md). Binaries are the `win-debug`
build unless noted; every one of these suites also ran inside the six `ctest`
runs above.

### T0.1 / T0.2 — `PublicView` combat block, run phases, mask channel, knowledge projection

| Suite | Result |
|---|---|
| `public_view_test` | **PASS** — 43 tests from 6 suites |

Covers the T0.1 audit-backed layout walk (including
`PublicViewLayout.V2TailHasNoImplicitPadding`, which pins
`offsetof(PublicView, gold)` so a tail-append that would break v1 offsets
cannot be silent), Runic Dome suppression parity with the omniscient encoder,
draw-permutation byte-equality, the per-phase screen assertions, the
chest-masking pre/post-open pair, and
`PublicViewKnowledge.FrozenEyeFullOrderRoundTrips`.

The audit-table deliverable itself is [../public-view-audit.md](../public-view-audit.md)
(v2, complete — no `T0.2` cell remains in the v1 column) and its
schema-evolution note. Its Class column is executable as of T0.5 — see the
T0.5 subsection below.

### T0.3 — `KnowledgeState` + observability transforms

| Suite / test | Result |
|---|---|
| `knowledge_test` | **PASS** — 12 tests |
| `RegistryGen.UnknownObservabilityValueFailsWithClearError` (codegen negative test) | **PASS** |
| `Knowledge.MembershipTableListsExactlyTheDeclaredRows` | **PASS** |

The named acceptance tests are all present and green:
`HeadbuttPlaceKnownTop`, `ShuffleClears`,
`WildStrikeAfterHeadbuttYieldsRelativeOrderConstraint`,
`FrozenEyeRevealsFullOrderAndSurvivesDraws`,
`LouseBiteRollRevealedAtBiteTelegraph`,
`RunicDomeSuppressesTelegraphReveal`, plus
`RecordingNeverPerturbsCombatStateOrRng`. The generated membership table lists
exactly `FROZEN_EYE` / `RUNIC_DOME`, and the codegen fails loudly on an unknown
`observability:` value.

### T0.4 — `resample_hidden` belief sampler

| Suite | Result |
|---|---|
| `resample_test` | **PASS** — 23 tests from 11 suites |

One suite per plan §2.4 table row — `SamplerDrawOrder`,
`SamplerFreshStreams`, `SamplerFakeRunSeed`, `SamplerEncounterSuffix`,
`SamplerRelicPools` (incl.
`CanSpawnRejectionCornerCaseIsHandledFromTheStoredPool`),
`SamplerMatchAndKeep`, `SamplerTreasureChest`, `SamplerMonsterRolls`,
`SamplerPublicQuantities`, `SamplerDeterminism` — plus the acceptance's named
canary:

- **`SamplerPoisonedSeedCanary.ParticleNeverTouchesTheTrueSeedOrItsStreams`
  — PASS.** The zero-engine-draw property is additionally type-checked by the
  distinct `SamplerRng` type.

### T0.5 — leak-gate CI: twins + tripwire

| Suite | Result |
|---|---|
| `twin_test` | **PASS** — 10 tests from 6 suites (1179 ms) |
| `tripwire_test` | **PASS** — 8 tests from 2 suites |
| `twin_fixture_test` | **PASS** — 5 tests (242 ms) |

**Twin sweep, per-phase counts** (emitted by
`TwinSweep.PublicViewAndMaskAreByteIdenticalInEveryRunPhase`, asserted by the
test itself, reproduced verbatim on this tree):

```
twin sweep: 10852 states; phase0=0 phase1=345 phase2=698 phase3=7560
            phase4=1605 phase6=120 phase7=73 phase8=18 phase9=367 phase10=66
```

10,852 states over the **9 reachable phases** (the acceptance bar is ≥ 1,000),
zero `PublicView`+mask differences. `TwinPhaseCoverage.UnimplementedRoomParkIsTwinInvariant`
covers the parked room phase, and
`TwinSweep.ResolutionQueuesAndTurnFlagAreNeverPerturbed` pins the T0.2 note
that queues / `turn_has_ended` are derived and not twin material.

**Reveal-timing:** `TwinRevealTiming.ChestContentsAreTwinVariantBeforeTheOpenAndPinnedAfter`,
`…LouseConstructionRollIsTwinVariantUntilItIsTelegraphed`,
`…MatchAndKeepFaceDownSlotsAreTwinVariantAndFlipsArePinned` — all PASS.

**Mask twin-invariance:** `TwinMask.RunAndCombatMasksAreByteIdenticalAcrossTwins`
— PASS.

**Known leak canary (audit §9a):**
`TwinDrawChoiceLeak.MaskReadsRawDrawSlotsWhileADrawSourcedChoiceIsOpen` —
PASS, i.e. the recorded leak still exists and the pin in `make_hidden_twin` is
still load-bearing. This test going red is the signal that the action space was
repaired and that the pin plus this test should both be deleted. Carried
forward to the training repo in
[../training-contract.md](../training-contract.md) §4a.

**Tripwire negative controls** (the "demonstrably fires" half of the
acceptance) — all four PASS:

- `TripwireNegative.FiresOnAScratchFieldAddedToRunController`
- `TripwireNegative.FiresWhenAClassificationRowIsRemoved`
- `TripwireNegative.FiresOnOverlappingRows`
- `TripwireNegative.FiresWhenAGapIsDeclaredTooSmall`

Positive side: `Tripwire.EveryClassifiedStructIsTiledExactly`,
`…EveryRowHasANonZeroSizeAndALeafClass`, `…RunSeedIsClassifiedHidden`,
`…TheStreamsAndPoolsAreClassifiedHidden`.

**Fixture round-trip:** `twin_fixture_test` green on the committed
`tests/golden/twin_fixtures/twins_v1.bin` —
`CommittedFileLoadsWithCurrentStamps`,
`ReplayingEveryCommittedCaseReproducesItsStoredView` (every case rebuilt from
its recipe reproduces the stored view, and so does its twin),
`WriteReadRoundTripIsByteIdentical`,
`LoaderRefusesAStampMismatchWithANamedReason`,
`LoaderRefusesATruncatedAndAnOverlongFile`.

`asan` coverage for this task's acceptance is the `asan` and `win-asan` rows of
§1, in both of which these three suites ran.

### T0.6 — sampler distributional suite (nightly mode)

Run **3× locally** through the documented nightly entry point, on the
`win-release` build:

```bash
tools/dist_check/sampler_dist.sh win-release     # STS_SAMPLER_DIST_MODE=nightly
```

All three runs exit 0, `5 tests from 2 test suites … PASSED`. Normalizing only
gtest's wall-clock timings, **run 1 vs run 2 and run 1 vs run 3 are
byte-identical** — including every p-value, which is the determinism property
the task's fixed sampler seeds are for.

Pre-registered family (nightly N, family-wise α = 1e-3 under Holm), identical
across all three runs:

| Hypothesis | chi² | df | p | n |
|---|---|---|---|---|
| `draw.unconstrained_permutation_uniform` | 20.328 | 23 | 0.622053 | 24000 |
| `draw.exact_prefix_conditional` | 2.173 | 5 | 0.824726 | 24000 |
| `draw.relative_order_interleaving` | 4.995 | 11 | 0.931409 | 24000 |
| `encounter.weak_suffix_pair` | 2.6212 | 5 | 0.758142 | 30000 |
| `encounter.strong_suffix_pair` | 69.9681 | 71 | 0.512351 | 90000 |
| `encounter.elite_suffix_pair` | 4.77283 | 3 | 0.189207 | 30000 |
| `relic.remainder_permutation_uniform` | 21.272 | 23 | 0.564481 | 24000 |
| `relic.remainder_position_marginal` | 11.233 | 11 | 0.423955 | 24000 |
| `seedfilter.weak_second_encounter` | 0.93895 | 2 | 0.625331 | 49615 |

`seedfilter.weak_second_encounter` is the bounded seed-filtered sanity check,
executed only in nightly mode and only on the sole uncoarsened row — the other
eight test the **declared contract**, per plan §2.6d and the coarsening list in
[../training-contract.md](../training-contract.md) §5a.

Negative controls (deliberately-biased sampler mutants, through the identical
statistic path at full N) — all rejected:

| Mutant | chi² | df | p |
|---|---|---|---|
| `mutant.draw_naive_shuffle` | 688.098 | 23 | 1.42503e-130 |
| `mutant.relic_remainder_early_return` | 33985.5 | 23 | 0 |
| `mutant.encounter_ignores_weights` | 4778.53 | 71 | 0 |

`SamplerDistribution.SuiteIsDeterministicAcrossReruns` and
`SamplerDistribution.PreRegisteredFamilyIsRetainedUnderHolm` are both green in
each run; the per-commit smoke subset (N/10, K=8) ran in all six `ctest` runs
of §1.

**Cross-host determinism re-verified at the gate, not inherited.** A fourth
run through the WSL/GCC `release` build —

```bash
tools/wsl_run.sh --script tools/dist_check/sampler_dist.sh release
```

— reproduces **every chi², df, p and n in both tables above, digit for
digit**, against the clang-cl `win-release` numbers. That is the property the
scheduled nightly depends on: a p-value that moved between hosts would make
"green on three consecutive nights" unfalsifiable.

**This is the one acceptance line the gate cannot complete now** — see §5.

### T0.7 — `public_hash` + omniscient boundary

| Suite | Result |
|---|---|
| `public_hash_test` | **PASS** — 11 tests |
| `omniscient_boundary_test` | **PASS** — 4 tests (3365 ms) |

Hash stability, both directions of the acceptance:
`HiddenTwinsHashEqualInCombat`, `HiddenTwinsHashEqualAtMapChoice`, and
`TwinsAreNotTriviallyIdenticalStates` (so the equality is not vacuous), against
`APublicScalarDifferenceChangesTheHash`, `TwoDifferentRunsHashDifferently`,
`TheRunPhaseIsPartOfTheIdentity`, `TheVersionStampIsPartOfTheIdentity`.
`MaskBytesAreInsideTheHashedRegion` pins the structural mask inclusion, and
`EncodingTwiceIntoDirtyBuffersHashesEqual` pins the every-byte-assigned
precondition the raw-byte hash rests on.

Boundary check fires on its negative test:
`OmniscientBoundary.RejectsAnActorThatReachesTheOmniscientSurface` (against
`AcceptsAPublicViewOnlyActor`, `AcceptsAHatchedContrastingComment`,
`FailsLoudlyOnABadArgument`), all run against committed fixture directories via
`--scan`, so it is durable on either host.

## 3. Check scripts

Run from Git-Bash on the Windows host (`check_omniscient_boundary.sh` is a
git-side check in default mode and must not go through WSL — conventions §6):

| Script | Output | Exit |
|---|---|---|
| `tools/check_stale_counts.sh` | `check_stale_counts: clean` | 0 |
| `tools/check_doc_links.sh` | `check_doc_links: clean` (50 files scanned, 53 indexed — re-run after this gate's two docs landed) | 0 |
| `tools/check_omniscient_boundary.sh` | `check_omniscient_boundary: clean (5 files checked)` | 0 |

All three are steps of the `stale-numbers` CI job, and all three were re-run
after this gate's two new docs were written — the `check_doc_links` totals
above already include them.

## 4. Published contract for the training repo

[../training-contract.md](../training-contract.md) — the consumer-facing
contract: `PUBLIC_VIEW_VERSION`, the layout summary and how to read the audit
table, the embedded `PvMask`, `public_hash` and its soundness precondition, the
omniscient-boundary rule and the check script's contract, the twin-fixture
container (pointing at the fixture README rather than duplicating it), the
`KnowledgeState` projection semantics including all four declared §2.2/§2.6d
coarsenings, and the audit-§9a mask leak with its canary test name. It cites
the landed docs rather than restating them.

## 5. Open residue at the gate — the sole one

**Sampler distributional suite green on ≥ 3 consecutive *scheduled* nightly
runs** (T0.6). This is a wall-clock obligation, not a verification the gate can
force: `.github/workflows/nightly.yml` fires on `schedule` (`cron: 0 7 * * *`)
plus `workflow_dispatch`, and **schedules only fire on `master`**, so run 1
must be forced by `workflow_dispatch` after this gate lands.

It is carried as the single row of the **Deferred obligations** table in
[../training-tasks.md](../training-tasks.md), owner **"GT0 gate check"** — that
row was verified present at the gate and is **re-owned, not discharged**. What
GT0 *does* discharge is everything that makes the nightly meaningful before it
has run three times: local 3× byte-identical determinism (the T0.6 subsection
of §2),
cross-host determinism (GCC and clang-cl produce digit-identical p-values,
re-verified here rather than inherited from the T0.6 Log), and the mutant
rejections. Record the three run URLs/dates in that row and mark it
DISCHARGED when observed.

Nothing else was found open. No T0 acceptance failed to reproduce on the
integrated tree.
