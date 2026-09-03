# S3.63 tier-4 distributional family — measured report

Written by S3.63 (2026-09-03) on branch `s363`, base `dedac58e` (S3-G1 taken).
Evidence for the S3.63 acceptance row in [../s3-tasks.md](../s3-tasks.md).
Methodology and α discipline are B5.3's and S2.44's, unchanged — see
`tools/dist_check/README.md` ("Pre-registered S2 act-2/3 family") for the
two-stage replicate rule this family reuses verbatim, and
[s2-verification.md](s2-verification.md) for the S2.44 precedent this report
mirrors.

---

## 0. The headline

**`RESULT PASS` at the pre-declared 20,000-seed scale.** All six registered
hypotheses retained at stage one (no replicate needed for the real family; the
campaign's replicate stage still ran because all three negative controls
reject by construction, exactly as S2.44's always does). All three negative
controls **CONTROL-REJECTED** in both stages. Zero exact/support-check
failures, over roughly 190,000 map generations, 60,000 monster-decision
draws, 60,000 relic-pool draws and 1,536 Heart-ladder cycles across the two
seed blocks.

Two findings surfaced during bring-up, both about the REGISTRATION rather
than the engine, and both resolved before the acceptance run (the B5.3/S2.44
"expectation library, not the engine, is often the bug" precedent held again):

1. The Act-4 floor-gated canSpawn family (hypotheses 5/6) was first
   hand-derived by reading `src/engine/relics/relic_pickup_{common,uncommon,
   rare}.cpp` in full and counting 12/9/6 blocked rows per tier (COMMON/
   UNCOMMON/RARE). That derivation is now an **exact, runtime-checked**
   assertion (`relic_tier_stratum`'s `observed_blocked == blocked` check) in
   addition to being the frozen law parameter, so a future relic added to
   either family would flag loudly rather than silently changing the pool
   composition under the test.
2. `front_scan_blocked_law` (S2.44's, `s2_expect.hpp`) was reused **unmodified**
   for the two canSpawn hypotheses despite Act-4's non-BOSS pool consumption
   walking front-then-end-inward rather than S2.44's pure front-scan. This is
   sound by a permutation-symmetry argument (stated in full in
   `tools/dist_check/src/s3_main.cpp`'s header) and was not treated as a
   registration change requiring a new law.

---

## 1. Registration

Six hypotheses, family-wise α = **0.01**, Holm-Bonferroni, the S2.44 two-stage
replicate-before-flagging rule applied uniformly to the family and to three
negative controls (one per mechanism class, not one per row — S2.44's
economy: 13 hypotheses, 4 mutants). Frozen **before** the first campaign run,
implemented in `tools/dist_check/src/s3_main.cpp`, whose header carries the
full registration text reproduced only in summary here.

| # | Hypothesis | Engine entry point | Null law |
|---|---|---|---|
| 1 | `s3.monster.spire_shield_case0_coin` | `spire_shield_init` | Fortify/Bash, 50/50 |
| 2 | `s3.monster.spire_spear_case2_coin` | `spire_spear_init` + 2×`spire_spear_roll_move` | Piercer/BurnStrike, 50/50 |
| 3 | `s3.monster.corrupt_heart_case0_coin` | `corrupt_heart_init` + `corrupt_heart_roll_move` | BloodShots/EchoAttack, 50/50 |
| 4 | `s3.map.emerald_gate_elite_index` | `assign_room_types` (has_emerald_key=false) | uniform rank, stratified by observed elite-node count |
| 5 | `s3.relic.act4_shop_can_spawn_front_scan` | `return_random_relic_key` (in_shop=true) | `front_scan_blocked_law`, 3 tier-strata |
| 6 | `s3.relic.act4_reward_can_spawn_front_scan` | `return_random_relic_key` (in_shop=false) | `front_scan_blocked_law`, 3 tier-strata |

Negative controls, judged at the family's strictest Holm threshold (α/6):

| Control | Represents |
|---|---|
| `mutant.spire_shield_case0_always_fortify` | the coin's `randomBoolean` draw dropped |
| `mutant.emerald_gate_always_first_node` | the draw's result ignored (rank hardcoded to 0) |
| `mutant.act4_can_spawn_rejection_returns_relic` | canSpawn rejection does not permanently consume the pool (S2.44's own M3) |

Exact/support checks, outside the Holm family (any failure flags the campaign
directly, the B5.3/S2.44 precedent):

- the emerald-gate **two-arm paired** comparison itself (trap 1's core claim):
  byte-identical room grids, identical elite-node count, and an mapRng counter
  delta of exactly the one skipped draw, between `has_emerald_key=false` and
  `=true` on the SAME generated map;
- the three coin flips' **deterministic surround**: every other `ai_rng` draw
  around each coin (the `rollMove` `num` draw, and the Spear's two
  deterministic cycle arms) costs exactly the draws the header notes claim, on
  every seed, regardless of the coin's own outcome;
- the Heart's **buffCount ladder**: Artifact(2) → Beat of Death(1) → Painful
  Stabs(−1) → Strength(10) → Strength(50) forever, read off the actual queued
  `ActionQueueItem`s `corrupt_heart_take_turn` produces, confirming the 3-bit
  saturating counter never advances past rung 4.
- the Act-4 floor-gated canSpawn family's blocked-row count per tier,
  runtime-derived and checked against the hand-derivation in §0.

---

## 2. Result — stage one / final verdicts (20,000 seeds, release preset, WSL)

Re-run command:

```bash
tools/wsl_run.sh release
tools/wsl_run.sh --script tools/dist_check/s3_run.sh release --seeds 20000
```

(`s3_run.sh` is the S3.63 analogue of `run.sh`/`s2_run.sh`, beside them;
`--seeds` refuses values below 10,000, the B5.3/S2.44 floor. Equivalently,
from an already-built `build/release` tree,
`./build/release/tools/dist_check/dist_check_s3 --seeds 20000` run directly
inside WSL.)

Raw output:

```
dist_check_s3 seeds=20000 stochastic_hypotheses=6 family_alpha=0.01 correction=Holm-Bonferroni+replicate
replicate stage ran: seed blocks are the stage-one blocks XOR the pre-registered salt
PASS s3.map.emerald_gate_elite_index n=20000 chi2=4.483212e+01 df=38 p=2.070786e-01 holm=1.666667e-03
PASS s3.monster.corrupt_heart_case0_coin n=20000 chi2=6.050000e-01 df=1 p=4.366766e-01 holm=2.000000e-03
PASS s3.relic.act4_reward_can_spawn_front_scan n=19998 chi2=2.288862e+01 df=29 p=7.816157e-01 holm=2.500000e-03
PASS s3.relic.act4_shop_can_spawn_front_scan n=19998 chi2=2.186440e+01 df=29 p=8.257605e-01 holm=3.333333e-03
PASS s3.monster.spire_shield_case0_coin n=20000 chi2=2.880000e-02 df=1 p=8.652416e-01 holm=5.000000e-03
PASS s3.monster.spire_spear_case2_coin n=20000 chi2=2.000000e-04 df=1 p=9.887166e-01 holm=1.000000e-02
CONTROL-REJECTED mutant.act4_can_spawn_rejection_returns_relic p=0.000000e+00 replicate_p=0.000000e+00 holm=1.666667e-03
CONTROL-REJECTED mutant.emerald_gate_always_first_node p=0.000000e+00 replicate_p=0.000000e+00 holm=1.666667e-03
CONTROL-REJECTED mutant.spire_shield_case0_always_fortify p=0.000000e+00 replicate_p=0.000000e+00 holm=1.666667e-03
RESULT PASS
```

Every hypothesis is retained (`PASS`) at stage one; nothing needed the
confirmatory replicate itself, but the replicate stage ran anyway because the
campaign-wide gate is "any row rejected, family or control" and all three
controls reject by construction (S2.44's own behaviour, `needs_replicate`
in `main`). All three controls show `replicate_p=0.000000e+00`, i.e.
**rejected in both stages**, satisfying the two-stage rule's requirement that
a control demonstrate power under the SAME rule the real family is judged by.

`n=19998` on the two relic rows (rather than 20000) is exact and expected: the
20,000-seed sweep splits evenly across three tier-strata
(`c.seeds / 3 = 6666`) times 3 = 19998; the two seeds dropped by integer
division carry no signal loss (comment in `relic_family_sweep`).

### 2.1 A20/A19-scale confirmation (10,000 seeds, three presets)

The same campaign was also run at the minimum registered scale (10,000 seeds)
on `debug` and `asan` (WSL, GCC/Clang) and on `win-release` (Windows,
clang-cl) to confirm the byte-identical-across-compilers property the repo's
fixture corpus already establishes for the engine proper. All three report
**identical statistics** (`chi2`, `p`, `holm` to the printed six digits) to
each other and to the first 10,000 seeds of the §2 run:

```
dist_check_s3 seeds=10000 stochastic_hypotheses=6 family_alpha=0.01 correction=Holm-Bonferroni+replicate
PASS s3.monster.spire_spear_case2_coin      n=10000 chi2=1.960000e+00 df=1  p=1.615133e-01
PASS s3.map.emerald_gate_elite_index        n=10000 chi2=3.619241e+01 df=38 p=5.532411e-01
PASS s3.monster.corrupt_heart_case0_coin    n=10000 chi2=4.840000e-02 df=1  p=8.258712e-01
PASS s3.monster.spire_shield_case0_coin     n=10000 chi2=1.600000e-03 df=1  p=9.680931e-01
PASS s3.relic.act4_shop_can_spawn_front_scan   n=9999 chi2=1.441420e+01 df=29 p=9.890705e-01
PASS s3.relic.act4_reward_can_spawn_front_scan n=9999 chi2=1.430625e+01 df=29 p=9.897136e-01
RESULT PASS
```

reproduced verbatim on WSL `debug`, WSL `asan` (zero sanitizer findings) and
Windows `win-release`.

---

## 3. What each row actually measured

**Rows 1–3 (coin flips).** Every count comes off the real
`spire_shield_init`/`_roll_move`, `spire_spear_init`/`_roll_move` and
`corrupt_heart_init`/`_roll_move`/`_take_turn` entry points over a freshly
seeded `ai_rng`, not a reimplementation of the branch. The "deterministic
surround" exact checks (§1) ran on every one of the 20,000 + 20,000 seeds
across both stages and never flagged, which is the claim that the coin is the
*only* degree of freedom at each of these three call sites.

**Row 4 (emerald gate).** The two-arm paired comparison ran on every seed
(40,000 `assign_room_types` calls per stage) and never flagged: holding the
emerald key removed exactly the one `setEmeraldElite` `mapRng` draw, marked no
node, and left the room grid and elite-node count byte-identical to the
paired arm that did not hold the key — trap 1's core claim, now exercised
distributionally rather than on the single S3.32 witness seed. The stochastic
half stratifies by the observed elite-node count (2 through 7 in this run) and
tests only the within-bucket uniformity of the row-major rank
`setEmeraldElite`'s draw chose — 38 degrees of freedom at 20,000 seeds.

**Rows 5–6 (Act-4 canSpawn).** At floor 55 (comfortably inside Act 4, past
every floor threshold the family gates on — 35/40/48/52), 12 of 33 COMMON
rows, 9 of 30 UNCOMMON rows and 6 of 28 RARE rows are permanently closed; the
front-scan consumption pattern this produces at both the shop and the
elite-combat-reward call sites matches S2.44's negative-hypergeometric law to
p ≈ 0.78–0.99 at every scale run.

**The Heart's buffCount ladder.** 64 seeds × 24 decision cycles (≥ 8
`GAIN_ONE_STRENGTH` hits per seed, 1,536 ladder observations total) all
produced the exact deterministic sequence and all saturated at rung 4 with no
drift, read off the actual `ActionQueueItem`s `corrupt_heart_take_turn`
queues rather than off the accessor functions alone.

---

## 4. Standing limits

- The family tests the SIM's own entry points against SIM-derived structural
  facts (elite-node counts, tier blocked-counts) and closed-form Java laws; it
  is not a live-capture witness. The capture-based obligations for these same
  mechanisms (trap 1's `mapRng` counter on a real Act-2→3 crossing, the
  Shield/Spear/Heart fights' `--combat` replay) belong to S3.62 and are
  tracked there, not discharged here.
- `kAct4TestFloor = 55` is fixed rather than swept across the full 51–57
  Act-4 floor range; every threshold the family gates on (35/40/48/52) is
  strictly below it regardless of the A19/A20 floor-base shift (design §4.3),
  so the row is floor-base-invariant by construction, but a relic added with
  a threshold at or above 55 would silently fall outside this row's coverage
  rather than fail loudly — the nearest analogue to S2.44's own "exact
  support checks are outside the stochastic family" caveat.
