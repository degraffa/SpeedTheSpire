# S2.43 verification dashboard — S2-G2 items 1–4

Generated deterministically by `tools/verify_report/generate_s2_report.py` — no arguments, the defaults name the cohorts below. Regenerate from the repository root with:

```bat
C:\Python39\python.exe tools\verify_report\generate_s2_report.py
```

Re-running it over unchanged inputs rewrites these files byte for byte. The raw captures stay uncommitted by design (stage-B design section 7.3); what is committed is this report, the per-artifact hash manifest that pins them, and the tool that reopens them. Newest capture consumed: **2026-08-26T08:25:53.031236Z**. The tool README ([../../tools/verify_report/README.md](../../tools/verify_report/README.md)) carries the argument forms and the rules this dashboard applies.

This is **evidence accounting, not a gate inference**. Every bar below is the number the inputs carry today; a bar with no evidence is printed as a literal shortfall, never as a pending success. Items 5–7 of the design section-6 bar belong to S2.46, S2.44 and S2.45 and are outside this report's scope.

Denominator: [../s2-design.md](../s2-design.md) section 6, S2-G2 items 1–4. Ledger row: [../s2-tasks.md](../s2-tasks.md) S2.43.

## Verdicts at a glance

| Bar | Verdict | Headline |
|---|---|---|
| Item 1 — breadth | **UNMET** | 2,000 distinct seeds, 1,998 full-run attempts, 0 untriaged, 0 open |
| Item 2 — Act-2 depth | **UNMET** | 0 of 3 Act-2 BOSS rows carry a zero-diff boss-reward claim |
| Item 3 — Act-3 depth | **UNMET** | 0 of 3 Act-3 BOSS rows witnessed killed, 0 double-boss runs |
| Item 4 — event depth | **UNMET** | 15 sighted, 0 dispositioned, **25 OWED** of 40 Act-2/3 event rows |

## Inputs, pins and determinism

- Artifacts reopened and hashed: **2,023** (4.03 GB). Roll-up over the sorted (campaign, seed, sha256) triples: `2dcb397e7a467d9e…`. Every per-artifact hash is committed beside this file as `s243-artifact-manifest.csv`.
- Retest classification log `s243_resweep5.log` (`5e61590a2fc7b180…`), 94 verdicts.
- Dispositions `s243_dispositions.json` (`936a06fbb3f3a9b3…`): 24 exact run items and 0 event-row items, zero wildcards.
- Sim-side census `scan_s243_prep.txt` (`41812c14f49bccb0…`): 200,000 scanned rows over 20,000 seeds, deepest floor 31.

| Cohort | Role | Workers | Runs | Policy | Policy seed | Fork pin | Driver | Pipeline | Schema |
|---|---|---:|---:|---|---:|---|---|---|---:|
| s243_breadth_rand | breadth | 8 | 500 | random-legal | 1234 | `9BC4BF6A…` | b1.7.0 | b5.4.0 | 1 |
| s243_breadth_take | breadth | 8 | 750 | external | 1234 | `9BC4BF6A…` | b1.7.0 | b5.4.0 | 1 |
| s243_breadth_skip | breadth | 8 | 750 | external | 1234 | `9BC4BF6A…` | b1.7.0 | b5.4.0 | 1 |
| s243_recap_take | recapture | 6 | 8 | external | 1234 | `AD4C44D1…` | b1.7.0 | b5.4.0 | 1 |
| s243_recap_skip | recapture | 4 | 10 | external | 1234 | `AD4C44D1…` | b1.7.0 | b5.4.0 | 1 |
| s243_recap2_take | recapture | 3 | 3 | external | 1234 | `370CBFA8…` | b1.7.0 | b5.4.0 | 1 |
| s243_recap2_skip | recapture | 2 | 2 | external | 1234 | `370CBFA8…` | b1.7.0 | b5.4.0 | 1 |

The fork pins deliberately differ across cohorts: the escape-window recaptures were taken under the two successive holds that closed that class, so collapsing them into one required aggregate pin would hide the very fact the recapture evidence exists to record.

### Scripted-policy pins, as the capture headers carry them

| Cohort | Policy | Policy cmd SHA-256 | Config | Config SHA-256 |
|---|---|---|---|---|
| s243_breadth_rand | random-legal | n/a | n/a | n/a |
| s243_breadth_take | external | `5694B7B3FE8C3DE1…` | policy_bossrelic_take.json | `B4E8C46418341F34…` |
| s243_breadth_skip | external | `5694B7B3FE8C3DE1…` | policy_bossrelic_skip.json | `C516F616C20C2E94…` |
| s243_recap_take | external | `5694B7B3FE8C3DE1…` | policy_bossrelic_take.json | `B4E8C46418341F34…` |
| s243_recap_skip | external | `5694B7B3FE8C3DE1…` | policy_bossrelic_skip.json | `C516F616C20C2E94…` |
| s243_recap2_take | external | `5694B7B3FE8C3DE1…` | policy_bossrelic_take.json | `B4E8C46418341F34…` |
| s243_recap2_skip | external | `5694B7B3FE8C3DE1…` | policy_bossrelic_skip.json | `C516F616C20C2E94…` |

## Item 1 — breadth

> ≥ 2,000 distinct full-run A20 Ironclad oracle attempts under mixed policies (random-legal + survival/scripted external policies), zero untriaged findings, zero open dispositions.

- Distinct breadth seeds, counted from the artifacts themselves: **2,000**; shortfall to 2,000: **0**.
- Full-run attempts — terminal outcome one of `act1_boss_reward`, `death`, `victory`: **1,998**; shortfall to 2,000: **2**. Attempts that reached no gameplay terminal: **2** — listed below, kept in the evidence inventory, never counted toward the 2,000.
- Policies: **external, random-legal**; distinct pinned policy identities: **external:policy_bossrelic_skip.json, external:policy_bossrelic_take.json, random-legal**; mixed-policy requirement met: **YES**.
- Classification as captured: **clean 1,906; state_divergence 94**.
- Classification as read today, the retest sweep applied to 94 runs: **clean 1,981; part 19**.
- Captured actions across the breadth cohorts: **236,218** (a visible diagnostic; there is no action quota in the S2-G2 bar). Replay-recognized capture-race records: **62** across **58** runs.
- Untriaged findings: **0**. A finding is triaged only when an exact (campaign, seed, classification) disposition exists — there is no wildcard and no `other` bucket, and an artifact this tool cannot classify aborts the report.
- Open dispositions: **0**. An `open-*` disposition is reviewed, but it is never acceptance.
- **Item 1: UNMET**

| Campaign | Seed | Terminal outcome |
|---|---|---|
| s243_breadth_skip.worker-008-of-008 | STS432031 | `noop_wedge` |
| s243_breadth_skip.worker-008-of-008 | STS432655 | `noop_wedge` |

### Divergence inventory — every consumed run whose current classification is not clean

| Campaign | Seed | As captured | Today | Disposition | Named exactly | Recapture fork | Recapture races |
|---|---|---|---|---|---|---|---:|
| s243_breadth_rand.worker-007-of-008 | STS430130 | state_divergence | part | standing-deviation | registry/relics.yaml row 107 (Cauldron) -- documented-deferred onEquip |  |  |
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
| s243_recap_skip.worker-003-of-004 | STS432530 | state_divergence | state_divergence | superseded-by-recapture | s243_recap2_skip.worker-001-of-002 / STS432530 (`f5438bc8ff06…`) | `370CBFA8…` | 0 |
| s243_recap_skip.worker-004-of-004 | STS432616 | state_divergence | state_divergence | superseded-by-recapture | s243_recap2_skip.worker-002-of-002 / STS432616 (`26ad51eeccca…`) | `370CBFA8…` | 0 |
| s243_recap_take.worker-002-of-006 | STS431657 | state_divergence | state_divergence | superseded-by-recapture | s243_recap2_take.worker-003-of-003 / STS431657 (`83b8de1da35c…`) | `370CBFA8…` | 0 |
| s243_recap_take.worker-003-of-006 | STS431170 | state_divergence | state_divergence | superseded-by-recapture | s243_recap2_take.worker-001-of-003 / STS431170 (`a4a44e4b4f25…`) | `370CBFA8…` | 0 |
| s243_recap_take.worker-006-of-006 | STS431537 | state_divergence | state_divergence | superseded-by-recapture | s243_recap2_take.worker-002-of-003 / STS431537 (`c77a22b390e6…`) | `370CBFA8…` | 0 |

Every superseding recapture named above was itself reopened, hashed and re-read from this report's own evidence set, and its own current classification is clean — the tool refuses to render a supersession it cannot verify, and refuses one that names a different seed. The last column is that recapture's own count of replay-recognized capture-race records, printed rather than assumed: the class was closed in two rounds, and only the round taken under the endBattle settle-lag hold reaches zero.

## Item 2 — Act-2 depth

> ≥ 1 zero-diff boss-reward claim **and boss-chest boss-relic pick** for every Act-2 registry BOSS row (both a take and at least one skip witnessed across the cohort), each followed by a zero-diff act-2→3 transition into a playable Act-3 floor.

- Clean runs that entered Act 2: **19**; clean runs that fought an Act-2 boss: **0**.

| Act-2 BOSS row | Boss fights | Boss-reward claims | Boss chests | Take cohort | Skip cohort | Act-2→3 transitions |
|---|---:|---:|---:|---:|---:|---:|
| Automaton | 0 | 0 | 0 | 0 | 0 | 0 |
| Champ | 0 | 0 | 0 | 0 | 0 | 0 |
| Collector | 0 | 0 | 0 | 0 | 0 | 0 |

**Literal shortfalls.**

- Act-2 BOSS rows with a zero-diff boss-reward claim: **0 of 3**; missing: **Automaton, Champ, Collector**.
- Act-2 BOSS rows with a zero-diff boss chest: **0 of 3**; missing: **Automaton, Champ, Collector**.
- Boss-relic **take** witnessed for: **no row**.
- Boss-relic **skip** witnessed for: **no row**.
- Zero-diff act-2→3 transition witnessed for: **no row**.
- **Item 2: UNMET**

The depth cohorts this bar needs do not exist yet. S2.43's breadth wave measured zero Act-2 boss fights across its whole cohort — the escalation number that opened S2.V2 — so these rows fill from S2.V2's scan output, and the accounting above then changes only in its numbers.

## Item 3 — Act-3 depth

> every Act-3 registry BOSS row witnessed killed zero-diff, and ≥ 3 completed A20 **double-boss** runs (both bosses in one run, gold settlement zero-diff, covering ≥ 2 distinct first-boss identities).

- Clean runs that entered Act 3: **0**.

| Act-3 BOSS row | Boss fights | Kills witnessed |
|---|---:|---:|
| Awakened One | 0 | 0 |
| Donu and Deca | 0 | 0 |
| Time Eater | 0 | 0 |

**Literal shortfalls.**

- Act-3 BOSS rows witnessed killed zero-diff: **0 of 3**; missing: **Awakened One, Donu and Deca, Time Eater**.
- Completed double-boss runs: **0**; shortfall to 3: **3**.
- Distinct first-boss identities across those runs: **0**; shortfall to 2: **2**.
- **Item 3: UNMET**

**Instrument note, stated rather than hidden.** The campaign report's `boss_kill_acts` is a *set* of act numbers and structurally cannot express two kills in one act, so double-boss detection here is artifact-side: a run counts when its own records witness two distinct Act-3 `act_boss` identities. No Act-3 run exists in the consumed evidence, so that detector is **unexercised by live data**; its behaviour is pinned by synthetic fixtures in the tool's unit tests instead of being asserted here, and the column above is a measured zero rather than a hard-wired false.

## Item 4 — event depth (the section 7.4 coverage join)

> every Act-2/3 event row sighted in ≥ 1 zero-diff oracle run *or* carrying an explicit per-row disposition (directed capture or a recorded reachability argument) — no wildcard dispositions.

- Act-2/3 registry event rows — every row whose `conditions.acts` includes act 2 or act 3: **40**.
- Sighted **in act 2 or 3** in a run whose current classification is clean: **15**.
- Carrying an exact per-row disposition: **0**.
- **OWED — neither sighted nor dispositioned: 25** (Match and Keep!, Golden Shrine, Upgrade Shrine, Accursed Blacksmith, Bonfire Elementals, Designer, Duplicator, FaceTrader, Fountain of Cleansing, Knowing Skull, Lab, N'loth, NoteForYourself, SecretPortal, The Joust, WeMeetAgain, Colosseum, Vampires, Falling, MindBloom, The Moai Head, Mysterious Sphere, SensoryStone, Tomb of Lord Red Mask, Winding Halls).
- **Item 4: UNMET**

A sighting counts only when it happens *in* act 2 or 3: an Act-1 draw of a shrine or of a cross-act special witnesses the Act-1 list, which is not what this bar is about — hence the separate any-act column. The sim-side census column is the rare-event context S2.43's prep scan measured; it is reach evidence for scheduling directed captures, never a substitute for an oracle sighting.

| ID | Row | game_id | Pool | Acts | Act-2/3 sightings | Any-act sightings | Sim census rows | Status |
|---:|---|---|---|---|---:|---:|---:|---|
| 12 | MATCH_AND_KEEP | Match and Keep! | SHRINE | 1,2,3 | 0 | 890 | 4,449 | OWED |
| 13 | GOLDEN_SHRINE | Golden Shrine | SHRINE | 1,2,3 | 0 | 148 | 3,342 | OWED |
| 14 | TRANSMORGRIFIER | Transmorgrifier | SHRINE | 1,2,3 | 2 | 117 | 3,507 | sighted-zero-diff |
| 15 | PURIFIER | Purifier | SHRINE | 1,2,3 | 2 | 158 | 4,436 | sighted-zero-diff |
| 16 | UPGRADE_SHRINE | Upgrade Shrine | SHRINE | 1,2,3 | 0 | 125 | 4,492 | OWED |
| 17 | WHEEL_OF_CHANGE | Wheel of Change | SHRINE | 1,2,3 | 4 | 324 | 3,469 | sighted-zero-diff |
| 18 | ACCURSED_BLACKSMITH | Accursed Blacksmith | SPECIAL | 1,2,3 | 0 | 117 | 3,434 | OWED |
| 19 | BONFIRE_ELEMENTALS | Bonfire Elementals | SPECIAL | 1,2,3 | 0 | 189 | 3,985 | OWED |
| 20 | DESIGNER | Designer | SPECIAL | 2,3 | 0 | 0 | 2 | OWED |
| 21 | DUPLICATOR | Duplicator | SPECIAL | 2,3 | 0 | 0 | 4 | OWED |
| 22 | FACE_TRADER | FaceTrader | SPECIAL | 1,2 | 0 | 193 | 4,008 | OWED |
| 23 | FOUNTAIN_OF_CLEANSING | Fountain of Cleansing | SPECIAL | 1,2,3 | 0 | 11 | 707 | OWED |
| 24 | KNOWING_SKULL | Knowing Skull | SPECIAL | 2 | 0 | 0 | 1 | OWED |
| 25 | LAB | Lab | SPECIAL | 1,2,3 | 0 | 55 | 3,580 | OWED |
| 26 | NLOTH | N'loth | SPECIAL | 2 | 0 | 0 | 4 | OWED |
| 27 | NOTE_FOR_YOURSELF | NoteForYourself | SPECIAL | 1,2,3 | 0 | 0 | 0 | OWED |
| 28 | SECRET_PORTAL | SecretPortal | SPECIAL | 3 | 0 | 0 | 0 | OWED |
| 29 | THE_JOUST | The Joust | SPECIAL | 2 | 0 | 0 | 6 | OWED |
| 30 | WE_MEET_AGAIN | WeMeetAgain | SPECIAL | 1,2,3 | 0 | 142 | 3,249 | OWED |
| 31 | THE_WOMAN_IN_BLUE | The Woman in Blue | SPECIAL | 1,2,3 | 2 | 148 | 3,350 | sighted-zero-diff |
| 32 | ADDICT | Addict | EVENT | 2 | 4 | 4 | 0 | sighted-zero-diff |
| 33 | BACK_TO_BASICS | Back to Basics | EVENT | 2 | 6 | 6 | 0 | sighted-zero-diff |
| 34 | BEGGAR | Beggar | EVENT | 2 | 2 | 2 | 0 | sighted-zero-diff |
| 35 | COLOSSEUM | Colosseum | EVENT | 2 | 0 | 0 | 0 | OWED |
| 36 | CURSED_TOME | Cursed Tome | EVENT | 2 | 4 | 4 | 0 | sighted-zero-diff |
| 37 | DRUG_DEALER | Drug Dealer | EVENT | 2 | 2 | 2 | 0 | sighted-zero-diff |
| 38 | FORGOTTEN_ALTAR | Forgotten Altar | EVENT | 2 | 2 | 2 | 0 | sighted-zero-diff |
| 39 | GHOSTS | Ghosts | EVENT | 2 | 4 | 4 | 0 | sighted-zero-diff |
| 40 | MASKED_BANDITS | Masked Bandits | EVENT | 2 | 4 | 4 | 0 | sighted-zero-diff |
| 41 | NEST | Nest | EVENT | 2 | 3 | 3 | 0 | sighted-zero-diff |
| 42 | THE_LIBRARY | The Library | EVENT | 2 | 2 | 2 | 0 | sighted-zero-diff |
| 43 | THE_MAUSOLEUM | The Mausoleum | EVENT | 2 | 6 | 6 | 0 | sighted-zero-diff |
| 44 | VAMPIRES | Vampires | EVENT | 2 | 0 | 0 | 0 | OWED |
| 45 | FALLING | Falling | EVENT | 3 | 0 | 0 | 0 | OWED |
| 46 | MIND_BLOOM | MindBloom | EVENT | 3 | 0 | 0 | 0 | OWED |
| 47 | THE_MOAI_HEAD | The Moai Head | EVENT | 3 | 0 | 0 | 0 | OWED |
| 48 | MYSTERIOUS_SPHERE | Mysterious Sphere | EVENT | 3 | 0 | 0 | 0 | OWED |
| 49 | SENSORY_STONE | SensoryStone | EVENT | 3 | 0 | 0 | 0 | OWED |
| 50 | TOMB_OF_LORD_RED_MASK | Tomb of Lord Red Mask | EVENT | 3 | 0 | 0 | 0 | OWED |
| 51 | WINDING_HALLS | Winding Halls | EVENT | 3 | 0 | 0 | 0 | OWED |

The full join, including the witnessing capture for every sighted row, is committed beside this file as `s243-event-coverage.csv`.

