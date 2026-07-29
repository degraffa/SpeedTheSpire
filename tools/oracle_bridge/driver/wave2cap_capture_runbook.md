# Wave-2 capture runbook — the bottle boundary and The Courier's restock

Campaign owner: Track C (captures), branch `wave2-capture` off `e15ebad`,
2026-07-28/29. Two deferred-obligation rows are the subject:

- **"Fork redeploy + a bottle-taking capture"** (deferred by wave-runlayer S3):
  the fork SOURCE emits `in_bottle_*` (PROTOCOL §3.13) and the translator maps
  it, but no deployed jar had ever emitted the field and no capture had ever
  taken a bottle — so the master-deck bottle bits had never been compared
  against the live game, and the bottle grid's REVERSE master-deck row order in
  the replay command map (`command_map.hpp`, `open_grid_session`) was a
  Java-derived claim (`getCardsOfType`'s `addToBottom` is `group.add(0, c)`,
  CardGroup.java:1052-1058 → :459-461) that only a live capture can prove.
- **"The Courier (shop relic)"**, the RESTOCK half: measure what a restock
  costs the seeded streams, and implement the recorded mask-refusal decision.

This runbook is the sibling of [b45_reward_spotdiff.md](b45_reward_spotdiff.md)
and deliberately does not restate its §1 environment decision or §2 preflight
gates; those are the same gates, unchanged. Read that file first.

## 1. Fork rebuild + redeploy — DONE 2026-07-28

`build_fork.ps1 -CheckDeterminism` from this worktree's vendored source (the
wave-C `convertCardToJson` change included):

| Artefact | Value |
|---|---|
| Built + deployed jar | `<game>\mods\CommunicationMod-oracle.jar` |
| SHA-256 (the NEW pin) | `ADDE86099E54EB4E1313BA32802DDFE4CD89FBCE20F369E9D57E2C36595BF886` |
| Determinism check | PASS (second full build byte-identical) |

The previous pin `7DC814AD…884733` (b45 §2 check 2) is the pre-`in_bottle_*`
build; if you see it deployed, the bottle boundary is NOT live. b45's §2
otherwise applies verbatim; the pin recorded there is superseded by this one
for every capture that needs the bottle fields.

Preflight (`wave2cap_preflight_20260728T224858Z_claude01`, seed STS00041,
`--max-actions 1`): launch log names Slay the Spire `12-18-2022`, ModTheSpire
`3.30.3`, basemod `5.56.0`, CommunicationMod-oracle `1.2.1-oracle.0`, no stock
CommunicationMod; strict `validate_artifacts.py --require-oracle --campaign`
passes; header `fork_jar_sha256` is the new pin. One cold-start relaunch
(heartbeat stale at 241s) before the run; the driver's environment gates held
on both launches.

## 2. Seed planning — `seed_scan` relic targeting

Neither target is reachable by walking `STS%05d` blindly: a Bottled relic must
be OFFERED on the policy's path, and The Courier must be ACQUIRED (its canSpawn
ANDs `!(getCurrRoom() instanceof ShopRoom)`, Courier.java:41-43, so it can
never be bought in the shop it modifies) and a merchant entered afterwards.
`seed_scan` gained per-relic observation for exactly this (commits `4541446`,
`99d7109`): offered / reward-offered / acquired / shop-while-owned, with
ANY-OF filter clauses.

**The affordability lesson (why reward-offered exists).** The first scan
selected 14 seeds on "any bottle OFFERED, ≥3 of 10 combos"; every one of their
bottles turned out to sit on a floor-3–5 merchant SHELF. A20 gold at that
depth is ~110–140 and an uncommon relic prices at ~265+ (250 base × A16 1.1 ×
jitter), and the game's shop choice list carries only AFFORDABLE rows — so a
shelf offer that early is one no policy, scripted or live, can accept. The
live round-A campaign over those seeds
(`wave2cap_bottle_20260728T224858Z_claude01`, 14 runs, strict-validated,
greedy) produced eleven shelf bottles and zero claimable ones — preserved as
the measurement that motivated the reward-offered split. Scan for
`--need-relic-reward-offered` when the acquisition must be free.

Scan artifacts (non-repo data root): `_oracle_data/planner/scan_bottles*.tsv`,
`scan_courier*.tsv`, seed lists beside them.

## 3. The bottle captures — SEVEN EVENTS, THE ROW'S QUESTION ANSWERED

Two strict-validated greedy campaigns carry seven bottle-taking events, every
one a treasure-chest claim whose mandatory 1-pick grid the policy drove
itself, with the run continuing into later combats afterwards:

| Campaign | Run | Bottle | Claimed at | Bottled card (game side) |
|---|---|---|---|---|
| `wave2cap_roundA2_20260728T224858Z_claude01` | STS00241 | Bottled Flame | floor 11 chest | Strike (deck idx 4 at run end) |
| same | STS04888 | Bottled Flame | floor 9 chest | — |
| same | STS04925 | Bottled Flame | floor 5 chest | Sword Boomerang |
| same | STS05143 | Bottled Flame | floor 5 chest | — |
| same | STS06578 | Bottled Flame | floor 9 chest | Strike (deck idx 2) |
| `wave2cap_roundA3_20260728T224858Z_claude01` | STS03244 | Bottled Lightning | floor 9 chest | Defend (deck idx 7) |
| same | STS03352 | Bottled Lightning | floor 9 chest | Defend (deck idx 8) |

**The boundary is live end-to-end.** The redeployed jar emits `in_bottle_flame`
/ `in_bottle_lightning` on the bottled master-deck instance in every
post-bottling dump, and `translate_cli` consumes the keys with **zero
unknown-field errors** on all bottle-carrying runs (the keys land on the
`deck` walk as `kMasterCardInBottleFlame` / `...Lightning`; combat-pile
occurrences are consumed and dropped, PROTOCOL §3.13). No Bottled Tornado was
reachable — its canSpawn needs a POWER in the master deck, which the greedy
policy rarely holds when a chest pops the uncommon pool; reported as searched,
not captured (the tornado bit rests on the unit-level mapping tests).

### 3a. THE GRID-ORDER VERDICT: **PROVEN, not refuted**

`replay_run_diff --replay` on STS04925 re-drove the whole run through the
bottling: the floor-5 chest claim opens the game's grid reading
`[Blood for Blood, Sword Boomerang, Bash, Strike, Strike+, Strike, Strike,
Strike]` — the deck's eight purgeable attacks in exactly REVERSE master-deck
order — and the capture's `choose 1` bottles **Sword Boomerang**, the deck's
second-from-last attack. The replay's descending `GridSession` snapshot maps
row 1 to that same master-deck index, and **91 records compared, every one
zero-diff**, master-deck `flags` included, through and past the bottling. An
ascending snapshot would have bottled an opening Strike and diverged on every
record from seq 59 on. The shape is promoted as
`ReplayCommandMap.ABottleGridSessionSnapshotsTheLegalIndicesDescending`
(tests/replay_command_map_test.cpp), which also pins the contrast (a smith
grid over the same deck stays ascending).

### 3b. What the replay could NOT reach, and why — the SHOP_ROOM gap

The other six bottle runs all walk through a merchant BEFORE their bottling
floor, and `--replay` currently stops at the first `SHOP_ROOM` screen
(`screen 'SHOP_ROOM' is not modelled by the run layer` — the command-map arm
Track H is adding concurrently). Every record up to each stop was zero-diff
(STS00241: 41 records, STS04888: 34, STS04925: 91, STS06578: 34). **When the
SHOP_ROOM arm lands, re-run `--replay` over the six** — the lightning runs
STS03244/STS03352 are the SKILL-grid reverse-order evidence waiting to be
diffed.

### 3c. Divergence found in passing — STS05143, stop-and-report

STS05143's replay diverges at seq 51 (floor-3 Cultist fight): RunState.hp 70
(game) vs 73 (sim) after an `end` — the sim under-took ~3 HP through a
5-block turn. Everything up to seq 50 is zero-diff at the RunState level. The
suspect site is seq 40-42: an **Elixir** use whose HAND_SELECT pick the
capture answers with `choose 0` + `proceed`; the capture then exhausts
{Defend, Ascender's Bane (ethereal, end of the same turn)} while the sim's
exhaust pile stays EMPTY (read through `--combat`, whose per-slot noise is
reconstruction numbering, but an empty-vs-2 exhaust count is not that noise).
Cascade: different hand next turn, different block, 3 HP through. Candidate
owners: the HAND_SELECT command-map arm (Track H's file) or the engine's
Elixir/ethereal path (Track E). NOT touched from this track; reproducer:
`(STS05143, --replay, first divergent field RunState.hp at seq 51; Elixir at
seq 40)`. **RESOLVED by `wave3-followup` — it was BOTH candidate owners,
stacked**: the HAND_SELECT `proceed` mapping was not the combat CONFIRM verb
(the optional screen never closed, so the sim froze mid-fight — the "empty
exhaust pile" was the frozen screen, not an ethereal miss), and, behind that,
Toy Ornithopter's in-combat heal was inline instead of the queued HealAction.
STS05143 and STS03352 both replay **CLEAN to their run terminals** now; the
full triage is the `wave3-followup` section of
[wavec_track2_replay_triage.md](wavec_track2_replay_triage.md).

## 4. The Courier captures — THIRTEEN PURCHASES, THE STREAM ACCOUNTING MEASURED

Three campaigns, all strict-validated and translating with zero unknown-field
errors:

1. **Round A** (part of `wave2cap_roundA2_…`): greedy over the fourteen
   `--need-shop-after-relic "The Courier"` scan qualifiers. Four live routes
   emerged — The Courier claimed from a reward row, a merchant entered
   afterwards: STS00427 (Courier fl7, shop fl14, 374 gold), STS02142 (fl9,
   shop fl11, 210), STS06944 (fl9, shop fl11, 249), STS08403 (fl3, shop fl5,
   130). Greedy never opens a merchant, which is exactly why the purchases
   needed scripts.
2. **`wave2cap_courier_reveal_20260728T224858Z_claude01`** — per-seed
   `--policy script` (gen_shop_script.py: the round-A command prefix verbatim,
   `choose shop`, trailing `state`s): dumps each shelf's choice list without
   buying, because a purchase script written blind cannot name the rows. All
   four shelves carry the Courier `x0.8` at init (purge 60 = 75 x 0.8 on the
   first shops; STS06944's floor-5 pre-Courier shop shows purge 75).
3. **`wave2cap_courier_buy_20260728T224858Z_claude01`** — the same prefixes
   with purchases by lowercased display name. Thirteen purchases, covering
   every restock kind.

### 4a. THE MEASURED TABLE (oracle stream counters, before → after each buy)

| Restock kind | Events | Measured, EVERY event | Java-derived model |
|---|---|---|---|
| colored card | 5 (Pommel Strike→Sever Soul 63, Thunderclap, Headbutt, Rupture→Fire Breathing 62, Flex→Ghostly Armor 62) | **cardRng +1, merchantRng +1** | rollRarity (cardRng.random(99), ShopRoom 9/37, blizz unmoved) + setPrice jitter; identity = MathUtils, costs the seeded streams NOTHING |
| colorless card | 2 (Swift Strike→Thinking Ahead 154, Mind Blast) | **cardRng +1, merchantRng +2** | merchantRng rare roll (0.3f) + cardRng identity + setPrice jitter — fully seeded |
| relic | 2 (Potion Belt→Bronze Scales 119 w/ common pool −1; Toolbox w/ rare pool −1) | **merchantRng +2, one pool end −1** | rollRelicTier + END-pop (no draw) + getNewPrice jitter |
| potion | 4 (Dexterity→Block Potion 41, Elixir, Weak Potion; Swift Potion→Gambler's Brew 62 at **potionRng +3**) | **merchantRng +1, potionRng +2 (+k)** | returnRandomPotion tier + identity (+k trap-14 rejections — k=1 observed live once) + getNewPrice jitter |

No other stream ever moved across a purchase, `cardBlizzRandomizer` never
moved, and no restock price carries the A16 x1.1 (setPrice/getNewPrice never
run applyDiscount — Java-derived; the observed prices are consistent).

### 4b. The sim cross-check — zero-diff

`replay_run_diff --shop` over the buy campaign (after teaching the walk the
protocol's name-form `choose` and the `state` no-op — this file's
`shop_choice_arg_to_index` + the elision):

    5 merchant(s) built, stock clean 5, purchase walks clean 5, 0 divergence(s)

That is: every Courier-discounted shelf id-for-id and price-for-price
(including STS06944's SECOND shop, merchantRng 16→32), and every
post-purchase `RunState` — gold, deck, relics, potions, purge cost, and all
five seeded stream triples through the restock draws — byte-equal to the
capture. The restocked colored slot never re-lists in these captures'
choice lists as a purchase (nothing bought it), so the mask-refusal deviation
is never exercised against the live game — exactly the property that makes it
a deviation the capture cannot refute: the sim refuses the one row whose
identity the game itself draws unseeded (`kShopRestockedUnknownCard`,
shop.hpp's Courier block; guard test
`CourierRestock.ColoredPurchaseSpendsOneCardRngOneMerchantRngAndRefusesTheSlot`).

## 5. Errors found / triage

1. **`powers.yaml` id 77 game_id was the display name, not the POWER_ID
   literal** — the first capture whose player carried No Block (STS03364, a
   Neow Panic Button) failed translation with `unknown power id
   "NoBlockPower"`. The row's own provenance already named the literal. Fixed
   (data only, id/hooks untouched) with the join pinned in
   `RegistryGen.GameIdTablesRoundTrip`; the capture re-translates OK.
   Commit `17bd6c9`.
2. **STS05143 combat divergence** — §3c above; stop-and-report to Track E /
   Track H (Elixir HAND_SELECT suspect site). NOT a bottle or Courier finding.
3. **`--replay` stops at every `SHOP_ROOM`** — the known command_map gap
   Track H is filling concurrently; six of seven bottle runs' replay windows
   are gated on it (§3b). Not touched from this track.

## 6. Stage-3 opportunistic items — declined, with reasons

- **Red Skull below-half entry capture**: NOT taken. The adjudication was
  already discharged from the shipped jar's `RedSkull$1` bytecode
  (redskull_capture_runbook.md §3-§4), which is stronger evidence than a
  sampled capture — and §5 of that page files the engine's queue-time /
  drain-time ordering defect as stop-the-line for the engine owner. A capture
  today would only re-observe the known unfixed defect.
- **Snecko Eye boss-swap capture**: NOT taken. It needs a Neow boss-swap
  route to a specific boss relic (a new planner facility: bossRelicPool[0]
  per seed) plus a policy that picks the swap — new scan surface and more
  game time against a row that "says not needed". Declined under the explicit
  budget permission; the search space actually tried is the one §2-§4
  document.

## 7. `wave2-integrate` union — the §3b window closed (2026-07-28)

The `SHOP_ROOM` arm (`wave2-harness` stage 2) and these captures met for the
first time on the `wave2-integrate` union (all four wave branches on master
`e15ebad`). `replay_run_diff --replay` over all seven bottle runs, offline,
`debug` build:

| Run | §3b window (pre-arm) | Union verdict | Attribution |
|---|---|---|---|
| STS04888 | zero-diff over 34, shop stop | **CLEAN to run terminal**, 159 records | — |
| STS03244 | shop stop | **CLEAN to run terminal**, 156 records | **the Bottled Lightning SKILL-grid descending-order proof, now live** — the whole run, bottling included, zero-diff |
| STS03352 | shop stop | 167 records, first div seq 143 (floor 11, `hp` 1 field) | **second reproducer of the §3c Elixir class**: `potion use 0` → HAND_SELECT `choose 1`/`proceed` (seq 142-144), sim under-takes ~5 HP, cascades — same shape as STS05143 seq 40-42. Zero-diff through its floor-9 bottling |
| STS04925 | CLEAN over 91 (§3a) | 159 records, first div seq 137 (floor 14, `gold` only) | the standing class-(c) mid-combat stolen-gold row (game purse drops −20/−40/−60 during the fight, sim settles at fold-back); reconverges after the combat, zero-diff to artifact end |
| STS06578 | zero-diff over 34, shop stop | 149 records to run terminal, first div seq 81 (floor 7, `gold` only) | same class-(c) row; reconverges, zero-diff to terminal |
| STS05143 | zero-diff over 50 (§3c) | 59 records, first div seq 51 — **the §3c divergence, byte-identical** | the known standing stop; not chased |
| STS00241 | zero-diff over 41, shop stop | 218 records to run terminal, **ONE divergent record** (seq 96, 8 fields) | **NEW, benign, race-class**: seq 95 is a Smoke Bomb use; the sim settles the escape synchronously (Burning Blood +6, end-of-combat settlement) while the capture's seq-96 dump catches the game mid-escape-animation still listing the fight; seq 97 on is zero-diff to terminal — the same mid-animation-dump family as the §"obtain race", a candidate for the same narrow classifier treatment. **CLASSIFIED by `wave3-followup`**: the record now reports RACE (`is_escape_settlement_fields` + the window gates) and STS00241 replays **CLEAN, 218 records, 1 escape-race** |

Every merchant in all seven runs walked zero-diff (the shops were the §3b
gate; none produced a divergence). The Courier `--shop` walk over the buy
campaign reproduces §4b on the union exactly: stock clean 5, purchase walks
clean 5, 0 divergences — which also exercises the union composition of this
file's two §4b fixes (`shop_choice_arg_to_index` + the `state` elision, now
living beside the shared resolvers in `command_map.hpp`).

**`wave3-followup` update**: the STS05143 and STS03352 rows above are
superseded — both replay **CLEAN to their run terminals** (113 / 248 records)
after the Elixir-class fixes (§3c resolution note; full before/after in the
`wave3-followup` section of
[wavec_track2_replay_triage.md](wavec_track2_replay_triage.md)). The other
five rows reproduce byte-identically.
