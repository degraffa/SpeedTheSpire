# G6 oracle spot campaign runbook — 30 seeds, Neow through boss reward

The G6 gate's third leg: **"Oracle spot campaign: ≥ 20 full-run seeds, Neow
through boss reward, zero un-triaged diffs through the run-level differ."**

This document is both the procedure and the record of the capture that ran it.
It is written the same way as [`b45_reward_spotdiff.md`](b45_reward_spotdiff.md),
[`b47_treasure_spotdiff.md`](b47_treasure_spotdiff.md) and
[`b48_shop_spotdiff.md`](b48_shop_spotdiff.md): environment, preflight, capture,
validation, the frontier table, the per-mode read-outs, the known-benign shapes,
and what actually ran.

**Headline: the seed-count half of the leg is met (30 ≥ 20) and every diff is
triaged, but the leg does NOT pass.** Two things block it, and both are stated in
full below:

1. **One class (a) real engine divergence** — five skeleton card rows carry no
   `upgraded:` program, so an upgraded Strike deals 6 instead of 9 and an
   upgraded Defend blocks 5 instead of 8. Eleven of the thirty runs upgrade one
   of those cards. **STOP-THE-LINE; nothing here was fixed.**
2. **No run claimed a boss reward.** Three of thirty reached the Act-1 boss and
   all three died, so the "Neow **through boss reward**" span is captured only up
   to the boss *fight*, never through its *reward*.

---

## 1. Environment

Unchanged from B4.5 §1 and design §1.2 as amended at §11 v0.1.7. Slay the Spire
`12-18-2022` (`[V2.3.4]`), ModTheSpire `3.30.3`, BaseMod `5.56.0`,
`CommunicationMod-oracle 1.2.1-oracle.0`. The driver is `b1.4.6`; the deployed
fork jar's SHA-256 is
`7DC814AD240CBBD9100B2E8C92B6AA97B4ADFBED62FFED7961C6E5DE15884733`.

The replay tool is built from `master` at `09f8847` with
`tools/wsl_run.sh release` — green, and the `release` preset is the one gates
require (conventions §2). Every read-out below is the **release**
`replay_run_diff`.

## 2. Preflight — the B4.5 §2 gate, all four checks

A one-seed, `--max-actions 1` capture under a **new** id. Never reuse a
preflight id; preserve the directory either way.

```bat
C:\Python39\python.exe orchestrator.py ^
    --campaign-id g6_preflight_20260728T053354Z_claude01 ^
    --seeds STS00041 --policy greedy --policy-seed 1234 ^
    --max-actions 1 --fresh
```

| # | Check | Result |
|---|---|---|
| 1 | `mts_launch<N>.log` names the sanctioned stack, and no stock `CommunicationMod` | **PASS** — `Slay the Spire (12-18-2022)`, `ModTheSpire (3.30.3)`, `basemod (5.56.0)`, `CommunicationMod-oracle (1.2.1-oracle.0)`; zero stock-`CommunicationMod` lines in either launch log |
| 2 | Deployed fork jar SHA-256 == the pin | **PASS** — `7DC814AD…4733`, re-derived with `Get-FileHash` against `<game>\mods\CommunicationMod-oracle.jar` |
| 3 | `campaign_progress.json` is `complete`, never `fatal_environment_drift` | **PASS** — `complete`, driver `b1.4.6`, 1 seed done, 0 failed |
| 4 | `validate_artifacts.py --require-oracle --campaign` | **PASS** — `1 file(s), 0 error(s)`, exit 0 |

The artifact header carries `oracle_block_enabled: true`,
`version_source: mts_launch1.log`, and `crosscheck_ok: true` on the seed
identity. Two launches are expected here and are not a fault: the preflight run
hits its 1-action cap immediately, stops heart-beating, and the orchestrator
relaunches once before reading the completed ledger. The pilot preflight
behaved identically.

## 3. The seed list

Thirty seeds, recorded verbatim at
`D:/STS_BG_Mod/_oracle_data/campaigns/g6_campaign_seeds.txt`:

- **24** — the first thirty rows of
  `_oracle_data/seed_scan/seeds_match_and_keep_k2.txt` **minus** the six already
  captured live by the greedy pilot (STS00212 / 465 / 506 / 711 / 783 / 947).
- **6** — the first six rows of `_oracle_data/seed_scan/seeds_treasure_k2.txt`
  not already captured by the pilot (which took STS00192 and STS00337 from it):
  STS00353, STS00462, STS00509, STS00572, STS00577, STS00700.

No top-up from fresh sequential STS01000+ was needed.

> **Note for the next capture.** The brief that commissioned this run described
> `seeds_match_and_keep_k2.txt` as "the 30 seeds"; it actually holds **91**. The
> list above takes its first thirty rows in file order and then tops up from
> `seeds_treasure_k2.txt` exactly as the brief's fallback said to. Both files
> come from the same `scan_STS00100-STS05099` sweep, so the two sources are
> equally deep-proven; nothing rides on the mix. Re-derive the count from the
> file, never from a brief (conventions §8).

## 4. Capture

```bat
C:\Python39\python.exe orchestrator.py ^
    --campaign-id g6_campaign_20260728T053354Z_claude01 ^
    --seeds D:/STS_BG_Mod/_oracle_data/campaigns/g6_campaign_seeds.txt ^
    --policy greedy --policy-seed 1234 ^
    --max-actions 1500 --stall-timeout 150 --campaign-timeout 7200 --fresh
```

`status: complete`, **30 done, 0 failed**, 3 launches, 2 relaunch events, 0
induced kills. Wall clock 05:39:42Z → 05:48:26Z, **8m 44s** for 3,829 injected
actions.

| Statistic | Value |
|---|---|
| Outcomes | **30 / 30 `death`** — no `noop_wedge`, no `error_wedge`, no `action_cap` |
| Median floor | **7** (mean 8.3, max 16) |
| Floor histogram | 3:1 · 4:1 · 5:2 · 6:9 · 7:3 · 8:3 · 10:6 · 12:1 · 13:1 · **16:3** |
| Median actions | 111.5 |
| Seeds needing a second attempt | 2 (STS01372, STS00462) — both collateral of §9's spurious relaunch, both clean on the retry |

Greedy behaved as the pilot predicted in the two respects that matter for
triage: **no run took Neow's boss-relic swap** (so none of B4.5 §7's deferred
`onEquip` / `energyMaster` replay stops occur), and **no run claimed a sapphire
key**. It did **not** behave as predicted in one: **twenty runs entered a shop
room**, which is why `--shop` is reported in §7 rather than skipped.

## 5. Validation

```bat
C:\Python39\python.exe validate_artifacts.py --require-oracle ^
    --campaign D:/STS_BG_Mod/_oracle_data/campaigns/g6_campaign_20260728T053354Z_claude01
```

**`30 file(s), 0 error(s)`, exit 0.** Strict mode passes: oracle block on every
in-game action, both pity fields, complete `{counter,s0,s1}` triples on all five
streams, a failure-free progress/manifest ledger whose ordered `seed_list` and
`seeds_done` match exactly, a bijection to the run and timing artifacts, joined
seed identities, exactly one terminal per run with contiguous sequence numbers,
and every timing row parsed mark-for-action.

## 6. The frontier table — `replay_run_diff --replay`, every run

```bash
build/release/tools/oracle_bridge/replay/replay_run_diff --replay \
    /mnt/d/STS_BG_Mod/_oracle_data/campaigns/g6_campaign_20260728T053354Z_claude01/run_*[!g].jsonl
```

`--- 30 file(s), 28 not clean ---`. Classes are the brief's: **(a)** real engine
divergence · **(b)** harness/mapping gap · **(c)** named deferred body ·
**(d)** capture artifact.

| Seed | Rec | First divergence | Stop | Class |
|---|---|---|---|---|
| STS00220 | 87 | seq 61 f3 `CARD_REWARD` (1) `potions[0] NONE→ColorlessPotion` | seq 86 `SHOP_ROOM` unmodelled | **(c)** card-CHOOSE potion row |
| STS00221 | 105 | seq 25 f2 `EVENT` (1) `hp 55→75` | run terminal | **(d)** §8.1 Wheel-of-Change page offset; next persistent diff seq 53 is **(c)** Skill Potion |
| STS00283 | 43 | seq 7 f1 (1) `floor 1→0` | seq 42 `SHOP_ROOM` | **(b)** §8.2 Neow `ITEM_REWARD` exit |
| STS00353 | 27 | **none** | seq 26 `SHOP_ROOM` | **(b)** `SHOP_ROOM` unmodelled — zero-diff to the stop |
| STS00365 | 29 | seq 23 f1 `COMBAT_REWARD` (12) | seq 28 event desync | **(a)** §8.0 upgraded Strike |
| STS00425 | 83 | seq 38 f3 `CARD_REWARD` (1) ColorlessPotion | seq 82 `SHOP_ROOM` | **(c)** card-CHOOSE potion row |
| STS00451 | 71 | seq 30 f3 (1) `SneckoOil` | seq 70 event desync | **(c)** Snecko Oil row |
| STS00462 | 147 | seq 84 f7 (1) `gold 110→130` | run terminal | **(c)** stolen-gold in-combat ordering row |
| STS00463 | 110 | **none** | run terminal | **CLEAN** |
| STS00509 | 171 | seq 126 f10 `COMBAT_REWARD` (11) | run terminal | **(a)** §8.0 — campfire Smith upgraded a Strike at seq 66 |
| STS00572 | 161 | **none** | run terminal | **CLEAN** |
| STS00577 | 59 | **none** | seq 58 `SHOP_ROOM` | **(b)** zero-diff to the stop |
| STS00610 | 28 | **none** | seq 27 `SHOP_ROOM` | **(b)** zero-diff to the stop |
| STS00683 | 139 | seq 32 f2 `EVENT` (2) `master_deck_count 11→10` | seq 138 event desync (`RUN_OVER`) | **(b)** §8.3 Match-and-Keep board invariant |
| STS00700 | 115 | seq 7 f1 (1) `floor 1→0` | run terminal | **(b)** §8.2 Neow `ITEM_REWARD` exit |
| STS00856 | 41 | seq 25 f2 `MAP` (1) `hp 60→53` | seq 40 `SHOP_ROOM` | **(b)** §8.4 event-exit residual (sim parked in `EVENT_DIALOG`) |
| STS01068 | 21 | seq 13 f1 (1) `hp 66→54` | seq 20 event desync | **(a)** §8.0 upgraded Strike |
| STS01221 | — | — | **translation aborted** | **(c)** §8.5 `DuplicationPower` |
| STS01314 | 60 | seq 22 f2 `EVENT` (37) relic-pool rotation | seq 59 `SHOP_ROOM` | **(d)** §8.1 Wheel-of-Change page offset (re-converges at seq 23); persistent frontier seq 50 is **(c)** Distilled Chaos |
| STS01372 | 105 | seq 39 f3 `CARD_REWARD` (1) ColorlessPotion | seq 104 `SHOP_ROOM` | **(c)** card-CHOOSE potion row |
| STS01789 | 21 | **none** | seq 20 `SHOP_ROOM` | **(b)** zero-diff to the stop |
| STS01857 | 79 | seq 21 f2 (1) `DistilledChaos` | run terminal | **(c)** Distilled Chaos (registry-documented) |
| STS01861 | 71 | seq 31 f2 `CARD_REWARD` (1) `AttackPotion` | seq 70 `SHOP_ROOM` | **(c)** card-CHOOSE potion row |
| STS01906 | 21 | **none** | seq 20 `SHOP_ROOM` | **(b)** zero-diff to the stop |
| STS02002 | 72 | seq 52 f3 (1) `hp 55→54` | run terminal | **(a)-candidate** §8.6 — 1 HP, unresolved |
| STS02009 | 100 | seq 61 f4 `EVENT` (1) `hp 67→56` | seq 99 unsimulated grid | **(d)** §8.1 Wheel-of-Change page offset; the stop is **(b)** §8.7 |
| STS02041 | 82 | seq 28 f1 (1) `hp 27→24` | seq 81 `SHOP_ROOM` | **(a)** §8.0 upgraded **Defend** (exactly 8−5) |
| STS02042 | — | — | **translation aborted** | **(c)** §8.5 `Vigor` / Akabeko row |
| STS02048 | 34 | **none** | seq 33 `SHOP_ROOM` | **(b)** zero-diff to the stop |
| STS02110 | 67 | seq 32 f3 (1) `potions[1] NONE→DistilledChaos` | seq 66 `SHOP_ROOM` | **(c)** Distilled Chaos |

**Tally:** 2 CLEAN · 7 more zero-diff to an early harness stop · **4 class (a)**
(+1 candidate) · 9 class (b) stops/frontiers · 11 class (c) · 3 class (d).
Fifteen of the thirty stops are the same `SHOP_ROOM` message.

## 7. Per-mode read-outs (breadth)

All five modes over the same 30 artifacts. Each is a separate invocation; the
modes are mutually exclusive.

| Mode | Result | Verdict |
|---|---|---|
| `--neow` | `30 seed(s): 28 fully zero-diff, 0 clean through activation only, 2 diverged` | **PASS.** The only two "diverged" are §8.5's translation aborts. **28 / 28 readable seeds zero-diff** on options, activation and post-choice — including both `THREE_SMALL_POTIONS` blessings, whose engine body is therefore proved live even though §8.2's harness gap cannot walk off the screen |
| `--event` | `47 sighting(s), 46 zero-diff (+0 obtain race), 1 diverged; entry-page option count matched 47 of 47`; `constructor deals: 12 read out, 11 zero-diff; 75 screen positions compared, 60 attempt outcomes reproduced, 120 grid rounds walked` | **PASS but for §8.3.** The single DIFF is STS00683's Match and Keep, and it is a false invariant in the checker, not a deal divergence |
| `--treasure` | `12 treasure room(s), construction clean 12, 12 opened (12 clean) / 0 skipped, in-room walks clean 12 (+0 partial), 0 divergence(s)` | **PASS.** Six times the corpus B4.7 closed on (which held exactly two chests), and every one opened rather than skipped |
| `--shop` | `20 merchant(s) built (0 with a visible shelf), stock clean 20, purchase walks clean 20 (+0 partial), 0 divergence(s)` | **PASS.** Twenty merchants' `merchantRng` / `cardRng` / `potionRng` stock construction proved. **Zero shelves** — greedy walks straight through, so no purchase, no purge and no `Meal Ticket` path is exercised. The stock half is oracle-proved; the buy half is not |
| default (reward) | `99 reward screen(s), assembly clean 95, claim clean 96 (+0 library-order-only), 6 failing file(s)` | **PASS but for §8.8.** 4 of the 6 failures are the three `STOLEN_GOLD` assemblies plus STS01372's floor-7 gold row; 2 are the translation aborts |

Zero `library-order-only` records across every mode — B4.5 §6's card-pool
library order stays closed, on 99 reward screens.

## 8. Findings, by class

### 8.0 — CLASS (a), STOP-THE-LINE: five card rows have no upgraded program

**Five rows in `registry/cards.yaml` carry no `upgraded:` block**: `id 1 STRIKE`,
`id 2 DEFEND`, `id 3 BASH`, `id 4 SHRUG_IT_OFF`, `id 5 POMMEL_STRIKE`. (The other
fifteen rows without one are the STATUS and CURSE cards, which have no upgraded
form; **106 of the 126 rows do carry one**.)

`include/sts/engine/cards.hpp:162-206` documents the consequence as a deliberate
default — *"A card with no `upgraded:` block in the YAML gets an upgraded program
byte-identical to its base"* — so `card_effects(def, 1)`, `card_cost(def, 1)` and
`card_flags(def, 1)` all return the **base** program for those five. In the game:

| Card | Java | Base → upgraded |
|---|---|---|
| Strike | `Strike_Red.upgrade` (`Strike_Red.java:57-62`) `upgradeDamage(3)` | 6 → **9** |
| Defend | `Defend_Red.upgrade` (`Defend_Red.java:43-48`) `upgradeBlock(3)` | 5 → **8** |
| Bash | `Bash.upgrade` (`Bash.java:54-60`) `upgradeDamage(2)` + `upgradeMagicNumber(1)` | 8 dmg / Vuln 2 → **10 / 3** |
| Shrug It Off | `ShrugItOff.upgrade` (`ShrugItOff.java:43-48`) `upgradeBlock(3)` | 8 → **11** |
| Pommel Strike | `PommelStrike.upgrade` (`PommelStrike.java:44-52`) `upgradeDamage(1)` + `upgradeMagicNumber(1)` | 9 dmg / draw 1 → **10 / draw 2** |

**Why this is class (a) and not class (c):** nothing carries it. It is on **no**
row of the Deferred obligations table and on no task's `Inherited:` line, so it
is invisible during execution — exactly what conventions §5 forbids. And the
justification still sitting in `registry/cards.yaml:16-19` —

> `upgraded:` … *"Absent here: the skeleton deck is all base cards (stage-a
> design §9); codegen emits base rows only **until CardDef gains the upgrade
> dimension (a later task)**."*

— is **stale**: `CardDef` gained the upgrade dimension, and 106 rows use it. That
is precisely the conventions §8 trap *"a comment asserting 'X does not exist yet'
is a bug signal … check whether the prerequisite arrived."* It arrived; these
five rows were never revisited.

**Two arithmetic-exact live reproducers, from two different cards:**

- **STS02041, `Defend+`.** Neow "Upgrade a Card" → grid `choose 6` = a **Defend**.
  First divergence seq 28, one field: `hp: 27 → 24`. **Exactly 8 − 5 = 3**, the
  block the sim's Defend+ never gained.
- **STS01068, `Strike+`.** Neow "Upgrade a Card" → grid `choose 3` = a **Strike**.
  At seq 7 the two sides are identical: Louse #0 at **3 HP, 9 block,
  Vulnerable 2** (`--combat` prints no `monsters[0].block` or `.powers` row).
  The capture then plays `play 4 0` = Strike+ and the Louse **dies**:
  9 × 1.5 = 13, minus 9 block = 4 through, 3 HP → 0. The sim's Louse survives at
  3 (`monsters[0].hp: 0 → 3` from `combat seq=8`), which is what 6 × 1.5 = 9
  against 9 block looks like. Both sides played the same card — the seq-8
  `discard[1]` rows resolve to a `Strike_R` with `upgrade 1` on **both** sides.
  From there the sim's floor-1 fight never ends, and every later record is
  downstream.

**Blast radius in this campaign: 11 of 30 runs upgrade a Strike or a Defend.**
Four via Neow (STS00365, STS00856, STS01068 → Strike; STS02041 → Defend) and
eight via a campfire Smith (STS00353, STS00365, STS00462, STS00509 ×2,
STS01221 ×3, STS01314, STS01906, STS02048). **Every single upgrade taken in the
whole campaign, by either route, landed on a Strike or a Defend** — the two most
common cards in the deck, so this is not a rare path. Several of those runs show
no diff only because the harness stopped at a `SHOP_ROOM` before the upgraded
card was drawn.

Not fixed here, per the brief. Whoever owns it should author the five `upgraded:`
programs from the Java above, delete the stale `cards.yaml:16-19` comment in the
same change, and add the tier-2 rows.

### 8.1 — CLASS (d), known-benign: Wheel of Change resolves its prize one page early

The sim commits Wheel of Change's prize on the **`spin`** press; the game commits
it on the **`Prize!`** press one record later. Three sightings, three different
prize kinds, one shape:

| Seed | Page | Prize | Diff at the early record |
|---|---|---|---|
| STS00221 | seq 25 `Prize!` | +20 HP heal | `hp: 55 → 75` (sim already healed) |
| STS02009 | seq 61 `Prize?` | −11 HP | `hp: 67 → 56` (sim already paid) |
| STS01314 | seq 22 `Prize!` | Toy Ornithopter | 37 fields — the whole common relic pool rotated by one front-pop, `count 33 → 32` |

**It re-converges on the next record every time** — STS01314 is zero-diff again
from seq 23 through seq 49. The net post-event state is right; only the page at
which the body commits differs. Whoever owns the event layer should decide
deliberately whether the sim models the intermediate page; it is recorded here
rather than changed. `--event` reports all three sightings zero-diff, because
that mode compares **arrival**, which is before the prize.

### 8.2 — CLASS (b): the Neow three-potions screen has no exit in the command map

`NeowRewardType::THREE_SMALL_POTIONS` delivers through a `COMBAT_REWARD` screen
inside the NeowRoom. The engine models it fully (`src/engine/neow.cpp:273-292`,
`NeowScreen::ITEM_REWARD`, `include/sts/engine/neow.hpp:145`) and needs **two**
distinct presses to leave: `run_advance.cpp:1478-1489` closes `ITEM_REWARD` only
on `kChooseProceed` (→ `neow_finish_payout` → `DONE`), and `:1490-1494` is the
`DONE` press that sets `RunPhase::MAP_CHOICE`.

The command map supplies neither. `command_map.hpp:439-443` maps a
`COMBAT_REWARD` `proceed` to **`MapKind::NOOP`** under the lazy-leave convention
(*"leaving is deferred to the map choice"*) — correct for a real combat-reward
room, wrong here — and the following map `choose` becomes `LEAVE_ROOM`, whose
`CHOOSE(dst)` falls into `ITEM_REWARD`'s `else` branch and is executed as
`claim_reward(dst)`. The sim never leaves `NEOW`.

Evidence that the engine half is right: **seq 0-6 are zero-diff on both affected
runs**, and `--neow` reads both out fully zero-diff (§7). The single field at
seq 7 is `floor: 1 → 0`. The fix belongs where the EVENT branch already solved
the same problem at `command_map.hpp:351` — discriminate on the **sim's phase**,
not the screen label. Not mine to make; the replay tool is not this task's to
edit.

Affects STS00283, STS00700 — i.e. exactly the two runs whose Neow blessing was
"Obtain 3 random Potions", and no others.

> **RESOLVED on branch `replay-neow-exit`.** The screen-label reading was the
> defect, and the phase discriminator was the right shape but the wrong site:
> `command_map.hpp:351` is the EVENT branch, while the NOOP is in the
> `COMBAT_REWARD` branch, which now asks for `RunPhase::NEOW` +
> `NeowScreen::ITEM_REWARD` before it elides. The single captured `proceed`
> maps to **two** `kChooseProceed` CHOOSEs, because one game frame crossed both
> run-layer states (`ITEM_REWARD` → `DONE` → `MAP_CHOICE`) and neither consumes
> RNG; the capture confirms it, going COMBAT_REWARD `proceed` straight to a
> `MAP` with no [Leave] page between. STS00700 now reads **CLEAN** to its run
> terminal and STS00283 zero-diff to its `SHOP_ROOM` stop.

### 8.3 — CLASS (b): the Match and Keep board invariant is unsound

`--event` reports one DIFF in the whole corpus:

```
EVENT DIFF STS00683 floor=2 Match and Keep! — the sim's board holds 4 copies of
"Regret"; initializeCards deals every identity exactly twice (GremlinMatchGame.java:84-86)
```

The cited line proves each *slot* is duplicated (`retVal.addAll(retVal2)`). It
does **not** prove the six dealt identities are distinct — and they need not be.
`GremlinMatchGame.initializeCards` (`GremlinMatchGame.java:64-80`) calls
`AbstractDungeon.returnRandomCurse()` **twice** at `:70-71` on the
`ascensionLevel >= 15` branch, with no dedup, so two of the six identities can
collide and a legitimate board can hold four copies of one curse.

The read-out's own numbers say the sim's board is right: **5 screen positions
named by the capture compared, 5 attempt outcomes reproduced, 10 grid rounds
walked** — a wrong board could not reproduce five match/miss outcomes. The
invariant fires before the comparison is credited, so a correct board is scored
as a divergence. ~~This is also the cause of STS00683's `--replay` frontier
(`master_deck_count: 11 → 10`, the Double Tap the run kept).~~ **That last
sentence is wrong** — see the correction in §8.4 below; the board invariant
lives only in the `--event` deal read-out and `--replay` never consults it.

The other **eleven** constructor deals in this campaign are zero-diff.

> **RESOLVED on branch `replay-neow-exit`.** The invariant is now the multiset
> shape the Java actually guarantees: every position filled, every count EVEN
> (that is all `retVal.addAll(retVal2)` proves), each count 2 or 4, and at most
> ONE identity at 4 — because only the two adjacent `returnRandomCurse()` draws
> can collide, the other four slots coming from disjoint pools. Three copies,
> two quadruples and a board of one identity all still fail loud.
> STS00683's deal reads `DEAL OK` and `--event` on this campaign is
> **47 / 47 zero-diff, 12 / 12 deals zero-diff**.

### 8.4 — CLASS (b): one event-exit residual

STS00856 seq 25: the capture is on the `MAP` while `sim_phase=EVENT_DIALOG`, and
by seq 26 the sim is a floor behind (`floor: 3 → 2`, `event_flags: 2064 → 16`).
The event is Golden Wing, whose exit page the sim did not consume. This is the
same family as the event-exit mapping fix already recorded in the ledger; one
residual case survives it.

> **RESOLVED on branch `replay-neow-exit`, and it was TWO mapping gaps, neither
> of them the event-exit door itself.**
>
> **(i) An event's two index spaces.** `screen_state.options[]` lists every
> dialog button, DISABLED ones included, and the run layer's option ordinal is
> that same full-list position (a body publishes `count` buttons plus an
> `enabled[]` mask). A `choose N` command instead indexes `choice_list`, the
> ENABLED buttons only — which is exactly what each option's `choice_index`
> records. Golden Wing offers `[Pray, Locked(disabled), Leave]` with
> `choice_index [0, -, 1]`; greedy pressed `choose 1` = **Leave**, the
> untranslated 1 named the **locked** gold branch, the sim's own `enabled[]`
> mask refused it, and the sim stayed on the intro page — so the NEXT record's
> exit press was applied to the intro page's option 0, **Pray**, which is the
> `hp: 60 → 53` at seq 25. `ScreenInfo` now carries `option_choice_index` and
> the EVENT branch translates between the two spaces, failing loud when no
> enabled button carries the index.
>
> **(ii) Match and Keep's board is indexed by SCREEN POSITION.** The fork's
> `getOrderedCards()` offers the cards still on the board and still face down,
> sorted by screen position, so a `choose N` names the N-th smallest offered
> position; the run layer's option index is the BOARD SLOT. Same cards, two
> index spaces, related by `mk_board.hpp`'s `match_screen_position`. Passing N
> through picks an unrelated card and, worse, still DOES something: a refused
> flip does not decrement `attemptCount`, so the walk desynchronises and the
> event never ends. `map_command` now sorts the sim's own still-face-down slots
> by screen position and checks the answer against each `card<position>` label.
>
> This — not §8.3's board invariant — is what produced STS00683's `--replay`
> frontier: the sim lost the Double Tap it matched. STS00856 only reaches its
> floor-3 Match and Keep at all once (i) lands, which is why (ii) surfaced here.
>
> After both: **STS00856 is zero-diff to its `SHOP_ROOM` stop**, and STS00683
> walks to its **run terminal** (186 records, up from a stop at seq 138) with
> its first divergence moved to seq 79 floor 5 — one field, in-combat
> `gold: 128 → 148`, i.e. the §8.8 class (c) *"Stolen-gold clamp vs in-combat
> gold ordering"* row, +20 for one steal.

### 8.5 — CLASS (c): two runs abort translation on a power with no registry row

Both are fail-loud, both name a documented deferral, and both cost the **entire**
artifact as replay evidence:

- **STS02042**, record 5: `unknown power id "Vigor"`. Obligation row
  **"Akabeko (Vigor power row)"** (B3.24, `UNASSIGNED — "card-batch consumers"`,
  *"no S1 Ironclad card grants Vigor, so no card batch will pick the power row
  up"*). A **relic** granted it, which is the case that row did not anticipate.
- **STS01221**, record 105: `unknown power id "DuplicationPower"`.
  `registry/potions.yaml:289-297` (`DUPLICATION_POTION`) documents it as
  deliberately deferred — *"the blocker is the RECURSIVE-PLAY opcode … NOT a
  missing power row: registering a DuplicationPower row without that opcode would
  be an inert power."*

Worth naming as a harness consequence rather than only a content one: the sim's
own `potion_use_implemented` mask keeps a deferred potion off the legal-action
list, but nothing stops the **game** from drinking one, and the differ then
aborts the file rather than reporting a bounded stop. **6.7 % of this campaign is
unreadable for that reason.** Whether the differ should degrade gracefully is a
question for the tool's owner.

### 8.6 — CLASS (a)-candidate, UNRESOLVED: STS02002's one HP

STS02002, seq 52, floor 3, one field, `hp: 55 → 54`, persisting to the run
terminal. Its Neow blessing was "Transform a Card" and it takes no campfire
upgrade, so §8.0 does not explain it. One HP is too small to attribute
confidently and it was not root-caused. **Flagged for the engine owner rather
than filed under a class it may not belong to.**

### 8.7 — CLASS (b): a grid stop that names the wrong relic

STS02009 seq 99 stops with *"the capture opens a master-deck grid the sim never
opened (sim phase COMBAT): the most recently acquired relic is **Burning
Blood**, whose onEquip body is deferred."* Burning Blood is the **starting**
relic and its `onEquip` is not deferred; the message is
`unsimulated_grid_reason` (`command_map.hpp:238-249`) naming
`rc.run.relic_count`'s last entry when **no** relic was in fact just acquired.
The real stop is that the sim was desynced into `COMBAT` (downstream of §8.1's
seq-61 offset) and the capture opened a grid. The reason text is misleading in
exactly the way `b45c1_replay_triage.md` fixed the phase-ordinal text; it needs
the same treatment.

> **RESOLVED on branch `replay-neow-exit`.** The reason now separates the two
> causes it used to conflate. A relic's `onEquip` runs at ACQUISITION, so it
> can only be pending on a phase the run layer acquires relics on — never in
> `COMBAT` or `RUN_OVER` — and the deferral claim is made only for the five
> relics whose `onEquip` really is deferred whole (`relic_pickup_boss.cpp`:
> Pandora's Box, Tiny House, Astrolabe, Empty Cage, Calling Bell). That list is
> a list and not a lookup on purpose: a deferred override is an explicit empty
> body, so `relic_on_equip_fn` returns a real function pointer either way and
> cannot tell them apart. STS02009's stop now reads *"…(sim phase COMBAT): no
> relic onEquip can be pending in that phase, so the two sides are on different
> screens (read the `first divergence:` line, not this stop)"*, and a modelled
> relic on an acquisition phase is named and ruled out rather than blamed.

### 8.8 — CLASS (b): the reward mode cannot see a Looter's theft, and CLASS (c) for the in-combat half

Four reward-mode failures, two distinct shapes.

**Three `STOLEN_GOLD` assemblies — class (b).** STS00462 f7, STS00683 f5,
STS01314 f7 all read
`reward items: [STOLEN_GOLD,GOLD,…] -> [GOLD,…]` with the claim **exactly 60
gold short each time** (147→87, 163→103, 248→188). The engine models the row
fully — `combat_rewards.cpp:276-279` emits it and `run_advance.cpp:329-393`
computes it from `settle_stolen_gold`, which reads `looter_stolen_gold(ms)` off
the **`MonsterState`** (`run_advance.cpp:337`). The default reward mode seeds a
translated **`RunState`** only, and the theft accumulator is combat state, so the
mode structurally cannot reconstruct the row. Not an engine gap; a limit of what
that mode seeds. (B4.5 §3's *"treat a divergence on a Looter floor as a real
divergence"* still stands for `--replay`; it is the reward mode's seeding that is
at issue.)

> **RESOLVED on branch `replay-neow-exit`, as a NAMED SEEDED INPUT rather than a
> reconstruction.** The Looter's steal count lives in `MonsterState.pad0` and
> CommunicationMod publishes no such field, so the translated combat state is 0
> whatever the thief did — nothing in the artifact can rebuild the accumulator.
> But the capture's own reward row carries the amount, and this mode already
> takes its `RunState`, its `miscRng` and its room type from the capture, so the
> amount is now read from `screen_state.rewards[].gold` and passed as
> `assemble_combat_rewards`' `stolen_gold_return`. It is already deducted from
> the seeded purse, which is what that parameter's contract requires: the game
> deducts at steal time. **One number is seeded; the row's POSITION in the list,
> its effect on the ≥ 4 potion-suppression threshold, the rest of the assembly's
> stream draws and the whole claim stay proved** — and the claim is where the
> sim's gold has to land on the capture's to the unit. Every such screen is
> counted and printed separately (`ASSEMBLY OK … (STOLEN_GOLD n seeded from the
> capture …)`, and `assembly clean N (M with a capture-seeded STOLEN_GOLD row)`)
> so no read-out line implies the theft itself was reproduced. Default mode on
> this campaign now reports **assembly clean 98 (3 seeded), claim clean 99, 3
> failing files** — §8.5's two translation aborts and STS01372's unattributed
> floor-7 gold row, which is unchanged and still unattributed.

**STS00462's in-combat `gold: 110 → 130` — class (c).** The sim is 20 gold
*higher* mid-fight because it deliberately does not deduct at steal time:
`run_advance.cpp:312` records the theft *"by the reward layer, not a combat-time
hook"*. That is the obligation row **"Stolen-gold clamp vs in-combat gold
ordering"** (B3.11, `UNASSIGNED — B5.2 verification, or whoever models mid-combat
gold timing`) observed live.

**STS01372 f7 — unattributed.** `treasure_rng` +1 and
`reward items: [CARD] -> [GOLD,CARD]`: the sim rolled a gold row the game did not
offer, on another floor-7 Looter-family room. The claim itself was clean
(gold=108 both sides). Most likely the escaped-thief branch (`run_advance.cpp:326`
*"An ESCAPED thief's share is simply…"*, and the obligation row **"Gremlin
move-99 escape"**), but that was **not confirmed** and is recorded as such.

### 8.9 — CLASS (c), documented in the registry but not on the ledger

Every potion frontier in §6 is a potion whose body is deliberately deferred, and
the sim's refusal to spend it is by design: the card-CHOOSE potion obligation row
records that *"they are also **fail-loud**: `potion_use_implemented` keeps every
still-deferred potion off the legal-action mask … instead of letting a USE
silently burn the slot."* The capture drinks it, the sim keeps it, and the belt
differs from that record on.

| Potion | Runs | Where it is recorded |
|---|---|---|
| Colorless Potion | STS00220, STS00425, STS01372 | obligation row *"In-combat card-CHOOSE potion bodies: Elixir, Attack/Skill/Power/**Colorless Potion**, Gambler's Brew, Liquid Memories"* |
| Attack / Skill Potion | STS01861, STS00221 | same row |
| Snecko Oil | STS00451 | obligation row *"Snecko Oil cost-randomization potion body"* |
| **Distilled Chaos** | STS01857, STS02110, STS01314 | **`registry/potions.yaml:301-308` only** — *"recursive play (a later opcode…). Deferred native."* **It is on no Deferred-obligations row.** Per conventions §5 that makes it invisible during execution; it should be added |

## 9. A harness defect the capture exposed — spurious relaunch of a healthy game

Twice, the orchestrator killed and relaunched a game that was demonstrably fine:

```
05:42:13Z heartbeat stale/absent 151s (> 150); game still up -- killing + relaunching
05:44:46Z heartbeat stale/absent 151s (> 150); game still up -- killing + relaunching
```

**The game was mid-combat both times.** `mts_launch1.log` ends with a live Looter
fight — `publishPostDraw`, `publish on card use: Defend_R`, `publishPreMonsterTurn`
at 05:42:15.1 — then `Child process has died…`, i.e. the kill. And a well-formed,
**fresh** heartbeat existed within 7-34 s of each kill (read directly off disk:
`utc 05:41:39Z` before the 05:42:13Z kill, `utc 05:44:39Z` before the 05:44:46Z
kill).

The reported age is the tell. `orchestrator.py:502` is

```python
hb_age = (now - hb.get("t", now)) if hb else (now - launch_started)
```

and **151 s is exactly `now - launch_started`** for both launches (started
05:39:42.98Z and 05:42:15.51Z). A real `hb["t"]` cannot coincidentally equal
`launch_started` to the same rounding twice, so `hb` was falsy — `read_json`
(`:130-135`) swallows `OSError` and `JSONDecodeError` and returns `None`. The
guard on `:503-504` then adds nothing, because when `hb` is falsy the two
conditions are the *same expression*:

```python
if hb_age > args.stall_timeout and (now - launch_started) > args.stall_timeout:
```

**So a single unreadable heartbeat sample, any time after `stall_timeout`
seconds, is sufficient to kill a healthy game — no retry, no confirmation, no
mtime fallback.** The heartbeat is also written non-atomically
(`campaign_driver.py:735-742`, a truncating `open(..., "w")`), unlike
`campaign_progress.json`, which the same class writes through a `.tmp`.

Honest caveat: a 25-second, **329,615-read** sampling loop against that exact
file during the campaign returned **zero** failures, so a torn read is *not*
supported by the evidence and the trigger remains unexplained. The structural
weakness above holds regardless of what makes the read fail.

**Cost:** 2 of 30 seeds (STS01372, STS00462) lost their first attempt and were
re-run from `start`. No evidence was corrupted — both retries completed and the
campaign strict-validates. But the driver retries only **once**, so a third
occurrence on one seed would fail it and fail strict validation with it.
Suggested fix, for the driver's owner: write the heartbeat through
`tmp` + rename, and/or fall back to the file's mtime, and/or require N
consecutive stale samples.

**Unrelated, also observed:** a potion reward that overflows the belt
(`NOT ENOUGH POTION SLOTS`, 4 occurrences) leaves the game emitting no fresh
actionable state, and the driver's 90 s `--timeout` watchdog is what unsticks it.
Two such stalls cost ~90 s each and are the dominant per-run time cost. Benign —
the noop path recovers and the artifact is unaffected.

## 10. Boss fights and boss rewards

**Three of thirty runs reached the Act-1 boss. Zero claimed a boss reward. Zero
victories.** All 30 terminals are `death`.

| Seed | Boss | Boss HP at the player's death | Player |
|---|---|---|---|
| STS01221 | **Slime Boss** | **0 / 150 — killed** | 0 / 75 |
| STS01372 | Slime Boss | 88 / 150 (41 % dealt) | 0 / 75 |
| STS00353 | Hexaghost | 184 / 264 (30 % dealt) | 0 / 75 |

STS01221 is the near miss and it is instructive: greedy **killed the Slime Boss
outright** and then died to the split — Spike Slime (L) dead, but Spike Slime (M)
32/32, Spike Slime (M) 32/32 and Acid Slime (L) 69/69 still up. That is not a
damage shortfall; it is a policy with no plan for the split's burst. A policy leg
scoped at surviving the split (block/AoE weighting once `Slime Boss` HP crosses
its split threshold) is the cheapest route to a first boss-reward capture. The
other two are ordinary attrition losses.

**Consequence for the gate.** The leg's span reads "Neow **through boss
reward**". This campaign proves Neow → boss *fight* on 3 seeds and Neow →
death on all 30; it does **not** reach a boss reward. Whether that span needs a
follow-up leg is the orchestrator's call.

## 11. Known-benign shapes (for the next capture)

Everything in this list was seen here, is understood, and is **not** a new
finding:

- **`screen 'SHOP_ROOM' is not modelled by the run layer`** — 15 of 30 stops.
  Greedy *does* enter shops. The run layer has no shop-screen model, so `--replay`
  stops there; `--shop` reads the same rooms out cleanly (§7). Seven runs are
  zero-diff right up to this stop.
- **`event command 'choose' arrived while the sim is in COMBAT/RUN_OVER`** — the
  mapping's desync guard refusing to hand a `CHOOSE` to a live combat. Always
  **downstream**; read the `first divergence:` line, never the stop.
- **Wheel of Change's one-page prize offset** — §8.1, re-converges next record.
- **A deferred potion staying in the sim's belt** — §8.9, by design and
  fail-loud.
- **`cards_played_this_turn`, `monster_move_history`, `monster_attacks_queued`
  reading 0 on the capture side** in `--combat` — translator gaps, not
  divergences. `--combat` is a diagnosis mode, not the acceptance.
- **`card_pool` / `hand` / `draw` index permutations in `--combat`** — the two
  sides keep independent pool layouts; compare identities, not indices.
- **Zero `library-order-only` records** — B4.5 §6 stays closed across 99 reward
  screens. A deck mismatch whose count and upgrade agree but whose card id
  differs would be a **regression**.

## 12. What actually ran — 2026-07-28

| Artifact | Path |
|---|---|
| Preflight | `D:\STS_BG_Mod\_oracle_data\campaigns\g6_preflight_20260728T053354Z_claude01\` |
| Campaign | `D:\STS_BG_Mod\_oracle_data\campaigns\g6_campaign_20260728T053354Z_claude01\` |
| Seed list | `D:\STS_BG_Mod\_oracle_data\campaigns\g6_campaign_seeds.txt` |
| Orchestrator log | `D:\STS_BG_Mod\_oracle_data\campaigns\g6_campaign_20260728T053354Z_claude01.out` |

Neither directory may be reused, overwritten or retried in place; a new attempt
gets a new id (README, "Treat a campaign id as immutable evidence").

**Reproduce the read-outs** from the repo root, against the release build:

```bash
tools/wsl_run.sh release
C=/mnt/d/STS_BG_Mod/_oracle_data/campaigns/g6_campaign_20260728T053354Z_claude01
B=build/release/tools/oracle_bridge/replay/replay_run_diff
$B --replay   $C/run_*[!g].jsonl     # the frontier table, §6
$B --neow     $C/run_*[!g].jsonl     # §7
$B --event    $C/run_*[!g].jsonl
$B --treasure $C/run_*[!g].jsonl
$B --shop     $C/run_*[!g].jsonl
$B            $C/run_*[!g].jsonl     # default reward mode
```

## 13. Recording the result

**Do not tick the G6 oracle-spot-campaign leg on this capture.** The seed count
and the triage bar are met — 30 ≥ 20, and every diff and every stop in §6 is
classified with evidence — but §8.0 is a live class (a) divergence, and
conventions §5 makes a surviving divergence stop-the-line. Two things must land
first:

1. **§8.0** — author the five `upgraded:` programs, delete the stale
   `registry/cards.yaml:16-19` justification in the same change, land the tier-2
   rows, and re-run this campaign under a **new** id.
2. **A boss-reward capture** — §10. Either a policy leg that survives the Slime
   Boss split, or an explicit orchestrator decision that the leg's "through boss
   reward" span is satisfied some other way.

The class (b) items (§8.2, §8.3, §8.4, §8.7, §8.8, §9) are reported, not fixed:
the replay tool and the driver are not this task's to edit. The class (c) items
need no action beyond §8.9's request that **Distilled Chaos** be given a
Deferred-obligations row, since a registry comment alone is invisible during
execution.

> **Follow-up landed on branch `replay-neow-exit`.** The five replay-tool class
> (b) items — §8.2, §8.3, §8.4 (which turned out to be two distinct index-space
> gaps), §8.7 and §8.8's `STOLEN_GOLD` half — are fixed, each with its own
> RED-first unit test. **§9 is the driver's and is untouched.** §6 and §7 above
> are the read-out of that capture at `master` `09f8847` and are left as the
> record of it; the post-fix read-out of the same artifacts is: `--replay` 30
> files, **27** not clean (STS00700 joins STS00463/STS00572 as CLEAN, and
> STS00283/STS00856 are zero-diff to their `SHOP_ROOM` stops); `--event`
> **47/47** sightings and **12/12** deals zero-diff; default reward mode 3
> failing files instead of 6. `--neow`, `--treasure` and `--shop` are
> byte-identical, as are the 161-run `--event` / `--treasure` regression sweeps
> and the six-deal pilot sweep. **§8.0 is untouched and still blocks the leg.**
