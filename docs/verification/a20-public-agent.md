# SIM_SEARCH_PIMC — the A20 public-information agent (TE.4)

Written 2026-10-10. This is the acceptance evidence for
`PolicyKind::SIM_SEARCH_PIMC` (`tools/fuzz/include/sts/fuzz/policy.hpp`,
`tools/fuzz/src/policy.cpp`, `tools/fuzz/src/policy_search.cpp`): a scripted
Ironclad agent that reads only public information and kills the A20 Act-1
boss on **87.8 %** of held-out seeds, against 35.4 % for the omniscient
`SIM_SEARCH` and 4.6 % for its information-limited twin `SIM_SEARCH_BLIND`
([sim-search-blind.md](sim-search-blind.md)). Scan TSVs are regenerable from
the commands in §6 and stay out of the tree.

## 0. Why

No learned agent in the training program had killed the A20 Act-1 boss at a
measurable rate (best: 2 of 2,000 runs, SpireTrainer T3.7), and the only
scripted policy that did, `SIM_SEARCH`, rolls its candidates out over the TRUE
controller — hidden draw order, future monster rolls and the true next floor
behind every map/event choice. A public agent that wins is both the honest
baseline for an information-limited learner and a teacher it can actually
imitate (T3.9's imitation of the privileged `SIM_SEARCH` plateaued for that
reason).

## 1. What the agent is

Everything below is gated on `is_public_agent(kind)` (true for
`SIM_SEARCH_PIMC` only); every other kind's code path is untouched (§4).

* **Particles.** Every rollout runs on a `resample_hidden` particle, never on
  `rc`. A decision draws K particles (`STS_PIMC_K`, default 8) from fresh
  `PolicyRng` draws and scores every candidate against all K (common random
  numbers); band-scored screens draw none. Boss combat uses K x 8
  (`STS_PIMC_BOSS_MULT`); elites K (`STS_PIMC_ELITE_HALVES`, in halves, default 2).
* **Combat.** A turn-plan search: each candidate is scored by the best plan that
  finishes the current turn after it (in-turn play / hand-select sequences up
  to END_TURN, 24 completions per particle), with a chance cut at draws and
  in-combat RNG rolls, scored OPEN-LOOP across particles (a plan is identified
  by its public end state, so a candidate is worth the best plan's particle
  average, not the average of each particle's own best plan). Leaves are the
  scripted completion to the end of the fight, which for this kind focuses the
  live monster with least HP+block, blocks only up to the probed incoming
  damage, and leads with Weak/Vulnerable. Terminals are GRADED (a win scores
  remaining HP, a loss scores how much of the board was still standing) instead
  of the inherited +1e9/-1e12, so a fight lost in every particle still orders
  its candidates. Potions are priced at 22 HP before the Act-1 boss. No boss
  2-ply (under particles its inner max is a clairvoyant bonus).
* **Cards.** A per-card Ironclad value table (base/upgraded, 0..100), a take
  bar of 20, a duplicate discount, Act-1-boss-specific bonuses read from the
  public `boss_list[0]`, upgrade = largest upgrade gain, purge/transform = lowest
  value.
* **Run layer.** Shop basket buying (relics, potions, table-priced cards, purge
  of the lowest-value card); a whole-act path rule over the public map that
  always walks a path with the fewest elites, then an exact HP-walk DP as the
  path value; one-room horizon for map/event rollouts; Neow takes the boss-relic
  swap; campfire rests below 90 % HP before the boss and 70 % elsewhere, else
  smiths; Fruit Juice / Entropic Brew drunk out of combat, other potions held;
  a boss-relic reward screen is left like a combat reward screen (fixes a
  floor-17 claim/skip 2-cycle, 22 livelocks per 1,000 runs -> 0).

## 2. Headline — held-out A20 grid

`STS700000`–`STS702499` (the [sim-search-blind.md](sim-search-blind.md) grid;
never scanned during development), A20, policy seed 0, `STS_PIMC_K=8`:

| policy | Act-1 boss KILL | Act-1 boss FIGHT | mean max floor |
|---|---:|---:|---:|
| `sim_search` (omniscient) | 884 / 2,500 (35.4 %) | 1,702 (68.1 %) | 16.47 |
| `sim_search_blind` | 115 / 2,500 (4.6 %) | 1,001 (40.0 %) | 11.58 |
| `sim_search_pimc`, particles only (`f6d87ef`) | 492 / 2,500 (19.7 %) | 1,713 (68.5 %) | 15.26 |
| **`sim_search_pimc` (landed)** | **2,194 / 2,500 (87.8 %, Wilson 86.4–89.0)** | **2,430 (97.2 %)** | **26.51** |

Paired against `sim_search` on the same seeds: 1,385 seeds killed only by the
public agent, 75 only by `sim_search` (McNemar p ≈ 1e-312). Boss win rate when
reached: Hexaghost 710/808 (87.9 %), The Guardian 739/835 (88.5 %), Slime Boss
745/787 (94.7 %). Only 70 of 2,500 runs die before the boss floor. Act 2:
806 Act-2 boss fights, 139 kills. Every row ends `run_over`; no livelock, no
failure.

These counts were measured on `c08ed70` with `STS_PIMC_K=8` set explicitly. The
landing commit only changes the default (`kPimcDefaultWorlds`) to 8, so
`seed_scan` with no environment reproduces them.

## 3. How it got there — dev grid

All tuning used `STS800000`–`STS800999` (A20, policy seed 0, K=8 unless noted),
Act-1 boss kills out of 1,000. Each lever was built in its own worktree from
`f6d87ef`, then independently re-verified: rebuilt from the committed tree,
reproduced byte-identically, audited for hidden-state reads and gating.

| step | commit | kills |
|---|---|---:|
| `sim_search` / `sim_search_blind` (reference) | master | 378 / 48 |
| particles only, K=4 / 8 / 16 | `f6d87ef` | 151 / 190 / 199 |
| lever: cards | `fd04ea0` | 330 |
| lever: run layer | `d25769b` | 384 |
| lever: combat evaluation (graded terminals 235, boss 1-ply x3 356, potion hold 364) | `6a7cb05` | 364 |
| lever: combat search (turn-plan, open-loop, boss x8) | `422e9ca` | 336 |
| run-layer pair (548 merged, +shop cards 571, +table purge 592) | `d9e21bc` | 592 |
| combat pair (boss x8, elite x1) | `dfbaa11` | 474 |
| all four | `c9252ac` | 860 |
| + boss-relic screen livelock fix (Act-1 column unchanged) | `837aea0` | 860 |
| + card take bar 30 -> 20 | `23411b5` | 883 |
| + potion-score overflow fix, `STS_TWIN_CHECK` | `c08ed70` | 883 |

The levers compound: the run layer delivers the agent to the boss (reach 70 %
-> 98 %) with a deck worth fighting with, and the combat work wins the fight
(boss win rate 27 % -> 90 %). Measured and not kept, among others: K=16 on the
landed agent (882 vs 883, 2x CPU), boss x4 (840), elite path rule off (850), a
leaner Act-1 deck (bar 42: 257 vs 318 at the lever), wider path room weights,
racing / successive halving for boss particles. The take bar and card table
were tuned on these seeds; the held-out 87.8 % against the dev 88.3 % is the
check that the tuning did not overfit.

## 4. Invariance, determinism, cost

* `sim_search,sim_search_blind` on `STS700000`–`STS700199` is byte-identical
  (`cmp`) to the master build after every lever, merge and the landing
  commit; one verifier additionally checked eight other kinds on 30 seeds.
* Every committed binary reproduced its measured TSV byte-for-byte when
  rebuilt by a second agent; scans are a pure function of
  (seed, policy, policy seed, `STS_PIMC_*`).
* `policy_search.cpp` compiles warning-free under `-Werror=conversion` /
  `-Werror=sign-conversion`.
* Cost: about 4,000 core-seconds per 1,000 A20 runs at K=8 (≈ 4 s per run,
  most of it Act-2 fights the agent now reaches), against ≈ 600 for the
  particles-only agent.

## 5. Public information only — the twin check

Code review (four lever verifiers plus a merge verifier) found no decision-time
read of hidden state. The empirical test is `STS_TWIN_CHECK=1`
(`tools/fuzz/src/policy.cpp`): every `SIM_SEARCH_PIMC` decision is also
computed on `make_hidden_twin(rc)` — same public state, every hidden quantity
redrawn — from an identical copy of the policy stream, and any disagreement is
printed as `TWIN_MISMATCH`. The true-state pick is the one played, so a checked
scan's trajectories equal an unchecked one's (verified on 300 rows).

On `STS800000`–`STS800299` (92,268 decisions) the raw check reports 276
mismatches, all on Match-and-Keep event screens and map choices. They trace to
two `resample_hidden` rows that shuffle the STORED window rather than a
canonical one (`resample_relic_pool_remainders`, `resample_match_and_keep_board`
in `src/engine/resample.cpp`; `resample_draw_order` sorts first, which is why
combat shows none): the same seed over a different hidden arrangement yields a
different but identically distributed particle. With those two rows sorted
before the shuffle — a diagnostic build only, not committed, because it would
move every particle consumer's realizations — the same 92,268 decisions show
**0 mismatches**. The agent's decisions are a function of public state and
its own stream.

## 6. Commands

```bash
# held-out headline (shard the range over processes; seed_scan is single-threaded)
seed_scan --seeds STS700000-STS702499 --policies sim_search_pimc \
  --policy-seeds 0 --ascension 20 --format tsv --out pimc.tsv --summary pimc.txt
# Act-1 boss kill = boss_killed_acts & 1 ; boss reached = boss_reached_acts & 1

# leak test (stderr carries TWIN_MISMATCH lines, if any)
STS_TWIN_CHECK=1 seed_scan --seeds STS800000-STS800299 --policies sim_search_pimc \
  --policy-seeds 0 --ascension 20 --format tsv --out twin.tsv 2> twin.stderr
```

## 7. Open items

Recorded as deferred obligations in [../training-tasks.md](../training-tasks.md):

* The live-game fidelity of the paths this agent opens and no earlier scripted
  line took — shop purchases, shop purge, the Neow boss-relic swap — has not
  been checked by an oracle capture.
* `src/engine/monster_gremlin_leader.cpp` `queue_rally` writes past its
  `xs[kMonsterCap+2]` stack array once a long simulated Gremlin Leader fight
  grows the monster records to `kMonsterCap` (found under ASan at K=16 on
  `STS800386`, Act 2). Silent in release; cannot touch the Act-1 metric.
* The relic-pool and Match-and-Keep resample rows are not canonical (§5), so
  the raw twin check is noisy on those screens.
* Training-side: the agent is the natural public imitation teacher for the
  full-run net (SpireTrainer's engine pin must move past this commit).
