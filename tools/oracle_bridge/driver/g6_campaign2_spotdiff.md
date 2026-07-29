# G6 oracle spot campaign runbook, second capture — 30 seeds + a boss-reward pursuit

The G6 gate's third leg: **"Oracle spot campaign: ≥ 20 full-run seeds, Neow
through boss reward, zero un-triaged diffs through the run-level differ."**

This is the §13 follow-up to [`g6_campaign_spotdiff.md`](g6_campaign_spotdiff.md),
re-run on a master that carries the starter-upgrade fix, the six replay-harness
fixes and driver `b1.4.7`. Same structure as its predecessor and as
[`b45_reward_spotdiff.md`](b45_reward_spotdiff.md),
[`b47_treasure_spotdiff.md`](b47_treasure_spotdiff.md) and
[`b48_shop_spotdiff.md`](b48_shop_spotdiff.md).

**Headline: the previous capture's two blockers are half-cleared and half-not,
and the leg still does NOT pass.**

1. **§8.0's class (a) starter-upgrade divergence is GONE.** All five skeleton
   rows now carry an `upgraded:` program; every run that upgrades a Strike, a
   Defend or a Bash replays correctly, and the two arithmetic reproducers
   (STS01068, STS02041) are zero-diff. §8.6's unresolved 1-HP candidate
   (STS02002) is **CLEAN** to its run terminal.
2. **Two NEW class (a) divergences surfaced**, both previously masked behind the
   starter-upgrade frontier: a **Stone Calendar `atBattleStart` ordering bug in
   the run layer** (§8.0 below — it loses a fight) and an **unmodelled
   `mapRng` draw on the emerald-key elite room** (§8.1). Both are
   **STOP-THE-LINE** and neither was fixed here.
3. **Still no boss reward.** 30 main runs + **39 supplementary runs** across
   **13 policy seeds** produced **12 Act-1 boss fights and zero claims**. The
   `is_boss_combat_reward` / `_claim_boss_reward` path never fired.

Everything else is triaged with evidence: **zero un-triaged diffs across all 69
captured runs.**

---

## 1. Environment

Unchanged from B4.5 §1 and design §1.2 as amended at §11 v0.1.7. Slay the Spire
`12-18-2022` (`[V2.3.4]`), ModTheSpire `3.30.3`, BaseMod `5.56.0`,
`CommunicationMod-oracle 1.2.1-oracle.0`. The driver is **`b1.4.7`**; the
deployed fork jar's SHA-256 is
`7DC814AD240CBBD9100B2E8C92B6AA97B4ADFBED62FFED7961C6E5DE15884733`.

The replay tool is built from `master` at `7df27ab` with `tools/wsl_run.sh
release` — green (`release: PASS`, zero failures; re-derive the count with
`ctest -N`, never from this page — conventions §8), and `release` is the preset
gates require (conventions §2). Every read-out below is the **release**
`replay_run_diff`.

## 2. Preflight — the B4.5 §2 gate, all four checks

A one-seed, `--max-actions 1` capture under a **new** id. Never reuse a
preflight id; preserve the directory either way.

```bat
C:\Python39\python.exe orchestrator.py ^
    --campaign-id g6_preflight_20260728T153342Z_claude01 ^
    --seeds STS00041 --policy greedy --policy-seed 1234 ^
    --max-actions 1 --fresh
```

| # | Check | Result |
|---|---|---|
| 1 | `mts_launch<N>.log` names the sanctioned stack, and no stock `CommunicationMod` | **PASS** — `Slay the Spire (12-18-2022)`, `ModTheSpire (3.30.3)`, `basemod (5.56.0)`, `CommunicationMod-oracle (1.2.1-oracle.0)`; zero stock-`CommunicationMod` lines in either launch log |
| 2 | Deployed fork jar SHA-256 == the pin | **PASS** — `7DC814AD…4733`, re-derived with `Get-FileHash` against `<game>\mods\CommunicationMod-oracle.jar` |
| 3 | `campaign_progress.json` is `complete`, never `fatal_environment_drift` | **PASS** — `complete`, driver **`b1.4.7`**, 1 seed done, 0 failed |
| 4 | `validate_artifacts.py --require-oracle --campaign` | **PASS** — `1 file(s), 0 error(s)`, exit 0 |

The artifact header carries `oracle_block_enabled: true`, `version_source:
mts_launch1.log`, `crosscheck_ok: true` on the seed identity, and — new in
`b1.4.7` — **`policy_seed: 1234` persisted into the header**. Two launches are
expected here and are not a fault, for the reason the previous runbook gives: the
preflight hits its 1-action cap immediately, stops heart-beating, and the
orchestrator relaunches once before reading the completed ledger.

## 3. The seed list

The **same thirty seeds** as the first capture, unchanged at
`D:/STS_BG_Mod/_oracle_data/campaigns/g6_campaign_seeds.txt` (its provenance is
recorded in that file and in the predecessor's §3). Re-deriving the count from
the file gives 30 (the file is 37 lines, 7 of them comments) — conventions §8.

## 4. Capture

```bat
C:\Python39\python.exe orchestrator.py ^
    --campaign-id g6_campaign2_20260728T153342Z_claude01 ^
    --seeds D:/STS_BG_Mod/_oracle_data/campaigns/g6_campaign_seeds.txt ^
    --policy greedy --policy-seed 1234 ^
    --max-actions 1500 --stall-timeout 150 --campaign-timeout 7200 --fresh
```

`status: complete`, **30 done, 0 failed**, **1 launch, 0 relaunch events, 0
induced kills**. Wall clock 15:41:38Z → 15:49:45Z, **8m 07s** for 3,864 injected
actions.

| Statistic | Value | First capture |
|---|---|---|
| Outcomes | **30 / 30 `death`** — no `noop_wedge`, no `error_wedge`, no `action_cap` | same |
| Median floor | **7** (mean 8.37, max 16) | 7 / 8.3 / 16 |
| Floor histogram | 3:1 · 4:1 · 5:2 · 6:9 · 7:3 · 8:3 · 10:5 · 12:2 · 13:1 · **16:3** | 10:6 · 12:1 elsewhere identical |
| Median actions | 114 | 111.5 |
| Seeds needing a second attempt | **0** | 2 |

**The brief's expectation that the `b1.4.7` GRID confirm fix would produce deeper
runs is NOT borne out, and the honest reading matters for the next brief.**
Upgrade uptake is unchanged: **12 of 30 runs finish with an upgraded card, 17
upgraded cards in total, in BOTH captures** (only STS00353 and STS00509 differ in
*which* cards). Exactly one seed got deeper — STS00462, floor 10 → 12. Five seeds
changed their injected-action count (STS00365, STS01068, STS01221, STS02041,
STS02048); the fix changed how many actions it costs to confirm a grid, not
whether the upgrade lands. Greedy under `--policy-seed 1234` was already taking
these upgrades under `b1.4.6`.

Greedy again took **no Neow boss-relic swap** (so none of B4.5 §7's deferred
`onEquip` / `energyMaster` replay stops occur) and again **entered 20 shop
rooms**, so `--shop` is reported in §7 rather than skipped. Every upgrade taken
in the whole campaign is a **Strike, a Defend or a Bash** — the same three of the
five rows §8.0 of the predecessor flagged, which is why the fix is so visible
here.

## 5. Validation

```bat
C:\Python39\python.exe validate_artifacts.py --require-oracle ^
    --campaign D:/STS_BG_Mod/_oracle_data/campaigns/g6_campaign2_20260728T153342Z_claude01
```

**`30 file(s), 0 error(s)`, exit 0.** Strict mode passes on the main campaign and
**on all thirteen supplementary campaigns** (`3 file(s), 0 error(s)` each, exit 0
— 39 further files, 0 errors).

## 6. The frontier table — `replay_run_diff --replay`, every run

```bash
build/release/tools/oracle_bridge/replay/replay_run_diff --replay \
    /mnt/d/STS_BG_Mod/_oracle_data/campaigns/g6_campaign2_20260728T153342Z_claude01/run_*[!g].jsonl
```

`--- 30 file(s), 26 not clean ---`. Classes are the brief's: **(a)** real engine
divergence · **(b)** harness/mapping gap · **(c)** named deferred body ·
**(d)** capture artifact.

| Seed | Rec | First divergence | Stop | Class |
|---|---|---|---|---|
| STS00220 | 87 | seq 61 f3 `CARD_REWARD` (1) `potions[0] NONE→ColorlessPotion` | seq 86 `SHOP_ROOM` | **(c)** card-CHOOSE potion row |
| STS00221 | 105 | seq 25 f2 `EVENT` (1) `hp 55→75` | run terminal | **(d)** §8.2 Wheel-of-Change page offset; next persistent diff seq 53 is **(c)** Skill Potion |
| STS00283 | 43 | **none** | seq 42 `SHOP_ROOM` | **(b)** zero-diff to the stop |
| STS00353 | 27 | **none** | seq 26 `SHOP_ROOM` | **(b)** zero-diff to the stop |
| STS00365 | 40 | **none** | seq 39 `SHOP_ROOM` | **(b)** zero-diff — **was class (a) upgraded Strike; FIXED** |
| STS00425 | 83 | seq 38 f3 `CARD_REWARD` (1) ColorlessPotion | seq 82 `SHOP_ROOM` | **(c)** card-CHOOSE potion row |
| STS00451 | 71 | seq 30 f3 (1) `SneckoOil` | seq 70 event desync | **(c)** Snecko Oil row; **also carries §8.1 (a) at seq 82, masked** |
| STS00462 | 170 | seq 85 f7 (1) `gold 110→130` | run terminal | **(c)** stolen-gold in-combat ordering row |
| STS00463 | 110 | **none** | run terminal | **CLEAN** |
| STS00509 | 171 | seq 135 f11 (1) `hp 50→55` | run terminal | **(c)** the named ledger row — §8.5 |
| STS00572 | 161 | **none** | run terminal | **CLEAN** |
| STS00577 | 59 | **none** | seq 58 `SHOP_ROOM` | **(b)** zero-diff to the stop |
| STS00610 | 28 | **none** | seq 27 `SHOP_ROOM` | **(b)** zero-diff to the stop |
| STS00683 | 186 | seq 79 f5 (1) `gold 128→148` | run terminal | **(c)** stolen-gold; **then §8.0 class (a) at seq 141** |
| STS00700 | 115 | **none** | run terminal | **CLEAN** |
| STS00856 | 37 | **none** | seq 36 `SHOP_ROOM` | **(b)** zero-diff to the stop |
| STS01068 | 40 | seq 39 f3 `GRID` (1) `potions[0] NONE→LiquidMemories` | seq 39 unsimulated grid | **(c)** card-CHOOSE potion row; **also §8.1 (a) at seq 74, masked** |
| STS01221 | — | — | **translation aborted** | **(c)** §8.6 `DuplicationPower` |
| STS01314 | 60 | seq 22 f2 `EVENT` (37) relic-pool rotation | seq 59 `SHOP_ROOM` | **(d)** §8.2 Wheel page offset (re-converges seq 23); persistent frontier seq 50 is **(c)** Distilled Chaos |
| STS01372 | 105 | seq 39 f3 `CARD_REWARD` (1) ColorlessPotion | seq 104 `SHOP_ROOM` | **(c)** card-CHOOSE potion row |
| STS01789 | 21 | **none** | seq 20 `SHOP_ROOM` | **(b)** zero-diff to the stop |
| STS01857 | 79 | seq 21 f2 (1) `DistilledChaos` | run terminal | **(c)** Distilled Chaos (now a ledger row) |
| STS01861 | 71 | seq 31 f2 `CARD_REWARD` (1) `AttackPotion` | seq 70 `SHOP_ROOM` | **(c)** card-CHOOSE potion row |
| STS01906 | 21 | **none** | seq 20 `SHOP_ROOM` | **(b)** zero-diff to the stop |
| STS02002 | 72 | **none** | run terminal | **CLEAN** — **the predecessor's §8.6 (a)-candidate is RESOLVED** |
| STS02009 | 100 | seq 61 f4 `EVENT` (1) `hp 67→56` | seq 99 unsimulated grid | **(d)** §8.2 Wheel page offset; **persistent frontier seq 96 is §8.1 class (a)** |
| STS02041 | 78 | **none** | seq 77 `SHOP_ROOM` | **(b)** zero-diff — **was class (a) upgraded Defend; FIXED** |
| STS02042 | — | — | **translation aborted** | **(c)** §8.6 `Vigor` / Akabeko row |
| STS02048 | 34 | **none** | seq 33 `SHOP_ROOM` | **(b)** zero-diff to the stop |
| STS02110 | 67 | seq 32 f3 (1) `potions[1] NONE→DistilledChaos` | seq 66 `SHOP_ROOM` | **(c)** Distilled Chaos |

**Tally:** 4 CLEAN (was 2) · 10 more zero-diff to a class (b) stop · **2 class
(a) shapes across 4 runs** · 14 class (b) stops · 14 class (c) frontiers · 3
class (d). Sixteen of the thirty stops are the same `SHOP_ROOM` message.

### 6.1 The supplementary corpus — 39 runs, same differ

`--- 39 file(s), 39 not clean ---`, and every one is attributed:

| First divergence | Runs | Class |
|---|---|---|
| `potions[0] NONE→ColorlessPotion` (STS01372) / `→DistilledChaos` (STS00353) | 22 | **(c)** card-CHOOSE potion row / Distilled Chaos row |
| **none** — zero-diff to the stop | 15 | **(b)** the stop only |
| translation aborted, `DuplicationPower` (STS01221 under ps42, ps777) | 2 | **(c)** `registry/potions.yaml` DUPLICATION_POTION deferral |

Stops: 19 `SHOP_ROOM`, 11 run terminal, 7 `event command 'choose' … in COMBAT`
(always downstream), 2 aborts. **No class (a) shape appears anywhere in the
supplementary corpus** — grepping its whole read-out for `relics[N].counter` and
`map_rng` returns zero hits, because these three seeds hold no Stone Calendar and
enter no emerald-key elite.

## 7. Per-mode read-outs (breadth)

All five modes over the 30 main artifacts, then over the 39 supplementary ones.
Each is a separate invocation; the modes are mutually exclusive.

| Mode | Main (30) | Supplementary (39) | Verdict |
|---|---|---|---|
| `--neow` | `28 fully zero-diff, 0 clean through activation only, 2 diverged` | `37 fully zero-diff, 0 …, 2 diverged` | **PASS.** The only "diverged" are §8.6's translation aborts. **65 / 65 readable seeds zero-diff** on options, activation and post-choice |
| `--event` | `48 sighting(s), 48 zero-diff, 0 diverged; entry-page option count matched 48 of 48 advisory check(s)`; `12 deals read out, 12 zero-diff; 73 positions compared, 60 attempt outcomes reproduced, 120 grid rounds walked` | `65 sightings, 65 zero-diff, 0 diverged; option count matched 65 of 65`; `20 deals, 20 zero-diff; 120 positions, 100 outcomes, 200 grid rounds` | **PASS, cleanly.** The predecessor's §8.3 Match-and-Keep false invariant is gone: **113 sightings and 32 constructor deals, all zero-diff, zero DIFFs** |
| `--treasure` | `12 treasure room(s), construction clean 12, 12 opened (12 clean) / 0 skipped, in-room walks clean 12, 0 divergence(s)` | `31 rooms, construction clean 31, 31 opened (31 clean) / 0 skipped, walks clean 31, 0 divergence(s)` | **PASS.** 43 chests, zero divergences; every `OPEN` line records that *"capture carries the expected trailing SAPPHIRE_KEY row"* and the row is reproduced |
| `--shop` | `20 merchant(s) built (0 with a visible shelf), stock clean 20, purchase walks clean 20, 0 divergence(s)` | `48 merchants (0 with a visible shelf), stock clean 48, walks clean 48, 0 divergence(s)` | **PASS on the stock half only.** 68 merchants' `merchantRng` / `cardRng` / `potionRng` construction proved. **Zero shelves** — greedy walks straight through, so no purchase, no purge and no `Meal Ticket` path is exercised. Unchanged from the predecessor |
| default (reward) | `100 reward screen(s), assembly clean 99 (3 capture-seeded STOLEN_GOLD), claim clean 100, 3 failing file(s)` | `142 screens, assembly clean 135 (3 seeded), claim clean 142, 9 failing file(s)` | **PASS but for §8.3.** **Claim is clean on all 242 screens.** The 12 failing files are 4 translation aborts + **8 reproductions of one shape**, §8.3 |

**Zero `library-order-only` records across every mode, on 242 reward screens** —
B4.5 §6's card-pool library order stays closed.

## 8. Findings, by class

### 8.0 — CLASS (a), STOP-THE-LINE: the run layer runs Stone Calendar's `atBattleStart` one step too late

**A run-layer combat calls `atBattleStart` AFTER turn 1's `atTurnStart`, so every
relic whose `atBattleStart` writes state directly is off by one turn for the
whole fight.** Stone Calendar is the S1 row that makes it observable.

`StoneCalendar.java:36-68` is `atBattleStart { counter = 0 }`, `atTurnStart {
++counter }`, `onPlayerEndTurn { if (counter == 7) → 52 THORNS to all enemies }`,
`onVictory { counter = -1 }`. The engine's body
(`src/engine/relics/relics_rare.cpp:206-237`) is a faithful transcription and is
not at fault.

The call **order** is:

- `src/engine/run_advance.cpp:551` — `begin_first_turn(s, dispatch_monster_turn)`,
  which runs `start_of_turn(TurnStart::kCombatStart)`
  (`action_queue.cpp:233-253`) and dispatches **`AT_TURN_START`** → Stone
  Calendar `++counter`, taking it from its out-of-combat `-1` to **0**.
- `src/engine/run_advance.cpp:611` — `dispatch_relics_at_battle_start(...)` →
  Stone Calendar `counter = 0`, which now **re-writes a value it should have set
  first**.

The Java order is the reverse, and the comment block at
`run_advance.cpp:559-564` *lists it correctly* before the code does the opposite:

```
applyStartOfCombatLogic()   (AbstractRoom.java:245)
applyStartOfTurnRelics()    (AbstractRoom.java:253)
```

**Why this is class (a) and not a documented deviation.** The paragraph at
`run_advance.cpp:572-598` does justify placing the dispatch after the pump, but
its whole argument is about relics whose `atBattleStart` **queues a step** —
*"`queue_relic_step` (relic_hooks.cpp) is addToBot-only, so the faithful
placement is after the opening draw has resolved"* — and its explicit "known
deviation, currently unobservable" list is exactly five addToTop bodies (Blood
Vial, Vajra, Bronze Scales, Oddly Smooth Stone, Pantograph), argued safe because
they are heals and player powers the draw does not read. **Stone Calendar queues
nothing**: `dispatch_relic_hook` calls the native body inline, so `slot.counter =
0` executes immediately, and the queue argument does not reach it. No Deferred
obligations row names it — the nearest, *"`dispatch_relics_at_pre_battle` at the
run entry"* (B3.27), is a **different hook** (`atPreBattle`) with a different
symptom (Snecko Eye's Confusion). Per conventions §5 an uncarried divergence is
invisible during execution and is stop-the-line.

**The live reproducer, arithmetic-exact and fatal.** STS00683, floor 12, a
three-Sentry elite fight. The sim's counter is exactly one behind the capture's
for the entire fight:

| seq | capture → sim | |
|---|---|---|
| 141-145 | `relics[1].counter: 1 → 0` | turn 1 |
| 146-149 | `2 → 1` | turn 2 |
| 150-151 | `3 → 2` | turn 3 |
| 152-155 | `4 → 3` | turn 4 |
| 156-158 | `5 → 4` | turn 5 |
| 159-162 | `6 → 5` | turn 6 |
| 163-164 | **`7 → 6`** | **turn 7 — the game fires, the sim does not** |
| 165 | `-1 → 7` | **capture's combat is OVER (`onVictory`); sim is still in `COMBAT`, 47 fields diverged** |

`relics[1]` is Stone Calendar, confirmed off the artifact (`relics: [('Burning
Blood', -1), ('StoneCalendar', 1)]` at seq 141). The counter's value at seq 141 is
the proof of the mechanism: the capture reads **1** (`atBattleStart` 0, then turn
1's `++`), the sim reads **0** (turn 1's `++` from `-1`, then `atBattleStart`'s
overwrite).

**Second reproduction:** the *first* campaign's independently captured
`g6_campaign_20260728T053354Z_claude01/run_STS00683` replays through the release
build with the identical offsets at the identical sequence numbers (141 → `1 →
0`, 146 → `2 → 1`, …). Two campaigns, two captures, one shape.

**Why it survived.** `tests/relic_rares_shop_test.cpp:356`
(`StoneCalendarFiresFiftyTwoThornsAtEndOfTurnSeven`) calls
`dispatch_relics_at_battle_start` and *then* `dispatch_relics_at_turn_start` in a
hand-written loop — it hard-codes the correct order and never drives either real
combat-entry path. The test proves the relic **body**; nothing tests the run
layer's **wiring**. That is the conventions §5 "no rule without its test" gap one
layer up. Note also that `combat_begin` (`advance.cpp`) never dispatches
`AT_BATTLE_START` **at all** — grep finds the call at `run_advance.cpp:611` and
nowhere else — so the batch API never resets the counter either; that half is
untested here and should be checked by whoever owns the fix.

Not fixed here, per the brief.

### 8.1 — CLASS (a), STOP-THE-LINE: the emerald-key elite room's `mapRng` draw is unmodelled

`MonsterRoomElite.applyEmeraldEliteBuff` (`MonsterRoomElite.java:39-69`), reached
from `AbstractPlayer.java:1603-1604`, rolls **`AbstractDungeon.mapRng.random(0,
3)`** on entering an elite room whose node carries the emerald key. The engine
models the *placement* draw — `include/sts/engine/map_rooms.hpp:18,46,201` cite
`AbstractDungeon.setEmeraldElite` (`AbstractDungeon.java:542-556`) and match the
oracle's post-`generateMap` triple — but nothing models the *entry* roll. The
string `emerald` does not occur anywhere in the run or combat entry path.

`mapRng` has exactly four consumers in the decompiled tree
(`MapGenerator.generateDungeon`, `RoomTypeAssigner.distributeRoomsAcrossMap`,
`setEmeraldElite`, and this one). The first three all run at act start, so a
draw at floor 6 can only be this one.

**Three seeds, both campaigns — six observations.** Sweeping every artifact for a
mid-run `mapRng` counter change:

| Seed | Where | `mapRng.counter` | In campaign 1? |
|---|---|---|---|
| STS00451 | seq 82, floor 6, `MonsterRoomElite` | 96 → 97 | yes, seq 82, identical |
| STS01068 | seq 74, floor 6, `MonsterRoomElite` | 98 → 99 | yes, seq 69, identical |
| STS02009 | seq 96, floor 6, `MonsterRoomElite` | 96 → 97 | yes, seq 96, identical |

**STS02009 is the clean witness.** Records 62-95 are zero-diff; at seq 96 exactly
**three** fields differ and all three are `map_rng` (`s0`, `s1`, `counter 97 →
96`), with the sim's `s1` equal to the capture's `s0` — the signature of being
exactly one step behind. On the other two seeds the shape is real but **masked**
behind an earlier class (c) potion frontier, which is why it never surfaced
before.

**Bounded blast radius, stated honestly.** The capture's Sentries at seq 96 are
`49/49, 49/49, 56/56` with `Artifact 1` and no Strength, Metallicize, Regenerate
or raised max HP — and the sim's match exactly. So in this capture the roll
happened and **no buff landed**, and within S1 nothing else reads `mapRng` after
act-start generation. The observable consequence is therefore **stream position
only**. It is still a real divergence of a stream the whole oracle is built on,
it is on no ledger row, and the mechanism by which the rolled buff fails to reach
the monsters was **not** root-caused here — do not treat "no buff observed" as
proof the effect can never matter.

> **CORRECTION (2026-07-28, `fix-battlestart-order` a03f257, per conventions
> §4):** the mechanism claim above is wrong — **the buff DID land in every
> observation.** "The sim's match exactly" was structurally unverifiable
> because `--replay` diffs `RunState` only and never compares monster HP or
> powers. The captures prove it directly: STS02009's Sentries read 49/49/56
> — impossible unbuffed (A20 range 39–45, `Sentry.java:63`), exactly
> 39/39/45 + round(25 %) = roll 1 — and STS00451's GremlinNob carries
> Strength 2 at entry, STS01068's Lagavulin Metallicize 8 + Strength 2 =
> roll 0 each (rolls independently recovered by inverting the post-roll
> `mapRng` state one XorShift128+ step). The honest caveat in this section
> was exactly right; the fix models the roll AND the buff (arms 0/1/2; arm
> 3 parked pending its `PowerId`, since registered as 91).

### 8.2 — CLASS (d), known-benign: Wheel of Change resolves its prize one page early

Unchanged from the predecessor's §8.1 and reproduced exactly. The sim commits the
prize on the **`spin`** press; the game commits it on the **`Prize!`** press one
record later. Three sightings, three prize kinds, one shape:

| Seed | Page | Prize | Diff at the early record |
|---|---|---|---|
| STS00221 | seq 25 | +20 HP heal | `hp: 55 → 75` (sim already healed) |
| STS02009 | seq 61 | −11 HP | `hp: 67 → 56` (sim already paid) |
| STS01314 | seq 22 | Toy Ornithopter | 37 fields — the common relic pool rotated by one front-pop, `count 33 → 32` |

It re-converges on the next record every time. `--event` reports all three
zero-diff, because that mode compares **arrival**, which is before the prize.

### 8.3 — CLASS (b): the default reward mode cannot see that the monsters escaped

**This resolves the predecessor's §8.8 "STS01372 f7 — unattributed", and the
answer is not the ledger row it guessed at.**

Eight reward screens across nine campaigns read:

```
treasure_rng: {…,3} -> {…,4}
reward items: [CARD] -> [GOLD,CARD]
```

— the sim rolls a GOLD row the game did not offer, one extra `treasureRng` draw.
The claim itself is clean every time (main campaign f7: `gold=108` both sides).

**The cause is a Looter that ESCAPED.** Reading the capture straight off the
artifact for the main campaign's STS01372 floor 7: the Looter's intent turns to
`ESCAPE` at seq 99 and it leaves at 4 HP; the player's gold has already gone
148 → 128 → 108 on two steals; and the reward screen at seq 102 carries **`[CARD]`
alone**. Verified identically on two supplementary runs (ps7 floor 4, escape at
1 HP; ps99 floor 8, escape at 10 HP) — same intent trace, same `[CARD]`-only
screen.

The engine is right and models this: `combat_rewards.cpp:268-292` carries
`RewardOutcome::MONSTERS_ESCAPED` and suppresses the gold roll under it, and
`combat_rewards.hpp:83-87` spells out that the flag is true iff *every* monster
escaped. What the **default reward mode** cannot do is seed that outcome: it
takes a translated `RunState`, and "did the group escape" is combat state — the
exact structural limit as §8.8's `STOLEN_GOLD` accumulator, which was solved by
seeding one number from the capture's own reward row. **Class (b), a limit of
what that mode seeds, not an engine gap.**

Honest caveat: `--replay` — which drives the whole run through the run layer and
*does* know the outcome — could not independently confirm the engine half here,
because on all eight of these runs an earlier class (c) potion frontier is
already open by the time the Looter escapes.

Eight reproductions: main STS01372 f7; supplementary ps3 f7, ps5 f7, ps7 f4,
ps13 f7, ps31337 f4, ps77 f7, ps99 f8.

### 8.4 — CLASS (b): the stops, all four shapes

Sixteen main-campaign and nineteen supplementary stops are `screen 'SHOP_ROOM' is
not modelled by the run layer`; eight more are the `event command 'choose'
arrived while the sim is in COMBAT` desync guard, which is **always** downstream
(read the `first divergence:` line, never the stop); two are the "capture opens a
master-deck grid the sim never opened" message, both of which now correctly say
*"no relic onEquip can be pending in that phase"* rather than blaming Burning
Blood — the predecessor's §8.7 fix, confirmed live. **None of these is new**, and
nine main-campaign runs plus fifteen supplementary ones are zero-diff right up to
one of them.

### 8.5 — CLASS (c): the named ledger row reproduces exactly

STS00509, **seq 135, floor 11**, one field, `hp: 50 → 55`, persisting to the run
terminal (and `hp: 56 → 61` at seq 137). This is the Deferred obligations row
**"STS00509 seq-135 Louse-turn 5-HP residual"** (deferred by
`fix-starter-upgrades`' frontier re-sweep, `UNASSIGNED — engine owner`), which
records the same seed, the same seq, the same floor and the same `hp: 50→55` off
the *first* campaign. A second, independent capture reproduces it to the unit.
No action beyond confirming the row is real and still open.

### 8.6 — CLASS (c): translation aborts and deferred potion bodies

Four runs abort translation, all fail-loud, all naming a documented deferral:
**`DuplicationPower`** (main STS01221 record 105; supplementary ps42 and ps777
STS01221 record 103) — `registry/potions.yaml` `DUPLICATION_POTION`, deferred on
the recursive-play opcode; and **`Vigor`** (main STS02042 record 5) — obligation
row *"Akabeko (Vigor power row)"*, granted by a relic, the case that row did not
anticipate. As the predecessor noted, the differ aborts the whole file rather
than reporting a bounded stop; that remains a question for the tool's owner.

Every potion frontier in §6 and §6.1 is a potion whose body is deliberately
deferred, and the sim's refusal to spend it is by design (`potion_use_implemented`
keeps it off the legal-action mask). **Distilled Chaos now has its own Deferred
obligations row** — the predecessor's §8.9 request was actioned.

| Potion | Runs | Row |
|---|---|---|
| Colorless Potion | STS00220, STS00425, STS01372, + 14 supplementary STS01372 | *"In-combat card-CHOOSE potion bodies: …"* (B3.23) |
| Attack / Skill Potion | STS01861, STS00221 | same row |
| **Liquid Memories** | STS01068 | same row (named in it explicitly) |
| Snecko Oil | STS00451 | *"Snecko Oil cost-randomization potion body"* (B3.23) |
| Distilled Chaos | STS01857, STS02110, STS01314, + 8 supplementary STS00353 | *"Distilled Chaos potion body"* — **now on the table** |

### 8.7 — What the predecessor's blockers did

For the record, since re-running the same seeds is the only way to see it:

- **§8.0 (five skeleton rows with no `upgraded:` program) — GONE.** STS00365 and
  STS02041, the two arithmetic reproducers, are now **zero-diff to their stops**;
  STS01068's frontier moved from seq 13 to seq 39 and is now a deferred potion.
  Bash is exercised too (STS00509 finishes with `Bash+`).
- **§8.6 (STS02002's unexplained 1 HP) — RESOLVED.** STS02002 is **CLEAN** to its
  run terminal, 72 records. It was a downstream shadow of the starter-upgrade
  bug after all.
- **§8.2, §8.3, §8.4, §8.7, §8.8's `STOLEN_GOLD` half — all confirmed fixed
  live.** STS00700 is CLEAN; STS00283 and STS00856 are zero-diff to their
  `SHOP_ROOM` stops; STS00683 walks to its terminal at 186 records; `--event` is
  113/113 sightings and 32/32 deals zero-diff with zero DIFFs; the grid-stop text
  no longer blames Burning Blood.

## 9. Driver behaviour — the predecessor's §9 did not recur

The first capture lost two seeds to spurious relaunches of a healthy game
(`heartbeat stale/absent 151s (> 150); game still up`). **This capture ran all 30
seeds on a single launch: 0 relaunch events, 0 induced kills, 0 seeds needing a
second attempt** — and all thirteen supplementary campaigns likewise ran
`launches: 1`, 0 relaunches, 0 induced kills. Fifty-two launches' worth of
opportunity, zero occurrences. The `b1.4.7` heartbeat hardening is consistent with
that, though a non-recurrence is not by itself proof the structural weakness the
predecessor described is gone.

The potion-belt overflow stall (`NOT ENOUGH POTION SLOTS`, unstuck by the driver's
90 s watchdog) was observed again and remains the dominant per-run time cost.
Benign — the noop path recovers and the artifact is unaffected.

## 10. Boss fights and boss rewards — the pursuit

**Main campaign: three of thirty reached the Act-1 boss, zero claimed a boss
reward, zero victories.** All 30 terminals are `death` — the same three seeds as
the first capture.

| Seed | Boss | State at the player's death |
|---|---|---|
| STS00353 | Hexaghost | 170 / 264 (36 % dealt) |
| STS01221 | **Slime Boss killed**, died to the split | split remnants 126 / 342 |
| STS01372 | Slime Boss | 88 / 150 (41 % dealt) |

### 10.1 Supplementary mini-campaigns

Per the brief, the three boss-reaching seeds were re-run under twelve further
`--policy-seed` values. **Thirteen campaigns, 39 runs, budget ~40 — stopped on
budget, not on a claim.**

```bat
C:\Python39\python.exe orchestrator.py ^
    --campaign-id g6_boss_ps<N>_20260728T153342Z_claude01 ^
    --seeds D:/STS_BG_Mod/_oracle_data/campaigns/g6_boss_seeds.txt ^
    --policy greedy --policy-seed <N> ^
    --max-actions 1500 --stall-timeout 150 --campaign-timeout 3600 --fresh
```

| `--policy-seed` | STS00353 | STS01221 | STS01372 | boss fights |
|---|---|---|---|---|
| 7 | **f16 Hexaghost** | **f16, boss killed → split** | f5 | 2 |
| 42 | f14 | **f16, boss killed → split** | f13 | 1 |
| 99 | f14 | f8 | f13 | 0 |
| 2024 | f14 | f5 | f4 | 0 |
| 3 | f5 | f5 | f10 | 0 |
| 5 | f14 | f5 | f13 | 0 |
| 11 | f14 | f12 | f3 | 0 |
| 13 | f5 | f7 | f14 | 0 |
| 31 | f14 | **f16 Slime Boss 98/150** | f5 | 1 |
| 77 | **f16 Hexaghost** | **f16 Slime Boss 82/150** | f13 | 2 |
| 555 | **f16 Hexaghost** | **f16 Slime Boss 82/150** | f3 | 2 |
| 777 | f5 | **f16, boss killed → split** | f3 | 1 |
| 31337 | f5 | f7 | f5 | 0 |

**39 / 39 `death`. Nine further boss fights. Zero `act1_boss_reward` terminals,
zero victories.** Combined with the main campaign: **12 Act-1 boss fights across
69 runs, zero claims.**

### 10.2 BOSS REWARD: NOT CLAIMED — what that means and why

The claim path is unambiguous and was never entered.
`campaign_driver.py:563-570` gates on `act == 1 && room_type ==
"MonsterRoomBoss" && screen_type ∈ {COMBAT_REWARD, CARD_REWARD}`; the loop at
`:1081-1084` then calls `_claim_boss_reward` (`:1219-1253`), which presses
`choose 0` until the screen empties and writes the terminal
**`act1_boss_reward`** — deliberately *without* pressing `proceed`, since the
boss chest is S2 (design §1.1). **No artifact in any of the fourteen campaigns
carries an `act1_boss_reward` terminal, and no record in any of them shows a
`COMBAT_REWARD` or `CARD_REWARD` screen in a `MonsterRoomBoss`.** The evidence of
absence is machine-checked over all 69 runs, not inferred from the outcome
column.

The failure mode is a **policy** one and is the same one the predecessor
diagnosed. STS01221 reaches the Slime Boss on 6 of 7 attempts that get to floor
16, and on **four** of them (`1234`, `7`, `42`, `777`) it *kills the Slime Boss
outright* and then dies to the split — the last boss record shows Spike Slime (L)
/ (M) remnants totalling ~290-342 HP against a dead player. That is not a damage
shortfall; greedy has no plan for the split's burst. Changing `--policy-seed`
reshuffles routes and fights but cannot supply that plan, which is why 39 runs
bought nine more boss fights and no claim.

**The cheapest route to a first boss-reward capture remains a policy leg scoped
at surviving the Slime Boss split** (block / AoE weighting once `Slime Boss` HP
crosses its split threshold), exactly as the predecessor's §10 proposed. A
`--policy script` run driving a hand-authored winning line on STS01221 is the
other option and would prove the driver's claim path directly.

## 11. Known-benign shapes (for the next capture)

Everything here was seen, is understood, and is **not** a new finding:

- **`screen 'SHOP_ROOM' is not modelled by the run layer`** — 16 of 30 main stops,
  19 of 39 supplementary. Greedy *does* enter shops; `--shop` reads the same
  rooms out cleanly (§7).
- **`event command 'choose' arrived while the sim is in COMBAT/RUN_OVER`** — the
  desync guard. Always **downstream**; read the `first divergence:` line.
- **The unsimulated-grid stop** — now correctly says no relic `onEquip` can be
  pending in a COMBAT phase, instead of naming Burning Blood.
- **Wheel of Change's one-page prize offset** — §8.2, re-converges next record.
- **A deferred potion staying in the sim's belt** — §8.6, by design and
  fail-loud.
- **`cards_played_this_turn`, `monster_move_history`, `monster_attacks_queued`
  reading 0 on the capture side** in `--combat` — translator gaps, not
  divergences.
- **`card_pool` / `hand` / `draw` index permutations in `--combat`** — the two
  sides keep independent pool layouts; compare identities, not indices.
- **Zero `library-order-only` records** — B4.5 §6 stays closed across 242 reward
  screens. A deck mismatch whose count and upgrade agree but whose card id
  differs would be a **regression**.

## 12. What actually ran — 2026-07-28

| Artifact | Path |
|---|---|
| Preflight | `D:\STS_BG_Mod\_oracle_data\campaigns\g6_preflight_20260728T153342Z_claude01\` |
| Main campaign | `D:\STS_BG_Mod\_oracle_data\campaigns\g6_campaign2_20260728T153342Z_claude01\` |
| Supplementary ×13 | `D:\STS_BG_Mod\_oracle_data\campaigns\g6_boss_ps{7,42,99,2024,3,5,11,13,31,77,555,777,31337}_20260728T153342Z_claude01\` |
| Main seed list | `D:\STS_BG_Mod\_oracle_data\campaigns\g6_campaign_seeds.txt` |
| Boss-pursuit seed list | `D:\STS_BG_Mod\_oracle_data\campaigns\g6_boss_seeds.txt` |
| Differ read-outs | `D:\STS_BG_Mod\_oracle_data\campaigns\g6c2_readouts\` |
| Orchestrator logs | `…\<campaign-id>.out` beside each directory |

No directory may be reused, overwritten or retried in place; a new attempt gets a
new id (README, "Treat a campaign id as immutable evidence").

**Reproduce the read-outs** from the repo root, against the release build:

```bash
tools/wsl_run.sh release
C=/mnt/d/STS_BG_Mod/_oracle_data/campaigns/g6_campaign2_20260728T153342Z_claude01
S=/mnt/d/STS_BG_Mod/_oracle_data/campaigns/g6_boss_ps*_20260728T153342Z_claude01
B=build/release/tools/oracle_bridge/replay/replay_run_diff
$B --replay   $C/run_*[!g].jsonl     # the frontier table, §6
$B --neow     $C/run_*[!g].jsonl     # §7
$B --event    $C/run_*[!g].jsonl
$B --treasure $C/run_*[!g].jsonl
$B --shop     $C/run_*[!g].jsonl
$B            $C/run_*[!g].jsonl     # default reward mode
$B --replay   $S/run_*[!g].jsonl     # §6.1, and each mode likewise
```

## 13. Recording the result

**Do not tick the G6 oracle-spot-campaign leg on this capture either.** Of the
leg's three clauses:

- **≥ 20 full-run seeds — MET.** 30 in the main campaign, 69 runs in all,
  every one strict-validating.
- **Zero un-triaged diffs through the run-level differ — MET.** Every frontier
  and every stop in §6 and §6.1 is classified with evidence, and the class (c)
  rows are named against the ledger.
- **Neow through boss reward — NOT MET.** §10: twelve boss fights, zero claims.

And two class (a) divergences now block it under conventions §5:

1. **§8.0 — Stone Calendar / `atBattleStart` ordering in `run_advance.cpp`.**
   Two captures, one fight lost. Whoever owns it should move the
   `dispatch_relics_at_battle_start` call ahead of turn 1's `atTurnStart` without
   giving up the addToBot placement the surrounding comment correctly argues for
   (the two are separable: the *immediate* body and the *queued* step do not have
   to run at the same point), add a run-layer test that drives a real
   `enter_combat`, and check whether `combat_begin` needs the dispatch too.
2. **§8.1 — the emerald-key elite's `mapRng.random(0, 3)`.** Three seeds, both
   campaigns. Model the draw, and root-cause why the rolled buff does not reach
   the monsters before assuming it never can.

The class (b) items (§8.3, §8.4) and the class (c) items are reported, not fixed:
the replay tool and the driver are not this task's to edit, and the class (c)
rows are already carried by the ledger. Two ledger corrections are worth making
in whatever change lands next:

- the *"Gremlin move-99 escape"* row (B3.16) says the `EscapeAction` body is
  *"unreachable in Act 1"*. **A Looter escaping on floors 4, 7 and 8 is Act 1**,
  and this capture reaches it eight times. The row's scope note is wrong even
  though §8.3's divergence turned out not to belong to it.
- §8.0 and §8.1 both need rows of their own if they are not fixed immediately.
