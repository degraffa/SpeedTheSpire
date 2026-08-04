# Stage B verification report

Generated deterministically by `tools/verify_report/generate_report.py`.
This is evidence accounting, not a G7 acceptance inference.

## G7 oracle evidence (literal)

- Distinct seeds: **3175**; shortfall to 2,000: **0**.
- Captured actions: **186035**.
- Replay-clean actions: **167695**.
- Strict zero-diff actions: **166675** (reported diagnostically; no action-count quota).
- Replay-recognized capture races: **22 records across 22 runs**; those runs are excluded from the strict total.
- Generator policies: **greedy, random-legal**; mixed-generator requirement met: **YES**.
- A20 Ironclad source runs: **3175 / 3175**; A20 modifier rows with tier-2 coverage: **20 / 20**; modifier criterion met: **YES**.
- Gameplay-terminal full-run attempts (death or Act-1 boss reward): **3175 / 3175**.
- Act-1 boss-reward claims: **23**; by boss: **Hexaghost=6, Slime Boss=10, The Guardian=7**; all three met: **YES**.
- Untriaged findings: **0**. A finding counts as triaged only when an exact campaign/seed/classification disposition exists.
- Open findings: **0**. An `open-*` disposition is reviewed but cannot satisfy the gate.
- Oracle breadth/depth criterion met: **YES**.

## Campaign inputs

| Campaign | Policy | Requested runs | Included distinct | Deduplicated |
|---|---|---:|---:|---:|
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | random-legal | 3000 | 3000 | 0 |
| g7_late_act1_b153_20260801_boss_min4 | greedy | 28 | 28 | 0 |
| g7_late_act1_b153_20260801_boss_min4_scan14k | greedy | 147 | 147 | 0 |

## Diff rates

- State-divergence runs per million captured actions: **682.667** (127 findings).
- All non-clean runs per million captured actions: **682.667** (127 findings).

## Divergence inventory

| Campaign | Seed | Classification | Disposition | Reference |
|---|---|---|---|---|
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS325092 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS325500 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS325776 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS325796 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS325972 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS326200 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS326268 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS326376 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS326492 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS326864 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS326924 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS327264 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS327536 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS327676 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS327812 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS328044 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS328164 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS328268 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS328316 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS328384 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS328772 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS328816 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS328968 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS328976 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS329448 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS329464 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS329544 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS329656 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS330924 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS331152 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS331608 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS331992 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS332232 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS332276 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS332332 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS332536 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS332680 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS332884 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS333032 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS333056 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS333100 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS333108 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS333508 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS333528 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS333584 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS334468 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS335168 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS335176 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS335220 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS335616 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS336072 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS336308 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS336592 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_random_parallel_b153_20260802_325000_336999.worker-001-of-004 | STS336812 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4 | STS400075 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4 | STS400327 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4 | STS400656 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4 | STS400818 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4 | STS401080 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4 | STS401126 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4 | STS401257 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4 | STS401321 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4 | STS401351 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4 | STS401439 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4 | STS401635 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4 | STS401784 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS402015 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS402072 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS402815 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS402852 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS403271 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS403429 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS403479 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS403498 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS403761 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS403945 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS404000 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS404056 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS404138 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS404140 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS404620 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS405111 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS405688 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS405717 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS405809 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS405874 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS405981 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS406682 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS406737 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS406751 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS406763 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS406808 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS407222 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS407381 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS407605 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS407711 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS407979 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS408083 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS408517 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS408677 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS408815 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS408898 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS409143 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS409642 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS410366 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS410404 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS410602 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS410822 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS411202 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS411361 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS411502 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS411617 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS411694 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS411802 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS411938 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS412075 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS412123 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS412424 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS412788 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS412843 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS413052 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS413172 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS413197 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS413217 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS413504 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS414014 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| g7_late_act1_b153_20260801_boss_min4_scan14k | STS414142 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |

Open dispositions remain open defects or harness gaps; disposition is not acceptance.

## Registry coverage and oracle sightings

- Tier-2 rows covered: **452 / 452**.
- Rows with a `game_id` and zero oracle sightings: **6**.

| Domain | ID | Row | game_id | Oracle sightings | Tier-2 |
|---|---:|---|---|---:|---|
| cards | 1 | STRIKE | Strike_R | 1507997 | direct |
| cards | 2 | DEFEND | Defend_R | 1208585 | direct |
| cards | 3 | BASH | Bash | 606205 | direct |
| cards | 4 | SHRUG_IT_OFF | Shrug It Off | 15876 | direct |
| cards | 5 | POMMEL_STRIKE | Pommel Strike | 20471 | direct |
| cards | 6 | ARMAMENTS | Armaments | 13104 | direct |
| cards | 7 | FLEX | Flex | 14040 | direct |
| cards | 8 | HAVOC | Havoc | 12383 | direct |
| cards | 9 | TRUE_GRIT | True Grit | 14720 | direct |
| cards | 10 | WARCRY | Warcry | 14610 | direct |
| cards | 11 | ANGER | Anger | 32532 | direct |
| cards | 12 | BODY_SLAM | Body Slam | 17414 | direct |
| cards | 13 | CLASH | Clash | 36043 | direct |
| cards | 14 | CLEAVE | Cleave | 20417 | direct |
| cards | 15 | CLOTHESLINE | Clothesline | 27187 | direct |
| cards | 16 | HEADBUTT | Headbutt | 20217 | direct |
| cards | 17 | HEAVY_BLADE | Heavy Blade | 27746 | direct |
| cards | 18 | IRON_WAVE | Iron Wave | 24557 | direct |
| cards | 19 | PERFECTED_STRIKE | Perfected Strike | 15454 | direct |
| cards | 20 | SWORD_BOOMERANG | Sword Boomerang | 30487 | direct |
| cards | 21 | THUNDERCLAP | Thunderclap | 16897 | direct |
| cards | 22 | TWIN_STRIKE | Twin Strike | 25422 | direct |
| cards | 23 | WILD_STRIKE | Wild Strike | 33454 | direct |
| cards | 24 | WOUND | Wound | 15030 | direct |
| cards | 25 | BURN | Burn | 8266 | direct |
| cards | 26 | DAZED | Dazed | 43824 | direct |
| cards | 27 | SLIMED | Slimed | 77498 | direct |
| cards | 28 | VOID | Void | 0 | direct |
| cards | 29 | CLUMSY | Clumsy | 3910 | direct |
| cards | 30 | DECAY | Decay | 5280 | direct |
| cards | 31 | DOUBT | Doubt | 15880 | direct |
| cards | 32 | INJURY | Injury | 4014 | direct |
| cards | 33 | NORMALITY | Normality | 3740 | direct |
| cards | 34 | PAIN | Pain | 4154 | direct |
| cards | 35 | PARASITE | Parasite | 3992 | direct |
| cards | 36 | REGRET | Regret | 12744 | direct |
| cards | 37 | SHAME | Shame | 2404 | direct |
| cards | 38 | WRITHE | Writhe | 5142 | direct |
| cards | 39 | ASCENDERS_BANE | AscendersBane | 310730 | direct |
| cards | 40 | BLOOD_FOR_BLOOD | Blood for Blood | 13737 | direct |
| cards | 41 | CARNAGE | Carnage | 12273 | direct |
| cards | 42 | DROPKICK | Dropkick | 7487 | direct |
| cards | 43 | HEMOKINESIS | Hemokinesis | 14772 | direct |
| cards | 44 | PUMMEL | Pummel | 6569 | direct |
| cards | 45 | RAMPAGE | Rampage | 8189 | direct |
| cards | 46 | RECKLESS_CHARGE | Reckless Charge | 6227 | direct |
| cards | 47 | SEARING_BLOW | Searing Blow | 8024 | direct |
| cards | 48 | SEVER_SOUL | Sever Soul | 14071 | direct |
| cards | 49 | UPPERCUT | Uppercut | 9366 | direct |
| cards | 50 | WHIRLWIND | Whirlwind | 8652 | direct |
| cards | 51 | BATTLE_TRANCE | Battle Trance | 4771 | direct |
| cards | 52 | BLOODLETTING | Bloodletting | 5215 | direct |
| cards | 53 | BURNING_PACT | Burning Pact | 6140 | direct |
| cards | 54 | DISARM | Disarm | 4358 | direct |
| cards | 55 | DUAL_WIELD | Dual Wield | 4320 | direct |
| cards | 56 | ENTRENCH | Entrench | 4221 | direct |
| cards | 57 | FLAME_BARRIER | Flame Barrier | 5819 | direct |
| cards | 58 | GHOSTLY_ARMOR | Ghostly Armor | 7044 | direct |
| cards | 59 | INFERNAL_BLADE | Infernal Blade | 7180 | direct |
| cards | 60 | INTIMIDATE | Intimidate | 5823 | direct |
| cards | 61 | POWER_THROUGH | Power Through | 6622 | direct |
| cards | 62 | RAGE | Rage | 5911 | direct |
| cards | 63 | SECOND_WIND | Second Wind | 5874 | direct |
| cards | 64 | SEEING_RED | Seeing Red | 4297 | direct |
| cards | 65 | SENTINEL | Sentinel | 6362 | direct |
| cards | 66 | SHOCKWAVE | Shockwave | 5255 | direct |
| cards | 67 | SPOT_WEAKNESS | Spot Weakness | 5367 | direct |
| cards | 68 | COMBUST | Combust | 6816 | direct |
| cards | 69 | DARK_EMBRACE | Dark Embrace | 4398 | direct |
| cards | 70 | EVOLVE | Evolve | 5438 | direct |
| cards | 71 | FEEL_NO_PAIN | Feel No Pain | 4798 | direct |
| cards | 72 | FIRE_BREATHING | Fire Breathing | 4254 | direct |
| cards | 73 | INFLAME | Inflame | 6268 | direct |
| cards | 74 | METALLICIZE | Metallicize | 9173 | direct |
| cards | 75 | RUPTURE | Rupture | 4379 | direct |
| cards | 76 | BARRICADE | Barricade | 4122 | direct |
| cards | 77 | BERSERK | Berserk | 6325 | direct |
| cards | 78 | BLUDGEON | Bludgeon | 5287 | direct |
| cards | 79 | BRUTALITY | Brutality | 5612 | direct |
| cards | 80 | CORRUPTION | Corruption | 4029 | direct |
| cards | 81 | DEMON_FORM | Demon Form | 4739 | direct |
| cards | 82 | DOUBLE_TAP | Double Tap | 6189 | direct |
| cards | 83 | EXHUME | Exhume | 5640 | direct |
| cards | 84 | FEED | Feed | 5024 | direct |
| cards | 85 | FIEND_FIRE | Fiend Fire | 5192 | direct |
| cards | 86 | IMMOLATE | Immolate | 11867 | direct |
| cards | 87 | IMPERVIOUS | Impervious | 3040 | direct |
| cards | 88 | JUGGERNAUT | Juggernaut | 5229 | direct |
| cards | 89 | LIMIT_BREAK | Limit Break | 5762 | direct |
| cards | 90 | OFFERING | Offering | 4890 | direct |
| cards | 91 | REAPER | Reaper | 3543 | direct |
| cards | 92 | BANDAGE_UP | Bandage Up | 1290 | direct |
| cards | 93 | BLIND | Blind | 2511 | direct |
| cards | 94 | DARK_SHACKLES | Dark Shackles | 978 | direct |
| cards | 95 | DEEP_BREATH | Deep Breath | 1120 | direct |
| cards | 96 | DISCOVERY | Discovery | 852 | direct |
| cards | 97 | DRAMATIC_ENTRANCE | Dramatic Entrance | 2640 | direct |
| cards | 98 | ENLIGHTENMENT | Enlightenment | 1424 | direct |
| cards | 99 | FINESSE | Finesse | 3046 | direct |
| cards | 100 | FLASH_OF_STEEL | Flash of Steel | 2332 | direct |
| cards | 101 | FORETHOUGHT | Forethought | 1234 | direct |
| cards | 102 | GOOD_INSTINCTS | Good Instincts | 1633 | direct |
| cards | 103 | IMPATIENCE | Impatience | 1252 | direct |
| cards | 104 | JACK_OF_ALL_TRADES | Jack Of All Trades | 368 | direct |
| cards | 105 | MADNESS | Madness | 2082 | direct |
| cards | 106 | MIND_BLAST | Mind Blast | 1440 | direct |
| cards | 107 | PANACEA | Panacea | 1838 | direct |
| cards | 108 | PANIC_BUTTON | PanicButton | 1563 | direct |
| cards | 109 | PURITY | Purity | 920 | direct |
| cards | 110 | SWIFT_STRIKE | Swift Strike | 2172 | direct |
| cards | 111 | TRIP | Trip | 1814 | direct |
| cards | 112 | APOTHEOSIS | Apotheosis | 1196 | direct |
| cards | 113 | CHRYSALIS | Chrysalis | 1422 | direct |
| cards | 114 | HAND_OF_GREED | HandOfGreed | 601 | direct |
| cards | 115 | MAGNETISM | Magnetism | 1170 | direct |
| cards | 116 | MASTER_OF_STRATEGY | Master of Strategy | 1444 | direct |
| cards | 117 | MAYHEM | Mayhem | 1643 | direct |
| cards | 118 | METAMORPHOSIS | Metamorphosis | 1176 | direct |
| cards | 119 | PANACHE | Panache | 1496 | direct |
| cards | 120 | SADISTIC_NATURE | Sadistic Nature | 739 | direct |
| cards | 121 | SECRET_TECHNIQUE | Secret Technique | 568 | direct |
| cards | 122 | SECRET_WEAPON | Secret Weapon | 1520 | direct |
| cards | 123 | THE_BOMB | The Bomb | 1421 | direct |
| cards | 124 | THINKING_AHEAD | Thinking Ahead | 1626 | direct |
| cards | 125 | TRANSMUTATION | Transmutation | 1303 | direct |
| cards | 126 | VIOLENCE | Violence | 1468 | direct |
| cards | 127 | CURSE_OF_THE_BELL | CurseOfTheBell | 2677 | direct |
| powers | 1 | STRENGTH | Strength | 99382 | direct |
| powers | 2 | VULNERABLE | Vulnerable | 47136 | direct |
| powers | 3 | WEAK | Weakened | 30944 | direct |
| powers | 4 | ARTIFACT | Artifact | 10780 | direct |
| powers | 5 | METALLICIZE | Metallicize | 9173 | direct |
| powers | 6 | FEEL_NO_PAIN | Feel No Pain | 4798 | direct |
| powers | 7 | DARK_EMBRACE | Dark Embrace | 4398 | direct |
| powers | 8 | COMBUST | Combust | 6816 | direct |
| powers | 9 | RUPTURE | Rupture | 4379 | direct |
| powers | 10 | SADISTIC | Sadistic | 118 | direct |
| powers | 11 | CORRUPTION | Corruption | 4029 | direct |
| powers | 12 | RAGE | Rage | 5911 | direct |
| powers | 13 | LOSE_STRENGTH | Flex | 14040 | direct |
| powers | 14 | DEXTERITY | Dexterity | 2896 | direct |
| powers | 15 | LOSE_DEXTERITY | DexLoss | 22 | direct |
| powers | 16 | THORNS | Thorns | 1646 | direct |
| powers | 17 | PLATED_ARMOR | Plated Armor | 1198 | direct |
| powers | 18 | REGEN | Regeneration | 245 | direct |
| powers | 19 | RITUAL | Ritual | 46754 | direct |
| powers | 20 | CURL_UP | Curl Up | 24082 | direct |
| powers | 21 | FRAIL | Frail | 12500 | direct |
| powers | 22 | SPLIT | Split | 4192 | direct |
| powers | 23 | NEXT_TURN_BLOCK | Next Turn Block | 5 | direct |
| powers | 24 | NO_DRAW | No Draw | 280 | direct |
| powers | 25 | FLAME_BARRIER | Flame Barrier | 5819 | direct |
| powers | 26 | EVOLVE | Evolve | 5438 | direct |
| powers | 27 | FIRE_BREATHING | Fire Breathing | 4254 | direct |
| powers | 28 | BUFFER | Buffer | 86 | direct |
| powers | 29 | INTANGIBLE | IntangiblePlayer | 50 | direct |
| powers | 33 | ANGER | Anger | 32532 | direct |
| powers | 40 | ANGRY | Angry | 550 | direct |
| powers | 45 | MODE_SHIFT | Mode Shift | 1288 | direct |
| powers | 46 | SHARP_HIDE | Sharp Hide | 848 | direct |
| powers | 48 | BARRICADE | Barricade | 4122 | direct |
| powers | 49 | BERSERK | Berserk | 6325 | direct |
| powers | 50 | BRUTALITY | Brutality | 5612 | direct |
| powers | 51 | DEMON_FORM | Demon Form | 4739 | direct |
| powers | 52 | DOUBLE_TAP | Double Tap | 6189 | direct |
| powers | 53 | JUGGERNAUT | Juggernaut | 5229 | direct |
| powers | 59 | CONFUSION | Confusion | 2000 | direct |
| powers | 73 | ENTANGLE | Entangled | 84 | direct |
| powers | 74 | SPORE_CLOUD | Spore Cloud | 5278 | direct |
| powers | 75 | THIEVERY | Thievery | 2792 | direct |
| powers | 77 | NO_BLOCK | NoBlockPower | 142 | direct |
| powers | 78 | SHACKLED | Shackled | 26 | direct |
| powers | 81 | MAYHEM | Mayhem | 1643 | direct |
| powers | 82 | MAGNETISM | Magnetism | 1170 | direct |
| powers | 83 | PANACHE | Panache | 1496 | direct |
| powers | 84 | THE_BOMB | TheBomb | 0 | direct |
| powers | 87 | VIGOR | Vigor | 164 | direct |
| powers | 88 | PEN_NIB | Pen Nib | 189105 | direct |
| powers | 91 | REGENERATE_MONSTER | Regenerate | 490 | direct |
| powers | 92 | DUPLICATION | DuplicationPower | 9 | direct |
| monsters | 1 | JAW_WORM | JawWorm | 63394 | direct |
| monsters | 2 | CULTIST | Cultist | 188894 | direct |
| monsters | 3 | LOUSE_NORMAL | FuzzyLouseNormal | 38690 | direct |
| monsters | 4 | LOUSE_DEFENSIVE | FuzzyLouseDefensive | 42310 | direct |
| monsters | 5 | SPIKE_SLIME_SMALL | SpikeSlime_S | 25250 | direct |
| monsters | 6 | SPIKE_SLIME_MEDIUM | SpikeSlime_M | 29540 | direct |
| monsters | 7 | ACID_SLIME_SMALL | AcidSlime_S | 28692 | direct |
| monsters | 8 | ACID_SLIME_MEDIUM | AcidSlime_M | 24596 | direct |
| monsters | 9 | SPIKE_SLIME_LARGE | SpikeSlime_L | 2328 | direct |
| monsters | 10 | ACID_SLIME_LARGE | AcidSlime_L | 2358 | direct |
| monsters | 11 | SLIME_BOSS | SlimeBoss | 2098 | direct |
| monsters | 12 | GREMLIN_NOB | GremlinNob | 3984 | direct |
| monsters | 13 | SENTRY | Sentry | 19674 | direct |
| monsters | 15 | LAGAVULIN | Lagavulin | 630871 | direct |
| monsters | 16 | GREMLIN_WARRIOR | GremlinWarrior | 576 | direct |
| monsters | 17 | GREMLIN_THIEF | GremlinThief | 466 | direct |
| monsters | 18 | GREMLIN_FAT | GremlinFat | 640 | direct |
| monsters | 19 | GREMLIN_TSUNDERE | GremlinTsundere | 326 | direct |
| monsters | 20 | GREMLIN_WIZARD | GremlinWizard | 200 | direct |
| monsters | 21 | THE_GUARDIAN | TheGuardian | 2338 | direct |
| monsters | 22 | HEXAGHOST | Hexaghost | 254307 | direct |
| monsters | 23 | SLAVER_BLUE | SlaverBlue | 2994 | direct |
| monsters | 24 | SLAVER_RED | SlaverRed | 1244 | direct |
| monsters | 25 | FUNGI_BEAST | FungiBeast | 6552 | direct |
| monsters | 26 | LOOTER | Looter | 306013 | direct |
| relics | 1 | BURNING_BLOOD | Burning Blood | 304750 | direct |
| relics | 2 | ANCHOR | Anchor | 188399 | direct |
| relics | 3 | BAG_OF_MARBLES | Bag of Marbles | 187861 | direct |
| relics | 4 | BAG_OF_PREPARATION | Bag of Preparation | 188605 | direct |
| relics | 5 | BLOOD_VIAL | Blood Vial | 189403 | direct |
| relics | 6 | BRONZE_SCALES | Bronze Scales | 189028 | direct |
| relics | 7 | CENTENNIAL_PUZZLE | Centennial Puzzle | 188533 | direct |
| relics | 8 | LANTERN | Lantern | 189653 | direct |
| relics | 9 | NUNCHAKU | Nunchaku | 188631 | direct |
| relics | 10 | ODDLY_SMOOTH_STONE | Oddly Smooth Stone | 188732 | direct |
| relics | 11 | ORICHALCUM | Orichalcum | 189369 | direct |
| relics | 12 | PEN_NIB | Pen Nib | 189105 | direct |
| relics | 13 | RED_SKULL | Red Skull | 188914 | direct |
| relics | 14 | VAJRA | Vajra | 188771 | direct |
| relics | 15 | HAPPY_FLOWER | Happy Flower | 188446 | direct |
| relics | 16 | AKABEKO | Akabeko | 189122 | direct |
| relics | 17 | ANCIENT_TEA_SET | Ancient Tea Set | 187953 | direct |
| relics | 18 | ART_OF_WAR | Art of War | 188131 | direct |
| relics | 19 | BOOT | Boot | 187717 | direct |
| relics | 20 | CERAMIC_FISH | CeramicFish | 187840 | direct |
| relics | 21 | DREAM_CATCHER | Dream Catcher | 188582 | direct |
| relics | 22 | JUZU_BRACELET | Juzu Bracelet | 188200 | direct |
| relics | 23 | MAW_BANK | MawBank | 187708 | direct |
| relics | 24 | MEAL_TICKET | MealTicket | 187921 | direct |
| relics | 25 | OMAMORI | Omamori | 188435 | direct |
| relics | 26 | POTION_BELT | Potion Belt | 188121 | direct |
| relics | 27 | PRESERVED_INSECT | PreservedInsect | 188310 | direct |
| relics | 28 | REGAL_PILLOW | Regal Pillow | 188507 | direct |
| relics | 29 | SMILING_MASK | Smiling Mask | 188735 | direct |
| relics | 30 | STRAWBERRY | Strawberry | 188871 | direct |
| relics | 31 | TINY_CHEST | Tiny Chest | 188726 | direct |
| relics | 32 | TOY_ORNITHOPTER | Toy Ornithopter | 189096 | direct |
| relics | 33 | WAR_PAINT | War Paint | 189728 | direct |
| relics | 34 | WHETSTONE | Whetstone | 188471 | direct |
| relics | 35 | CIRCLET | Circlet | 0 | direct |
| relics | 36 | BLUE_CANDLE | Blue Candle | 188893 | direct |
| relics | 37 | GREMLIN_HORN | Gremlin Horn | 188751 | direct |
| relics | 38 | HORN_CLEAT | HornCleat | 188083 | direct |
| relics | 39 | INK_BOTTLE | InkBottle | 188560 | direct |
| relics | 40 | KUNAI | Kunai | 188750 | direct |
| relics | 41 | LETTER_OPENER | Letter Opener | 188767 | direct |
| relics | 42 | MERCURY_HOURGLASS | Mercury Hourglass | 189342 | direct |
| relics | 43 | ORNAMENTAL_FAN | Ornamental Fan | 188960 | direct |
| relics | 44 | SHURIKEN | Shuriken | 188915 | direct |
| relics | 45 | SUNDIAL | Sundial | 188347 | direct |
| relics | 46 | SELF_FORMING_CLAY | Self Forming Clay | 188374 | direct |
| relics | 47 | PAPER_PHROG | Paper Frog | 188088 | direct |
| relics | 48 | STRIKE_DUMMY | StrikeDummy | 187807 | direct |
| relics | 49 | MEAT_ON_THE_BONE | Meat on the Bone | 189020 | direct |
| relics | 50 | MUMMIFIED_HAND | Mummified Hand | 188813 | direct |
| relics | 51 | PANTOGRAPH | Pantograph | 188671 | direct |
| relics | 52 | BOTTLED_FLAME | Bottled Flame | 189301 | direct |
| relics | 53 | BOTTLED_LIGHTNING | Bottled Lightning | 188021 | direct |
| relics | 54 | BOTTLED_TORNADO | Bottled Tornado | 188523 | direct |
| relics | 55 | DARKSTONE_PERIAPT | Darkstone Periapt | 188556 | direct |
| relics | 56 | ETERNAL_FEATHER | Eternal Feather | 188671 | direct |
| relics | 57 | FROZEN_EGG | Frozen Egg 2 | 188242 | direct |
| relics | 58 | MOLTEN_EGG | Molten Egg 2 | 187951 | direct |
| relics | 59 | TOXIC_EGG | Toxic Egg 2 | 188345 | direct |
| relics | 60 | PEAR | Pear | 188420 | direct |
| relics | 61 | QUESTION_CARD | Question Card | 188397 | direct |
| relics | 62 | SINGING_BOWL | Singing Bowl | 188984 | direct |
| relics | 63 | THE_COURIER | The Courier | 188621 | direct |
| relics | 64 | WHITE_BEAST_STATUE | White Beast Statue | 188468 | direct |
| relics | 65 | MATRYOSHKA | Matryoshka | 188604 | direct |
| relics | 66 | GINGER | Ginger | 188977 | direct |
| relics | 67 | OLD_COIN | Old Coin | 189094 | direct |
| relics | 68 | BIRD_FACED_URN | Bird Faced Urn | 188281 | direct |
| relics | 69 | UNCEASING_TOP | Unceasing Top | 189185 | direct |
| relics | 70 | TORII | Torii | 188907 | direct |
| relics | 71 | STONE_CALENDAR | StoneCalendar | 188664 | direct |
| relics | 72 | SHOVEL | Shovel | 188650 | direct |
| relics | 73 | WING_BOOTS | WingedGreaves | 188491 | direct |
| relics | 74 | THREAD_AND_NEEDLE | Thread and Needle | 189359 | direct |
| relics | 75 | TURNIP | Turnip | 188993 | direct |
| relics | 76 | ICE_CREAM | Ice Cream | 189336 | direct |
| relics | 77 | CALIPERS | Calipers | 189014 | direct |
| relics | 78 | LIZARD_TAIL | Lizard Tail | 189278 | direct |
| relics | 79 | PRAYER_WHEEL | Prayer Wheel | 189242 | direct |
| relics | 80 | GIRYA | Girya | 189813 | direct |
| relics | 81 | DEAD_BRANCH | Dead Branch | 188776 | direct |
| relics | 82 | DU_VU_DOLL | Du-Vu Doll | 189154 | direct |
| relics | 83 | POCKETWATCH | Pocketwatch | 188883 | direct |
| relics | 84 | MANGO | Mango | 189316 | direct |
| relics | 85 | INCENSE_BURNER | Incense Burner | 189534 | direct |
| relics | 86 | GAMBLING_CHIP | Gambling Chip | 189523 | direct |
| relics | 87 | PEACE_PIPE | Peace Pipe | 189008 | direct |
| relics | 88 | CAPTAINS_WHEEL | CaptainsWheel | 188784 | direct |
| relics | 89 | FOSSILIZED_HELIX | FossilizedHelix | 188511 | direct |
| relics | 90 | TUNGSTEN_ROD | TungstenRod | 188807 | direct |
| relics | 91 | MAGIC_FLOWER | Magic Flower | 188983 | direct |
| relics | 92 | CHARONS_ASHES | Charon's Ashes | 188967 | direct |
| relics | 93 | CHAMPION_BELT | Champion Belt | 189252 | direct |
| relics | 94 | SLING_OF_COURAGE | Sling | 187932 | direct |
| relics | 95 | HAND_DRILL | HandDrill | 185850 | direct |
| relics | 96 | TOOLBOX | Toolbox | 187180 | direct |
| relics | 97 | CHEMICAL_X | Chemical X | 187316 | direct |
| relics | 98 | LEES_WAFFLE | Lee's Waffle | 187643 | direct |
| relics | 99 | ORRERY | Orrery | 186692 | direct |
| relics | 100 | DOLLYS_MIRROR | DollysMirror | 186855 | direct |
| relics | 101 | ORANGE_PELLETS | OrangePellets | 186910 | direct |
| relics | 102 | PRISMATIC_SHARD | PrismaticShard | 187562 | direct |
| relics | 103 | CLOCKWORK_SOUVENIR | ClockworkSouvenir | 186790 | direct |
| relics | 104 | FROZEN_EYE | Frozen Eye | 187092 | direct |
| relics | 105 | THE_ABACUS | TheAbacus | 187536 | direct |
| relics | 106 | MEDICAL_KIT | Medical Kit | 186574 | direct |
| relics | 107 | CAULDRON | Cauldron | 186269 | direct |
| relics | 108 | STRANGE_SPOON | Strange Spoon | 186310 | direct |
| relics | 109 | MEMBERSHIP_CARD | Membership Card | 186679 | direct |
| relics | 110 | BRIMSTONE | Brimstone | 186772 | direct |
| relics | 111 | ODD_MUSHROOM | Odd Mushroom | 732 | direct |
| relics | 112 | FUSION_HAMMER | Fusion Hammer | 190897 | direct |
| relics | 113 | VELVET_CHOKER | Velvet Choker | 191022 | direct |
| relics | 114 | RUNIC_DOME | Runic Dome | 190892 | direct |
| relics | 115 | SLAVERS_COLLAR | SlaversCollar | 189196 | direct |
| relics | 116 | SNECKO_EYE | Snecko Eye | 190696 | direct |
| relics | 117 | PANDORAS_BOX | Pandora's Box | 190623 | direct |
| relics | 118 | CURSED_KEY | Cursed Key | 191123 | direct |
| relics | 119 | BUSTED_CROWN | Busted Crown | 191073 | direct |
| relics | 120 | ECTOPLASM | Ectoplasm | 191140 | direct |
| relics | 121 | TINY_HOUSE | Tiny House | 191553 | direct |
| relics | 122 | SOZU | Sozu | 191114 | direct |
| relics | 123 | PHILOSOPHERS_STONE | Philosopher's Stone | 191009 | direct |
| relics | 124 | ASTROLABE | Astrolabe | 191331 | direct |
| relics | 125 | BLACK_STAR | Black Star | 191033 | direct |
| relics | 126 | SACRED_BARK | SacredBark | 189196 | direct |
| relics | 127 | EMPTY_CAGE | Empty Cage | 191284 | direct |
| relics | 128 | RUNIC_PYRAMID | Runic Pyramid | 191137 | direct |
| relics | 129 | CALLING_BELL | Calling Bell | 190856 | direct |
| relics | 130 | COFFEE_DRIPPER | Coffee Dripper | 190840 | direct |
| relics | 131 | BLACK_BLOOD | Black Blood | 187343 | direct |
| relics | 132 | MARK_OF_PAIN | Mark of Pain | 190633 | direct |
| relics | 133 | RUNIC_CUBE | Runic Cube | 191025 | direct |
| relics | 134 | GOLDEN_IDOL | Golden Idol | 187110 | direct |
| relics | 135 | NEOWS_LAMENT | NeowsBlessing | 12032 | direct |
| relics | 136 | SPIRIT_POOP | Spirit Poop | 104 | direct |
| relics | 137 | WARPED_TONGS | WarpedTongs | 341 | direct |
| relics | 138 | CULTIST_MASK | CultistMask | 0 | direct |
| relics | 139 | FACE_OF_CLERIC | FaceOfCleric | 33 | direct |
| relics | 140 | GREMLIN_MASK | GremlinMask | 117 | direct |
| relics | 141 | NLOTHS_MASK | NlothsMask | 52 | direct |
| relics | 142 | SSSERPENT_HEAD | SsserpentHead | 15 | direct |
| potions | 1 | BLOOD_POTION | BloodPotion | 1512 | direct |
| potions | 2 | ELIXIR | ElixirPotion | 748 | direct |
| potions | 3 | HEART_OF_IRON | HeartOfIron | 495 | direct |
| potions | 4 | BLOCK_POTION | Block Potion | 2694 | direct |
| potions | 5 | DEXTERITY_POTION | Dexterity Potion | 2498 | direct |
| potions | 6 | ENERGY_POTION | Energy Potion | 1842 | direct |
| potions | 7 | EXPLOSIVE_POTION | Explosive Potion | 1970 | direct |
| potions | 8 | FIRE_POTION | Fire Potion | 2354 | direct |
| potions | 9 | STRENGTH_POTION | Strength Potion | 1886 | direct |
| potions | 10 | SWIFT_POTION | Swift Potion | 1998 | direct |
| potions | 11 | WEAK_POTION | Weak Potion | 2076 | direct |
| potions | 12 | FEAR_POTION | FearPotion | 962 | direct |
| potions | 13 | ATTACK_POTION | AttackPotion | 1442 | direct |
| potions | 14 | SKILL_POTION | SkillPotion | 1091 | direct |
| potions | 15 | POWER_POTION | PowerPotion | 949 | direct |
| potions | 16 | COLORLESS_POTION | ColorlessPotion | 1016 | direct |
| potions | 17 | STEROID_POTION | SteroidPotion | 1221 | direct |
| potions | 18 | SPEED_POTION | SpeedPotion | 1010 | direct |
| potions | 19 | BLESSING_OF_THE_FORGE | BlessingOfTheForge | 983 | direct |
| potions | 20 | REGEN_POTION | Regen Potion | 1930 | direct |
| potions | 21 | ANCIENT_POTION | Ancient Potion | 1848 | direct |
| potions | 22 | LIQUID_BRONZE | LiquidBronze | 805 | direct |
| potions | 23 | GAMBLERS_BREW | GamblersBrew | 720 | direct |
| potions | 24 | ESSENCE_OF_STEEL | EssenceOfSteel | 897 | direct |
| potions | 25 | DUPLICATION_POTION | DuplicationPotion | 579 | direct |
| potions | 26 | DISTILLED_CHAOS | DistilledChaos | 911 | direct |
| potions | 27 | LIQUID_MEMORIES | LiquidMemories | 870 | direct |
| potions | 28 | CULTIST_POTION | CultistPotion | 506 | direct |
| potions | 29 | FRUIT_JUICE | Fruit Juice | 680 | direct |
| potions | 30 | SNECKO_OIL | SneckoOil | 517 | direct |
| potions | 31 | FAIRY_POTION | FairyPotion | 3034 | direct |
| potions | 32 | SMOKE_BOMB | SmokeBomb | 699 | direct |
| potions | 33 | ENTROPIC_BREW | EntropicBrew | 344 | direct |
| events | 1 | BIG_FISH | Big Fish | 180161 | direct |
| events | 2 | THE_CLERIC | The Cleric | 180894 | direct |
| events | 3 | DEAD_ADVENTURER | Dead Adventurer | 187949 | direct |
| events | 4 | GOLDEN_IDOL | Golden Idol | 187110 | direct |
| events | 5 | GOLDEN_WING | Golden Wing | 181014 | direct |
| events | 6 | WORLD_OF_GOOP | World of Goop | 182671 | direct |
| events | 7 | LIARS_GAME | Liars Game | 181089 | direct |
| events | 8 | LIVING_WALL | Living Wall | 181198 | direct |
| events | 9 | MUSHROOMS | Mushrooms | 188358 | direct |
| events | 10 | SCRAP_OOZE | Scrap Ooze | 182904 | direct |
| events | 11 | SHINING_LIGHT | Shining Light | 182717 | direct |
| events | 12 | MATCH_AND_KEEP | Match and Keep! | 187907 | direct |
| events | 13 | GOLDEN_SHRINE | Golden Shrine | 187488 | direct |
| events | 14 | TRANSMORGRIFIER | Transmorgrifier | 187335 | direct |
| events | 15 | PURIFIER | Purifier | 187593 | direct |
| events | 16 | UPGRADE_SHRINE | Upgrade Shrine | 186729 | direct |
| events | 17 | WHEEL_OF_CHANGE | Wheel of Change | 187694 | direct |
| events | 18 | ACCURSED_BLACKSMITH | Accursed Blacksmith | 187493 | direct |
| events | 19 | BONFIRE_ELEMENTALS | Bonfire Elementals | 187388 | direct |
| events | 20 | DESIGNER | Designer | 189196 | direct |
| events | 21 | DUPLICATOR | Duplicator | 189196 | direct |
| events | 22 | FACE_TRADER | FaceTrader | 187215 | direct |
| events | 23 | FOUNTAIN_OF_CLEANSING | Fountain of Cleansing | 189056 | direct |
| events | 24 | KNOWING_SKULL | Knowing Skull | 189196 | direct |
| events | 25 | LAB | Lab | 187372 | direct |
| events | 26 | NLOTH | N'loth | 189196 | direct |
| events | 27 | NOTE_FOR_YOURSELF | NoteForYourself | 0 | direct |
| events | 28 | SECRET_PORTAL | SecretPortal | 189196 | direct |
| events | 29 | THE_JOUST | The Joust | 189196 | direct |
| events | 30 | WE_MEET_AGAIN | WeMeetAgain | 187867 | direct |
| events | 31 | THE_WOMAN_IN_BLUE | The Woman in Blue | 187550 | direct |
| encounters | 1 | CULTIST | Cultist | 188894 | direct |
| encounters | 2 | JAW_WORM_ENC | Jaw Worm | 134838 | direct |
| encounters | 3 | TWO_LOUSE | 2 Louse | 91998 | direct |
| encounters | 4 | SMALL_SLIMES | Small Slimes | 96573 | direct |
| encounters | 5 | BLUE_SLAVER | Blue Slaver | 292007 | direct |
| encounters | 6 | GREMLIN_GANG | Gremlin Gang | 163767 | direct |
| encounters | 7 | LOOTER | Looter | 306013 | direct |
| encounters | 8 | LARGE_SLIME | Large Slime | 286995 | direct |
| encounters | 9 | LOTS_OF_SLIMES | Lots of Slimes | 164483 | direct |
| encounters | 10 | EXORDIUM_THUGS | Exordium Thugs | 235388 | direct |
| encounters | 11 | EXORDIUM_WILDLIFE | Exordium Wildlife | 242278 | direct |
| encounters | 12 | RED_SLAVER | Red Slaver | 166139 | direct |
| encounters | 13 | THREE_LOUSE | 3 Louse | 290817 | direct |
| encounters | 14 | TWO_FUNGI_BEASTS | 2 Fungi Beasts | 294183 | direct |
| encounters | 15 | GREMLIN_NOB | Gremlin Nob | 626554 | direct |
| encounters | 16 | LAGAVULIN_ENC | Lagavulin | 630871 | direct |
| encounters | 17 | THREE_SENTRIES | 3 Sentries | 637824 | direct |
| encounters | 18 | THE_GUARDIAN | The Guardian | 253313 | direct |
| encounters | 19 | HEXAGHOST | Hexaghost | 254307 | direct |
| encounters | 20 | SLIME_BOSS | Slime Boss | 251902 | direct |
| encounters | 21 | THE_MUSHROOM_LAIR | The Mushroom Lair | 0 | direct |
| a20 | 1 | A1 | n/a | n/a | sweep |
| a20 | 2 | A2 | n/a | n/a | sweep |
| a20 | 3 | A3 | n/a | n/a | sweep |
| a20 | 4 | A4 | n/a | n/a | sweep |
| a20 | 5 | A5 | n/a | n/a | sweep |
| a20 | 6 | A6 | n/a | n/a | sweep |
| a20 | 7 | A7 | n/a | n/a | sweep |
| a20 | 8 | A8 | n/a | n/a | sweep |
| a20 | 9 | A9 | n/a | n/a | sweep |
| a20 | 10 | A10 | n/a | n/a | sweep |
| a20 | 11 | A11 | n/a | n/a | sweep |
| a20 | 12 | A12 | n/a | n/a | sweep |
| a20 | 13 | A13 | n/a | n/a | sweep |
| a20 | 14 | A14 | n/a | n/a | sweep |
| a20 | 15 | A15 | n/a | n/a | sweep |
| a20 | 16 | A16 | n/a | n/a | sweep |
| a20 | 17 | A17 | n/a | n/a | sweep |
| a20 | 18 | A18 | n/a | n/a | sweep |
| a20 | 19 | A19 | n/a | n/a | sweep |
| a20 | 20 | A20 | n/a | n/a | sweep |
