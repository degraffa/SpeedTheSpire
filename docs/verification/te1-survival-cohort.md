# TE.1 survival-biased campaign cohort — evidence summary

Written by the TE.1 task (2026-08-03). This is evidence accounting for the
TE.1 acceptance block in [../training-tasks.md](../training-tasks.md); the
campaign artifacts themselves stay uncommitted under the design §7.3 data
root, exactly as every prior campaign's do.

## What ran

- Campaign group: `D:\STS_BG_Mod\_oracle_data\campaigns\te1_survival_b160_20260803_420000_420499`
  (8 isolated instances via `campaign_pipeline.py run --instances auto`).
- Seeds: **STS420000–STS420499** — 500 fresh *sequential* seeds
  (`generate-seeds`), deliberately **not** pre-scanned: the coverage below is
  the policy's, not a seed filter's.
- Action source: **`--policy external`** — the promoted survival-biased
  policy (`tools/oracle_bridge/driver/survival_policy_cmd.py`, empty config)
  running as a separate policy binary behind the new STS-POLICY-IO v1 hook,
  `--policy-seed 1234`. The binary's SHA-256 is pinned in every
  `campaign_progress.json`/artifact header
  (`BDF49784DAED698C15E7229CC0BFFC960370CF0D47AD5689F4B9F2E5E1A13BD6`).
- Driver `b1.6.0`, pipeline `b5.3.0`.

## Acceptance metrics (from `parallel_report.json`, sha256 `24823dcf8a89…`)

- Runs: **500 / 500 completed, 0 failed** (69,917 captured actions,
  2,227 s capture wall clock, 31.4 actions/s aggregate).
- **Boss-fight reach: 155 / 500 = 31.0 %** (bar: ≥ 30 %). Per-run
  `boss_fight_reached` is recorded by the driver from the decision states
  (`room_type == MonsterRoomBoss`), not inferred from floors.
- **Boss-reward claims: 35** (bar: ≥ 10) — Slime Boss 13, Hexaghost 11,
  The Guardian 11: every Act-1 registry boss claimed.
- Outcomes: 35 `act1_boss_reward`, 464 `death`, 1 `noop_wedge`.
- The random-policy baseline this replaces measurably cannot produce this:
  the reviewed 5,000-run random prefix had 97.58 % of deaths on floors 1–7
  and zero boss rewards ([handoff](../handoff-2026-07-30.md)).

## Replay triage (Stage B process: exact per-finding dispositions)

- Diff classifications: **387 clean / 113 state_divergence**; replay-clean
  actions 49,422; strict zero-diff actions 48,049; 16 replay-recognized
  capture races (all `escape-race`).
- All **113** divergent runs carry an exact campaign/seed/classification
  disposition in
  `…\te1_survival_b160_20260803_420000_420499\triage\divergence_dispositions.json`
  (sha256 `1f10ada47f08…`): **zero untriaged findings.**
  - **110** shape-checked by the new conservative classifier
    (`tools/oracle_bridge/driver/standing_triage.py`): 97 Looter
    stolen-gold-only (Looter presence verified per divergent seq against the
    run artifact), 8 Fairy-in-a-Bottle belt-slot timing, 5 both — the two
    already-decided standing families, no other field differing.
  - **2** manually reviewed Smoke-Bomb escape-settlement races (STS420084,
    STS420366): the replay recognizes the race, and a command injected
    during the escape window makes the downstream settlement incomparable —
    the recorded capture-artifact precedent.
  - **1 open finding** (STS420252, `open-product-divergence`): **Sharp Hide
    retaliation on the killing blow.** The game's THORNS `DamageAction` is
    not cancelled when its source dies, so the killing attack against a
    Defensive-Mode Guardian still costs the player 4 HP; the sim's pump
    stops at combat-over with that item still queued (hp 22 vs 26 to the
    terminal). Reproducer promoted at
    `…worker-005-of-008\triage\pending\STS420252.reproducer.json`; needs an
    engine fix in combat-over pump termination for queued THORNS damage
    (kin to the G7 proactive-audit THORNS family). Disposition recorded;
    fix deliberately left to its own focused task.

## Cross-checks

- Sim-side pre-scan evidence (kept beside the artifacts:
  `te1_boss_scan_420000_479999.{tsv,summary.txt}`): the B5.1 E0 fuzz
  heuristics reach the boss in only **0.80 %** of 240,000 scanned
  (seed × policy × policy-seed) runs over the same seed range — the
  promoted driver-side policy's 31 % is the deliverable's delta, not a
  seed-selection effect.
- A 2-seed pilot (`te1_pilot_external_20260803`) ran the external hook end
  to end first: 2/2 replay-clean, 186/186 strict zero-diff actions.
