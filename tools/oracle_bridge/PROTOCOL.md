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
| `rewards[].link` | relic | I (S2 scope) | :283 | SAPPHIRE_KEY link — keys are final-act-gated, out of S1 (design §1.1) |

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
| `relics` | list<relic> | I (S2 scope) | :326 | boss-relic pick — the run terminates at act-1 boss combat rewards, before the boss chest (design §1.1 "Out"); documented for completeness |

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

### 3.12 Monster — `convertMonsterToJson` (:666-705)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `id` | str | S | :668 | monster id |
| `name` | str | I (localization) | :669 | |
| `current_hp` | int | S | :670 | |
| `max_hp` | int | S | :671 | |
| `intent` | str | S | :673,675 | `AbstractMonster.Intent` name — forced `NONE` under Runic Dome (:672; a boss relic, S2-typical, noted) |
| `move_id` | int | S | :678 | current `EnemyMoveInfo.nextMove` byte |
| `move_base_damage` | int | S | :679 | |
| `move_adjusted_damage` | int | S | :682,684 | shown intent damage |
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

### 3.14 Power — `convertCreaturePowersToJson` (:770-819)

| Field | Type | Disposition | Source | Note |
|---|---|---|---|---|
| `id` | str | S | :774 | |
| `name` | str | I (localization) | :775 | |
| `amount` | int | S | :776 | |
| `damage` | int | S | :779 | optional (`damage` field, when present) |
| `card` | card | S | :783 | optional nested card (Nightmare etc.) |
| `misc` | int | S | :797 | optional; first of basePower/maxAmt/storedAmount/hpLoss/cardsDoubledThisTurn present |
| `just_applied` | bool | S | :811 | optional; `justApplied`/`skipFirst` |

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

- Total `.put("…")` emission sites in `GameStateConverter.java`: **155**
  (`grep -cE '\.put\("[^"]+"' GameStateConverter.java`). This count includes
  branch duplicates (e.g. `chest_type` in two `instanceof` arms, `event_id`
  four times, `body_text` twice, `move_adjusted_damage` twice, `price` on three
  shop item kinds).
- Distinct JSON key names emitted: **116**.
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
referenced to design §2.5.

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
| `streams` | object | 1-2 | see §5.2 | the 14 RNG streams |
| `cardBlizzRandomizer` | int | 3 | `AbstractDungeon.cardBlizzRandomizer` | card-reward rarity pity offset |
| `blizzardPotionMod` | int | 4 | `AbstractRoom.blizzardPotionMod` | potion-drop ratchet (±10) |
| `eventPity` | object | 5 | `EventHelper` | `{monster,shop,treasure}` floats — `?`-room pity chances; `MONSTER_CHANCE`/`SHOP_CHANCE` read by reflection (private static), `TREASURE_CHANCE` public |
| `purgeCost` | int | 6 | `ShopScreen.purgeCost` | run-persistent card-removal ramp (75 +25/purge). The relic-adjusted `actualPurgeCost` is already the stock `screen_state.purge_cost` (§3.9) |
| `eventList` | list<str> | 7 | `AbstractDungeon.eventList` | remaining Exordium events (removed on use) |
| `shrineList` | list<str> | 7 | `AbstractDungeon.shrineList` | remaining shrines |
| `specialOneTimeEventList` | list<str> | 7 | `AbstractDungeon.specialOneTimeEventList` | remaining shared one-time events |
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
