# Oracle bridge driver (Stage B)

This directory holds the oracle-bridge driver family. Protocol details are in
[../PROTOCOL.md](../PROTOCOL.md) (surveyed from the vendored source at B0.1).

| File | Task | Role |
|---|---|---|
| `campaign_driver.py` | **B1.4** | the real campaign driver — the CommunicationMod-oracle child: seeded A20 Ironclad starts, random-legal + scripted generators, per-run JSONL artifacts (design 2.7), crash detection, seed-level resume, batch over a seed list |
| `orchestrator.py` | **B1.4** | Windows-host outer loop that owns the game process: writes config.properties, launches ModTheSpire under the bundled JRE 8, relaunches on crash/hang/boss-reward, induces a kill for acceptance |
| `validate_artifacts.py` | **B1.4** | validates run JSONL against the PROTOCOL.md schema (header + action `state_json` + oracle block + terminal) |
| `echo_driver.py` | B0.2 | bring-up child: logs every state JSON, forwards side-channel commands, `--verify` re-parse |

## B1.4 campaign driver

`campaign_driver.py` is the CommunicationMod-oracle child (the game spawns it and
owns its stdio — see [Topology](#topology-protocolmd-1)). It is a strict
**lock-step** stepper: it sends **one command per fresh `ready_for_command`**
state and **never blind-resends** on silence (prolonged silence is a crash, per a
wall-clock watchdog — the B0.2 contamination postmortem, ledger B1.1 Log). It
plays seeded A20 Ironclad runs to a terminal state (death, Act-1 boss-reward
claimed — design 1.1, so it stops *before* the boss chest — legal-action
exhaustion, or an action cap), writing one JSONL artifact per run.

Because the game (not the orchestrator) is the driver's parent, the driver cannot
own game launch/kill/relaunch — `orchestrator.py` does. The driver owns the
per-run protocol, the artifacts, and a durable `campaign_progress.json`; a
crashed game costs one run, not the campaign (design 7.1(2)). Resume granularity
is **one seed** (the protocol exposes no mid-run save): an interrupted seed is
re-run from `start` on the next launch (retry-once, then failed).

### Running a campaign (operator, Windows host)

```bat
C:\Python39\python.exe orchestrator.py ^
    --campaign-id b14_accept ^
    --seeds D:/STS_BG_Mod/_oracle_data/campaigns/b14_seeds.txt ^
    --policy random-legal ^
    --kill-after-seeds 3 --fresh
```

The orchestrator writes `%LOCALAPPDATA%\ModTheSpire\CommunicationMod\config.properties`
(so `runAtGameStart=true` spawns the driver with the right args), launches the
game under `<game>\jre\bin\java.exe` (bundled JRE 8 — never system Java, ledger
B1.1 Log), watches `campaign_progress.json` + `campaign_heartbeat.json`, and
relaunches on crash / hang / boss-reward until the driver marks the campaign
complete. `--kill-after-seeds N` induces one deliberate mid-campaign game kill to
exercise crash-resume (the B1.4 acceptance bar).

An oracle campaign is valid only when the explicitly selected
`CommunicationMod-oracle` fork emits `game_state.oracle`. The driver checks the
first in-dungeon dump before it creates an artifact or advances the policy; a
missing block records `status: fatal_environment_drift`, and the orchestrator
stops instead of relaunching. Run campaign acceptance with
`validate_artifacts.py --require-oracle`; the default validator remains
backward-compatible with old, deliberately non-oracle B1.4 artifacts.
Strict campaign validation requires a complete, failure-free progress and
manifest ledger, exact ordered seed completion, at least one valid in-game
oracle action per run, and one current run plus timing artifact per completed
seed with no missing or extra files. Run headers are bound to campaign id,
seed string/long/getLong, attempt, policy, schema, driver, and fork identity;
every in-game `game_state.seed` and `oracle.seed` must agree. Strict run grammar
requires contiguous action sequence numbers, exactly one final terminal, and
matching injected-action/terminal/done summaries. Timing sidecars are parsed in
full and joined mark-for-action (sequence, command, floor, and screen), with
their headers bound to the same campaign/schema/driver/fork identity.

Treat a campaign id as immutable evidence. A new live attempt gets a new id;
failed directories are preserved rather than retried in place. `--fresh`
removes only this invocation's known control files, launch logs, and exact
requested-seed run/timing names. It deliberately preserves unexpected files,
which strict validation then rejects as stale instead of silently deleting.
Campaign ids are single safe path components; both CLIs reject rooted paths,
separators, `.`/`..`, and any campaign directory or file whose resolved path
escapes `data-root`.

Artifacts land under `<data-root>/<campaign-id>/`:
`run_<seed>_a20_ironclad.jsonl` (one per run), `campaign_progress.json`,
`campaign_manifest.json`, `campaign_heartbeat.json`,
`orchestrator_timeline.json`, `mts_launch<N>.log`. The **data root is never
committed** (design 7.3); default `D:\STS_BG_Mod\_oracle_data\campaigns`.

### Artifact schema (design 2.7)

Line 1 is a `header` (schema/driver version, game+mod-set, **fork-jar sha256**,
seed as **both** base-35 string and long, ascension, character, policy). Then one
`action` record per injected action — `{action_command, sim_action_bits (null,
B1.5 fills it), ready_for_command, available_commands, state_json}` — where
`state_json` is the game's dump **verbatim / un-pruned** (lossless: the translator
B1.5 enforces unknown-field-is-error). A final synthetic
`__terminal_observed__` action may preserve the post-claim state; it is
sequence-bearing but is not counted as an injected action or timing mark. The
file ends with exactly one `terminal` record (`outcome`, floor, act, actions).
Validate with:

```bash
python validate_artifacts.py --campaign D:/STS_BG_Mod/_oracle_data/campaigns/b14_accept
```

### Scripted mode

`--policy script --script <file>` paces a fixed command list (one per line) through
the same lock-step gate instead of the random-legal generator.

---

## B0.2 echo_driver

`echo_driver.py` is the minimal CommunicationMod child process: it logs every
game-state JSON the game emits and forwards commands the operator supplies. It is
the bring-up tool for the bridge; `campaign_driver.py` grew from here.

## Topology (PROTOCOL.md §1)

CommunicationMod launches one external child and wires **the child's own
stdio** to the game:

```
game --(state JSON, one object per line, to child stdin)--> echo_driver.py
game <--(commands, \n/NUL-delimited, from child stdout)---- echo_driver.py
```

Because the child's stdin/stdout belong to the game, the operator cannot type
commands into them. `echo_driver.py` therefore reads commands from a
**side-channel file** (`--commands`): every newline-appended line is forwarded
verbatim to the game. On startup it emits one `state` command (the safe
readiness signal — forces a dump, changes nothing) to unblock the game's
`readMessageBlocking` handshake (10 s timeout, PROTOCOL.md §1.3).

## Wiring (config.properties)

CommunicationMod reads its config **only at game startup** from
`%LOCALAPPDATA%\ModTheSpire\CommunicationMod\config.properties`. Set:

```properties
runAtGameStart=true
command=C:/Python39/python.exe D:/STS_BG_Mod/SpeedTheSpire/tools/oracle_bridge/driver/echo_driver.py --log D:/STS_BG_Mod/_oracle_data/run_capture.jsonl --commands D:/STS_BG_Mod/_oracle_data/commands.txt --latest D:/STS_BG_Mod/_oracle_data/latest_state.json
```

- **Forward slashes only.** Java `.properties` treats `\` as an escape; forward
  slashes avoid that and Java `ProcessBuilder` accepts them on Windows.
- **No spaces in any path.** The mod does `command.trim().split("\\s+")` into
  argv (CommunicationMod.java:271), so a space inside a path would split it.
- `runAtGameStart=true` makes the mod spawn the driver at launch. Otherwise use
  the mod-settings **"(Re)start external process"** button. Editing the file
  while the game is running has no effect until the next launch.

## Scriptable launch (design §1.2 paths)

For oracle work, use `orchestrator.py`; its launch is deliberately explicit:

```bat
cd /d D:\SteamLibrary\steamapps\common\SlayTheSpire
"D:\SteamLibrary\steamapps\common\SlayTheSpire\jre\bin\java.exe" -jar "D:\SteamLibrary\steamapps\workshop\content\646570\1605060445\ModTheSpire.jar" --skip-launcher --mods basemod,CommunicationMod-oracle
```

- ModTheSpire `1605060445`, BaseMod `1605833019`, CommunicationMod
  `2131373661` (Steam workshop, appid 646570).
- The stock workshop `CommunicationMod` and the fork share the same
  `SpireConfig("CommunicationMod", ...)` namespace. A GUI launch may therefore
  spawn the campaign driver while loading the stock jar, producing plausible
  artifacts with no oracle block. The GUI path is **not equivalent for oracle
  campaigns**; never rely on its remembered mod selection.
- Stock `CommunicationMod` remains usable only for the deliberately non-oracle
  B0.2 protocol bring-up. Never load stock and fork together.
- CommunicationMod's stderr lands in `communication_mod_errors.log` in the game
  directory — first place to look if the child never attaches.

## Driving a run

Append commands (one per line) to the `--commands` file; blank lines and `#`
comments are ignored. Command grammar: PROTOCOL.md §2.

```bash
CMD=D:/STS_BG_Mod/_oracle_data/commands.txt
echo 'start ironclad 20 <SEED>'  >> "$CMD"   # seeded A20 Ironclad run
echo 'state'                     >> "$CMD"   # force a fresh dump
echo 'choose 0'                  >> "$CMD"   # pick the first listed choice
echo '__quit__'                  >> "$CMD"   # detach the driver cleanly
```

Send state-changing commands only while the latest state has
`ready_for_command: true` (PROTOCOL.md §1.5). The seed is the game's **base-35
display string**, not a raw long (PROTOCOL.md §2.1).

## Artifacts and verification

- Capture (`--log`) and the latest-state snapshot (`--latest`) live under the
  bridge **data root** `D:\STS_BG_Mod\_oracle_data` (design §7.3), outside the
  repo — **never committed** (raw JSONL traces are campaign artifacts).
- `python echo_driver.py --verify <log.jsonl>` re-parses a capture and reports
  that every recorded state line is valid JSON (the B0.2 acceptance check). Runs
  standalone, no game.
- The driver logs a `meta` throughput record (`recv`, `sent`, `recv_per_s`) on
  clean exit; that `recv_per_s` is the stock-game baseline actions/sec.
