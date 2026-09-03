# S3-G1 verification report — "S3 rules complete"

Gate evidence for `docs/s3-tasks.md`'s **S3-G1**, checked literally against
[s3-design.md](../s3-design.md) §6 on the integrated tree (base
`759f73237b9068a5a5ec6447ee8698c602afc787`, worktree `_wt/s3g1`, branch
`s3g1`). Under the 2026-09-03 owner evidence directive
(conventions.md §1, s3-tasks.md protocol bullet 4) this project writes and
runs no unit tests; every item below is build evidence or a real-run replay
verdict, re-run on this tree rather than carried forward from a task Log.

This report's second half — the **`UNVERIFIED-until-captured` list** — is
**S3.62's work order** (design §6 item 9, S3-G2 item 9): every row must reach
zero before S3-G2 can close.

## 1. Six presets build

`win-debug` / `win-asan` / `win-release` (clang-cl via a task-scoped
vcvars64+LLVM wrapper, `s3g1env.cmd`; `/EHsc` verified present in each
`CMakeCache.txt` before build) and WSL `debug` / `asan` / `release`
(`tools/wsl_run.sh --script tools/build_presets.sh debug asan release`, exit
0, `PRESETS BUILT: debug asan release`). All six exit 0, zero warnings
promoted to errors. **PASS.**

## 2. Stage-A fixtures and golden vectors byte-identical

`build/win-release/bin/gen_combat_fixtures.exe` run once: `generated 20/20
fixtures`. `git status tests/golden/` empty before and after — byte-identical.
**PASS.**

## 3. Committed corpora zero-diff, injected controls fail loud

`tools/wsl_run.sh --script tools/corpus_replay.sh` (release preset) over all
three committed CI corpora (`act1_a20_50`, `three_act_a20_5`, `keys_a20_4`)
under `--replay`, `--costs` and `--masks`:

| corpus | `--replay` | `--costs` | `--masks` |
|---|---|---|---|
| `act1_a20_50` (50 seeds) | ZERO-DIFF | ZERO-DIFF | ZERO-DIFF |
| `three_act_a20_5` (5 runs) | ZERO-DIFF | ZERO-DIFF | ZERO-DIFF |
| `keys_a20_4` (4 captures) | ZERO-DIFF | ZERO-DIFF | ZERO-DIFF |

All nine injected-divergence negative controls (`state` / `cost` / `mask`,
one per corpus per kind) **fail loud** ("control fails loud, as required"),
proving each comparison has teeth.

`--replay --vitals` over `three_act_a20_5` and `keys_a20_4`: both
**VITALS-CLEAN**. **PASS.**

## 4. Information layer: `PUBLIC_VIEW_VERSION` 7, GT0 leak gates

`PUBLIC_VIEW_VERSION = 7` — `include/sts/engine/public_view.hpp:155`.

`twin_test` and `tripwire_test`, run directly against the WSL `release`
build (not through ctest):

- **`twin_test`: 11/11 PASSED** (stale-count-ok — a run result recorded at
  this gate check, re-derivable via `ninja -C build/release test/run
  twin_test`, not a claim carried forward). Per-phase sweep table
  (`TwinSweep.PublicViewAndMaskAreByteIdenticalInEveryRunPhase`): `10737
  states; phase0=0 phase1=345 phase2=687 phase3=7495 phase4=1579 phase6=120
  phase7=67 phase8=18 phase9=360 phase10=66` — zero leaks. The directed
  Act-4 coverage table (`TwinPhaseCoverage.Act4DoorCrossingRoomsAndTerminal
  AreTwinInvariant`) drives 16 states × 15 twin seeds at both A15 and A20 —
  Act4 Elite/MAP_CHOICE/REST_SITE/SHOP, Door (keys forced / no keys),
  `RUN_OVER`/ACT3_STOP, `RUN_OVER`/HEART(TrueVictory) — zero leaks.
- **`tripwire_test`: 10/10 PASSED** (stale-count-ok — same basis as above),
  including all three negative controls
  firing by name: `FiresWhenAClassificationRowIsRemoved` (unclassified bytes
  between `stolen_live` and `<end>`), `FiresOnOverlappingRows` (overlap
  between `monster_cursor` and `elite_cursor`), `FiresWhenAPaddingRowIs
  DeclaredTooSmall` (unclassified bytes between `pad_emerald` and
  `rewards`).

`tools/dist_check/sampler_dist.sh release` (nightly mode — the script's
sanctioned entry point, `STS_SAMPLER_DIST_MODE=nightly`): **5/5 PASSED**, all
nine pre-registered hypotheses retained under Holm:

| hypothesis | chi2 | df | p | n |
|---|---|---|---|---|
| draw.unconstrained_permutation_uniform | 20.328 | 23 | 0.622053 | 24000 |
| draw.exact_prefix_conditional | 2.173 | 5 | 0.824726 | 24000 |
| draw.relative_order_interleaving | 4.995 | 11 | 0.931409 | 24000 |
| encounter.weak_suffix_pair | 2.6212 | 5 | 0.758142 | 30000 |
| encounter.strong_suffix_pair | 69.9681 | 71 | 0.512351 | 90000 |
| encounter.elite_suffix_pair | 4.77283 | 3 | 0.189207 | 30000 |
| relic.remainder_permutation_uniform | 21.272 | 23 | 0.564481 | 24000 |
| relic.remainder_position_marginal | 11.233 | 11 | 0.423955 | 24000 |
| seedfilter.weak_second_encounter | 0.93895 | 2 | 0.625331 | 49615 |

and all three mutants correctly rejected (`mutant.draw_naive_shuffle`
p=1.4e-130, `mutant.relic_remainder_early_return` p=0,
`mutant.encounter_ignores_weights` p=0).

`sampler_dist_test` also run directly in **smoke mode**
(`STS_SAMPLER_DIST_MODE` unset — the per-commit N; H9/`seedfilter` not
executed at this scale): **5/5 PASSED**, eight hypotheses retained under Holm
at the smaller n (2400-9000 per row), same three mutants correctly rejected.

**PASS.**

## 5. The four-act soak

[s3-52-four-act-soak.md](s3-52-four-act-soak.md) exists. All 17 recorded
sha256 artifacts under `D:\STS_BG_Mod\_oracle_data\s3\s352_soak\`
(`s352_summary_s0..s5.kv`, `s352_report_s0..s5.txt`,
`s352_merged_report.txt`, `s352supp_summary_s0.kv`,
`s352supp_report_s0.txt`, `livelock_probe/probe_summary_s0.kv`,
`livelock_probe/probe_report_s0.txt`) recomputed and matched **exactly**
against the report's table — 17/17. Not re-run (10M-action soak; verifying
the recorded artifacts is the gate's instruction). **PASS.**

## 6. `check_stale_counts.sh` / `check_doc_links.sh`

Both run from Git Bash on the Windows host: `check_stale_counts: clean`;
`check_doc_links: clean (60 files scanned, 64 indexed)`. **PASS.**

## 7. Document conflicts found and fixed during this gate check

Per conventions §4 ("discovering any conflict between documents is
stop-the-line; fix the losing document in the same change"): three
`s3-design.md` markers that had already been resolved by a landed task's Log
and the ledger's own Deferred-obligations table, but whose placeholder text
in the frozen design doc was never rewritten in place, were corrected with
new §9 change-log entries in this commit:

- §2.3's back-attack `UNVERIFIED — needs decompile check` marker (resolved
  by S3.42: the engine does model facing, one flag bit, and the S3 collapse
  is exact).
- §4.1's `SpireHeart$CUR_SCREEN` ordinal-order marker (resolved by S3.31:
  `INTRO 0, MIDDLE 1, MIDDLE_2 2, DEATH 3, GO_TO_ENDING 4`).
- §4.1's clicks-1-2 collapse-or-model open question (resolved by S3.31:
  MODEL all four, witnessed by both three-act corpus victories' post-victory
  tail going from 0 of 5 to 5 of 5 records compared).

No mechanic changed; all three are text-only corrections carrying forward a
decision already made and evidenced elsewhere in the tree.

---

## 8. The `UNVERIFIED-until-captured` list (S3.62's work order)

Every row below is landed (code merged, all-preset-build + corpus-replay
evidence passing) but has **no live-game capture witness yet**. The
**reach precondition** for every Act-4 row is the same and is named once
here rather than in each row: **a keyed run that clears both Act-3 boss
rooms (the A20 double-boss precedent) and takes the Door with all three
keys** — no such capture exists today (S3.22 measured 39,296 key-policy
rows: 417 Act-3 boss fights, 14 first-boss kills carrying all three keys,
**zero** keyed victories; S3.61 re-measures this on the gated tree before
S3.62 schedules captures). Rows discharged by an Act-4 entry alone (traps
2/3/8, a20.yaml rows 1/20) need only that the run *reach* Act 4, not that it
defeat Shield-and-Spear or the Heart.

### 8.1 Registry rows (§2 inventory — S3-G1 bar item 1)

| row | domain / id | discharged by | prerequisite |
|---|---|---|---|
| `Shield and Spear` | encounters.yaml 62 (ELITE, act 4) | S3.62 Shield-and-Spear fight capture, `--combat` zero-diff | reach + Act-4 elite room entered |
| `The Heart` | encounters.yaml 63 (BOSS, act 4) | S3.62 Heart-kill capture, `--combat` zero-diff | reach + Act-4 boss room entered |
| `SPIRE_HEART` | events.yaml 52 (member of no act) | S3.62 Heart-kill capture (only reachable through the GO_TO_ENDING arm) | reach |
| `Surrounded` | powers.yaml 136 (player flag) | S3.62 Shield-and-Spear fight capture | reach + elite entered |
| `Back Attack` | powers.yaml 137 (monster flag) | S3.62 Shield-and-Spear fight capture, **both kill orders** (trap 7) | reach + elite entered |
| `Beat of Death` | powers.yaml 138 | S3.62 Heart-kill capture, a lethal turn where the killing card's retaliation lands (S3.44) | reach + boss entered + a lethal turn |
| `Invincible` | powers.yaml 139 | S3.62 Heart-kill capture, a hit exceeding the pool + a later restore (trap 9); also the first witness of the `misc_field` tag's `Invincible`/`maxAmt` member (S3.21) | reach + boss entered |
| `SpireShield` | monsters.yaml 67 | S3.62 Shield-and-Spear fight capture | reach + elite entered |
| `SpireSpear` | monsters.yaml 68 | S3.62 Shield-and-Spear fight capture | reach + elite entered |
| `CorruptHeart` | monsters.yaml 69 | S3.62 Heart-kill capture | reach + boss entered |

### 8.2 §5 traps (S3-G1 bar item 2)

| trap | statement | status | discharged by |
|---|---|---|---|
| 1 | holding the emerald key removes a `mapRng` draw from every later act | **DISCHARGED 2026-09-03 by S3.23** — 7 same-seed pairs, maps differ in exactly the burning-elite mark; promoted into `keys_a20_4` | — |
| 2 | Act-4 construction consumes no `mapRng` beyond seeding | UNVERIFIED-until-captured | S3.62 Act-4 entry capture (A20 + below A20, S3.32) |
| 3 | Act-4 construction consumes no `monsterRng` | UNVERIFIED-until-captured | S3.62 Act-4 entry capture (A20 + below A20, S3.32) |
| 4 | Act-4 monsters spend exactly ONE `monsterHpRng` draw each | UNVERIFIED-until-captured | S3.62 Shield-and-Spear + Heart fights, `--combat` (S3.41) |
| 5 | the Heart kill still spends one `miscRng.random(-5,5)` | UNVERIFIED-until-captured | S3.62 Heart-kill capture, terminal gold (S3.33) |
| 6 | emerald key row makes the four-item potion-suppression branch reachable | UNVERIFIED-until-captured | S3.62 Black Star burning-elite capture (S3.11's sixth / S3.23's residue) |
| 7 | the two guards share one `Surrounded`/`BackAttack` lifetime; kill order is observable | UNVERIFIED-until-captured | S3.62 two Act-4 elite captures, one per kill order, `--combat` (S3.42) |
| 8 | A20 has no double boss in Act 4; A1 has no elite quota there | UNVERIFIED-until-captured | S3.62 Act-4 entry capture (one boss room, one elite node) |
| 9 | Invincible resets at the monster's turn start; is a damage modifier, not block | UNVERIFIED-until-captured | S3.62 Heart-kill capture (S3.43) |
| 10 | the Act-4 constants are dead and must stay dead | UNVERIFIED-until-captured | S3.62 Act-4 shop + elite captures |
| 11 | the `Spire Heart` dialog costs a floor and fires relic room-entry hooks | **PARTIALLY DISCHARGED** — the floor half (52, not 51) is witnessed by S3.31's committed three-act corpus (both double-boss victories, 5 of 5 post-victory records compared zero-diff); the **Maw Bank +12 gold** half is UNVERIFIED-until-captured | S3.62 (S3.31) — a `Spire Heart` capture holding Maw Bank |

### 8.3 `a20.yaml` rows (S3-G1 bar item 3 — all IMPLEMENTED, capture named in-row)

| row | level | Act-4 addition | discharged by |
|---|---|---|---|
| 1 | A1 | negative: elite quota has no Act-4 effect (`generateRoomTypes` never runs) | S3.62 Act-4 entry capture |
| 3 | A3 | Shield/Spear damage + `skewerCount` 3→4 | S3.62 Shield-and-Spear fight |
| 4 | A4 | Heart damage + `bloodHitCount` 12→15 | S3.62 Heart fight |
| 8 | A8 | Shield/Spear HP 110/160 → 125/180 (one `monster_hp_rng` draw each) | S3.62 Shield-and-Spear fight |
| 9 | A9 | Heart HP 750→800 (one `monster_hp_rng` draw) | S3.62 Heart fight |
| 18 | A18 | Artifact tier 1→2 (both guards); Spear's Burn pile discard→draw-top; Shield's SMASH block →flat 99 | S3.62 Shield-and-Spear fight |
| 19 | A19 | Heart's Invincible 300→200, Beat of Death 1→2 | S3.62 Heart fight |
| 20 | A20 | negative: no double boss fires in Act 4 (`TheBeyond`-gated) | S3.62 Act-4 entry capture |

All eight rows carry `IMPLEMENTED` in `registry/a20.yaml`'s `notes` field
with the re-read provenance citation and the A20 capture named in-row, per
the bar's literal wording.

### 8.4 Task-level capture debts named in their own Logs

| debt | owning task | status |
|---|---|---|
| S3.11/S3.23's sixth capture: a burning-elite claim on a Black Star run (trap 6) | S3.23 `[~]` | **STILL OWED — the sole reason S3.23 is not `[x]`.** Carried to S3.62 with the constructive route S3.22 found: a fifth `PolicyKind` refusing the emerald row while `act == 1`, since `s323_STS508459_keys` proves an Act-2 emerald claim is reachable when the Act-1 elite is not taken |
| S3.24's Courier restock capture | S3.24 `[x]` | UNVERIFIED-until-captured — a seed where the driver owns The Courier and buys the same colored slot twice in one shop visit, replayed `--replay --shop` |
| S3.31's Maw Bank +12 gold on the `Spire Heart` floor | S3.31 `[x]` | UNVERIFIED-until-captured — see trap 11 above |
| S3.31's Door GO_TO_ENDING arm | S3.31 `[x]` | UNVERIFIED-until-captured — no capture has ever held all three keys; discharged together with the Act-4 entry capture it hands off to |
| S3.32's two Act-4 entry captures (A20 + below A20) | S3.32 `[x]` | UNVERIFIED-until-captured — discharges traps 2/3/8 and a20.yaml rows 1/20 together |
| S3.33's four Act-4 rooms (rest, shop purchase, elite reward claim, Heart's terminal gold) | S3.33 `[x]` | UNVERIFIED-until-captured — the fifth item, the (3,4) node's map symbol, has **no comparison consumer** (`neutralize_incomparable` zeroes `map[]` on both sides) and needs no capture |
| S3.41's two fights (Shield-and-Spear, Heart) | S3.41 `[x]` | UNVERIFIED-until-captured — discharges all ten registry rows in §8.1 (two captures, not ten) |
| S3.42's two kill-order captures + the facing derivation's live-capture half | S3.42 `[x]` | UNVERIFIED-until-captured — discharges trap 7; the derivation itself is proven from source + a scripted standalone-combat witness, so only the *capture* half is owed |
| S3.43's one Heart capture (Invincible pool exceed + restore, `buffCount` ≥ 3) | S3.43 `[x]` | UNVERIFIED-until-captured — also the first witness of the `misc_field` tag's `Invincible`/`maxAmt` member |
| S3.44's Beat of Death instance on a lethal turn | S3.44 `[x]` | UNVERIFIED-until-captured — the engine fix (the drain rebuild at `resolve_pending_post_combat_actions_at_terminal`) is proven correct on 21 on-disk Sharp Hide captures + the fixture replay; only the Act-4-specific consumer (Beat of Death THORNS on a killing card) has no capture |
| `EchoForm`/`cardsDoubledThisTurn`, the fifth `misc_field` union member (S3.21) | **no ledger row currently owns this** | UNVERIFIED-until-captured — unlike the other four union members this is **not** Act-4-gated (Echo Form is an ordinary Ironclad rare power); it needs only an ordinary capture in which the player plays Echo Form and doubles a card. Flagged here as a gap: S3.62's breadth campaign (≥ 2,000 mixed-policy attempts) is the natural place to pick it up, but no task currently claims it as an explicit deliverable |

**Table size: 10 registry rows + 11 traps (1 discharged, 1 partially) + 8
a20.yaml rows + 11 task-level debt rows = 40 rows tracked, 38 outstanding
`UNVERIFIED-until-captured` items** (trap 1 fully discharged; trap 11 half
discharged). Every outstanding row funnels into the same handful of physical
captures S3.62 must take: **one Shield-and-Spear fight** (both kill orders),
**one Heart-kill capture** (a lethal Beat-of-Death turn, an Invincible pool
exceed + restore, `buffCount` ≥ 3), **two Act-4 entry captures** (A20 +
below A20), **an Act-4 shop purchase**, **an Act-4 rest**, **a `Spire Heart`
capture holding Maw Bank**, **a Black Star burning-elite claim**, **a
Courier restock**, and **an ordinary Echo Form capture**. Reach (a keyed
double-boss A20 victory) is the shared precondition for every Act-4-gated row
among them; S3.61 re-measures it and S3.62 schedules the captures once it
does.
