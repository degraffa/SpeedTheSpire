# Slay the Spire Agent — Implementation Plan
 
**Scope of this document:** the concrete build plan for the simulator (design → verified implementation → performance hardening) and the training-experiment program that runs on it. Design decisions are stated as decisions, with rationale and exit criteria. Guiding principles are collected at the end. Difficulty target is **Ascension 20 from day one** (rationale in §2.4).
 
**Ground rules established elsewhere and assumed here:** hierarchical agent (combat MCTS + acquisition scoring + pathing expectimax + meta-value function), imitation prior from public run data, hardware = 5800X3D/5070 Ti primary + 3600X sim node, cloud burst optional and gated on profiling.
 
---
 
## Part 0 — Training Design First (planned before the simulator, executed after)
 
The simulator is a product with one customer: the training loop. So we specify the customer's requirements first, and let them dictate simulator architecture. This section is the contract.
 
### 0.1 What the trainer will actually do to the simulator
 
**Combat training (MCTS self-play).** The search will: snapshot a combat state thousands of times per decision, roll forward under determinized draw orders, evaluate leaves via batched GPU inference, and restore. Requirements imposed: O(memcpy) state snapshot/restore; explicit, forkable RNG streams so a rollout can fix the shuffle stream while sampling others; a step API that can advance *heterogeneous batches* of states; states compact enough that ~10k live states per worker stay L3-resident.
 
**Counterfactual resets.** Experiments will repeatedly ask "same run state, different choice" — e.g., re-simulate a fight 500 times from the same pre-combat state for the gauntlet evaluator, or branch a run at a card-reward screen. Requirement: any state, at any decision point, is constructible from bytes alone — no hidden globals, no ambient singletons, no state living outside the struct. This single requirement drives most of the architecture below.
 
**Full-run generation.** Fine-tuning consumes complete A20 runs at reduced search budgets. Requirement: the *entire* run loop (Neow through boss, including map, events, shops, rest sites, rewards with pity counters) must exist in the fast simulator — a sim that covers only combat would force the meta-level to train against the slow real game, which the compute math forbids.
 
**Trajectory schema.** Every component trains from the same logged representation. Requirement: a versioned, fixed-layout binary state encoding, defined *before* simulator code is written, used identically by the simulator, the CommunicationMod bridge (real-game states translate into the same schema), the replay buffer, and the networks' feature encoders. Schema version is stamped into every trajectory; loaders refuse mismatches.
 
### 0.2 Throughput contract
 
Derived from the experiment budget (10–30 full-run experiments of ~300k runs each, plus combat self-play of 1–10M encounters, on 8+6 local cores):
 
| Metric | Target | Floor (acceptable) |
|---|---|---|
| Combat steps/sec/core (interpreter, no NN) | 150k | 50k |
| Full combats/sec/core (random policy) | 1,000 | 300 |
| Full A20 runs/sec, whole 5800X3D, 25-sim MCTS | 1.0 | 0.4 |
| State snapshot cost | one memcpy ≤ 4KB | ≤ 8KB |
| Sim → schema serialization | zero-copy | ≤ 1µs |
 
These numbers are the Stage C exit criteria. They are set so that a 300k-run experiment completes in ≤ 4 days on the primary machine alone.
 
### 0.3 Decisions the training design forces on the simulator
 
D0.1 — **Batch-of-states is the only public API.** Single-game convenience wrappers may exist for tests, but the engine's contract is `advance(states[], actions[]) -> (states[], observations[], rewards[])`.
 
D0.2 — **The simulator is headless-only and UI-ignorant from the first commit.** No rendering hooks, no frame concepts, no animation timing. Anything in the base game that exists for presentation (queued VFX ordering that does not affect outcomes) is explicitly out of scope; anything presentation-adjacent that *does* affect outcomes (action-queue resolution order — it does) is in scope.
 
D0.3 — **Observation encoding lives inside the simulator.** The NN feature encoder (int8/fp16 tensors for the inference server) is a simulator module compiled with the engine, so encoding is one pass over the flat state with no intermediate allocation, and Python never touches hot-path bytes.
 
---
 
## Part 1 — The Simulator
 
No usable headless, high-performance StS implementation exists; we build one, using the game's source as the behavioral reference and the desktop game (via CommunicationMod) as the runtime oracle.
 
### Stage A — High-Level Design (Weeks 1–4)
 
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
 
**Decision: Rust, unless you are materially more fluent in C++.** The deciding factor is failure mode, not speed: the worst project outcome is a memory bug that corrupts one state in ten million and shows up as unexplainable training divergence months later. Rust converts that class of bug into compile errors. Escape hatch: the effect-interpreter inner loop may use `unsafe` blocks after the safe version is proven correct, benchmarked, and differential-tested.
 
**A.2 — GPU role: none in the simulator, by design, with the door held open.** All rule execution on CPU. The GPU serves NN inference exclusively. The architecture nevertheless preserves the port option: flat SoA-compatible state, batch semantics, and effect dispatch as a bytecode interpreter — exactly the megakernel shape a CUDA port wants. Re-evaluation trigger: only if Stage C targets are met and profiling still shows experiment throughput gated on rule execution (not inference, not I/O) — then port the *combat inner loop only*, validated by the same diff harness against the CPU engine.
 
**A.3 — Core architecture (accuracy and speed are the same design here).**
 
*State:* one trivially-copyable struct per game phase (CombatState ~2–4KB; RunState ~4–8KB embedding map, deck, relics, counters, and all RNG streams). Fixed-capacity arrays with counts (deck ≤ 128 cards, ≤ 5 monsters, ≤ 24 powers/entity — assert on overflow). No pointers, no heap types. Snapshot = memcpy; hash = fast pass over contiguous bytes (transposition tables and diff-testing both use this).
 
*Rules-as-data:* card/relic/power behavior expressed as opcode sequences in constexpr tables generated from a single YAML/CSV registry (id, cost, targeting, effect program, upgrade delta), interpreted by one dispatch loop. The registry doubles as documentation and as the checklist for verification coverage. Genuinely bespoke mechanics (Snecko randomization, Necronomicon, Dead Branch chains) get named native branches — budget ~15% of cards needing custom code.
 
*Action-queue fidelity:* the base game resolves effects through GameActionManager's queue, and resolution order is gameplay-relevant (power triggers, on-draw effects, relic ordering by acquisition index). The simulator implements an equivalent explicit queue with the same ordering semantics, derived from the source — this is the one piece of the game's *engine* (not just rules) we replicate rather than simplify, because most subtle behavioral divergences live here.
 
**A.4 — RNG: exact replication of the base game's mechanism.** This is a correctness cornerstone, specified precisely:
 
- **Generator:** libGDX `RandomXS128` — xorshift128+ with libGDX's specific seeding scramble (murmurhash3-style mix of the input seed). Implement it bit-exactly; unit-test the raw stream against captured Java outputs (first 10k longs for a battery of seeds) before anything else is built on it.
- **Named streams, not one RNG:** replicate the game's full set — among them `monsterRng`, `aiRng`, `shuffleRng`, `cardRandomRng` (in-combat card randomness: Snecko, discoveries), `monsterHpRng`, `potionRng`, `treasureRng`, `relicRng`, `eventRng`, `merchantRng`, `cardRng` (reward pools), `miscRng`, plus map-gen. Each stream's **counter is part of the state struct**, and streams are restorable as (seed, stream-id, counter) triples — this is what makes both seed-replay diffing and determinized MCTS forking possible.
- **Correlated/per-floor reseeding:** the base game re-derives several streams per floor as a function of (run seed, floor number) rather than advancing one global stream — which is why, e.g., unrelated actions on one floor don't perturb the next floor's event roll, and why some outcomes are correlated in ways players exploit. Replicate this derivation exactly, from source, stream by stream; document each stream's lifecycle (run-scoped vs floor-scoped vs combat-scoped) in the registry.
- **Pity/counter systems ride on top:** rare-card pity offsets, potion-drop counter (±10% steps), and shop pricing draw from specific streams with persistent counters stored in RunState.
- **Seed format:** accept both raw signed-64 seeds and the game's base-35 display alphabet, converting exactly as `SeedHelper` does, so a seed copied from the desktop game addresses the identical run in the simulator.
Acceptance test for this whole section: for N=100 seeds, the simulator's Neow options, act-1 map, every combat's enemy rolls and shuffles, and every reward must match the desktop game action-for-action. RNG divergence anywhere is a stop-the-line bug.
 
**A.5 — Scope ladder (sim side).** S1: Ironclad, Act 1, all A20 modifiers, full Neow/map/events/shops/rest/rewards for Act 1. S2: Acts 2–3 + boss-relic swaps. S3: Act 4, keys, Heart. S4: other characters. Each scope tier ships with its verification suite before the next begins; the trainer consumes S1 the moment it passes.
 
**A.6 — Source-code usage protocol.** The decompiled source is the *specification*; the implementation is a re-expression, not a transliteration (both for licence hygiene in a personal project and because transliterating Java object soup would forfeit the performance design). Working rule: for every registry entry and engine subsystem, cite the source class/method it was derived from in a `provenance` field — when the diff harness finds a divergence, the first debugging step is re-reading the cited Java.
 
### Stage B — Implementation with Continuous Verification (Weeks 4–16)
 
The rule: **no card, relic, or subsystem is "done" until its verification exists.** Verification is not a phase after implementation; it is the definition of implemented.
 
**B.1 — The oracle bridge.** A harness driving the desktop game headlessly-ish via CommunicationMod + a rendering-stripped fork: it can (a) start a run on a chosen seed, (b) inject an action sequence, (c) dump full game state as JSON after every action. A translator maps that JSON into our binary schema. This bridge is built *first* — before mass rule implementation — because every later test rides on it. Budget real time here; CommunicationMod state dumps won't cover everything (RNG counters, some hidden counters), so the fork gets a small patch exposing them.
 
**B.2 — Four verification tiers, cheapest first.**
 
1. *RNG stream tests:* bit-exact comparison of each named stream against captured Java outputs across seeds and floor-derivations. Run in milliseconds; gate every commit.
2. *Registry unit tests:* per card/relic/power, a table-driven test asserting effect outcomes in constructed states (damage math with strength/vulnerable/weak stacking order, block interactions, trigger ordering). Derived from source reading; catches transcription errors without needing the oracle.
3. *Seed-replay differential tests:* the workhorse. For a seed, drive *identical action sequences* through oracle and simulator; diff full states after every action. Action sequences come from three generators: random-legal policy (breadth), current agent policy (adversarial coverage — the agent gravitates to exploitable divergences), and directed scripts targeting known-nasty interactions. Any field mismatch = auto-filed bug with (seed, action-prefix) reproducer, which is a permanent regression test once fixed.
4. *Distributional tests:* for stochastic aggregates that are awkward action-for-action (map-shape statistics across 10k seeds, reward-rarity frequencies vs pity spec), chi-square comparisons against oracle-collected distributions.
**B.3 — Continuous operation.** The 3600X node runs tiers 1–3 continuously: a fuzz farm cycling seeds 24/7, feeding a divergence dashboard (diffs found per million actions, coverage per registry entry). CI on every commit runs tiers 1–2 plus a 50-seed replay smoke test.
 
**B.4 — Definition of done for S1 (Act 1/Ironclad/A20):** 1M+ fuzzed actions across ≥ 2,000 seeds with zero state diffs; 100% registry coverage by tier-2 tests; all A20 modifiers verified (elite/boss HP and damage tables, curse in starting deck, reduced potion/gold/healing rules — each is a registry entry with provenance); throughput ≥ floor targets (hardening to *target* levels is Stage C, but floors must hold here so training can start).
 
### Stage C — Performance Hardening (Weeks 14–18, overlapping B's tail)
 
Entry condition: S1 verified. Protocol: benchmark-driven, one hypothesis at a time, never trading verified behavior for speed — the diff suite runs after every optimization, no exceptions (fast-but-wrong is the project's death mode).
 
Measurement rig: fixed benchmark workloads (10k-combat batch, 1k-full-run batch) in CI, reporting steps/sec/core, IPC, L3 miss rate, branch miss rate via `perf stat`. The two health metrics from the design: L3 misses ≈ 0 (if not: states got fat, or allocation crept in) and branch misses low (if not: re-bucket dispatch by encounter/effect).
 
Ordered optimization backlog (stop when §0.2 targets are met): confirm zero hot-loop allocations (arena audit); dispatch bucketing — sort work by (encounter, phase) so warps of identical effects run consecutively; observation-encoding fusion into the step loop; SMT on/off A-B on the 5800X3D (uncertain win with a cache-resident loop — measure, don't assume); `-march=native`/LTO/PGO pass; only then micro-SIMD of damage/block math if still short. Exit: targets met on both the 5800X3D and (scaled) the 3600X node, results pinned in CI so regressions fail the build.
 
---
 
## Part 2 — Training Program on the Simulator
 
### 2.1 Experiment ladder
 
Each rung has an entry gate (what must exist), a success metric, and a cheap config. All rungs run at A20 rules.
 
**E0 — Baselines (gate: S1 sim).** Random-legal and scripted-heuristic agents (simple priority scripts) through Act 1. Purpose: reference numbers for every later claim, and adversarial fuzz traffic for B.3. Metric: none to beat; these *are* the floor.
 
**E1 — Imitation (gate: schema + datasets; runs before/parallel to sim completion).** Train acquisition, pathing-prior, and meta-value nets on human data filtered to high-ascension runs (A15+ where volume allows, weighting A20). Metric: held-out decision accuracy vs A20 winners; qualitative review of picks in curated scenarios.
 
**E2 — Combat agent, Act 1 pool (gate: S1 + E1 nets).** MCTS self-play over Act-1 encounters at A20 stats, deck contexts sampled from human-run distributions. Metric: beats scripted baseline and matches/exceeds human HP-loss distributions per encounter on a frozen (deck, encounter, seed) benchmark suite.
 
**E3 — Assembled hierarchy, Act 1 (gate: E2).** Full Act-1 runs: pathing expectimax with known tables, acquisition net, combat agent. First measure the *untrained assembly* — that number is the baseline all fine-tuning must beat. Then fine-tune meta-value + acquisition on self-generated Act-1 runs, with the gauntlet evaluator (simulated boss/elite win-rates) as auxiliary value targets. Metric: Act-1 completion rate and exit-state quality (HP, deck strength per gauntlet) on 500 frozen seeds, verified by real-game replay of a sample.
 
**E4 — Full game (gate: S2/S3 sim tiers).** Extend to Acts 2–3, then keys + Heart. Same pattern: assemble, baseline, fine-tune, verify in the real game.
 
### 2.2 End-to-end simulation vs staged scope — the explicit trade
 
Arguments for full-game e2e from the start: no distribution mismatch at act boundaries (an Act-1-only agent overvalues short-horizon tempo because its episodes end at the act boss); the meta-value's whole job is pricing long-horizon scaling, which truncated episodes can't teach.
 
Arguments for staged (chosen): simulator verification is the schedule's long pole, and Act 1 verified is months earlier than Act 3 verified; every pipeline bug is cheaper to find on short episodes; E2/E3 results are meaningful and motivating early.
 
**Resolution: staged execution with e2e-aware objectives.** Act-1 training uses a *bootstrapped horizon*: episodes end at the act boss but are rewarded by the imitation-pretrained meta-value's assessment of the exit state (deck/relics/HP as priced by full-run human outcomes), not by mere completion. This imports the long-horizon signal before the long-horizon simulator exists, and is precisely the mechanism that prevents "Act-1 myopia." When S2 lands, the bootstrap is replaced by real continuation, and E3 policies are re-baselined rather than trusted.
 
### 2.3 Experiment hygiene (mechanics, not principles)
 
Frozen seed sets per rung (E2: 2k combat seeds; E3/E4: 500 run seeds), never trained on. Miniature config (reduced card pool, 25-sim search, 50-seed eval) as the mandatory first pass for every idea; full config only after a mini-config win. One-page journal entry per experiment: hypothesis, config hash, sim version, result, verdict. Behavioral dashboards (elites fought/act, deck size, rest ratios, HP-per-relic exchange) with A20-human sanity bands; out-of-band = automatic replay review. Any simulator fix invalidates results trained on the buggy version — retrain, don't rationalize.
 
### 2.4 Why A20 from day one
 
Adopted, with the reasoning made explicit: difficulty changes the *optimal policy*, not just the score. A0-optimal play is materially laxer — sloppier blocking, greedier picks, cheaper elite math — and curriculum from A0 would spend compute learning habits A20 punishes, then more compute unlearning them (negative transfer). Training at A20 also matches the best available imitation data (top players' public runs skew A20) and makes every benchmark comparable to the most-studied human baselines.
 
Costs, and their mitigations: terminal wins are rare at A20 (strong humans sit roughly 10–50% depending on character and player), so pure win/loss signal is sparse — mitigated by the bootstrapped meta-value (dense, human-anchored) and gauntlet auxiliary targets, which is exactly why those exist in the design. Evaluation must therefore never be win-rate alone: HP-adjusted progress, act-completion rates, and gauntlet-scored exit states are the tracked metrics, with win rate as the headline that moves last. One honest fallback is pre-registered: if E3 fine-tuning shows no improvement over the assembled baseline after three full-config attempts, run a *diagnostic* A10 arm — not as curriculum, but to distinguish "signal too sparse" from "pipeline broken."
 
---
 
## Part 3 — Guiding Principles (the short version, governing everything above)
 
**Verification is the product.** The simulator's value is trust, not speed; speed is worthless the day the agent finds a divergence to exploit. Hence: no rule without its test, continuous fuzzing, and real-game replay for all headline numbers.
 
**The trainer is the customer.** Every simulator decision traces to a training requirement (Part 0). When a sim convenience conflicts with a trainer need, the trainer wins.
 
**Determinism everywhere.** Same seed + same actions + same binary = same bytes, across machines. This is what makes bugs reproducible, experiments comparable, and the oracle usable.
 
**Bake in what is known; learn only what is hard.** Probability tables, RNG structure, and map search are engineered; deck evaluation and exchange rates are learned. Never spend model capacity on facts the source code states.
 
**Feedback-loop latency is the real budget.** Mini-configs, walking skeletons, staged scope, and behavioral dashboards all exist to shorten the time between an idea and its verdict. Protect that loop before protecting throughput.
 
**Fail loudly.** Asserts on every invariant, stop-the-line on RNG divergence, schema-version refusal, out-of-band behavior flags. The expensive failures in this project are the silent ones.
 
---
 
## Milestones
 
| # | Milestone | Exit evidence | Target week |
|---|---|---|---|
| M1 | Design doc frozen + walking skeleton | Skeleton passes diff harness on 5-card micro-game | 4 |
| M2 | Oracle bridge + RNG bit-exactness | Tier-1 tests green across 100 seeds, incl. floor reseeding | 6 |
| M3 | S1 rules complete | 100% registry tier-2 coverage | 12 |
| M4 | S1 verified | 1M actions / 2k seeds, zero diffs; floor throughput | 16 |
| M5 | Perf targets met | §0.2 table green in CI on both machines | 18 |
| M6 | E1 imitation nets shipped | Held-out accuracy report + scenario review | 14 (parallel) |
| M7 | E2 combat agent | Beats baselines on frozen combat suite | 24 |
| M8 | E3 assembled + first fine-tune win | Improvement over assembly baseline on 500 seeds, real-game-verified sample | 30 |
| M9 | S2 sim + E4 kickoff | Acts 2–3 verified; re-baselined hierarchy | 34+ |
 
Weeks assume 10–15 hobby hours plus unattended machine time; 1.5× slippage is normal and the gates matter more than the dates.
