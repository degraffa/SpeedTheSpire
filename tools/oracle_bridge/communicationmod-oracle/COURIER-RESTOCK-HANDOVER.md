# The Courier restock seed patch — hand-over to S3.21

**Written by S3.24 (2026-09-03). Read by S3.21, which owns the one S3 fork
redeploy.** S3.24 landed the patch *source* and proved it compiles and matches
its seam; it deliberately did **not** build a deployed jar, touch the game
install, or write the `PROTOCOL.md` section — all three belong to S3.21's single
redeploy (`docs/s3-tasks.md`, S3.21 deliverable (d) and (e)).

## What lands in the jar

| | |
|---|---|
| New class | `src/main/java/communicationmod/patches/CourierRestockSeedPatch.java` |
| Patched seam | `ShopScreen.purchaseCard(AbstractCard)` — `@SpireInstrumentPatch`, an `ExprEditor` over `MethodCall` |
| Replaced calls | the **two** `AbstractDungeon.getCardFromPool` calls at `ShopScreen.java:615-617` (the restock draw and its dead `while (c.color == COLORLESS)` re-roll guard) |
| New config flag | `oracleCourierRestockSeed`, default **`true`**, in the same `SpireConfig("CommunicationMod","config")` store as `oraclePlaytimePin`; also a mod-settings toggle |
| Untouched | `getColorlessCardFromPool` (already seeded on `cardRng`), `setPrice`, `rollRarity`, and every `getCardFromPool` caller outside `purchaseCard` |

## Before / after

**Before.** With The Courier owned, buying a colored card replaces it in place:

```java
AbstractCard c = AbstractDungeon.getCardFromPool(
        AbstractDungeon.rollRarity(), hoveredCard.type, false).makeCopy();
//                                                      ^^^^^ ShopScreen.java:615
```

`rollRarity()` is one seeded `cardRng.random(99)`. `useRng = false` sends
`CardGroup.getRandomCard(CardType, boolean)` (`CardGroup.java:540-553`) down its
`MathUtils.random(tmp.size() - 1)` branch — libGDX's **global** RandomXS128,
seeded from JVM-startup entropy, whose position also advances with rendering and
VFX draws. The restocked card's identity is therefore not a function of
`(seed, actions)` **in principle**. It was the last such value in scope. The
simulator's answer since S1 was a named refusal: the slot restocked as an
unnameable sentinel that was kept off the legal-action mask and `shop_buy_card`
refused byte-stably.

**After.** The instrument patch replaces that call — and only that call — with
`CourierRestockSeedPatch.restockCardFromPool(rarity, type, useRng)`, which
reproduces `getCardFromPool`'s pool walk exactly (`AbstractDungeon.java:1538-1576`,
including the switch's deliberate downward fallthroughs and the POWER type's
upward recursion) and takes the one index draw from a **dedicated seeded
stream** instead of `MathUtils.random`. The restocked slot now carries a real,
reproducible `CardId`, the simulator draws the same one, and the buy-refusal is
lifted on both sides.

With the flag **off** the helper defers straight to
`AbstractDungeon.getCardFromPool(rarity, type, useRng)` — retail bit for bit, so
`oracleCourierRestockSeed` joins `oraclePlaytimePin` and the three strip flags in
the set that must be off to reproduce the stock-equivalence baseline.

## The stream seeding — the contract both sides implement

```
new Random(Settings.seed + 1000003L + AbstractDungeon.cardRng.counter)
```

then **one** `random(size - 1)` on it, over the type-filtered,
`Collections.sort`ed rarity view.

The simulator's identical construction is `courier_restock_stream(run_seed,
card_rng.counter)` in `include/sts/engine/shop.hpp`
(`kCourierRestockSeedOffset = 1000003`), called from `src/engine/shop.cpp`'s
colored restock branch, which then walks the pool with the shared
`shop_card_from_pool`.

Four properties, each load-bearing:

1. **The counter is read AFTER the restock's own `rollRarity()` draw.** Java
   evaluates the argument before the invocation, so by the time the helper runs
   `cardRng.counter` is already bumped — which is exactly where the simulator
   reads `rs.card_rng.counter`.
2. **Successive restocks never repeat.** Every colored restock spends one
   `rollRarity` draw on `cardRng` first, so the counter strictly increases
   between them. This matters more after S3.24 than before it, because a
   restocked slot is now buyable and a slot can restock repeatedly in one visit.
3. **No stored stream moves.** The restock stream is constructed at the draw and
   discarded. It is not a game stream, never enters a save file or
   `oracle.streams`, and adds no `RunState` byte — so `SCHEMA_VERSION` does not
   move, and `cardRng` / `merchantRng` / `potionRng` advance per restock exactly
   as the wave2cap_courier_* campaign measured them (colored = cardRng +1 /
   merchantRng +1).
4. **The offset cannot alias another derived stream.** The game's derived
   streams sit at `seed + floorNum` (0..~60) and `seed + act offset`
   ({1, 200, 600, 1200}); 1000003 is past both for any counter a run reaches,
   and `RandomXS128`'s constructor murmur-scrambles the seed, so adjacent
   counters give uncorrelated streams. **The offset is FROZEN**: changing it
   changes every restocked identity, and it must change on both sides in one
   commit or never.

The draw is **distributionally exact** against retail: same view, same inclusive
`random(size - 1)` bound, same "an empty view returns null before it indexes
anything, and therefore costs no draw" rule (`CardGroup.java:545-547`) — only the
source of the index differs.

## Evidence S3.24 produced

- **The jar builds** from the patched source under JDK 8:
  `powershell -ExecutionPolicy Bypass -File tools\oracle_bridge\build_fork.ps1 -NoDeploy`
  → `build\oracle_fork\CommunicationMod-oracle.jar`, containing
  `communicationmod/patches/CourierRestockSeedPatch.class`. **Not deployed and
  not committed** — S3.21 owns the redeploy and the recorded SHA-256.
- **The instrument patch matches its seam**, verified offline the way
  `OraclePlaytimePinPatch` was (PROTOCOL.md §5.4's closing paragraph): running
  ModTheSpire's own `InstrumentPatchInfo.doPatch` sequence
  (`Method.invoke(null)` → `(ExprEditor)` → `CtBehavior.instrument`) with this
  patch's `Instrument()` against the real `ShopScreen` bytecode from
  `desktop-1.0.jar` gives **2** `getCardFromPool` calls in `purchaseCard` before,
  `instrumentedCalls == 2`, **0** `getCardFromPool` and **2**
  `restockCardFromPool` calls after, `getColorlessCardFromPool` still **1** and
  `setPrice` still **2**, and the patched class still recompiles to bytecode
  (38356 bytes). The editor also logs one line per replacement, so a
  game-version drift that silently matched nothing shows up in
  `mts_launch<N>.log` rather than only in a divergent capture.

## What S3.21 must do

1. **Carry this patch into the single redeploy** and record the new jar's
   SHA-256 with the rest of S3.21's changes; the flag needs no orchestrator
   config change (an older `config.properties` falls through to the `Properties`
   default, i.e. ON).
2. **Add `PROTOCOL.md` §5.5, "The Courier restock seed (fork behavior change,
   S3.24)"** — after §5.4, in the same mould: the retail quote, why the value is
   outside `(seed, actions)`, the patched seam and its blast radius, the exact
   seed formula above with the simulator's matching call named, the flag and its
   place in the equivalence baseline, and the offline verification result quoted
   in the section above. `shop.hpp` and `CourierRestockSeedPatch`'s class
   javadoc both already point at "PROTOCOL.md 5.5" by name.
3. **Note the one behavioural consequence for the differ:** a Courier shop's
   restocked colored row now has an identity the sim can name, so
   `replay_run_diff --shop`'s stock/purchase walk compares it like any other row
   instead of skipping it, and the row is on the legal-action mask (a
   `--replay` script may now buy it).

## What is still owed after the redeploy

The **restock capture** that witnesses the draw is **S3.62's**, and until it
lands the S3.24 row is `UNVERIFIED-until-captured`. What it needs: a seed whose
shop the driver can actually make restock — i.e. a run that owns **The Courier**
(a SHOP-tier relic, so a shop that stocks it, or a Neow/boss path that grants
it) and then reaches a shop with enough gold to buy a **colored** card twice.
Find one with `seed_scan` filtered for a shop floor plus the relic, then script
the purchase through `driver/script_policy_cmd.py` the way the
wave2cap_courier_* runbook (`../driver/wave2cap_capture_runbook.md` §4) already
scripts Courier purchases — buying **by name**, because a restock renumbers the
choice list under a script written in advance. The acceptance is the capture
replaying zero-diff through `replay_run_diff --replay` **and** `--shop`, with the
restocked row's id matching.
