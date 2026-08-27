# S2.V2 sim-consulting driver — measured sim-side reach report

Written by S2.V2 (2026-08-27). This is the acceptance evidence for the
[s2-tasks.md](../s2-tasks.md) S2.V2 row: the measured reach of the
sim-consulting scripted policy at scanned scale, the deterministic-replay
verification, and the concrete triple list the S2-G2 item-2/3 depth cohorts
schedule from. Scan artifacts (row TSVs, scripts) are regenerable from the
commands quoted here and stay out of the tree; the committed evidence is this
report plus the named tests.

## 0. The headline, stated honestly

* **The escalation instrument works.** The `sim_search` policy reaches and
  kills at rates no prior instrument in this repo approaches: **37.2 %
  Act-1 boss kill** on 40,000 fresh seeds (E0's measured best was 0.12 %;
  the live b1.7.0 driver's was 2.5 % of 750), **0.36 % Act-2 boss kill on
  the first pass** rising to **19.6 %** on the re-seeded second pass, and —
  for the first time in any sim- or driver-side measurement — **complete
  A20 three-act victories**, i.e. double-boss kills.
* **Every S2-G2 item-2 cohort is schedulable now**, take and skip, all
  three Act-2 registry bosses, each line continuing through the boss-relic
  pick and the act-2→3 transition into a playable Act-3 floor. The Mind
  Bloom directed-capture line is schedulable (21 stage-1 lines fired it).
* **Item 3 is schedulable except for one registry row.** Three double-boss
  victories exist over two distinct first-boss identities (Time Eater ×2,
  Donu and Deca ×1), all on the {Time Eater, Donu and Deca} pair — so Time
  Eater and Donu and Deca are witnessed killed. **Awakened One is not: 0
  kills in 553 Awakened-first Act-3 boss fights** at a scale where the
  other pair's conversion (3/585 = 0.51 %) predicts ~2.8. Per the row's own
  instruction this is reported as a shortfall, not routed around; §6 has
  the numbers and the mechanism hypothesis.

## 1. What ran

Release preset (`win-release`, clang-cl), `seed_scan` at its S2.42 defaults
(`--ascension 20`, `--max-actions 12000`), policies `sim_search` /
`sim_search_skip` (the S2.V2 additions, `tools/fuzz/src/policy_search.cpp`).
**107,424 scanned rows, 32,961,364 actions**, in four waves:

| wave | cases | rows |
|---|---|---|
| stage 1, breadth | STS100000–STS139999 × `sim_search` × ps0 | 40,000 |
| stage 2, depth re-seed | the 1,532 act-2 boss-FIGHT seeds × ps1–8; the 144 act-2 boss-KILL seeds × ps0–23; the same 144 × `sim_search_skip` × ps0–7 | 16,864 |
| stage 3, victory hunt | the act-3 boss-fight seed union (24→42 seeds) × ps0–191 | 6,912 |
| stage 4, Awakened hunt | the 105 Awakened-One-first act-3 seeds × ps192–831 | 43,648 |

The re-seeding waves are the sanctioned cherry-pick: a (seed, policy_seed)
pair IS the cohort unit (design §6 item 3), and the one-draw tie-break gives
one seed many distinct deterministic lines.

## 2. Measured reach

Stage 1 (40,000 fresh seeds, one line each — the honest base rate):

| act | boss FIGHT | boss KILL |
|---|---|---|
| 1 | 27,878 (69.70 %) | 14,892 (37.23 %) |
| 2 | 1,532 (3.83 %) | 144 (0.36 %) — Champ 80 / Automaton 40 / Collector 24 |
| 3 | 8 rows | 0 |

End reasons: `run_over` 38,244, `livelock` 1,727 (4.3 %, see §7),
`action_cap` 29 (the truncation witness the S2.42 row demands printing —
0.07 %, so the 12,000-action budget is not clipping the measurement).

Stage 2, on the 144 act-2-kill seeds × 24 policy seeds (3,456 rows):
Act-1 kill 83.4 %, **Act-2 kill 19.6 %** (677 rows; Champ 374 / Automaton
154 / Collector 149), 79 act-3 boss-fight rows. The skip cohort
(`sim_search_skip`, same seeds × 8) delivered **51 act-2 kills with the
boss relic SKIPPED** (Champ 31 / Collector 13 / Automaton 7).

Stages 3–4, all waves combined: **1,138 Act-3 boss-fight rows** (Awakened
One first 553, Donu and Deca first 357, Time Eater first 228) and **three
victories** — complete A20 double-boss runs:

| seed | policy | policy_seed | first boss | steps |
|---|---|---|---|---|
| STS128113 | sim_search | 27 | Time Eater | 716 |
| STS128113 | sim_search | 47 | Time Eater | 651 |
| STS108107 | sim_search | 153 | Donu and Deca | 718 |

All three fought the {Time Eater, Donu and Deca} pair (both orders; the
scripts' floor-50+ play targets name Donu, Deca and TimeEater and never
AwakenedOne).

Baselines for the delta: E0 sim policies measured 0.12 % Act-1 kill and
structurally 0 past Act 1 ([s242-deep-reach.md](s242-deep-reach.md)); the
b1.7.0 live driver measured 29.5 %/28.3 % Act-1 boss fight, 19/1500 Act-1
kills, and **0 Act-2 boss fights in 2,000 attempts** (the S2.43 breadth
wave, the escalation trigger).

## 3. The policy, and what keeps it inside the sanction

`sim_search` (`tools/fuzz/src/policy_search.cpp`, `PolicyKind` values 5–6,
appended): combat decisions are a 1-ply search over engine snapshots — copy
the trivially-copyable `RunController`, advance the copy through the
candidate plus a deterministic threat-aware static completion of the rest of
the fight, score the outcome with an integer evaluation — with a bounded
2-ply through a boss fight's opening turns. Map-node and event-option
decisions are scored by a one-floor rollout of the same shape. The remaining
run-layer decisions are the b1.7.0 heuristics ported from
`greedy_policy.py`: R1's deck gate (widened by an Act-2+ standout clause),
R2's potion discipline (as evaluation hold-values), the `ACT_PROFILES`
overlays, R4's boss-relic never-take list. `sim_search_skip` differs in
exactly one rule — R4 answers SKIP — mirroring the SHA-pinned
`policy_bossrelic_take/skip.json` cohort identities.

No wall clock, no floats, no unseeded randomness: every score is integer
arithmetic, every bound (`kRolloutBudget`, `kRolloutTurnCap`, the 2-ply
breadth/turn gates) a constant hit in deterministic order, and the single
stochastic input is the shared one-draw-per-decision tie-break from
`policy_seed`. The preview is deliberately omniscient — it advances copies
of the real controller, draws and monster rolls included. That is the
sanctioned design (§6: "sim pre-scan chooses (seed, policy, policy-seed)
triples whose scripted line reaches the target"): the product is a scripted
LINE the oracle replays and confirms zero-diff, not an agent playing under
the information contract. GT0's player-information layer is neither
consulted nor affected.

## 4. Deterministic replay — the acceptance bar

* `--verify-determinism` over STS100000–STS100499 × {`sim_search`,
  `sim_search_skip`} × ps{0,1} (2,000 rows, every case scanned twice):
  **`determinism_mismatches=0`**.
* Every cohort triple in §5 was emitted under `--verify-determinism`
  individually (row + trajectory compared across two scans):
  **0 mismatches, 14/14**.
* Every emitted script is additionally replay-verified at write time: the
  emitter re-drives the trajectory from `run_begin` and refuses to write a
  file whose terminal `fuzz::hash_controller` does not equal the scanned
  row's `final_hash` (pinned by
  `SimSearchScript.RefusesATrajectoryThatDoesNotReproduceTheHash`).
* Unit pins: `SimSearch.ScanIsDeterministicIncludingTheTrajectory`,
  `SimSearchScript.EmitsOneStepPerActionAndVerifiesTheReplay`.

## 5. The schedulable cohort — the triple list

Every triple below is (seed, policy, policy_seed) on the committed engine
(schema 8), emitted as an STS-SCRIPT v1 file and determinism-verified. To
regenerate any line's script:

```bash
seed_scan --seeds <SEED>-<SEED> --policies <policy> --policy-seeds <ps> \
          <filter> --script-dir <dir> --verify-determinism --out scan.tsv
```

**Act-2 depth, TAKE cohort** (filter `--need-boss-kill-act 2 --min-act 3`;
each line kills the named Act-2 boss, opens the chest, PICKS a boss relic,
and crosses into a playable Act-3 floor — max_floor ≥ 35 in every line):

| seed | policy | ps | act-2 boss | steps | final_hash |
|---|---|---|---|---|---|
| STS100009 | sim_search | 0 | Champ | 474 | 8b6edea3… |
| STS100439 | sim_search | 0 | Champ | 402 | 7a5adcc8… |
| STS100038 | sim_search | 0 | Automaton | 453 | cf8320b1… |
| STS100075 | sim_search | 0 | Automaton | 437 | 533c7a03… |
| STS107575 | sim_search | 0 | Collector | 566 | 76dc6ba0… |
| STS108173 | sim_search | 0 | Collector | 430 | 4dbf4517… |

**Act-2 depth, SKIP cohort** (filter `--need-boss-kill-act 2`, policy
`sim_search_skip` — the boss-relic screen is opened once and SKIPPED, at the
Act-1 chest and again at the Act-2 chest, and the run still crosses into
Act 3):

| seed | policy | ps | act-2 boss | steps | final_hash |
|---|---|---|---|---|---|
| STS105134 | sim_search_skip | 0 | Champ | 442 | d8283dae… |
| STS111111 | sim_search_skip | 0 | Automaton | 443 | 7dd22a33… |
| STS108173 | sim_search_skip | 0 | Collector | 389 | d0bc6de2… |

**Act-3 depth, double-boss victories** (filter `--need-victory`): the three
triples in §2's table — STS128113/ps27, STS128113/ps47 (first-boss identity
Time Eater), STS108107/ps153 (first-boss identity Donu and Deca). Three
completed A20 double-boss lines over two distinct first-boss identities:
the ≥3-over-≥2 clause of item 3 is met. Time Eater and Donu and Deca are
each witnessed killed (each victory kills both bosses of the pair).

**Mind Bloom directed capture** (filter
`--need-event MindBloom --min-act 3`): STS101166 / sim_search / ps0
(731 steps — also an Automaton take-kill line) and STS103364 / sim_search /
ps0 (671 steps). 21 stage-1 lines fired Mind Bloom in Act 3; these two are
the scheduled pair.

## 6. The shortfall: Awakened One, and why it is reported rather than fixed

"Every Act-3 registry BOSS row witnessed killed" needs an Awakened One kill.
The scan output cannot schedule one: **0 victories in 553 Awakened-One-first
Act-3 boss fights** (waves V/W/X/Y/Z, 43,648 dedicated re-seed rows on the
105 seeds whose Act-3 first boss is Awakened One), against 3/585 (0.51 %) on
the {Time Eater, Donu and Deca} pair. At the pair's rate 553 fights predict
~2.8 kills; observing zero (p ≈ 0.06 under that rate) plus the mechanism —
Curiosity gains strength every time the player plays a Power, and this
policy's widened R1 gate deliberately builds Power-carrying decks; the fight
is 640+ HP over two phases with two Cultist adds, followed at A20 by a
second boss on the same HP pool — says the miss is structural for this
policy shape, not a lottery still worth buying tickets in.

Per the ledger row's own instruction ("if reach is insufficient for the
bars even with deeper search, STOP and report — do not weaken the bar"),
the Awakened One row is left explicitly unscheduled. The options the next
session can weigh: an Awakened-aware variant of the one rule that feeds it
(hold Powers in that fight — a checkable criterion in the R4 never-take
tradition, since Curiosity names the rule it invalidates), more scale, or
an owner disposition on the bar. Everything else in items 2–3 schedules
from the output that exists today.

## 7. Known limits of this instrument

* **4.7 % of scanned rows end as LIVELOCK** (5,025/107,424), concentrated
  on boss floors (stage 1: 948 at floor 16, 762 at 17): a select/deselect
  oscillation on optional hand-select screens where the 1-ply rollout
  strictly prefers each toggle from the other's state. The CONFIRM tie-bias
  closes exact ties only; the oscillation is documented at the bias in
  `policy_search.cpp` with reproducer STS100007 / sim_search / ps0. These
  rows simply fail to qualify for cohorts; they do not contaminate emitted
  scripts (a script is only written for a filter-hitting row, and its
  replay is hash-verified).
* **The scan cost is policy-dependent.** The search multiplies engine
  advances per decision; the per-decision product is bounded
  (`kMoveBudgetProduct`, the boss-2-ply gates, the turn ramp), and the
  40,000-seed stage-1 wave costs ~6 minutes across 16 release-preset
  processes. Debug/asan presets are for the unit tests, not for scans.
* **A scripted line is sim-exact, not yet live-confirmed.** The zero-diff
  confirmation of these triples is S2.43's depth-cohort campaign, through
  `script_policy_cmd.py`'s stop-on-desync contract; any desync it records
  is Stage-B capture evidence by design. The known granularity seams
  (confirmation-only screens; the GRID pick-then-confirm shape) are handled
  by the follower's glue rules, which never advance the script cursor;
  anything else stops the run. The first live campaign added a third glue
  rule beside the original two (2026-08-27, divergence_STS100009_ps0): a
  screen whose only legal candidate is a single `choose` — the collapsed
  one-click dialog (Neow's opening `talk`; the Woman in Blue / Sensory
  Stone vestigial-click class the engine deliberately collapses) — is
  answered without consuming a step, match-first still applying; and one
  SKIP rule for the mirror seam (step 2 of the same run): a scripted
  `proceed`/`confirm` whose live state offers neither alias is consumed
  without emitting — the game auto-advances where the sim steps (the
  blessing click opens the MAP directly) — with a real desync still
  stopping on the following step. Glue rules 1 and 3 read the PROGRESS
  candidates — the always-available `potion discard N` side actions are
  excluded (third witness, divergence_STS100439_ps0: the post-rest
  campfire aftermath), while a scripted `potion_discard` matches first.
