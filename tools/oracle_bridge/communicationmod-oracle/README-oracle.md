# CommunicationMod-oracle (the SpeedTheSpire fork)

This directory vendors upstream `ForgottenArbiter/CommunicationMod` v1.2.1
(commit `70ca84b1e8daff3eb4fe7f66775ce39926133c7f`, MIT — see `LICENSE`),
forked per stage-b-design §2.4. Upstream's own docs are in `README.md`
(untouched, for drift auditing). Fork policy: pinned at v1.2.1, never tracks
later upstream (the game is frozen at `12-18-2022`, design §1.2).

**The fork's `ModTheSpire.json` deliberately does not match upstream's.**
Upstream v1.2.1 declares `sts_version 11-30-2020` / `mts_version 3.18.1`; the
fork declares the stack it is actually sanctioned against, `12-18-2022` /
`3.30.3` (design §1.2, amended at B4.5 — §11 v0.1.7). Those two fields have
**different** ModTheSpire semantics and only one can refuse a launch:
`mts_version` is a hard *minimum* enforced by `Patcher` (a modal dialog, i.e. a
hang under `--skip-launcher`, if the installed MTS is older), while
`sts_version` is a mod-select-GUI warning that `--skip-launcher` never reaches.
Full read-out in `../PROTOCOL.md` §0.1 — check it before editing either field.

## Fork identity

- modid `CommunicationMod-oracle` (in `src/main/resources/ModTheSpire.json`,
  now concrete values — the upstream `${...}` maven placeholders died with the
  maven build). Distinct from upstream's `CommunicationMod` so the two mods
  can never be confused in a ModTheSpire `--mods` list.
- Config is still read from
  `%LOCALAPPDATA%\ModTheSpire\CommunicationMod\config.properties` — the
  `SpireConfig("CommunicationMod", …)` name in code is untouched (B1.1 ships
  **zero behavioral patches**; the B0.2 driver wiring works as-is).

## Building (Windows host; excluded from WSL CI)

No Maven. `../build_fork.ps1` drives JDK 8 `javac` + a deterministic zip
writer directly:

```powershell
powershell -ExecutionPolicy Bypass -File ..\build_fork.ps1 -CheckDeterminism
```

- **Inputs:** `desktop-1.0.jar` (game install), `ModTheSpire.jar` (workshop
  1605060445), `BaseMod.jar` (workshop 1605833019), stock
  `CommunicationMod.jar` (workshop 2131373661), JDK 8 at
  `C:\Program Files\Java\jdk1.8.0_171`. All overridable via parameters.
- **gson:** the upstream pom shades gson 2.8.5 to `com.autoplay.gson`
  (maven-shade relocation). The script reproduces this by rewriting
  `com.google.gson` → `com.autoplay.gson` in staged sources (the vendored
  sources stay untouched) and packaging the already-relocated gson classes
  **extracted from the stock workshop jar** — byte-identical gson bytecode to
  the jar that produced the B0.2 baseline, and no new dependency download.
- **Determinism:** entries ordinal-sorted, timestamps pinned to a fixed epoch
  (`build_fork.ps1:45`, arbitrary — it is a reproducibility constant, **not** a
  game-version claim, and is deliberately left alone when the runtime pin
  moves), no manifest; `-CheckDeterminism` runs the full pipeline twice and
  compares SHA256. The jar hash is what campaign artifact headers cite
  (design §2.7). **Editing `ModTheSpire.json` changes this hash** — re-derive it
  and update the forward-looking citations (`../driver/b45_reward_spotdiff.md`).
- **Output:** `<repo>/build/oracle_fork/CommunicationMod-oracle.jar`,
  deployed to `<game>\mods\CommunicationMod-oracle.jar` (skip with
  `-NoDeploy`). The jar is a build artifact — never committed.

## Running

As the stock mod (see `../driver/README.md`), but select the fork's modid:

```bat
cd /d D:\SteamLibrary\steamapps\common\SlayTheSpire
java -jar "D:\SteamLibrary\steamapps\workshop\content\646570\1605060445\ModTheSpire.jar" --skip-launcher --mods basemod,CommunicationMod-oracle
```

Never list both `CommunicationMod` and `CommunicationMod-oracle` — they carry
the same patch classes and would double-patch.

## Patch families (design §2.4)

| Family | Status |
|---|---|
| 1. Oracle state block (§2.5) | landed at B1.2 (`GameStateConverter`, flag `oracleBlock`); grown at S3.21 (2026-09-03) by the **oracle contract v2** additions — the three `Settings.has*Key` booleans + `isFinalActAvailable`, `dungeonId`, the map node's `has_emerald_key` and the power `misc` union's `misc_field` tag. All four ride the same `oracleBlock` gate or are emitted only when set, so they need **no flag of their own** and are not part of the equivalence baseline: none of them changes a game rule. `PROTOCOL.md` §5.6 |
| 2. Rendering-strip / fast-forward (§2.2) | landed at B1.3 (see below) |
| 3. Campaign QoL (fast startup, restart hooks) | scoped at B1.4 with the driver |
| 4. Oracle-contract pins (a retail behavior that is not a function of `(seed, actions)`) | `patches/OraclePlaytimePinPatch`, landed at S2.43 (2026-08-27), flag `oraclePlaytimePin`; `patches/CourierRestockSeedPatch`, source landed at S3.24 (2026-09-03) and carried into the jar by S3.21's redeploy (2026-09-03), flag `oracleCourierRestockSeed` — see both below, and `PROTOCOL.md` §5.5 |

B1.1 = build pipeline only: the fork jar must reproduce the stock jar's
behavior byte-for-byte (B0.2 capture replay, uuid-normalized per PROTOCOL.md).

## Oracle-contract pin: SecretPortal's wall clock (S2.43, `oraclePlaytimePin`)

`AbstractDungeon.getShrine`'s SecretPortal candidate is gated on
`CardCrawlGame.playtime >= 800.0f` (`AbstractDungeon.java:1929-1933`), and the
draw is by INDEX into the surviving list (`:1937`) — so past 800 s of live play
every Act-3 `?` room resolved to a different event than a sim-emitted script
expects. `OraclePlaytimePinPatch` is a `@SpireInstrumentPatch` that replaces
**only that one field read** with a pinned `0.0f`, and `oracle.playtime` is
emitted from the same helper so the capture records the effective value the gate
saw. Every other `playtime` reader — the save file, metrics, SPEED_CLIMBER, the
end-of-run screens — still sees the true wall clock. Full contract, the
nine-reader audit, and the offline verification: `../PROTOCOL.md` §5.4.

**This flag is part of the equivalence baseline.** `oraclePlaytimePin` defaults
**on** and changes game behavior, so reproducing stock/pre-B1.3 behavior now
means turning it off **as well as** the three strip flags below.

## Oracle-contract seed: The Courier's restocked card (S3.24, `oracleCourierRestockSeed`)

`ShopScreen.purchaseCard`'s Courier branch replaces the bought colored card with
`getCardFromPool(rollRarity(), hoveredCard.type, **false**)`
(`ShopScreen.java:615-617`). The `useRng = false` argument sends
`CardGroup.getRandomCard(CardType, boolean)` down its `MathUtils.random` branch —
libGDX's **global** RandomXS128, seeded from JVM-startup entropy and advanced by
rendering — so the restocked card's identity is not a function of
`(seed, actions)` at all, and it was the last such value in scope.
`CourierRestockSeedPatch` is a `@SpireInstrumentPatch` that replaces **only those
two calls** with a helper reproducing `getCardFromPool`'s pool walk exactly and
indexing it with a dedicated stream,
`new Random(Settings.seed + 1000003L + cardRng.counter)` — the same construction
the simulator makes in `shop.hpp`'s `courier_restock_stream`. Nothing else is
touched: `getColorlessCardFromPool` was already seeded, and every
`getCardFromPool` caller outside `purchaseCard` keeps retail's behaviour.

**This flag is part of the equivalence baseline**, like `oraclePlaytimePin`
above: `oracleCourierRestockSeed` defaults **on**, so reproducing stock/pre-B1.3
behaviour means turning it off as well.

The patch source landed at S3.24; **the jar it rides is S3.21's single
redeploy**, which also owns the `PROTOCOL.md` §5.5 write-up. Full before/after,
the seeding contract and the offline verification:
[COURIER-RESTOCK-HANDOVER.md](COURIER-RESTOCK-HANDOVER.md).

## Rendering-strip / fast-forward patches (B1.3, design §2.2)

Three individually-toggleable patch families make wall-clock time stop mattering
without changing gameplay state or queue order ("remove time and pixels, never
order or state"). Config flags live in the same
`SpireConfig("CommunicationMod","config")` store, all default **on**; with all
three **off** — and, since S2.43 and S3.24, `oraclePlaytimePin` and
`oracleCourierRestockSeed` off too — the fork is
byte-identical to its pre-B1.3 behaviour (the strip-equivalence baseline). Each
is also a toggle in the mod-settings panel.

| Flag (`config.properties`) | Patch class | Seam (provenance into `SlayTheSpireDecompiled`) |
|---|---|---|
| `stripDrawSuppression` | `patches/StripDrawSuppressionPatch` | Prefix-return `AbstractDungeon.render(SpriteBatch)` (`dungeons/AbstractDungeon.java:2153`) — skips all scene/room/effect draws; `CardCrawlGame.render` keeps `this.update()` (:368) + `sb`/`glClear` (:371-372,426) so the GL surface stays live. |
| `stripAnimationCollapse` | `patches/StripAnimationCollapsePatch` | Prefix `LwjglGraphics.getDeltaTime()` (`backends/lwjgl/LwjglGraphics.java:132`) → fixed `STEP=0.043` while stripping. Collapses every `-= getDeltaTime()` timer (action `tickDuration` `AbstractGameAction.java:74`; room `waitTimer`/`endBattleTimer` `AbstractRoom.java:233,279`; fade `AbstractDungeon.java:2311,2318`; `AbstractEvent.waitTimer` :103) at one chokepoint. |
| `stripFastCadence` | `patches/StripFastCadencePatch` | Postfix `DesktopLauncher.loadSettings` (`desktop/DesktopLauncher.java:107`) → `foregroundFPS=backgroundFPS=0` (uncapped), `vSyncEnabled=false` (overrides :118,145). Read pre-init via its own read-only `SpireConfig`. |

**Why `STEP` is small and non-round (0.043), not a big constant.** Game logic
fires on timer edges, and two edge hazards bite a naive delta:
- *Leap-over*: some presentation edges fire on an INTERMEDIATE threshold, e.g.
  `BattleStartEffect.showIntent()` sets each monster's dumped `intent` only once
  its 4.0s effect duration falls below 3.0 and a sub-timer elapses. A huge delta
  leaps duration past done in one frame, skipping it → `intent` diverges. A small
  step steps through.
- *Exact-zero landing*: `AbstractEvent.update` (:101-107) shows event dialog
  options only on the frame `waitTimer` crosses `< 0.0f` (Neow's starts at 1.5).
  A round step that evenly divides the start (0.05→30, 0.1→15) lands on exactly
  0.0, skipping the `< 0.0f` edge — options never show, yet `waitTimer==0`
  reports ready, hanging the event. A non-round step never evenly divides the
  game's round timer values.

`getRawDeltaTime()` is deliberately left unpatched (it and `getDeltaTime` are
separate methods over the same field, `LwjglGraphics.java:132-139`), so the
frame-skip guard `if (getRawDeltaTime() > 0.1f) return` (`CardCrawlGame.java:362`)
keeps its stability role.

Equivalence + throughput are proven by the driver A/B harness
(`../driver/{extract_scripts,compare_ab,measure_throughput}.py`): the same fixed
per-seed scripts run strip-on and strip-off, dumps byte-compared after dropping
`uuid` (PROTOCOL.md). See the B1.3 ledger Log for the acceptance numbers.
