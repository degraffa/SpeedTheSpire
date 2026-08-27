# S2.43 verification dashboard — S2-G2 items 1–4

Generated deterministically by `tools/verify_report/generate_s2_report.py` — no arguments, the defaults name the cohorts below. Regenerate from the repository root with:

```bat
C:\Python39\python.exe tools\verify_report\generate_s2_report.py
```

Re-running it over unchanged inputs rewrites these files byte for byte. The raw captures stay uncommitted by design (stage-B design section 7.3); what is committed is this report, the per-artifact hash manifest that pins them, and the tool that reopens them. Newest capture consumed: **2026-08-27T17:49:39.744145Z**. The tool README ([../../tools/verify_report/README.md](../../tools/verify_report/README.md)) carries the argument forms and the rules this dashboard applies.

This is **evidence accounting, not a gate inference**. Every bar below is the number the inputs carry today; a bar with no evidence is printed as a literal shortfall, never as a pending success — the shortfall columns are unchanged from the run that printed four of them, and only their numbers moved. Items 5–7 of the design section-6 bar belong to S2.46, S2.44 and S2.45 and are outside this report's scope.

The cohorts are consumed under five **roles**: `breadth` (item 1), `recapture` (the escape-window recaptures that supersede breadth captures), `depth` (the S2.V2 cohorts carrying items 2 and 3, plus the Mind Bloom directed captures), `iteration` (the divergence-stopped waves that drove the day's fixes — every one replays clean today and each carries an exact campaign-level disposition), and `preflight` (fork-pin preflights, required clean on both readings rather than dispositioned).

Denominator: [../s2-design.md](../s2-design.md) section 6, S2-G2 items 1–4. Ledger row: [../s2-tasks.md](../s2-tasks.md) S2.43.

## Verdicts at a glance

| Bar | Verdict | Headline |
|---|---|---|
| Item 1 — breadth | **MET** | 2,002 distinct seeds, 2,000 full-run attempts, 0 untriaged, 0 open |
| Item 2 — Act-2 depth | **MET** | 3 of 3 Act-2 BOSS rows carry a zero-diff boss-reward claim, 3 a boss-chest pick, 3 an act-2→3 transition |
| Item 3 — Act-3 depth | **MET** | 3 of 3 Act-3 BOSS rows witnessed killed, 3 completed double-boss runs over 2 first-boss identities |
| Item 4 — event depth | **MET** | 31 sighted, 9 dispositioned, **0 OWED** of 40 Act-2/3 event rows |

## Inputs, pins and determinism

- Artifacts reopened and hashed: **2,066** (4.29 GB). Roll-up over the sorted (campaign, seed, sha256) triples: `b7713a687deacf86…`. Every per-artifact hash is committed beside this file as `s243-artifact-manifest.csv`.
- Retest classification log `s243_resweep7.log` (`71a546f42e05d5a1…`), 2066 verdicts.
- Dispositions `s243_dispositions.json` (`a68df1a91c472272…`): 23 exact run items, 9 event-row items and 22 campaign-row items, zero wildcards.
- Retest verdicts naming a campaign outside the consumed cohorts: **0** — at zero, the sweep and the cohort selection cover exactly the same corpus, which is printed rather than assumed.
- Sim-side census `scan_s243_prep.txt` (`41812c14f49bccb0…`): 200,000 scanned rows over 20,000 seeds, deepest floor 31.

| Cohort | Role | Workers | Runs | Policy | Policy seed | Fork pin | Driver | Pipeline | Campaign status | Run rows from |
|---|---|---:|---:|---|---:|---|---|---|---|---|
| s243_breadth_rand | breadth | 8 | 500 | random-legal | 1234 | `9BC4BF6A…` | b1.7.0 | b5.4.0 | complete | campaign report |
| s243_breadth_take | breadth | 8 | 750 | external | 1234 | `9BC4BF6A…` | b1.7.0 | b5.4.0 | complete | campaign report |
| s243_breadth_skip | breadth | 8 | 750 | external | 1234 | `9BC4BF6A…` | b1.7.0 | b5.4.0 | complete | campaign report |
| s243_breadth_top2 | breadth | 1 | 2 | external | 1234 | `370CBFA8…` | b1.7.0 | b5.4.0 | complete | campaign report |
| s243_recap_take | recapture | 6 | 8 | external | 1234 | `AD4C44D1…` | b1.7.0 | b5.4.0 | complete | campaign report |
| s243_recap_skip | recapture | 4 | 10 | external | 1234 | `AD4C44D1…` | b1.7.0 | b5.4.0 | complete | campaign report |
| s243_recap2_take | recapture | 3 | 3 | external | 1234 | `370CBFA8…` | b1.7.0 | b5.4.0 | complete | campaign report |
| s243_recap2_skip | recapture | 2 | 2 | external | 1234 | `370CBFA8…` | b1.7.0 | b5.4.0 | complete | campaign report |
| s2v2_take_e | depth | 1 | 2 | external | 0 | `370CBFA8…` | b1.7.1 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_take_107575 | depth | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.1 | b5.4.0 | complete | campaign report |
| s2v2_take_100075_b | depth | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.1 | b5.4.0 | complete | campaign report |
| s2v2_take_108173_c | depth | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.1 | b5.4.0 | complete | campaign report |
| s2v2_take_100038_b | depth | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.1 | b5.4.0 | complete | campaign report |
| s2v2_take_100009_c | depth | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.1 | b5.4.0 | complete | campaign report |
| s2v2_skip_b | depth | 1 | 3 | external | 0 | `370CBFA8…` | b1.7.1 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_db47_b | depth | 1 | 1 | external | 47 | `370CBFA8…` | b1.7.1 | b5.4.0 | complete | campaign report |
| s2v2_dbv_103509a | depth | 1 | 1 | external | 347 | `ABD95268…` | b1.7.1 | b5.4.0 | complete | campaign report |
| s2v2_dbv_103509b | depth | 1 | 1 | external | 472 | `ABD95268…` | b1.7.1 | b5.4.0 | complete | campaign report |
| s2v2_awk_105835 | depth | 1 | 1 | external | 317 | `370CBFA8…` | b1.7.1 | b5.4.0 | complete | campaign report |
| s2v2_mb_102529 | depth | 1 | 1 | external | 25 | `ABD95268…` | b1.7.1 | b5.4.0 | complete | campaign report |
| s2v2_mb_118993 | depth | 1 | 1 | external | 28 | `ABD95268…` | b1.7.1 | b5.4.0 | complete | campaign report |
| s2v2_mb_103364 | depth | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.1 | b5.4.0 | complete | campaign report |
| s2v2_take | iteration | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.0 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_take_b | iteration | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.0 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_take_c | iteration | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.0 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_take_d | iteration | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.0 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_take_100009 | iteration | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.1 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_take_100075 | iteration | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.1 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_take_108173 | iteration | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.1 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_take_108173_b | iteration | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.1 | b5.4.0 | complete | campaign report |
| s2v2_skip_a | iteration | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.0 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_skip_108173 | iteration | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.1 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_skip_108173_b | iteration | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.1 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_db27_a | iteration | 1 | 1 | external | 27 | `370CBFA8…` | b1.7.0 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_db27_b | iteration | 1 | 1 | external | 27 | `370CBFA8…` | b1.7.1 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_db47_a | iteration | 1 | 1 | external | 47 | `370CBFA8…` | b1.7.0 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_db153_a | iteration | 1 | 1 | external | 153 | `370CBFA8…` | b1.7.0 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_db153_b | iteration | 1 | 1 | external | 153 | `370CBFA8…` | b1.7.1 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_db153_c | iteration | 1 | 1 | external | 153 | `370CBFA8…` | b1.7.1 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_db153_d | iteration | 1 | 1 | external | 153 | `370CBFA8…` | b1.7.1 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_mindbloom_a | iteration | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.0 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_mindbloom_b | iteration | 1 | 1 | external | 0 | `370CBFA8…` | b1.7.1 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_awk_153269 | iteration | 1 | 1 | external | 174 | `370CBFA8…` | b1.7.1 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s2v2_awk_193303 | iteration | 1 | 1 | external | 106 | `370CBFA8…` | b1.7.1 | n/a | fatal_environment_drift | campaign progress (+1 capture stopped mid-seed) |
| s243_preflight | preflight | 1 | 1 | random-legal | 1234 | `9BC4BF6A…` | b1.7.0 | b5.4.0 | complete | campaign report |
| fork_pin_preflight | preflight | 1 | 1 | random-legal | 1234 | `ABD95268…` | b1.7.1 | b5.4.0 | complete | campaign report |

The fork pins deliberately differ across cohorts, and collapsing them into one required aggregate pin would hide the very fact this evidence exists to record: the breadth wave ran under the 2026-08-26 redeploy, the escape-window recaptures under the two successive holds that closed that class, the S2.V2 depth waves under the second of those, and the last depth captures under the 2026-08-27 SecretPortal playtime pin.

A cohort whose campaign status is not `complete` is a wave the driver stopped mid-seed — normal for the depth and iteration waves, since a scripted line that desynchronises ends its campaign. Those workers never reach the postprocess that writes `report.json`, so their run rows come from `campaign_progress.json` and their classification can only come from the retest sweep. The capture the driver died on has no completed run row at all; it is still reopened, hashed and classified, and the count is the parenthesised number above.

### Scripted-policy pins, as the capture headers carry them

| Cohort | Policy | Policy cmd SHA-256 | Config | Config SHA-256 |
|---|---|---|---|---|
| s243_breadth_rand | random-legal | n/a | n/a | n/a |
| s243_breadth_take | external | `5694B7B3FE8C3DE1…` | policy_bossrelic_take.json | `B4E8C46418341F34…` |
| s243_breadth_skip | external | `5694B7B3FE8C3DE1…` | policy_bossrelic_skip.json | `C516F616C20C2E94…` |
| s243_breadth_top2 | external | `5694B7B3FE8C3DE1…` | policy_bossrelic_take.json | `B4E8C46418341F34…` |
| s243_recap_take | external | `5694B7B3FE8C3DE1…` | policy_bossrelic_take.json | `B4E8C46418341F34…` |
| s243_recap_skip | external | `5694B7B3FE8C3DE1…` | policy_bossrelic_skip.json | `C516F616C20C2E94…` |
| s243_recap2_take | external | `5694B7B3FE8C3DE1…` | policy_bossrelic_take.json | `B4E8C46418341F34…` |
| s243_recap2_skip | external | `5694B7B3FE8C3DE1…` | policy_bossrelic_skip.json | `C516F616C20C2E94…` |
| s2v2_take_e | external | `AFE570EDF3F708D1…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_take_107575 | external | `DC8AA06F2F58C214…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_take_100075_b | external | `CA9A78D8B7360354…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_take_108173_c | external | `CA9A78D8B7360354…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_take_100038_b | external | `CA9A78D8B7360354…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_take_100009_c | external | `CA9A78D8B7360354…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_skip_b | external | `AFE570EDF3F708D1…` | follower_sim_search_skip.json | `687B4B9CA8270B81…` |
| s2v2_db47_b | external | `9D094F5FCF5C9EEE…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_dbv_103509a | external | `CA9A78D8B7360354…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_dbv_103509b | external | `CA9A78D8B7360354…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_awk_105835 | external | `CA9A78D8B7360354…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_mb_102529 | external | `CA9A78D8B7360354…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_mb_118993 | external | `CA9A78D8B7360354…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_mb_103364 | external | `CA9A78D8B7360354…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_take | external | `BAB474D647215C3B…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_take_b | external | `4CE19127D035EE72…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_take_c | external | `149A770833C3761A…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_take_d | external | `149A770833C3761A…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_take_100009 | external | `CA9A78D8B7360354…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_take_100075 | external | `DC8AA06F2F58C214…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_take_108173 | external | `DC8AA06F2F58C214…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_take_108173_b | external | `CA9A78D8B7360354…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_skip_a | external | `149A770833C3761A…` | follower_sim_search_skip.json | `687B4B9CA8270B81…` |
| s2v2_skip_108173 | external | `DC8AA06F2F58C214…` | follower_sim_search_skip.json | `687B4B9CA8270B81…` |
| s2v2_skip_108173_b | external | `CA9A78D8B7360354…` | follower_sim_search_skip.json | `687B4B9CA8270B81…` |
| s2v2_db27_a | external | `149A770833C3761A…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_db27_b | external | `AFE570EDF3F708D1…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_db47_a | external | `149A770833C3761A…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_db153_a | external | `1D584B14015AD875…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_db153_b | external | `9D094F5FCF5C9EEE…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_db153_c | external | `DC8AA06F2F58C214…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_db153_d | external | `CA9A78D8B7360354…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_mindbloom_a | external | `AFE570EDF3F708D1…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_mindbloom_b | external | `9D094F5FCF5C9EEE…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_awk_153269 | external | `CA9A78D8B7360354…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s2v2_awk_193303 | external | `CA9A78D8B7360354…` | follower_sim_search.json | `0A90034E05C624CB…` |
| s243_preflight | random-legal | n/a | n/a | n/a |
| fork_pin_preflight | random-legal | n/a | n/a | n/a |

## Item 1 — breadth

> ≥ 2,000 distinct full-run A20 Ironclad oracle attempts under mixed policies (random-legal + survival/scripted external policies), zero untriaged findings, zero open dispositions.

- Distinct breadth seeds, counted from the artifacts themselves: **2,002**; shortfall to 2,000: **0**. The `s243_breadth_top2` cohort is folded in here: it exists because the instrument found the original wave holding 1,998 full-run attempts, not 2,000.
- Full-run attempts — terminal outcome one of `act1_boss_reward`, `death`, `victory`: **2,000**; shortfall to 2,000: **0**. Attempts that reached no gameplay terminal: **2** — listed below, kept in the evidence inventory, never counted toward the 2,000.
- Policies: **external, random-legal**; distinct pinned policy identities: **external:policy_bossrelic_skip.json, external:policy_bossrelic_take.json, random-legal**; mixed-policy requirement met: **YES**.
- Classification as captured: **clean 1,908; state_divergence 94**.
- Classification as read today, the retest sweep moving 94 of these runs: **clean 1,984; part 18**.
- Captured actions across the breadth cohorts: **236,532** (a visible diagnostic; there is no action quota in the S2-G2 bar). Replay-recognized capture-race records: **62** across **58** runs.
- Untriaged findings: **0**. A finding is triaged only when an exact (campaign, seed, classification) disposition exists — there is no wildcard and no `other` bucket, and an artifact this tool cannot classify aborts the report.
- Open dispositions: **0**. An `open-*` disposition is reviewed, but it is never acceptance.
- **Item 1: MET**

| Campaign | Seed | Terminal outcome |
|---|---|---|
| s243_breadth_skip.worker-008-of-008 | STS432031 | `noop_wedge` |
| s243_breadth_skip.worker-008-of-008 | STS432655 | `noop_wedge` |

### Divergence inventory — every consumed run whose current classification is not clean

| Campaign | Seed | As captured | Today | Disposition | Named exactly | Recapture fork | Recapture races |
|---|---|---|---|---|---|---|---:|
| s243_breadth_skip.worker-001-of-008 | STS432280 | state_divergence | part | superseded-by-recapture | s243_recap_skip.worker-004-of-004 / STS432280 (`dd2ff15a0a8b…`) | `AD4C44D1…` | 1 |
| s243_breadth_skip.worker-001-of-008 | STS432456 | state_divergence | part | superseded-by-recapture | s243_recap_skip.worker-002-of-004 / STS432456 (`c7a567b8e27d…`) | `AD4C44D1…` | 1 |
| s243_breadth_skip.worker-001-of-008 | STS432616 | state_divergence | part | superseded-by-recapture | s243_recap2_skip.worker-002-of-002 / STS432616 (`26ad51eeccca…`) | `370CBFA8…` | 0 |
| s243_breadth_skip.worker-002-of-008 | STS432737 | state_divergence | part | superseded-by-recapture | s243_recap_skip.worker-002-of-004 / STS432737 (`f9069dc53f3d…`) | `AD4C44D1…` | 1 |
| s243_breadth_skip.worker-003-of-008 | STS432226 | state_divergence | part | superseded-by-recapture | s243_recap_skip.worker-003-of-004 / STS432226 (`6018e3ccda98…`) | `AD4C44D1…` | 1 |
| s243_breadth_skip.worker-003-of-008 | STS432530 | state_divergence | part | superseded-by-recapture | s243_recap2_skip.worker-001-of-002 / STS432530 (`f5438bc8ff06…`) | `370CBFA8…` | 0 |
| s243_breadth_skip.worker-006-of-008 | STS432165 | state_divergence | part | superseded-by-recapture | s243_recap_skip.worker-002-of-004 / STS432165 (`c97f56927f50…`) | `AD4C44D1…` | 1 |
| s243_breadth_skip.worker-006-of-008 | STS432365 | state_divergence | part | superseded-by-recapture | s243_recap_skip.worker-001-of-004 / STS432365 (`dadae986bc9e…`) | `AD4C44D1…` | 1 |
| s243_breadth_skip.worker-008-of-008 | STS432031 | state_divergence | part | superseded-by-recapture | s243_recap_skip.worker-001-of-004 / STS432031 (`e406241715d9…`) | `AD4C44D1…` | 1 |
| s243_breadth_skip.worker-008-of-008 | STS432655 | state_divergence | part | superseded-by-recapture | s243_recap_skip.worker-001-of-004 / STS432655 (`ae343c40450c…`) | `AD4C44D1…` | 1 |
| s243_breadth_take.worker-002-of-008 | STS431073 | state_divergence | part | superseded-by-recapture | s243_recap_take.worker-001-of-006 / STS431073 (`ab32bc0e524f…`) | `AD4C44D1…` | 1 |
| s243_breadth_take.worker-002-of-008 | STS431105 | state_divergence | part | superseded-by-recapture | s243_recap_take.worker-002-of-006 / STS431105 (`3cd734a64c39…`) | `AD4C44D1…` | 1 |
| s243_breadth_take.worker-002-of-008 | STS431433 | state_divergence | part | superseded-by-recapture | s243_recap_take.worker-005-of-006 / STS431433 (`48e9fd29420d…`) | `AD4C44D1…` | 1 |
| s243_breadth_take.worker-002-of-008 | STS431537 | state_divergence | part | superseded-by-recapture | s243_recap2_take.worker-002-of-003 / STS431537 (`c77a22b390e6…`) | `370CBFA8…` | 0 |
| s243_breadth_take.worker-002-of-008 | STS431657 | state_divergence | part | superseded-by-recapture | s243_recap2_take.worker-003-of-003 / STS431657 (`83b8de1da35c…`) | `370CBFA8…` | 0 |
| s243_breadth_take.worker-003-of-008 | STS431170 | state_divergence | part | superseded-by-recapture | s243_recap2_take.worker-001-of-003 / STS431170 (`a4a44e4b4f25…`) | `370CBFA8…` | 0 |
| s243_breadth_take.worker-003-of-008 | STS431250 | state_divergence | part | superseded-by-recapture | s243_recap_take.worker-004-of-006 / STS431250 (`4db4edde306d…`) | `AD4C44D1…` | 2 |
| s243_breadth_take.worker-007-of-008 | STS431590 | state_divergence | part | superseded-by-recapture | s243_recap_take.worker-001-of-006 / STS431590 (`00c909f4cb14…`) | `AD4C44D1…` | 1 |
| s243_recap_skip.worker-003-of-004 | STS432530 | state_divergence | part | superseded-by-recapture | s243_recap2_skip.worker-001-of-002 / STS432530 (`f5438bc8ff06…`) | `370CBFA8…` | 0 |
| s243_recap_skip.worker-004-of-004 | STS432616 | state_divergence | part | superseded-by-recapture | s243_recap2_skip.worker-002-of-002 / STS432616 (`26ad51eeccca…`) | `370CBFA8…` | 0 |
| s243_recap_take.worker-002-of-006 | STS431657 | state_divergence | part | superseded-by-recapture | s243_recap2_take.worker-003-of-003 / STS431657 (`83b8de1da35c…`) | `370CBFA8…` | 0 |
| s243_recap_take.worker-003-of-006 | STS431170 | state_divergence | part | superseded-by-recapture | s243_recap2_take.worker-001-of-003 / STS431170 (`a4a44e4b4f25…`) | `370CBFA8…` | 0 |
| s243_recap_take.worker-006-of-006 | STS431537 | state_divergence | part | superseded-by-recapture | s243_recap2_take.worker-002-of-003 / STS431537 (`c77a22b390e6…`) | `370CBFA8…` | 0 |

Every superseding recapture named above was itself reopened, hashed and re-read from this report's own evidence set, and its own current classification is clean — the tool refuses to render a supersession it cannot verify, and refuses one that names a different seed. The last column is that recapture's own count of replay-recognized capture-race records, printed rather than assumed: the class was closed in two rounds, and only the round taken under the endBattle settle-lag hold reaches zero.

### Iteration cohorts — the divergence-stopped waves, dispositioned per campaign

Every capture below replays clean on today's engine, so none of them raises a run finding. What was superseded is a whole cohort **seat**, not a (seed, classification) pair, so each carries an exact campaign-level disposition instead: either the clean successor that refilled the same seat — which the tool re-reads from its own evidence set and refuses unless every one of that cohort's captures is clean today — or the landed fix that closed the divergence the wave found.

| Cohort | Status | Named exactly | Reference |
|---|---|---|---|
| s2v2_awk_153269 | resolved | — | docs/s2-tasks.md S2.43 -- the Act-3 event-roll divergence class is SecretPortal's wall-clock gate, fixes e7338a4 + e61b358 |
| s2v2_awk_193303 | resolved | — | docs/s2-tasks.md S2.43 -- a held EGG upgrades the reward OFFER, fix bd2dc55 |
| s2v2_db153_a | resolved | — | docs/s2-tasks.md S2.43 -- the Act-3 event-roll divergence class is SecretPortal's wall-clock gate, fixes e7338a4 + e61b358 |
| s2v2_db153_b | resolved | — | docs/s2-tasks.md S2.43 -- the Act-3 event-roll divergence class is SecretPortal's wall-clock gate, fixes e7338a4 + e61b358 |
| s2v2_db153_c | resolved | — | docs/s2-tasks.md S2.43 -- the Act-3 event-roll divergence class is SecretPortal's wall-clock gate, fixes e7338a4 + e61b358 |
| s2v2_db153_d | resolved | — | docs/s2-tasks.md S2.43 -- the Act-3 event-roll divergence class is SecretPortal's wall-clock gate, fixes e7338a4 + e61b358 |
| s2v2_db27_a | resolved | — | docs/s2-tasks.md S2.43 -- LEAD D, in-combat card COST state, fix 37b543e |
| s2v2_db27_b | resolved | — | docs/s2-tasks.md S2.43 -- LEAD D, in-combat card COST state, fix 37b543e |
| s2v2_db47_a | superseded-by-recapture | s2v2_db47_b | docs/s2-tasks.md S2.43 -- S2.V2 double-boss seat, STS128113 sim_search ps47 |
| s2v2_mindbloom_a | resolved | — | docs/s2-tasks.md S2.43 -- LEAD D, in-combat card COST state, fix 37b543e |
| s2v2_mindbloom_b | resolved | — | docs/s2-tasks.md S2.43 -- LEAD D, in-combat card COST state, fix 37b543e |
| s2v2_skip_108173 | resolved | — | docs/s2-tasks.md S2.43 -- the spawn pre-pass never reached the run layer (`act2-hp-offset`), fix 0300d4b |
| s2v2_skip_108173_b | resolved | — | docs/s2-tasks.md S2.43 -- the spawn pre-pass never reached the run layer (`act2-hp-offset`), fix 0300d4b |
| s2v2_skip_a | superseded-by-recapture | s2v2_skip_b | docs/s2-tasks.md S2.43 -- S2.V2 Act-2 SKIP cohort, STS105134 |
| s2v2_take | superseded-by-recapture | s2v2_take_100009_c | docs/s2-tasks.md S2.43 -- S2.V2 depth wave, STS100009 sim_search ps0, wave 1 of 4 |
| s2v2_take_100009 | superseded-by-recapture | s2v2_take_100009_c | docs/s2-tasks.md S2.43 -- The Library's read pick is a GRID screen |
| s2v2_take_100075 | superseded-by-recapture | s2v2_take_100075_b | docs/s2-tasks.md S2.43 -- the STS100075 Neow-potion divergence is a PHANTOM |
| s2v2_take_108173 | superseded-by-recapture | s2v2_take_108173_c | docs/s2-tasks.md S2.43 -- the spawn pre-pass never reached the run layer (`act2-hp-offset`) |
| s2v2_take_108173_b | superseded-by-recapture | s2v2_take_108173_c | docs/s2-tasks.md S2.43 -- S2.V2 Act-2 Collector take seat, earlier capture |
| s2v2_take_b | superseded-by-recapture | s2v2_take_100009_c | docs/s2-tasks.md S2.43 -- S2.V2 depth wave, STS100009 sim_search ps0, wave 2 of 4 |
| s2v2_take_c | superseded-by-recapture | s2v2_take_100009_c | docs/s2-tasks.md S2.43 -- the first depth ENGINE divergence (double-tapped Rampage) |
| s2v2_take_d | superseded-by-recapture | s2v2_take_e | docs/s2-tasks.md S2.43 -- S2.V2 depth wave, STS100439 sim_search ps0 |

## Item 2 — Act-2 depth

> ≥ 1 zero-diff boss-reward claim **and boss-chest boss-relic pick** for every Act-2 registry BOSS row (both a take and at least one skip witnessed across the cohort), each followed by a zero-diff act-2→3 transition into a playable Act-3 floor.

- Clean runs that entered Act 2: **35**; clean runs that fought an Act-2 boss: **16**.

| Act-2 BOSS row | Boss fights | Boss-reward claims | Boss chests | Boss-relic picks (take) | Skips | Act-2→3 transitions |
|---|---:|---:|---:|---:|---:|---:|
| Automaton | 4 | 4 | 4 | 3 | 1 | 4 |
| Champ | 8 | 8 | 8 | 7 | 1 | 8 |
| Collector | 4 | 4 | 4 | 4 | 0 | 4 |

**Literal shortfalls.**

- Act-2 BOSS rows with a zero-diff boss-reward claim: **3 of 3**; missing: **none**.
- Act-2 BOSS rows with a zero-diff boss chest: **3 of 3**; missing: **none**.
- Act-2 BOSS rows with a zero-diff boss-chest boss-relic **pick**: **3 of 3**; missing: **none**.
- Boss-relic **take** witnessed for: **Automaton, Champ, Collector**.
- Boss-relic **skip** witnessed for: **Automaton, Champ**.
- Zero-diff act-2→3 transition witnessed for: **Automaton, Champ, Collector**.
- Boss-relic policy configs the tool could not attribute to a take/skip cohort: **none**.
- **Item 2: MET**

The take/skip axis is read from the SHA-pinned policy config each capture's own header names, never from a directory name: the breadth/recapture waves ran the S2.42 `policy_bossrelic_take/skip` pair, the S2.V2 depth waves the scripted follower's `follower_sim_search{,_skip}.json`. This bar's per-row evidence is the depth cohorts' — S2.43's breadth wave measured **zero** Act-2 boss fights across all 2,000 attempts, which is the escalation number that opened S2.V2 in the first place.

## Item 3 — Act-3 depth

> every Act-3 registry BOSS row witnessed killed zero-diff, and ≥ 3 completed A20 **double-boss** runs (both bosses in one run, gold settlement zero-diff, covering ≥ 2 distinct first-boss identities).

- Clean runs that entered Act 3: **16**; clean runs that ended in victory: **3**.

| Act-3 BOSS row | Boss fights | Kills witnessed |
|---|---:|---:|
| Awakened One | 2 | 1 |
| Donu and Deca | 5 | 3 |
| Time Eater | 4 | 4 |

**Literal shortfalls.**

- Act-3 BOSS rows witnessed killed zero-diff: **3 of 3**; missing: **none**.
- Completed double-boss runs: **3**; shortfall to 3: **0**. Runs that reached the second boss without completing: **2** — reported, never counted toward the bar.
- Distinct first-boss identities across the completed runs: **2**; shortfall to 2: **0**.
- **Item 3: MET**

| Double-boss run | Act-3 identities, in order | Outcome | Counts toward the bar |
|---|---|---|---|
| s2v2_awk_105835.worker-001-of-001 / STS105835 | Awakened One → Donu and Deca | `death` | no (lost to the second boss) |
| s2v2_db47_b.worker-001-of-001 / STS128113 | Time Eater → Donu and Deca | `victory` | YES |
| s2v2_dbv_103509a.worker-001-of-001 / STS103509 | Donu and Deca → Time Eater | `victory` | YES |
| s2v2_dbv_103509b.worker-001-of-001 / STS103509 | Donu and Deca → Time Eater | `victory` | YES |
| s2v2_mb_118993.worker-001-of-001 / STS118993 | Time Eater → Donu and Deca | `death` | no (lost to the second boss) |

**Instrument note, stated rather than hidden.** Neither half of this bar can be read off the driver's own act sets. The campaign report's `boss_kill_acts` is a *set* of act numbers and structurally cannot express two kills in one act, so double-boss detection is artifact-side: a run counts when its own records witness two distinct Act-3 `act_boss` identities, and it counts toward the bar only when it also ends in victory. The per-row **kill** column is read the same way and for a sharper reason: the captures show the Act-2 boss chest's trailing MAP record already carrying `act: 3`, so `3 in boss_kill_acts` is true of every run that merely crossed into Act 3. A later Act-3 identity can only appear through the A20 double-boss handoff, which is reached only off the first boss's death, so every identity but the last is witnessed killed and the last is witnessed killed exactly on a victory. Gold settlement is not a separate column because it is not a separate assertion: each run above replays clean to its run terminal, and `RunState.gold` is compared at every one of those records.

## Item 4 — event depth (the section 7.4 coverage join)

> every Act-2/3 event row sighted in ≥ 1 zero-diff oracle run *or* carrying an explicit per-row disposition (directed capture or a recorded reachability argument) — no wildcard dispositions.

- Act-2/3 registry event rows — every row whose `conditions.acts` includes act 2 or act 3: **40**.
- Sighted **in act 2 or 3** in a run whose current classification is clean: **31**.
- Carrying an exact per-row disposition: **9**.
- **OWED — neither sighted nor dispositioned: 0** (none).
- **Item 4: MET**

A sighting counts only when it happens *in* act 2 or 3: an Act-1 draw of a shrine or of a cross-act special witnesses the Act-1 list, which is not what this bar is about — hence the separate any-act column. The sim-side census column is the rare-event context S2.43's prep scan measured; it is reach evidence for scheduling directed captures, never a substitute for an oracle sighting. It is also an Act-1-dominated scan (its policies rarely leave Act 1), so a zero there is weak evidence about act 2/3 and the per-row dispositions say so individually rather than leaning on the column.

Design section 6 item 4 sanctions exactly two per-row alternatives to a sighting, and every disposition below is one of them, written per row with its own argument: `directed-capture-scheduled` or `reachability-argument`. There is no wildcard status and no bulk note — a row dispositioned as reachable must say what makes it reachable and what would schedule it.

| ID | Row | game_id | Pool | Acts | Act-2/3 sightings | Any-act sightings | Sim census rows | Status |
|---:|---|---|---|---|---:|---:|---:|---|
| 12 | MATCH_AND_KEEP | Match and Keep! | SHRINE | 1,2,3 | 23 | 926 | 4,449 | sighted-zero-diff |
| 13 | GOLDEN_SHRINE | Golden Shrine | SHRINE | 1,2,3 | 2 | 152 | 3,342 | sighted-zero-diff |
| 14 | TRANSMORGRIFIER | Transmorgrifier | SHRINE | 1,2,3 | 2 | 119 | 3,507 | sighted-zero-diff |
| 15 | PURIFIER | Purifier | SHRINE | 1,2,3 | 2 | 162 | 4,436 | sighted-zero-diff |
| 16 | UPGRADE_SHRINE | Upgrade Shrine | SHRINE | 1,2,3 | 0 | 125 | 4,492 | disposition-on-record |
| 17 | WHEEL_OF_CHANGE | Wheel of Change | SHRINE | 1,2,3 | 4 | 332 | 3,469 | sighted-zero-diff |
| 18 | ACCURSED_BLACKSMITH | Accursed Blacksmith | SPECIAL | 1,2,3 | 10 | 139 | 3,434 | sighted-zero-diff |
| 19 | BONFIRE_ELEMENTALS | Bonfire Elementals | SPECIAL | 1,2,3 | 6 | 210 | 3,985 | sighted-zero-diff |
| 20 | DESIGNER | Designer | SPECIAL | 2,3 | 6 | 6 | 2 | sighted-zero-diff |
| 21 | DUPLICATOR | Duplicator | SPECIAL | 2,3 | 2 | 2 | 4 | sighted-zero-diff |
| 22 | FACE_TRADER | FaceTrader | SPECIAL | 1,2 | 0 | 193 | 4,008 | disposition-on-record |
| 23 | FOUNTAIN_OF_CLEANSING | Fountain of Cleansing | SPECIAL | 1,2,3 | 0 | 11 | 707 | disposition-on-record |
| 24 | KNOWING_SKULL | Knowing Skull | SPECIAL | 2 | 0 | 0 | 1 | disposition-on-record |
| 25 | LAB | Lab | SPECIAL | 1,2,3 | 1 | 56 | 3,580 | sighted-zero-diff |
| 26 | NLOTH | N'loth | SPECIAL | 2 | 0 | 0 | 4 | disposition-on-record |
| 27 | NOTE_FOR_YOURSELF | NoteForYourself | SPECIAL | 1,2,3 | 0 | 0 | 0 | disposition-on-record |
| 28 | SECRET_PORTAL | SecretPortal | SPECIAL | 3 | 0 | 0 | 0 | disposition-on-record |
| 29 | THE_JOUST | The Joust | SPECIAL | 2 | 0 | 0 | 6 | disposition-on-record |
| 30 | WE_MEET_AGAIN | WeMeetAgain | SPECIAL | 1,2,3 | 6 | 160 | 3,249 | sighted-zero-diff |
| 31 | THE_WOMAN_IN_BLUE | The Woman in Blue | SPECIAL | 1,2,3 | 8 | 156 | 3,350 | sighted-zero-diff |
| 32 | ADDICT | Addict | EVENT | 2 | 18 | 18 | 0 | sighted-zero-diff |
| 33 | BACK_TO_BASICS | Back to Basics | EVENT | 2 | 20 | 20 | 0 | sighted-zero-diff |
| 34 | BEGGAR | Beggar | EVENT | 2 | 10 | 10 | 0 | sighted-zero-diff |
| 35 | COLOSSEUM | Colosseum | EVENT | 2 | 0 | 0 | 0 | disposition-on-record |
| 36 | CURSED_TOME | Cursed Tome | EVENT | 2 | 54 | 54 | 0 | sighted-zero-diff |
| 37 | DRUG_DEALER | Drug Dealer | EVENT | 2 | 6 | 6 | 0 | sighted-zero-diff |
| 38 | FORGOTTEN_ALTAR | Forgotten Altar | EVENT | 2 | 10 | 10 | 0 | sighted-zero-diff |
| 39 | GHOSTS | Ghosts | EVENT | 2 | 8 | 8 | 0 | sighted-zero-diff |
| 40 | MASKED_BANDITS | Masked Bandits | EVENT | 2 | 6 | 6 | 0 | sighted-zero-diff |
| 41 | NEST | Nest | EVENT | 2 | 18 | 18 | 0 | sighted-zero-diff |
| 42 | THE_LIBRARY | The Library | EVENT | 2 | 7 | 7 | 0 | sighted-zero-diff |
| 43 | THE_MAUSOLEUM | The Mausoleum | EVENT | 2 | 18 | 18 | 0 | sighted-zero-diff |
| 44 | VAMPIRES | Vampires | EVENT | 2 | 10 | 10 | 0 | sighted-zero-diff |
| 45 | FALLING | Falling | EVENT | 3 | 12 | 12 | 0 | sighted-zero-diff |
| 46 | MIND_BLOOM | MindBloom | EVENT | 3 | 4 | 4 | 0 | sighted-zero-diff |
| 47 | THE_MOAI_HEAD | The Moai Head | EVENT | 3 | 2 | 2 | 0 | sighted-zero-diff |
| 48 | MYSTERIOUS_SPHERE | Mysterious Sphere | EVENT | 3 | 6 | 6 | 0 | sighted-zero-diff |
| 49 | SENSORY_STONE | SensoryStone | EVENT | 3 | 6 | 6 | 0 | sighted-zero-diff |
| 50 | TOMB_OF_LORD_RED_MASK | Tomb of Lord Red Mask | EVENT | 3 | 7 | 7 | 0 | sighted-zero-diff |
| 51 | WINDING_HALLS | Winding Halls | EVENT | 3 | 15 | 15 | 0 | sighted-zero-diff |

The full join, including the witnessing capture for every sighted row, is committed beside this file as `s243-event-coverage.csv`.

