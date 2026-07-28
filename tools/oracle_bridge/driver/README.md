# Oracle bridge driver (Stage B)

This directory holds the oracle-bridge driver family. Protocol details are in
[../PROTOCOL.md](../PROTOCOL.md) (surveyed from the vendored source at B0.1).

| File | Task | Role |
|---|---|---|
| `campaign_driver.py` | **B1.4** | the real campaign driver — the CommunicationMod-oracle child: seeded A20 Ironclad starts, random-legal / greedy / scripted generators, per-run JSONL artifacts (design 2.7), crash detection, seed-level resume, batch over a seed list |
| `greedy_policy.py` | **B4.x** | the `--policy greedy` scorer: a pure, depth-seeking heuristic over the parsed dump (combat, map, rewards, screens) |
| `cards_sidetable.json` | **B4.x** | committed per-card damage/block numbers the greedy policy scores with |
| `gen_cards_sidetable.py` | **B4.x** | regenerates `cards_sidetable.json` from `registry/cards.yaml` (dev-time, needs PyYAML; the driver itself never imports it) |
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
stops instead of relaunching.

**The runtime stack is observed, never assumed (B4.5).** Immediately after that
check the driver reads the exact append-only `mts_launch<N>.log` allocated for
its game process, parses ModTheSpire's `Version Info:` / `Mod list:` block, and
writes **those** values into the artifact header (`game.sts_version`,
`game.mts_version`, `game.basemod_version`, `game.version_source`, plus the full
`mods_loaded` map). The orchestrator binds that filename to the driver command
and passes a one-use token through the game process's inherited environment;
resume continues numbering above every preserved log, while a later GUI launch
inherits the persisted command but not the matching environment binding. It
refuses with the same
`fatal_environment_drift` status, carrying a distinguishing `kind`, when the
observed stack is not the sanctioned one
(`stack_version_mismatch` — which also covers stock `CommunicationMod` loaded
beside the fork, and the fork being absent), when the log has no readable
version block (`stack_unparseable`), or when there is no launch log at all
or it is not bound to this orchestrator launch (`stack_unobservable` — the
GUI-launch case, which is exactly how the invalid `b45_rewards` capture arose).
Before B4.5 those header fields were hard-coded constants, so every artifact
ever written asserted the sanctioned versions regardless of what actually
launched. The sanctioned values now live in `campaign_driver.py`'s
`SANCTIONED_*` constants and are only ever *compared* against the log, never
copied into a header; moving the pin means editing those constants **and**
design §1.2 deliberately (design §11 v0.1.7).

Run campaign acceptance with
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
An in-progress ledger also refuses resume under a different driver revision,
preventing one campaign from mixing capture logic even when schema and fork are
unchanged.
Campaign ids are single safe path components; both CLIs reject rooted paths,
separators, `.`/`..`, and every symlink/junction/reparse redirect at a campaign
directory or direct-child file. `--fresh` never follows an owned-looking name
to a different target, even when that target remains inside the campaign.

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

### Policies

| `--policy` | Choice rule |
|---|---|
| `random-legal` (default) | uniform over `expand_legal_actions` — the game's own `available_commands`, expanded to concrete arguments |
| `greedy` | the **same** expansion, ranked by `greedy_policy.score_action` |
| `script` | a fixed command list (one per line), `--script <file>` or `--script-dir <dir>` |

**`--policy greedy` (depth).** `random-legal` is unbiased and therefore shallow:
across 41 recent runs its median death floor was 3 and its deepest was 12, so the
states a deep capture needs — the treasure chest at floor 8+, the Act-1 boss at
16-17 — essentially never appear. `greedy` keeps the same legal-by-construction
expansion and only changes which candidate is taken:

- **combat** — mirrors the sim's fuzz scoring (`tools/fuzz/src/policy.cpp`
  `move_score`): lethal first, then `damage*4 + block` when nothing is swinging
  and `block*4 + damage` while a monster's intent is an attack, focus fire on the
  lowest-HP live monster, `end` strictly last. Per-card numbers come from
  `cards_sidetable.json`; a card outside the S1 registry scores as cheap utility.
- **map** — non-combat > monster > elite, and the boss node when it is offered.
  This is deliberately **inverted** relative to the sim's elite-first fuzz
  weights: the fuzzer wants long varied fights, this wants floors survived.
- **screens** — claim relics/gold/potions, **never** a `SAPPHIRE_KEY` row (it
  retires the linked relic ungranted — `RewardItem.claimReward`,
  RewardItem.java:255-330 case 6), leave card rewards unopened (skipping is
  always safe), open treasure chests, rest when hurt / smith when healthy, and
  leave shops without buying.

Ties are broken with the run's own policy RNG (`Random(f"{policy_seed}:{seed}")`,
one draw per decision), so a greedy campaign is reproducible from
`(--policy-seed, seed)` exactly as the random-legal one is.

Regenerate the side table after any `registry/cards.yaml` change:

```bash
python tools/oracle_bridge/driver/gen_cards_sidetable.py
```

`test_oracle_campaign.py::CardSideTableTest` fails if the committed JSON drifts
from the registry.

### Scripted mode and its two gates

`--policy script` paces a fixed command list through the same lock-step gate.
Two checks stand between a stale script and a silently mutated run:

- `cmd_verb_ready` waits (bounded by `--max-settle`) for the game to advertise
  the command's **verb**; a verb that never arrives ends the run as
  `cmd_never_ready`.
- `cmd_args_ready` then checks the **arguments** against the very state the
  command is about to enter: `choose <i>` against `choice_list`, `play <i> [t]`
  against the hand and the monster list. Out of range ends the run as
  `cmd_arg_invalid` instead of firing a command the game answers with an
  InvalidCommand that the eight-error budget quietly absorbs. `choose <name>`
  (the protocol's string form) and any collection the dump does not carry are
  passed through untouched. Neither live policy is affected — both draw from
  `expand_legal_actions` and are in range by construction.

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
