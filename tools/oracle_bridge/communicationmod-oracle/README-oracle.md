# CommunicationMod-oracle (the SpeedTheSpire fork)

This directory vendors upstream `ForgottenArbiter/CommunicationMod` v1.2.1
(commit `70ca84b1e8daff3eb4fe7f66775ce39926133c7f`, MIT — see `LICENSE`),
forked per stage-b-design §2.4. Upstream's own docs are in `README.md`
(untouched, for drift auditing). Fork policy: pinned at v1.2.1, never tracks
later upstream (the game is frozen at 11-30-2020).

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
- **Determinism:** entries ordinal-sorted, timestamps pinned (2020-11-30),
  no manifest; `-CheckDeterminism` runs the full pipeline twice and compares
  SHA256. The jar hash is what campaign artifact headers cite (design §2.7).
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
| 1. Oracle state block (§2.5) | landed at B1.2 (`GameStateConverter`, flag `oracleBlock`) |
| 2. Rendering-strip / fast-forward (§2.2) | landed at B1.3 (see below) |
| 3. Campaign QoL (fast startup, restart hooks) | scoped at B1.4 with the driver |

B1.1 = build pipeline only: the fork jar must reproduce the stock jar's
behavior byte-for-byte (B0.2 capture replay, uuid-normalized per PROTOCOL.md).

## Rendering-strip / fast-forward patches (B1.3, design §2.2)

Three individually-toggleable patch families make wall-clock time stop mattering
without changing gameplay state or queue order ("remove time and pixels, never
order or state"). Config flags live in the same
`SpireConfig("CommunicationMod","config")` store, all default **on**; with all
three **off** the fork is byte-identical to its pre-B1.3 behaviour (the
strip-equivalence baseline). Each is also a toggle in the mod-settings panel.

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
