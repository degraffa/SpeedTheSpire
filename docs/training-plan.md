# Training Plan — Player-Information Layer + A20H Program (Phase T)

**Status:** landed 2026-08-01; binding for Phase T. This document is
the binding spec for the Phase T ledger in
[training-tasks.md](training-tasks.md), amendable only via an entry in its
change log below. It refines Part 2 of
[InitialPlan.md](../InitialPlan.md); where the two disagree on training
mechanics, this document governs Phase T execution and InitialPlan.md remains
intent/rationale. Where this document touches engine internals, the frozen
design docs and [conventions.md](conventions.md) win on any overlap.
Simulator-side work it mandates (the information layer) lands in this repo
under the existing conventions; everything downstream of tensors lands in the
separate training repo that task T1.1 creates.

Objective, unchanged from InitialPlan D0.8: maximize mean P(A20 Heart kill)
on fresh seeds, Ironclad first, under UI-equivalent information and a declared
search/inference budget. Terminal reward 1/0, no discounting, no alternative
utilities. Meta-goal: minimum wall-clock to maximum win rate.

What this plan keeps from InitialPlan Part 2: exact-simulator expert
iteration (no learned dynamics, no two-player "self-play" framing), A20 rules
from day one, one value currency, a hard information contract with leak
tests, enumerated-legal-action policy heads, three disjoint seed populations
with paired evaluation, quarantine-on-simulator-bug. What it changes is
listed in §8.

---

## 1. The information contract

**Public = derivable by a perfect-memory player from the revealed
action–observation history.** Not "currently on screen."

**Rules vs realizations.** The boundary hides RNG *realizations*, never game
*rules*. Move-selection tables, reward odds, pity formulas, and ?-room
distributions are public knowledge (community-documented, derivable from
play); only outcomes are hidden. An agent that knows Gremlin Nob's exact move
weights is informed, not clairvoyant — the same reason it may know card
damage values. Concretely: the belief sampler and search use the engine's
real AI and reward code on sampled hidden state, so search-derived targets
embed exact rule knowledge conditioned only on public history.

**Independence is the contract, not an approximation.** The true generative
process reads several streams from literally the same `Random(seed)` sequence
at different offsets, and floor-scoped streams are functions of
`(seed, floor)` (stage-a-design.md §3.4). Exploiting that identity requires
inferring the shared 64-bit state — seed-cracking — which a player cannot do
and the evaluation contract bans. The sampler therefore draws streams
independently: the max-entropy posterior given player information. This is a
normative definition, and the sampler's distributional tests validate against
*this contract's* closed-form conditionals, not against seed-correlated
reality (§2.6d).

Most of the perfect-memory tracker already exists as engine state and goes
into the observation verbatim: `card_blizz_randomizer` /
`blizzard_potion_mod` (run_state.hpp:170-171), `event_pity_*` and the
event/shrine membership bitsets (run_state.hpp:188-213), `purge_cost`,
per-monster `move_history`, and the encounter-list cursors (on
`RunController`, run_advance.hpp:268-275). Encoding them is information
availability, not a strategy heuristic.

## 2. The player-information layer (simulator repo)

### 2.1 `PublicView`

A versioned POD produced by a pull call beside `legal_actions()` — do not
fatten `StepResult` (it embeds `ObsBuffer` by value on the hot path). Per
phase:

- **Always:** hp/max-hp/gold/floor/ascension, master deck (multiset), relics
  with displayed counters, potions, the full current-act map including the
  burning-elite node (modeled in S1; the key *item* is S3), all pity and
  membership counters, consumed encounter prefix.
- **Combat:** everything the 240-byte stub (observation.hpp) omits — player
  powers, monster block, full per-monster power lists (the stub truncates at
  4 of 24 slots), draw pile as an unordered multiset except
  KnowledgeState-constrained positions, discard/exhaust contents. The
  encoder is audited field-by-field against `CombatState`; a silently
  missing field cripples learning invisibly.
- **Screens:** direct projection of the reward/shop/event/Neow/rest
  transient structs. One trap: `TreasureChest` rolls contents at
  construction; the player pre-open sees only chest size — masked until the
  open action.
- **The legal-action mask is an observation channel.** A legality bit
  computed from hidden state is a leak the observation-equality test cannot
  catch. `RunActionMask` bytes are part of the hashed public serialization
  and sit under the same twin tests.
- **Forward-compatibility decided now:** the v1 layout reserves zero-filled
  fields for known S2/S3 structure — keys bitflags (already in `RunState`,
  run_state.hpp:161), the boss-relic choice screen, act index to 4, the
  second-boss slot — and declares which schema changes are additive vs
  breaking, so S1-era checkpoints, shards, and eval snapshots stay
  forward-readable when Acts 2–4 land.

### 2.2 `KnowledgeState`

The only genuinely new tracking: draw-pile position knowledge, maintained
*inside the engine* at placement/reveal/shuffle events (only the engine
knows when a reveal happens). Two S1-real subtleties: (a) Frozen Eye is in
the S1 registry (relics.yaml), so full-order reveal is needed now; (b)
random-position insertion (Wild Strike, Reckless Charge) after a known
placement (Headbutt) makes the exact posterior a uniform interleaving
preserving relative order — so KnowledgeState tracks **relative order
constraints**, not absolute indices. It also records revealed monster
construction rolls (e.g., Louse bite damage, rolled at spawn and revealed by
the first attack intent).

### 2.3 Observability transforms as data

Runic Dome intent-hiding stays at its current observation write site. A
two-member transform vocabulary in code (`HIDE_INTENT`,
`REVEAL_DRAW_ORDER`); registry rows declare membership via an
`observability:` field. No more machinery than S1 needs.

### 2.4 `resample_hidden(state, sampler_rng)` — the belief sampler

Observations here are noiseless deterministic reveals and every hidden
quantity's conditional law given public history is available in closed form:
constrained re-seeding is **exact posterior sampling under the declared
contract** — no particle filtering, no weights, no degeneracy. Per-source
treatment:

| Hidden source | Treatment |
|---|---|
| Draw-pile order | Uniformly permute unknown slots subject to KnowledgeState order constraints. Sampler-private RNG only — never spend engine-stream draws, or particle counters desync from public history |
| Future intents | Fresh `aiRng`; `move_history` is public and the engine's own move-legality rules constrain continuations |
| In-combat randomness | Fresh `cardRandomRng` / `miscRng` |
| Encounter-list suffix | **Condition, don't reroll:** generation is sequential with last-one/two exclusions, i.e. Markov (encounters.hpp) — continue the chain past the observed prefix with a fresh stream and overwrite the stored suffix in `RunController.lists`. The boss list conditions on `boss_list[0]` (public from the map) — load-bearing for the A20 double boss in S2 |
| Relic-pool remainders | Uniformly re-permute the unrevealed `[0,count)` window per tier (front-pop rewards / end-pop shop preserved). Pop-time `canSpawn` rechecks create corner cases where the remainder is not exactly "initial multiset minus observed pops" — handled in the sampler, verified by the distribution tests |
| Mid-event hidden state (Match & Keep board) | Pin revealed flips, permute the rest — a dedicated row because "fresh streams" contradicts observed flips mid-event |
| ?-rooms, future rewards/shops/chests/potions | Fresh streams; pity counters and membership bitsets preserved (public) |
| Monster HP / construction rolls | Public once revealed; fresh for un-entered fights |
| Current-visit shop stock, Neow options, act map, chest size | Public — copy |
| `mapRng` | Fresh per particle (the emerald-elite entry buff is a live mid-run `mapRng` draw; copying the true stream would fuse it across particles) |
| Floor-stream derivation, future acts | Each particle gets a **fresh fake run seed**, so `seed+floor` reseeds regenerate fake futures automatically (every engine consultation reads the state's own `run_seed`; no ambient true seed). Valid at particle construction time. `map_stream` for future acts is the same pattern, currently uncalled — S2 inherits it |

### 2.5 `public_hash`

xxh3 over `PublicView` + mask bytes, keying search statistics by information
state. Omniscient/debug access to raw state keeps a *different function
name*, so a grep — not vigilance — enforces the boundary.

### 2.6 Leak gates

Sim-repo CI, per commit: **(a)** hidden-twin byte equality — a
`make_hidden_twin()` utility asserting identical `PublicView` bytes across
twins in every phase; the same utility generates fixtures the training repo
consumes. **(b)** total-byte classification tripwire — every byte of
`RunController` classified public/hidden/derived (`run_seed` itself:
hidden), failing when `sizeof` grows unclassified. **(c)** reveal-timing
tests (chest/reward masking before vs after the revealing action; mask
derivation audited from public state only). Nightly: **(d)** sampler
distribution tests against the contract's closed-form conditionals (Markov
chain continuation, uniform remaining-permutation, per stream, on short
prefixes and marginals) — *not* against seed-filtered reality, which the
contract deliberately excludes; a small bounded seed-filtered sanity check
runs only where acceptance rates permit. Training repo, promotion-gating:
**(e)** policy-logit invariance across twins, **(f)** search-statistic
invariance with pinned sampler seed, **(g)** a probe predicting hidden facts
from observations, gated on **no advantage over a reference predictor given
the belief marginals** — not absolute chance, because e.g. a Cultist's next
intent is legitimately ~100 % predictable from public information.

### 2.7 Repo boundary

**Semantics vs tensors.** Simulator repo: `PublicView`, `KnowledgeState`,
transforms, `resample_hidden`, `public_hash`, twin/leak/sampler tests —
leak-freedom is an engine correctness property tested against engine
internals. Training repo: dtypes, normalization, token layouts, embeddings,
networks, search — everything that churns weekly and must not pay this
repo's verification ceremony. (This narrows InitialPlan D0.3: the engine
owns the *view*, not the tensor encoder; C++ actors linking the engine get
zero-copy access either way.)

## 3. Agent architecture

### 3.1 Model

One network, two trunks, shared content embeddings keyed by the registry's
append-only, static_assert-pinned u16 ids, with headroom (~2,048 rows per
domain) so S2–S4 content appends without remapping. Combat trunk: set/entity
transformer over card tokens (id, zone, cost-now, upgrade, order-constraint
flag), player powers, monster tokens (hp/block/intent/history/powers),
relics, potions, run context. Run trunk: deck/relic/potion tokens, a map-DAG
encoder over the visible act map, current-screen tokens. Policy head scores
the enumerated legal actions (verb, source, target, option) — the engine's
`ActionMask` / `RunActionMask` already enumerates exactly this shape.

Heads: **V = P(Heart kill | public information)** is the sole decision
currency. Auxiliary heads: exit-HP *distributions*, death probability,
per-upcoming-fight outcome distributions, act/Heart reach, resource deltas.
Distributional heads are load-bearing (§3.3). The standalone
encounter-outcome model is deferred to Phase T4+ — at Act-1 scale the run
trunk's per-upcoming-fight aux heads give path screening without a second
learned-model lifecycle.

Start small (~10–25M params); search multiplies model quality and every
parameter costs leaf throughput. Size up only after the actor profile
exists.

Deck-vs-future-threats evaluation (elites, act boss, future bosses, Heart)
is provided as *information*, not encoded strategy: the observation carries
the visible map, act-boss identity, encounter-pool residues, and key state;
aux heads learn per-upcoming-fight outcome predictions; the run planner
queries V under candidate routes. Where a threat-priority ordering is real,
it emerges from V.

### 3.2 Search

**Combat: public-belief tree search.** Tree keyed by `public_hash`; each
simulation draws a hidden world from `resample_hidden`; simulations use
exact engine transitions inside the sampled world while statistics aggregate
per information state — POMCP's tree discipline without its particle
apparatus; the strategy-fusion guard is the public keying. Budget algebra:
sims/decision means **total leaf evaluations**. Branching density: every
card draw is a reveal, so below the first end-turn the public tree branches
too densely for deep expansion at small budgets — coarsen by evaluating V at
reveal afterstates rather than expanding past dense chance nodes. Root
selection: **Gumbel sequential halving** (principled policy improvement at
small visit counts; its fixed root-heavy schedule batches better on GPU than
PUCT's serial root dependence); PUCT in-tree; PUCT-at-root is the ablation.
The exact configuration — total evals, candidate count, halving schedule,
per-simulation world sampling vs a shared scenario bank — is a **declared
output of the T1.3 spike's sweep**, not a prior commitment.

**Run level: structured expectimax, not full-run tree search.** The act map
is a small visible DAG; reveal distributions (?-room pity, encounter
residue, reward odds) are known public quantities. Sparse expectimax with V
at the leaves; shops/removals as small sequence searches; imminent fights
get real combat search; distant fights get aux-head estimates.

**Adaptive budget everywhere:** policy-only for forced/low-entropy
decisions; escalate on value-at-stake × uncertainty. The final agent is
net + search; policy-only is a reported baseline.

### 3.3 Tail risk: objective unchanged, estimator repaired

Maximizing mean P(win) is the complete objective; tail risk is priced
automatically, and CVaR-style utilities would double-count risk and reject
necessary gambles. The real hazard is the optimizer's curse: search
argmaxes over noisy value estimates, argmax-over-noise is biased toward
overestimated lines, and value nets are least accurate in rare low-HP tail
states. Mitigations, all uncertainty-handling rather than strategy: (i) back
predicted exit-HP/death *distributions* into run-level leaf values at
irreversible decisions; (ii) a last-layer ensemble on the value head, with
disagreement as an **epistemic-only** proxy driving lower-confidence-bound
selection at irreversible nodes — ablation-gated, coefficient tuned on
paired win rate (a vanilla-ensemble LCB penalizing aleatoric variance would
recreate the CVaR mistake); (iii) promotion-gate calibration reports
**conditioned on HP bucket and act**.

## 4. Training program

### 4.1 The currency ladder

Each rung is a versioned artifact with a pre-registered validation event:

- **V0** — run-level P(win | floor, HP, deck, relics, gold) fit on the
  public human run dataset (~77M runs; wins *and* losses; weighted to high
  ascension). Known biases: human skill, human deck distributions,
  2018–2020 patch vintage.
- **V1** — re-fit on the agent's own Act-1 runs (bootstrapped horizon:
  episodes end at the act boss, exits valued by current V). The V0→V1 delta
  is the first *coarse signal* about human-data bias (it conflates skill
  bias, bootstrap error, and distribution shift — a signal, not a
  measurement).
- **V2** — re-fit on real Acts 1–3 continuations when S2 lands, with a
  pre-registered re-baseline delta and a threshold that triggers retraining
  downstream heads from scratch rather than fine-tuning.
- **V3** — the true terminal signal (Heart kill) when S3 lands.

Combat expert iteration always maximizes **E[V_k(exit RunState)]**. The
combat/run seam is total — HP, gold, potions, max-HP changes, relic
counters all fold back into `RunState` — so V over RunState prices every
combat externality with no hand-coded exchange rates. **Data lifecycle
rule:** at each currency bump or engine fix, shards are either reanalyzed
under the new V (targets refreshed, features kept) or quarantined by
sim-commit range — decided per event, never silently discarded.

### 4.2 Human data policy

Pays: V0; macro-decision imitation priors (card pick, path, Neow, shop,
event — wins and losses); encounter-outcome pretraining from per-fight
`damage_taken` records ({floor, enemies, damage, turns} — hundreds of
millions of fights). Bonus the dataset's `seed_played` field enables:
floor-scoped streams are pure functions of (seed, floor), so
**floor-boundary states of human runs are largely reconstructible by macro
replay**, making search-relabeling of human macro decisions feasible;
within-combat states are not reconstructible (no action logs). Cannot pay:
combat actions (absent from the dataset — combat is search-bootstrapped,
full stop). Hygiene: verify the dump carries `seed_played` and player
identifiers before promising player-disjoint splits; treat patch vintage as
a measured bias (validate V0 against sim rollouts).

### 4.3 Phase ladder

Phases and gates are the Phase T ledger's structure
([training-tasks.md](training-tasks.md)); summary:

- **T0 (sim repo) and TE (engine-track coordination):** T0 is the
  information layer of §2; TE, in parallel, is survival-biased drivers
  feeding the G7 campaign and S2 planning opened immediately (content
  authoring has zero dependency on training results; every week S2 slips,
  the headline slips).
- **T1 (training repo bootstrap):** repo scaffold; trajectory schema with
  full version stamps and refuse-on-mismatch loaders; the **actor
  throughput spike** whose sole job is measuring R (NN evals/s at
  production batch) and t_enc (observation encode cost) — the two
  unmeasured numbers every budget hangs on; dataset ingestion + tabular V0;
  eval harness. **No durable training before the leak gates are green and R
  is measured.**
- **T2 (combat expert iteration, parallel with S2):** snapshot bank from
  survival-biased play stratified by floor/deck/HP; from-scratch ExIt with
  the exploration kit stated up front (Gumbel root sampling,
  action-sampling temperature, replay freshness — first-generation collapse
  is the classic failure); leaf currency V0→V1; exactly-solvable
  micro-combats as permanent ground-truth search tests; branch-K
  counterfactual resets with common random numbers at macro decisions.
- **T3 (Act-1 integrated agent):** macro imitation heads;
  encounter-outcome pretraining; run-level expectimax planner;
  bootstrapped-horizon full-run training valued by V1; behavioral
  dashboards.
- **T4 (S2 lands):** PublicView additive extension; V2 re-fit with
  pre-registered re-baseline (expect macro heads to be re-baselined, not
  fine-tuned — truncated-horizon tempo bias is real); combat
  re-distillation; double-boss content.
- **T5 (S3 lands, headline):** keys/Act 4/Heart; V3; the full flywheel —
  generation at modest search, teacher search allocated by
  uncertainty/impact, reanalysis under the newest net, failure mining, and
  every exploit-suspicious high-value line replayed through the real-game
  oracle before it is trusted.
- **T6 (optional):** other characters — shared pretraining, per-character
  tuning. Off the critical path.

### 4.4 Capacity rule

T0's sim-side deliverables, G7 closure, and S2 authoring compete for the
same sim-repo orchestrator capacity. Priority order: **G7 close →
PublicView layer → S2 authoring.** S2/S3 verification gates must be
satisfiable by *scripted* survival drivers, with trained checkpoints an
accelerant — otherwise combat training lands on the engine critical path,
reversing the intended dependency arrow.

## 5. Systems

The simulator is effectively free (27.16M combat steps/s/core; 204,749
random-policy A20 runs/s whole-machine — B5.5). Random runs average ~47
steps because they die by floor 7; plan against **D ≈ 1,500 decisions per
competent run**. On the owned hardware (5800X3D + one 5070 Ti; 3600X
secondary), R for a 10–25M-param entity transformer is plausibly
**30k–100k evals/s** — a 3× bracket, which is why measuring R is the
spike's whole purpose. At 48 total evals/decision: ~0.7–2.4 GPU-s per run,
~35k–120k generated runs/GPU-day, teacher tier ~10× less, a 10k-paired-run
eval ~2–7 GPU-hours. Rough cycle sketch at the pessimistic bracket: ~10
generations × 100k runs × ~1 GPU-s ≈ 12 GPU-days per full-run ExIt cycle
plus comparable teacher/eval overhead — feasible on one good GPU,
comfortable with a modest cloud burst. One 16-thread CPU box generates
leaves for ~15–75 GPUs' worth of demand: interpreter micro-optimization is
off the critical path; the meaningful Stage-C benchmark is *achieved NN
evals/s at production batch*.

Architecture: C++ actor processes statically linking the engine; hundreds
of concurrent searches per worker (concurrency *across* searches fills
B ≥ 512 batches); lock-free leaf queues → per-GPU fp16 inference
(TensorRT/LibTorch); Python owns optimization and orchestration only;
atomic weight hot-swap. Search uses the mask-supplied `advance` overload
(+38.0 % / +84.3 % documented at advance.hpp:319-323); a small slice of the
fleet runs the debug preset permanently so mask-contract violations fail
loudly in production. Replay shards: fixed-layout POD records (public obs,
mask, sparse search distribution, action, outcome, aux targets) plus a
**restricted sidecar** of full states stored as keyframes + action logs —
determinism makes intermediates reconstructible, cutting sidecar volume
from ~18 MB/run naive to ~1 MB/run (~100 GB/day per GPU-scale actor fleet).
Reconstruction needs the exact engine binary, so quarantined sim-commit
ranges strand their sidecars — accepted and logged. Instrument from day
one: per-decision time breakdown (encode/step/copy/tree/NN-wait),
batch-size and queue-wait histograms, steps-per-run per weights version,
learner ingest vs actor production.

## 6. Evaluation and promotion

Three disjoint seed populations (dev smoke; frozen paired-validation;
untouched/rotating holdout). All comparisons on identical seeds, paired
bootstrap or McNemar; the decision-relevant quantity is the *paired
difference*, whose SE depends on discordance (up to ~0.7 pp at 10k pairs if
uncorrelated; better in practice, and better still while win rates sit at
5–20 % early). Promotion ladder:

1. **Frozen decision suite** — ~10k stratified snapshots (lethal puzzles,
   path forks, shops, Neow, elite-entry HP states), scored in seconds,
   reported per category. **Split to avoid circularity:** exactly-solvable
   cases gate hard on ground truth forever; search-labeled cases carry
   *versioned* labels re-derived on every champion upgrade and gate on
   agreement-*trend* regressions — a never-refreshed suite gated hard would
   fossilize early-network strategy.
2. Policy-only win rate.
3. Paired standard-search eval.
4. Weekly full three-tier report (policy-only / standard / max-search) with
   compute per decision attached.

Gates: paired improvement at declared budget; no suite regression per the
split rule; HP-bucketed calibration sane; probe gate at reference-predictor
parity; sampled real-client replays zero-diff. Headline: A20H win rate at
declared budget on untouched seeds. Dashboards: act/Heart reach,
conditional Heart-kill, death floor/cause, key timing, potion economy,
calibration/Brier.

## 7. Critical path and top risks

The critical path is **engine content**: G7 → S2 → S3 — serial simulator
work, gut-estimated at 5–8 months (a planning prior, not a promise). Every
training-track item fits inside that shadow *if started now and the §4.4
capacity rule is enforced* — so the day S3 lands, headline training starts
with a pretrained combat net, calibrated currency, tuned search, proven
actor loop, and eval harness.

Top risks: (1) **S2/S3 verification methodology** — random-volume campaigns
cannot verify late-act content; gates must be coverage-cohort-based and
satisfiable by scripted survival drivers. (2) **Currency miscalibration
compounding** — versioned re-fits on schedule; calibration as a blocking
gate. (3) **Actor throughput shortfall** — every budget hangs on R; measure
it first, and if achieved batch size collapses, restructure search
scheduling before touching anything else.

## 8. Deltas vs InitialPlan.md Part 2

1. Belief machinery: POMCP/particle anchor dropped; `resample_hidden`
   constrained re-seeding is exact under the declared contract;
   public-history keying stays as the strategy-fusion guard; D0.5's
   verification demand stays.
2. E1 demoted from gate to accelerant: combat ExIt gates on the contract +
   S1 + a minimal tabular V0 only. Added: per-fight damage-record
   pretraining of encounter heads (the committed plan never sources them
   from human data).
3. Currency ladder replaces the frozen meta-value, with re-baseline deltas,
   retrain thresholds, and the reanalyze-vs-quarantine lifecycle rule.
4. Risk mechanism added, objective unchanged: distributional heads
   load-bearing + epistemic-only ensemble-LCB (ablation-gated) +
   HP-bucketed calibration gates.
5. Repo boundary stated: semantics vs tensors; D0.3 narrowed.
6. Sequencing: S2 authoring starts now; survival drivers feed G7 now; the
   spike measures R before any budget is believed; PublicView reserves
   S2/S3 fields now.
7. Search defaults: Gumbel-at-root with the budget algebra pinned and
   reveal branching coarsened; PUCT-at-root as ablation; run level is
   expectimax over the visible DAG, not full-run tree search.
8. Encounter-outcome model deferred to T4+.
9. Housekeeping — stop-the-line per conventions §4, so the same change
   that lands this document must patch InitialPlan.md: it still "decides
   Rust" in a C++ repo, still targets ≤4KB snapshots (CombatState is
   4,696 B at S1 schema v6, against an 8 KB ceiling), and carries a
   superseded weeks schedule. **Discharged:** the landing change patched
   all three (A.1 decision resolution, §0.2 snapshot row, stage-heading
   calendars) and added the Part 2 execution pointer.

## Change log

- 2026-08-01 — document created (proposed, uncommitted).
- 2026-08-01 — landed; the §8.9 InitialPlan.md patches were discharged in
  the same change per conventions §4.
