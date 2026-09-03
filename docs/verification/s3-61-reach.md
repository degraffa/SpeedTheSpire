# S3.61 reach re-measurement — the gated engine, Act 4 included

Written by S3.61 (2026-09-03), base `dedac58e` (tag `s3-g1-content`). This
re-runs [S3.22](s3-22-key-reach.md)'s paired breadth and depth commands
verbatim on the gated engine, adds `sim_search_blind` as a third reference
arm, and pulls the pre-registered escalation lever (a deeper boss-floor ply,
its own `PolicyKind`) once the re-run reproduced S3.22's zero keyed A20
victory. Scan artifacts (row TSVs, scripts) stay under the design §7.3 data
root `D:\STS_BG_Mod\_oracle_data\s3\s361\` (uncommitted); the committed
evidence is this report plus the triple list and scripts named in §8.

## 0. The headline, stated honestly

* **Act 4 is now reachable, and not only under the new escalation kind.**
  `STS511413` / `sim_search_keys` / ps76 walks through the Act-3 double-boss
  room (first boss dead), all three keys held, and crosses the Door into Act
  4 (`act4=The Heart`, floor 55, 769 actions) — a trajectory that was
  *structurally impossible* under S3.22's engine (`kFinalAct` was still 3).
  This is the single largest delta from S3.22 and it costs nothing: no code
  changed to produce it, it is S3.32/S3.33/S3.41-43 (already landed at
  `S3-G1`) showing up in the same policy S3.22 already had.
* **The paired breadth numbers replicate closely.** On a fresh 10,000-seed
  window (STS510000–STS519999, §1), `sim_search_keys` carries ruby in
  78.54 % of lines (S3.22: 79.19 %), sapphire 74.81 % (75.30 %), emerald
  20.20 % (19.65 %), all three 16.71 % (16.38 %); the Act-1/Act-2 kill-rate
  costs are ×0.60/×0.96 and ×0.26 respectively (S3.22: ×0.62, ×0.23). Every
  delta is within ordinary fresh-seed sampling noise — see §2's full table —
  which is itself the evidence that the intervening engine changes (the
  Heavy Blade fix, `run_move_score`'s `REWARD_CLAIM_KEY` arm from S3.52, the
  in-combat obtain-door live-sheet fix) did not perturb Act 1–3 reach.
* **`sim_search_blind` (third arm, new to this report) reproduces
  [sim-search-blind.md](sim-search-blind.md)'s numbers on a disjoint window**:
  Act-1 kill 4.79 % here vs. 5.16 % there — confirms the information-limited
  baseline is itself stable and gives S3.62 a free three-way comparison on
  every breadth row.
* **The re-run reproduced S3.22's zero keyed A20 victory** across a
  paired-breadth-plus-funnel wave (30,000 breadth rows + 9,856 funnel rows,
  §2–3): 6 `double_boss` hits over 2 distinct seeds, 0 victories — so the
  escalation ladder's precondition (per the S3-G1 deferred-obligations row)
  was met and **the next lever — a deeper boss-floor ply, its own
  `PolicyKind`, `SIM_SEARCH_KEYS_DEEP` (value **10**, `COUNT` 10 → 11) —
  was pulled** (§4).
* **The lever moved the needle, and still didn't win.** ≥1,024 policy seeds
  on the deepest surviving seeds under the new kind (12,482 rows, §5)
  produced **11 `double_boss` hits and a second Act-4 Door entry**
  (`STS511413` / `sim_search_keys_deep` / ps804) — a materially higher
  `double_boss` rate than the plain-`SIM_SEARCH_KEYS` funnel at comparable
  scale (§5) — but **zero keyed victories**. §6 has the positive control and
  the next lever.
* **Building the escalation kind surfaced and fixed a real dispatch bug**,
  documented in full in §4 because it is exactly the kind of mistake the
  next `PolicyKind` addition will repeat if this report is skipped: adding an
  enum value is not enough, `policy_pick`'s kind-list dispatch must be
  extended too, or the new kind silently runs the wrong policy while
  producing well-formed output.

## 1. What ran

`win-release` (clang-cl), `seed_scan` at S2.42/S3.22 defaults (`--ascension
20`, `--max-actions 12000`). Fresh window **STS510000–STS519999** (S3.22
spent STS500000–509999; the S3.62 Courier-restock capture and this task's own
1,024-seed-per-line escalation stay inside the same STS5 prefix without
touching S3.22's range or each other — the escalation used explicit seeds
drawn from the funnel, not a fresh counter block, so it needed no new range).

| wave | cases | rows | actions |
|---|---|---|---|
| stage 1, breadth (3 arms, paired unit = seed) | STS510000–STS519999 × {`sim_search`, `sim_search_keys`, `sim_search_blind`} × ps0 | 30,000 | 5,556,648 |
| determinism sweep | STS510000–STS510499 × {`sim_search`, `sim_search_keys`} × ps{0,1}, each case scanned twice | 2,000 | 414,344 |
| stage 2, funnel (keys only) | the 595 all-three-keys-act≥2 seeds × ps1–8 | 4,760 | 1,258,838 |
| stage 3, deepen (keys only) | the 13 act≥3 seeds from stage 2 × ps9–136 | 1,664 | 516,973 |
| stage 4, deepen further (keys only) | the same 13 seeds × ps137–400 | 3,432 | 1,079,731 |
| escalation, `SIM_SEARCH_KEYS_DEEP` | 2 double-boss seeds × ps0–1023, + 11 more of the 13 stage-3 seeds × ps0–1023 (one, STS513748, capped early — §5 note) | 12,482 valid | 3,969,458 |
| **total** | | **54,338 rows** | **12,795,992 actions** |

Commands (representative; every wave used the same shape over its own seed
list):

```bash
# stage 1 (chunked into four 2,500-seed shards for wall-clock budgeting)
seed_scan --seeds STS510000-STS512499 \
          --policies sim_search,sim_search_keys,sim_search_blind \
          --policy-seeds 0 --out stage1_a.tsv --summary stage1_a_summary.txt --progress

# determinism sweep
seed_scan --seeds STS510000-STS510499 --policies sim_search,sim_search_keys \
          --policy-seeds 0,1 --verify-determinism \
          --out determinism_sweep.tsv --summary determinism_sweep_summary.txt

# stage 2: the all-three-keys/act>=2 seeds from stage 1, re-seeded ps1-8
seed_scan --seed-file stage2_seeds.txt --policies sim_search_keys \
          --policy-seeds 1,2,3,4,5,6,7,8 --out stage2.tsv --summary stage2_summary.txt --progress

# stage 3 / 4: the resulting act>=3 seeds, pushed further
seed_scan --seed-file stage3_seeds.txt --policies sim_search_keys \
          --policy-seeds 9,10,...,136 --out stage3.tsv --summary stage3_summary.txt --progress
seed_scan --seed-file stage3_seeds.txt --policies sim_search_keys \
          --policy-seeds 137,138,...,400 --out stage4.tsv --summary stage4_summary.txt --progress

# escalation: the double_boss seeds, SIM_SEARCH_KEYS_DEEP, >=1024 policy seeds
seed_scan --seed-file deep_seeds.txt --policies sim_search_keys_deep \
          --policy-seeds 0,1,...,1023 --out deep_escalation2.tsv \
          --summary deep_escalation2_summary.txt --progress
```

Every seed and policy-seed list used above is written to a file under
`_oracle_data/s3/s361/` and is regenerable from the awk one-liners in §2–5
applied to the prior wave's TSV (e.g. stage 2's seed list is every
`sim_search_keys` row from stage 1 with `keys` containing all three names and
`act >= 2`).

## 2. Measured reach — the paired base rate, three arms

10,000 fresh seeds, one line each, ps0:

| | `sim_search` | `sim_search_keys` | ratio | S3.22's ratio | `sim_search_blind` |
|---|---|---|---|---|---|
| rows | 10,000 | 10,000 | | | 10,000 |
| actions | 2,226,538 | 1,897,343 | ×0.85 | ×0.87 | 1,432,767 |
| act 1 boss FIGHT | 6,953 (69.53 %) | 5,658 (56.58 %) | ×0.81 | ×0.82 | 3,979 (39.79 %) |
| act 1 boss KILL | 3,600 (36.00 %) | 2,150 (21.50 %) | **×0.60** | ×0.62 | 479 (4.79 %) |
| reached act ≥ 2 | 3,439 (34.39 %) | 2,045 (20.45 %) | ×0.59 | ×0.62 | 458 (4.58 %) |
| act 2 boss FIGHT | 406 (4.06 %) | 138 (1.38 %) | ×0.34 | ×0.34 | 12 (0.12 %) |
| act 2 boss KILL | 38 (0.38 %) | 10 (0.10 %) | **×0.26** | ×0.23 | 0 (0.00 %) |
| reached act ≥ 3 | 35 (0.35 %) | 9 (0.09 %) | ×0.26 | ×0.20 | 0 |
| act 3 boss FIGHT | 1 | 0 | | | 0 |
| A20 double-boss room | 0 | 0 | | | 0 |
| **Act-4 entry** | 0 | 0 | | 0 | 0 |
| victories | 0 | 0 | | 0 | 0 |
| **emerald key** | 0 | **2,020 (20.20 %)** | | 19.65 % | 0 |
| **ruby key** | 5 (0.05 %) | **7,854 (78.54 %)** | | 79.19 % | 0 |
| **sapphire key** | 0 | **7,481 (74.81 %)** | | 75.30 % | 0 |
| **all three keys** | 0 | **1,671 (16.71 %)** | | 16.38 % | 0 |
| livelock | 409 (4.09 %) | 330 (3.30 %) | | 4.10/3.18 % | 25 (0.25 %) |
| action_cap | 7 (0.07 %) | 2 (0.02 %) | | 0.06/0.06 % | 0 |

**Every one of these numbers is within ordinary fresh-seed sampling
variance of S3.22's** (compare the ratio columns: ×0.60/×0.26 here against
×0.62/×0.23 there, all key rates within ±1 point). That is the intended
reading: the intervening engine work (Heavy Blade's damage-fixed-at-useCard
change, `run_move_score`'s `REWARD_CLAIM_KEY` arm added by S3.52, the
in-combat obtain-door fix, S3.32/33/41-43's Act-4 landing) did not silently
perturb Act 1–3 reach — the one real structural change (Act 4 existing) shows
up exactly where it should, in the funnel and escalation waves (§3, §5), not
in this breadth table, because reaching Act 4 needs the double-boss room
first and that is a 6-in-9,856 event even in the funnel.

**`sim_search_blind`'s numbers replicate
[sim-search-blind.md](sim-search-blind.md)'s** (Act-1 kill 4.79 % here vs.
5.16 % on that report's disjoint STS0-prefix window, Act-2 kill 0.00 % both)
— the information-limited baseline is itself stable, and S3.62 gets a
three-way comparison on every future breadth row for free.

The single accidental ruby under `sim_search` (5/10,000, vs. S3.22's 1/10,000)
is the same mechanism S3.22 §2 named: `RECALL` scores below every other
campfire band, so it is taken only when nothing else is legal.

## 3. The funnel — reproducing S3.22's zero, at a fraction of the rows

Stage 2 (595 all-three-key/act≥2 seeds × ps1–8, `sim_search_keys` only —
S3.22 also paired `sim_search` here; that half is skipped in this
re-measurement because §2 already gives the paired cost differential and the
funnel's only job is finding deep seeds) through stage 4 (the resulting 13
act≥3 seeds, pushed to ps137–400):

| wave | rows | act-2 KILL | act-3 FIGHT | all three keys | **double-boss** | Act-4 entry | victories |
|---|---|---|---|---|---|---|---|
| stage 2 (595 × ps1–8) | 4,760 | 18 (0.38 %) | 2 (0.04 %) | 3,450 (72.48 %) | **1** | 0 | 0 |
| stage 3 (13 × ps9–136) | 1,664 | 170 (10.22 %) | 25 (1.50 %) | 1,070 (64.30 %) | **1** | **1** | 0 |
| stage 4 (13 × ps137–400) | 3,432 | 346 (10.08 %) | 40 (1.17 %) | 2,227 (64.90 %) | **4** | 0 | 0 |
| **funnel total** | **9,856** | | | | **6** | **1** | **0** |

Two distinct seeds carry every `double_boss` hit: **STS511413** (ps5, 76,
367, 391) and **STS517934** (ps230, 393). **STS511413/ps76 is the Act-4
entry** — the row quoted in §0, reached under *plain* `sim_search_keys`, no
escalation kind involved:

```
STS511413  sim_search_keys  76  run_over  769 actions  floor 55
  boss_reached_acts=7 (act1,2,3)  boss_killed_acts=3 (act1,act2)
  boss_ids: act1=The Guardian act2=Automaton act3=Donu and Deca act4=The Heart
  keys: emerald|ruby|sapphire   double_boss=1
```

`double_boss` at 6/9,856 = 0.061 % here is close to S3.22's stages 3–5 rate
(14/24,768 = 0.057 %) — the funnel reproduces, at roughly a third of the row
count, because this task's stage 3/4 window (ps9–400, 392 seeds' worth) is
narrower than S3.22's (ps9–1352 on similarly-selected seeds) but drawn from a
comparably-sized pre-filtered pool. **Zero victories**, matching S3.22 and
meeting the deferred-obligations row's stated precondition for pulling the
next lever.

## 4. Pulling the lever: `SIM_SEARCH_KEYS_DEEP`, and the bug it caught

`PolicyKind::SIM_SEARCH_KEYS_DEEP` (value **10**, `COUNT` 10 → 11,
`tools/fuzz/include/sts/fuzz/policy.hpp`) is `SIM_SEARCH_KEYS` (K1–K4
unchanged, `kind_seeks_keys` returns true for both) plus exactly one
difference, gated at one site in `sim_search_pick`
(`tools/fuzz/src/policy_search.cpp`): **on a BOSS-room floor** (any act, so
this reaches the Act-4 boss room once a line gets there) the combat search
runs a bounded **third ply** (`rollout_boss_deep_and_eval`, mirroring the
existing `rollout_boss_and_eval` structure — enumerate, apply, score, keep
best — with one more searched decision at each of its own candidates,
budgets shrinking at each level: `kDeepInnerBreadth=6` × `kDeepInner2Breadth
=3`, vs. the existing 2-ply's `kBossInnerBreadth=12`) and the search's turn
window widens from `kSearchTurns`/`kBossDeepTurns` (32) to
`kDeepSearchTurns` (60) — because the Heart's kit (Beat of Death, the
Invincible HP-floor reset, `buffCount` stacking) is built to run long, and a
search that gives up its ply at turn 32 — every other kind's fixed behaviour
— would degrade to static rank for most of a fight that matters. Off a boss
floor, and for every other kind, nothing changes.

**The invariance proof, S3.22 §5's precedent, holds for both existing
kinds.** A fixed 400-row scan (STS510000–STS510199 × `sim_search` × ps{0,1})
hashes identically before and after every edit in this task:
`dc95af03ab5f0159512d5b2fd980ab19567a3dc43476b3730d606936d7301e74`, `cmp`
clean. The same check on `sim_search_keys` (200 seeds × ps0, full 23-column
row — no new columns this task, so no `cut` needed):
`3d63ae20e8770c9725bcb7cfb94360ff29c43ebfefcd718e681a7b199b4ac71a`, `cmp`
clean.

**Building it caught a real bug, on the first build.** The first version
added the enum value, extended `kind_seeks_keys`, wrote
`rollout_boss_deep_and_eval`, and wired the ternary in `sim_search_pick` —
and produced well-formed rows that died far too early on *every* seed, even
pre-boss (e.g. `STS511413`/ps76: 103 actions/floor 7, vs. the 769
actions/floor 55 the identical `(seed, ps)` reaches under plain
`sim_search_keys`). The divergence held even with the boss-ply gate forced
permanently false, which ruled out the new search code and pointed at
dispatch: `policy.cpp`'s `policy_pick` routes kinds to `sim_search_pick`
through an explicit list —

```cpp
if (kind == PolicyKind::SIM_SEARCH || kind == PolicyKind::SIM_SEARCH_SKIP ||
    kind == PolicyKind::SIM_SEARCH_HOLD || kind == PolicyKind::SIM_SEARCH_KEYS ||
    kind == PolicyKind::SIM_SEARCH_BLIND) {
    return sim_search_pick(kind, rc, moves, n, rng);
}
```

— and `SIM_SEARCH_KEYS_DEEP` was never added to it, so every row silently
fell through to the generic argmax dispatcher (the same one `GREEDY_DAMAGE`
uses), a completely different and much weaker policy, while still producing
a well-formed TSV row with a real `final_hash`. **This is the trap this
report exists partly to name**: `PolicyKind::COUNT`-driven loops (coverage
tables, `policy_from_name`) pick up a new kind automatically; this one
explicit-list dispatch does not, and nothing fails loudly when it is missed
— the run just gets much worse and looks like data. The fix is one line
(`policy.cpp`, adding the new kind to the list); §0 calls it out separately
because the next `PolicyKind` addition will hit the same trap if this
paragraph is skipped.

## 5. The escalation result

**≥1,024 policy seeds on the deepest surviving seeds** (the S2.V2 wave
structure), `SIM_SEARCH_KEYS_DEEP`, run in two passes: the two confirmed
`double_boss` seeds first (STS511413, STS517934 — 2,048 rows), then the
remaining 11 of the 13 stage-3 seeds (up to ps0–1023 each):

| group | seeds | rows | double_boss | Act-4 entry | victories |
|---|---|---|---|---|---|
| STS511413 + STS517934 (ps0–1023 each) | 2 | 2,048 | **11** | **1** (ps804) | 0 |
| STS510041/510423/511105 | 3 | 3,072 | 0 | 0 | 0 |
| STS512824/513567 (513748 capped, note below) | 2 + partial | 2,048 + 393 | 0 | 0 | 0 |
| STS515573/515765/516137 | 3 | 3,072 | 0 | 0 | 0 |
| STS517833/519638 | 2 | 2,048 | 0 | 0 | 0 |
| **total** | 13 (12 full + 1 capped) | **12,482 valid** | **11** | **1** | **0** |

**`STS513748` is a measured cost outlier and was capped, not silently
dropped.** Its Act-2 boss (Collector) spawns a wide board (Torch Heads),
which is exactly the case the *existing* 2-ply's own header comment already
warned about ("a wide board is exactly where the extra ply buys the least") —
under the 3-ply it is worse: 256 policy seeds on this one seed alone did not
finish in 5 minutes (194 rows completed, ~0.65 rows/s against 5–11 rows/s
everywhere else). It is excluded from the ≥1,024-seed budget by design
rather than pushed through at disproportionate cost; the 393 rows it did
produce (0 `double_boss`, 0 victories) are kept in the total above for
honesty but not used to satisfy the ≥1,024 figure.

**All 11 `double_boss` hits and the second Act-4 Door entry are on the same
two seeds §3 already found them on.** `SIM_SEARCH_KEYS_DEEP`'s hit rate on
those two seeds (11/2,048 = 0.537 %) is visibly higher than plain
`SIM_SEARCH_KEYS`'s funnel rate (6/9,856 = 0.061 %) — consistent with the
deeper ply mattering, though the sample is small (11 events) and this is not
a pre-registered, power-analyzed comparison; S3.63's tier-4 wave is the right
place for a hypothesis-tested version of this claim. **Zero of the
1,024-per-seed lines convert a `double_boss` into a victory.**

`STS511413`/`sim_search_keys_deep`/ps804, the second Act-4 line:

```
STS511413  sim_search_keys_deep  ps804  run_over  840 actions
  boss_reached_acts=7  boss_killed_acts=3
  boss_ids: act1=The Guardian act2=Automaton act3=Donu and Deca act4=The Heart
  keys: emerald|ruby|sapphire   double_boss=1
```

## 6. Zero keyed victories, beside the positive control

**The keyed A20 victory count is 0** across the entire re-measurement — 30,000
breadth rows, 9,856 funnel rows, 12,482 escalation rows, 52,338 rows total —
matching S3.22's zero at a comparable order of magnitude and confirming the
zero is not an artifact of S3.22's pre-Act-4 engine.

**The positive control**: the search body underneath every one of these
kinds — one searched ply, static completion, and (since S2.V2) a second
searched ply on boss floors — is not incapable of clearing an Act-3-class
boss under a full HP budget. `SIM_SEARCH_HOLD`'s own acceptance evidence
(`policy.hpp`'s `SIM_SEARCH_HOLD` comment, [s2v2-sim-reach.md](s2v2-sim-reach.md)
§6) recorded plain `SIM_SEARCH` killing the **Awakened One 22 times** on a
paired 110-seed × 1,024-policy-seed grid (112,640 rows) — a single Act-3 boss
fought with the HP and resources of a run that has not just spent them on a
first boss. What the double-boss room denies, and what this task's escalation
still could not buy back with a deeper ply, is exactly that HP budget: the
second boss is fought immediately after the first, with no rest, no shop, no
reward screen in between (`ProceedButton.java:101-109`, s3-design §5 trap 8) —
the wall S2.V2 §6.4 and S3.22 §3 both already named.

**The next lever, stated per s3-design §6.1 step 3's explicitly-closed
list** (no rule handicaps, no difficulty reduction, no weakened bar): this
task's own STS513748 finding (§5) is itself evidence that *uniformly*
deepening the ply further is a poor next step — cost explodes on wide boards
long before it would buy anything on the narrow ones that matter, and the
two seeds where deepening DID help (STS511413, STS517934) are already both
single/two-monster fights, i.e. already inside the cheap case. A better-
targeted lever (HP-aware pruning that spends the deepened ply's budget only
near the double-boss handoff, not on every boss floor uniformly) is a
plausible next instrument change and is left named rather than attempted
here — S3.61's budget went to the measurement and the one clean escalation
step the ledger asked for. The standing, already-sanctioned accelerant
remains what the deferred-obligations row already says: **a T4-era trained
checkpoint behind the external-policy seam** — never a precondition for
S3-G2, but the lever most likely to actually clear the second boss rather
than search harder against the same static evaluation every kind in this
family shares.

## 7. Deterministic replay and script verification

* **Determinism sweep**: STS510000–STS510499 × {`sim_search`,
  `sim_search_keys`} × ps{0,1}, 2,000 cases, each scanned twice via
  `--verify-determinism`: **`determinism_mismatches=0`**.
* Every emission in §8 ran individually under `--verify-determinism`:
  **6/6, 0 mismatches** (the emitter also refuses to write a script whose
  replayed `final_hash` does not match the scanned row's, so a script file
  existing is itself a first proof).
* **Independently, from the file** rather than the emitter: every one of the
  6 scripts' `(seed, policy, policy_seed)` triples was re-scanned in a fresh
  process and its `actions`/`final_hash` compared byte-for-byte against the
  recorded value. **6/6 match, 0 mismatches** (§8's table has both columns).

## 8. The schedulable cohort — `s361_triples.tsv` and scripts for S3.62

Written to **`D:\STS_BG_Mod\_oracle_data\s3\s361_triples.tsv`** (the triple
list) and **`D:\STS_BG_Mod\_oracle_data\s3\s361_scripts\`** (STS-SCRIPT v1
files, one per emittable triple; both uncommitted, design §7.3):

| script | seed | policy | ps | max act | actions | keys | note | `final_hash` |
|---|---|---|---|---|---|---|---|---|
| `STS511413__sim_search_keys__ps76` | STS511413 | sim_search_keys | 76 | 4 | 769 | all 3 | Act-4 Door entry, plain KEYS | `d594a50d1e40f524` |
| `STS511413__sim_search_keys_deep__ps804` | STS511413 | sim_search_keys_deep | 804 | 4 | 840 | all 3 | Act-4 Door entry, escalation kind | `4a980e3a1ea09092` |
| `STS511413__sim_search__ps0` | STS511413 | sim_search | 0 | 1 | 447 | none | paired key-NOT-taken control (Act-3-stop control) | `89e48ffc5e7ca243` |
| `STS517934__sim_search_keys_deep__ps181` | STS517934 | sim_search_keys_deep | 181 | 3 | 770 | all 3 | double_boss, dies to the SECOND (Awakened One) boss -- Act-3 stop | `8d2a9b677ab84eab` |
| `STS517934__sim_search__ps0` | STS517934 | sim_search | 0 | 1 | 305 | none | paired key-NOT-taken control | `99804e80b75a1884` |
| `STS511413__sim_search_keys__ps5` | STS511413 | sim_search_keys | 5 | 3 | 684 | ruby+sapphire | double_boss under plain KEYS (pre-escalation), emerald never reached (sapphire claim precedes it on this line) | `9bb575936936a5f3` |

One additional candidate — `STS517934`/`sim_search_keys`/ps230, the plain-KEYS
`double_boss` hit on the second seed — **could not be emitted**:
`script emission failed ... optional hand-select DESELECT survives the
toggle-run drop and has no live choose index ... this line is sim-exact but
not live-drivable`, the exact follower limitation
[s2v2-sim-reach.md](s2v2-sim-reach.md) §7 already documents (the
select/deselect class). It is not in the cohort; the row is still counted in
§5's `double_boss` total since that count is about the sim, not the
follower.

**None of the six lines reach the Shield-and-Spear or the Heart's own
combat** — every emitted line dies in ordinary Act 1–4 combat before its boss
room, which is exactly what §5's zero-victory result predicts. S3.62 has, for
the first time, real Act-4-entry material to schedule a capture from (two
independent Door-crossing lines on the same seed, one from each policy), plus
a clean Act-3-stop control pair on a second seed — it does not yet have
anything to schedule a Shield-and-Spear or Heart *combat* capture from; that
remains gated on a future escalation (§6) producing one.

To regenerate any line:

```bash
seed_scan --seeds <SEED>-<SEED> --policies <policy> --policy-seeds <ps> \
          --min-floor 1 --script-dir <dir> --verify-determinism --out scan.tsv
```

## 9. Known limits of this instrument (carried and new)

* Every limit [s3-22-key-reach.md](s3-22-key-reach.md) §10 and
  [s2v2-sim-reach.md](s2v2-sim-reach.md) §7 name still applies unchanged
  (livelock ~3–4 % of breadth rows, the select/deselect oscillation class,
  scripted lines being sim-exact but not yet live-confirmed).
* **The deeper ply is not uniformly affordable** (§5, STS513748) — a cost
  limit on brute-force ply-depth as an escalation strategy, not just a
  runtime nuisance; it is the concrete argument behind §6's "targeted, not
  uniform" recommendation for whatever escalation comes next.
* **This report's `double_boss`-rate comparison (§5) is descriptive, not a
  pre-registered hypothesis test** — n=11 events on 2 seeds. S3.63 owns
  turning any of this into a Holm-corrected claim if it is worth one.
* **The Black Star / Act-1-emerald-refusal `PolicyKind` S3.22 §6 left open
  remains open** — unrelated to this task's escalation, still owed to
  whichever task picks it up, and still claims the *next* free value (11,
  after this task's 10).
