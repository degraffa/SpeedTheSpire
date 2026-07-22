# Oracle bridge driver (Stage B B0.2)

`echo_driver.py` is the minimal CommunicationMod child process: it logs every
game-state JSON the game emits and forwards commands the operator supplies. It
is the bring-up tool for the bridge; the real campaign driver (B1.4) grows from
here. Protocol details it relies on are in [../PROTOCOL.md](../PROTOCOL.md)
(surveyed from the vendored source at B0.1).

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

ModTheSpire, run from the game directory so it finds `desktop-1.0.jar`:

```bat
cd /d D:\SteamLibrary\steamapps\common\SlayTheSpire
java -jar "D:\SteamLibrary\steamapps\workshop\content\646570\1605060445\ModTheSpire.jar" --skip-launcher --mods basemod,CommunicationMod
```

- ModTheSpire `1605060445`, BaseMod `1605833019`, CommunicationMod
  `2131373661` (Steam workshop, appid 646570).
- The GUI path (Steam → *Play with Mods*, or launching `mts-launcher.jar`) is
  equivalent; `--skip-launcher` just bypasses the mod-picker for automation.
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
