# Slay the Spire Agent — Implementation Plan
 
**Scope of this document:** the concrete build plan for the simulator (design → verified implementation → performance hardening) and the training-experiment program that runs on it. Design decisions are stated as decisions, with rationale and exit criteria. Guiding principles are collected at the end. Difficulty target is **Ascension 20 from day one** (rationale in §2.4).
 
**Ground rules established elsewhere and assumed here:** hierarchical agent
(public-information combat search + structured run planning + one canonical
Heart-win value), imitation warm-start from public run data, exact simulator
dynamics rather than a learned world model, hardware = 5800X3D/5070 Ti primary
+ 3600X sim node, cloud burst optional and gated on profiling. The final agent
is the model **plus** search; policy-only play is a latency baseline, not the
maximum-win-rate endpoint.
 
---
 
## Part 0 — Training Design First (planned before the simulator, executed after)
 
The simulator is a product with one customer: the training loop. So we specify the customer's requirements first, and let them dictate simulator architecture. This section is the contract.
 
### 0.1 What the trainer will actually do to the simulator
 
**Combat training (public-information expert iteration).** This is a
single-player stochastic control problem, so "self-play" is the wrong model:
search improves the current policy, and the network distils that improved
policy. Search will snapshot a combat state thousands of times per decision,
sample hidden-state particles consistent with what the player has observed,
evaluate leaves via batched GPU inference, and restore. Requirements imposed:
O(memcpy) state snapshot/restore; explicit, forkable RNG streams; a first-class
belief sampler for hidden draw order / intent / future pools; a step API that
can advance *heterogeneous batches* of states; states compact enough that ~10k
live states per worker stay L3-resident. "Determinization" always means a fresh
compatible sample for a rollout — never the live state's true hidden future.

**Fair-information boundary.** Raw simulator state is omniscient: it includes
RNG state, shuffled pile order, hidden intent under Runic Dome, and pre-shuffled
future pools. Neither the network nor a search-tree key may consume those bytes.
The trainer acts from a versioned public observation plus a public knowledge
state (observable history and legitimately known card positions). Search nodes
are keyed by public action-observation history; exact-state hashes are allowed
only inside a sampled particle. A separate omniscient/debug agent is useful as
an upper bound but is never mixed into headline results.
 
**Counterfactual resets.** Experiments will repeatedly ask "same run state, different choice" — e.g., re-simulate a fight 500 times from the same pre-combat state for the gauntlet evaluator, or branch a run at a card-reward screen. Requirement: any state, at any decision point, is constructible from bytes alone — no hidden globals, no ambient singletons, no state living outside the struct. This single requirement drives most of the architecture below.
 
**Full-run generation.** Fine-tuning consumes complete A20 runs at reduced search budgets. Requirement: the *entire* run loop (Neow through boss, including map, events, shops, rest sites, rewards with pity counters) must exist in the fast simulator — a sim that covers only combat would force the meta-level to train against the slow real game, which the compute math forbids.
 
**Trajectory schema.** Every record carries two deliberately different
representations: (1) the exact versioned simulator snapshot used by the oracle,
replay, counterfactual reset, and reanalysis; and (2) the versioned
player-visible observation / knowledge state used by the network. The full
snapshot is a restricted sidecar and must never become an accidental model
feature. Records also stamp the simulator commit, state schema, observation
schema, action schema, registry/rules hash, model id, and search configuration;
loaders refuse incompatible versions.
 
### 0.2 Throughput contract
 
Derived from the experiment budget (10–30 full-run experiments of ~300k runs
each, plus 1–10M self-generated/search-labelled combat encounters, on 8+6 local
cores):
 
| Metric | Target | Floor (acceptable) |
|---|---|---|
| Combat steps/sec/core (interpreter, no NN) | 150k | 50k |
| Full combats/sec/core (random policy) | 1,000 | 300 |
| Full A20 runs/sec, whole 5800X3D, 25-sim MCTS | 1.0 | 0.4 |
| State snapshot cost | one memcpy; the `combat_state.hpp` static_assert ceiling (raised with schema growth) is the live bound | ≤ 8KB |
| Sim → schema serialization | zero-copy | ≤ 1µs |
 
These numbers are the Stage C exit criteria. They are set so that a 300k-run experiment completes in ≤ 4 days on the primary machine alone. (Stage B's B5.5 acceptance already measured the simulator-only floors far above target; the open Stage C question is the actor path — encoding, belief sampling, search, inference — not the interpreter.)

The 25-simulation row is the mandatory cheap configuration and throughput
baseline, not a declaration that 25 simulations is the final agent. Stage C
also measures the complete actor path — public encoding, belief sampling,
search, batched neural inference, and trajectory writing — and publishes a
win-rate/latency curve for policy-only, normal-search, and maximum-search
budgets. No fixed final search budget is chosen before that profile exists.
 
### 0.3 Decisions the training design forces on the simulator
 
D0.1 — **Batch-of-states is the only public API.** Single-game convenience wrappers may exist for tests, but the engine's contract is `advance(states[], actions[]) -> (states[], observations[], rewards[])`.
 
D0.2 — **The simulator is headless-only and UI-ignorant from the first commit.** No rendering hooks, no frame concepts, no animation timing. Anything in the base game that exists for presentation (queued VFX ordering that does not affect outcomes) is explicitly out of scope; anything presentation-adjacent that *does* affect outcomes (action-queue resolution order — it does) is in scope.
 
D0.3 — **Observation encoding lives inside the simulator.** The NN feature encoder (int8/fp16 tensors for the inference server) is a simulator module compiled with the engine, so encoding is one pass over the flat state with no intermediate allocation, and Python never touches hot-path bytes.

D0.4 — **Public observation is not raw state.** The engine exposes a
player-visible observation for every run phase and a separate public-information
hash. It omits unrevealed order, RNG internals, future outcomes and hidden
intent, while representing legitimate knowledge such as Frozen Eye order or
cards deliberately placed on top of the draw pile.

D0.5 — **Belief sampling is a verified simulator service.** Search asks for
hidden-state particles conditional on public history; it does not call ad-hoc
`shuffle()` code. Each hidden category has preserved constraints and
distributional tests against fresh simulator seeds.

D0.6 — **The policy scores an enumerated legal-action list.** The simulator
continues to execute the packed `Action`, but the trainer logs stable semantic
descriptors (verb, source entity, target entity, option identity) and the
behavior probability. The model never learns legality and never relies on one
giant fixed action vocabulary.

D0.7 — **The exact simulator is the dynamics model.** Use the useful
AlphaZero/Expert-Iteration loop (policy/value prior → stronger search → distil)
without learning MuZero dynamics. Learned encounter-outcome models may
accelerate run planning, but exact combat search remains their teacher and
validation oracle.

D0.8 — **The objective and information/compute contract are frozen together.**
The headline is mean probability of an A20 Act-4 Heart kill on fresh seeds,
under UI-equivalent information and a declared search/inference budget. Terminal
reward is 1 for a Heart kill and 0 otherwise, with no discount. HP, act reached,
death risk and resource outcomes are auxiliary predictions, not alternative
utilities.
 
---
 
## Part 1 — The Simulator
 
No usable headless, high-performance StS implementation exists; we build one, using the game's source as the behavioral reference and the desktop game (via CommunicationMod) as the runtime oracle.
 
### Stage A — High-Level Design (landed; stage-a-design.md is the frozen record)
 
Deliverable: a design document freezing the decisions below, plus a walking skeleton — one enemy, five cards, full RNG plumbing, batch API, and the diff harness connected end to end. The skeleton exists to prove the architecture before mass card implementation begins.
 
**A.1 — Language: Rust or C++.** Both meet the performance bar; the decision criteria are correctness economics and your fluency.
 
| Criterion | C++ | Rust |
|---|---|---|
| Peak perf of the interpreter loop | Equal | Equal |
| Memory-corruption risk (silent trainer poison) | Real; a stray write corrupts states *quietly* | Largely eliminated at compile time |
| Fearless threading for the worker pool | Manual discipline | Enforced by the type system |
| Ecosystem for this project (PyO3/pybind, criterion/google-bench, cargo vs CMake) | Mature | Mature, less build-system friction |
| Direct transliteration from Java reference | Slightly more natural | Requires more idiom translation |
| Future CUDA port of combat kernel | First-class | Via FFI boundary (fine — the kernel would be C++/CUDA either way) |
 
**Decision: Rust, unless you are materially more fluent in C++ — resolved: C++20, the fluency clause applied.** The original deciding factor was failure mode, not speed: the worst project outcome is a memory bug that corrupts one state in ten million and shows up as unexplainable training divergence months later, which Rust converts into compile errors. The C++ execution compensates with the mitigations the repo now carries: the oracle diff harness, byte-identical builds across three compilers, ASan/UBSan presets on both hosts, and the continuous fuzz soak. The comparison table is preserved above for the record.
 
**A.2 — GPU role: none in the simulator, by design, with the door held open.** All rule execution on CPU. The GPU serves NN inference exclusively. The architecture nevertheless preserves the port option: flat SoA-compatible state, batch semantics, and effect dispatch as a bytecode interpreter — exactly the megakernel shape a CUDA port wants. Re-evaluation trigger: only if Stage C targets are met and profiling still shows experiment throughput gated on rule execution (not inference, not I/O) — then port the *combat inner loop only*, validated by the same diff harness against the CPU engine.
 
**A.3 — Core architecture (accuracy and speed are the same design here).**
 
*State:* one trivially-copyable struct per game phase (CombatState ~2–4KB; RunState ~4–8KB embedding map, deck, relics, counters, and all RNG streams). Fixed-capacity arrays with counts (deck ≤ 128 cards, ≤ 5 monsters, ≤ 24 powers/entity — assert on overflow). No pointers, no heap types. Snapshot = memcpy; hash = fast pass over contiguous bytes (transposition tables and diff-testing both use this).
 
*Rules-as-data:* card/relic/power behavior expressed as opcode sequences in constexpr tables generated from a single YAML/CSV registry (id, cost, targeting, effect program, upgrade delta), interpreted by one dispatch loop. The registry doubles as documentation and as the checklist for verification coverage. Genuinely bespoke mechanics (Snecko randomization, Necronomicon, Dead Branch chains) get named native branches — budget ~15% of cards needing custom code.
 
*Action-queue fidelity:* the base game resolves effects through GameActionManager's queue, and resolution order is gameplay-relevant (power triggers, on-draw effects, relic ordering by acquisition index). The simulator implements an equivalent explicit queue with the same ordering semantics, derived from the source — this is the one piece of the game's *engine* (not just rules) we replicate rather than simplify, because most subtle behavioral divergences live here.
 
**A.4 — RNG: exact replication of the base game's mechanism.** This is a correctness cornerstone, specified precisely:
 
- **Generator:** libGDX `RandomXS128` — xorshift128+ with libGDX's specific seeding scramble (murmurhash3-style mix of the input seed). Implement it bit-exactly; unit-test the raw stream against captured Java outputs (first 10k longs for a battery of seeds) before anything else is built on it.
- **Named streams, not one RNG:** replicate the game's full set — among them `monsterRng`, `aiRng`, `shuffleRng`, `cardRandomRng` (in-combat card randomness: Snecko, discoveries), `monsterHpRng`, `potionRng`, `treasureRng`, `relicRng`, `eventRng`, `merchantRng`, `cardRng` (reward pools), `miscRng`, plus map-gen. Each stream's **counter is part of the state struct**, and streams are restorable as (seed, stream-id, counter) triples — this makes seed-replay diffing exact and lets the belief sampler construct controlled public-history-compatible particles without exposing the live hidden future.
- **Correlated/per-floor reseeding:** the base game re-derives several streams per floor as a function of (run seed, floor number) rather than advancing one global stream — which is why, e.g., unrelated actions on one floor don't perturb the next floor's event roll, and why some outcomes are correlated in ways players exploit. Replicate this derivation exactly, from source, stream by stream; document each stream's lifecycle (run-scoped vs floor-scoped vs combat-scoped) in the registry.
- **Pity/counter systems ride on top:** rare-card pity offsets, potion-drop counter (±10% steps), and shop pricing draw from specific streams with persistent counters stored in RunState.
- **Seed format:** accept both raw signed-64 seeds and the game's base-35 display alphabet, converting exactly as `SeedHelper` does, so a seed copied from the desktop game addresses the identical run in the simulator.
Acceptance test for this whole section: for N=100 seeds, the simulator's Neow options, act-1 map, every combat's enemy rolls and shuffles, and every reward must match the desktop game action-for-action. RNG divergence anywhere is a stop-the-line bug.
 
**A.5 — Scope ladder (sim side).** S1: Ironclad, Act 1, all A20 modifiers, full Neow/map/events/shops/rest/rewards for Act 1. S2: Acts 2–3 + boss-relic swaps. S3: Act 4, keys, Heart. S4: other characters. Each scope tier ships with its verification suite before the next begins; the trainer consumes S1 the moment it passes.
 
**A.6 — Source-code usage protocol.** The decompiled source is the *specification*; the implementation is a re-expression, not a transliteration (both for licence hygiene in a personal project and because transliterating Java object soup would forfeit the performance design). Working rule: for every registry entry and engine subsystem, cite the source class/method it was derived from in a `provenance` field — when the diff harness finds a divergence, the first debugging step is re-reading the cited Java.
 
### Stage B — Implementation with Continuous Verification (landed through M3; G7 open in stage-b-tasks.md)
 
The rule: **no card, relic, or subsystem is "done" until its verification exists.** Verification is not a phase after implementation; it is the definition of implemented.
 
**B.1 — The oracle bridge.** A harness driving the desktop game headlessly-ish via CommunicationMod + a rendering-stripped fork: it can (a) start a run on a chosen seed, (b) inject an action sequence, (c) dump full game state as JSON after every action. A translator maps that JSON into our binary schema. This bridge is built *first* — before mass rule implementation — because every later test rides on it. Budget real time here; CommunicationMod state dumps won't cover everything (RNG counters, some hidden counters), so the fork gets a small patch exposing them.
 
**B.2 — Four verification tiers, cheapest first.**
 
1. *RNG stream tests:* bit-exact comparison of each named stream against captured Java outputs across seeds and floor-derivations. Run in milliseconds; gate every commit.
2. *Registry unit tests:* per card/relic/power, a table-driven test asserting effect outcomes in constructed states (damage math with strength/vulnerable/weak stacking order, block interactions, trigger ordering). Derived from source reading; catches transcription errors without needing the oracle.
3. *Seed-replay differential tests:* the workhorse. For a seed, drive *identical action sequences* through oracle and simulator; diff full states after every action. Action sequences come from three generators: random-legal policy (breadth), current agent policy (adversarial coverage — the agent gravitates to exploitable divergences), and directed scripts targeting known-nasty interactions. Any field mismatch = auto-filed bug with (seed, action-prefix) reproducer, which is a permanent regression test once fixed.
4. *Distributional tests:* for stochastic aggregates that are awkward action-for-action (map-shape statistics across 10k seeds, reward-rarity frequencies vs pity spec), chi-square comparisons against oracle-collected distributions.
**B.3 — Continuous operation.** The 3600X node runs tiers 1–3 continuously: a fuzz farm cycling seeds 24/7, feeding a divergence dashboard (diffs found per million actions, coverage per registry entry). CI on every commit runs tiers 1–2 plus a 50-seed replay smoke test.
 
**B.4 — Definition of done for S1 (Act 1/Ironclad/A20):** 1M+ fuzzed actions across ≥ 2,000 seeds with zero state diffs; 100% registry coverage by tier-2 tests; all A20 modifiers verified (elite/boss HP and damage tables, curse in starting deck, reduced potion/gold/healing rules — each is a registry entry with provenance); throughput ≥ floor targets (hardening to *target* levels is Stage C, but floors must hold here so training can start).
 
### Stage C — Performance Hardening (not yet opened; gated on G7)
 
Entry condition: S1 verified. Protocol: benchmark-driven, one hypothesis at a time, never trading verified behavior for speed — the diff suite runs after every optimization, no exceptions (fast-but-wrong is the project's death mode).
 
Measurement rig: fixed simulator workloads (10k-combat batch, 1k-full-run
batch) plus a trainer-facing actor benchmark, reporting steps/sec/core,
decisions/sec, neural evaluations/decision, belief particles/decision, GPU batch
occupancy, IPC, L3 miss rate, and branch miss rate. The two simulator health
metrics from the design remain L3 misses ≈ 0 (if not: states got fat, or
allocation crept in) and branch misses low (if not: re-bucket dispatch by
encounter/effect). The end-to-end benchmark is authoritative for training
capacity: once neural inference or orchestration dominates, faster rule
execution alone does not shorten experiments.
 
Ordered optimization backlog (stop when §0.2 targets are met): confirm zero hot-loop allocations (arena audit); dispatch bucketing — sort work by (encounter, phase) so warps of identical effects run consecutively; observation-encoding fusion into the step loop; SMT on/off A-B on the 5800X3D (uncertain win with a cache-resident loop — measure, don't assume); `-march=native`/LTO/PGO pass; only then micro-SIMD of damage/block math if still short. Exit: targets met on both the 5800X3D and (scaled) the 3600X node, results pinned in CI so regressions fail the build.
 
---
 
## Part 2 — Training Program on the Simulator

> **Execution note (2026-08-01):** Phase T execution of this part is governed
> by [docs/training-plan.md](docs/training-plan.md) (spec) and
> [docs/training-tasks.md](docs/training-tasks.md) (ledger); where they refine
> or contradict details below, they win. This part remains the
> intent-and-rationale record.

### 2.1 Experiment ladder
 
Each rung has an entry gate (what must exist), a success metric, and a cheap config. All rungs run at A20 rules.
 
**E−1 — Trainer contract (gate: S1 schema/API).** Ship the versioned
public observation and knowledge state, dynamic legal-action descriptors,
public-information hash, verified belief sampler, trajectory container, and
evaluation harness. Leak-invariance tests pair internal states that differ only
in hidden order/RNG/intent and require identical public encodings and direct
policy outputs. Belief-sampler tests pin conditional constraints and
frequencies. No durable model training begins before this gate.

**E0 — Baselines (gate: E−1 + S1 sim).** Random-legal,
scripted-heuristic, behavioral-cloning, policy-only, and small-search agents
through Act 1. Add a deliberately omniscient full-state agent as a debug upper
bound, labelled ineligible for headline comparison. Purpose: reference numbers
for every later claim, search-correctness tests on exactly enumerable
micro-combats, and adversarial fuzz traffic for B.3.

**E1 — Human/data bootstrap (gate: E−1 + datasets; runs before/parallel to
later sim scope).** Pretrain shared content embeddings, macro-policy priors, and
the meta-value on high-ascension human runs (A15+ where volume requires,
weighting A20). Use wins **and losses**, split by player and seed; winner-only
accuracy has survivorship bias and cannot calibrate win probability.
Counterfactually re-label selected human states with simulator search rather
than treating the recorded action as truth. Metrics: held-out action likelihood,
Heart/act value calibration, and curated scenario review.

**E2 — Combat agent, Act 1 pool (gate: S1 + E1).** Train a shared combat
policy/value from realistic deck/relic/HP contexts using high-budget
public-information search as teacher, then distil it into a cheaper student.
Contexts increasingly come from the current agent rather than permanently from
the human distribution. Metrics: direct policy beats scripts; belief search
measurably improves over the direct policy at equal states; distillation retains
most of that gain; and both improve downstream continuation value on a frozen
combat suite. HP loss and death distributions remain diagnostics, not the
objective.

**E3 — Assembled hierarchy, Act 1 (gate: E2).** Full Act-1 runs combine
structured map search, enumerated reward/shop/rest/event alternatives, the
combat agent, an encounter-outcome model, and one canonical meta-value. First
measure the untrained assembly. Then fine-tune on self-generated Act-1 runs,
with boss/elite gauntlets and resource-outcome predictions as auxiliary targets.
Metric: paired improvement in Act-1 exit continuation value over the assembly
baseline, plus real-game replay of a sample.

**E4 — Acts 2–3 continuation (gate: S2).** Replace the Act-1 bootstrap with
real continuation, re-baseline every component, and train through the A20 double
Act-3 boss. A changed combat policy invalidates the encounter-outcome model
until that model is refreshed from new combat results.

**E5 — A20 Heart (gate: S3).** Add key strategy, Act 4, Shield and Spear, and
the Heart; remove every act-boundary surrogate; run end-to-end search-guided
policy iteration. Primary metric: Heart-kill rate on the untouched A20H
evaluation population at a declared compute budget, with a real-client
zero-divergence sample.

**E6 — Other characters (gate: S4 + E5).** Share representation pretraining
where it helps, then allow character-specific fine-tuning/checkpoints for peak
win rate. Report each character independently; an aggregate never hides a weak
character.
 
### 2.2 End-to-end simulation vs staged scope — the explicit trade
 
Arguments for full-game e2e from the start: no distribution mismatch at act boundaries (an Act-1-only agent overvalues short-horizon tempo because its episodes end at the act boss); the meta-value's whole job is pricing long-horizon scaling, which truncated episodes can't teach.
 
Arguments for staged (chosen): simulator verification is the schedule's long pole, and Act 1 verified is months earlier than Act 3 verified; every pipeline bug is cheaper to find on short episodes; E2/E3 results are meaningful and motivating early.
 
**Resolution: staged execution with e2e-aware objectives.** Act-1 training
uses a *bootstrapped horizon*: episodes end at the act boss but are valued by a
frozen meta-value's assessment of the full exit state, not by mere completion.
This imports long-horizon signal before the long-horizon simulator exists.
When S2 lands, the bootstrap is replaced by real continuation and E3 policies
are re-baselined rather than trusted.

Curriculum changes **horizon and starting-state distribution, not ascension**:
exactly enumerable micro-combats → realistic combat snapshots → late-run/Heart
snapshots → act continuations → full runs, all under A20 rules. Snapshots come
from real or self-generated reachable states; impossible hand-authored decks do
not become the training distribution.

### 2.3 Experiment and evaluation hygiene

Three disjoint seed populations: a small development/smoke set; a large frozen
paired-validation population used for checkpoint promotion; and an untouched
or rotating final holdout, because repeated selection over a "never trained on"
validation set still overfits it. Model comparisons use the same game seeds and
planner-randomness streams with paired bootstrap or McNemar intervals. Near a
50% win rate, roughly 10k independent runs still gives only about
one-percentage-point precision; sub-point claims need tens of thousands, not a
story built from a few wins.

Every candidate is evaluated as three explicit agents: direct policy,
standard/adaptive search, and maximum-search. Report wall time, simulator nodes,
belief particles and neural evaluations per decision with win rate; model size
and search budget trade against one another, so a score without its compute
contract is incomplete.

Miniature config (reduced card pool, 25-simulation search, smoke evaluation) is
the mandatory first pass for every idea; full config only follows a mini-config
win. Each experiment records hypothesis, config hash, simulator commit, state
schema, observation schema, action schema, registry hash, model id, search
config, result, and verdict.
Behavioral dashboards track act/Heart reach, death floor/cause, elites,
deck size, keys, rest/smith ratio, potion use, shops, and value calibration.
Out-of-band behavior triggers replay review. Any simulator fix quarantines
affected trajectories and invalidates results trained on the buggy version —
retrain, don't rationalize.
 
### 2.4 Why A20 from day one
 
Adopted, with the reasoning made explicit: difficulty changes the *optimal policy*, not just the score. A0-optimal play is materially laxer — sloppier blocking, greedier picks, cheaper elite math — and curriculum from A0 would spend compute learning habits A20 punishes, then more compute unlearning them (negative transfer). Training at A20 also matches the best available imitation data (top players' public runs skew A20) and makes every benchmark comparable to the most-studied human baselines.
 
Costs, and their mitigations: terminal wins are rare enough that pure win/loss
supervision is sparse. Auxiliary heads predict act/Heart reach, combat survival,
death floor, exit-HP distribution, incoming damage, potion/resource use,
permanent gains, and clutter; targeted late-run starts and the bootstrapped
meta-value shorten credit assignment. These heads improve representation and
diagnosis but do not redefine utility: final action selection maximizes expected
Heart-win probability. One honest fallback is pre-registered: if E3
fine-tuning shows no improvement over the assembled baseline after three
full-config attempts, run a *diagnostic* A10 arm — not as curriculum, but to
distinguish "signal too sparse" from "pipeline broken."

### 2.5 Model and planner architecture

**Typed entity representation.** Use content embeddings and typed tokens rather
than a giant flat vector: global run/combat context; card instances with
upgrade/cost/zone and legitimately known position; player powers; monsters and
visible intent/history; relics in acquisition order; potions; the master deck;
the current offer/screen; and the visible map graph. Hidden draw contents are
represented as an unordered multiset except for positions the player actually
knows. Start with a modest entity/set transformer for combat and owned content,
plus a map-graph encoder; scale only after inference profiling.

**Dynamic action scoring.** Enumerate legal candidates and score each from the
global state, verb, source entity, target entity and option identity. This one
shape covers card×target, potion×target, end turn, reward/skip, map node,
master-deck selection, shop item, confirm and dialog choices. Truly forced UI
transitions may be auto-collapsed; sequential choices whose order changes state
remain real decisions.

**Two brains, one currency.** Combat and run planning use separate trunks with
shared content embeddings. Both ultimately estimate
`V(I) = P(A20H win | public information I, declared downstream compute)`.
Combat leaf evaluation includes full run context; fight victory with maximum HP
is not a sufficient objective because potions, Feed/Ritual Dagger, relic
counters and future paths matter. Card/relic/event choices enumerate
alternatives; shops beam-search purchase/removal sequences; map planning searches
the visible DAG and samples uncertain rooms. A learned encounter-outcome model
predicts death, exit HP, resource use and permanent gains to screen many paths;
exact combat search trains it and rechecks finalists.

Begin with a shared encounter-conditioned combat model. Per-encounter
specialists, adapters, mixture-of-experts, ensembles and per-character final
models are ablations after a shared baseline demonstrates where capacity or
negative transfer remains.

### 2.6 Public-information search

At a root information state, the belief sampler constructs compatible hidden
particles. The same scenario bank is used across candidate root actions for
variance reduction, but it is independent of the live state's true hidden
future. A simulation alternates deterministic player afterstates and exact
chance/RNG transitions. When an outcome becomes observable, the public-history
tree branches on that observation. Descendant policies cannot condition on
which particle was sampled unless its distinguishing fact has been revealed.

Use a POMCP-style public tree / particle search or an equivalent batched sparse
search first. Compare ordinary policy-guided PUCT against Gumbel sequential
halving at the root, which is attractive when the budget cannot visit every
action. Enumerate the ordinary legal action set; sampled-action methods are
reserved for a future genuinely combinatorial macro action. Use public hashes
for cross-particle statistics and exact hashes only for transpositions within a
particle. Search budget is adaptive: little or none for forced/high-margin
decisions, more for lethal turns, bosses, path forks, card rewards, shops and
high ensemble disagreement.

Method anchors: [Expert Iteration](https://arxiv.org/abs/1705.08439),
[POMCP](https://papers.nips.cc/paper/2010/hash/edfbe1afcf9246bb0d40eb4d8027d90f-Abstract.html),
[Gumbel policy improvement](https://openreview.net/forum?id=bERaNdoegnO), and
[Reanalyse](https://arxiv.org/abs/2104.06294). These are patterns to adapt, not
excuses to replace the exact simulator with a learned dynamics model.

### 2.7 Training flywheel and promotion

1. **Bootstrap** shared embeddings, macro priors and a calibrated value from
   human wins/losses plus heuristic and exact-search microcases.
2. **Teach** realistic states with expensive public-information search. Store
   the improved root distribution and action values, not only the chosen move.
3. **Distil** with policy cross-entropy, binary Heart-win value loss, an
   optional robust action-value/ranking loss, and auxiliary prediction losses.
   Search values at small budgets are noisy; do not force exact regression to
   every estimate.
4. **Generate** fresh A20 experience with the current student plus modest
   search. Spend the expensive teacher on high-entropy, ensemble-disagreement,
   rare-mechanic, high-impact and fatal states.
5. **Reanalyse** prioritized old states using the latest network/search so
   expensive full trajectories continue to improve their targets.
6. **Mine failures.** Deaths and suspicious high-value lines become permanent
   tactical cases and oracle-replay candidates. The trained agent is also an
   adversarial simulator fuzzer.

C++ actor/search workers link the simulator directly and submit leaves from
many unrelated roots to one asynchronous fp16/bf16 GPU inference service.
Python/PyTorch owns optimization, scheduling and analysis, never hot-path state
handling. If one GPU cannot learn and serve inference concurrently without
starvation, alternate collection and learner epochs before buying cloud
capacity. Profile the 3600X node for quantized CPU inference versus large
remote batches; never pay a LAN round trip per individual action.

A candidate promotes only when its paired Heart-win interval improves at the
declared compute budget, it does not regress the tactical suite, calibration and
behavioral diagnostics remain sane, and sampled real-client replays remain
zero-diff. The tactical suite prevents forgetting but never substitutes for the
untouched seed population.
 
---
 
## Part 3 — Guiding Principles (the short version, governing everything above)
 
**Verification is the product.** The simulator's value is trust, not speed; speed is worthless the day the agent finds a divergence to exploit. Hence: no rule without its test, continuous fuzzing, and real-game replay for all headline numbers.
 
**The trainer is the customer.** Every simulator decision traces to a training requirement (Part 0). When a sim convenience conflicts with a trainer need, the trainer wins.
 
**Determinism everywhere.** Same seed + same actions + same binary = same bytes, across machines. This is what makes bugs reproducible, experiments comparable, and the oracle usable.

**Fair information is part of correctness.** Simulator truth, player knowledge,
and model input are three different objects. Exact hidden state is retained for
replay and counterfactuals; policy and search decisions use only public
information and a verified belief over what remains hidden. A clairvoyant agent
is a failed experiment even if every simulator transition is correct.
 
**Bake in what is known; learn only what is hard.** Probability tables, RNG structure, and map search are engineered; deck evaluation and exchange rates are learned. Never spend model capacity on facts the source code states.

**One value currency.** Combat, pathing, acquisition, shops and events all
serve expected A20 Heart-win probability. Local HP, death risk, act progress and
gauntlet scores teach perception and shorten credit assignment; none becomes a
permanent competing objective.

**Search teaches and remains available at test time.** High-budget search
creates counterfactual supervision, the network amortizes it, and adaptive
search spends compute where the student is uncertain or the decision is
important. Policy-only strength is useful, but it is not the ceiling.
 
**Feedback-loop latency is the real budget.** Mini-configs, walking skeletons, staged scope, and behavioral dashboards all exist to shorten the time between an idea and its verdict. Protect that loop before protecting throughput.
 
**Fail loudly.** Asserts on every invariant, stop-the-line on RNG divergence, schema-version refusal, out-of-band behavior flags. The expensive failures in this project are the silent ones.
 
---
 
## Milestones
 
| # | Milestone | Exit evidence | Depends on |
|---|---|---|---|
| M1 | Design doc frozen + walking skeleton | Skeleton passes its independent diff harness | — |
| M2 | Oracle bridge + RNG bit-exactness | Tier-1 stream/floor tests and live bridge green | M1 |
| M3 | S1 rules complete | Registry tier-2 coverage gate | M2 + registry |
| M4 | S1 verified | Frozen Stage-B zero-diff, coverage, A20, fuzz and throughput gates | M3 |
| M5 | Simulator + actor performance targets met | §0.2 simulator floors plus end-to-end search/inference profile green | M4 |
| M6 | E−1 trainer contract shipped | Public observation/knowledge/action schemas; leak-invariance and belief-distribution tests; versioned replay/eval harness | stable S1 API |
| M7 | E0/E1 baselines and bootstrap shipped | Baseline report; player/seed-disjoint human-data policy and calibrated value report | M6 + datasets |
| M8 | E2 combat agent | Direct policy beats scripts; belief search adds paired value; distilled student retains the gain | M6–M7 + S1 |
| M9 | E3 integrated Act-1 agent | Paired improvement over the assembled baseline; real-game-verified sample | M8 |
| M10 | S2 + E4 continuation | Acts 2–3 verified; surrogate removed; hierarchy re-baselined through the double boss | M9 + S2 |
| M11 | S3 + E5 A20H agent | Keys/Act 4/Heart complete; untouched-seed Heart-win report at declared compute; real-client zero-diff sample | M10 + S3 |
| M12 | S4 + E6 character agents | Per-character A20H reports after shared pretraining and character-specific tuning | M11 + S4 |

The milestones are dependency gates, not calendar promises. Feedback-loop
latency and acceptance evidence determine sequencing; unattended compute is
scheduled only after the corresponding miniature experiment wins.

---

## Training-plan revision note — 2026-07-29

The original hierarchy, A20-first stance, human-data warm-start, meta-value,
batched simulator contract, staged scope and real-game verification remain.
The training plan now makes five previously implicit requirements explicit:

1. Raw simulator state and player-visible model input are separate versioned
   artifacts.
2. Hidden-state determinization is a verified public-history-conditioned belief
   sampler, not the live future copied into MCTS.
3. The useful learning loop is exact-model expert iteration with optional
   adaptive test-time search, not two-player "self-play" or learned MuZero
   dynamics.
4. Every module optimizes expected A20 Heart-win probability; HP, risk and
   gauntlet outputs are auxiliary.
5. A headline result is inseparable from its information contract, seed split,
   statistical interval, simulator/version provenance and compute budget.
