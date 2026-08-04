# Phase T Task Ledger — Player-Information Layer + A20H Training Program

Execution tracker for [training-plan.md](training-plan.md) (the Phase T
spec — this file never overrides it; on conflict the plan doc wins and this
file gets fixed). [stage-a-design.md](stage-a-design.md) and
[stage-b-design.md](stage-b-design.md) remain frozen and in force for
everything they cover; [conventions.md](conventions.md) is binding on every
task here exactly as it is for the Stage B ledger.

**This ledger holds only what is open.** When a task lands, its block gains
a Log line; large completed blocks move to a `training-log.md` archive once
one exists, mirroring the Stage B convention.

## Orchestrator protocol

- Statuses: `[ ]` todo · `[~]` in progress · `[x]` done · `[!]` blocked.
- One sub-agent per task, self-contained brief, own worktree via
  `tools/task_worktree.sh create <task>` (run from Windows); orchestrator
  re-verifies and lands on `master` — one task = one commit. Model choice
  per CLAUDE.md: Fable for larger/ambiguous tasks, Opus for established
  boilerplate.
- A task is **done only when its Acceptance block passes** — run the
  commands, don't infer. Tests land in the same change as the code they
  verify.
- Respect `Deps:`. Tasks with disjoint deliverables and satisfied deps may
  run in parallel (∥). Gates **GT0–GT5** are stop-the-line for their phase;
  the GT namespace is deliberately distinct from Stage A/B's G1–G7 so Stage
  C planning can continue the G series without collision.
- **Two-repo rule:** T0.x and TE.x tasks land in this repo under full
  conventions. T1.x+ tasks land in the training repo that T1.1 creates;
  they are *tracked* here until that repo grows its own ledger (a T1.1
  deliverable), after which this file keeps only the cross-repo gates.
  Training-repo tasks never modify this repo except where a task block
  explicitly says so.
- **Capacity rule (plan §4.4):** sim-repo agent capacity is prioritized
  **G7 close → T0.x → TE.2 (S2 authoring)**, in that order. Nothing in this
  ledger may starve the open G7 gate in
  [stage-b-tasks.md](stage-b-tasks.md).
- Task body prose before the `**Deps:**` line is the block's Deliverables
  field (conventions §3 shape, folded for brevity).
- `tools/check_stale_counts.sh` exempts ledger `[x]`/`[!]` lines and
  `docs/*-log.md` archives generically (globs generalized in the change
  that landed this ledger — no per-ledger checker edits needed again).
  Task Logs still cite `ctest -N` rather than quoting counts, per the
  CLAUDE.md rule.
- Gate tags: sim-repo gates tag here (GT0 = `gt0-info-layer`);
  training-repo gates (GT1+) tag in the training repo once it exists.

## Deferred obligations

| Obligation | Deferred by | Owner task | Detail |
|---|---|---|---|
| Sampler distributional suite green on ≥ 3 consecutive *scheduled* nightly runs (local 3× stability + cross-host determinism proven at landing; schedules fire only on master — force run 1 via workflow_dispatch) | T0.6 | GT0 gate check | `.github/workflows/nightly.yml` → `tools/dist_check/sampler_dist.sh`; record the three run URLs/dates here when observed, then mark DISCHARGED |

---

## Phase T0 — Information layer, simulator repo (Gate GT0 = InitialPlan M6)

- **T0.1** `[x]` ∥ **PublicView: combat block + schema skeleton.** New
  versioned POD `PublicView` and pull API
  `encode_public_view(const RunController&, PublicView&)` beside
  `legal_actions` (do not fatten `StepResult`). Combat section closes every
  gap in the `ObsBuffer` stub: player powers, monster block, full
  per-monster power lists (no 4-slot truncation), draw pile as unordered
  multiset, discard/exhaust contents, potion slots. Reserved zero-filled
  fields for S2/S3 structure (keys bitflags, boss-relic choice, act index
  to 4, second-boss slot) land in v1 per plan §2.1. Trivially copyable,
  explicit padding, one `PUBLIC_VIEW_VERSION` stamp.
  **Deps:** — **Acceptance:** field-by-field audit table
  (CombatState/RunController field → PublicView field | derived | excluded-
  hidden) committed beside the header and complete — the audit is the
  deliverable's proof, and T0.5's tripwire consumes it; a schema-evolution
  note (which changes are additive vs breaking) committed beside the
  header; unit tests cover a populated combat state incl. Runic Dome
  suppression parity with `encode_observation`; all six presets green.
  **Log:** 2026-08-03 — landed. `PublicView` v1 (3760 B POD, 24-slot power
  lists, sorted draw multiset, reserved S2/S3 fields) +
  `encode_public_view`; audit table + schema-evolution note in
  [public-view-audit.md](public-view-audit.md) (both `flags` words audited
  bit-by-bit, T0.2 rows pre-structured); 16 unit tests incl. Runic Dome
  parity and draw-permutation byte-equality; six presets green
  (`ctest -N` for the count). Open notes for T0.2: combat relic mirror is
  deferred to the always-block; queues/`turn_has_ended` classified
  derived-excluded pending the mask channel; `MonsterState.pad0` excluded
  wholesale — revealed rolls arrive via T0.3 projection.

- **T0.2** `[x]` **PublicView: run phases + mask channel + knowledge
  projection.** Per-phase sections projecting the
  reward/shop/event/Neow/rest/treasure screen structs; `TreasureChest`
  contents masked until opened (size only); `RunActionMask` bytes included
  in the hashed public serialization. Always-block:
  hp/max-hp/gold/floor/ascension, master deck multiset, relics+counters,
  potions, full current-act map incl. emerald node, pity and membership
  counters, consumed encounter prefix. **KnowledgeState projection into
  PublicView**: known positions / relative-order constraints / Frozen-Eye
  full order, plus revealed monster construction rolls — the §3.1
  order-constraint flags the training repo reads live here (an omitted
  field is twin-invariant, so no later test class catches this seam).
  **Deps:** T0.1, T0.3 **Acceptance:** per-phase unit tests assert (a) each
  screen's on-screen contents appear, (b) chest contents absent pre-open /
  present post-open, (c) the serialized view of two states differing only
  in chest contents is byte-identical pre-open, (d) a Headbutt-known top
  card and a Frozen-Eye full-order state each round-trip their constraints
  through `encode_public_view`; audit table extended to every RunController
  transient struct; presets green. **Log:** 2026-08-03 — landed.
  `PublicView` v2 (additive tail-append, 6032 B, v1 offsets pinned by
  test): always-block incl. full act map + emerald node, per-phase screens
  gated on *on-screen* (not non-empty — Dead Adventurer pre-stocks
  `rc.rewards`), `PvMask` embedded by value so the mask cannot be dropped
  from the hash, knowledge projection as per-slot constraint ranks
  parallel to the sorted draw multiset. Two unbriefed leaks found and
  masked: per-event `EventDialogState.scratch` publicity (Dead Adventurer
  packs its loot shuffle + elite id there at room entry) and Match & Keep
  face-down board ids. Master deck deliberately carried in engine order —
  it is the index space `can_choose_master_deck[]` addresses. Audit
  extended (§8 transients / §9 mask / §10 knowledge). Six presets green.
  Note for T0.5: `make_hidden_twin` must NOT perturb resolution queues or
  `turn_has_ended` (derived, mask-moving — not twin material).

- **T0.3** `[x]` ∥ **KnowledgeState + observability transforms.** In-engine
  draw-position knowledge with **relative order constraints** (not absolute
  indices): set by Headbutt-style placement, full-order reveal by Frozen
  Eye, cleared/rewritten on shuffle, correctly weakened by random-position
  insertion (Wild Strike/Reckless Charge interleaving semantics per plan
  §2.2); revealed monster construction rolls (e.g. Louse bite damage)
  recorded at reveal. Transform vocabulary `HIDE_INTENT` /
  `REVEAL_DRAW_ORDER` in code; registry `observability:` field declares
  membership (Runic Dome, Frozen Eye) with codegen support.
  **Deps:** — **Acceptance:** named tests: Headbutt place→known-top;
  shuffle clears; Wild Strike after Headbutt yields relative-order (not
  absolute) constraint; Frozen Eye reveals full order and survives
  in-combat draws; codegen fails loudly on an unknown `observability:`
  value (negative test, `parse_pickup`-style — the loader currently
  ignores unknown row keys silently, so the field needs a fail-loud
  parser) and the generated membership table lists exactly the declared
  rows (Runic Dome, Frozen Eye); presets green. **Log:** 2026-08-03 —
  landed. `KnowledgeState` lives as a by-value `RunController` member
  (CombatState/RunState stay byte-hashed; controller is the sanctioned
  transient layer), reached from engine mutation sites via a per-thread
  RAII `KnowledgeScope`; hooks at every draw-pile mutation incl. Louse
  bite-roll reveal with Runic Dome retro-gating. `observability:` registry
  field + fail-loud parser + generated membership table (exactly
  FROZEN_EYE/RUNIC_DOME). All named acceptance tests green; six presets
  green. Deliberate contract choice recorded in `knowledge.hpp`: random
  insertion implements the plan §2.2 uniform-interleave contract, which is
  *coarser* than the JDK mechanic (`addToRandomSpot` can never displace
  the top card) — sound but weaker; T0.4/T0.6 must sample against the
  contract, not the mechanic.

- **T0.4** `[x]` **`resample_hidden` belief sampler.** Implements the plan
  §2.4 per-source table: draw permutation under KnowledgeState constraints
  (sampler-private RNG — zero engine-stream draws); encounter-suffix Markov
  continuation overwriting `RunController.lists`, boss list conditioned on
  the public `boss_list[0]`; relic-pool remainder re-permutation honoring
  pop-time `canSpawn` corner cases; Match & Keep board pin-and-permute;
  fresh streams for lazily-drawn sources; fresh `mapRng`; **fresh fake run
  seed per particle** so floor reseeds regenerate fake futures; all public
  quantities preserved.
  **Deps:** T0.3 **Acceptance:** unit tests per table row (each row's
  preserve/condition/fresh behavior asserted on constructed states); a
  particle stepped through a full combat + floor transition never touches
  the true seed (asserted via a poisoned-seed canary state); determinism:
  same sampler seed → identical particle; presets green. **Log:** 2026-08-03 —
  landed. `resample_hidden` in `resample.hpp/.cpp` with a distinct
  `SamplerRng` type so zero-engine-draw is type-checked; per-row coverage
  incl. marker-shuffle draw permutation against the §2.2 contract,
  Markov encounter-suffix continuation (`continue_monster_lists`),
  boss-list conditioning, relic-remainder re-permutation (Girya-forced
  canSpawn rejection verified), Match & Keep pin-and-permute, fresh
  independent streams + fake run seed per particle. Poisoned-seed canary
  replaces run_seed and all fourteen streams and demands byte-identical
  particles through combat + reward + floor reseed. Six presets green.
  Contract coarsenings declared at-site (relic membership under canSpawn;
  Match & Keep miss-memory) — T0.6 must test the contract, not
  seed-filtered reality; chest contents conditioned on the public size
  band per §2.1.

- **T0.5** `[x]` **Leak-gate CI: twins + tripwire.**
  `make_hidden_twin(state, rng)` utility (permute draw suffix, reroll
  streams, re-permute pool remainders, re-continue encounter suffix);
  per-commit tests: twin `PublicView`+mask byte-equality at every phase; the
  **total-byte classification tripwire** — every `RunController` byte
  classified public/hidden/derived (`run_seed`: hidden), failing when
  `sizeof` grows without a classification row; reveal-timing tests; mask
  bits derived from public state only (twin-invariant masks). Exports twin
  fixtures for the training repo.
  **Deps:** T0.2, T0.4 **Acceptance:** twin equality green across ≥ 1,000
  fuzz-generated states covering every RunPhase; tripwire demonstrably
  fires on a scratch field addition (negative test); fixture export
  round-trips; presets green incl. asan. **Log:** 2026-08-04 — landed.
  `make_hidden_twin` built ON `resample_hidden` (one definition of
  hidden), `public_view_first_difference` names the leaking member;
  queues/`turn_has_ended` unperturbed per the T0.2 note, pinned by test.
  Byte tripwire in `byte_class.hpp`: literal-gap rows over
  RunController + substructs, static_assert + runtime naming of
  unclassified ranges; four parameterized negative controls. Twin sweep:
  10,852 states, 9 phases (per-phase counts asserted), zero leaks.
  Fixtures: recipe+payload `twins_v1.bin` (18 cases) — rebuild by
  replay, loader refuses stamp/length mismatch. Found+fixed recurrence
  of the undeclared-padding trap (CombatState ×2, RunController tail —
  no offset/sizeof/fixture moved; conventions §8 updated, tripwire named
  the general elimination). KNOWN mask leak documented (audit §9a):
  slot-indexed DRAW-source CHOOSE exposes draw slot types — action-space
  change, out of scope; `TwinDrawChoiceLeak` goes red when fixed.

- **T0.6** `[x]` ∥ **Sampler distributional suite (nightly).** Tests
  against the *contract's* closed-form conditionals (plan §2.6d): small-pile
  permutation enumeration chi-square; encounter-suffix continuation vs
  brute-force chain enumeration; relic-remainder uniformity; a bounded
  seed-filtered sanity check only on short prefixes where acceptance rates
  permit. Wired as a nightly job beside the existing dist-check tooling.
  **Deps:** T0.4 **Acceptance:** suite green on ≥ 3 consecutive nightly
  runs; each hypothesis pre-registered with its α in the test header;
  deliberately-biased sampler mutant (negative control) rejected.
  **Log:** 2026-08-03 — landed. `tests/sampler_dist_test.cpp`: 9
  pre-registered hypotheses (draw permutation/prefix/interleave,
  encounter suffix ×3 vs brute-force chain enumeration, relic remainder
  ×2, one seed-filtered sanity check on the sole uncoarsened row),
  family-wise α = 1e-3 via Holm over the executed set, fixed sampler
  seeds → deterministic across nights and hosts (GCC and clang-cl
  byte-identical p-values). Three support-complete mutants rejected at
  p ≤ 1.4e-130 through the identical statistic path at full N. Smoke
  subset (N/10, K=8) in per-commit ctest; nightly entry
  `tools/dist_check/sampler_dist.sh` + `.github/workflows/nightly.yml`
  (07:00 UTC + workflow_dispatch). Six presets green. The ≥ 3
  consecutive scheduled nightly runs remain OPEN — carried in Deferred
  obligations (GT0 owns checking it; schedules only fire on master, so
  force the first with workflow_dispatch).

- **T0.7** `[x]` ∥ **`public_hash` + omniscient boundary.** xxh3 over
  `PublicView`+mask bytes; raw-state access for the omniscient/debug agent
  renamed to a grep-enforceable distinct spelling; a repo check (extending
  `tools/check_stale_counts.sh`-style scripting) fails if training-facing
  code paths reference the omniscient spelling.
  **Deps:** T0.2 **Acceptance:** hash stability test (twin states hash
  equal, public-differing states hash unequal); boundary grep check runs in
  CI and fires on a negative test; presets green. **Log:** 2026-08-03 —
  landed. `public_hash` (xxh3 over the full 6032-B padding-assigned
  `PublicView`, mask structurally included; dirty-buffer test pins
  byte-determinism) in `public_hash.cpp`. Omniscient spelling applied at
  the only two real definition sites: `omniscient_observation.hpp`
  (`omniscient_encode_observation` / `OmniscientObsBuffer`) and
  `StepResult::omniscient_obs`; batch `advance()` untouched (legitimate
  engine API). `tools/check_omniscient_boundary.sh` scans pre-declared
  training trees + a public-side denylist (missing denylist file = hard
  error), `omniscient-boundary-ok` line hatch, third step of the
  `stale-numbers` CI job; durable negative test via committed fixture
  dirs. Six presets green.

### GT0 `[ ]` **Gate: sim-side information layer (schemas + leak gates — the sim half of M6)** — tag `gt0-info-layer`
**Deps:** T0.1–T0.7
- [ ] All T0 acceptance blocks re-run green at the gate.
- [ ] Twin fixtures + `PublicView`/mask schema docs published for the
      training repo.
- [ ] CLAUDE.md "Current state" updated.
**Log:** —

---

## Phase TE — Engine-track coordination (this repo; ∥ with T0)

- **TE.1** `[x]` ∥ **Survival-biased campaign drivers.** Promote the B5.1
  heuristic policies into oracle-campaign drivers: a scripted
  survival-biased policy (block-aware, potion-using, elite-avoiding)
  driving campaign cohorts that reach late-Act-1 floors, boss fights, and
  boss rewards — the coverage the random-policy campaign measurably cannot
  produce ([handoff-2026-07-30.md](handoff-2026-07-30.md)). Hook: campaign
  harness accepts a policy binary/config as the action source.
  **Deps:** — (coordinates with, never blocks, the open G7 gate)
  **Acceptance:** a 500-run campaign cohort reports ≥ 30 % boss-fight
  reach and ≥ 10 boss-reward claims; cohort report lands beside the
  existing campaign artifacts; zero un-triaged diffs or diffs triaged per
  the Stage B process. **Log:** 2026-08-03 — landed. `campaign_driver.py`
  b1.6.0: `--policy external --policy-cmd` (STS-POLICY-IO v1, line-JSON,
  SHA-256-pinned into campaign identity); `survival_policy_cmd.py`
  promotes the B5.1 survival heuristic (byte-identical to
  `--policy greedy` under empty config) — the same seam GT2 routes agent
  checkpoints through. 500-seed un-pre-scanned cohort
  (STS420000–420499): boss-fight reach 155/500 = 31.0 % (bar 30),
  35 boss-reward claims across all three registry bosses (bar 10),
  0 failed seeds, zero untriaged: 110 auto-shape-checked standing
  (Looter/Fairy via new `standing_triage.py`), 2 Smoke-Bomb
  escape-window races per precedent, 1 open product divergence
  promoted to a reproducer — Sharp Hide THORNS on the killing blow
  (see the stage-b deferred-obligations row this task added). Evidence:
  [verification/te1-survival-cohort.md](verification/te1-survival-cohort.md).

- **TE.2** `[x]` **Open S2 planning.** A fresh planning exercise per the
  G7 closing action, pulled forward per plan §4.4: S2 scope doc (Acts 2–3
  content inventory, boss chest/act transition, boss-relic pick, A20
  double boss) and the S2 verification-gate design — coverage-cohort-based
  and satisfiable by TE.1-class scripted drivers, not raw action volume.
  Registry authoring may begin under it immediately (append-only ids make
  early authoring safe). Also updates
  [stage-b-tasks.md](stage-b-tasks.md)'s G7 "Then:" line to record the S2
  scope exercise as satisfied by this task, so a G7-closing agent does not
  re-open a duplicate (Stage C planning stays with G7, unclaimed here).
  **Deps:** — (capacity-gated behind G7 + T0.x per plan §4.4)
  **Acceptance:** S2 design doc + its own task ledger reviewed and landed
  with the scope inventory complete against the base game's Act 2–3
  content lists; G7 "Then:" cross-reference updated; first registry
  authoring wave dispatched under it. **Log:** 2026-08-04 — landed.
  [s2-design.md](s2-design.md) (scope: 40 encounter rows, 37 monster
  classes, 20 event list rows, ~10 relic + 5 card + 0 potion rows, run
  layer incl. cardRng counter snap at act transition and RNG-free boss
  chest; verification: S2-G1 content gate + S2-G2 coverage-cohort gate
  built on the TE.1 STS-POLICY-IO seam) + [s2-tasks.md](s2-tasks.md)
  (Phases S2.0–S2.4, local gate namespace, Wave-1 id blocks granted,
  7 UNVERIFIED markers each owned by a ledger row). G7 "Then:" updated;
  Wave 1 (S2.01–S2.04) dispatched by the orchestrator at landing.

---

## Phase T1 — Training repo bootstrap (Gate GT1)

- **T1.1** `[ ]` **Training repo scaffold.** New repo; engine consumed as a
  pinned submodule/subtree with the six-preset CMake flow; CI (build +
  engine fixture replay + unit tests); its own conventions/ledger docs
  seeded from this file's protocol; version-stamp plumbing (sim commit,
  SCHEMA_VERSION, registry manifest hash, PublicView version, weights
  version, search config id).
  **Deps:** — **Acceptance:** CI green on a hello-world actor that steps a
  batch of `RunController`s through `advance` and hashes states
  byte-identically to a committed fixture. **Log:** —

- **T1.2** `[ ]` ∥ **Trajectory schema + storage.** Fixed-layout POD
  records (public obs, mask, sparse search distribution, action, outcome,
  aux targets) in append-only memory-mapped shards; refuse-on-mismatch
  loaders; **restricted sidecar** as keyframes + action logs with
  reconstruction-by-replay verified; quarantine = metadata filter by
  sim-commit range.
  **Deps:** T1.1, GT0 **Acceptance:** write→read round-trip; loader
  refuses a stamped-incompatible shard (negative test); a sampled
  intermediate state reconstructs bit-exactly from keyframe + action log;
  sidecar bytes/run measured and recorded. **Log:** —

- **T1.3** `[ ]` ∥ **Actor throughput spike.** Tiny net + real Gumbel
  root / PUCT in-tree search + `resample_hidden` particles + GPU batching
  (fp16, pinned, double-buffered), on Act-1 combat snapshots. Sole job:
  **measure R** (achieved NN evals/s at production batch) and **t_enc**,
  publish the per-decision time breakdown (encode/step/copy/tree/NN-wait),
  and sweep the search configuration (total evals, candidates, halving
  schedule, world sampling) per plan §3.2. Uses the mask-supplied `advance`
  overload.
  **Deps:** T1.1, T0.1, T0.4, T0.7 (real PublicView, sampler, and
  public_hash — the spike exercises the true actor path, and T0.7 pulls in
  T0.2 for honest full-view t_enc)
  **Acceptance:** a committed numbers doc: R at ≥ 3 batch sizes and ≥ 2 net
  widths, t_enc, batch-fill and queue-wait histograms, and the plan §5
  budget numbers re-derived from measured values; sweep-selected search
  config recorded as the Phase T2 default. **Log:** —

- **T1.4** `[ ]` ∥ **Dataset ingestion + tabular V0.** Acquire the public
  run dataset; verify `seed_played` and player-identifier availability
  (recording the answer — it gates split design); filters (character,
  ascension, patch era); the **minimal tabular V0** P(win | floor, HP,
  deck, relics, gold) with calibration report; per-fight `damage_taken`
  extraction pipeline for later encounter-head pretraining.
  **Deps:** T1.1 **Acceptance:** ingestion is deterministic/re-runnable;
  V0 calibration (reliability diagram + Brier) on a held-out player- or
  run-disjoint split, choice recorded; V0 additionally validated against
  sim-rollout outcomes on ≥ 20 reconstructed floor-boundary states with
  the vintage-bias delta recorded (plan §4.2); damage-record table row
  counts and schema documented. **Log:** —

- **T1.5** `[ ]` ∥ **Eval harness + decision suite v0.** Three seed
  populations provisioned (dev / frozen paired-validation /
  untouched-holdout, generation procedure and the holdout's rotation
  trigger documented); paired runner with
  McNemar/bootstrap reporting; decision-suite v0 = exactly-solvable
  micro-combats with ground-truth optimal values (hard gate class); the
  versioned-label class scaffolding (labels re-derived on champion
  upgrade, agreement-trend gating) per plan §6.
  **Deps:** T1.1, T1.2 **Acceptance:** harness runs end-to-end on the
  random policy + one scripted policy and emits the paired report; suite
  scoring reproduces ground truth for an exact-search reference agent.
  **Log:** —

- **T1.6** `[ ]` **Training-side leak gates.** Policy-logit invariance and
  search-statistic invariance across GT0 twin fixtures (pinned sampler
  seed); the probe gate — hidden-fact prediction from
  observations/embeddings at **reference-predictor parity** (reference =
  belief-marginal predictor), wired as a promotion gate not CI.
  **Deps:** GT0, T1.3 **Acceptance:** invariance tests green on the spike
  net; probe harness demonstrably detects a deliberately-leaked
  observation (negative control) and passes on the clean encoder. **Log:** —

### GT1 `[ ]` **Gate: trainer contract live (completes InitialPlan M6) — no durable training before this**
**Deps:** T1.1–T1.6
(M7 deliberately maps to no gate: E1 is demoted from gate to accelerant per
plan §8 delta 2; its surviving pieces are T1.4/T3.1/T3.2.)
- [ ] Leak gates green (T0.5, T0.6, T1.6).
- [ ] R and t_enc measured; budget table re-derived (T1.3).
- [ ] V0 shipped with calibration report (T1.4).
- [ ] Eval harness + seed populations frozen (T1.5).
**Log:** —

---

## Phase T2 — Act-1 combat expert iteration (Gate GT2; ∥ with S2 engine work)

- **T2.1** `[ ]` ∥ **Snapshot bank.** Reachable-state harvesting from
  survival-biased policies (the four landed B5.1 E0 heuristics suffice for
  the first bank; TE.1's campaign drivers and, later, agent checkpoints
  improve it — TE.1 is deliberately not a dep), stratified by
  floor/deck-archetype/HP; branch-K memcpy reset tooling; bank format
  versioned with the trajectory schema.
  **Deps:** GT1 **Acceptance:** bank of ≥ 100k snapshots with the
  stratification report: ≥ 20 % of snapshots from floors 8+ and every
  RunPhase represented (vs the random-policy baseline's 97.6 % floor-1–7
  mass); reload + twin-test spot check green on ≥ 1,000 sampled
  snapshots. **Log:** —

- **T2.2** `[ ]` **Combat ExIt loop v1.** From-scratch expert iteration on
  the bank: teacher search at high budget → distill policy + value —
  including the **distributional exit-HP/death value heads and the
  last-layer value ensemble** (plan §3.3 i–ii; the ensemble ships here,
  its LCB *use* ships in T3.4); exploration kit explicit (Gumbel root
  sampling, action-sampling temperature schedule, replay freshness
  targets); leaf currency V0; micro-combat ground-truth suite runs every
  generation. Production-loop plumbing per plan §5 lands here: atomic
  weight hot-swap, day-one telemetry (learner ingest vs actor production,
  steps-per-run per weights version), and ≥ 1 permanent debug-preset
  worker.
  **Deps:** T2.1, T1.3 **Acceptance:** on the frozen combat suite, paired:
  search > direct policy > scripted baselines at p < 0.01, and the
  distilled student retains ≥ 60 % of the paired search gain (thresholds
  pre-registered **here, before dispatch**; tighten only via a change-log
  entry); no micro-combat ground-truth regressions; telemetry counters
  demonstrably live in a generation run. **Log:** —

- **T2.3** `[ ]` **Currency machinery + V1.** Versioned value-artifact
  registry; V1 re-fit on self-play Act-1 outcomes (bootstrapped horizon);
  the reanalyze-vs-quarantine lifecycle implemented as a shard-metadata
  operation; V0→V1 delta report (labeled a coarse signal per plan §4.1).
  **Deps:** T2.2 **Acceptance:** re-distillation against V1 completes with
  the delta report committed; lifecycle op demonstrated on a real shard
  set (reanalyzed targets refresh; quarantined range excluded from the
  next training run's manifest). **Log:** —

- **T2.4** `[ ]` ∥ **Branch-K counterfactual tooling.** Common-random-
  number paired branching at macro decisions (card reward take-vs-skip,
  shop buy-vs-pass, Neow options): identical sampled worlds across
  branches, per-decision advantage estimates into the trajectory schema
  (T2.4 owns this record-type addition as a versioned schema bump; T2.2
  consumes it behind the refuse-on-mismatch loaders); measured
  variance-reduction report vs unpaired estimates.
  **Deps:** T2.1 **Acceptance:** on ≥ 1k paired card-reward branches, the
  paired estimator's variance reduction vs unpaired is measured and
  reported; records land in the schema and are consumed by a smoke
  training run. **Log:** —

### GT2 `[ ]` **Gate: combat agent (E2 / M8-equivalent)**
**Deps:** T2.2, T2.3
- [ ] T2.2 paired metrics hold on the frozen suite at the declared budget.
- [ ] HP-bucketed value calibration report sane (plan §3.3).
- [ ] A checkpoint routed through the TE.1 campaign harness as an
      oracle-campaign driver (training output becomes verification input).
- [ ] The weekly three-tier report cadence (plan §6 item 4) starts at this
      gate.
**Log:** —

---

## Phase T3 — Act-1 integrated agent (Gate GT3 = M9-equivalent)

- **T3.1** `[ ]` ∥ **Macro imitation heads.** Card-pick / path / Neow /
  shop / event heads pretrained on the dataset (wins and losses,
  patch-era weighting); held-out action-likelihood report.
  **Deps:** GT1 (T1.4) **Acceptance:** held-out likelihood beats a
  frequency baseline per head; qualitative screen review of top
  disagreements committed. **Log:** —

- **T3.2** `[ ]` ∥ **Encounter-outcome pretraining.** Per-upcoming-fight
  outcome heads (damage taken, turns, death) pretrained on the per-fight
  `damage_taken` records; validated against sim rollouts on
  reconstructible floor-boundary states (plan §4.2).
  **Deps:** GT1 (T1.4) **Acceptance:** held-out prediction beats
  per-encounter-mean baseline; sim-rollout validation report quantifies
  patch/skill bias on ≥ 20 reconstructed states. **Log:** —

- **T3.3** `[ ]` ∥ **Run-level planner.** Sparse expectimax over the
  visible act-map DAG using public reveal distributions with V at leaves;
  shop/removal small sequence search; combat-search invocation policy for
  imminent fights; adaptive budget rules (plan §3.2).
  **Deps:** GT1 **Acceptance:** on constructed map scenarios with known
  optimal routes under a fixed V, the planner recovers them; budget
  telemetry (evals per decision by phase) emitted. **Log:** —

- **T3.4** `[ ]` **Act-1 full-run training loop.** Bootstrapped-horizon
  full-run generation (exit states valued by V1) integrating T3.1–T3.3 +
  the T2 combat agent; **distributional value backup and epistemic-only
  ensemble-LCB at irreversible run decisions** (elite entry, event HP
  payments, Neow gambles — plan §3.3 i–ii), ablation-gated with the LCB
  coefficient tuned on paired win rate; behavioral dashboards (death
  floor/cause, elite count, deck stats, potion economy, calibration).
  **Deps:** T2.3, T3.1, T3.2, T3.3 **Acceptance:** paired improvement over
  the untrained assembled baseline on the frozen validation population at
  the declared budget; LCB ablation report committed (paired win rate must
  not drop with LCB on, else it ships disabled); dashboards live; an
  oracle-replayed sample of runs zero-diff. **Log:** —

- **T3.5** `[ ]` ∥ **Decision suite v1.** Populate the versioned-label
  suite classes beyond T1.5's micro-combats: ~10k stratified snapshots
  across lethal puzzles, path forks, shops, Neow, event choices, and
  elite-entry HP states, harvested from the T2.1 bank and live
  trajectories; labels from mega-budget search under the current champion,
  re-derived on champion upgrade per plan §6.
  **Deps:** T2.1 **Acceptance:** per-category snapshot counts committed
  (≥ 1k per category); label-refresh machinery demonstrated on one
  champion bump; agreement-trend gating wired into the promotion
  ladder. **Log:** —

### GT3 `[ ]` **Gate: integrated Act-1 agent (M9-equivalent)**
**Deps:** T3.4, T3.5
- [ ] T3.4 paired improvement + zero-diff sample re-verified at the gate.
- [ ] T3.4's LCB ablation decision recorded (on with measured coefficient,
      or off with the report showing why).
- [ ] Decision-suite versioned-label class refreshed from the new champion.
**Log:** —

---

## Phase T4 — Acts 2–3 extension (Gate GT4 = M10-equivalent)

**Blocked on:** engine S2 verified (its own ledger, per TE.2).

- **T4.1** `[ ]` **PublicView S2 extension.** Populate the reserved fields
  (boss-relic screen, act index, double-boss); additive-compatibility
  check: S1-era shards/checkpoints still load. **Modifies the simulator
  repo: yes** — the PublicView/leak-suite work lands there under full
  conventions, in its own worktree (explicit exception to the two-repo
  rule).
  **Deps:** S2, GT0 **Acceptance:** twin + tripwire suites green over S2
  phases; an S1-era shard loads and trains under the extended schema.
  **Log:** —

- **T4.2** `[ ]` **V2 re-fit + pre-registered re-baseline.** V2 on real
  Acts 1–3 continuations; the pre-registered T3-result re-baseline with
  the retrain-from-scratch threshold (plan §4.1); combat re-distillation
  against V2.
  **Deps:** T4.1, GT3 **Acceptance:** re-baseline report committed;
  threshold decision executed and recorded; calibration gates green.
  **Log:** —

### GT4 `[ ]` **Gate: Acts 1–3 agent through the A20 double boss**
**Deps:** T4.2
**Log:** —

---

## Phase T5 — A20H headline (Gate GT5 = M11-equivalent)

**Blocked on:** engine S3 verified (keys, Act 4, Heart).

- **T5.1** `[ ]` **Act-4/keys/Heart integration + V3.** Key-state planning
  surfaces through the existing observation fields; V3 = true terminal
  Heart-kill signal; bootstrapped-horizon machinery removed. **Modifies
  the simulator repo: yes** — S3-phase leak/twin-suite extensions land
  there under full conventions (explicit exception to the two-repo rule).
  **Deps:** S3, GT4 **Acceptance:** end-to-end A20H runs generate and
  train; leak/twin suites green over S3 phases. **Log:** —

- **T5.2** `[ ]` **Full flywheel.** Teacher-search allocation by
  uncertainty/impact (lethal states, ensemble disagreement, rare
  mechanics, deaths); reanalysis of stored trajectories under the newest
  net; failure mining into permanent tactical cases; oracle-audit path for
  exploit-suspicious high-value lines.
  **Deps:** T5.1 **Acceptance:** each flywheel component demonstrated on
  live generation with its telemetry; ≥ 1 mined-failure case and ≥ 1
  oracle audit completed end-to-end. **Log:** —

- **T5.3** `[ ]` **Headline evaluation.** Untouched-seed A20H win-rate
  report at declared budgets, three tiers (policy-only / standard /
  max-search), with compute per decision, calibration, and behavioral
  dashboards; promotion per plan §6.
  **Deps:** T5.2 **Acceptance:** the report exists with paired confidence
  intervals on the untouched population and a zero-diff real-client replay
  sample. **Log:** —

### GT5 `[ ]` **Gate: A20H headline result (M11-equivalent)**
**Deps:** T5.3
Then: open Phase T6 (other characters) as a fresh planning exercise if
desired.
**Log:** —

---

## Change log

- 2026-08-01 — ledger created (proposed, uncommitted) with Phases T0–T5,
  TE, and gates GT0–GT5; revised same-day per two-agent audit (T1.3 deps
  repaired; KnowledgeState→PublicView projection moved into T0.2; plan
  §3.3/§4.2/§5/§6 deliverables assigned to T1.4/T2.2/T3.4/T3.5; GT0/GT1
  M6 split corrected; TE.2 G7 cross-reference added; two-repo exceptions
  made explicit on T4.1/T5.1).
- 2026-08-01 — landed. `check_stale_counts.sh` exemptions generalized to
  `docs/*-tasks.md` ledgers and `docs/*-log.md` archives (protocol bullet
  discharged); stage-b's G7 Log now points at the campaign handoff and
  records TE.2's ownership of the S2 scope exercise.
