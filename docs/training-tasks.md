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
- **Capacity rule (plan §4.4):** sim-repo agent capacity was prioritized
  **G7 close → T0.x → TE.2 (S2 authoring)** while those were open (all
  three are `[x]`). **Since 2026-09-03 (S2-G2 taken):** S2.V3 residue
  closure (fixes already in hand) → the training-repo unblockers (engine
  pin bump, T1.2) → S3 planning (its own ledger once it lands) → the
  T1.4s value-bootstrap accelerant → TE.3. The plan's §4.4 accelerant
  clause is now load-bearing in the other direction: S3 verification at
  depth is expected to use the trained Act-1–3 agent as its driver, so
  training-first is also headline-first.
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
| Engine CMake uses `CMAKE_SOURCE_DIR` in 18 places, so `add_subdirectory` consumption is impossible (SpireTrainer must use ExternalProject: coarse 1-entry engine ctest, duplicate gtest fetch) | T1.1 | **DISCHARGED 2026-09-02 (sim side)** — `build: PROJECT_SOURCE_DIR everywhere, so the engine embeds via add_subdirectory` | Every repo-owned `${CMAKE_SOURCE_DIR}` in the seven build files became `${PROJECT_SOURCE_DIR}` (only `./CMakeLists.txt` calls `project()`, so it is the engine root embedded or not); the quiet failure mode was `target_include_directories(sts_engine PUBLIC ${CMAKE_SOURCE_DIR}/include)` exporting the CONSUMER's headers. `CMAKE_RUNTIME_OUTPUT_DIRECTORY` on WIN32 stays `CMAKE_BINARY_DIR` deliberately — googletest hard-codes that bin/ as a target property, and pointing our tests elsewhere cost every one of them a `0xc0000135` (observed, then documented in conventions §8). Nothing needed a `PROJECT_IS_TOP_LEVEL` guard. **Evidence:** `tools/check_embed_consumer.sh` (new, hand-run) built a throwaway `add_subdirectory` consumer on the **Windows/clang-cl host** — embedded build clean, `embed_smoke` linked and ran, and `ctest -N | tail -1` in the CONSUMER's build tree reported `Total Tests: 2699`, i.e. the engine's suites arrive as per-test entries, not one opaque entry; its `#error` decoy header was verified as a negative control by temporarily restoring the old spelling. `win-debug` configure+build+ctest fully green on the final tree. **Training side DISCHARGED 2026-09-03 (T1.1b, SpireTrainer `33a99a0`):** pin `bfd95a2` → `6c50a0b`, `ExternalProject_Add` replaced by `add_subdirectory`, `sts_engine` a real target, googletest fetched once |
| Sampler distributional suite green on ≥ 3 consecutive *scheduled* nightly runs (local 3× stability + cross-host determinism proven at landing; schedules fire only on master — force run 1 via workflow_dispatch) | T0.6 | GT0 gate check | `.github/workflows/nightly.yml` → `tools/dist_check/sampler_dist.sh`; record the three run URLs/dates here when observed, then mark DISCHARGED. **Re-owned at the GT0 gate (2026-08-04) and still OPEN** — the gate re-ran the suite 3× locally in nightly mode with byte-identical p-values, which is everything short of the scheduled runs themselves |
| Stored records carry `outcome_kind = kOpen` and zeroed outcome/value/aux targets — an append-only writer cannot go back once a run ends | T1.2 | T2.3 (**narrowed 2026-09-03 by T1.4s**) | Filling them is a read-old-shard / write-new-shard pass, which is exactly the shape of T2.3's **reanalyze** operation, so T1.2 deliberately did not half-build it. `RecordedRunStats::outcome_kind` carries the answer for a caller that wants it immediately. A loader must never read `outcome_return` from a `kOpen` record as if it were a target. **T1.4s discharges this for OFFLINE producers** (`floor_rollout.hpp`): a run's rows are held in memory until the run terminates, stamped with the outcome block and `value_target` there, and only then appended — nothing on disk is rewritten and the buffer is bounded by one run's floor count. What remains for T2.3 is the ONLINE case, an actor that must publish rows before its run ends, which is the only one that genuinely needs a rewrite pass. `outcome_return` is still 0 in T1.4s's shards, and correctly so: `weights_version` is `none`, i.e. no currency is named. |
| Quarantine has no **committed** `CommitOrder` — the ordered list of sim commits this repo has ever pinned | T1.2 | T2.3 | Git shas are unordered, so "the range from A to B" is only evaluable against a declared order (`quarantine.hpp`). T1.2 demonstrated the filter with an order built in the tool; the lifecycle operation needs one in the repo, appended by the same reviewed change that moves the pin (conventions, "Moving the engine pin"). Until it exists, every real shard is `kUnknownCommit` to any policy but a hand-built one. |
| The keyframe interval (default 64) is unswept, and `SidecarReader::reconstruct` linear-scans both sidecar streams | T1.2 | **DISCHARGED 2026-09-03 (T2.1)** | Both halves. *Interval:* `bank_check` sweeps 8/16/32/64/128/256/512 over real SIM_SEARCH runs and measures sidecar bytes/run against actions replayed per reconstruction (`SpireTrainer/docs/verification/t2-1-snapshot-bank.md` (training repo) §4); the decision is to KEEP 64, and the measurement is the point -- the byte curve does NOT flatten past 64 (64 -> 128 saves ~39 %, 128 -> 256 another ~34 %), so it was a real trade. It goes to 64 because T2.1 gave the program a bank, which is now the durable random-access surface, leaving the sidecar's latency to matter more than its bytes; 256 is recorded as the measured sweet spot if volume ever binds. `sidecar.hpp` carries the argument at the constant. *Index:* `include/sts/training/stream_index.hpp` -- `RecordKind::kStreamIndex`, ONE entry per (run, stream) rather than one per record (a per-record index would be 32 B against a 16 B action record). `SidecarWriter::finish` writes one per stream, `SidecarReader` uses them when present and scans when absent, and §4 measures both paths reconstructing identical states (12.7x, then 6.0x on a larger timed set). |
| The value-artifact registry has no CHECKER that a registered `sha256` still matches the file it names | T1.4s | T2.3 | `artifacts/value-artifacts.json` records each artifact's digest, and `v0s_fit.py` writes it — but nothing re-verifies it on the way in. A registry whose hashes are never checked is a comment. T2.3 owns the registry's lifecycle (promote / retire / reanalyze against a champion), so the guard belongs in the same change as the first operation that reads an entry it did not write. Until then, `python -c "import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())"` is the manual check. |
| `v0s.1` has only ever seen FLOOR-BOUNDARY states, so a search that queries it at a mid-combat leaf is extrapolating | T1.4s | **MEASURED 2026-09-03 (T1.7); the decision it informs is T2.2's** | The corpus is one row per floor, by construction. T1.7 is the first consumer that could measure the gap and did: it queries V0s at every searched decision *and* at the combat exit, and reports both (`SpireTrainer/docs/verification/t1-7-tracer-bullet.md` (training repo)). Over 4,096 episodes/generation the mid-combat mean is **0.3946** against exit means of 0.2400 / 0.2709 / 0.2731, i.e. a mean absolute gap of **0.161 → 0.131 → 0.129** — large relative to the target's own spread (sd ≈ 0.17), and *systematically optimistic*: a mid-combat state still holds the HP it is about to lose, so the floor-boundary table reads it as a healthier floor-boundary state than the one the combat actually exits into. T1.7 therefore does NOT query V0s at a search leaf at all — the leaf value is the network's own head, and V0s is used once per episode, at the exit, which is the plan's E[V(exit RunState)] and is in-distribution. What is left for **T2.2** is the decision the number now supports: either keep that arrangement, or generate a combat-state corpus (a one-line change to the boundary test in `roll_floor_rows`) and fit a V0 that is honest at a mid-combat leaf. |
| The V0s → V0h comparison is PRE-REGISTERED but not run | T1.4s | T1.4 | The protocol — features, target re-labelling, split, the paired-bootstrap statistic, and the four registered deltas with what each triggers — is section 9 of `SpireTrainer/docs/verification/t1-4s-v0s.md` (training repo), written before the human dump exists so the comparison cannot be designed around its result. T1.4 runs it. If the dump cannot support the bootstrapped-horizon re-labelling, the protocol says to declare the comparison impossible and record that, not to weaken it. |
| `engine::RunController` is not portable between PROCESSES: `RunController::lists` holds three `std::array<std::string_view, N>` into the registry's static strings, so a state memcpy'd to disk carries pointers | T2.1 | engine repo (**surfaced to the orchestrator**) | T2.1's bank is the first artifact this program writes in one process and reads in another, and the first `bank_check` against a real bank SEGFAULTED in `encode_public_view` -> `encode_prefix` dereferencing a stored view; it is also a determinism defect, since ASLR would make a shard's bytes a function of where the generator was loaded. T2.1 fixed it CONSUMER-SIDE: a bank record stores the three lists as registry `EncounterDef` ids, zeroes the views in the stored state, and `bank_restore_state` rebinds them on the way in (`bank_capture_lists` refuses, naming the key, if an entry is not a registry encounter). **T1.2's `SidecarKeyframe` has the same flaw and is NOT fixed** -- nothing reconstructs a sidecar across processes today, so it is latent rather than live. The durable fix is in the engine (ids in `MonsterLists`, resolved at use) rather than a second copy of the workaround, which makes it an engine-repo change and therefore stop-and-surface under conventions §6. Full write-up: `SpireTrainer/docs/verification/t2-1-snapshot-bank.md` (training repo) §8. |
| The v0 observation tensorization is COMBAT-ONLY and capped: no map DAG, no per-phase screen sections, no run trunk, and every entity group truncates (measured 120 dropped entities over the 256-snapshot bank) | T1.3 | T2.2 | `pv_encode.hpp` is a v0 whose job was to make `t_enc` a measured number and give the spike a real entity-set input, not to be the Phase-T2 token layout. Plan §3.1's run trunk (deck/relic/potion tokens, a map-DAG encoder, current-screen tokens) has no encoder at all yet, and the spike could not need one because it searches inside combat. `EncodedObs::tokens_dropped` / `actions_dropped` are counted and reported so the truncation is a number rather than a suspicion; the policy head's 64-action cap dropped NOTHING on this bank (mean root fanout 7.15) but `sts::fuzz::kMoveCap` is 163, so a run-layer action set will exceed it. |
| The search is combat-scoped: potions and every run-layer decision are outside the searched action set | T1.3 | T2.2 | `Search` steps `rc.combat` with the COMBAT-level `advance` (the overload that takes the mask, advance.hpp:319-323). `USE_POTION` legality lives on `RunActionMask` and that overload does not dispatch it at all, so potions are simply not searchable at this layer. Plan §3.2's run level is structured expectimax over the act map, which is a different search and a later phase; what T2.2 inherits is the combat one plus the knowledge that adding potions means either a run-level mask-supplied overload (an ENGINE change) or a hybrid step. |
| The GPU inference path runs on the DEFAULT CUDA stream: no two-stream copy/compute overlap, because there is no CUDA toolkit | T1.3 | T2.2 | `<c10/cuda/CUDAStream.h>` and `<ATen/cuda/CUDAEvent.h>` include `cuda_runtime_api.h`, a CUDA TOOLKIT header; the box has the driver and LibTorch's bundled runtime DLLs and no toolkit, and T1.3's download allowlist does not cover NVIDIA's site. `nn.cpp` therefore double-buffers host STAGING against device execution and waits with `torch::cuda::synchronize()`. Measured, that costs little today — 3.55 ms in `begin_batch` against 0.02 ms in `end_batch`, i.e. the host side is ~170x the device wait — but the same measurement is what makes the actor LAUNCH-BOUND, and the fix for that (CUDA graphs, or TensorRT) needs the toolkit too. |
| The Phase-T2 default search config was selected STRUCTURALLY, not on decision quality | T1.3 | T2.2 | The spike's weights are random, so no row of its sweep says anything about which configuration plays better; T1.3 says so in the doc and picks on the plan's own commitments plus measured root fanout. The axes ARE swept and their COSTS measured (16/48/128 evals, 4/8/16 candidates, Gumbel-SH vs PUCT at the root, reveal coarsening on/off, per-simulation vs 8/32-world banks), so T2.2 re-runs the same grid against a trained net and changes `STS_TRAIN_SEARCH_CONFIG_ID` if the quality ordering disagrees. |
| A shard's `weights_version` is fixed at BUILD time, so an ONLINE actor that hot-swaps weights cannot label which generation produced a record — and if it did, its own build's reader would refuse the shard | T1.7 | T2.3 | `make_shard_header` fills all six stamp fields from `version_stamp()`, and `weights_version` is a CMake cache variable baked into a generated header at configure time; `ShardReader::open` then compares it **exactly** against the running build's stamp. T1.7 swapped the module three times inside one process, so three generations of shards produced by three demonstrably different nets (sha256 `102e35bb…`, `7b42a12d…`, `87de1213…`) all carry `weights_version = none`. Demonstrated rather than argued: a copy of generation 0's shard with that header field patched to `tracer.g1` is refused by name (`weights_version_mismatch: shard 'tracer.g1' != manifest 'none'`, exit 2) by the same reader that accepts the unpatched file. T1.7's workaround is to record the weights path + sha256 in the per-generation `manifest.json`, so the provenance sits beside the shard rather than inside it. The real fix — a runtime-settable weights identity in the header, and a loader that compares it against a POLICY rather than against its own build — is the versioned-artifact lifecycle T2.3 owns, and it is the same shape as quarantine's `CommitOrder` row above. |
| The trajectory shard carries no TENSORS, so an online actor and its learner must agree on a second file format out of band | T1.7 | T2.2 | `DecisionRecord` stores the `PublicView` and the sparse search distribution; the learner needs the tokenized observation and the per-slot action encoding, which the record has no room for and which re-deriving in Python would duplicate `pv_encode.cpp`. T1.7 wrote a tracer-local companion (`observations.stsobs`, one record per decision, joined by (run_id, step_index), layout published in the generation's `manifest.json`) rather than append a `RecordKind` to the durable container for a throwaway loop. T2.2 has to decide which it is: a fifth record kind in the shard container, or a declared sidecar format with its own header and refusal path. Two facts from T1.7 bear on it — the companion is ~5.3 KB against the record's ~13.9 KB, and the join it forces (sparse `action_bits` → dense slot) is where the `Action{0}` sentinel defect surfaced, so the join is worth keeping *somewhere* as a check even if the storage merges. |

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

### GT0 `[x]` **Gate: sim-side information layer (schemas + leak gates — the sim half of M6)** — tag `gt0-info-layer`
**Deps:** T0.1–T0.7
- [x] All T0 acceptance blocks re-run green at the gate.
- [x] Twin fixtures + `PublicView`/mask schema docs published for the
      training repo.
- [x] CLAUDE.md "Current state" updated.
**Log:** 2026-08-04 — closed. Every T0.1–T0.7 acceptance re-run on the
integrated tree (base `e7512c8`), not carried forward from the task Logs:
six presets configure/build/`ctest` green (`ctest -N` for the count, same
total under GCC and clang-cl); `public_view_test`, `knowledge_test` +
`RegistryGen.UnknownObservabilityValueFailsWithClearError`, `resample_test`
incl. `SamplerPoisonedSeedCanary.ParticleNeverTouchesTheTrueSeedOrItsStreams`,
`twin_test` (sweep 10,852 states over 9 phases, per-phase counts asserted,
zero leaks) + `tripwire_test` (4 negative controls fire) + `twin_fixture_test`
(recipe replay + write/read round-trip + both refusal tests), `public_hash_test`,
`omniscient_boundary_test`; all three check scripts clean. Sampler suite run
3× in NIGHTLY mode via `tools/dist_check/sampler_dist.sh` — byte-identical
p-values across all three, nine pre-registered hypotheses retained under Holm,
three mutants rejected. **No acceptance failed to reproduce.** Published
[training-contract.md](training-contract.md) (the consumer-facing contract:
`PUBLIC_VIEW_VERSION` 2, layout + how to read the audit, `PvMask` embedding,
`public_hash` and its soundness precondition, the omniscient-boundary rule and
the check's contract, the twin-fixture container by reference, the
`KnowledgeState` projection and all four declared §2.2/§2.6d coarsenings, and
the audit-§9a mask leak with its canary test name); evidence in
[verification/gt0-info-layer.md](verification/gt0-info-layer.md); CLAUDE.md
"Current state" updated. **Sole open residue, explicitly re-owned by this
gate and NOT discharged:** T0.6's ≥ 3 consecutive *scheduled* nightly runs —
the Deferred-obligations row was verified present, and stays open until the
three run URLs/dates are recorded there (schedules fire only on master, so
force run 1 with `workflow_dispatch` after this lands).

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

- **TE.3** `[ ]` ∥ **Handicap-assisted deep-state generator.** *(Priority
  note 2026-09-03: behind T1.4s/T2.1 — the Act-1 bank comes from
  `SIM_SEARCH`, which kills the Act-1 boss on ~37 % of A20 seeds; TE.3 is
  needed when Act-2/3 strata are, i.e. before T4, not before T2.)* Sim-side
  driver that reaches Act 2–3 states cheaply for the T2.1 snapshot bank
  (and any S2-side coverage use), closing the measured gap that scripted
  policies lose ~×30 per act (S2-G1 soak: `victories = 0`, so unassisted
  harvesting starves the deep strata). Mechanism contract — **assist,
  don't mutate**, with implementation latitude inside it: (a) per-turn
  assist damage enqueued through the effect interpreter as real DAMAGE
  effects (real triggers and death handling; no fake powers, no direct
  monster-HP pokes, no player hp/max-hp edits), and (b) death rescue by
  floor-boundary snapshot + rollback-and-escalate (on player death,
  restore the snapshot, raise the assist level, continue) — so every
  harvested state is a legal engine state with a legitimately-accumulated
  deck/relic/gold/HP progression; the *trajectory* is impossible, the
  *states* are not. Assist schedule deterministic from (seed, config),
  config hash in artifact identity (TE.1 discipline). Every snapshot
  records provenance — assist level at harvest + rolled-back death count —
  so downstream training stratifies/weights/excludes on it (plan §4.3:
  the distribution shift is measured, not assumed). Harvest container
  format documented for the training repo; bank formatting itself stays
  T2.1's. Origin: external idea (Jorbs's handicap-annealing curriculum),
  adopted per the 2026-08-26 plan change-log entry as state generation
  only.
  **Deps:** — (S2-G1 content and the landed E0 policy seam suffice;
  coordinates with, never starves, the open S2.43/S2-G2 work per the
  plan §4.4 capacity ordering)
  **Acceptance:** a cohort of ≥ 500 assisted runs harvests ≥ 10k
  snapshots (floor-boundary and combat) with ≥ 30 % from Act 2+ floors
  and every RunPhase reachable in a three-act run represented, boss chest
  included; ≥ 1,000 sampled snapshots reload, step through `advance`
  under the debug preset, and pass the `make_hidden_twin` spot-check;
  same (seed, config) → byte-identical harvest; reach report (per-act
  reach, floor/deck-size distributions, assist-level histogram,
  rolled-back death counts) vs the TE.1 survival-biased baseline
  committed under `docs/verification/`. **Log:** —

---

## Phase T1 — Training repo bootstrap (Gate GT1)

- **T1.1** `[x]` **Training repo scaffold.** New repo; engine consumed as a
  pinned submodule/subtree with the six-preset CMake flow; CI (build +
  engine fixture replay + unit tests); its own conventions/ledger docs
  seeded from this file's protocol; version-stamp plumbing (sim commit,
  SCHEMA_VERSION, registry manifest hash, PublicView version, weights
  version, search config id).
  **Deps:** — **Acceptance:** CI green on a hello-world actor that steps a
  batch of `RunController`s through `advance` and hashes states
  byte-identically to a committed fixture. **Log:** 2026-08-04 — landed
  as `D:\STS_BG_Mod\SpireTrainer` (commit `85a9639`, local only — remote
  creation pending). Engine = pinned submodule at `gt0-info-layer`
  (`bfd95a2`), pin enforced by `check_submodule_pin.sh` (refuses
  `branch =` entries); six-field VersionStamp (CRLF-normalized registry
  hash — cross-host stable); acceptance fixture: 8 A20 seeds →
  RUN_OVER, run_state_hash + public_hash identical under clang-cl and
  GCC, loader refuses stamp mismatch; six presets green incl. the full
  engine suite via ExternalProject. Boundary check wraps the engine's
  script over the four training trees. **Per the two-repo rule, T1.x
  tracking now lives in `SpireTrainer/docs/training-tasks.md`; this
  ledger keeps the GT1+ cross-repo gates.** Engine-side follow-up filed
  below: `CMAKE_SOURCE_DIR` → `CMAKE_CURRENT_SOURCE_DIR` refactor.
  **2026-09-03 (T1.1b)** — engine pin moved `bfd95a2` (tag `gt0-info-layer`)
  → `6c50a0b` (S2 verified, 118 commits on), and `ExternalProject_Add`
  replaced by `add_subdirectory(external/SpeedTheSpire engine)` in the same
  commit, discharging the deferred obligation above on the consumer side. The
  engine is now one CMake invocation with this project: `sts_engine` is a real
  target (every `add_dependencies(<t> sts_engine_build)` deleted), googletest
  is fetched once instead of twice, and the engine's suites arrive as per-test
  ctest entries rather than one opaque `engine_suite`. `tests/golden/
  actor_smoke_v1.txt` was regenerated — it REFUSED to compare, by design,
  because `SCHEMA_VERSION` 6→8 and `PUBLIC_VIEW_VERSION` 2→6. Every hash
  moved; the finding is that the eight seeds' step counts (120/130/101/68/75/
  78/90/87) and terminal phase (RUN_OVER) are unchanged, so the ladder's
  trajectory is the same and only the hashed bytes differ. **Acceptance (real
  runs, per the owner's 2026-09-03 direction):** all six presets configure +
  build green — win-debug / win-asan / win-release through the vcvars+LLVM
  wrapper, and `debug` / `asan` / `release` through `tools/wsl_run.sh
  --script`; `actor_smoke` run on its eight real A20 seeds under all six
  produces a byte-identical fixture (sha256
  `8654ae010fa349a052ec48742e90bf786f5183c1d240795f15bcf6aa3b19c733` under
  both clang-cl and GCC). `tools/training/check_omniscient_boundary.sh` clean;
  `tools/check_submodule_pin.sh` clean after the commit. (Incidentally, before
  the direction landed: `ctest --preset win-debug` and `win-asan` were 100%
  green over the whole embedded suite — `ctest -N | tail -1` = 2747.)

- **T1.2** `[x]` ∥ **Trajectory schema + storage.** Fixed-layout POD
  records (public obs, mask, sparse search distribution, action, outcome,
  aux targets) in append-only memory-mapped shards; refuse-on-mismatch
  loaders; **restricted sidecar** as keyframes + action logs with
  reconstruction-by-replay verified; quarantine = metadata filter by
  sim-commit range.
  **Deps:** T1.1, GT0 **Acceptance:** write→read round-trip; loader
  refuses a stamped-incompatible shard (negative test); a sampled
  intermediate state reconstructs bit-exactly from keyframe + action log;
  sidecar bytes/run measured and recorded.
  **Inherited:** the version stamp T1.1 built is the shard header's
  identity — all six fields, refuse-on-mismatch, per conventions §6.
  **Log:** 2026-09-03 — landed on engine pin `6c50a0b`.
  `include/sts/training/{trajectory_schema,mapped_file,shard,sidecar,quarantine,trajectory_recorder}.hpp`
  + their `src/training/` bodies. `DecisionRecord` carries the public
  observation (`engine::PublicView`, which EMBEDS the mask — contract §4, so
  there is deliberately no second mask field to disagree with it), the sparse
  search distribution, the action, the outcome block and the four aux targets;
  the fanout bound is *derived* from `RunActionMask`'s action-bearing slots, not
  picked. Shards are append-only with a fixed 512-byte header holding all six
  stamp fields, read back through a read-only memory mapping, with **no record
  count in the header** so a crashed writer leaves an arithmetically-visible
  torn tail rather than a lie. The restricted sidecar is keyframes + an action
  log; quarantine is a loader-level metadata filter over a declared sim-commit
  order (unknown commits are excluded, never admitted).
  **Acceptance — real runs, not test cases** (owner direction 2026-09-03; the
  harness is `storage_numbers`, which exits non-zero on any failure, and the
  four in-flight gtest files from the 2026-08-25 session were deleted rather
  than finished). Every line below is checked on the artifacts of the same
  invocation that wrote them, and the full PASS list is
  `SpireTrainer/docs/verification/t1-2-storage-numbers.md` (training repo):
  *write→read round-trip* — 749 baseline + 331 deep records re-opened through
  the mapping and memcmp-identical to the bytes written, with `public_hash`
  re-derived from every read-back view matching the stored hash; *refusal* —
  a real shard copied and altered in one field is refused by name
  (`sim_commit_mismatch`, `schema_version_mismatch`,
  `record_layout_version_mismatch`, and `record_kind_mismatch` for the sidecar
  keyframe stream opened as a decision shard), while `read_shard_header` still
  reads the refused shard's metadata, which is what quarantine needs; *quarantine*
  — those three real files classified 1 accepted / 1 quarantined / 1
  unknown_commit; *reconstruction* — 101 baseline + 13 deep sampled
  intermediate states rebuilt from nearest keyframe + action log through the
  engine's own `advance()`, all **bit-exact** (longest replay 63 actions), the
  deep ones on **seed 1 under the engine's `SIM_SEARCH` scripted driver, which
  ends act 2 / floor 24 after 331 decisions** — so reconstruction is witnessed
  across a real act transition, not only inside Act 1. *Measured sidecar
  bytes/run:* **26,310 B** over the eight baseline runs (281 B/decision, 41×
  against a snapshot-per-decision) and **76,016 B** for the deep run
  (229 B/decision, 50×), at the default keyframe interval of 64.
  **Defect found and fixed in the process:** `DecisionRecord` had four bytes of
  *implicit* tail padding at this pin (body 13940, struct 13944) — indeterminate
  bytes in a record that is memcpy'd to disk and memcmp'd back. The tail pad is
  now *computed* from `sizeof(PublicView) + sizeof(SearchDistribution)` rather
  than hand-picked, so it cannot rot on the next pin move, and two static_asserts
  hold it (no implicit tail padding; the 64-byte prefix constant matches
  `offsetof(view)`).
  All six presets configure + build green and run the harness with identical
  output: win-debug / win-asan / win-release (clang-cl) and debug / asan /
  release (GCC/WSL) all produce a byte-identical
  `t1-2-storage-numbers.md` and a byte-identical `actor_smoke_v1.txt`.
  `tools/training/check_omniscient_boundary.sh` clean.

- **T1.3** `[x]` ∥ **Actor throughput spike.** Tiny net + real Gumbel
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
  config recorded as the Phase T2 default.
  **Inherited:** the selected search config becomes
  `STS_TRAIN_SEARCH_CONFIG_ID`'s first real value.
  **Inherited (2026-09-03):** the box's Python environment cannot drive the
  GPU — Python 3.9.7 with a CPU-only torch 1.8.1, while the RTX 5070 Ti
  (Blackwell) needs CUDA ≥ 12.8 and a 2025+ PyTorch. A dedicated env (a
  fresh venv or conda env, Python 3.12, current CUDA PyTorch) is the first
  deliverable of this task, and the C++ actor's inference path (LibTorch vs
  TensorRT vs ONNX Runtime — plan §5 names the first two) is a **recorded
  decision** in the numbers doc, with the env's exact versions pinned in a
  requirements/lock file the training repo commits.
  **Log:** 2026-09-03 — landed. Numbers doc:
  `SpireTrainer/docs/verification/t1-3-actor-spike.md` (training repo);
  environment: `SpireTrainer/docs/environment.md` (training repo) + `requirements.lock`.

  *The environment* (deliverable 1): `D:\STS_BG_Mod\_train_env\` — uv 0.12.9,
  a uv-managed CPython **3.12.14**, **torch 2.11.0+cu128** (cuDNN 91900) and
  LibTorch 2.11.0+cu128 (build hash `70d99e99`), all from
  `download.pytorch.org` / `astral.sh` with every URL, size and sha256 in
  `environment.md`. Verified by a real run, not an import:
  `torch.cuda.is_available()` true, device `NVIDIA GeForce RTX 5070 Ti`,
  **capability (12, 0)** — the line that proves the wheel carries Blackwell
  kernels rather than JIT-ing PTX — and a 4096-cubed fp16 matmul at
  **1.344 ms = 102.3 TFLOP/s**. Nothing is committed but the lock file and the
  doc; the tree is 8 GB.

  *The inference-path decision* (recorded, as the block requires):
  **LibTorch + TorchScript + fp16 + CUDA. The ONNX Runtime fallback was not
  reached.** LibTorch's headers build clean under clang-cl 22 and the whole
  path worked the day it was downloaded. Three things had to be discovered:
  (i) `find_package(Torch)` is unusable here — it reaches
  `find_package(CUDAToolkit REQUIRED)` and this box has the driver and
  LibTorch's bundled runtime DLLs and no toolkit, so
  `cmake/StsTrainLibtorch.cmake` hand-rolls two include dirs, five import
  libraries and one linker flag; (ii) that flag,
  `-INCLUDE:?warp_size@cuda@at@@YAHXZ`, is load-bearing — without it nothing
  names a torch_cuda symbol, torch_cuda.dll is never loaded, and
  `torch::cuda::is_available()` returns **false on a working GPU** (two probes
  did exactly that); (iii) LibTorch's Windows build is release-CRT, so a
  `/MDd` build crosses two heaps and **segfaults with an unflushed stdout** —
  the build now forces `/MD` and strips `/RTC1` whenever LibTorch is
  configured, the same remedy the engine's sanitizer wiring uses.

  *The spike* (deliverable 2): `src/training/{pv_encode,search,nn}.cpp` +
  `main_actor_spike.cpp`,
  `include/sts/training/{pv_encode,search,nn,leaf_queue}.hpp`,
  `tools/training/export_tiny_net.py`. A v0 tensorization of `PublicView`
  (96 entity tokens x 8 features + 64 enumerated actions x 4 categorical
  columns, one shared embedding table with ~2,048 rows per content domain per
  plan §3.1); a public-belief tree keyed by `public_hash` with a
  `resample_hidden` particle per simulation, exact engine transitions through
  the **mask-supplied `advance` overload**, Gumbel sequential halving at the
  root, PUCT in-tree and reveal afterstates coarsened per plan §3.2; a
  lock-free (Vyukov) leaf queue feeding one batched fp16 inference server with
  pinned host staging and two slots. Nets exported from the pinned env:
  1.20M (d=64), 2.61M (d=128) and 17.6M (d=384, 6 layers — inside plan §5's
  10-25M bracket, so the budget table rests on a MEASURED number at the plan's
  own model size instead of an extrapolation).

  **Acceptance — real runs.** Snapshot bank: **256** real A20 combat decisions,
  floors 1-16, mean root fanout 7.15, reached by stepping real seeds under the
  engine's `SIM_SEARCH` driver. **`t_enc` = 1.43 us** per observation over the
  full view, as a per-observation MINIMUM over 40 repetitions (0.57
  encode_public_view incl. the embedded mask + 0.40 public_hash + 0.46 our
  tokenizer). **R** at four batch sizes x three widths:
  w64 20.2k/75.5k/158.2k/292.4k, w128 25.7k/99.2k/198.1k/208.1k, plan384
  13.7k/24.4k/24.4k/25.0k evals/s at batch 64/256/512/1024 (best of 12). Per-decision
  breakdown (us): encode 249, step 147, copy 30, sample 140, tree 83, with NN
  wait 479k us of *suspended* time that overlaps other searches. End to end:
  1,536 concurrent searches on 12 worker threads gives **3,188 decisions/s and
  145,600 evals/s** (73 % of the same net's raw ceiling at the same batch),
  batch-fill mean **512.0 of a 512 cap**, with queue-wait and batch-fill
  histograms in the doc. Budget
  re-derived from measured R: at the plan's own D = 1,500 and 48
  evals/decision, `plan384` gives **2.95 GPU-s per run and ~29k runs/GPU-day**,
  i.e. the PESSIMISTIC end of plan §5's 30k-100k bracket and below its floor.
  A search result was written into a real `DecisionRecord` (fanout 5 of the
  mask-derived 610) as the container check.

  **Chosen Phase-T2 default: `e48-c8-gsh-rc-w0`**, and
  `STS_TRAIN_SEARCH_CONFIG_ID`'s CMake default moved from `none` to it. The
  rule is stated in the doc and is STRUCTURAL, deliberately: the spike's
  weights are random, so no sweep row carries information about decision
  quality, and a default picked on a quality figure derived from random weights
  would be invented. It keeps the plan's own commitments (Gumbel SH at the root
  — §3.2 makes PUCT-at-root the ablation — reveal coarsening, the 48-eval
  budget), takes a fresh particle per simulation, and takes the smallest
  candidate count that covers the measured mean root fanout (7.15 gives 8).

  **Two findings worth carrying forward.** (1) The inference path is
  **launch-bound, not FLOP-bound**: staging + enqueue costs 3.52 ms of host
  time per batch against 0.03 ms of device wait, so plan §5's "batches >= 512"
  is not a preference on this hardware and the first optimization to reach for
  is CUDA graphs or TensorRT, not a smaller net. (2) Throughput noise on this
  box is severe and ONE-SIDED — a second CPU-saturating job (T1.4s's generator)
  ran throughout — so R and the sweep report the BEST of N repetitions and the
  sweep prints each row's full spread; rows whose spreads overlap are ties, and
  the doc says so rather than reading a winner out of noise.

  **Presets.** `win-release` builds AND RUNS the spike on the GPU (that is the
  real run). All six configure + build; the five without
  `-DSTS_TRAIN_LIBTORCH_DIR` build the same sources with the reference
  evaluator and publish no R. `tools/training/check_omniscient_boundary.sh`
  clean; `tools/check_submodule_pin.sh` clean.

- **T1.4** `[ ]` ∥ **Dataset ingestion + tabular V0h (human data).** Acquire the public
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
  counts and schema documented.
  **Priority note (2026-09-03):** this is an **accelerant off the critical
  path** — GT1's "V0 shipped" bar is satisfied by T1.4s's sim-fitted V0s,
  and V0h's job becomes the pre-registered V0s→V0h bias measurement (plan
  §4.1). No local copy of the dump exists on this box (checked 2026-09-03);
  acquiring it is a user action, so this task starts when the data is on
  disk, not before. **Log:** —

- **T1.4s** `[x]` ∥ **Sim-fitted V0s (the first currency rung).** A
  scripted-rollout generator in the training repo that links the engine's
  `fuzz_core` scripted policies (`SIM_SEARCH` / `SIM_SEARCH_SKIP`, plus the
  E0 kinds for state diversity — all pure functions of (state, mask,
  PolicyRng), no engine-stream draws) and records the `PublicView` + mask at
  **every floor boundary** into T1.2 shards, with the run's outcome labels
  (max floor, per-act boss fight/kill, death floor, victory). The fit is the
  bootstrapped-horizon rule V1 already uses: a state in act *k* is labelled
  by whether act *k*'s boss was killed. Tabular P(kill current-act boss |
  act, floor-in-act, hp bucket, max-hp bucket, gold bucket, deck size,
  upgrade count, relic count) with additive smoothing; if the buckets starve
  at the tails, a small gradient-boosted model over the same features is
  the sanctioned fallback, chosen by held-out calibration and recorded.
  Ships as value artifact `v0s.1` in the T2.3-style versioned registry
  (this task creates the registry's first entry; T2.3 inherits it). Known
  biases, stated in the doc: the scripted policy's skill and its
  truncated horizon. If the engine's `tools/fuzz` targets are not built when
  the engine is embedded by `add_subdirectory`, the one sim-side change is a
  CMake option exposing `fuzz_core` — that edit lands in this repo under
  full conventions and is the only engine change this task may make.
  **Deps:** T1.2, and the engine pin at ≥ 6c50a0b (three-act content)
  **Acceptance:** ≥ 1M floor-boundary rows generated deterministically
  (same seed set → byte-identical shards, asserted); the training repo's
  own `check_omniscient_boundary` clean over the generator; calibration
  report (reliability diagram + Brier, per act and per hp bucket) on a
  seed-disjoint split committed under the training repo's
  `docs/verification/`; V0s beats the per-(act, floor) base-rate predictor
  on held-out Brier by a margin the report states; the V0s→V0h comparison
  protocol pre-registered in the same doc (features, split, the delta that
  would trigger re-fitting downstream heads).
  **Inherited (2026-09-03):** T2.3 takes the value-artifact registry this task
  creates, T1.7 takes the generator as its snapshot source and `v0s.1` as its
  leaf currency, and T1.4 takes the pre-registered V0s→V0h protocol — all three
  are on their own `**Inherited:**` lines and in the Deferred table above.
  **Log:** 2026-09-03 — landed on engine pin `6c50a0b`. Report:
  `SpireTrainer/docs/verification/t1-4s-v0s.md` (training repo); artifact
  `artifacts/v0s.1/v0s_table.npz` (sha256
  `3ea63a2220fc0550957c60a83708c7c4b2c75f9bbccc31f21ee4850a3a00e4c4`, 126,094 B)
  registered as the first entry of `artifacts/value-artifacts.json`.

  **`fuzz_core` needed NO engine change.** The block allowed one CMake option as
  the single permitted sim-side edit; it was not needed and was not made. The
  engine's `tools/fuzz` directory sits inside its `if(STS_BUILD_TESTS)` block,
  which this repo's `STS_TRAIN_ENGINE_TESTS=ON` already turns on, so
  `if(TARGET fuzz_core)` in `src/training/CMakeLists.txt` links it exactly as
  `storage_numbers` has since T1.2. With `STS_TRAIN_ENGINE_TESTS=OFF` the
  generator still builds and refuses a scripted policy by name instead of
  silently substituting one.

  **What landed.** `rollout_floor_rows`
  (`src/training/main_rollout_floor_rows.cpp`) over
  `include/sts/training/floor_rollout.hpp` + `src/training/floor_rollout.cpp`,
  plus `sha256.{hpp,cpp}` so the tool prints its own artifacts' digests rather
  than quoting an external `sha256sum`; and the fit,
  `tools/training/v0s_fit.py` (numpy only; scikit-learn optional and only for
  the GBM comparison — **no torch/CUDA was installed, that env is T1.3's**).

  **The `kOpen` obligation, discharged for offline producers.** A run's
  floor-boundary rows are held in memory until the run TERMINATES, stamped there
  with the outcome block and `value_target`, and only then appended. Nothing on
  disk is rewritten — the shard stays strictly append-only — and the buffer is
  bounded by one run's floor count (≤ ~56 records), not by the shard. What
  remains for T2.3 is the ONLINE case (an actor publishing rows before its run
  ends), which is the only one that needs a rewrite pass. `outcome_return` stays
  0 because `weights_version` is `none`: no currency is named, so writing a
  number there would be inventing one.

  **Generation (real runs, the acceptance).** Fit cohort: seeds `[1, 36001)` x
  {`sim_search`, `sim_search_skip`}, policy seed 20260903, A20 Ironclad —
  **72,000 runs, 1,259,568 floor-boundary rows**, 65,175,786 decisions, 3,901.5 s
  wall (**322.8 rows/s, 18.5 runs/s** at 16 threads), 63 shards x 20,000 records
  (17.6 GB, uncommitted, at `D:\STS_BG_Mod\_train_data\v0s\main`). Deepest
  floor 51; rows by act 1,095,356 / 163,379 / 833; boss fights 50,720 / 1,993 / 7
  and kills 26,242 / 156 / 0. Out-of-cohort corpus: seeds `[500001, 508001)` x
  {random, greedy_damage, greedy_block, hoard_gold, always_event, ladder} —
  48,000 runs, 345,380 rows, 83 s (4,137.7 rows/s). The ~8 rows/s the block
  quoted from `seed_scan` was pessimistic by ~40x for this workload.

  **Determinism, asserted.** The whole 1,259,568-row sweep was generated TWICE —
  16 threads / chunk 512, then 10 threads / chunk 1024 — and all **63 shard
  sha256s and `runs.csv` are identical** (first shard
  `9d7c6aaf40d96511…`, last `415a9ba4b6472145…`, `runs.csv`
  `c60c8b72f94b4627…`). `v0s_fit.py --verify-manifest` is the comparison and
  refuses to fit on a single mismatch — it reads only the manifest, so the
  second sweep's 17.6 GB of shards were deleted after the comparison and its
  `manifest.json` + `runs.csv` kept, which is all a re-check needs. Additionally byte-identical
  **across hosts**: seeds `[1, 201)` x 4 policies under WSL/GCC `release`
  (12 threads) and Windows/clang-cl `win-release` (4 threads) produced identical
  digests. Three negative controls, each refused by name and exiting non-zero:
  a verification manifest from a different sweep
  (`the verification sweep is not the same sweep: seed_begin is 500001 there and
  1 here`); one tampered digest inside a valid verification manifest
  (`shard digests differ in 1 shard(s): floor_rows_00002.stsshard`); and one
  altered `sim_commit` byte inside a real shard (`sim_commit_mismatch: … shard
  'zc50a0ba…' != manifest '6c50a0ba…'`), which is the Python reader holding the
  same refuse-on-mismatch line as `shard.hpp`'s `open()`.

  **The label, cross-checked twice.** Bootstrapped horizon: a state in act *k*
  is 1 iff act *k*'s boss was killed in that run, the boss probe copied in shape
  from the engine's own fuzz driver (a transition into `COMBAT` with
  `room_type == Boss`; a kill is that combat LEAVING `COMBAT` with
  `combat_outcome == KILLED`, which is the only probe that works for Act 3,
  whose kill opens no screen at all). The record's `value_target` and the
  per-run `runs.csv` boss bits agree on **1,259,568 of 1,259,568** rows; act-3
  kills and `run_is_victory` terminals agree at 0 = 0, which the report flags as
  VACUOUS on this corpus rather than counting it as evidence.

  **The fit and its calibration.** Hierarchical additive smoothing, L1
  (act, floor-in-act) → L2 (+hp, max-hp) → L3 (+gold, deck, upgrades, relics),
  each level shrunk toward its parent with a per-level alpha chosen on dev
  (1 / 100 / 200); the prior is STRUCTURAL (`cell // divisor`), which is what
  gives an unoccupied cell its parent's value — a data-averaged prior sent every
  empty cell to the global rate and was measured LOSING to its own L1 baseline
  by 5 % before the fix. 248,832 cells, 20,645 occupied in train. Seed-disjoint
  70/10/20 split: 882,619 / 125,274 / 251,675 rows over 25,200 / 3,600 / 7,200
  seeds. On the never-selected-on test split: **V0s Brier 0.201164 vs the
  per-(act, floor) base-rate predictor's 0.205597** — a margin of **0.004433
  (2.16 % relative)**, whose paired bootstrap over the 14,400 test RUNS gives a
  95 % interval of **[0.003901, 0.004917], excluding 0**. Level by level:
  0.235175 → 0.205597 → 0.202346 → 0.201164. Excluding the 5,290 already-settled
  rows (post-boss floors, p ≥ 0.9, all label 1 — the report names them rather
  than letting them flatter the aggregate) the numbers are 0.205483 vs 0.210011.
  The sanctioned GBM fallback was fitted and **won by 0.81 % on dev** — under the
  1 % threshold the `CHOICE_RULE` declared before either model existed, so the
  table ships and the report says plainly that the GBM was the better model by
  less than the pre-declared margin. Out-of-cohort (the policy-skill bias, made
  a number): on the E0/ladder corpus V0s scores 0.128224 against that corpus's
  own base-rate constant of 0.002755, because these policies kill an act boss
  0.28 % of the time.

  **Builds and guards.** All six presets configure + build green — win-debug /
  win-asan / win-release through a `t14senv`-style vcvars+LLVM wrapper, and
  debug / asan / release through `tools/wsl_run.sh --script`. A real
  **ASan/UBSan** generation (60 runs over sim_search / greedy_damage / ladder,
  686 rows) exits 0 clean. `tools/training/check_omniscient_boundary.sh` clean;
  `tools/check_submodule_pin.sh` clean. No gtest cases were added and `ctest` was
  not used as acceptance (owner direction 2026-09-03, conventions §7).

  **Conventions fixed in passing (§5, stop-the-line):** §3 still said
  Claude-authored commits carry a `Co-Authored-By` trailer. The 2026-09-03
  ledger-mirror commit announced the opposite in its subject and changed only
  `training-tasks.md`, leaving the losing document unfixed; §3 now says **no
  attribution trailer**.

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
  observation (negative control) and passes on the clean encoder.
  **Inherited:** the twin fixture container and its refusal rules are
  specified in the engine's `tests/golden/twin_fixtures/README.md`; the one
  recorded, *open* mask leak (draw-sourced choices) is contract §4a — a probe
  that recovers draw-slot types while such a screen is open is finding THAT,
  not a new defect.
  **Inherited (from T1.3):** "the spike net" in the Acceptance above is now a
  real artifact — `tools/training/export_tiny_net.py` exports it and
  `Evaluator` (`nn.hpp`) runs it — and the encoder whose invariance is being
  tested is `encode_observation` (`pv_encode.hpp`). Two properties of that
  encoder matter here and are not obvious: the enumerated ACTION ORDER is part
  of the observation (a policy logit vector is indexed by position, so
  invariance means the same order too), and the draw pile enters as the view's
  CANONICALLY SORTED multiset plus its order-constraint annotations, which is
  the only channel any order knowledge legitimately reaches a token through.
  **Log:** —

- **T1.7** `[x]` **Tracer-bullet expert-iteration loop (non-durable).** One
  end-to-end cycle of the T2.2 shape, run BEFORE GT1 and deliberately
  throwaway — permitted by the plan's rule, which forbids *durable*
  training before the gate, and existing to pull integration failures
  (batch starvation, hot-swap races, shard/loader mismatches, learner
  ingest lagging actor production) forward by months. Graph: ≥ 1k Act-1
  combat snapshots (the T1.4s generator's floor-boundary states advanced
  into their next combat, or the T2.1 bank if it exists) → the T1.3 actor
  with real search and `resample_hidden` worlds → T1.2 shards → a Python
  learner step on the tiny net (policy + value, leaf currency V0s if landed,
  else a constant labelled as such) → weights exported and hot-swapped into
  the running actor → next generation. Every plan §5 day-one telemetry
  counter live (per-decision time breakdown, batch-fill and queue-wait
  histograms, steps-per-run per weights version, learner ingest vs actor
  production).
  **Deps:** T1.2, T1.3 (T1.4s is an accelerant, not a dep)
  **Inherited (from T1.3):** the actor and the net-export path exist and are
  the ones to use, not ones to rebuild. `actor_spike`
  (`src/training/main_actor_spike.cpp`) already runs the whole graph this task
  needs except the learner step and the hot swap: snapshot bank — concurrent
  `Search` objects — lock-free leaf queue — batched fp16 `InferenceServer` — a
  `DecisionRecord` (T1.3 asserts one real search result fits one). The net is
  defined and exported by `tools/training/export_tiny_net.py`, and `Evaluator`
  (`nn.hpp`) is the seam a HOT SWAP plugs into — it is already an interface
  with two implementations, and swapping weights is swapping the
  `torch::jit::script::Module` behind it. **Every plan §5 day-one counter this
  task's acceptance names is already live and printed** (per-decision
  encode/step/copy/sample/tree/NN-wait, batch-fill and queue-wait histograms);
  what T1.7 adds is steps-per-run per weights version and learner-ingest vs
  actor-production. Read T1.3's numbers doc before sizing a generation: on
  this box the inference path is LAUNCH-bound, and throughput noise is
  one-sided and large enough that a per-generation report must say how many
  repetitions it is the best of.
  **Acceptance:** three generations complete unattended; a per-generation
  report (throughput, batch-fill, ingest-vs-production, wall-clock per
  generation) committed under the training repo's `docs/verification/`;
  every artifact the loop produced is deleted or labelled non-durable and
  nothing from it is registered as a value artifact or checkpoint; the
  list of integration defects found and fixed is in the Log.
  **Inherited (2026-09-03, from T1.4s):** the snapshot source exists —
  `rollout_floor_rows` (`src/training/main_rollout_floor_rows.cpp`) plays A20
  runs under the engine's scripted `PolicyKind`s and writes T1.2 decision shards
  with outcome labels, deterministically and at any thread count; its
  floor-boundary states advanced into their next combat are exactly this task's
  "≥ 1k Act-1 combat snapshots". The leaf currency exists too: `v0s.1`
  (`artifacts/value-artifacts.json`), so the graph's value head is a real
  currency and not the constant the block allows for. Two things come with it:
  V0s has never seen a mid-combat state (deferred-obligation row above — this
  task is the first that can measure the extrapolation), and V0s is
  POLICY-CONDITIONAL, measured out-of-cohort in the report's §7, so the loop's
  snapshots must be generated by the policy whose currency it is using.
  **Inherited (2026-09-03, from T2.1):** the block's "or the T2.1 bank if it
  exists" is now the first option, not the fallback — 120,004 snapshots with
  22,009 already IN `RunPhase::COMBAT`, so this task's ">= 1k Act-1 combat
  snapshots" needs no advancing step at all. Use `BankReader` +
  `bank_restore_state`; the bank is restricted on the sidecar's terms and a raw
  memcpy of `record.state` is wrong (its encounter lists are zeroed). Note the
  bank's policy mix is seven scripted kinds, which matters for the
  V0s-is-policy-conditional obligation above: filter to `sim_search` /
  `sim_search_skip` (the `policy` field, 24.9 % each) when the loop's value
  head is `v0s.1`.
  **Log:** 2026-09-03 — landed on engine pin `6c50a0b`. Report:
  `SpireTrainer/docs/verification/t1-7-tracer-bullet.md` (training repo).
  Three generations completed unattended in **144.7 s** in ONE actor process.

  **The graph, and what is new versus what was inherited.** New:
  `tracer_actor` (`src/training/main_tracer_actor.cpp`), the learner
  (`tools/training/tracer_learner.py`), the driver + report generator
  (`tools/training/tracer_loop.py`), and the V0s read side
  (`include/sts/training/v0s_table.hpp` + `src/training/v0s_table.cpp`, fed by
  `tools/training/v0s_export_flat.py`). Inherited and NOT rebuilt: T2.1's bank
  and `bank_restore_state`, T1.3's `Search` / `InferenceServer` / `pv_encode` /
  `export_tiny_net.py`, T1.2's shard container and `set_search_distribution`,
  T1.4s's per-run buffering pattern and `v0s.1`. The DRIVER is deliberately
  thin and the ACTOR is the long-lived process: a driver that ran the actor once
  per generation would never exercise the hot swap at all, which is one of the
  four integration failures this block names.

  **Where V0s is used, stated exactly.** The tree's leaf value is the NETWORK's
  value head — that is what expert iteration trains. `v0s.1` is the CURRENCY,
  evaluated **once per episode, at the state the combat EXITS into** (the plan's
  E[V(exit RunState)]), and stamped onto every record of that episode before the
  shard is sealed — T1.4s's buffering pattern applied to a combat instead of a
  run, so the stream stays strictly append-only and no record is ever written
  with `outcome_kind = kOpen`. A dying episode's exit value is 0. V0s is never
  queried at a search leaf, because the exit state is floor-boundary-shaped and
  a mid-combat state is not — and the extrapolation that would be is now a
  MEASUREMENT rather than a worry (deferred table above): mid-combat mean
  0.3946 against exit means 0.2400 / 0.2709 / 0.2731, mean absolute gap
  0.161 → 0.131 → 0.129, systematically optimistic.

  **Acceptance (real runs).** 2,048 Act-1 combat snapshots drawn from the T2.1
  bank (120,004 records scanned, 4,886 eligible after filtering to
  `RunPhase::COMBAT`, act 1, non-terminal, `sim_search`/`sim_search_skip`),
  stratified round-robin over 14 occupied (floor bucket, phase, HP bucket)
  cells, restored through `bank_restore_state` with 0 failures. Three
  generations x 4,096 episodes: **29,201 / 27,697 / 28,872 records**, 1,198 /
  1,113 / 837 decisions per second, 36.2 / 35.7 / 36.1 evals per decision, mean
  batch fill 245 / 226 / 190 of a 512 cap at 1,536 concurrent searches, **zero
  queue-full spins**. Learner: 600 Adam steps per generation, policy CE
  1.6139→1.5450, 1.5679→1.5047, 1.5066→1.4918 and value MSE 0.08966→0.01145,
  0.01168→0.00739, 0.00779→0.00645, with **policy-target coverage 1.000000** in
  all three. Hot swap: three swaps, 0.03–0.04 s each, at an EPISODE boundary
  (strictly stronger than the "between decisions" the block asks for, and the
  right unit — it is what keeps every record of one episode attributable to one
  weights version). Every plan §5 day-one counter is live and printed per
  generation: the encode/step/copy/sample/tree/NN-wait breakdown, the batch-fill
  and queue-wait histograms, steps-per-run per weights version, and learner
  ingest vs actor production. Movement across the three generations, with **no
  significance test run** (that is T2.2's): deaths 31.9 % → 23.8 % → 23.4 %,
  exit HP fraction 0.355 → 0.414 → 0.423, damage taken 24.4 → 19.9 → 19.2, exit
  V0s 0.240 → 0.271 → 0.273.

  **Nothing is promoted.** `artifacts/value-artifacts.json` is untouched and
  still names exactly one entry. No checkpoint kept. The 2.0 GB the loop wrote
  (1.57 GB of shards + observation companions, four 2.4 MB `.pt` modules, the
  1.0 MB flat V0s projection) was deleted by the driver's `--cleanup` pass,
  leaving 59 KB: a `README.md` titled **NON-DURABLE**, the three per-generation
  `manifest.json` / `learner.json`, and `tracer_run.json`. `v0s.1.flat` is a
  DERIVED projection of the registered artifact, not a second artifact — it
  carries the source sha256, the C++ loader refuses a file whose digest is not
  the one the caller names and re-checks 256 corpus-sampled probe rows against
  the value Python computed for them, and the export re-checks the artifact
  against the registry and its own act-start table against
  `v0s_fit.floor_in_act` over all 1,259,568 fit-corpus rows (**0
  disagreements**) before writing a byte.

  **The integration defects found and fixed — the deliverable.** Full write-ups
  with their symptoms are in the report; the list:
  1. *The Python shard reader's header format drifted from `ShardHeader`, and
     the refusal it produced named the right field for the wrong reason.* A
     hand-written `struct` format spent TWO eight-byte pads where the header has
     one, sliding every field from `writer_run_id` on; the symptom was not a
     crash but `sim_commit_mismatch: shard '763377e81335181f1ff01f57e75610b3' !=
     manifest '6c50a0ba763377e81335181f1ff01f57e75610b3'` — a plausible value
     and a completely wrong diagnosis. Fixed by taking `v0s_fit.py`'s validated
     format verbatim plus `assert struct.calcsize(_HEADER_FMT) == 512` at
     import.
  2. *`engine::Action{bits == 0}` is a legal, common action, so zero cannot be
     an empty-slot sentinel.* `ActionVerb::PLAY_CARD` is 0 and the args are the
     high bytes, so "play hand card 0, untargeted" IS `Action{0}`; marking slots
     live with `bits != 0` deleted **15.3 %** of the policy-target mass and
     would have trained silently. Caught by the coverage check (0.847442 against
     `--min-coverage 0.999`). Fixed on both sides of the join — a slot is live
     iff `act_valid[slot]`, a sparse entry iff its column index is below
     `search_fanout` — and the check stays in as a permanent gate.
  3. *A shard's `weights_version` is fixed at BUILD time*, so an online actor
     that hot-swaps weights cannot label which generation produced a record —
     and if it did, its own build's reader would refuse the shard. Demonstrated
     by a patched-header negative control refused by name
     (`weights_version_mismatch: shard 'tracer.g1' != manifest 'none'`, exit 2).
     Worked around here (weights path + sha256 in the generation manifest);
     the durable fix is in the deferred table above, owner T2.3.
  4. *"Learner ingest vs actor production" is ambiguous, and the flattering
     reading is wrong by 25x.* Ingest alone says the learner has 35x headroom
     (39–45k rec/s against 837–1,198); end to end, on the clock the loop
     actually sees, it is 1,479–1,731 rec/s, i.e. **1.3x–1.8x** the actor,
     because ~16 s of every 17–20 s learner step is fixed cost (torch import,
     CUDA context, the TorchScript export and read-back). Both rates are now
     emitted and the report prints the ratio from the end-to-end one.
     Consequence for T2.2: at this net size the loop is nearly balanced, so the
     first thing that makes the learner the bottleneck is a bigger net, not more
     data.

  **Findings that are not defects** (report §"Findings that are not defects"):
  17–19 % of combat "decisions" have exactly one legal action and are stepped
  without a search and without a record; batch fill is set by search concurrency
  and not by thread count (8.4 of 256 at 64 concurrent searches, 190–245 of 512
  at 1,536) **and its histogram is BIMODAL, so the mean is the wrong statistic**
  — 34–38 % of batches launch at exactly the 512 cap and another 34–48 % at 32
  or below (the generation tail draining), with the middle thinly populated; and
  the run is ONE repetition, so T1.3's one-sided-noise finding applies verbatim
  and none of these throughput numbers should be read as a measurement of R.

  **Presets and guards.** All six configure + build `tracer_actor` — win-debug /
  win-asan / win-release through a `t17env`-style vcvars+LLVM wrapper, and
  debug / asan / release through `tools/wsl_run.sh --script`. `win-release` with
  `-DSTS_TRAIN_LIBTORCH_DIR` is the real run; a **win-asan** (ASan/UBSan,
  clang-cl) generation over 24 episodes on the reference evaluator exits 0
  clean. `tools/training/check_omniscient_boundary.sh` clean (49 files);
  `tools/check_submodule_pin.sh` clean. No gtest cases were added and `ctest`
  was not used as acceptance (owner direction 2026-09-03; conventions §7).

### GT1 `[ ]` **Gate: trainer contract live (completes InitialPlan M6) — no durable training before this**
**Deps:** T1.1–T1.6
(M7 deliberately maps to no gate: E1 is demoted from gate to accelerant per
plan §8 delta 2; its surviving pieces are T1.4/T3.1/T3.2.)
- [ ] Leak gates green (T0.5, T0.6, T1.6).
- [x] R and t_enc measured; budget table re-derived (T1.3 — 2026-09-03).
- [ ] V0 shipped with calibration report — V0s (T1.4s), or V0h (T1.4) if
      the dump landed first.
- [ ] Eval harness + seed populations frozen (T1.5).
- [x] The tracer-bullet loop ran ≥ 3 generations and its report is
      committed (T1.7 — 2026-09-03, SpireTrainer `ee2af52`).
**Log:** —

---

## Phase T2 — Act-1 combat expert iteration (Gate GT2; ∥ with S2 engine work)

- **T2.1** `[x]` ∥ **Snapshot bank.** Reachable-state harvesting from
  survival-biased policies (the four landed B5.1 E0 heuristics suffice for
  the first bank; TE.1's campaign drivers and, later, agent checkpoints
  improve it — TE.1 is deliberately not a dep; deep strata additionally
  from TE.3's handicap-assisted generator, whose assist-level and
  rolled-back-death provenance fields carry into the bank schema so
  training can stratify, weight, or exclude on them — TE.3 is likewise an
  accelerant, not a dep), stratified by
  floor/deck-archetype/HP; branch-K memcpy reset tooling; bank format
  versioned with the trajectory schema.
  **Deps:** T1.2 (the versioned schema) — GT1 is deliberately **not** a
  dep since 2026-09-03: bank harvesting is data generation from scripted
  policies that already exist, and waiting on V0/eval/leak gates for it
  put the whole T2 phase behind an external data acquisition
  **Acceptance:** bank of ≥ 100k snapshots with the
  stratification report: ≥ 20 % of snapshots from floors 8+ and every
  RunPhase represented (vs the random-policy baseline's 97.6 % floor-1–7
  mass), with a provenance breakdown (survival-biased vs handicap-assisted)
  whenever TE.3 states are included; reload + twin-test spot check green
  on ≥ 1,000 sampled snapshots.
  **Inherited:** the bank format is the T1.2 shard container — `RecordKind` is
  append-only and never renumbered, and a bank stream is a fourth kind rather
  than a new file format. Two T1.2 obligations land here (table above): the
  keyframe interval is unswept, and the sidecar reader linear-scans, so the
  harvester is what should build an **on-disk** index. The deep-run harness in
  `storage_numbers` already shows the shape — the engine's `SIM_SEARCH`
  scripted driver, reachable because the engine is embedded, driving
  `record_run` through a `RunPolicy`.
  **Log:** 2026-09-03 — landed on engine pin `6c50a0b`. Report:
  `SpireTrainer/docs/verification/t2-1-snapshot-bank.md` (training repo).
  Bank (uncommitted, 1.31 GiB): `D:\STS_BG_Mod\_train_data\bank\main`.

  **What landed.** `BankSnapshotRecord` + `StreamIndexEntry` as `RecordKind`
  4 and 5 in the T1.2 container (append-only, never renumbered — the bank is a
  new *kind*, not a new file format, exactly as this block's `**Inherited:**`
  line required); `include/sts/training/{snapshot_bank,stream_index}.hpp` +
  bodies; and two tools, `bank_harvest` (generation) and `bank_check`
  (verification + the report). They are a SIBLING of `rollout_floor_rows`
  rather than a mode of it, because one `--mode` flag between a public
  artifact and a restricted one is the seam that should be a separate program.

  **The bank.** 120,004 snapshots over 13 shards from 12,938 A20 runs
  (10,567 of them contributing), seeds `[1, 1850)` x seven policies —
  `sim_search`, `sim_search_skip`, `random`, `greedy_damage`, `greedy_block`,
  `hoard_gold`, `always_event` — policy seed 20260903, 4,853,004 decisions
  stepped, 372,552 candidates offered, 651 s at 16 threads.
  `sizeof(BankSnapshotRecord)` = 11,704 B, of which 11,600 is the
  `RunController`. Stratification (report §1): **floors 8+ = 58,001 =
  48.33 %** (bar: >= 20 %); every reachable `RunPhase` present — NEOW 1.77 %,
  MAP_CHOICE 20.42 %, COMBAT 18.34 %, COMBAT_REWARD 18.42 %, RUN_OVER 4.01 %,
  REST_SITE 9.71 %, TREASURE_ROOM 4.14 %, EVENT_DIALOG 13.56 %, SHOP 7.43 %,
  **BOSS_TREASURE 2.20 %**; five HP bands all between 15.8 % and 23.5 %; deck
  buckets 2.5/56.2/38.1/3.2 %; acts 1/2/3 at 83.15/16.76/0.09 %; the two
  SIM_SEARCH policies 24.9 % each and the five E0 kinds 7–16 % each.
  Provenance is **100 % `scripted` at assist level 0, TE.3 = 0** — the field
  exists and is measured rather than assumed, which is what the plan asks for.
  Two `RunPhase` values cannot appear and the report says so: `NONE` is the
  pre-`run_begin` value, and `ROOM_UNIMPLEMENTED` offers no legal action and is
  unreachable at this pin — 0 in the bank and 0 of 2,000 freshly-played
  `random` runs, corroborating S2-G1's zero-parks result. That is a CHECK, not
  a footnote: it fails the day a park becomes reachable.

  **The baseline, measured rather than quoted.** 2,000 `random` A20 runs,
  90,388 decisions: **98.55 %** of them on floors 0–7 (the block quotes
  97.6 %). The bank's own policy mix, unstratified, is 22.98 % there; the
  stratified bank is 51.7 %. The quota is what moves it.

  **Reload + twin spot check (real run, `win-debug`).** 1,000 snapshots on a
  fixed stride over the whole bank: 1000/1000 stored `public_hash` re-derived
  from the reloaded state; 1000/1000 found byte-identically again through the
  on-disk index by (run_id, step); 946/946 non-terminal recorded actions still
  mask-legal and 946/946 still moved the state under `advance()`; and
  **1000/1000 byte-identical `encode_public_view` between the state and
  `engine::make_hidden_twin(state, seed)`** — the engine's own twin utility,
  not a reimplementation.

  **Branch-K memcpy resets.** 120 branch points, 368 branches, 287 distinct
  post-step run hashes. 120/120: the base state byte-unchanged after the
  fan-out; every branch advanced; stepping branch 0 further left branches
  1..K-1 byte-identical (the independence claim, checked rather than assumed);
  the common-random-number fan-out byte-reproducible; all branches sharing one
  pre-step world under CRN; and the branches' worlds differing under
  independent sampling — the unpaired baseline T2.4's variance report is made
  against.

  **Determinism, asserted.** The same configuration swept twice more and
  compared digest by digest: Windows/clang-cl at 12 threads / chunk 1400, and
  **WSL/GCC release at 16 threads / chunk 700**. All 13 shard digests plus
  `runs.csv`, `visits.csv` and `bank.index` identical in both — 16 files
  compared, 0 differing, across thread count, chunk size, host and compiler.
  First shard `ddaae7e075fdf7f8a1…`, last `c5aa00df08c3565be8…`, `runs.csv`
  `8a4e4f6b570f55fc1e…`. The verification sweeps' shards were deleted and their
  manifests kept, which is all a re-check needs (`bank_check
  --verify-manifest`).

  **Defect found and worked around: a `RunController` is not portable between
  PROCESSES** (report §8, and a new deferred obligation above).
  `RunController::lists` holds three `std::array<std::string_view, N>` into the
  registry's static strings, so a memcpy'd state carries POINTERS. Invisible
  inside one process — which is why T1.2's sidecar, which writes and replays in
  the same invocation, never saw it — and a segfault the first time
  `bank_check` loaded a bank another process wrote. A bank record therefore
  stores the three lists as registry ids, zeroes the views in the stored state,
  and `bank_restore_state` rebinds them; `bank_capture_lists` refuses by name
  rather than storing a 0 for a key it cannot resolve. It is also what makes
  the bank deterministic at all: a stored pointer would move with ASLR.

  **Both T1.2 obligations discharged** (table above, report §4/§7): the
  keyframe interval is swept at 8/16/32/64/128/256/512 and KEPT at 64 on the
  numbers, and `stream_index.hpp` puts the index ON DISK as a fifth record
  kind, one entry per (run, stream), used by the bank and by both sidecar
  streams with the scan as fallback.

  **Acceptance (real runs; no gtest cases written, per the owner's 2026-09-03
  direction).** All six presets configure + build green — `win-debug` /
  `win-asan` / `win-release` through a vcvars+LLVM wrapper, `debug` / `asan` /
  `release` through `tools/wsl_run.sh --script`. `bank_check` runs green under
  all six against the same bank: **every `bank_check` check passes** under win-debug,
  win-release, win-asan, and WSL debug/release/asan (the committed report is
  the `win-debug` run, NDEBUG undefined). `tools/training/
  check_omniscient_boundary.sh` clean (34 files); `tools/check_submodule_pin.sh`
  clean.

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
  worker. Pre-registered fallback (plan §4.3 contingency): if the paired
  bars below stay unmet after the declared exploration kit is exhausted,
  the next lever is assist-*annealed* generation via the TE.3 knob —
  adopted only with a plan change-log entry, never silently.
  **Inherited (2026-09-03, from T2.1):** the bank exists —
  `D:\STS_BG_Mod\_train_data\bank\main`, 120,004 snapshots, regenerable
  byte-for-byte by `bank_harvest` from the parameters in its `manifest.json`.
  Read it through `BankReader` and turn a record into a live controller ONLY
  through `bank_restore_state`: the payload's `lists` views are zeroed and a
  raw memcpy of `record.state` gives a controller whose encounter lists are
  empty — a wrong answer rather than a crash, which is the worse failure. The
  bank is a RESTRICTED artifact on the sidecar's terms (snapshot_bank.hpp): a
  bank state is an input to the ENGINE, never to an encoder or a network, and
  every key a consumer stratifies or weights on is a public quantity in the
  record's own header, so sampling never touches the payload. Provenance is
  100 % `scripted` at assist level 0 today; the fields to stratify on are
  there for the day TE.3 lands.
  **Inherited (2026-09-03, from T1.7): THE LOOP IS THE SKELETON — extend it,
  do not rebuild it.** `tracer_actor` + `tools/training/tracer_learner.py` +
  `tools/training/tracer_loop.py` already run this task's whole graph end to
  end, three generations unattended: bank → stratified Act-1 combat snapshots →
  concurrent `Search` with the exploration kit (Gumbel root sampling inside the
  search, an action-sampling temperature schedule outside it) → T1.2 shards with
  the outcome block stamped at the combat exit → a Python learner step on the
  tiny net → TorchScript export → atomic hot swap into the STILL-RUNNING actor
  → next generation. Every plan §5 day-one counter is live and printed.
  What T2.2 must ADD, and what T1.7 deliberately did not build: the
  distributional exit-HP/death heads and the last-layer value ensemble; replay
  freshness targets and a replay buffer (T1.7 trains on one generation and
  throws it away); the micro-combat ground-truth suite; the ≥ 1 permanent
  debug-preset worker; and the paired statistics, which T1.7 states plainly it
  did not run. Four things T1.7 measured that change how this task is sized:
  (i) the loop is nearly BALANCED end to end (learner 1.3x–1.8x the actor, not
  35x — the ingest rate is the misleading one), so a bigger net makes the
  learner the bottleneck before more actors do; (ii) 17–19 % of combat
  "decisions" have one legal action and must not be searched or recorded;
  (iii) batch fill is set by search concurrency, not thread count (8.4/256 at 64
  concurrent searches, 190–245/512 at 1,536); (iv) `v0s.1` is systematically
  optimistic at a mid-combat state by ~0.13–0.16 absolute against its own exit
  value, which is the number the "generate a combat-state corpus?" decision in
  the deferred table now rests on. Two deferred obligations land here from
  T1.7 (table above): the tensor-storage decision (a fifth `RecordKind` versus a
  declared companion format), and — jointly with T2.3 — the build-time
  `weights_version`, which is what stops an online actor from labelling which
  generation produced a record.
  **Inherited (from T1.3):** `STS_TRAIN_SEARCH_CONFIG_ID` now defaults to
  **`e48-c8-gsh-rc-w0`** — 48 leaf evaluations per decision, Gumbel top-8 at
  the root with sequential halving, PUCT in-tree, reveal-afterstate coarsening
  on, a fresh `resample_hidden` particle per simulation. **That default was
  chosen structurally, not on quality** (T1.3's weights were random; see the
  deferred-obligations table), so re-running T1.3's sweep grid against this
  task's trained net and either confirming or moving the id is T2.2's work,
  and a move is a `SearchConfig::id()` change plus the CMake default plus a
  note here. Three more T1.3 obligations land here (table above): the v0
  tensorization is combat-only and capped, potions and run-layer actions are
  outside the searched action set, and the inference path is single-stream
  and launch-bound — which is the constraint the plan §5 production-loop
  plumbing half of this task runs into first.
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
  **Deps:** S2 (**satisfied 2026-08-27** — simulator tag `s2-g2-verified`;
  the bar's evidence is the simulator repo's
  docs/verification/s2-verification.md), GT0 **Acceptance:** twin +
  tripwire suites green over S2 phases; an S1-era shard loads and trains
  under the extended schema.
  **Log:** 2026-09-02 — **sim half re-derived as ALREADY SATISFIED; no
  engine change and no `PUBLIC_VIEW_VERSION` bump owed.** Every reserved
  field this row names is populated with its declared meaning today:
  `act_reserved` since v2, `second_boss_reserved` since v5 (S2.28), and
  `boss_relic_choice_reserved[3]` since S2.11 (source re-pointed to
  `run.boss_chest` at S2.47, schema v8) — all three additive (audit case 1),
  so an S1-era PublicView record of the SAME version reads unchanged. The
  S2-phase twin/tripwire coverage is in place (`BossChest.*` pins the
  unopened-twin byte equality and the `seen`-gated reveal; the S2.28
  double-boss tests pin `second_boss_reserved` through `RUN_OVER`; the
  total-byte tripwire is over the whole `RunController`). One consumer-facing
  wording fix landed with this note: [training-contract.md](training-contract.md)
  §2's group table still said the boss-relic field was "zero until S2/S3".
  What remains of T4.1 is the training-repo half only — loading an S1-era
  shard under the current stamp — and it is tracked in `SpireTrainer`.

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
- 2026-08-26 — TE.3 added (handicap-assisted deep-state generator), per
  the same-day plan §4.3 amendment; T2.1 gains the TE.3 bank source +
  provenance-breakdown acceptance line, T2.2 gains the pre-registered
  assist-annealing fallback sentence. T2.x edits mirrored verbatim into
  `SpireTrainer/docs/training-tasks.md` per its tracked-in-both-places
  rule.
- 2026-09-03 (night) — T1.7 (`ee2af52`) landed in SpireTrainer: three
  non-durable generations end to end, four integration defects found and
  fixed; block, T2.2's Inherited line and deferred rows mirrored; GT1's
  T1.7 item ticked.
- 2026-09-03 (evening) — T2.1 (`806fadd`) and T1.3 (`7c18297`) landed in
  SpireTrainer; blocks, Inherited lines and deferred rows mirrored verbatim.
  One row is ENGINE-owned and new: `engine::RunController` is not portable
  between processes (`RunController::lists` holds `std::string_view`s into
  registry statics) — the durable fix is ids in `MonsterLists`, an engine task.
- 2026-09-03 (later still) — T1.4s landed in SpireTrainer (`0d5484e`): 1.26M
  floor-boundary rows under SIM_SEARCH/SIM_SEARCH_SKIP, value artifact
  `v0s.1`; block + rows mirrored verbatim.
- 2026-09-03 (later) — T1.1b and T1.2 landed in SpireTrainer (`33a99a0`, `4f731d4`);
  their Log text and T1.2's three deferred rows mirrored here verbatim. The
  same-day T1.x amendments below were mirrored into the SpireTrainer ledger.
- 2026-09-03 — *shortest path to the first training loop* (owner-directed:
  reach training as early as possible). Mirrors the same-day plan change-log
  entry: T1.4 renamed V0h and demoted to an accelerant; **T1.4s** (sim-fitted
  V0s) and **T1.7** (non-durable tracer-bullet loop) added; GT1's V0 bar
  satisfiable by either V0; T2.1's dep relaxed from GT1 to T1.2; T1.3 gains
  the GPU-environment Inherited line; TE.3 carries a priority note; the
  capacity rule restated for the post-S2-G2 world. The T1.x edits are to be
  mirrored verbatim into `SpireTrainer/docs/training-tasks.md` at the next
  training-repo landing (tracked-in-both-places rule).
- 2026-09-02 — the `CMAKE_SOURCE_DIR` deferred obligation is DISCHARGED on the
  sim side: the engine now builds correctly under `add_subdirectory` from a
  foreign top level, guarded by `tools/check_embed_consumer.sh` and written up
  in conventions §8. SpireTrainer's own ExternalProject→add_subdirectory switch
  remains, tracked in its ledger.
