# CommunicationMod protocol survey (B0.1)

Every claim below is read from the **vendored** upstream source under
`communicationmod-oracle/`, not from samples, wikis, or memory. Citations are
`File.java:line` relative to `communicationmod-oracle/src/main/java/communicationmod/`.

## 0. Provenance & license

| Item | Value | Evidence |
|---|---|---|
| Upstream repo | `github.com/ForgottenArbiter/CommunicationMod` | design §1.2 |
| Pinned ref | tag `v1.2.1` = commit `70ca84b1e8daff3eb4fe7f66775ce39926133c7f` | `git rev-list -n1 v1.2.1` == checked-out HEAD |
| Version match | `pom.xml` `<version>1.2.1</version>`; `sts_version 11-30-2020`; `mts_version 3.18.1` | pom.xml:8,16-17 |
| Local jar match | Steam workshop item `2131373661/CommunicationMod.jar`, `ModTheSpire.json` version `1.2.1`, sts_version `11-30-2020` | jar `ModTheSpire.json` |
| CHANGELOG top | `#### v1.2.1 #### * Fix an issue where the external process was not sent state` | CHANGELOG.md:3-4 |
| License | **MIT** — `LICENSE` file: "MIT License", Copyright (c) 2019 ForgottenArbiter | LICENSE:1-3 |

The pinned tag, the source `pom.xml`, and the locally installed workshop jar
agree on version 1.2.1 / patch 11-30-2020. Source is vendored **source-only**:
no `.jar`/`.class`/build artifacts (verified by `find`); the single binary is
`src/main/resources/Icon.png` (626 B mod-badge, loaded at
CommunicationMod.java:219 — a build input, not a build output).

### 0.1 The fork's own runtime pin (amended 2026-07-26, B4.5)

The four rows above describe **upstream**, and every one of them is still
literally true: upstream v1.2.1 really was authored against StS `11-30-2020` /
MTS `3.18.1`, and the stock workshop jar really does declare that. They are
evidence about someone else's artifact and are **not** this project's pin.

The **fork's** pin — the sanctioned runtime the oracle bridge captures on — is
separate, and since B4.5 it is stated where it is actually enforced:

| Item | Value | Evidence |
|---|---|---|
| Slay the Spire | `12-18-2022` (`[V2.3.4]`) | `CardCrawlGame.VERSION_NUM`/`TRUE_VERSION_NUM` (CardCrawlGame.java:125-126) in the decompiled tree; `Version Info:` in every campaign's `mts_launch<N>.log` |
| ModTheSpire | `3.30.3` | `Version Info:` in every campaign's `mts_launch<N>.log` |
| BaseMod | `5.56.0` (workshop item `1605833019`) | `Mod list:` in every campaign's `mts_launch<N>.log` |
| Fork manifest | `src/main/resources/ModTheSpire.json` `sts_version`/`mts_version` | this repo |

**Why the fork manifest deliberately diverges from upstream's.** Those two
fields are not inert metadata — ModTheSpire reads both, with *different*
semantics, and only one of them can refuse a launch:

- **`mts_version` is a hard minimum.** `ModInfo.MTS_Version` is parsed as a
  **semver** (`ModInfo$VersionDeserializer`), and both `Patcher.initializeMods`
  and `Patcher.sideloadMods` compare it against `Loader.MTS_VERSION`; if the
  declared version is *greater* than the installed one the mod is refused with
  `ERROR: <name> requires ModTheSpire v<X> or greater!` on stdout **and a modal
  `JOptionPane` dialog** — which under the scripted `--skip-launcher` launch is
  an indefinite hang, not a clean failure. Declaring `3.30.3` therefore turns
  the sanctioned MTS floor into something ModTheSpire itself enforces; it is
  safe only while the installed MTS is ≥ `3.30.3`, which for an auto-updating
  workshop item it is. A *downgrade* below the pin now fails loudly by design.
- **`sts_version` is a launcher warning only.** It is consumed solely by
  `ModPanel` (the mod-select GUI), which string-compares it to
  `Loader.STS_VERSION` and, on mismatch, sets a status tooltip reading "This
  mod explicitly supports StS …. You are running StS …. You may encounter
  problems running it." Nothing gates on it, and `--skip-launcher` never builds
  a `ModPanel` at all. Correcting it to `12-18-2022` changes **no** load
  behavior — it only stops the manifest asserting something false.

All of the above was read from the installed `ModTheSpire.jar`
(`1605060445`) with `javap`, not from memory or upstream docs.

## 1. Message framing (settles design [confirm at B0.1] #1)

### 1.1 Process model

CommunicationMod launches one external child process
(`ProcessBuilder(getSubprocessCommand())`, CommunicationMod.java:322-326) whose
command comes from `config.properties` key `command`
(`%LOCALAPPDATA%\ModTheSpire\CommunicationMod\config.properties`). The child's
**stderr** is redirected to `communication_mod_errors.log`
(CommunicationMod.java:323-324). Two dedicated threads carry the byte streams
(CommunicationMod.java:222-227):

| Thread | Stream | Direction |
|---|---|---|
| `DataWriter` | `listener.getOutputStream()` = child **stdin** | game → child (state JSON out) |
| `DataReader` | `listener.getInputStream()` = child **stdout** | child → game (commands in) |

### 1.2 Direction and delimiting

- **Game → child (state):** `DataWriter.run` takes each queued message, writes
  `message.getBytes()`, writes a single `'\n'`, and `flush()`es
  (DataWriter.java:31-33). So **state JSON goes to the child's stdin, one JSON
  object per line, newline-terminated.**
- **Child → game (commands):** `DataReader.run` reads the child's stdout
  char-by-char; `-1` (EOF, none available) is skipped, and a message ends on
  either `'\n'` **or** `0` (NUL) (DataReader.java:28-34). Non-empty buffers are
  enqueued as commands. So **commands come from the child's stdout, delimited
  by `\n` or NUL.**

Both directions are therefore **line-delimited JSON/text over the child's
stdio**. This confirms design §2.1 / §2.3's previously-unverified assumption.

### 1.3 Startup handshake

On process start the game **blocks** waiting for the child's first message
(`readMessageBlocking`, CommunicationMod.java:335) up to
`maxInitializationTimeout` seconds (default `10`, CommunicationMod.java:52,
260-266); on timeout the child is killed (CommunicationMod.java:336-343). So the
child must emit one line promptly at startup to signal readiness. After that,
`receivePreUpdate` polls the read queue each frame and dispatches commands
(CommunicationMod.java:86-106).

### 1.4 The two message shapes on the wire (game → child)

1. **Status object** — `GameStateConverter.getCommunicationState()`
   (GameStateConverter.java:51-62), emitted after every stable state change
   (`sendGameState`, CommunicationMod.java:230-233). Top-level keys:
   `available_commands`, `ready_for_command`, `in_game`, and `game_state`
   (only when `in_game`). This is the object catalogued in §3.
2. **Error object** — on an `InvalidCommandException`, `receivePreUpdate`
   sends `{ "error": <message>, "ready_for_command": <bool> }`
   (CommunicationMod.java:98-104). These two keys are **protocol control**, not
   game state (disposition: ignored-with-reason — protocol plumbing).

### 1.5 `ready_for_command` semantics

`ready_for_command` = `GameStateListener.isWaitingForCommand()`
(GameStateConverter.java:54 → GameStateListener.java:235-237, returns the
private static `waitingForCommand`). It is set **true** when a stable dungeon or
menu state change is detected (`checkForDungeonStateChange`,
GameStateListener.java:216-233; `checkForMenuStateChange`, :196-207) and set
**false** on command execution (`registerCommandExecution`, :44-46) or when
in-game logic registers a pending change (`registerStateChange`, :28-31).
"Stable" means: not fading in/out, action manager `WAITING_ON_USER` with empty
`actions`/`cardQueue`/`preTurnActions`, event wait-timers at 0, and (in combat)
either a screen is up or it is the player's turn (`hasDungeonStateChanged`,
:97-188). The driver must only send state-changing commands while
`ready_for_command` is true; `state` and `key`/`click`/`wait` are the exceptions
that don't require it in the same way.

## 2. Command grammar (settles design [confirm at B0.1] #2)

Source: `CommandExecutor.executeCommand` (CommandExecutor.java:33-86). The
command line is **lowercased** (`command.toLowerCase()`, :34) then split on
whitespace (`\\s+`, :35). `available_commands` (see §3) advertises exactly the
subset currently legal (`getAvailableCommands`, :88-118); an unavailable command
raises `InvalidCommandException` (:39-41). "State-changing?" is the boolean
`executeCommand` returns — it drives whether a fresh state is sent.

| Command | Aliases | Args | Availability | Source |
|---|---|---|---|---|
| `play` | — | `play <hand_index 1-10> [monster_index]` (index 0→10; target required for ENEMY / SELF_AND_ENEMY cards) | combat, no screen up, a playable card exists | :196-240 |
| `end` | — | none (ends turn) | combat, no screen up | :242-244 |
| `choose` | — | `choose <index \| choice_name>` | a choice list is non-empty and `play` is not available | :246-253, :553-583 |
| `potion` | — | `potion <use\|discard> <slot> [monster_index]` (target if `targetRequired`) | player holds ≥1 real potion | :255-313 |
| `confirm` | `proceed` | none (presses the confirm/proceed button) | screen's confirm button available | :56-59, :315-317 |
| `cancel` | `skip`, `return`, `leave` | none (presses the cancel/skip/return/leave button) | screen's cancel button available | :60-65, :319-321 |
| `start` | — | `start <character> [ascension 0-20] [seed]` — see §2.1 | not in dungeon, main menu present | :66-68, :323-382 |
| `state` | — | none — forces a state dump; **does not** change game state (returns `false`) | always | :69-71, :192-194 |
| `key` | — | `key <key_name> [timeout_frames]` (default 100) | in dungeon | :72-74, :384-407, keymap :471-551 |
| `click` | — | `click <LEFT\|RIGHT> <x> <y> [timeout_frames]` (coords ×`Settings.scale`) | in dungeon | :75-77, :409-453 |
| `wait` | — | `wait <frames>` | in dungeon | :78-80, :455-469 |

`state` is always appended to the available list (:116). `key`/`click`/`wait`
are added whenever `isInDungeon()` (:111-115). Confirm/cancel are advertised by
their **screen-specific button label** (`ChoiceScreenUtils.getConfirmButtonText`
/ `getCancelButtonText`, :103,106) but accepted under any of their aliases
(:121-124).

Notes verified from source:
- `play` hand index is **1-based**, and `0` maps to `10`
  (CommandExecutor.java:206-208).
- `choose` matches the exact choice string first, else parses an integer index
  into the current choice list (:557-570).
- `potion` requires `use|discard` and a numeric slot; `use` on a thrown potion
  needs a monster index (:287-303).

### 2.1 `start` seed syntax — base-35 string, NOT a raw long

`executeStartCommand` (CommandExecutor.java:323-382):

- **character** (`tokens[1]`): matched case-insensitively against
  `AbstractPlayer.PlayerClass` names; `"silent"` is accepted as an alias for
  `THE_SILENT` (:331-339).
- **ascension** (`tokens[2]`, optional): integer, bounds-checked `0..20`
  (:343-352).
- **seed** (`tokens[3]`, optional): taken as `tokens[3].toUpperCase()`,
  **validated against `^[A-Z0-9]+$`**, then converted with
  `seed = SeedHelper.getLong(seedString)` (:353-359). This is the game's
  **base-35 alphanumeric display string** (stage-a §3.5:
  `0123456789ABCDEFGHIJKLMNPQRSTUVWXYZ`, no `O`), **not** a raw decimal long.
  Trial seeds (`TrialHelper.isTrialSeed`) are routed to `Settings.specialSeed`
  with `Settings.isTrial=true` and `seedSet=false` (:360-365). If no seed is
  given, `SeedHelper.generateUnoffensiveSeed(new Random(System.nanoTime()))`
  supplies one (:367-369).

Consequence for the driver / campaign header (design §2.7): the `start` command
must be fed the **display string** (as the desktop seeded-run UI shows it), and
the artifact header's `seed as long` field is the `Settings.seed` echoed in the
state dump (`game_state.seed`, §3), while the base-35 string is what round-trips
through `start`.

## 3. `GameStateConverter` JSON field catalog

Disposition legend (per design §2.6 fail-loudly policy):

- **schema-mapped (S)** — translated into a `RunState`/`CombatState` schema
  field; participates in differential testing.
- **ignored-with-reason (I)** — on the explicit ignore-list; reason given
  (presentation/localization, protocol plumbing, nondeterministic instance id,
  or S2 scope). Drift here is fine; a *new* field in no list fails translation.
- **oracle-block (O)** — the stock value is insufficient/untrustworthy for
  bit-exact diffing; the authoritative value is supplied by the fork's §2.5
  oracle state block. Stock field is advisory only.
- **display-derived (D)** — a presentation value the converter computes from a
  semantic field for the banner/UI. It is redundant with (and can be transiently
  stale versus) its semantic anchor, so it is **not** diffed and the translator
  does not read it — the value is reconstructed from the anchor instead. Drift in
  a `D` field alone is not schema drift. (Added v0.1.2 / B1.3, which dually proved
  the monster `intent`/`move_adjusted_damage` pair display-derived over a 20-seed
  A/B; the semantic anchor is `move_id`. See §3.12 and design §11 v0.1.2.)

Coverage is organized by the object each converter emits. Every `.put("…")`
site in `GameStateConverter.java` maps to exactly one row below (see §4).

### 3.1 Status wrapper — `getCommunicationState` (:51-62)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `available_commands` | list<str> | S | :53 | the game's own legal-command set — a `legal_actions()` oracle (design §2.3) |
| `ready_for_command` | bool | I (protocol plumbing) | :54 | framing handshake (§1.5), not game state |
| `in_game` | bool | I (protocol routing) | :56 | menu vs. dungeon selector |
| `game_state` | object | S (container) | :58 | present only when `in_game`; §3.2 |

### 3.2 `game_state` — `getGameState` (:94-146)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `screen_name` | str | S | :97 | `AbstractDungeon.CurrentScreen` enum name |
| `is_screen_up` | bool | S | :98 | gates command legality |
| `screen_type` | str | S | :99 | CommunicationMod `ChoiceType`; selects `screen_state` |
| `room_phase` | str | S | :100 | `AbstractRoom.RoomPhase` |
| `action_phase` | str | S | :101 | action-manager phase |
| `current_action` | str | I (transient) | :103 | in-flight action class name; present only mid-resolution |
| `room_type` | str | S | :105 | room class simple-name |
| `current_hp` | int | S | :106 | |
| `max_hp` | int | S | :107 | |
| `floor` | int | S | :108 | `floorNum`; sanity anchor (§2.5 #10) |
| `act` | int | S | :109 | |
| `act_boss` | str | S | :110 | `bossKey` |
| `gold` | int | S | :111 | |
| `seed` | long | S | :112 | `Settings.seed` (raw long; §2.1) |
| `class` | str | S | :113 | |
| `ascension_level` | int | S | :114 | |
| `relics` | list<relic> | S | :121 | §3.16 |
| `deck` | list<card> | S | :128 | master deck; §3.13 |
| `potions` | list<potion> | S | :135 | includes empty `PotionSlot`s; §3.17 |
| `map` | list<node> | S | :137 | §3.11 |
| `choice_list` | list<str> | S | :139 | present only when `choose` is available |
| `combat_state` | object | S (container) | :142 | present only in COMBAT phase; §3.10 |
| `screen_state` | object | S (container) | :144 | dispatched per `screen_type`; §3.3-3.9 |

### 3.3 `screen_state` for CHEST / REST — `getRoomState` (:148-162)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `chest_type` | str | S | :152,155 | Treasure & boss-treasure chest class |
| `chest_open` | bool | S | :153,156 | |
| `has_rested` | bool | S | :158 | RestRoom, phase COMPLETE |
| `rest_options` | list<str> | S | :159 | campfire choices (legal actions) |

### 3.4 `screen_state` for EVENT — `getEventState` (:188-234)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `body_text` | str | I (localization) | :206,217 | display prose, current language |
| `event_name` | str | I (localization) | :219 | localized `NAME` |
| `event_id` | str | S | :221,226,228,230 | event identity (`ID`; `"Neow Event"` for Neow) |
| `options` | list<option> | S | :232 | below |
| `options[].text` | str | I (localization) | :197,210 | full localized option text |
| `options[].disabled` | bool | S | :198,211 | option legality |
| `options[].label` | str | I (localization) | :199,212 | short localized label |
| `options[].choice_index` | int | S | :201,213 | index for `choose` (only when enabled) |

### 3.5 `screen_state` for CARD_REWARD — `getCardRewardState` (:243-253)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `bowl_available` | bool | S | :245 | Singing Bowl option |
| `skip_available` | bool | S | :246 | |
| `cards` | list<card> | S | :251 | RNG-generated reward group — a prime diff target |

### 3.6 `screen_state` for COMBAT_REWARD — `getCombatRewardState` (:265-289)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `rewards` | list<reward> | S | :287 | below |
| `rewards[].reward_type` | str | S | :270 | `RewardItem.RewardType` name |
| `rewards[].gold` | int | S | :274 | `goldAmt + bonusGold` (GOLD/STOLEN_GOLD) |
| `rewards[].relic` | relic | S | :277 | §3.16 |
| `rewards[].potion` | potion | S | :280 | §3.17 |
| `rewards[].link` | relic | I (S2 scope) | :283 | SAPPHIRE_KEY link. The key is out of S1, but the row **does** appear on every Act-1 chest open (design §1.1 / §11 v0.1.6, correcting "never fires in S1"). No RNG; ignored here, and a capture claims the linked **relic**, never the key |

### 3.7 `screen_state` for MAP — `getMapScreenState` (:299-312)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `current_node` | node | S | :302 | §3.11 |
| `next_nodes` | list<node> | S | :308 | legal next moves |
| `first_node_chosen` | bool | S | :309 | `firstRoomChosen` |
| `boss_available` | bool | S | :310 | |

### 3.8 `screen_state` for BOSS_REWARD — `getBossRewardState` (:320-328)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `relics` | list<relic> | S | :326 | boss-relic pick. **Promoted from `I (S2 scope)` by S2.42; STORED AND DIFFED since S2.47 (schema v8, 2026-08-10).** The old reason ("the run terminates at act-1 boss combat rewards, before the boss chest", design §1.1 "Out") stopped being true at capture driver `b1.7.0`, which plays through the chest — and an `I` field is *never diffed*, so design §6's S2-G2 item 2 (a **zero-diff** boss-relic pick) was unachievable while this row said `I`. S2.47 landed the storage the S2.42 deferral row demanded: `BossChestState` moved from transient `RunController` state into `RunState.boss_chest` (a pure tail append; `run_state.hpp`), the translator now **emits** this field into it (`seen`=1, `screen`=RELIC_SELECT, exactly three registry-**joined** offers — an unknown boss relic still fails translation loudly, and any count other than three is drift), and `diff_run_states` compares the group by name (`boss_chest.relics[i]` / `.screen` / `.seen` / `.chose_relic`). A BOSS_REWARD dump is the **only** dump that attests the offers, so the replay differ neutralizes the group on records whose capture side carries `seen == 0` (`neutralize_unattested_boss_chest`, replay `main.cpp`) |

### 3.9 `screen_state` for SHOP_SCREEN — `getShopScreenState` (:339-365)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `cards` | list<card> | S | :359 | each carries an extra `price` (:346) |
| `relics` | list<relic> | S | :360 | each carries an extra `price` (:351) |
| `potions` | list<potion> | S | :361 | each carries an extra `price` (:356) |
| `purge_available` | bool | S | :362 | |
| `purge_cost` | int | S | :363 | `ShopScreen.actualPurgeCost` — the run-persistent purge ramp (design §2.5 #6 is thereby directly observable) |
| `price` (on shop card/relic/potion) | int | S | :346,351,356 | `merchantRng`-scaled price |

### 3.10 `combat_state` — `getCombatState` (:506-546)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `monsters` | list<monster> | S | :512 | §3.12 |
| `draw_pile` | list<card> | **O** | :533 | contents mapped, but **order is not the true shuffled order** — line :503 states the draw pile is not randomized when sent; authoritative order needs `shuffleRng` via the fork oracle block (§2.5 #1) |
| `discard_pile` | list<card> | S | :534 | |
| `exhaust_pile` | list<card> | S | :535 | |
| `hand` | list<card> | S | :536 | |
| `limbo` | list<card> | S | :537 | |
| `card_in_play` | card | S | :539 | `player.cardInUse`, when present |
| `player` | object | S (container) | :541 | §3.15 |
| `turn` | int | S | :542 | `GameActionManager.turn` |
| `cards_discarded_this_turn` | int | S | :543 | |
| `times_damaged` | int | S | :544 | `damagedThisCombat` (Blood for Blood) |

### 3.11 Map node — `convertMapRoomNodeToJson`/`convertCoordinatesToJson`/`convertMapToJson` (:555-603)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `x` | int | S | :583 | |
| `y` | int | S | :584 | |
| `symbol` | str | S | :601 | room-type symbol (`?`,`$`,`T`,`M`,`E`,`R`) — encodes room type |
| `parents` | list<{x,y}> | S | :572 | edge sources (the :551 doc-comment "Not implemented" is stale — it **is** populated for full-map nodes) |
| `children` | list<{x,y}> | S | :573 | edge destinations |
| `has_emerald_key` | bool | S | fork addition (S3.21) | `MapRoomNode.hasEmeraldKey` (:61) — the burning-elite mark `setEmeraldElite` writes into ONE elite node per act (AbstractDungeon.java:539-548). **Emitted only when true**, so absence means false and every capture made by an earlier fork build is byte-unchanged (the same shape `misc` and the Bottled trio use). It appears on whichever converter emits the node: the full-map walk, `screen_state.current_node` and `screen_state.next_nodes`. It matters because `MonsterRoomElite.addEmeraldKey` gates its reward row on `getCurrMapNode().hasEmeraldKey` (:90) — without the field a capture cannot say WHICH elite was the burning one, and the differ can only infer it from a claim that may never happen. The engine stores the marked node as the `emerald_x`/`emerald_y` pair (`map_rooms.hpp`), not as a per-node bit, so the translator consumes this structurally today; **S3.11** is the task that gives it a compared consumer |

**The Act-4 map, and the one node it does not emit.** `convertMapToJson` walks
`AbstractDungeon.map` and emits **only nodes with at least one edge**
(`node.hasEdges()`, :703). `TheEnding.generateSpecialMap` (TheEnding.java:72-139)
builds a 5×7 grid whose rooms all sit in column 3, wires rest→shop→elite with
`connectNode` and elite→boss with an explicit `MapEdge` (:86-88), and leaves the
28 other nodes roomless and edgeless. **The `TrueVictoryRoom` node (3,4) has no
edge at all** — it is never entered through the map; `goToTrueVictoryRoom` builds
a *fresh* `MapRoomNode(3, 4)` (ProceedButton.java:191-192). So an Act-4 `map`
array carries exactly **four** nodes (3,0)…(3,3) and the victory node is absent
by the same rule that hides every roomless node in acts 1-3. This is verified
retail behaviour, deliberately **not** changed: emitting edgeless nodes would
rewrite the `map` array of every capture ever taken. The engine's Act-4 map is a
constant (s3-design §4.4), so four compared nodes is the whole navigable map.

### 3.12 Monster — `convertMonsterToJson` (:666-705)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `id` | str | S | :668 | monster id |
| `name` | str | I (localization) | :669 | |
| `current_hp` | int | S | :670 | |
| `max_hp` | int | S | :671 | |
| `intent` | str | **D** | :673,675 | `AbstractMonster.Intent` banner of the current move — **display-derived** from `move_id` (the semantic anchor, byte-identical in every B1.3 diff). Reads `DEBUG` on a living monster until `showIntent` refreshes the banner (§11 v0.1.2 / B1.3). Forced `NONE` under Runic Dome (:672; a boss relic, S2-typical) |
| `move_id` | int | S | :678 | current `EnemyMoveInfo.nextMove` byte — **the semantic move/intent anchor** the translator maps (§3.12 note) |
| `move_base_damage` | int | S | :679 | pre-power move damage; semantic |
| `move_adjusted_damage` | int | **D** | :682,684 | shown intent damage = `move_base_damage` adjusted by powers (Strength/Vulnerable) — **display-derived** and display-coupled to `intent`: `== -1` exactly when `intent`==`DEBUG`. The sim recomputes it from `move_base_damage` + powers (§11 v0.1.2 / B1.3) |
| `move_hits` | int | S | :691 | attack multiplier (1 if not multi) |
| `last_move_id` | int | **O** | :695 | from `moveHistory` — stock exposes only 2 back; the sim tracks 3 and the fork must dump the full history (§2.5 #9) |
| `second_last_move_id` | int | **O** | :698 | as above (§2.5 #9) |
| `half_dead` | bool | S | :700 | |
| `is_gone` | bool | S | :701 | `isDeadOrEscaped()` |
| `block` | int | S | :702 | |
| `powers` | list<power> | S | :703 | §3.14 |

### 3.13 / 3.5 / 3.9 Card — `convertCardToJson` (:623-642)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `name` | str | I (localization) | :625 | |
| `uuid` | str | I (nondeterministic instance id) | :626 | random per-instance UUID — would break bit-exact diffs; identity comes from `id` |
| `misc` | int | S | :628 | only emitted when non-zero (Ritual Dagger etc.) |
| `is_playable` | bool | S | :631 | legality (only when in a room with monsters) |
| `cost` | int | S | :633 | `costForTurn` (-2 unplayable, -1 X) |
| `upgrades` | int | S | :634 | `timesUpgraded` |
| `id` | str | S | :635 | `cardID` — translator join key (registry `game_id`, design §2.6) |
| `type` | str | S | :636 | `CardType` name |
| `rarity` | str | S | :637 | `CardRarity` name |
| `has_target` | bool | S | :638 | |
| `exhausts` | bool | S | :639 | |
| `ethereal` | bool | S | :640 | |
| `price` | int | S | :346 | present only on shop cards (§3.9) |
| `in_bottle_flame` | bool | S | fork addition (Wave-C track 2) | only emitted when true (`AbstractCard.inBottleFlame`); absent == false, so captures made by earlier fork builds translate unchanged. Mapped to the master-deck bottle flag bits (engine `run_deck.hpp`) on the `deck` walk only — combat-pile occurrences are consumed and dropped, because combat `flags` are registry-derived and use the `CardFlag` namespace |
| `in_bottle_lightning` | bool | S | fork addition | same shape (`inBottleLightning`) |
| `in_bottle_tornado` | bool | S | fork addition | same shape (`inBottleTornado`) |

### 3.14 Power — `convertCreaturePowersToJson` (:770-819)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `id` | str | S | :774 | |
| `name` | str | I (localization) | :775 | |
| `amount` | int | S | :776 | |
| `damage` | int | S | :779 | optional (`damage` field, when present) |
| `card` | card | S | :783 | optional nested card (Nightmare etc.) |
| `misc` | int | S | :797 | optional; the first of `basePower`/`maxAmt`/`storedAmount`/`hpLoss`/`cardsDoubledThisTurn` the power declares, found by reflection over that fixed name order. **The union is TAGGED since S3.21** — see `misc_field` below and §5.6 |
| `misc_field` | str | O (tag) | fork addition (S3.21) | names which of the five private fields `misc` was read from. Emitted **only alongside `misc`**, so a power with no misc value is byte-unchanged and a capture made by an earlier fork build simply has no tag. That absence is the contract's hinge: **no tag → the translator keeps its existing per-power inference; tag present → the inference is verified against it** and a disagreement aborts the translation. A value outside the five names is drift |
| `just_applied` | bool | S | :811 | optional; `justApplied`/`skipFirst` |

**Why the union had to be tagged (the S3.21 (c) deliverable, and the discharge
of the stage-b "translator power `misc` fields" deferred row).** Upstream picks
the first present field and says nothing about which one it was, so a reader
holding `{"id":"Invincible","misc":300}` cannot distinguish `maxAmt` from a
`basePower` some other power might also declare. Until Act 4 that was a latent
ambiguity: the only claimed member was player-owned Combust's `hpLoss`, which
the translator resolves by power id (`CombatState.flags`, B3.7). Act 4 makes it
live — `InvinciblePower`'s private `maxAmt` (InvinciblePower.java:18-29) is the
union's *second* member, on a power the differ compares every turn of the Heart
fight, and `atStartOfTurn` restores `amount = maxAmt` (:44-48) so the two
numbers genuinely differ within a turn. The tag turns a silent misread into a
loud one.

### 3.15 Player — `convertPlayerToJson` (:720-733)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `max_hp` | int | S | :722 | |
| `current_hp` | int | S | :723 | |
| `powers` | list<power> | S | :724 | §3.14 |
| `energy` | int | S | :725 | `EnergyPanel.totalCount` |
| `block` | int | S | :726 | |
| `orbs` | list<orb> | S | :731 | §3.18 |

### 3.16 Relic — `convertRelicToJson` (:830-836)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `id` | str | S | :832 | `relicId` |
| `name` | str | I (localization) | :833 | |
| `counter` | int | S | :834 | relic counter (some are RNG-relevant run state; the value itself is emitted) |
| `price` | int | S | :351 | present only on shop relics (§3.9) |

### 3.17 Potion — `convertPotionToJson` (:849-862)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `id` | str | S | :851 | (`PotionSlot` for empty slots) |
| `name` | str | I (localization) | :852 | |
| `can_use` | bool | S | :858 | legality |
| `can_discard` | bool | S | :859 | |
| `requires_target` | bool | S | :860 | `isThrown` |
| `price` | int | S | :356 | present only on shop potions (§3.9) |

### 3.18 Orb — `convertOrbToJson` (:874-881)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `id` | str | S | :876 | |
| `name` | str | I (localization) | :877 | |
| `evoke_amount` | int | S | :878 | |
| `passive_amount` | int | S | :879 | |

### 3.19 Other `screen_state`s (grid / hand-select / game-over)

These arise from screens CommunicationMod supports; all appear under
`screen_state` per `getScreenState` (:461-487).

**GRID — `getGridState` (:379-404):** `cards` (S, :395), `selected_cards`
(S, :396), `num_cards` (S, :397), `any_number` (S, :398), `for_upgrade`
(S, :399), `for_transform` (S, :400), `for_purge` (S, :401), `confirm_up`
(S, :402).

**HAND_SELECT — `getHandSelectState` (:414-433):** `hand` (S, :424),
`selected` (S, :429), `max_cards` (S, :430), `can_pick_zero` (S, :431).

**GAME_OVER — `getGameOverState` (:441-455):** `score`
(I — out-of-model presentation meta, :452), `victory` (S — terminal outcome,
:453).

## 4. Coverage accounting (acceptance)

- Total `.put("…")` emission sites in `GameStateConverter.java`: **155 in
  upstream v1.2.1 at B0.1**
  (`grep -cE '\.put\("[^"]+"' GameStateConverter.java`). This count includes
  branch duplicates (e.g. `chest_type` in two `instanceof` arms, `event_id`
  four times, `body_text` twice, `move_adjusted_damage` twice, `price` on three
  shop item kinds). **The figure is upstream's, not the fork's, and is kept as
  the provenance baseline it was measured as** — the fork has added emission
  sites in every wave since (the whole `oracle` block, the Bottled trio,
  S3.21's key/dungeon/`misc_field`/`has_emerald_key` rows). Re-derive the
  fork's own current figure with the same command rather than quoting one from
  here; it is a number nothing re-checks.
- Distinct JSON key names emitted: **116 in upstream v1.2.1 at B0.1**, same
  caveat.
- Distinct **(container, key)** fields catalogued in §3 (each with a
  disposition): **141** — one row per field a given converter can emit,
  covering every one of the 155 sites.
- `GameStateConverter.java` is the **only** source file that emits game-state
  JSON; the sole other `.put("…")` site in the mod is the two-key error object
  in `CommunicationMod.java:99-101` (documented in §1.4, disposition I).

Verification command used:
`grep -noE '\.put\("[^"]+"' GameStateConverter.java` — every listed key/line has
a corresponding §3 row and disposition.

Disposition summary: the great majority are **schema-mapped**; **ignored** are
localization strings (`name`/`body_text`/`event_name`/option `text`/`label`),
the nondeterministic card `uuid`, protocol-plumbing (`ready_for_command`,
`in_game`, `current_action`, error keys), and S2-scope (`boss_reward.relics`,
`combat_reward … link`, `score`); **oracle-block** are `draw_pile` (order) and
monster `last_move_id`/`second_last_move_id` (move history), each cross-
referenced to design §2.5; **display-derived (D)** are monster `intent` and
`move_adjusted_damage` (banner values computed from the `move_id` anchor; §3.12,
design §11 v0.1.2 / B1.3).

## 5. The `"oracle"` state block (fork addition, B1.2)

The `CommunicationMod-oracle` fork appends one extra key, **`oracle`**, to the
`game_state` object (GameStateConverter §3.2) on every in-dungeon dump. It
carries the hidden RNG/pity/pool state the stock converter cannot see but that
bit-exact differential testing needs — the frozen inventory of design §2.5.

- **Gate.** Emitted only when the fork config flag `oracleBlock` is true
  (`config.properties`; default `true`, also a mod-settings toggle). When false
  the fork's output is byte-identical to stock (no `oracle` key) — this is how
  B1.3 proves the rendering-strip patches don't perturb the dump.
- **Scope.** Stock consumers use the stock jar and never see this key; the fork
  is a distinct modid (`CommunicationMod-oracle`).
- **Provenance.** Every field's game source is cited in the B1.2 commit body.
  Emitter: `GameStateConverter.getOracleState()` / `rngToJson()`.

### 5.1 `oracle` field catalog

| Field | Type | §2.5 row | Source | Note |
|---|---|---|---|---|
| `seed` | long | 10 | `Settings.seed` | run seed (signed long; base-35 via `start`) |
| `floor` | int | 10 | `AbstractDungeon.floorNum` | anchor |
| `act` | int | 10 | `AbstractDungeon.actNum` | anchor |
| `ascension` | int | 10 | `AbstractDungeon.ascensionLevel` | anchor |
| `dungeonId` | str | S3.21 | `AbstractDungeon.id` | **the dungeon IDENTITY, not its ordinal** — `"Exordium"`/`"TheCity"`/`"TheBeyond"`/`"TheEnding"`. It is the string the game itself branches on (DungeonMap.java:68's `id.equals("TheEnding")`, `getShrine`'s SecretPortal arm), so the differ reads the same key the rules do. No schema field: it is **cross-checked against the `act` anchor** and a disagreement aborts the translation. §5.6 |
| `hasRubyKey` | bool | S3.21 | `Settings.hasRubyKey` | §5.6. Mapped into `RunState::keys` (`kKeyRuby`) and **compared**, on any record that carries the block |
| `hasEmeraldKey` | bool | S3.21 | `Settings.hasEmeraldKey` | §5.6, `kKeyEmerald` |
| `hasSapphireKey` | bool | S3.21 | `Settings.hasSapphireKey` | §5.6, `kKeySapphire` |
| `isFinalActAvailable` | bool | S3.21 | `Settings.isFinalActAvailable` | §5.6. The fourth conjunct of the same gate (SpireHeart.java:151, MonsterRoomElite.java:90). A **profile** unlock, not run state — the sanctioned save has it true (design §1.1) — so it has no schema home and is never diffed; it is emitted so a capture taken on a locked profile is visible in the artifact rather than only in a divergence |
| `playtime` | float | 10 | `OraclePlaytimePinPatch.effectivePlaytime()` | s2-design §5 trap 5 duty. **The EFFECTIVE wall clock the SecretPortal gate read**, not the raw `CardCrawlGame.playtime` — since 2026-08-27 (S2.43) the fork pins that gate, so this is `0.0f` whenever `oraclePlaytimePin` is on and the true `CardCrawlGame.playtime` when it is off; **§5.4** has the contract. Still NEVER diffed and still `oracle` — but READ since 2026-08-27: `--replay`/`--event` hand it to `RunController::playtime_seconds` so an Act-3 `getShrine` draw picks the index the game picked. The engine's own default is 0.0f, i.e. the pin |
| `streams` | object | 1-2 | see §5.2 | the 14 RNG streams |
| `cardBlizzRandomizer` | int | 3 | `AbstractDungeon.cardBlizzRandomizer` | card-reward rarity pity offset |
| `blizzardPotionMod` | int | 4 | `AbstractRoom.blizzardPotionMod` | potion-drop ratchet (±10) |
| `eventPity` | object | 5 | `EventHelper` | `{monster,shop,treasure}` floats — `?`-room pity chances; `MONSTER_CHANCE`/`SHOP_CHANCE` read by reflection (private static), `TREASURE_CHANCE` public |
| `purgeCost` | int | 6 | `ShopScreen.purgeCost` | run-persistent card-removal ramp (75 +25/purge). The relic-adjusted `actualPurgeCost` is already the stock `screen_state.purge_cost` (§3.9) |
| `eventList` | list<str> | 7 | `AbstractDungeon.eventList` | remaining Exordium events (removed on use) |
| `shrineList` | list<str> | 7 | `AbstractDungeon.shrineList` | remaining shrines |
| `specialOneTimeEventList` | list<str> | 7 | `AbstractDungeon.specialOneTimeEventList` | remaining shared one-time events |
| `encounterLists` | object | B5.2 | `AbstractDungeon.{monsterList,eliteMonsterList,bossList}` | `{monster,elite,boss}`: the live remaining encounter-key order. Monster and elite entries are removed from the front when a room settles; the boss list retains the shuffled act order |
| `relicPools` | object | 8 | `AbstractDungeon.{common,uncommon,rare,shop,boss}RelicPool` | `{common,uncommon,rare,shop,boss}`: each the live shuffled pool **order** (front popped for rewards, end for shop) |
| `monster_move_history` | list | 9 | `AbstractMonster.moveHistory` | present only in COMBAT: `[{id, move_history:[byte,…]}]`, one per monster in room order (full history, not stock's 2-back) |

### 5.2 `oracle.streams` — the 14 RNG streams

`streams` maps each stream name to `{counter, s0, s1}` (or the key is **absent**
when the stream is still null — pre-init, e.g. `neowRng` before the blessing
screen). `counter` is `Random.counter` (save-parity draw count); `s0`/`s1` are
the raw xorshift128+ state `RandomXS128.getState(0)`/`getState(1)` (seed0/seed1),
emitted as **signed** Java longs. Provenance: `Random.java:17-18`
(`public RandomXS128 random; public int counter`), `RandomXS128.getState`.

- **Run-scoped (7):** `monsterRng`, `eventRng`, `merchantRng`, `cardRng`,
  `treasureRng`, `relicRng`, `potionRng`.
- **Floor-scoped (5):** `monsterHpRng`, `aiRng`, `shuffleRng`, `cardRandomRng`,
  `miscRng` — each reseeded `Random(Settings.seed + floorNum)` on room entry
  (AbstractDungeon.java:1747-1751), i.e. the sim's `floor_stream(seed, floor)`.
- **Act-scoped (1):** `mapRng`.
- **Event-scoped (1):** `neowRng` = `NeowEvent.rng`, a fresh
  `Random(Settings.seed)` created at the blessing screen (NeowEvent.java:289/363).

### 5.3 Verified at B1.2 (one scripted run each, Windows host)

- `relicRng.counter == 5` at first in-dungeon dump (the 5 init pool shuffles,
  AbstractDungeon.java:1237-1241).
- Floor-scoped `(s0,s1)` at floors 1-3 == sim `floor_stream(STS12345, N)`
  bit-for-bit (read off `cardRandomRng` at `counter==0`).
- `blizzardPotionMod` ratchets across combat rewards (0→10→0→10 over floors 1-3).
- `eventList` shrinks 11→10 when an event fires (floor-5 `?`→"Liars Game").

### 5.4 The SecretPortal playtime pin (fork behavior change, S2.43 — 2026-08-27)

**The contract is the patched fork, not the retail client.** This is the same
oracle-contract shape as the Discovery wasted-regens boundary and the
Explosive-Potion THORNS boundary: where a retail behavior is not a function of
`(seed, actions)`, the fork is what moves, and the capture is scored against the
fork.

**What retail does.** `AbstractDungeon.getShrine` builds a candidate list `tmp`
and draws `tmp.get(rng.random(tmp.size() - 1))` (`AbstractDungeon.java:1937`).
One candidate is gated on wall clock:

```java
case "SecretPortal":
    if (!(CardCrawlGame.playtime >= 800.0f) || !id.equals("TheBeyond"))
        continue block22;              // AbstractDungeon.java:1929-1933
```

An omitted candidate does not go merely *unseen* — it **shortens `tmp` and moves
the drawn index**. The simulator pins that predicate false (`event_framework.hpp`,
the PLAYTIME block; s2-design §5 trap 5), so before this patch **every Act-3 `?`
room past 800 s of live wall clock resolved to a different event than the
sim-emitted script expected**, and the capture desynced from its own script
mid-run. Live witnesses: STS108107 at 924.34705 s and STS153269 at 960.92236 s.

**What the fork now does.** `patches/OraclePlaytimePinPatch` is a
`@SpireInstrumentPatch` on `AbstractDungeon.getShrine(Random)` whose `ExprEditor`
replaces **that one `getstatic CardCrawlGame.playtime`** with
`OraclePlaytimePinPatch.effectivePlaytime()`, which returns `0.0f` (the sim's
`kUnmodelledPlaytimeSeconds`) while the pin is on. The gate is therefore shut for
the whole run, exactly as the simulator has it, and a scripted deep capture stays
reproducible at any depth. Trap 5's sim-side pin is now **mirrored** by the
capture side instead of diverging from it.

**The anchor stays truthful.** `oracle.playtime` (§5.1) is emitted from the *same*
`effectivePlaytime()` helper (`GameStateConverter.getOracleState`), so the capture
records the **effective** value the gate saw rather than a wall clock the gate
never consulted. Gate and anchor are one function and cannot disagree; `--replay`
feeds the recorded value to `RunController::playtime_seconds`, so the replay's
gate input is exactly the game's gate input. The field is still dispositioned
`oracle` and still never diffed.

**Blast radius — every `CardCrawlGame.playtime` reader in the 12-18-2022 tree,
and its disposition.** Only the first row is patched; the instrument patch is
scoped to `getShrine`, so the field itself still accumulates normally and the
other eight readers see the true wall clock.

| Reader | What it does | Disposition |
|---|---|---|
| `AbstractDungeon.java:1930` | the SecretPortal `>= 800.0f` shrine gate | **PATCHED** — the whole point; it is the only reader that branches seeded-RNG-visible state |
| `AbstractDungeon.java:2001` | `playtime += Gdx.graphics.getDeltaTime()` under `!CardCrawlGame.stopClock` — the sole accumulator | untouched; the field still tracks real wall clock (deliberately: nothing else is perturbed) |
| `AbstractDungeon.java:2601`, `CardCrawlGame.java:599`, `:1246`, `:891` | resets to `0.0f` at floor ≤ 1 / new game / main-menu return, and reload from `saveFile.play_time` | untouched; unaffected by a read-site patch |
| `SaveFile.java:190`, `:349` | `play_time` / `metric_playtime` written into the save | untouched — the save keeps the true elapsed time, so a save/continue restores the same clock the game had |
| `Metrics.java:117` | `addData("playtime", …)` in the run-metrics upload | untouched; telemetry only, and no campaign run uploads |
| `AbstractMonster.java:1063` | `playtime <= 1200.0f` → `UnlockTracker.unlockAchievement("SPEED_CLIMBER")` on the final-act boss kill | untouched. This is the ONLY other reader that does anything but present a number, and holding the *field* at zero would have made it fire spuriously — the read-site patch is what avoids that. It is also profile-side (achievement), never a run-pool gate (design §1.1) |
| `DeathScreen.java:74`, `VictoryScreen.java:73`, `GameOverScreen.java:287`, `:379-383` | end-of-run timing display, the fastest-win leaderboard upload, score-bonus tiers | untouched; presentation/leaderboard, outside the dumped state |
| `SaveSlot.java:79-87`, `RunHistoryScreen.java:1103`, `StatsScreen.updateVictoryTime`/`incrementPlayTime` | main-menu slot time, run-history header, profile stats | untouched; presentation |

**The flag.** `oraclePlaytimePin` in the same `SpireConfig("CommunicationMod",
"config")` store, **default `true`**, also a mod-settings toggle. The campaign
orchestrator's generated `config.properties` does not name it, so it falls
through to that default (`SpireConfig` layers the file over the defaults
`Properties`). With it **false**, both the gate and the anchor read the real
`CardCrawlGame.playtime` again — i.e. the pre-2026-08-27 fork bit for bit — so
this flag, not only the three strip flags, must be off to reproduce the
stock-equivalence baseline (README-oracle "Rendering-strip" section).

**Verified without launching the game.** ModTheSpire's `InstrumentPatchInfo
.doPatch` is exactly `Method.invoke(null)` → `(ExprEditor)` →
`CtBehavior.instrument(editor)`. Running that same sequence with the patch's own
`Instrument()` against the real `AbstractDungeon` bytecode from
`desktop-1.0.jar` shows: exactly **1** `CardCrawlGame.playtime` read in
`getShrine` before, `instrumentedReads == 1`, **0** reads and exactly **1**
`effectivePlaytime()` call after, `AbstractDungeon.update`'s read/write pair
unchanged, and the patched class still compiling to bytecode. The editor also
logs one line per replacement, so a game-version drift that silently matched
nothing shows up in `mts_launch<N>.log` rather than only in a divergent capture.

### 5.5 The Courier restock seed (fork behavior change, S3.24 — 2026-09-03)

**The contract is the patched fork, not the retail client** — the same
oracle-contract shape as §5.4's playtime pin, the Discovery wasted-regens
boundary and the Explosive-Potion THORNS boundary: where a retail behaviour is
not a function of `(seed, actions)`, the fork is what moves, and the capture is
scored against the fork.

**What retail does.** With **The Courier** owned, buying a card replaces it in
place (`ShopScreen.purchaseCard`):

```java
AbstractCard c = AbstractDungeon.getCardFromPool(
        AbstractDungeon.rollRarity(), hoveredCard.type, false).makeCopy();
//                                                      ^^^^^ ShopScreen.java:615
```

`rollRarity()` is one seeded `cardRng.random(99)`, so the restocked card's
**rarity** is reproducible. Its **identity** is not: `useRng = false` sends
`CardGroup.getRandomCard(CardType, boolean)` (CardGroup.java:540-553) down its
`MathUtils.random(tmp.size() - 1)` branch — libGDX's **global** `RandomXS128`,
seeded from JVM-startup entropy and advanced by rendering and VFX draws. It was
the last value in scope outside `(seed, actions)`. The simulator's answer since
S1 was a named refusal: the slot restocked as an unnameable sentinel
(`kShopRestockedUnknownCard`) that was kept off the legal-action mask and whose
purchase was refused byte-stably.

**What the fork now does.** `patches/CourierRestockSeedPatch` is a
`@SpireInstrumentPatch` on `ShopScreen.purchaseCard(AbstractCard)` whose
`ExprEditor` replaces **the two `AbstractDungeon.getCardFromPool` calls at
ShopScreen.java:615-617** (the restock draw and its dead
`while (c.color == COLORLESS)` re-roll guard) — and only those — with
`CourierRestockSeedPatch.restockCardFromPool(rarity, type, useRng)`. The helper
reproduces `getCardFromPool`'s pool walk exactly (AbstractDungeon.java:1538-1576,
including the switch's deliberate downward fallthroughs and the POWER type's
upward recursion) and takes its one index draw from a **dedicated seeded
stream** instead of `MathUtils.random`.

**The seeding — the contract both sides implement.**

```
new Random(Settings.seed + 1000003L + AbstractDungeon.cardRng.counter)
```

then **one** `random(size - 1)` on it, over the type-filtered,
`Collections.sort`ed rarity view. The simulator's identical construction is
`courier_restock_stream(run_seed, card_rng.counter)` in
`include/sts/engine/shop.hpp` (`kCourierRestockSeedOffset = 1000003`), called
from the colored-restock branch of `src/engine/shop.cpp`, which then walks the
pool with the shared `shop_card_from_pool`. Four properties, each load-bearing:

1. **The counter is read AFTER the restock's own `rollRarity()` draw.** Java
   evaluates the argument before the invocation, so `cardRng.counter` is
   already bumped when the helper runs — which is exactly where the simulator
   reads `rs.card_rng.counter`.
2. **Successive restocks never repeat**: every colored restock spends one
   `rollRarity` draw on `cardRng` first, so the counter strictly increases
   between them. This matters more after S3.24 than before it, because a
   restocked slot is now buyable and one slot can restock repeatedly in a visit.
3. **No stored stream moves.** The restock stream is constructed at the draw and
   discarded. It is not a game stream, never enters a save file or
   `oracle.streams`, and adds no `RunState` byte — `SCHEMA_VERSION` does not
   move, and `cardRng`/`merchantRng`/`potionRng` advance per restock exactly as
   the wave2cap_courier_* campaign measured them.
4. **The offset cannot alias another derived stream.** The game's derived
   streams sit at `seed + floorNum` (0..~60) and `seed + act*300`
   ({300, 600, 900, 1200}); 1000003 is past both for any counter a run reaches,
   and `RandomXS128`'s constructor murmur-scrambles the seed, so adjacent
   counters give uncorrelated streams. **The offset is FROZEN**: changing it
   changes every restocked identity, and it must change on both sides in one
   commit or never.

The draw is **distributionally exact** against retail: same view, same inclusive
`random(size - 1)` bound, same "an empty view returns null before it indexes
anything, and therefore costs no draw" rule (CardGroup.java:545-547). Only the
source of the index differs.

**The flag.** `oracleCourierRestockSeed`, in the same
`SpireConfig("CommunicationMod", "config")` store as `oraclePlaytimePin`,
**default `true`**, also a mod-settings toggle. With it **false** the helper
defers straight to `AbstractDungeon.getCardFromPool(rarity, type, useRng)` —
retail bit for bit — so this flag joins `oraclePlaytimePin` and the three strip
flags in the set that must be off to reproduce the stock-equivalence baseline
(README-oracle "Rendering-strip"). The campaign orchestrator's generated
`config.properties` does not name it, so it falls through to that default
(`SpireConfig` layers the file over the defaults `Properties`).

**What it changes for the differ.** A Courier shop's restocked colored row now
has an identity the sim can name, so `replay_run_diff --shop`'s stock/purchase
walk compares it like any other row instead of skipping it, and the row is on
the legal-action mask — a `--replay` script may now buy it.

**Verified without launching the game.** ModTheSpire's
`InstrumentPatchInfo.doPatch` is exactly `Method.invoke(null)` → `(ExprEditor)`
→ `CtBehavior.instrument(editor)`. Running that same sequence with this patch's
own `Instrument()` against the real `ShopScreen` bytecode from `desktop-1.0.jar`
gives **2** `getCardFromPool` calls in `purchaseCard` before,
`instrumentedCalls == 2`, **0** `getCardFromPool` and **2**
`restockCardFromPool` after, `getColorlessCardFromPool` still **1** and
`setPrice` still **2**, and the patched class still recompiling to bytecode
(38356 bytes). The editor logs one line per replacement, so a game-version drift
that silently matched nothing shows up in `mts_launch<N>.log` rather than only
in a divergent capture.

### 5.6 Oracle contract v2 — keys, the dungeon id, Act 4, and the tagged `misc` union (S3.21 — 2026-09-03)

The single S3 fork redeploy. Four additions, all **purely additive**: every one
is either a new key or a key emitted only when a flag is set, so a capture made
by an earlier fork build translates and replays byte-for-byte as it did. The
`oracleBlock` gate is unchanged — with it off, the fork's output is still
byte-identical to stock.

#### (a) The three keys, and the dungeon identity

`oracle.hasRubyKey` / `hasEmeraldKey` / `hasSapphireKey` / `isFinalActAvailable`
(Settings.java:64-67) and `oracle.dungeonId` (`AbstractDungeon.id`); catalog
rows in §5.1. The keys live in `Settings`, not in `AbstractDungeon`, so **no
stock field exposed them** and the differ could previously only infer key state
from a claim record it might not even have captured. Four separate rules read
them: the campfire Recall gate (CampfireUI.java:94-96), the burning-elite reward
row (MonsterRoomElite.java:90), the sapphire chest's linked row
(AbstractRoom.java:545-547) and the `Spire Heart` branch (SpireHeart.java:151)
that decides whether a run can reach Act 4 at all — plus the highest-risk one,
`setEmeraldElite`'s `!Settings.hasEmeraldKey` guard (AbstractDungeon.java:543),
which **removes a `mapRng` draw from every act generated after the key is
taken**.

The translator maps the three booleans into `RunState::keys`
(`kKeyRuby`/`kKeyEmerald`/`kKeySapphire`) and **the replay differ compares the
field**. It is compared as a PAIR, exactly like `boss_chest`: a record that
carries the block is compared, a record that does not has the field neutralized
on both sides. That gate is not caution for its own sake —
`act1_a20_50/STS71037` seq 83 opens a LargeChest offering
`RELIC Mummified Hand` + `SAPPHIRE_KEY` and answers `choose 1`, **taking the
key**, so the S3.11 run layer rightly sets `kKeySapphire` there while the
pre-redeploy capture has nothing to say. Comparing that unconditionally would
score a correct simulator against an absent claim.

`dungeonId` gets no schema field — `act` already names acts 1-3 and Act 4 is
act 4 — but it is not merely deferred either: it is **cross-checked against the
`act` anchor** (`Exordium`/`TheCity`/`TheBeyond`/`TheEnding` for 1/2/3/4) and a
disagreement aborts the translation, because it is the string the game itself
branches on.

#### (b) Act 4 — what is already generic, and what was actually missing

`GameStateConverter` is dungeon-agnostic almost everywhere, so most of Act 4
needed **verification, not implementation**. Read from the fork source and the
12-18-2022 decompile:

| Act-4 surface | Emission | Status |
|---|---|---|
| `TheEnding` dungeon id | `AbstractDungeon.id` was emitted **nowhere** | **ADDED** as `oracle.dungeonId` — the one real gap |
| `act == 4` | `game_state.act` / `oracle.act` (:109) | generic, already correct |
| the boss key `The Heart` | `game_state.act_boss` = `AbstractDungeon.bossKey` (:110) | generic; `TheEnding.initializeBoss` fills `bossList` with no RNG (TheEnding.java:188-196) |
| the special 5×7 map | `convertMapToJson` (:702) | generic — **four** nodes, (3,0)…(3,3). The `TrueVictoryRoom` node (3,4) has no edge and is therefore not emitted; see the §3.11 note, which is verified retail behaviour and deliberately unchanged |
| the `Spire Heart` screens | `getEventState` reads the event class's static `ID` by reflection (:343-355); `SpireHeart.ID` is `"Spire Heart"` (SpireHeart.java:47) | generic, already correct. `VictoryRoom.phase == EVENT` (VictoryRoom.java:21-33) puts `ChoiceScreenUtils.getCurrentChoiceType()` on its `EVENT` arm (:72), so `screen_type` is `EVENT` and the four clicks arrive as ordinary one-option event records |
| the `TrueVictoryRoom` terminal | `room_type` = `"TrueVictoryRoom"`, `screen_name` = `"NO_INTERACT"` (TrueVictoryRoom.java:26-31), `screen_type` = `NONE` (ChoiceScreenUtils' `default` arm, :109-110) | generic; the room is non-interactive by construction, and `NO_INTERACT` needs no `ChoiceType` |
| the Door | `screen_name` = `"DOOR_UNLOCK"` (SpireHeart.java:94-98), `screen_type` = `NONE` | generic, same `default` arm |

The repo side matches: the translator now recognises `"Spire Heart"` as a
**non-pool sentinel event id** — the second one after Neow's, and for the same
reason (the event is constructed by its room and is a member of no act
event/shrine/special list, so giving it a pool `EventId` would put a non-pool
entry into the three membership bitsets that pool ids index). Before this, that
id **aborted** translation, which is why every three-act victory capture's
four-click tail had to be dropped. It also handles Act 4's
**empty-by-construction** `eventList` and `shrineList` (`TheEnding` overrides
both initialisers with empty bodies, TheEnding.java:198-200, :211-213): reading
them through the act-local "initially present, now absent ⇒ fired" derivation
would have marked all eleven Exordium events and all six shrines FIRED off two
empty arrays.

#### (c) The tagged `misc` union

`power.misc_field` — §3.14 has the row and the rationale. This discharges the
stage-b deferred obligation "translator: power `misc` fields other than
player-owned Combust (the five-way untagged union)".

#### (d) The Courier patch rides this jar

§5.5. `oracleCourierRestockSeed` needs no orchestrator config change; an older
`config.properties` falls through to the `Properties` default, i.e. ON.

#### The equivalence baseline, restated

Five fork flags must be **off** to reproduce stock bit for bit: the three
rendering-strip flags, `oraclePlaytimePin` (§5.4) and `oracleCourierRestockSeed`
(§5.5). S3.21's own additions need no flag of their own: they are emissions
inside the existing `oracleBlock` gate (the keys and `dungeonId`) or
emitted-only-when-set fields (`has_emerald_key`, `misc_field`), and none of them
changes a single game rule.

