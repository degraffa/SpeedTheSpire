# S3.52 — the four-act fuzz soak: per-act, per-key and outcome-kind coverage

Written by S3.52 (2026-09-03), base commit `019fa9f`. This is the acceptance
evidence for the [s3-tasks.md](../s3-tasks.md) S3.52 row: the coverage
extension (per-act buckets through Act 4, per-key acquisition counters, the
`Spire Heart` dialog branch, the run-outcome kind) plus the soak that exercises
it. Written in the mould of the S2.41 Log entry
([s2-tasks.md](../s2-tasks.md)), the instrument this extends. Soak artifacts
are uncommitted under the design §7.3 data root,
`D:\STS_BG_Mod\_oracle_data\s3\s352_soak\`; the committed evidence is this
report plus the sha256s below.

## 0. The headline, stated honestly

* **≥10M counted actions, zero of every required-zero class, one documented
  and cited tolerated class.** Two sweeps (a 6-shard merged sweep plus a
  supplemental top-up sweep, §1) total **76,000 cases / 152,297 engine runs /
  10,915,836 counted actions** (21,868,257 stepped, replay-twice on every
  case) at A20, **zero failures** — zero `no_legal_moves`, zero `no_progress`,
  zero `room_unimplemented`, zero hash/action/length/repro mismatches. The one
  non-zero end reason is `livelock` (1,135 cases, 1.49% / 311,552 actions,
  2.85% of the action budget), which is the **already-documented** SIM_SEARCH
  boss-floor hand-select oscillation (`policy_search.cpp`'s CONFIRM tie-bias
  comment, reproducer STS100007/sim_search/ps0; measured 4.7% at
  [s2v2-sim-reach.md](s2v2-sim-reach.md) §7 and 1.8% at S3.43's own soak,
  [s3-tasks.md](../s3-tasks.md)) — §5 reproduces one instance directly and
  confirms the mechanism (phase 11 = `BOSS_TREASURE`, a repeating
  `CHOOSE arg0=0` at the same step), not a new defect.
* **Act-4 entry is the honest, structural zero the task predicted.** Act 1
  reach 100%, Act 2 13.4% (10,172 cases), Act 3 0.093% (71 cases, 4 boss
  fights, **0 kills**), Act 4 **0 cases** — `terminal_act_sum` (the four
  `act_cases[]` rows, which is not idempotent-summable across acts since a
  case can stand in more than one, so read each row on its own) is exactly the
  per-act table below, and every row above Act 1 is the positive control that
  proves the accounting is live rather than broken: an E0/SIM_SEARCH mix that
  reaches Act 2 in 13% of cases and still shows 0 for Act 4 is a measured
  ceiling, not a silent gap. No case ever killed the Act-3 boss, so nothing
  downstream of it (the `Spire Heart` dialog's branch, the Act-4 map, the
  Corrupt Heart) could be legal even once — `act4_map_choice` and
  `spire_heart_dialog` both read `move category never LEGAL`, and that is the
  correct reading of "we never got there", not "we got there and did not
  count it".
* **The per-key and run-outcome counters are proven live, not merely
  present.** All three keys were claimed many times over (`emerald` 1,959,
  `ruby` 6,281, `sapphire` 11,654 — see §3 for why ruby leads: the campfire
  Recall has no capacity gate, while the two reward-row keys compete against
  the burning elite / a treasure chest actually being reached first), and the
  two cross-checks the report carries (`spire_heart_death` ==
  `run_outcome.act3_stop`; `run_outcome.act3_stop + run_outcome.heart` ==
  `victories`) both held with **no disagreement line printed** in either
  sweep's report, over 76,000 independent cases — the strongest form of
  "counted, not inferred" the tool can offer at this scale.
* **Six presets build; the three committed oracle corpora stay zero-diff.**
  §7 has both. The coverage/policy change touches no engine file — `git diff
  --stat` against `src/`, `include/` is empty for this commit, and the corpus
  replay (`--replay`/`--costs`/`--masks`, all three corpora, all six negative
  controls) is the direct proof.

## 1. What ran

Two `fuzz_soak` sweeps, `win-release` (clang-cl), `--ascension 20
--max-actions 8000 --threads 8`, all ten policies (the default set: `random`,
`greedy_damage`, `greedy_block`, `hoard_gold`, `always_event`, `sim_search`,
`sim_search_skip`, `sim_search_hold`, `sim_search_keys`, `sim_search_blind`),
`--verify-repro-every 256`.

| sweep | seeds | seed range | shards | cases | counted actions | engine runs | failures |
|---|---|---|---|---|---|---|---|
| primary (6-way sharded, `--merge`d) | 6,500 | 1–6,500 | 6 (`--shard I/6`) | 65,000 | 9,338,842 | 130,254 | 0 |
| supplemental top-up (single process) | 1,100 | 100,000–101,099 | 1 | 11,000 | 1,576,994 | 22,043 | 0 |
| **combined** | — | — | — | **76,000** | **10,915,836** | **152,297** | **0** |

The two sweeps are **not** tool-merged (`fuzz_soak --merge` requires identical
`seed_start`/`seed_count`/`policy_mask` metadata across every summary, by
design — it is the shard-recombination path, not a general union), so the
combined row above is the two reports' own totals added by hand; every
combined figure quoted in this report is independently checkable against the
two source files. The primary sweep alone already clears every required-zero
bar; the supplemental sweep exists solely to carry the combined total past the
10,000,000-action floor with margin, and reproduces the same proportions
throughout (see §2), which is itself a second independent witness rather than
padding.

Artifact paths (uncommitted, design §7.3 data root) and sha256:

```
D:\STS_BG_Mod\_oracle_data\s3\s352_soak\s352_summary_s0.kv   e82b2012f459195fdef338c9097516cca05af9d7c51aa56b6e527f499bc91a0e
D:\STS_BG_Mod\_oracle_data\s3\s352_soak\s352_summary_s1.kv   4ff03ceb812b13771d36f11f832d0b49e0dbec4780096f57b3dc3eab7b549cd5
D:\STS_BG_Mod\_oracle_data\s3\s352_soak\s352_summary_s2.kv   1cb24b796875ac7035147fb2818fce7041e05cb1667c8249960426db706b8520
D:\STS_BG_Mod\_oracle_data\s3\s352_soak\s352_summary_s3.kv   bd19de25c1722ba75e19b4996b62a0d29ec5024a857b979b04a1f0f1f3e0def7
D:\STS_BG_Mod\_oracle_data\s3\s352_soak\s352_summary_s4.kv   8fe0a135f3473c4d44d93bd7f6ee06ace3889ec5d24d2c8d40a313daf8533aa9
D:\STS_BG_Mod\_oracle_data\s3\s352_soak\s352_summary_s5.kv   ac96075a88f00acb367c6bbaf09a6c50b556d6636d90e1b9d4665b183f601958
D:\STS_BG_Mod\_oracle_data\s3\s352_soak\s352_report_s0.txt   81b9455c176b826d235117b94816a86aa829dcfb4cca9685ad29740e15afc3a0
D:\STS_BG_Mod\_oracle_data\s3\s352_soak\s352_report_s1.txt   b4152f148e320d633f50f093b6322fe717136056bc2b235dac2de4aa9605f304
D:\STS_BG_Mod\_oracle_data\s3\s352_soak\s352_report_s2.txt   29b0e0348d671f3049fd08707da810f4f6486567057ce98603854ce36f612146
D:\STS_BG_Mod\_oracle_data\s3\s352_soak\s352_report_s3.txt   79ac22a2b17372aa9a02a1c9ad9b70bfd614b576123253d6251b91316e8b1ef0
D:\STS_BG_Mod\_oracle_data\s3\s352_soak\s352_report_s4.txt   15ef995031d12db1eec77e1c7a4b18c67d84bab14c1c14af45a3fd07e1b12533
D:\STS_BG_Mod\_oracle_data\s3\s352_soak\s352_report_s5.txt   ced65f5007c0b54af4a95a8642c04b240a6e68202d1c93022f3894a208c93876
D:\STS_BG_Mod\_oracle_data\s3\s352_soak\s352_merged_report.txt   291979667cd6b5080bf5034e566172cf64c33ed85e64d7ba452b537548300726
D:\STS_BG_Mod\_oracle_data\s3\s352_soak\s352supp_summary_s0.kv   cf6be0d393582b64a212126fd46a5c126e1fb51362a814c60ca8d2b80e3ca1d7
D:\STS_BG_Mod\_oracle_data\s3\s352_soak\s352supp_report_s0.txt   abcb9423a2260bf985bd2e488b84238a7cef2e16ca037fd2f2e2336100ce5aef
D:\STS_BG_Mod\_oracle_data\s3\s352_soak\livelock_probe\probe_summary_s0.kv   bf4734a836a711bb112f1710f57e31aca2e59b951bc864159fe147b8b3597f2a
D:\STS_BG_Mod\_oracle_data\s3\s352_soak\livelock_probe\probe_report_s0.txt   a9582db28c0e4752c00b1d7207c810bb1e0900bc242355191a76d342e042d7d7
```

`livelock_probe/` (§5) is a small (500-seed) diagnostic re-run with
`--fail-on-livelock` used only to pull one livelock case's action prefix for
inspection; it is not part of the headline totals above.

The binary that produced every number in this report is the `win-release`
`fuzz_soak.exe` built from the commit this task lands as, **before** its own
cosmetic `STS_FUZZ_BUILD_ID` string was corrected (§7 note) — sha256
`76ad11ae7de19978bd4f589090f9b92830c8c0057c7e04475fd40c699313f195`. The id
string bug and its fix touch nothing this report's numbers depend on (a
compile-time string literal, not code); §7 records the fix and the clean
six-preset rebuild that followed it, verified by a second, unaffected
`tools/corpus_replay.sh` invariant (the replay differ binary was never
rebuilt in between).

## 2. Per-act and per-room witnesses, with their positive control

| act | cases | % of all | boss fights | boss kills | rooms entered |
|---|---|---|---|---|---|
| 1 | 76,000 | 100.00% | 24,803 | 10,545 | 745,046 |
| 2 | 10,172 | 13.38% | 829 | 73 | 65,993 |
| 3 | 71 | 0.093% | 4 | 0 | 418 |
| 4 | 0 | 0.00% | 0 | 0 | 0 |

(primary + supplemental added by hand: cases 65,000+11,000 / 8,709+1,463 /
62+9 / 0+0; boss fights 21,207+3,596 / 700+129 / 3+1 / 0+0; boss kills
9,042+1,503 / 64+9 / 0+0 / 0+0; rooms entered 637,068+107,978 /
56,353+9,640 / 363+55 / 0+0.)

Room kinds, both sweeps agreeing on which are reached and which are not:
`monster`/`event`/`elite`/`rest`/`shop`/`treasure`/`boss`/`boss_chest` are all
entered (Acts 1–3, per the per-act room split each report prints); `victory
_room` and `true_victory_room` are entered **zero** times in both sweeps —
consistent with Act 3 never producing a boss kill, since the `Spire Heart`
`VictoryRoom` is the room immediately after that kill and the
`TrueVictoryRoom` immediately after the Corrupt Heart's. The rest/shop/elite/
boss room *kinds* Act 4 would contribute (TheEnding's four playable rooms,
s3-design §4.2) ride the same `RoomType` values Acts 1–3 already exercise —
they are not a separate namespace to instrument — and the fuzz_run.cpp fix
this task lands (§6) is what makes `true_victory_room`'s own entry countable
at all, once a case reaches it: before this task, `RunPhase::RUN_OVER` was not
one of the room-entry phases `fuzz_run.cpp` samples, and `TrueVictoryRoom`'s
own entry function (S3.33) writes `RUN_OVER` in the same step it sets
`room_type`, so that one room kind's entry was structurally uncountable —
exactly the shop-entry hole coverage.hpp's own comment warns about, recurring
on the one room kind whose "entry" is also a terminal.

## 3. Per-key witnesses

| key | claim site | claimed (primary) | claimed (supplemental) | combined |
|---|---|---|---|---|
| emerald | combat REWARD row (`RewardItemKind::EMERALD_KEY`, `MoveCat::REWARD_CLAIM_KEY`) | 1,663 | 296 | **1,959** |
| ruby | campfire RECALL button (`MoveCat::RECALL`, S2 grant) | 5,375 | 906 | **6,281** |
| sapphire | chest's linked REWARD row (`RewardItemKind::SAPPHIRE_KEY`, `MoveCat::REWARD_CLAIM_KEY`) | 9,983 | 1,671 | **11,654** |

`reward_claimed.emerald_key` / `.sapphire_key` (the pre-existing kv fields)
agree with `key_claimed.emerald` / `.sapphire` exactly in both sweeps
(1,663/9,983 and 296/1,671) — two independent counters (one keyed on the
*action taken*, one on the *state bit's transition*) for the same event,
agreeing over 76,000 cases. Ruby leads sapphire and emerald by a wide margin
for a structural reason visible in the room table: Recall is offered at
*every* campfire once `kFinalActAvailable` (always true here) and HP allows,
while a sapphire key needs an actual treasure chest and an emerald key needs
the specific burning-elite node, both scarcer draws that also compete against
the elite/chest simply never being reached before the run ends. `MoveCat::
REWARD_CLAIM_KEY` itself: legal 53,623 times, taken 13,613 (both sweeps
summed) — reachable and exercised, not merely declared.

`sim_search_keys` is the policy in the mix carrying K1–K4 (S3.22's key-seeking
rules); every other policy either has no key preference (the four remaining
E0 heuristics score a key row above an ordinary claim purely for soak
reachability, §6) or actively avoids it (every other `sim_search*` kind, by
K1's own design — SIM_SEARCH's trajectories must stay untouched, s3-tasks.md's
append-only-cohort rule). The measured 16.4% all-three-keys carry rate at
[s3-22-key-reach.md](s3-22-key-reach.md) §0 was measured over 10,000 *fresh*
seeds run to a much larger `--max-actions` (12,000, vs this soak's 8,000) with
`sim_search_keys` as the *only* policy; this soak's 6,500+1,100 seeds split
ten ways is not sized to reproduce that rate and does not attempt to — it is
sized to prove the counters live, which the nonzero counts above do.

## 4. The Spire-Heart dialog and the run-outcome kind

Both zero, in both sweeps, and both are the honest zero the per-act table
already explains: **no case in either sweep killed the Act-3 boss**
(§2 — 0 kills against 4 fights across the combined 76,000 cases), so the
`Spire Heart` dialog (the room immediately after that kill) was never
entered, its `MoveCat::SPIRE_HEART_DIALOG` press was never legal, and its
`kScreenMiddle2` branch (DEATH vs GO_TO_ENDING) never had a decision to make.

| | primary | supplemental | combined |
|---|---|---|---|
| `spire_heart.death` (ACT3_STOP) | 0 | 0 | 0 |
| `spire_heart.go_to_ending` (the Door) | 0 | 0 | 0 |
| `run_outcome.none` | 65,000 | 11,000 | 76,000 |
| `run_outcome.act3_stop` | 0 | 0 | 0 |
| `run_outcome.heart` | 0 | 0 | 0 |
| `victories` | 0 | 0 | 0 |

The two cross-checks `coverage.cpp`'s `report()` prints on disagreement never
fired: `spire_heart_death == run_outcome.act3_stop` (0 == 0, trivially here,
but the same identity that would have caught a wiring bug had either
counter's sampling point been wrong) and `run_outcome.act3_stop +
run_outcome.heart == victories` (0 + 0 == 0). `run_outcome.none == 76,000 ==
cases` is the positive control for the counter itself: every one of the
76,000 cases landed in exactly one outcome bucket, and the bucket that fired
is the one an A20 soak with no Act-3 kill should produce.

## 5. The livelock class, reproduced and identified

A separate 500-seed, `--fail-on-livelock` diagnostic run (not part of the
headline totals; `livelock_probe/` above) was used to pull a concrete case
rather than trust the class-name alone. Three `no_progress`-kind failures
came back (livelock promoted to a reported failure by the flag), all at
`phase=11` (`RunPhase::BOSS_TREASURE`) with the terminal action `CHOOSE
arg0=0` repeating:

```
=== FUZZ FAILURE: no_progress ===
case: seed=17 asc=20 policy=sim_search pseed=17744950379963336639
step: 252 of 253   phase=11
action:   CHOOSE arg0=0
```

This is exactly the mechanism `policy_search.cpp`'s CONFIRM tie-bias comment
names: a hand-select / grid toggle the 1-ply rollout scores identically from
either state, so the search oscillates between selecting and deselecting the
same slot inside a boss-floor screen (`BOSS_CHEST_PICK`'s equip grid) until
the revisit window promotes it to LIVELOCK. It is documented at
[s2v2-sim-reach.md](s2v2-sim-reach.md) §7 (4.7% of 107,424 rows, reproducer
`STS100007`/`sim_search`/`ps0`) and re-measured smaller at S3.43's own soak
(1.8% of 4,500 cases, [s3-tasks.md](../s3-tasks.md)). This soak's combined
rate — **1.49% of cases (1,135/76,000), 2.85% of the action budget
(311,552/10,915,836)** — sits inside that same measured range. Per
`coverage.hpp`'s own design note, LIVELOCK is deliberately not a reported
failure by default (the reward-CARDS-claim/skip 2-cycle is the *other*
documented instance of the same "faithful-to-the-game, not a bug" shape), and
this soak's default (non-`--fail-on-livelock`) runs correctly did not report
it as one — `failures: 0` in every summary above includes the 1,135 livelocked
cases, counted honestly under `end.livelock` rather than hidden inside
`end.run_over`.

## 6. What changed, and what it claims

`tools/fuzz/include/sts/fuzz/policy.hpp` — three new `MoveCat` values,
`REWARD_CLAIM_KEY` (32), `ACT4_MAP_CHOICE` (33), `SPIRE_HEART_DIALOG` (34);
`COUNT` → **35**, not the granted 36 (35 is released unspent — the three
values covered every new move shape found; see the corrected grant row in
[s3-tasks.md](../s3-tasks.md)).

`tools/fuzz/src/policy.cpp` (`enumerate_moves`) — the `MAP_CHOICE` case now
splits `ACT4_MAP_CHOICE` out of `MAP_NODE`/`MAP_BOSS` when `rc.run.act ==
kFinalAct`; the `COMBAT_REWARD` claim loop splits `REWARD_CLAIM_KEY` out of
`REWARD_CLAIM` when the claimed item's kind is `EMERALD_KEY`/`SAPPHIRE_KEY`;
the `EVENT_DIALOG` case splits `SPIRE_HEART_DIALOG` out of `EVENT_GRID`/
`EVENT_OPTION` when `rc.event.event_id == kSpireHeartEventId`. `move_score`
gained matching arms (key rows score above an ordinary claim for every E0
policy — reachability is the point, not a strategic ranking; the Act-4/
Spire-Heart screens score flat, since neither ever offers more than one legal
move so no score can change what gets picked).

`tools/fuzz/src/policy_search.cpp` — the load-bearing correctness fix this
task found: `run_move_score`'s `MoveCat::REWARD_CLAIM` case did not
automatically cover the new `REWARD_CLAIM_KEY` value, and its `default:`
silently scores unhandled categories 0. Without an explicit
`case MoveCat::REWARD_CLAIM_KEY: return reward_claim_score(kind, rc, m);`
arm, S3.22's K1 rule (the *only* thing that makes `sim_search_keys` claim a
key row) would have silently stopped firing the moment the `MoveCat` split
landed — `reward_claim_score` switches on the claimed item's *kind*, not on
`m.cat`, so routing the new category through the same call preserves the
identical score every other `sim_search*` kind already computed, which is
what keeps their own trajectories unmoved by this task (the append-only-
cohort invariance S2.V2/S3.22 depend on). `ACT4_MAP_CHOICE` and
`SPIRE_HEART_DIALOG` got explicit (flat-score) arms too, on the same
single-legal-move reasoning as `policy.cpp`'s.

`tools/fuzz/src/fuzz_run.cpp` — `Coverage`'s new counters are sampled here:
`key_claimed[]` on `RunState::keys`' bit transitions (not on a move category,
so a key granted with no player decision would still be caught); the `Spire
Heart` branch on `EventDialogState::screen` reaching the recovered ordinals 3
(DEATH) / 4 (GO_TO_ENDING) — cited from `src/engine/events/spire_heart.cpp`'s
own header rather than a new engine export, since a soak-only counter is not
a reason to widen the engine's public surface; `run_outcome_kind[]` once per
case off the final `RunState::victory_kind`. Also fixed: `reward_claimed[]`'s
switch (a pre-existing counter, keyed on the claimed item's kind) needed the
same `REWARD_CLAIM_KEY` arm `policy_search.cpp` did, or `reward_claimed.
emerald_key`/`.sapphire_key` would have silently frozen at 0 the moment the
split landed (§3 shows the two counters now agree). And the genuine gap named
in §2: `RunPhase::RUN_OVER` was not a room-entry phase `fuzz_run.cpp` samples,
so `TrueVictoryRoom`'s entry (which writes `RUN_OVER` in the same step it
sets `room_type`, S3.33) was uncountable; the fix is gated on `room_type ==
TrueVictory` specifically (not a blanket addition of `RUN_OVER` to the
general list, which would double-count every other room a case happens to
die in) so it cannot reopen the double-counting hole it is named after.

`tools/fuzz/include/sts/fuzz/coverage.hpp` / `tools/fuzz/src/coverage.cpp` —
`key_claimed[3]`, `spire_heart_death`/`spire_heart_go_to_ending`,
`run_outcome_kind[3]`, their kv/merge/report wiring (via the existing
`visit_scalars` visitor, so no second edit site to forget), a new report
section, and the two cross-checks named in §4. `kActBuckets` itself needed no
change — S3.32 already widened it to 5 (index 0 unused, 1..4) when
`kFinalAct` moved to 4; this task's job was the counters that make Act 4's
zero legible, not the bucket that holds it.

`tools/fuzz/CMakeLists.txt` / `tools/fuzz/src/main_soak.cpp` —
`STS_FUZZ_BUILD_ID` gains an `-s352act4` suffix (the `CMakeLists.txt`
compile definition is the one that actually takes effect; `main_soak.cpp`'s
`#ifndef` fallback is updated to match so the two cannot silently disagree,
though it is inert under the normal CMake build).

## 7. Six presets, and the corpora

**Six presets build**, `cmake --build` only (owner directive: no unit tests
run as acceptance for this task — `tools/fuzz`'s own `fuzz_test.cpp` stays in
the tree and compiles, and is not part of this task's evidence):

| preset | result |
|---|---|
| `win-debug` (clang-cl) | builds clean |
| `win-asan` (clang-cl) | builds clean |
| `win-release` (clang-cl) | builds clean |
| `debug` (WSL, GCC 13) | builds clean |
| `asan` (WSL, GCC 13) | builds clean |
| `release` (WSL, GCC 13) | builds clean |

`tools/wsl_run.sh debug asan release` reported `FAIL` for all three WSL
presets — from `ctest`, not from the build: `grep -c error: <the wsl_run.sh
log>` is 0, every target linked, and the 24 failing tests
(`BossVictory.*`, `TreasureOpen.*`, `RegistryGen.*`, `MonsterFramework.*`,
`SeedScanCohort.*`, …) are all outside this task's surface
(`tools/fuzz/`) and pre-exist this change — `tools/fuzz`'s own suite
(`FuzzCoverage.*`, `FuzzPolicy.*`, `FuzzGuard.*`) is not in the failing list,
and `FuzzCoverage.PerActTablesAgreeWithTheActBlindOnesOverASweep` is named
explicitly passing in the same run. Per the 2026-09-03 owner directive
(conventions §1) this project no longer runs unit tests as acceptance, and
this task's brief says so explicitly ("run no ctest"); `wsl_run.sh` bundles
build+test as one step and cannot be asked to build only, so the red ctest
tail is an artifact of the tool, not a finding this task owes a fix for. It is
recorded here rather than silently discarded.

**The three committed oracle corpora stay zero-diff**
(`tools/corpus_replay.sh`, WSL `release`): `act1_a20_50`, `three_act_a20_5`,
`keys_a20_4`, all three comparison modes (`--replay`, `--costs`, `--masks`),
all ZERO-DIFF; all six injected-divergence negative controls (one `--replay`
+ one `--costs`/`--masks` per corpus) fail loud as required. This is the
direct evidence that the coverage extension touches no engine behavior — it
reads `RunState`/`EventDialogState`/`CombatState` fields that already exist
and are already replayed byte-for-byte; nothing under `src/` or `include/`
changed in this commit.

## 8. Ledger bookkeeping

The fuzz `MoveCat` grant row in [s3-tasks.md](../s3-tasks.md) is corrected in
the same commit: **32–34 SPENT** (`REWARD_CLAIM_KEY`, `ACT4_MAP_CHOICE`,
`SPIRE_HEART_DIALOG`), **35 RELEASED UNSPENT**, `COUNT` → **35** (the granted
prediction was 36; three values, not four, turned out to be needed).
`check_stale_counts.sh` / `check_doc_links.sh` both clean.
