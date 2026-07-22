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
| 1. Oracle state block (§2.5) | lands at B1.2 |
| 2. Rendering-strip / fast-forward (§2.2) | lands at B1.3 |
| 3. Campaign QoL (fast startup, restart hooks) | scoped at B1.4 with the driver |

B1.1 = build pipeline only: the fork jar must reproduce the stock jar's
behavior byte-for-byte (B0.2 capture replay, uuid-normalized per PROTOCOL.md).
