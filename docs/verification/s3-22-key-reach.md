# S3.22 key-aware sim-consulting driver — measured key reach report

Written by S3.22 (2026-09-03). This is the acceptance evidence for the
[s3-tasks.md](../s3-tasks.md) S3.22 row: the measured reach of the key-seeking
sim-consulting policy at scanned scale, the deterministic-replay verification,
the filter controls, and the concrete script list [S3.23](../s3-tasks.md)
schedules its directed captures from. Written in the mould of
[s2v2-sim-reach.md](s2v2-sim-reach.md), which is the instrument this extends.
Scan artifacts (row TSVs, scripts) are regenerable from the commands quoted
here and stay out of the tree, under the design §7.3 data root
`D:\STS_BG_Mod\_oracle_data\s3\`; the committed evidence is this report.

## 0. The headline, stated honestly

* **Keys are now reachable at scale, and they are expensive.** On 10,000
  fresh seeds the new `sim_search_keys` kind carries the **ruby key in
  79.2 %** of lines, the **sapphire in 75.3 %**, the **emerald in 19.7 %**,
  and **all three in 16.4 %**. `sim_search` on the identical grid carries
  effectively none (one accidental ruby in 10,000).
* **The cost is measured, not asserted.** On that same paired grid the
  Act-1 boss KILL rate falls **36.4 % → 22.5 %** (×0.62) and the Act-2 boss
  KILL rate **0.47 % → 0.11 %** (×0.23). Key-seeking buys keys by spending
  survival: a burning elite is a fight the baseline declines, a claimed
  sapphire key is a chest relic thrown away, and a spent Recall is a
  campfire not spent healing.
* **The keyed line reaches the A20 second Act-3 boss room and does not
  clear it.** Across all **39,296** `sim_search_keys` rows (breadth plus
  stages 2–5; the stage-3′ replica is excluded so nothing is counted twice)
  there are **417 Act-3 boss fights, every one of them carrying all three
  keys**, **14 lines that killed the first Act-3 boss while carrying all
  three keys** (the A20 double-boss room entered, over 3 distinct seeds) and
  **zero keyed victories**.
* **Act-4 entry, Shield-and-Spear kills and Heart kills are all zero, and
  the zero is structural rather than a scan failure.** `engine::kFinalAct`
  is still 3 (S3.32 owns the move), so no run can leave Act 3. §4 puts each
  zero beside a positive control on the same rows.
* **One requested cohort could not be produced and the reason is a policy
  property, not bad luck:** a **Black Star burning-elite claim** (S3.11's
  capture 6, §5 trap 6). §6 has the measurement — 380 of 380 emitted Black
  Star lines claim the emerald key in **Act 1**, and Black Star cannot be
  owned before the Act-1 boss chest.

## 1. What ran

Release preset (`win-release`, clang-cl), `seed_scan` at its S2.42 defaults
(`--ascension 20`, `--max-actions 12000`), policies `sim_search` (unchanged)
and `sim_search_keys` (this task's addition,
`tools/fuzz/src/policy_search.cpp`). **62,464 scanned rows, 18,054,009
actions** in six waves, plus a separate determinism sweep (2,000 further rows,
each case scanned twice) and the per-triple emission scans:

| wave | cases | rows |
|---|---|---|
| stage 1, breadth (paired) | STS500000–STS509999 × {`sim_search`, `sim_search_keys`} × ps0 | 20,000 |
| stage 2, depth re-seed (paired) | the 566 all-three-keys act-2 seeds × both policies × ps1–8 | 9,056 |
| stage 3, act-2 depth (keys only) | the 135 keyed-act-2-boss-fight seeds × ps9–72 | 8,640 |
| stage 3′, Black Star replica | the same 135 seeds × ps9–72, `--track-relic "Black Star"` | 8,640 |
| stage 4, act-3 hunt (keys only) | the 39 keyed-act-2-KILL seeds × ps73–328 | 9,984 |
| stage 5, victory hunt (keys only) | the 6 keyed-act-3-boss-fight seeds × ps329–840, then ×ps841–1352 | 6,144 |
| determinism sweep | STS500000–STS500499 × both policies × ps{0,1}, each case scanned twice | 2,000 |

**The seed range is fresh and disjoint from every prior cohort.** The prior
ranges, read off the committed reports and the `_oracle_data` ledgers rather
than remembered: S2.41/S2.42 used **STS00100–STS05099**
(`_oracle_data/s241/s241_scan.tsv`, `s242/s242_scan.tsv`), S2.V2 used
**STS100000–STS199999** ([s2v2-sim-reach.md](s2v2-sim-reach.md) §1, §6.2),
S2.V3 used **STS200000–STS239999** plus one S2.V2 carry-over seed, STS103509
(`_oracle_data/s2v3/*.tsv`, `s2v3/lines/`), and the S2.43
and S3.21 live captures used **STS430000–STS431999**
(`_oracle_data/s243_seeds_*.txt`, `s321_preflight_seed.txt`). No artifact in
either tree names an `STS5xxxxx` seed, so S3.22 takes the **STS5** prefix;
this wave spends STS500000–STS509999 and leaves the rest of it free for S3.23
and S3.62.

Stage 3′ is a deliberate replica, not a second measurement: it is stage 3's
exact case list with one `--track-relic` added, and its aggregate is
**identical** to stage 3's in every column
(`_oracle_data/s3/s322/stage3_a_agg.txt` vs `blackstar_agg.txt`). That is
`seed_scan.hpp`'s "targets only ADD observation; no engine stream or policy
decision reads them" measured rather than assumed.

## 2. Measured reach — baseline against key-seeking

**Stage 1, the paired base rate** (10,000 fresh seeds, one line each, ps0):

| | `sim_search` | `sim_search_keys` | ratio |
|---|---|---|---|
| rows | 10,000 | 10,000 | |
| actions | 2,235,690 | 1,944,023 | ×0.87 |
| act 1 boss FIGHT | 6,966 (69.66 %) | 5,741 (57.41 %) | ×0.82 |
| act 1 boss KILL | 3,643 (36.43 %) | 2,245 (22.45 %) | **×0.62** |
| reached act ≥ 2 | 3,478 (34.78 %) | 2,146 (21.46 %) | ×0.62 |
| act 2 boss FIGHT | 403 (4.03 %) | 138 (1.38 %) | ×0.34 |
| act 2 boss KILL | 47 (0.47 %) | 11 (0.11 %) | **×0.23** |
| reached act ≥ 3 | 46 (0.46 %) | 9 (0.09 %) | ×0.20 |
| act 3 boss FIGHT | 4 | 1 | |
| A20 double-boss room | 1 | 0 | |
| victories | 0 | 0 | |
| **emerald key** | 0 | **1,965 (19.65 %)** | |
| **ruby key** | 1 (0.01 %) | **7,919 (79.19 %)** | |
| **sapphire key** | 0 | **7,530 (75.30 %)** | |
| **all three keys** | 0 | **1,638 (16.38 %)** | |
| all three + act-3 boss fight | 0 | 1 | |
| **Act-4 entry** | 0 | 0 | |
| **Shield-and-Spear kill** | 0 | 0 | |
| **Heart kill** | 0 | 0 | |
| livelock | 410 (4.10 %) | 318 (3.18 %) | |
| action_cap | 6 (0.06 %) | 6 (0.06 %) | |
| fuzz failures | 0 | 0 | |

Two rows in that table are worth reading slowly. The **single accidental ruby
key under `sim_search`** is not noise in the observation: `SIM_SEARCH` scores
`MoveCat::RECALL` at 250, below every other campfire band, so it takes the
Recall only on a campfire where nothing else is legal — once in 10,000 lines.
And the **`sim_search` double-boss row** (STS508004 / ps0, floor 51) is the
positive control for the new `double_boss` probe: the column is capable of
being 1, so the keyed zeros in this table are measurements.

**Stage 2, the re-seeding depth pass** — the 566 seeds on which the key
policy carried all three keys and reached Act 2 at ps0, re-run on **both**
policies at ps1–8, so the comparison stays paired on the cohort unit
((seed, policy-seed) is the unit, the S2.V2 wave structure):

| | `sim_search` | `sim_search_keys` |
|---|---|---|
| rows | 4,528 | 4,528 |
| act 1 boss KILL | 3,334 (73.63 %) | 3,066 (67.71 %) |
| act 2 boss FIGHT | 549 (12.12 %) | 330 (7.29 %) |
| act 2 boss KILL | 72 (1.59 %) | 30 (0.66 %) |
| act 3 boss FIGHT | 4 | 2 |
| all three keys | 0 | 3,268 (72.17 %) |
| A20 double-boss room / victories | 0 / 0 | 0 / 0 |

The gap narrows on selected seeds (×0.92 on the Act-1 kill, against ×0.62 at
breadth) and the Act-2 kill gap stays at ×0.41: re-seeding recovers most of
the survival the key rules cost, and none of the Act-2 depth.

**Stages 3–5, key-policy only** (the S2.V2 stage-3/4 shape: a hunt, not a
paired arm):

| wave | rows | act-2 KILL | act-3 FIGHT | all three keys | **double-boss room** | victories |
|---|---|---|---|---|---|---|
| stage 3 (135 seeds × ps9–72) | 8,640 | 259 (3.00 %) | 22 (0.25 %) | 6,761 (78.25 %) | 0 | 0 |
| stage 4 (39 seeds × ps73–328) | 9,984 | 899 (9.00 %) | 89 (0.89 %) | 8,528 (85.42 %) | **3** | 0 |
| stage 5a (6 seeds × ps329–840) | 3,072 | 978 (31.84 %) | 145 (4.72 %) | 2,932 (95.44 %) | **5** | 0 |
| stage 5b (6 seeds × ps841–1352) | 3,072 | 1,020 (33.20 %) | 158 (5.14 %) | 2,944 (95.83 %) | **6** | 0 |

Every one of the **417** Act-3 boss fights `sim_search_keys` produced across
every wave (1 + 2 + 22 + 89 + 145 + 158, the stage-3′ replica excluded) is a
line carrying all three keys — the `all3 + act-3 fight` column equals the
`act-3 fight` column in every wave — which is what makes them S3-G2-relevant
rather than merely deep.

## 3. The fourteen keyed first-Act-3-boss kills, and the missing victory

The **`double_boss` probe** is the exact witness for "the first Act-3 boss
died" and is new in this task: `room_type == Boss && act == kFinalAct &&
boss_cursor >= 1`. `boss_cursor` counts boss rooms COMPLETED
(`run_advance.cpp`'s `next_room_transition_impl`), so standing in a final-act
boss room with it ≥ 1 is `goToDoubleBoss`'s synthetic `MapRoomNode(-1, 15)`.
It exists because [s2v2-sim-reach.md](s2v2-sim-reach.md) §6.1 had to
reconstruct the same fact from `max_floor == 51` after the `victory` probe
gave that campaign a false "0 Awakened One kills"; a column is cheaper than a
correction.

Fourteen rows set it, over **three distinct seeds**:

| seed | policy_seed(s) | `boss_ids[act 3]` after the handoff | keys |
|---|---|---|---|
| STS502962 | 226, 272, 384, 945, 1088 | Time Eater | all three |
| STS506383 | 173, 483, 800, 812, 1138, 1171, 1196, 1315 | Awakened One | all three |
| STS508459 | 749 | Donu and Deca | all three |

The `boss_ids[act 3]` column names the **second** boss, not the first: the
double-boss handoff overwrites the mirror with `bossList.get(0)`
(`run_advance.cpp:2865-2890`, the S2.43 finding), so these rows say "the run
is standing in front of Time Eater / Awakened One / Donu and Deca having
already killed the other one".

**Zero of them win.** That is the same wall S2.V2 §6.4 recorded for its
Awakened-first lines — the second boss finishes a run whose HP pool paid for
the first — made harder here by the keys, since the emerald elite is an extra
A20 elite fight and the sapphire key costs a chest relic that would otherwise
be carried into Act 3.

**The escalation ladder was entered, and its first lever is spent.** s3-design
§6.1 step 3 pre-registers the order: more policy-seed budget on seeds already
known to reach, then a deeper boss-floor ply, and **not** rule handicaps.
Stages 4, 5a and 5b are that first lever — up to 1,024 policy seeds on the
deepest seeds, **16,128 rows, 6,018,290 actions** — and it moved the
double-boss count from 0 to 14 without producing a victory. **The next lever is therefore
the deeper boss-floor ply**, which is a change to the search body and is
recorded here as the recommended next step rather than taken in this task
(the S3.22 deliverable is the instrument and the measurement; a search-depth
change would move `SIM_SEARCH` too unless it is a fourth kind again).

## 4. The zeros, each beside a positive control

Every act-4 number in this report is zero, and the reason is named in the
S3.22 block's own `Inherited` line: S3.21 moved the planner's `kMaxActs` to 4
so the vocabulary exists, and `engine::kFinalAct` is **still 3** (S3.32 owns
that move), so no run can leave Act 3 and an act-4 clause is answerable and
matches nothing. Rather than assert that, the same rows were asked both
questions (`_oracle_data/s3/s322/filter_controls.txt` and the deep-triple
control):

Three deep triples (STS502962/ps226, STS506383/ps173, STS508459/ps749),
identical rows under every clause:

| clause | qualifying |
|---|---|
| `--need-act 3` | **3 of 3** |
| `--need-act 4` | 0 of 3 |
| `--need-boss-act 3` | **3 of 3** |
| `--need-boss-act 4` | 0 of 3 |
| `--need-keys --need-act 3` | **3 of 3** |
| `--need-keys --need-act 4` | 0 of 3 |
| `--need-heart-kill` | 0 of 3 |

And on a fixed 200-seed slice (STS500000–STS500199 × `sim_search_keys` × ps0),
the key clauses answering non-trivially beside the act-4 ones answering zero:

| clause | qualifying seeds (of 200) |
|---|---|
| (none) | 200 |
| `--need-key emerald` | 42 |
| `--need-key ruby` | 164 |
| `--need-key sapphire` | 155 |
| `--need-keys` | 35 |
| `--need-act 2` | 38 |
| `--need-boss-kill-act 1` | 41 |
| `--need-act 4` / `--need-boss-act 4` / `--need-boss-kill-act 4` / `--need-heart-kill` | **0** |

The **Shield-and-Spear kill rate** is zero for the same structural reason,
and it has its own positive control in the new `elite_killed_acts` column
(`room_type == Elite && phase == COMBAT_REWARD` — an elite room always opens
a reward screen; only a non-endless TheBeyond BOSS is suppressed,
AbstractRoom.java:327). The probe fires in acts 1, 2 **and 3** and is zero
only in act 4:

| wave | a1 | a2 | a3 | a4 |
|---|---|---|---|---|
| stage 1, `sim_search` | 6,116 | 327 | 4 | **0** |
| stage 1, `sim_search_keys` | 5,590 | 116 | 0 | **0** |
| stages 4–5 (16,128 rows) | 15,603 | 5,599 | 275 | **0** |

So "0 Shield-and-Spear kills" is a statement about act 4 existing, not about
the counter.

## 5. The policy, and what keeps `sim_search` untouched

`sim_search_keys` (`PolicyKind` value **8**, `COUNT` 8 → 9) is `sim_search`
plus exactly four run-layer rules, each gated on `kind == SIM_SEARCH_KEYS` at
one site:

* **K1 — the key reward rows.** `EMERALD_KEY` and `SAPPHIRE_KEY` score
  `kKeyRowClaim` (1200), above the 900 relic row, so the row is claimed
  first. The emerald row is free (a free-standing row appended after the
  relic, MonsterRoomElite.java:94-98); **the sapphire row is not** — claiming
  it silently destroys the chest's relic row unrewarded
  (RewardItem.java:317-326), which is exactly why `sim_search` scores both
  rows `kRewardCardClosed` and why this is a separate kind rather than a
  scoring-table repair.
* **K2 — the campfire.** `MoveCat::RECALL` scores 750, above the 700 pre-boss
  rest, while HP is above 50 % of max; below that gate it scores
  `sim_search`'s unchanged 250 and the run heals. "Exactly one campfire on
  RECALL" is enforced by the **game**, not by a counter: the button exists
  only while `!hasRubyKey` (CampfireUI.java:94-96, `rest_sites.cpp:203`), so
  taking it is the only thing that removes it.
* **K3 — the burning elite.** A map candidate whose destination is the act's
  `emerald_x`/`emerald_y` node gets +30,000 (100 player HP in the run
  evaluation's currency) while the emerald key is unheld and HP is above
  60 %. A candidate that merely **keeps the node reachable** gets +8,000,
  decided by exact forward-edge reachability over the 15×7 map DAG
  (`node_reaches_emerald`, a row-by-row 7-bit frontier over
  `kEdgeLeft/Center/Right`) — not by a "steer towards the column" heuristic,
  which would be wrong on any map whose columns do not connect. The test is
  conservative: Wing Boots can jump to an unconnected node, which only adds
  reachability. The approach band is what makes the emerald key reachable at
  all — on a 50-seed A/B during development the emerald carry rate was
  **8 % without it and 18 % with it**.
* **K4 — the other two rooms.** +15,000 for a Treasure destination while the
  sapphire key is unheld, +10,000 for a Rest destination while the ruby key
  is unheld and HP allows.

The bonuses are added **after** the one-floor rollout rather than folded into
`run_layer_eval`, so the rollout keeps pricing the fight — and death — exactly
as the baseline does: a line that dies on the burning elite inside the rollout
still scores `kEvalDefeat` and is refused.

**`sim_search` is provably untouched, and the proof is a hash rather than an
argument.** The same fixed scan — STS500000–STS500199 × `sim_search` ×
ps{0,1}, 400 rows — was run on the base commit `e39aa7b` and again on this
branch. The three columns S3.22 appends (`keys`, `elite_killed_acts`,
`double_boss`) are new output by construction, so the comparison is over the
twenty pre-existing columns, `final_hash` among them:

```bash
seed_scan --seeds STS500000-STS500199 --policies sim_search --policy-seeds 0,1 \
          --out before.tsv          # on e39aa7b
seed_scan --seeds STS500000-STS500199 --policies sim_search --policy-seeds 0,1 \
          --out after.tsv           # on this branch
tr -d '\r' < before.tsv                 > inv_before.tsv
tr -d '\r' < after.tsv  | cut -f1-20    > inv_after.tsv
sha256sum inv_before.tsv inv_after.tsv
```

Both files: **`91e84391ad1c16a2e8b498d18138851e1363adb75ec8943f67f86368574d355b`**,
`cmp` clean. (`tr -d '\r'` is a line-ending normalisation only — the tool
writes CRLF on Windows and `cut` drops the CR with the trailing field.)

## 6. The one cohort that could not be produced: Black Star

S3.11's Log names six captures S3.23 needs; the sixth is a **burning-elite
claim on a Black Star run**, the only shape in which
`addPotionToRewards`' four-item suppression (AbstractRoom.java:597-599, §5
trap 6) is reachable, because gold + relic + `EMERALD_KEY` is three rows and
not four.

`sim_search_keys` cannot produce it, and the reason is a property of the rule
set rather than of the seeds. Stage 3′ tracked the relic over 8,640 rows:
**436 rows acquired Black Star**, of which **380 also carried the emerald key
and reached act ≥ 2**. All 380 were emitted as STS-SCRIPT files and the act of
the `EMERALD_KEY` claim step read off each:

```
scripts emitted: 380
emerald claims by act:
    380 "act":1
```

**380 of 380 in Act 1.** Black Star is a BOSS relic and can only be owned from
the Act-1 boss chest onward; K3 claims the emerald key at the first burning
elite the run can reach, which is in Act 1 on essentially every map, and once
the key is held `setEmeraldElite` places no burning elite in any later act
(the very gate S3.11 (c) added). The two events are therefore ordered apart by
construction.

The constructive route, handed to **S3.23** rather than guessed at: a
one-rule variant that refuses the emerald row while `act == 1` (a fifth K-rule
in a fifth `PolicyKind`, on the S2.V2 "separate kind, never a change to
SIM_SEARCH" precedent), or a hand-written script line on a seed whose Act-1
burning elite is unreachable. Either way it is a policy change, and S3.22's
deliverable list does not contain one — so this report records the measured
impossibility instead of a silence.

## 7. Deterministic replay — the acceptance bar

* `--verify-determinism` over **STS500000–STS500499 × {`sim_search`,
  `sim_search_keys`} × ps{0,1}** (2,000 rows, every case scanned twice, run
  as four seed-range shards): **`determinism_mismatches=0`** in every shard,
  all four exit 0.
* Every emitted cohort script in §8 was written under `--verify-determinism`
  individually: **18/18, 0 mismatches**.
* Every emitted script is replay-verified at write time by the emitter (it
  re-drives the trajectory from `run_begin` and refuses to write a file whose
  terminal `fuzz::hash_controller` does not equal the scanned row's
  `final_hash`), and — independently, from the **file** rather than from the
  emitter — every script was re-verified afterwards by re-scanning the triple
  its header names and comparing hashes and step counts:
  **`scripts OK=18 MISMATCH=0`** (`_oracle_data/s3/s322/script_verify.txt`).

## 8. The schedulable cohort — the script list for S3.23

Written to **`D:\STS_BG_Mod\_oracle_data\s3\s322_scripts\`** (uncommitted, the
design §7.3 data root). Nine seeds, each with a **pair**: the key-seeking line
and, on the **same seed**, the `sim_search` line that takes no key — which is
what makes the pair a control rather than two runs.

| script (`<name>.script.jsonl`) | steps | max act | max floor | emerald claim | sapphire claim | recall | chest branch | `final_hash` |
|---|---|---|---|---|---|---|---|---|
| `STS500270__sim_search_keys__ps0` | 193 | 2 | 19 | 1 | 1 | 1 | SAPPHIRE_KEY | `470875c268242ae6` |
| `STS500270__sim_search__ps0` | 261 | 2 | 22 | 0 | 0 | 0 | RELIC | `0208c5da9a175309` |
| `STS503370__sim_search_keys__ps0` | 174 | 2 | 21 | 1 | 1 | 1 | SAPPHIRE_KEY | `9101ae4f1c0ff85c` |
| `STS503370__sim_search__ps0` | 217 | 2 | 27 | 0 | 0 | 0 | RELIC | `49b495139a01d733` |
| `STS506060__sim_search_keys__ps0` | 197 | 2 | 25 | 1 | 1 | 1 | SAPPHIRE_KEY | `aba2c089c2446d43` |
| `STS506060__sim_search__ps0` | 214 | 2 | 24 | 0 | 0 | 0 | RELIC | `03dbbe9b73259a9a` |
| `STS507768__sim_search_keys__ps0` | 166 | 2 | 21 | 1 | 1 | 1 | SAPPHIRE_KEY | `dc5dde9f0f1ed4a1` |
| `STS507768__sim_search__ps0` | 166 | 2 | 21 | 0 | 0 | 0 | RELIC | `954f692c0140882d` |
| `STS508399__sim_search_keys__ps0` | 185 | 2 | 18 | 1 | 1 | 1 | SAPPHIRE_KEY | `c121342acfca4499` |
| `STS508399__sim_search__ps0` | 305 | 2 | 25 | 0 | 0 | 0 | RELIC | `d595f273c20fa6f4` |
| `STS509397__sim_search_keys__ps0` | 182 | 2 | 18 | 1 | 1 | 1 | SAPPHIRE_KEY | `5dae6c8a761033b2` |
| `STS509397__sim_search__ps0` | 282 | 2 | 21 | 0 | 0 | 0 | RELIC | `e429c67ad0d3d961` |
| `STS502962__sim_search_keys__ps226` | 773 | **3** | **51** | 1 | 1 | 1 | SAPPHIRE_KEY | `577f6fafe6272e4d` |
| `STS502962__sim_search__ps0` | 440 | 2 | 33 | 0 | 0 | 0 | RELIC | `f8c312f2b0ef6a15` |
| `STS506383__sim_search_keys__ps173` | 588 | **3** | **51** | 1 | 1 | 1 | SAPPHIRE_KEY | `884cf591d430627c` |
| `STS506383__sim_search__ps0` | 287 | 2 | 33 | 0 | 0 | 0 | RELIC | `aacb8a12f05b072e` |
| `STS508459__sim_search_keys__ps749` | 684 | **3** | **51** | 1 | 1 | 1 | SAPPHIRE_KEY | `e7d5fc62b7bae422` |
| `STS508459__sim_search__ps0` | 499 | 2 | 33 | 0 | 0 | 0 | RELIC | `5e54c94e8f7f4e8f` |

Mapped onto the six captures S3.11's Log asks S3.23 for:

1. **Emerald claim + the run continuing into the next act's map generation**
   — every `sim_search_keys` line above: each claims `EMERALD_KEY` at a
   burning elite in Act 1 and crosses into Act 2 (max act ≥ 2). §5 trap 1's
   whole content is this crossing, not the claim record.
2. **The paired key-NOT-taken control on the same seed** — the
   `sim_search` line directly beneath each, which reaches Act 2 on the same
   seed having claimed no key, so `setEmeraldElite` keeps its `mapRng` draw
   and the two Act-2 maps must **differ**.
3. **Sapphire claim, KEY branch** — the `SAPPHIRE_KEY` chest branch in every
   `sim_search_keys` line.
4. **Sapphire claim, RELIC branch** — the `RELIC` chest branch in every
   control, at the **same chest on the same floor**. On STS507768 the pair is
   exact: step `i:54` of the control opens the chest at floor 9 and claims
   `RELIC Kunai`; step `i:71` of the keys line opens the same chest and claims
   `SAPPHIRE_KEY`, after which the Kunai row is gone and the next claim is
   `GOLD` — the destructive link, visible in two scripts on one seed.
5. **A ruby Recall** — the `rest`/`recall` step in every `sim_search_keys`
   line (and in none of the controls).
6. **A Black Star burning-elite claim** — **not produced**; §6 has the
   measurement and the constructive route.

Three of the keyed lines reach **floor 51 with all three keys** — a live
capture of one of those is the deepest keyed line this instrument can offer
today, and is what an Act-4 attempt will eventually be built from.

To regenerate any line's script:

```bash
seed_scan --seeds <SEED>-<SEED> --policies <policy> --policy-seeds <ps> \
          --min-floor 1 --script-dir <dir> --verify-determinism --out scan.tsv
```

## 9. The follower already speaks these steps

The emitter's vocabulary for all three new decisions was already in place when
S3.22 started — `reward_kind_text` learned `EMERALD_KEY`/`SAPPHIRE_KEY` in
S3.11 (CommunicationMod's own `RewardItem.RewardType.name()` spellings) and the
`rest` step has emitted `opt: "recall"` since S2.V2 (the campfire button's live
name is `RecallOption` → `recall`, `ChoiceScreenUtils.getCampfireOptionName`).
So **no change to `script_policy_cmd.py` was needed**, and that claim is
checked rather than asserted: `match_step` was driven offline against
synthesized CommunicationMod dumps in the shapes `GameStateConverter`
emits them (`_oracle_data/s3/s322/follower_check.txt`):

```
emerald claim (emitted step)                   -> choose 2
sapphire claim, KEY branch (emitted step)      -> choose 1
sapphire control, RELIC branch                 -> choose 0
recall (emitted step)                          -> choose 2
NEG emerald claim on a screen without the row  -> Divergence: reward screen has no #0 EMERALD_KEY row ...
NEG recall on a campfire that does not offer it -> Divergence: rest option: no choice named 'recall' ...
NEG sapphire claim when the row is not legal   -> Divergence: derived command 'choose 1' ... not among the 1 legal candidates
```

Three positives resolve to a live `choose N`; three negative controls raise
`Divergence`, i.e. the follower stops the run rather than improvising — the
stop-on-desync contract the whole seam rests on.

## 10. Known limits of this instrument

* **The key rules are a cost, and the cost compounds with depth.** ×0.62 on
  the Act-1 kill and ×0.23 on the Act-2 kill at breadth (§2). A cohort that
  needs both keys and depth pays both.
* **Zero keyed victories, so no keyed Act-4 attempt exists yet**, and the
  next pre-registered lever is a deeper boss-floor ply (§3). This report
  does not weaken any bar to hide that; the S3.22 Acceptance says a missing
  Act-4 line is a reportable result and this is the report.
* **Act 4, the Heart and Shield-and-Spear are unreachable by construction
  until S3.32 moves `engine::kFinalAct`.** Every filter that names them
  parses, answers, and answers zero (§4). **S3.32 must re-run §4's table**;
  it is the cheapest possible check that the move landed.
* **The Black Star conjunction is unreachable under this policy** (§6), so
  S3.11's sixth capture is still owed and now has a named reason.
* **`double_boss` names the ROOM, not the kill of the second boss.** It is
  the exact witness for the first Act-3 boss dying, which is what S2.V2 §6.1
  had to reconstruct; the second boss's death is still `victory`.
* **A scripted line is sim-exact, not yet live-confirmed.** The zero-diff
  confirmation of these 18 scripts is **S3.23**'s, through
  `script_policy_cmd.py`'s stop-on-desync contract; any desync it records is
  Stage-B capture evidence by design. Every follower limit
  [s2v2-sim-reach.md](s2v2-sim-reach.md) §7 lists (the four glue rules, the
  livelock class) applies here unchanged.
* **Livelock is ~3–4 % of rows**, the same select/deselect oscillation
  s2v2-sim-reach §7 documents; those rows simply fail to qualify for cohorts
  and never reach an emitted script.
