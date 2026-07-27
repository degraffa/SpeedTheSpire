# B4.8 oracle spot-diff runbook — the merchant

The B4.8 acceptance's oracle leg: **a shop floor zero-diff — stock, prices and
sale index**. The tier-2 leg (the sixteen-draw `merchantRng` sequence, the
price pipeline, the purge ramp, the shop-relic hooks and the CHOOSE flow) is
`shop_test` and runs in CI; this leg needs the live game, which is **launched
manually by a human operator** (`CLAUDE.md`, oracle-bridge section) — an agent
cannot start it. Everything up to the capture is prepared below; the capture
and the read-out are the remaining human steps.

This runbook is the sibling of
[b45_reward_spotdiff.md](b45_reward_spotdiff.md) and
[b414_neow_spotdiff.md](b414_neow_spotdiff.md), and deliberately does **not**
restate the first one's §1 environment decision or §2 preflight: those are the
same gates, unchanged, and duplicating them is how one of them goes stale. Do
§1 and §2 of that file first, with a **new campaign tag**, then come back here.

## 1. What is already proven without a live capture

Unusually for one of these legs, a large part of it is already discharged from
**recorded data**. `shop_test`'s `ShopCapture` reproduces a real A20 Ironclad
merchant — run `STS00008` of the b13 twenty-seed sweep, seed `1790050543758`,
floor 3 — id for id and price for price, from the pre-entry stream triples in
that capture's `oracle` block:

| Stream | Before | After | Why |
|---|---|---|---|
| `cardRng` | 9 | 21 | 5 x (rarity roll + type-filtered pool index) + 2 colourless |
| `merchantRng` | 0 | 16 | the sixteen-draw table in `shop.hpp` |
| `potionRng` | 3 | 10 | 3 tier rolls + 4 trap-14 rejection draws |

That already pins the whole build for one shop, and it is what fixes the base
price tables the decompiled tree cannot show on its own (see the note in
`shop.hpp`: `sts-classes.jar` carries no inner classes, so CFR emitted
`AbstractRelic.getPrice`'s switch with `$SwitchMap` indices and no constant
names). **It is not the acceptance leg**, for two reasons: it is a single shop
from a campaign taken for a different task, and it exercises no purchase. What
the live capture adds is breadth (several seeds, several floors), a shop
reached by the driver on purpose, and the post-purchase state.

Aim the capture at the things that vector could not cover.

## 2. Capture (operator, Windows host)

A shop is not on floor 1, so unlike the Neow leg this needs a policy that walks
until it finds one. Put **five or more** base-35 seed strings, one per line, in
`D:/STS_BG_Mod/_oracle_data/campaigns/b48_seeds.txt`. Prefer seeds whose Act-1
map has a shop in the first half of the act (a `$` node on rows 1-6) so the run
reaches it before combat variance can end it; the B4.5 exclusions on elites and
chests do not apply here, but a run that dies before the shop is a wasted seed.

Derive a never-before-used id from the successful preflight tag of
`b45_reward_spotdiff.md` §2:

```bat
set B48_SHOP_ID=b48_shop_oracle_%B45_TAG%

C:\Python39\python.exe orchestrator.py ^
    --campaign-id %B48_SHOP_ID% ^
    --seeds D:/STS_BG_Mod/_oracle_data/campaigns/b48_seeds.txt ^
    --policy random-legal ^
    --fresh
```

`random-legal` buys whatever it can afford and eventually presses Proceed, so
each run's JSONL carries the shop screen at entry, one or more purchases, and
the state after each. The same id / `--fresh` / symlink rules as B4.5 apply
verbatim.

Then require the oracle gate before translating:

```bat
C:\Python39\python.exe validate_artifacts.py --require-oracle ^
    --campaign D:/STS_BG_Mod/_oracle_data/campaigns/%B48_SHOP_ID%
```

## 3. Translate

```bash
tools/wsl_run.sh debug          # builds translate_cli among everything else
build/debug/tools/oracle_bridge/translator/translate_cli \
    /mnt/d/STS_BG_Mod/_oracle_data/campaigns/$B48_SHOP_ID/run_*.jsonl
```

Expected: `OK` per file, **zero unknown-field errors**. The shop arrives as
`screen_type: SHOP_SCREEN`; B4.8 content-validated that slice (potion ids
joined through the registry, every `price` and `purge_cost` type-checked,
`purge_available` required boolean), so a failure here is real drift — a new
key, a renamed potion, a price that stopped being an integer — not the known
gap. The slice is still deliberately **storage-less**: translation outputs
`RunState`/`CombatState`, and a merchant is derived state the game itself
rebuilds from `(seed, merchantRng.counter)`.

## 4. The read-out — what must match

### 4a. THE STOCK, at shop entry

Reproduce the sim side from the capture's own pre-entry record — the last
record before the `choose <node>` that entered the shop. Seed a `RunState` from
its `oracle.streams` triples for `cardRng`, `merchantRng` and `potionRng`, its
`oracle.relicPools` three lists (common / uncommon / shop), its
`cardBlizzRandomizer`, its relics and its floor, then call `generate_shop`.
`ShopCapture` in `tests/shop_test.cpp` is a worked example of exactly that
setup and can be copied row for row.

| Field | Sim source |
|---|---|
| `screen_state.cards[0..4]` id + price | `shop.colored[i]` — ATTACK, ATTACK, SKILL, SKILL, POWER in that order |
| `screen_state.cards[5..6]` id + price | `shop.colorless[i]` — UNCOMMON then RARE |
| the SALE card | the one colored slot whose price is about half its siblings' — must be `shop.sale_index` |
| `screen_state.relics[0..2]` id + price | `shop.relics[i]`; slot 2 is always SHOP-tier |
| `screen_state.potions[0..2]` id + price | `shop.potions[i]` |
| `purge_cost` | `shop.actual_purge_cost` |
| `merchantRng` `{counter, s0, s1}` | +16 from the pre-entry state, raw state included |
| `cardRng`, `potionRng` | +12-or-more (a dedupe re-roll costs an extra draw) and +3-or-more from the pre-entry state |
| `cardBlizzRandomizer` | **unchanged** — see the traps below |

### 4b. THE POST-PURCHASE STATE

Take the record after each purchase and diff its translated `RunState` against
the sim stepped through the same `CHOOSE`:

| Field | Why it moves |
|---|---|
| `gold` | the price, through `lose_gold(..., in_shop=true)` |
| `master_deck[]` | a bought card is APPENDED (`CardGroup.addToTop` is `group.add(c)`) |
| `relics[]` | a bought relic, in acquisition order, with its `onEquip` applied |
| `potions[]` | a bought potion in the first free slot |
| `relics[].counter` for Maw Bank | **-2 after the first coin spent in any shop** |
| `purge_cost` | +25 per removal, and it persists into the NEXT shop |
| `miscRng` | only if the bought relic's `onEquip` draws (War Paint / Whetstone) |

Drive the sim side by hand for now (`run_begin`, then the `CHOOSE` sequence the
artifact's `action_command` records), exactly as B4.5 §5 describes — the
generalized replay adapter is still the undischarged B1.6 obligation.
`diff_run_states` (`tools/diff_harness`, `sts/diff/differ.hpp`) prints the
field-by-field diff.

### The traps the read-out exists to catch

1. **The purge cost is a STATIC in the game.** `ShopScreen.purgeCost` survives
   between shops and is only reset at the dungeon reset that precedes a new run
   (`CardCrawlGame.java:478` -> `ShopScreen.resetPurgeCost`). A second shop in
   the same run that offers removal at 75 again is a divergence; so is a *first*
   shop that offers it at anything but 75.
2. **Ascension never moves the purge cost.** The A16 `x1.1` is applied with
   `affectPurge=false` (`ShopScreen.java:227-229`), so an A20 shop's stock is
   10 % dearer and its removal service is not. A capture showing 82 or 83 would
   mean the flag was read wrong.
3. **The shop's rarity table is the ShopRoom's, and alternation is OFF.**
   `ShopRoom.getCardRarity` forwards with `useAlternation=false` over
   `baseRareCardChance = 9` / `baseUncommonCardChance = 37`
   (`ShopRoom.java:35-36`, `:52-55`), so no relic can bend a shop card's rarity
   the way one bends a combat reward's — and `cardBlizzRandomizer` is READ but
   never written. A capture where it moves across a shop contradicts the model.
4. **Four relics can never be stocked.** Maw Bank, Smiling Mask, The Courier
   and Old Coin all AND their floor gate with
   `!(getCurrRoom() instanceof ShopRoom)`, and the merchant is built after
   `setCurrMapNode`. Seeing one of them on a shop shelf is a divergence, and an
   RNG-visible one: the closed gate makes the end-pop discard that id and pop
   another.
5. **The third relic slot rolls no tier.** It is always
   `RelicTier.SHOP` (`ShopScreen.java:365`), which is why a fresh shop is 16
   `merchantRng` draws and not 17. A counter of 17 means slot 2 rolled a tier.

### The known-benign mismatches

- **The RED reward pools** (`kIroncladCommonPool` / `...UncommonPool` /
  `...RarePool`) are still emitted in registry-id order — the interim
  library-order deviation documented in B4.5 §6. It does **not** reach the shop:
  the five colored slots go through `CardGroup.getRandomCard(type, useRng)`,
  which **sorts** its type-filtered view by `cardID` (CardGroup.java:539-552),
  so `kIronclad{Common,Uncommon,Rare}{Attack,Skill,Power}Pool` are order-exact
  and **a shop card-id mismatch IS a divergence**. Same for the two colourless
  slots, whose pools are sorted by the same mechanism.
- **The Courier's restock is not implemented.** If a captured run owns The
  Courier, its prices will still match (the `x0.8` discount is live) but the
  slot it bought from will be EMPTY in the sim and RESTOCKED in the capture.
  That is the deferred obligation, not a bug — and the reason it is deferred is
  in the read-out's favour: the replacement card is drawn with `useRng=false`,
  i.e. off libGDX's unseeded `MathUtils` global rather than `cardRng`
  (`ShopScreen.java:615-617`), so that identity has no reproducible answer at
  all. **A Courier capture is still worth taking** — it is the only way to see
  how many draws, if any, the restock costs the seeded streams.

## 5. Recording the result

Zero-diff on >= 3 seeds discharges the acceptance: tick B4.8's checkbox, drop
the "code landed; blocked on the manual oracle capture" header from its ledger
block, and record the campaign id plus the seed list in its Log. Anything else
is a divergence: follow conventions §5 — re-read the cited Java first, audit
the fork's strip patches second, promote the reproducer to a regression
fixture, and do not tick the box.

## 6. What actually ran — 2026-07-27

**No `b48_shop_oracle_*` campaign was launched, and §2's seed-picking advice was
never needed.** `b47_treasure_oracle_20260727T204809Z_claude01`, taken for
B4.7, walks thirty runs deep into Act 1 under the same `random-legal` policy,
and three of them reached a merchant: **STS00054, STS00057 and STS00074** — the
three seeds §5's bar asks for, without a new capture. All three are
strict-validated and translate `OK`.

Those three runs hold **five merchants**, because two of them enter two shop
rooms:

| Run | Floor | What the capture shows |
|---|---|---|
| STS00054 | 2 | entered and left without opening the screen — streams and pools only |
| STS00054 | 7 | full shelf; buys Havoc (59) and an Explosive Potion (56) |
| STS00057 | 5 | full shelf; **purges a card for 75**, ramping the run cost to 100 |
| STS00074 | 3 | full shelf; buys a Skill Potion (57) and Havoc (54) |
| STS00074 | 5 | full shelf; broke at 17 gold, buys nothing |

**The read-out is a committed mode.** §4 says to drive the sim side by hand; it
is now `replay_run_diff --shop`, which does §4a and §4b per visit:

```bash
build/debug/tools/oracle_bridge/replay/replay_run_diff --shop \
    /mnt/d/STS_BG_Mod/_oracle_data/campaigns/<campaign>/run_*.jsonl
```

Two details of the mechanism are worth recording, because §4 leaves them open.

- **A shop `choose i` indexes the game's `choice_list`, not a slot.** The list
  is the AFFORDABLE, unsold rows by lowercased display name — purge first if it
  can be paid for, then cards, relics, potions — so it renumbers after every
  purchase. The harness resolves the command by joining that name back to the
  captured shelf rather than re-deriving the filter, which keeps the read-out
  measuring the merchant instead of measuring an affordability model.
- **The sale slot is inferred from the capture alone.** The screen carries no
  sale flag, so the mode takes the colored slot with the smallest
  price/base-price ratio (base from the row's own `rarity`) and requires both
  that it be `shop.sale_index` and that the ratio really be a halving. Price
  equality across all seven cards would already imply it; this makes the check
  independent of the simulator's own answer.

**Result: all five merchants zero-diff.** Every card id, relic id, potion id
and price on the four visible shelves; every purge cost and `purge_available`;
`merchantRng` +16 exactly on all five, `cardRng` +12-or-more and `potionRng`
+3-or-more, against the first in-room record, with all five relic-pool orders
and `cardBlizzRandomizer` compared alongside. Then the whole `RunState` after every
purchase. Three visits walk end to end clean; two stop, **after every purchase
was verified**, at an out-of-combat potion discard the run layer has no verb for.

**On the traps.** Trap 1 and trap 2 are both confirmed: the first shop of each
run offered removal at exactly 75, STS00057's purge ramped the run-persistent
cost to 100, and no A20 shelf moved the purge cost off those values while every
stock price carried the x1.1. Trap 5 held on all five (16 draws, never 17), and
trap 3's `cardBlizzRandomizer` never moved across a build. Trap 4 was not
exercised — none of the five popped one of the four shop-gated relics — so it
stays on the tier-2 test (`ShopDrawOrder.ShopRelicDrawsSeeTheInShopCanSpawnGate`).

**On the known-benign mismatches.** No card-id mismatch occurred anywhere, as
§4's first bullet predicts. The Courier was not owned by any of the three runs,
so the restock question is untouched and its deferred row stands unchanged.

**Frozen in CI.** STS00074's floor-3 merchant, including both purchases, is now
`ShopCapture.B47Seed1790050543999Floor3MatchesTheRecordedMerchantAndItsPurchases`
in `tests/shop_test.cpp` — the §1 vector's sibling, and the first one that
exercises spending.
