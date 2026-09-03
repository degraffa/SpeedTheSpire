# S2 Task Ledger — Acts 2–3 (TheCity + TheBeyond)

Execution tracker for [s2-design.md](s2-design.md) (the S2 scope +
verification spec — this file never overrides it; on conflict the design doc
wins and this file gets fixed). [stage-a-design.md](stage-a-design.md) and
[stage-b-design.md](stage-b-design.md) remain frozen and in force for
everything they cover; [conventions.md](conventions.md) is binding on every
task here exactly as it was for Stage B. Authored by TE.2
([training-tasks.md](training-tasks.md)); the training program's Phase T4 is
blocked on this ledger's exit gate S2-G2.

**This file holds only what is open.** When tasks land, their blocks gain
Log lines; large completed blocks move to an `s2-log.md` archive once one
exists, mirroring the Stage B convention.

## Orchestrator protocol

- Statuses: `[ ]` todo · `[~]` in progress · `[x]` done · `[!]` blocked.
- One sub-agent per task, self-contained brief, own worktree via
  `tools/task_worktree.sh create <task>` (from Windows); orchestrator
  re-verifies and lands on `master` — one task = one commit. Model choice
  per CLAUDE.md.
- A task is **done only when its Acceptance block passes** — run the
  commands, don't infer. Registry YAML is code: entries land with their
  tier-2 tests in one commit.
- Respect `Deps:`; ∥ marks parallel-safe groups (disjoint deliverables).
  Gates **S2-G1 / S2-G2** are stop-the-line for their phase. The gate
  namespace is ledger-local on purpose: the G-series stays reserved for
  Stage C planning (G7's closing note), the GT-series is the training
  ledger's.
- **Cross-ledger shared namespaces** (`RunPhase`, fuzz `MoveCat`,
  `MonsterIntent`, opcodes, `PowerId`/`CardId`/… blocks) remain allocated
  in [stage-b-tasks.md](stage-b-tasks.md) "Shared namespaces — allocation
  now in force" — the single authority. This ledger records only which S2
  task holds which granted block; claim there first, record here second.
- Task body prose before the `**Deps:**` line is the block's Deliverables
  field.
- Capacity rule (training-plan §4.4) still applies: S2 authoring never
  starves open T0.x work; content authoring has zero dependency on
  training results.

## Registry id blocks granted to Wave 1

Granted by TE.2 at ledger creation, recorded per the stage-b protocol
(append-only; unspent ids gap, never backfill; re-derive current maxima
from `registry/*.yaml` before extending):

| Domain | Block | Holder |
|---|---|---|
| `encounters.yaml` | 22–43 (Act 2), 44–61 (Act 3) | S2.01 |
| `events.yaml` | 32–44 (Act-2 list), 45–51 (Act-3 list) | S2.02 |
| `relics.yaml` | 143–150 (151–154 returned unissued — design's ~10 was 8) | S2.03 |
| `cards.yaml` | 128–132 used, 133 reserve | S2.03 |
| `monsters.yaml` | 27–48 (Act 2), 49–66 (Act 3) | S2.2x batches, sub-blocks at dispatch |
| `powers.yaml` | 93–135 | S2.2x batches, sub-blocks at dispatch |

Wave-2 sub-blocks (granted 2026-08-07; full table incl. RunPhase/MoveCat/
RoomType/opcode/hook claims and adjudications is in
[stage-b-tasks.md](stage-b-tasks.md) "S2 Wave-2 allocations"):
`monsters.yaml` S2.21 = 27–30 (**31 released unspent — permanent gap**),
S2.22 = 32–36;
`powers.yaml` S2.21 = 93–94 (HEX, FLIGHT — **both spent**), S2.22 = 95
(MALLEABLE — row ownership corrected from the S2.21 block text, which
stale-listed Malleable/PlatedArmor/Barricade; the design doc wins, and the
S2.21 block text has now been corrected in place).

## Deferred obligations

Same semantics as the Stage B table (live carrier; discharge in place).

| Obligation | Deferred by | Owner task | Detail |
|---|---|---|---|
| Gremlin move-99 escape (`EscapeAction` body + `deathReact`/`escapeNext` trigger) | B3.16 (stage-b table: "UNASSIGNED — Act-2 owner") | S2.23 | **DISCHARGED by S2.23 (2026-08-07) — as a FINDING on one half and a third PRODUCER on the other; re-derived, not inherited.** (a) The `EscapeAction` BODY was in fact already landed in Act 1, by S1's Looter, as `Opcode::ESCAPE` (40) + `kMonsterFlagEscaped` (bit 24); S2.23 adds a THIRD producer of it, `GremlinLeader.die()` (GremlinLeader.java:224-241), through the new `MonsterDieAfterFn` slot — one queued ESCAPE per non-dying record, the leader excluding ITSELF only because `super.die()` ran first. That is the escape the Gremlin Leader's minions actually experience. (b) The `deathReact`/`escapeNext` TRIGGER — and therefore gremlin move 99 — remains **UNREACHABLE IN EVERY ACT** and stays unmodelled. Evidence, from `grep -rn "deathReact()\|escapeNext()\|new EscapeAction" com/` with each hit read: `escapeNext()` has NO caller anywhere in the tree; the only `deathReact()` call is `BanditBear.java:131`, whose group is Bandits (BanditPointy/BanditLeader/BanditBear, MonsterHelper.java:513-515) and contains no gremlin; the leader's fan-out queues `new EscapeAction(m)` DIRECTLY and never enters `case 99` or telegraphs `Intent.ESCAPE`. **The `deathReact` obligation is RE-POINTED, not closed:** it is live for `BanditLeader` (:82) and `BanditPointy` (:70) in the Act-2 "Masked Bandits" event combat (encounters.yaml id 41), and its owner is the **S2.31/S2.32 event-combat owner**, not this batch. Consequence recorded at the code: `BLOCK_RANDOM_MONSTER`'s valid-list filter reads the TELEGRAPHED intent and `isDying`, never the escaped flag (GainBlockRandomMonsterAction.java:26-38), so a leader-fan-out escapee is still a legal block recipient — in the engine AND in the game. Checked and deliberately left exact rather than "improved" into `monster_dead_or_escaped`; pinned by `CityElites.AnEscapedGremlinNeverTelegraphsEscapeIntent`. Wording amended in place at `monster_gremlin.hpp` note (1) and `combat_state.hpp`'s `kMonsterFlagEscaped` comment, both of which said "unreachable in Act 1". The stage-b row (docs/stage-b-tasks.md) is marked DISCHARGED in the same commit and points here. **THE RE-POINTED HALF IS DISCHARGED by S2.32 (2026-08-09) — a verified negative at the only live trigger.** BanditBear.die() (BanditBear.java:127-133) is the tree's ONE deathReact caller; both overrides it can reach (BanditLeader.java:82-86, BanditPointy.java:70-74) queue ONE TalkAction each behind `!isDeadOrEscaped()` and nothing else, and the base body is empty (AbstractMonster.java:912-913) — pure presentation, so the Bear registers NO die fn on either edge (explicit nullptrs at monster_dispatch.cpp) and gremlin move 99 stays unreachable in every act. Pinned by `CityEventsII.BearDeathReactIsPresentationOnly`; the stage-b twin row carries the same discharge |
| `JawWorm(..., true)` constructor variant semantics | TE.2 scope pass | S2.26 | **DISCHARGED — the boolean changes EXACTLY TWO things and NEITHER is a stat** (2026-08-09, S2.26). `JawWorm.java:71-110` read in full: the 2-arg ctor delegates with `hard = false` and the 3-arg one has exactly one caller in the game, MonsterHelper's Jaw Worm Horde, which builds three worms with `true` (MonsterHelper.java:549-550). It sets (a) `firstMove = false` (:77-79), which suppresses the forced opening CHOMP so the opening telegraph runs the full getMove num-tree against an EMPTY move history — and the DRAW COUNT is unchanged, because every arm's history predicate is false on an empty history, so no tiebreak `randomBoolean` is reached and the opening still costs exactly one `random(99)`; and (b) a non-empty `usePreBattleAction` (:112-118), `ApplyPowerAction` Strength(bellowStr) THEN `GainBlockAction`(bellowBlock) in that addToBottom order — +5 / 9 at A20, the same two numbers in the same order as the BELLOW move's own program. **Every `setHp` range and every tier column sits OUTSIDE the hardMode guard (:81-104)**, so the id-1 row's columns are trusted unchanged for both variants, which is exactly what this row asked. Modelled with NO new `MonsterId` and NO schema change (the Lagavulin awake-init precedent): `jaw_worm_init_hard` + a `pad0` latch + an encounter-key branch in `run_advance.cpp`; the registered pre-battle fn is a no-op without the latch, so the Exordium worm and its Stage-A fixtures are byte-identical. Pinned by `BeyondNormalsII.OrdinaryJawWormIsUnchangedByTheHardModeAddition`, `HardJawWormSpendsTheSameDrawsAndReadsTheRoll`, `HardJawWormOpensWithAnyOfTheThreeMoves`, `HardJawWormPreBattleGivesStrengthThenBlock` and `JawWormHordeSpawnsThreeHardWorms` |
| Rest-site Recall option surface at Acts 2–3 (`isFinalActAvailable`, ruby key) | TE.2 scope pass (s2-design §4.5) | S2.13 | **DISCHARGED — YES, present; already modelled; zero engine work** (2026-08-07, S2.13). `CampfireUI.initializeButtons` (CampfireUI.java:94-96, read in full) appends the `RecallOption` under `Settings.isFinalActAvailable && !Settings.hasRubyKey` and **no act test whatsoever** — the only `id.equals` in that file is a flavour-text branch at :258 — so the row's real scope is *every* rest site in *every* act, Act 1 included, and this row's "Act-2/3" framing was wrong about the scope while right about the consequence. `Settings.isFinalActAvailable` (Settings.java:642) is profile state, constant for a run. The append lands **after** the relic veto sweep and **before** the `cannotProceed` auto-complete, so it can never be vetoed and a boss-relic-locked campfire stays open while the key is on offer. All of it has been live since S1 — `RestOptionKind::RECALL` (`rest_sites.hpp`), `kFinalActAvailable`, the post-sweep append (`rest_sites.cpp:193-205`) — and the grant is **not** "stubbed to S3": the RECALL arm sets `keys \|= kKeyRuby` (`CampfireRecallEffect.java:39-53` → `ObtainKeyEffect`). What is still S3 is only what the key is *for*. Pinned by `RestMenu.RecallIsOfferedInEveryActAndIsNeverVetoed`; s2-design §4.5 carries the withdrawal |
| Translator's `event_flags` FIRED derivation is act-local | S2.13 | S2.43 | The translator reconstructs "fired" as "initially in the list and now absent" (`translate.cpp`, the `eventList`/`shrineList` blocks), which is complete only while a list is never refilled. From Act 2 on it is not: `dungeonTransitionSetup` clears both (AbstractDungeon.java:2576-2577) and the constructor rebuilds them (:291, :293), so an Act-2 dump **cannot witness an Act-1 event or shrine fire** while the simulator's `event_flags` rightly still carries it — a differ false-RED on the first Act-2/3 differential capture that draws a shrine. The one-time specials are unaffected (carried by identity, never rebuilt), and so is all of Act 1, which is why nothing is red today. Closing it needs cross-record accumulation over a capture that starts at floor 1 — the capture campaign's call; the alternative is a narrow differ recognizer in the `b14` RACE mould. Decide before the first Act-2 shrine capture is scored. **DISCHARGED (2026-08-26, S2.43 pre-work) — cross-record accumulation, decided AND landed.** The masking-recognizer alternative was REJECTED on the aliasing argument: shrine ids occupy the same six bits in every act (`id - kShrineListFirstId`), so a positional mask cannot tell "fired in Act 1, rightly absent from this act's derivation" from "the sim wrongly claims an Act-2 fire" — it would forfeit exactly the Act-2/3 shrine coverage S2-G2 item 4 exists to witness. Accumulation is EXACT, not approximate, on `--replay`'s structural precondition (a floor-1 walk): a fire is visible in the very next record of its own act (per-action records; an event fires on ?-room entry and its own dialog records follow before any crossing), so at record k the union over records ≤ k of act-local derivations IS the sim's whole-run set; within one act the derivation is monotone, so the fold is a byte-exact no-op for every landed Act-1 verification, the committed 50-seed corpus included; and no false green is possible since substitution only adds bits the capture itself attested earlier. Landed as `sts::replay::FiredAccum` (`tools/oracle_bridge/replay/src/fired_accum.hpp`, its own INTERFACE target in the `command_map`/`mk_board` mould), folded in `replay_one` before the neutralizers; the claim and purchase flows need no fold — both seed the sim FROM captured records, so their derivations are act-local-symmetric. Pinned by `ReplayFiredAccum.*` (5): the Act-1 no-op, the cross-act regain, the shrine-refire single-bit case, no-false-green, and the fold's direction |
| SecretPortal's `false` pin rests solely on unmodelled wall-clock playtime | S2.13 | S2.33 | **DISCHARGED as a task obligation — the pin STAYS; the deviation is now a permanent frozen one carried by trap 5, not by this table** (2026-08-09, S2.33). The Beyond-events owner re-read SecretPortal.java:22-89 in full and confirms there is nothing else to model behind the gate: the class itself is gate-free dialog (take → a synthetic `MapRoomNode(-1, 15)` + `MonsterRoomBoss` + `nextRoomTransitionStart()`, i.e. skip-to-boss; leave → openMap), so the ONLY thing standing between the sim and this event is the `CardCrawlGame.playtime >= 800.0f` half of the getShrine gate (AbstractDungeon.java:1929-1933) — nondeterministic wall-clock input, deliberately unrepresentable in a sim whose whole verification story is determinism in (seed, actions). S2.33 therefore lands **no body and no row flip** for id 28: `build_shrine_pool`'s `eligible = false` pin stands in every act, `SecretPortalIsPinnedFalseInEveryActIncludingTheBeyond` keeps anyone from "fixing" the act half alone, and s2-design §5 trap 5 (already sharpened by S2.13) is the live carrier — including the capture-side duty that the campaign driver record playtime so a violated assumption is detectable. Revisit only with a reproducer, per the frozen S2 decision. **REPRODUCER FOUND — 2026-08-27, S2.43 depth triage — and the pin still stands.** Two Act-3 witnesses proved the deviation is an INDEX bug, not a missing event: `getShrine` draws `tmp.get(rng.random(tmp.size()-1))` (:1937), so omitting SecretPortal shortens the list and changes which event EVERY Act-3 `?` room past 800 s of wall clock returns. The gate is now the Java's own predicate over an explicit `playtime_seconds` input defaulting to 0.0f, so the simulator is byte-identical to the pin while `--replay` can feed the capture's `oracle.playtime`; an act-only approximation was refuted by a 710 s capture. Full disposition, evidence and the owner question it leaves open are in the S2.43 row |
| `seed_scan`'s planner-side event-flag decode is still one word | S2.13 | S2.42 | S2.13 split the FIRED bitset into `RunState::event_flags` (ids 1..31) + `event_flags_hi` (ids 32..63) and mirrored it into the engine accessors, `PublicView` (v3), `twin.cpp`, the differ and the translator. `tools/oracle_bridge/planner` was **OFF LIMITS to S2.13** (S2.42 held the file concurrently), so `event_flag_set` / `decode_event_flags` / `event_flags_text` / `SeedRow::event_flags` there still take a single `uint32_t` and the accumulator at `seed_scan.cpp:199` ORs only the low word. Consequence is **under-reporting in an offline analysis tool, not a false green**: no Act-2/3 event ever appears in a seed-scan row, and `tests/seed_scan_test.cpp`'s "ids > 31 read false" guard is still literally true of the planner helper. Fix = take both words (or a `uint64_t`) through those four signatures and widen the guard. Not urgent until seed-scan is pointed at Act 2. **DISCHARGED (2026-08-26, S2.43 pre-work — S2.43's §7.4 event-depth coverage join is exactly "seed-scan pointed at Act 2").** All four signatures now take one `uint64_t` whose layout is the two storage words laid side by side — lo 32 = `event_flags` (ids 1..31 at bit id-1), hi 32 = `event_flags_hi` (ids 32..63 at bit id, the engine hi word's bit (id-32) shifted up 32; bit 31 unused, exactly as the engine leaves it) — and the observer ORs BOTH engine words per step. The old "ids > 31 read false" guard test INVERTED into `SeedScanEventFlags.CombinedWordMirrorsBothEngineWords`, which fires every registry event through the ENGINE accessor and requires the planner helper to read the resulting `lo | (hi << 32)` pair, so the two layouts cannot drift apart silently; the ascending decode/text pin now crosses the word boundary (`DecodesInAscendingIdOrderAcrossBothWords`). TSV/JSON emit the wider number through the same columns; the s243_prep artifacts predate the widening but their low words are unchanged, so the Act-1 kill cohort stands. NOTE the fix rode the same session's engine repair: the hi word's bit assignment was `(id-33)` with an UNDERFLOW at id 32 that x86 shift-masking hid (see the 2026-08-26 UB commit) — the widening deliberately landed AFTER that repair so it mirrors the intended `(id-32)` layout, not the accident |
| Boss chest + sapphire-key row interaction | TE.2 scope pass (s2-design §4.5) | S2.11 | **DISCHARGED — NO** (2026-08-07, S2.11). `BossChest.open(boolean)` (BossChest.java:49-63) FULLY OVERRIDES `AbstractChest.open` with **no `super` call**, so the `isFinalActAvailable && !hasSapphireKey` append at AbstractChest.java:95-97 is unreachable from the boss chest — and so are `randomizeReward`'s treasureRng roll, gold, the curse, `addRelicToRewards`, `onChestOpenAfter` and `combatRewardScreen.open`. Pinned by `BossChest.NeverAppendsTheSapphireKeyRow` and `BossChest.FiresNoRelicChestHooks` |
| `Lab` in ProceedButton.java:115's combat-event list with no encounter | TE.2 scope pass (s2-design §2.3) | S2.33 | **DISCHARGED — reward-screen plumbing confirmed, zero engine work** (2026-08-09, S2.33). ProceedButton.update's EventRoom arm (ProceedButton.java:110-121, read in full) is not a "combat-event list": it decides where **Proceed on a COMBAT_REWARD screen inside an EventRoom** goes. For the seven listed classes (Mushrooms, MaskedBandits, DeadAdventurer, **Lab**, Colosseum, MysteriousSphere, MindBloom) it closes the screen AND opens the dungeon map; for every other event it merely closes the screen and un-hides, dropping back to whatever dialog remains. Lab qualifies with no encounter because it hides its dialog (`GenericEventDialog.hide()`, Lab.java:49), sets the room COMPLETE and opens the reward screen with three potions (Lab.java:50-58) — the reward screen IS its exit, so Proceed must go straight to the map. The engine has modelled exactly that since B4.13: `open_potion_reward_screen` (one_time_specials.cpp) clears the dialog and transitions to COMBAT_REWARD, whose Proceed goes to MAP_CHOICE. The same collapsed shape covers the OTHER side of the split — Woman in Blue / Wheel of Change / Sensory Stone are NOT in the list, so the game bounces through a vestigial dialog "Leave" click; the engine's landed convention (argued at shrines.cpp `applyResult` and beyond_events.cpp Sensory Stone) collapses that no-state-change click, which S1's G7 capture verification already accepted for the two Act-1 members |
| Exact Act-2/3 entry floors (17/34 assumption) | TE.2 scope pass (s2-design §4.2) | S2.12 | **DISCHARGED** (2026-08-07, S2.12). The answer is a PAIR per act, and conflating its halves was the whole risk: **17/34 are the CONSTRUCTION floors** — what `dungeonTransitionSetup`, the constructor chain, `generateMap`, `setEmeraldElite` and the BGM draw observe, and what the un-reseeded floor-scoped five still carry (`seed+17` / `seed+34`) — while **18/35 are the first PLAYABLE rooms**. Span = 17 = 15 map rows + boss + boss chest; the crossing itself adds **no** floor, because `isDungeonBeaten = true` (ProceedButton.java:249-250) is exactly what makes `updateFading` skip `nextRoomTransition` (:2317-2326). Table in s2-design §4.2; engine constants `kActFloorSpan` / `act_floor_base`, with `run_cur_row = floor − base − 1` replacing the Act-1-only `floor − 1` |
| `generateStrongEnemies(12)` regeneration on an exhausted `monsterList` | S2.11 (the boss-exit pop it added) | S2.12 | **DISCHARGED — UNREACHABLE, no body written** (2026-08-07, S2.12). Re-derived for Acts 2–3, which call `generateWeakEnemies(2)`: SUPPLY is weak + 1 first-strong + 12 strong = **15** (Act 1 = 16); DEMAND is at most **14** — one walked path visits 15 rooms, one per map row, of which the act-independent generator forces row 8 Treasure and row 14 Rest, leaving 13 `monsterList`-consuming rooms (a ? room that rolls MONSTER is one of those 13, not an extra) plus the one pop that leaving the boss room performs. Margin 2 in Act 1, **1** in Acts 2–3. The loud `assert` in `next_room_transition_impl` stays and now carries that arithmetic in full; writing untestable machinery for an unreachable arm would be worse than an assert that names why it cannot fire |
| Fork redeploy + bottle-taking capture (stage-b table row, "next capture-campaign owner") | wave-runlayer S3 (stage-b) | S2.43 | **INHERITANCE ALREADY SATISFIED — verified 2026-08-26 at S2.43 launch.** The stage-b row was DISCHARGED by Track C `wave2-capture` (2026-07-28/29) between this ledger's authoring and now: fork redeployed (pin `ADDE8609…`), SEVEN bottle-taking captures strict-validated, and the reverse-master-deck-order claim PROVEN by STS04925's zero-diff `--replay` through the bottling (91 records), promoted as `ReplayCommandMap.ABottleGridSessionSnapshotsTheLegalIndicesDescending`; procedure in `tools/oracle_bridge/driver/wave2cap_capture_runbook.md`. S2.43 owes nothing here beyond what its own campaigns capture anyway. The 2026-08-26 redeploy (pin `9BC4BF6A…`, determinism-checked) supersedes `ADDE8609…` and ADDS the trap-5 `playtime` anchor; `in_bottle_*` emission is unchanged. **PIN CITATION AMENDED 2026-08-27 (S2.43 close) — the `9BC4BF6A…` figure above was CORRECT for the deploy it describes and has since been superseded three times, so read it as one link in a chain rather than as the current pin.** It is attested by the capture headers of `s243_preflight` and all three breadth cohorts, which is what the S2.43 dashboard's cohort table prints. The chain, each link attested by the `fork_jar_sha256` in the headers of the captures taken under it: `9BC4BF6A…` (2026-08-26 redeploy, breadth wave) → `AD4C44D1…` (escape-window round 1, the `isEscaping` hold — `s243_recap_*`) → `370CBFA8…` (escape-window round 2, the endBattle settle-lag hold — `s243_recap2_*`, `s243_breadth_top2` and most of the S2.V2 depth waves) → **`ABD95268462FA31E7F7498B45BA4539E3731CC38E59850B547D03AE6F372A4C1` (2026-08-27 SecretPortal playtime pin, the CURRENT deployed jar)**, live-preflighted by campaign `fork_pin_preflight` (STS900010, clean) and carried by the last depth captures (`s2v2_mb_102529`, `s2v2_mb_118993`, `s2v2_dbv_103509a/b`). The S2.43 row's mid-day observation that the jar on disk hashed `370CBFA8…` while this row cited `9BC4BF6A…` was true when written — the deploy had already moved on twice — and is closed by this amendment, not by a rewrite of the earlier figure |
| `BOSS_REWARD.screen_state.relics` — schema **storage** for the boss-relic offers | S2.42 (which promoted the disposition but not the storage) | **S2.47** (opened 2026-08-10, the pre-S2.43 storage task this row demands; S2.43 consumes) | **STORAGE HALF DISCHARGED by S2.47 (2026-08-10, schema v8) — see the S2.47 Log for the full landing.** Evidence trail: PROTOCOL.md §3.8 dispositioned this `I (S2 scope)` because "the run terminates at act-1 boss combat rewards, before the boss chest" — no longer true at capture driver `b1.7.0`. An `I` field is **never diffed**, so design §6 S2-G2 item 2 (a *zero-diff* boss-chest boss-relic pick) was unachievable while the row said `I`. S2.42 took the contained half (row → `S`, registry join fail-loud, field `fr.defer`red). S2.47 took the rest: `BossChestState` moved out of the transient controller into `RunState.boss_chest` (pure tail append, `SCHEMA_VERSION` 7→8 at this row's planned site — the pad-carve alternative measured 3 contiguous bytes max and was rejected), `kTraceFormatV2` follows to 8, the translator **emits** the three joined offers with the reveal bits a live BOSS_REWARD screen implies, `diff_run_states` compares the group by name (`boss_chest.relics[i]`/`.screen`/`.seen`/`.chose_relic`), and the replay differ gates the comparison on capture-side attestation (`neutralize_unattested_boss_chest`). Pinned by `Translator.BossRewardRelicsAreEmittedIntoBossChestStorage`, `Translator.BossRewardOffersRoundTripThroughTheDifferBothWays` (both directions), `Translator.BossRewardRelicsRejectAnyCountButThree`, `Translator.BossRewardRelicsStillJoinTheRegistryAndFailLoud` (kept — the join survived the emit) and `RunDifferBossChest.EveryMemberNamedSeparately`. **What S2.43 still owes this row: the live campaign scoring itself** — a captured boss-chest pick through the differ, zero-diff, which is G2-2 item 2's evidence, not storage. |
| S2.31 payout relics/cards are now REACHABLE with their bodies still deferred | S2.03 (landed them acquisition-only, naming S2.31 as body owner) / S2.31 (granted the acquisition, declined the bodies) | **S2.34** (opened 2026-08-09, satisfying this row's "body task before the gate" demand) | **DISCHARGED by S2.34 (2026-08-09) — all seven bodies landed, each pinned tier-2 against its cited Java re-read in full; see the S2.34 Log.** The seven: **Bloody Idol** `onGainGold` heal 5 at BOTH gainGold doors — the run-layer `gain_gold` fan-out (heal through the out-of-combat onPlayerHeal door) AND the in-combat producer the row's own framing under-counted: Hand of Greed's `GreedAction` calls `player.gainGold` at the kill (GreedAction.java:38, the ONLY in-combat gainGold in Acts 1–3 scope), so `op_damage_greed` now runs Ectoplasm's early return and the fan-out at combat time, healing through `heal_player_with_relics` (Magic Flower ×1.5, Mark of the Bloom → 0); **Enchiridion** `atPreBattle` (native, rides RANDOM_ATTACK_TO_HAND's new pool selector over the pre-existing `kIroncladPowerPool`); **Nilry's Codex** `onPlayerEndTurn` → `Opcode::CODEX` (the DISCOVERY choice surface, RED-combat-pool sampler, always-skippable, zero wasted regens, random-spot draw-pile insert); **Necronomicon** `onUseCard` once-per-turn replay + `atTurnStart` re-arm (Double Tap's replay machinery; latch = `CombatState.flags` bit 6, counter stays −1); **Necronomicurse** `triggerOnExhaust` — NO new CardTrigger was needed (the S2.31-era premise was stale: the `on_exhaust:` program column, live since Sentinel, IS the triggerOnExhaust seam) — authored as an addToBot MAKE_CARD via the new `on_exhaust_bottom` column; **Mutagenic Strength** `atBattleStart` native (addToTop reversal → resolution cosmetic/LoseStrength/Strength, slot order pinned); **Ritual Dagger** `Opcode::RITUAL_DAGGER` (73) — misc-based damage, DAMAGE_GREED kill gate, `initial_misc: 15` seeded at the obtain door, master-deck propagation at the combat fold-back (documented deviation: a same-uuid replay-copy kill grows only the transient copy). Also closed in passing: **Mark of the Bloom's in-combat onPlayerHeal half** (the S2.33 acquisition made it reachable while `heal_player_with_relics` knew only Magic Flower) |
| Mind Bloom boss re-fight **directed capture** (the oracle half of S2.33's acceptance) | S2.33 (sim half landed + pinned; no capture seat) | S2.43 | S2.33's acceptance reads "Mind Bloom's Act-1-boss re-fight replays zero-diff in a directed capture". The SIM half is landed and pinned against the decompile (beyond_events_test.cpp: the one-randomLong JDK shuffle twin, the fixed 25/50 gold row, the RARE `returnRandomRelic` pop, the EventRoom-not-boss combat flags, and the victory→reward→map walk), but the bridge never runs from a task worktree and two sibling event batches held the game install concurrently, so **no live capture was run** — deferred, not skipped. S2.43 (the next capture campaign) owes: a directed Act-3 capture that draws MindBloom, takes "I am War", plays the re-fight to the reward claim, and scores zero-diff through the differ; the seed/policy triple can come from `seed_scan --need-boss-id` once Act-3 reach is live (see the reach row above). Watch two rows while scoring it: the translator's act-local FIRED derivation (row above — an Act-2/3 event capture is exactly where it false-REDs) and trap-5's requirement that the driver record playtime. **DISCHARGED 2026-08-27 (S2.43 close) — the directed captures were run and scored, TWO of them, on two different Act-1 bosses.** `s2v2_mb_102529` (STS102529, policy-seed 25 — the Guardian re-fight) and `s2v2_mb_118993` (STS118993, policy-seed 28 — the Hexaghost re-fight), both scheduled off S2.V2's `seed_scan --need-event MindBloom --min-act 3` output, both taking "I am War", winning the re-fight and claiming the fixed 25/50 gold reward, both `clean` at capture and CLEAN under the full-corpus `s243_resweep7.log`. The full Mind Bloom line `s2v2_mb_103364` (STS103364) also replays CLEAN end to end now that the death-terminal fix (45f9528) landed. Both watched rows behaved: the FIRED accumulation row was already discharged in S2.43's pre-work and no Act-2/3 shrine capture false-REDs, and playtime is recorded and now pinned on both sides (see the fork-pin row above). Evidence: the MindBloom event row reads `sighted-zero-diff` with 4 act-3 sightings in [verification/s243-dashboard.md](verification/s243-dashboard.md)'s item-4 join |
| Act-2 / Act-3 **measured** sim-side reach numbers | S2.42 (instrument built; measurement structurally impossible) | S2.41 (re-runs as content lands) / S2.43 | An Act-2/3 combat room parks at `RunPhase::ROOM_UNIMPLEMENTED` and the first row of every act is a forced Monster row, so sim-side Act-2/3 reach is **0 by construction** until S2.23/S2.24 (Act 2) and S2.27/S2.28 (Act 3). **RE-RUN HALF DISCHARGED by S2.41 (2026-08-09)**: with every S2.2x/S2.3x batch landed, §1's command was re-run verbatim (50,000 rows, release, `determinism_mismatches=0`) and the Act-2/3 cells are now **measured, at 0** — Act-1 reproduced to the row, `room_unimplemented` went 8 → 0, and the fuzz soak's new per-act coverage agrees over 100,000 independent cases (act 2 entered by 0.11 % of cases, act-2 boss fought 0 times, act 3 never). What is left of this row is therefore **not a content obligation**: sim-side depth is bounded by the E0 policies (~×30 loss per act), so a three-act sim number needs a different policy or the driver, not a later re-run. S2.43's live driver numbers are the remaining half. [s242-deep-reach.md](verification/s242-deep-reach.md) records those cells as *pending content* rather than estimating them; re-run its §1 command as those batches land, the report format does not change. Double-boss detection (design §6 G2-3) is deliberately **unbuilt** rather than shipped as an always-false column — a field hard-wired false under a comment naming a future task is the shape conventions §8 calls a bug signal — and should use whichever run-layer flag S2.28 lands |
| The Courier's restocked colored-card identity (the one unseeded value in scope) | S1 shop model (the permanent named refusal, shop.hpp `kShopRestockedUnknownCard`) | **Post-S2-G2 task** (owner-directed 2026-08-10: implement the Courier fully, eventually) | Retail's draw is genuinely not reverse-engineerable from (seed, actions): `getCardFromPool(..., useRng=false)` reads libGDX's global `MathUtils.random`, a RandomXS128 seeded from JVM-startup entropy whose stream position also advances unpredictably with rendering/VFX draws — the value is outside the sim's input domain in principle, not just in practice. "Fully correct" is nevertheless achievable on the project's own terms, in two halves that must land together: (a) **sim-side** — draw the identity from a dedicated seeded stream; the retail draw is uniform over the eligible (rarity, type) pool (CardGroup.getRandomCard's `MathUtils.random(size-1)`; the implementing task re-derives the exact eligibility/duplicate loop from ShopScreen), so a seeded draw is distributionally exact, gives training the right decision problem, and lifts the buy-refusal; (b) **oracle-side** — patch the vendored fork so that one call consumes the same seeded stream, making the slot zero-diff verifiable under the established oracle-contract precedent (the Discovery wasted-regens / Explosive-Potion THORNS boundary: the contract is the *patched fork*, not the retail client). Needs a fork redeploy, so ride it with one that is happening anyway. Until then the `kShopRestockedUnknownCard` refusal stands |
| Keys as obtainable content — emerald-elite node flag + EMERALD_KEY reward row; SAPPHIRE_KEY linked-row claim semantics | stage-b design §1.1 "Out" / s2-design §1 (S1/S2 scope) | **S3 planning** (owner-directed 2026-08-10) | The owner reviewed the deviation/exclusion inventory and directs that emerald-key rewards be implemented in the Act-4 wave: store `setEmeraldElite`'s chosen node (the mapRng draw is already modelled — combat_rewards.hpp:107-112 records that only the node flag is missing), surface the burning-elite combat's EMERALD_KEY reward row, and give the sapphire chest append its real claim semantics (today it is modelled as an ignored linked row that costs no RNG or state parity, per stage-b-design §1.1). Ruby is already live (the Recall arm grants it). This row exists so S3 planning inherits an explicit obligation instead of rediscovering the S1/S2 scope decision |
| Per-step throughput **attribution** across S2 (the ×0.712 combat step / ×0.498 batch measured against B5.5) | S2.45 (an absolute one-build floor task: it measures, it does not A/B or optimise) | **S3 throughput work** | S2.45's floors all hold by ≥ ×166, and run-level stepping is ×0.956 of B5.5 — so S2.48's per-advance `sync_live_gold` costs nothing measurable. What is *unattributed* is the rest: combat stepping ×0.712 and the 10k-state batch ×0.498 across a whole content stage. Run length is constant (47.04 actions/run vs 46.93), so both are per-step cost ratios, not workload changes. Leading candidate for the batch is state size, not the interpreter — `sizeof(CombatState)` 3,896 → 8,088 B takes that benchmark's working set ~39 MB → ~81 MB against a 96 MiB L3, and it is a bandwidth-bound sweep. The sanctioned instrument is the interleaved `tools/bench_ab.sh` over two binaries, never two sequential runs; the narrow, cheap A/B that isolates the two commits the S2.45 brief flagged is **`d57e077` against `646bd18`** on `bench_advance_mask` + `bench_throughput`. Matters because training wall-clock is the currency the S3 baseline is quoted in ([verification/s245-throughput.md](verification/s245-throughput.md) §6). **DISCHARGED 2026-09-03 by S3.64 — `RESULT: UNMEASURED`, honestly, not a gap.** The named A/B ran at n=5, 8 and 12 interleaved pairs on both benchmarks; every round answered `RESULT: UNMEASURED` (`|mean| <= 2*sem`), because both A/B windows landed under host CPU 90-98% (a sibling WSL build under `vmmemWSL`, corroborated by a cross-worktree `ccache` hit against a sibling worktree during S3.64's own rebuild) — 4-8× this box's own ±2.8% calibration spread — and growing `-n` did not converge toward significance. The ×0.712/×0.498 ratios therefore remain attributed only to the state-size candidate above (`sizeof(CombatState)` unmoved at 8,088 B since this row was written), not confirmed. Full methodology and every round: [verification/s3-64-throughput.md](verification/s3-64-throughput.md) |

---

## Phase S2.0 — Registry authoring + codegen groundwork (∥; no engine deps; **Wave 1 = S2.01–S2.04**)

Safe to dispatch immediately: append-only ids, disjoint files, tier-2 tests
are table-projection tests that do not require engine consumers. These four
are the "first registry authoring wave" the TE.2 acceptance names.

- **S2.01** `[x]` ∥ **encounters.yaml Acts 2–3 + act-keyed codegen.** All 40
  rows of design §2.1 (Act 2: 5 weak / 8 strong / 3 elite / 3 boss / 3
  event; Act 3: 3 weak / 8 strong / 3 elite / 3 boss / 1 event), weights,
  exclusions (incl. Chosen's two-key exclusion and 3 Darklings'
  self-exclusion), and miscRng composition programs (spawnGremlin ×2 pool
  draw; spawnShapes 3-or-4 draw-without-replacement over the 6-slot pool;
  getAncientShape ×2; fixed lists elsewhere). Extend
  `tools/registry_gen` so `encounter_table.hpp` emits per-act pool tables;
  the emitted `act: 1` tables stay byte-identical. Composition programs may
  need one new node kind for spawnShapes' shared-pool 3/4 draw — reuse
  `{pool: ...}` if it fits; any new node kind is a generator+schema change
  landed with its negative test (unknown node fails loudly).
  **Deps:** — **Acceptance:** tier-2 table tests pin every new row (pool
  membership, weight, exclusion set, program shape) against the cited
  Java, re-read in full; codegen determinism check green; S1 encounter
  table hash unchanged; six presets green.
  **Log:** 2026-08-04 — landed. 40 rows, ids 22–61 (both blocks exactly
  filled); per-(act,pool) `EncounterPoolTable` emission with flat
  `kEncounters` unchanged; NO new composition node kind ({pool:} covered
  spawnGremlin with-replacement, spawnShapes 3/4 without-replacement,
  getAncientShape construct-only); self-exclusion loosening with its own
  negative test. Act-1 byte identity proven by diff (one deleted count
  line) + sha256 of the extracted section + the durable
  `ActOnePoolsUnchangedByTheActExtension`. 9 differential tier-2 tests +
  2 codegen negatives; six presets green. Design-doc §2.1 fixes recorded
  in-row: live Act-3 event key is "2 Orb Walkers" (the "Mysterious
  Sphere" ENCOUNTER key at MonsterHelper.java:582-584 is dead content,
  excluded per S1 practice); TheCity pool citations re-derived
  (weak 94-104 / strong 106-120 / elite 122-130). The per-act weak-draw
  count (Acts 2–3 use generateWeakEnemies(2) vs kWeakSegment = 3)
  is run-layer state deliberately left to S2.12.
- **S2.02** `[x]` ∥ **events.yaml Act-2/3 rows.** 20 identity rows
  (design §2.3) in Java insertion order (TheCity.java:184-199 then
  TheBeyond.java:178-187), `implemented: false`, each with `conditions`
  metadata for its draw gate (Moai Head's idol/hp gate, Colosseum's
  map-position gate, floor gates) and provenance; per-act list-membership
  metadata for the six existing shrine rows and the 14 one-time rows'
  act gates (AbstractDungeon.java:1882-1942) — schema extension mirrors
  how S1 recorded Exordium membership, additive only.
  **Deps:** — **Acceptance:** generated event tables list the new rows
  exactly once with S1 enum values unchanged (static_assert pins);
  membership/gate metadata pinned by tier-2 against the cited lines;
  six presets green.
  **Log:** 2026-08-04 — landed. 20 identity rows (32–44 City, 45–51
  Beyond) in Java add order; `conditions` now mandatory with
  `pool` + `acts` → `EventDef::pool`/`act_mask`/`event_in_act()`;
  SPECIAL masks carry the getShrine act-gate half (list built once in
  Exordium, carried by reference — CardCrawlGame.java:1102-1119).
  16 tier-2 tests + 6 generator negatives; six presets green. Found
  load-bearing: **shrine list order differs between Act 1 and Acts
  2–3** (Wheel of Change last vs first) — Act-1 shrine-bitset
  bit↔position mapping must be re-derived per act, pinned by
  `ShrineListOrderDivergesBetweenActOneAndActsTwoThree`, owned by
  S2.13; also S2.13's: `event_flags` uint32 has no bit for ids 32–51
  (guarded + tested). Beggar's gold ≥ 75 gate authored from source
  (design §2.3 omitted it); Cursed Tome Circlet fallback flagged for
  S2.31–S2.33.
- **S2.03** `[x]` ∥ **relics.yaml + cards.yaml S2 identity rows.** The ~10
  event-relic rows already enumerated in relics.yaml's header commentary
  (Bloody Idol, Enchiridion, Nilry's Codex, Necronomicon, Mutagenic
  Strength, N'loth's Gift, Red Mask, Mark of the Bloom, Cultist Mask,
  N'loth's Mask — SPECIAL/EVENT tier, no `pool_order`), plus the 5 card
  rows (Apparition, Bite, J.A.X., Ritual Dagger, Necronomicurse). Rows
  whose hook bodies are combat-relevant land `native:` only with bodies
  (link-error discipline) — otherwise land acquisition-only metadata with
  the body task named in the row comment, following the S1 "deliberate
  no-op" convention where the Java hook is genuinely out-of-combat.
  **Deps:** — **Acceptance:** tier-2 rows for every new entry (tier,
  sources, unremovability flags for Necronomicurse per CardGroup.java:981 /
  AbstractPlayer.java:744); id pins unchanged for all prior rows; six
  presets green.
  **Log:** 2026-08-04 — landed. 8 SPECIAL relic rows (ids 143–150; the
  design's "~10" counted two FaceTrader faces already landed in S1 —
  correction recorded in the registry block comment, **ids 151–154
  returned unissued**) + 5 SPECIAL card rows (128–132; 133 reserve).
  Both canSpawn gate families were already fully S1-encoded (enumerated
  and cross-checked complete); what S2 changes is reachability
  (S2.11/S2.12). Bodies deferred with named owners except the two the
  acceptance names: Necronomicon on_equip (Omamori/Calling Bell
  precedent) and Necronomicurse's two unremovability sites; Ritual
  Dagger's program deliberately EMPTY (loud) pending its bespoke misc
  opcode. 16 tier-2 tests; six presets green. Ectoplasm provenance
  corrected in place (canSpawn :54-57, onEquip :44-47 — design §2.4 and
  the S1 row were both off). Flagged for S2.31/S2.32: Ghosts A15+ deals
  3 Apparitions not 5; Vampires strips STARTER_STRIKE from the master
  deck. Stale sidetable `_provenance.scope` string noted, left to its
  generator's owner.
- **S2.04** `[x]` ∥ **a20.yaml S2 status refresh.** Update the affected
  rows' notes/status per design §2.5/§4: A5 (between-act heal becomes
  live), A6 (verified run-start-only — negative pinned), A12 (per-act
  0.125/0.25 halving), A13 (boss gold in Acts 2–3 incl. double boss and
  Mind Bloom's 25/50), A20 (double boss). No engine change in this task —
  status text + provenance only, with the engine work owned by S2.12/S2.24
  /S2.28; rows keep S1 status until their owner lands (this task makes the
  ownership explicit, preventing the "someone fixes A12 casually" drift).
  **Deps:** — **Acceptance:** `check_stale_counts.sh` +
  `check_doc_links.sh` clean; registry manifest row count unchanged;
  tier-2 suite green (no behavior change).

## Phase S2.1 — Run layer

  **Log:** 2026-08-04 — landed. Five rows' notes/provenance extended
  (A5/A6/A12/A13/A20) with re-verified citations; S1 STATUS prefixes
  unmoved by design, pinned by `AffectedRowsKeepTheirS1StatusPrefix`;
  new fact recorded: Mind Bloom boss re-fight pays fixed 25/50 gold at
  A13+ with no miscRng draw (MindBloom.java:73-77), ownership split
  S2.24/S2.28/S2.33. 6 tier-2 tests; six presets green; row count
  unchanged (20).
- **S2.11** `[x]` **Boss chest + boss-relic pick.** TreasureRoomBoss room
  flow after Act-1/Act-2 boss rewards: chest construction at entry, 3
  front-pops of `bossRelicPool` with `canSpawn` recursion + Red Circlet
  fallback, pick/skip semantics (the burn is at entry; skip is a reversible
  screen close; `noPick` on leave), Neow-swap pool-state composition. Claims a
  `RunPhase` value (and fuzz `MoveCat`) via the stage-b namespace table.
  Design §4.1 + §5 trap 3.
  **Deps:** — (the boss pool is an S1 domain; S2.03's Necronomicon row is
  only needed if a directed test wants its on-equip curse, and that test
  belongs to S2.03 itself) **Acceptance:** unit tests for pop order,
  pool depletion across both consumers, skip, canSpawn rejection
  (Ectoplasm in Act 2 — trap 9), sapphire-key row question resolved and
  pinned (deferred-obligations row); fuzz soak reaches the new phase;
  Stage-A combat fixtures + registry-generated tables byte-identical,
  `tests/golden/twin_fixtures/twins_v1.bin` regenerated via its checked-in
  generator; six presets green.
  **Log:** 2026-08-07 — landed. `RunPhase` **11** BOSS_TREASURE, `RoomType`
  **8** TreasureBoss, fuzz `MoveCat` **28–31** (`COUNT`→32) — the whole grant
  spent, nothing released. **The scout dossier was wrong on the routing and
  the correction is the biggest thing in this task:** `goToTreasureRoom`
  (ProceedButton.java:179-187) → `nextRoomTransitionStart` →
  `AbstractDungeon.updateFading`'s `if (!isDungeonBeaten) nextRoomTransition()`
  (:2317-2325) means the chest entry is a **full room transition** — floor++,
  the trap-7 five-stream reseed, and the relic `onEnterRoom` /
  `justEnteredRoom` fan-outs (Maw Bank pays 12 gold at the chest, pinned).
  Off-map, not floor-less. Two consequences recorded in the deferred table:
  the boss/chest floor pair (16/17, Act-2 first room 18) as evidence for
  S2.12's row, and the `monsterList` pop on boss exit — `MonsterRoomBoss
  extends MonsterRoom`, so `nextRoomTransition`'s `instanceof MonsterRoom` arm
  fires; fixed here, with the unreachable `generateStrongEnemies(12)` arm left
  as a loud assert and a new S2.12 row. Three more findings: **all five**
  `on_equip_screen` BOSS relics are pickable at this chest, so the pick goes
  through the `RelicEquipContext` door and the request is re-homed onto the
  controller's existing `NeowState` grid / `RewardScreen` — the call-site-only
  change `relic_pools.hpp:126-128` predicted, costing **no** namespace value
  (no new `ChoiceKind`, no new sentinel, no new `RunActionMask` or `PublicView`
  field, `PUBLIC_VIEW_VERSION` unmoved). Skip semantics amended in design §4.1:
  the burn is unconditional at entry, the skip is a *reversible* close and the
  chest reopens with the same three, `noPick` is metrics-only. Sapphire-key row
  **DISCHARGED — NO**. Also fixed in place: the stale `Ectoplasm.java:50-53`
  citation in design §2.4 and `relic_pools.hpp:58`; the stale
  `relic_pools.hpp:137-140` "Wave-C allocations stay unspent" claim; and
  `fuzz_test.cpp`'s `MoveCatCountIsPastEveryEnumerator`, which had been
  asserting `25 < 32` since SHOP landed instead of finding the real highest
  enumerator. `kRoomTypeCount` moved to `map_rooms.hpp` beside the enum with a
  `static_assert` at both ends (it had none, a silent OOB hazard).
  `twins_v1.bin` regenerated (`sizeof(RunController)` moved); BOSS_TREASURE is
  deliberately NOT in the generator's `kWanted[]` — no scripted policy wins the
  act-1 boss, so it would only print a permanent warning, and the twin
  invariance is pinned directly in `boss_chest_test.cpp` instead.
- **S2.12** `[x]` **Act transition + Acts 2–3 map generation.**
  `dungeonTransitionSetup` semantics in engine order (design §4.2):
  actNum, **cardRng counter snap** (trap 1), pity resets, list clears,
  `blizzardPotionMod` reset, A5 heal vs full heal; constructor-chain
  ordering (generateMonsters → initializeBoss → …); `mapRng = seed +
  actNum*100/200`; per-act constants (§2.5); `setEmeraldElite` per act;
  monster-list generation for both acts off the continuing `monsterRng`
  (uses S2.01 tables); card-pool rebuild idempotence; one-time list
  carried. Floor continuity (exact boundary floors from source — deferred
  row).
  **Deps:** S2.01 **Acceptance:** named tests per trap (counter-snap
  bands incl. the exact-250 non-snap; A5 arithmetic; pity reset vs
  cardBlizz carry; mapRng offsets per act); a three-act sim run under a
  scripted policy completes deterministically twice with identical
  hashes; six presets green.
  **Log:** 2026-08-07 — landed. **No namespace value spent** (none was
  granted and none was needed): no new `RunPhase`, `RoomType`, `MoveCat`,
  `ChoiceKind`, mask field or `RunState` field, so `SCHEMA_VERSION`,
  `PUBLIC_VIEW_VERSION`, every struct `sizeof`/offset and
  `twins_v1.bin` are all unmoved and the Stage-A fixtures are
  byte-identical. **The victory terminal moved to its real place.** S1 put
  it at the Act-1 boss reward proceed, S2.11 at the boss chest's proceed;
  the chest's proceed is now the ACT TRANSITION, so `run_is_victory()`
  moved with its producer to the **Act-3 boss kill** — which opens no
  reward screen and no chest at all, because `AbstractRoom.java:327`'s
  guard is false for a non-endless `TheBeyond` boss, and `ProceedButton`'s
  chest branch (:111-113) needs `screen == COMBAT_REWARD`. What survives
  that guard is the gold add at :286-297, ahead of it: **one `miscRng`
  draw whose gold never reaches the purse** (it goes to an unclaimed
  room reward list), pinned differentially against an Act-2 twin.
  **RESIDUE, and it is the honest one:** the fuzz soak's `victories`
  counter now reads **0 for every soak until S2.28** lands the Act-3
  bosses, because no run can walk into an Act-2/3 room while its monsters
  are unimplemented. That is a content gap, not a regression; the
  seed-116 guard was retargeted to the property it was really written for
  (a non-terminal phase must never advertise an empty mask) and now
  asserts the crossing happened and the run parked cleanly. For the same
  reason the **three-act sim reaches Act 3 by driving the two boss-chest
  crossings through the public `next_room_transition_boss_chest` edge**
  rather than by walking Act-2 combats; every state it does visit is
  stepped through the real `advance()`/`legal_actions()` API, both passes
  hash identically at every step, and the run reaches floor 35. When
  S2.2x lands, the placement can be deleted and the same driver becomes a
  pure policy walk.
  **Dossier correction (trap 1).** The S2 scout dossier claimed a
  `random(999)` replay of the counter snap "gives the right counter and
  the wrong state". It does not — and `rng_stream.hpp`'s own
  `from_seed_counter`/`from_seed_set_counter` pair already said so: the
  xorshift128+ advance is a function of the NUMBER of `nextLong()` calls
  alone, never of the value consumed, and `nextInt(1000)`'s rejection loop
  retries with probability ~1e-16 per call, so the two replays coincide.
  `advance_counter_to` uses `randomBoolean` because that is what
  `Random.setCounter` writes, not because the alternative diverges; the
  test asserts the coincidence explicitly and discriminates against an
  off-by-one STEP COUNT instead, which is the mistake that can actually be
  made. Other findings: **`run_cur_row` was Act-1-only** —
  `floor − 1` is right only because Act 1's base is 0, and it is now
  `floor − act_floor_base(act) − 1` with a saturating −1, which is also
  the act-general spelling of the two `floor == 0` "first pick of the act"
  guards in the map mask and the Wing Boots charge test. **`TheBeyond`
  never replaces `currMapNode`** (TheCity.java:49-50 vs
  TheBeyond.java:35-47), so Act 3 opens still holding Act 2's off-grid
  `TreasureRoomBoss` node — inert (neither it nor an `EmptyRoom` is a
  `MonsterRoom`, so no `monsterList` pop) but modelled and pinned as a
  negative. **`ELITE_CHANCE`'s reset to 0.0f is unrepresentable and that
  is correct**: its only consumer sits under a `DeadlyEvents` mod gate
  (EventHelper.java:190-192, :204-207), and note it resets to 0.0f, not to
  the 0.1f the monster row ramps from. Fixed in passing:
  `heal_out_of_combat` did not run the `onPlayerHeal` fan-out at all, so
  **Mark of the Bloom** — whose registry row already says every caller of
  that seam must handle a 0 return — would have been silently ignored by
  the rest heal as well as this one; the suppressor is now written and
  tested (its granting event body stays S2.33's).
  **A12 stopped being a no-op** — `cardUpgradedChance` is now keyed on
  (act, ascension) at its use site (`card_upgraded_chance`,
  combat_rewards.hpp; 0.0 / A12? 0.125 : 0.25 / A12? 0.25 : 0.5), so the
  Act-1 draw keeps its stream position and Acts 2–3 get the outcome. The
  matching **`a20.yaml` A12 status flip was deliberately NOT made here** —
  this task's Acceptance says *registry untouched*, and S2.04's Log put
  the row's status under its engine owner. It is a one-row status edit
  plus its tier-2 expectation, and it should ride with whichever S2.2x
  task first makes an Act-2 card reward observable end-to-end.
  **Left for S2.13, deliberately and loudly:** the per-act event/shrine
  list rebuild. `initializeEventList`/`initializeShrineList` are draw-free,
  so the crossing touches neither membership bitset — inventing Act-2/3
  membership is S2.13's deliverable and rewriting Act-1 membership would
  be worse than stale. Until it lands, an Act-2/3 ? room that resolves to
  EVENT draws from the Act-1 list: deterministic, wrong content, named at
  the call site and pinned by a test that moves with S2.13. *(Closed
  2026-08-07 by S2.13: `reinit_act_event_pools` now runs at step (10), and
  `TheOneTimeEventPoolCarriesByReference`'s two forward-looking expectations
  moved with it.)*
- **S2.13** `[x]` **?-rooms, one-time pool, and rest sites across acts.**
  Per-act event/shrine list rebuild + the one-time pool's cross-act
  depletion semantics; the act-gated one-time draw filters
  (design §2.3); EventHelper pity reset wiring; the Recall-option probe
  (deferred row) resolved and either modeled or pinned absent.
  **Deps:** S2.02, S2.12 **Acceptance:** draw-gate tests per gated row
  (Designer/Duplicator/FaceTrader/Knowing Skull/N'loth/Joust/
  SecretPortal-pinned-false per trap 5); cross-act depletion test (Act-1
  draw removes for Act 2); six presets green.
  **Log:** 2026-08-07 — landed. **The crossing is asymmetric in three
  different directions, and the ledger's "cross-act depletion" framing names
  only one of them.** `dungeonTransitionSetup` CLEARS `eventList` and
  `shrineList` (AbstractDungeon.java:2576-2577) and the new dungeon's
  constructor REBUILDS both (:291, :293), while `specialOneTimeEventList` is
  absent from that clear list and is handed over BY IDENTITY
  (CardCrawlGame.java:1102-1119; the only `initializeSpecialOneTimeEventList`
  call site is Exordium.java:54). So: the event list returns at the new act's
  width, **all six shrines return to the pool at every act** — a shrine drawn
  in Act 1 is drawable twice more — and only the one-time pool depletes
  run-wide. `init_event_pools` is therefore **split**: `reinit_act_event_pools`
  (act crossing, event + shrine only) and `init_event_pools` (run_begin,
  delegating the shared half so the two cannot drift). Both failure modes the
  split invites are pinned negatively:
  `TheCrossingDoesNotRerunTheRunStartSpecialInit` and
  `ReinitReadsTheCurrentActNotThePrevious`.
  **`event_flags` was widened by carving declared padding, NOT by a schema
  bump.** `RunState::pad_rng_align[6]` became `pad_rng_align_lo[2]` +
  `uint32_t event_flags_hi` (ids 32..63 at bit id-33): no offset moved, no
  `sizeof`, `SCHEMA_VERSION` stays 6, every Stage-A fixture is byte-identical,
  and the arithmetic is `static_assert`ed rather than trusted (`offsetof %
  4 == 0` and "closes the hole ahead of the RNG block"). Widening
  `event_flags` in place was not available: `PublicView`'s contract forbids
  changing an existing field's width, and the differ + translator both consume
  it. `PublicView` took a legal **v3 tail append** of the same word, placed
  AFTER `action_mask` so no v2 offset — the mask channel's included — moved;
  6032 → 6036 bytes, `twins_v1.bin` regenerated via its checked-in generator
  (the fixture stamps `PUBLIC_VIEW_VERSION` and `sizeof(PublicView)`). All
  access goes through `event_flag_set`/`event_flag_test`; no call site
  open-codes the shift, and ids outside [1,63] are no-ops rather than UB.
  **Bit meaning and draw position are deliberately different mappings.** For
  the event pool they coincide (each act's ids are dense and in add order).
  For shrines they do not: Exordium ends with Wheel of Change
  (Exordium.java:238-246), TheCity and TheBeyond — byte-identical to each
  other — put it SECOND (TheCity.java:210-218 == TheBeyond.java:198-206).
  `shrine_membership` bit i keeps its registry meaning (EventId 12+i) in every
  act, and the divergence lives in a 6-entry order table used only when
  appending to the draw list. That way round because the bitset is
  byte-compared by the differ and mirrored into `PublicView`, and neither
  knows the act at compare time; it also leaves Act 1 bit-for-bit unchanged.
  This is S2.02's `ShrineListOrderDivergesBetweenActOneAndActsTwoThree`
  discharged on the engine side.
  **Verification-only, confirmed and not re-implemented:** the EventHelper
  pity reset (one call site in the whole tree, AbstractDungeon.java:2575;
  already wired at `act_transition` step (4) and pinned) — and the second,
  literal-valued pity write near `run_advance.cpp:1734` is inside `run_begin`,
  a different function, so it is not a duplicate; the six `build_shrine_pool`
  act gates, which S1 had already written correctly against :1889-1933,
  including N'loth's same-test-written-twice (`!id.equals("TheCity") &&
  !id.equals("TheCity")` — one test, Act 2 only, NOT "any act but 2").
  **Genuinely new gates:** Beggar (`gold >= 75`, :1970-1973 — s2-design §2.3
  had omitted it), Colosseum (`currMapNode.y > map.size()/2`; `map` is the
  15-ROW list so the divide is 7 and the gate is row >= 8, derived from
  RunState via a new `event_map_row` pinned equal to `run_cur_row` across
  every act × floor — **S2.33 inherits this, it does not re-derive it**), and
  The Moai Head (`hasRelic("Golden Idol") || (float)hp/(float)maxHp <= 0.5f`,
  written as a float divide per trap 19).
  **Two comments that had become lies are rewritten rather than extended:**
  `build_event_pool`'s `default:` arm no longer claims Moai Head / Beggar /
  Colosseum "guard act-2/3 keys Exordium never holds", and the
  raw-nonempty-but-filtered-empty shrine deviation no longer claims structural
  unreachability — that argument DIES IN ACT 3, where FaceTrader is
  act-excluded and SecretPortal is pinned false, so the state is constructible
  and the deviation is real (the game would evaluate `tmp.get(rng.random(-1))`
  at :1937 and crash after burning one counter tick). Pinned by
  `ARawNonemptyButFilteredEmptyShrinePoolReturnsZero`.
  **Deferred rows:** the Recall probe is **DISCHARGED — YES, already modelled,
  zero engine work**, and its premise was wrong about scope
  (CampfireUI.java:94-96 has no act test at all, so the button is on every
  Act-1 rest too, and the ruby-key grant is live, not stubbed). **Three new
  rows opened**: the translator's act-local FIRED derivation (S2.43 — a
  differ false-RED waiting on the first Act-2 shrine capture, because an
  Act-2 dump cannot witness an Act-1 shrine fire while the sim rightly
  carries it); SecretPortal's pin now resting solely on unmodelled playtime
  (S2.33); and `seed_scan`'s planner-side one-word decode (S2.42 —
  `tools/oracle_bridge/planner` was off limits here, so it under-reports
  Act-2/3 fires; the `tests/seed_scan_test.cpp` guard stays true and its
  comment now points at the row instead of at S2.13).
  **For S2.31–S2.33:** an Act-2/3 `?` room now selects the right id, commits
  the right pool bit and the right flag word, and parks at
  `ROOM_UNIMPLEMENTED` — each body only has to fill `event_dialog_impl` and
  flip `implemented: true`. **No S2.3x task should re-touch
  `build_event_pool` / `build_shrine_pool` / the membership masks.** And
  `ActOneUnreachableSpecialsAreExactlyTheUnimplementedOnes`
  (`act_event_lists_test.cpp`) is **S2.3x's to retire**: it still holds today
  because S2.13 lands no bodies, and it will go red the moment
  Designer/Duplicator/Knowing Skull/N'loth/Joust get one.

## Phase S2.2 — Monster batches (each = YAML rows + engine bodies + tier-2, the B3.13–B3.22 pattern; ∥ across disjoint batches once S2.01 lands)

- **S2.2F** `[x]` **Shared monster framework (added 2026-08-07 from the six
  batch scout dossiers; lands FIRST, serially — every remaining batch builds
  on it).** The one CombatState schema bump (SCHEMA_VERSION 6→7,
  owner-approved: `kMonsterCap` 7→24 with an explicit sizeof ceiling probe;
  `MonsterState` position/draw_x key so spawn slots stop being hand-derived;
  the OBTAIN_CARD in-combat master-deck accumulator drained by run_advance —
  Omamori-safe by construction; `kMonsterFlagHalfDead` global bit 25) with
  the 20 combat fixtures regenerated exactly once. Death edges:
  `MonsterDieFn` → bool veto (Darkling/Awakened One suppress `super.die()`),
  new `MonsterDieAfterFn` (Reptomancer/Automaton/Collector post-die bodies),
  `monster_basically_dead` split from `monster_dead_or_escaped` with the
  ~30 call sites classified (targeting counts halfDead dead;
  combat-over/turn-queue counts it alive). Hooks 15–17 dispatch sites
  (DURING_TURN at the action_queue stub; ON_AFTER_USE_CARD at the
  interp_cards reserved seam; ON_INFLICT_DAMAGE on the attacker's powers
  after wasHPLost). Opcodes 68–70 (OBTAIN_CARD, CLEAR_CARD_QUEUE,
  END_PLAYER_TURN — the forced-turn-end verb). Spawn conventions ratified
  (HP roll at takeTurn into `amount`; spawned monsters run pre-battle;
  SUICIDE triggerRelics arm implemented; queue index remap verified with a
  directed test — two scouts disagreed on whether it exists).
  `MONSTER_ROLL_TIMINGS` 2 CONSTRUCTOR_BEFORE_HP. Full grant table:
  [stage-b-tasks.md](stage-b-tasks.md) "S2 Wave-3 allocations".
  **Deps:** — **Acceptance:** a directed test per new surface (hook
  dispatch, cap growth past 7 live records, die veto both edges, basically-
  dead split at a classified site of each kind, accumulator drain vs
  Omamori, END_PLAYER_TURN mid-queue); fixture regeneration recorded; six
  presets green; layout probes (`offsetof`/`sizeof`) not predictions.
  **Log:** **`kMonsterCap` is 23, not the granted 24** — the one deviation, and
  it is arithmetic, not judgement. 24 measures `sizeof(CombatState)` **8304**
  against the frozen 8192 B ceiling; 23 measures **8088** (headroom 104). The
  grant was estimated against `MonsterState` 116 B / `CombatState` 3928 B,
  which are PRE-schema-v6 numbers — the v6 `PowerSlot` widening (4 → 8 B over
  24 slots) took `MonsterState` to 212 B and nearly doubled the per-slot cost.
  Owner-adjudicated at 23 with no ceiling change. The measured cap/size table,
  and the two rejected alternatives (raise the ceiling; split `kPowerCap` into
  a smaller per-MONSTER power cap, which does fit 24 at 7504 B), are recorded
  in `combat_state.hpp` so nobody re-derives them. Note 24 measures 8304 and
  not the naive 8300: slot-count parity moves the implicit tail padding by 4 B,
  which is why these are probe results and not arithmetic. 23 vs 24 costs
  nothing real — none of the three consumers has a derived safe bound, so both
  are budget picks and the hard assert is what protects the invariant.

  **The bump was NOT CombatState-side, contrary to the dispatch brief.**
  `PublicView` embeds `PvMonster monsters[kMonsterCap]`, the
  `monster_roll_known`/`monster_roll` pair, and — through the embedded mask
  channel — `RunActionMask`'s per-(card, monster) and per-(potion, monster)
  target grids. The monster block is MID-RECORD, so `kPublicViewFixedBytes`
  moved 5656 → 8312, `sizeof(PvMask)` 376 → 616, `sizeof(PublicView)`
  6036 → 8932 and `OmniscientObsBuffer` 240 → 656. That makes
  **`PUBLIC_VIEW_VERSION` 3 → 4 the first BREAKING public-view bump**: no
  in-place reinterpretation of a v1–v3 record exists, so v1–v3 shards are
  reanalyze-or-quarantine. Classified against the audit's already-enumerated
  "any capacity change" case, which named `kMonsterCap`. Twins regenerated.
  Found by probing, not predicted — the brief expected twins not to move.

  **Fixture regeneration proven, not asserted.** All 20 combat fixtures are a
  pure ARRAY EXTENSION: per record, bytes `[0, 3388)` identical, a 3392-byte
  (16 × `MonsterState`) run of ZEROS inserted at the old `monsters[]` end, and
  the remainder identical — verified byte-for-byte against `HEAD` for every
  record of every file. Headers differ only in `state_size`; the on-disk tag
  stays `kTraceFormatV1` (=1), so **the B1.6 v1 compatibility read is
  RETAINED** and only `state_size` moves. `kTraceFormatV2` follows to 7.

  **The queue-remap disagreement was never a disagreement.** `spawn_monster_at_slot`
  DOES remap `monster_queue` indices ≥ slot and does NOT remap pending
  `action_queue` items. The two scouts were describing different queues and
  both were right; the header already documented both halves. Pinned with one
  directed test asserting both directions at once.

  Other surfaces, all landing with NO producer and each with a directed test
  driving it through a synthetic fixture: `kMonsterFlagHalfDead` (global bit
  25) and the `monster_basically_dead` split, with every one of the ~26
  `monster_dead_or_escaped` call sites classified (19 moved to the
  basically-dead sense, 7 stayed targeting) and every stale "halfDead has no S1
  producer" comment rewritten per conventions §8 — including the two that had
  become *wrong*: `power_regenerate_monster.cpp`'s guard is now load-bearing
  (the end-of-round walk no longer filters half-dead monsters out for it), and
  the Feed / Hand-of-Greed `|| halfDead` terms are now implemented rather than
  documented-inert. `MonsterDieFn` gains a **bool veto** (`true` == suppress
  `super.die()`, for the Darkling's and Awakened One's `if (!cannotLose)`
  overrides) and a new **`MonsterDieAfterFn`** slot for the post-`super.die()`
  bodies (Reptomancer, Bronze Automaton, The Collector, Awakened One) — two
  slots rather than a phase argument, so each keeps its ordering claim
  literally true. `SUICIDE`'s `triggerRelics` arm implemented (it read
  `flags` bit 0 nowhere before). Hooks **15 `DURING_TURN`** (at the
  `applyTurnPowers` stub, whose "no monster powers with a turn hook" comment
  was DELETED — the prerequisite arrived), **16 `ON_AFTER_USE_CARD`** (at the
  reserved `interp_cards.cpp` seam, likewise deleted rather than amended) and
  **17 `ON_INFLICT_DAMAGE`** (the ATTACKER's power list, after `wasHPLost`);
  `kHookCount` → 18, and the `powers.hpp` byte-equal chain gained the three
  plus **`ON_POWER_REMOVED` (14), which had been missing since it landed**.
  Opcodes **68 `OBTAIN_CARD`** (accrues into the new zero-cost `CombatState`
  accumulator; drained by `run_advance` EVERY PUMP STEP, not at the fold-back,
  because deferring it would let the fold's relic-counter copy clobber
  Omamori's decrement), **69 `CLEAR_CARD_QUEUE`** and **70 `END_PLAYER_TURN`**
  — the last two carrying the `limbo` trap explicitly: the Java's
  `player.limbo` is the autoplay group, this engine's `limbo` is `cardInUse`,
  and a literal port would exhaust the card being played, so only the
  queue-clear half is modelled. `MONSTER_ROLL_TIMINGS` **2
  `CONSTRUCTOR_BEFORE_HP`**, which forced `burn_unspawned_ctor_rolls` into a
  genuine TWO-PASS walk around the `setHp` draw — same draw count, different
  order, and the order is the entire product of that function. Spawn
  conventions ratified: `MonsterState.draw_x` (free, in what was `pad1[2]`) +
  `smart_position_for`, which reproduces `getSmartPosition`'s **break**
  semantics rather than a count, and an OPT-IN pre-battle-on-spawn arm
  (`SPAWN_MONSTER` flags bit 16) because `SpawnMonsterAction` does not run it
  and `SummonGremlinAction` does.

  **One latent defect found and fixed in passing.** `power_dark_embrace.cpp`
  walked `hp > 0` under a comment that said `areMonstersBasicallyDead` — so
  Dark Embrace kept drawing after a Looter ESCAPED. Two neighbouring sites
  (Dead Branch, Magnetism) cite Dark Embrace as using "the same gate"; that was
  false until now. Found by classifying the call sites, not by a failing test.

  **Named residue, not stubbed.** The attacker-side cancel
  (`DamageAction.java:69-73`, `info.owner.isDying || info.owner.halfDead`
  cancels a queued multi-hit attack's remaining hits) is unmodelled in BOTH
  terms; the `isDying` half is a pre-existing divergence whose fix would move
  landed Act-1 behaviour and committed fixtures. A comment at `op_damage` names
  it and asks whichever batch lands the first halfDead producer to implement
  both terms together. *(The halfDead batches landed without it; discharged
  later by S2.49 — the guard is live as `damage_attacker_cancelled`,
  `interp_damage.cpp`, both terms together, and no committed fixture moved.)*
  Also: no monster is yet both mid-combat spawnable and
  pre-battle-bearing, so the pre-battle arm's test drives the wiring and both
  halves of the precondition rather than a stream side effect.

  `check_stale_counts` + `check_doc_links` clean; design §11 v0.1.10 entry;
  **A third-occurrence padding defect, found by the `win-*` half of the preset
  matrix and fixed in the follow-up commit.** The first pass of this task
  reported "six presets green" having run only the three WSL ones; the skipped
  half caught a real bug.
  `ThreeActSim.ScriptedPolicyRunCompletesDeterministicallyTwiceWithIdenticalHashes`
  failed on **`win-asan` only** -- two runs of one seed diverging at hash step 6.
  Root cause: `MonsterLists`' three 7-byte alignment gaps and `RunController`'s
  4-byte gap before `lists` were IMPLICIT padding inside a byte-hashed struct.
  They had been NOTICED -- `byte_class.hpp` carried all four as `STS_BC_GAP`
  rows -- and that is exactly why nothing caught them: a declared gap TILES as
  well as a declared member and is still never WRITTEN. The injecting site is
  `rc.lists = MonsterLists{}`: `MonsterLists{}` on an aggregate is
  aggregate-initialisation ([dcl.init.list]/3), which initialises members and
  leaves padding alone, and the trivially-copyable assignment then memcpys that
  temporary's indeterminate padding into the hashed controller. This task did
  NOT create the defect -- growing `sizeof(RunController)` merely moved the
  stack frame enough that two calls stopped landing on identically-dirty memory,
  which is why it had hidden on Linux (fresh pages read zero) since the gaps
  were introduced. All four are now declared `pad_*` members; no offset, no
  `sizeof`, no fixture and no `SCHEMA_VERSION` moved. Conventions section 8
  gained the third-occurrence entry and the structural elimination
  (`Tripwire.NoDeclaredGapsInByteHashedStructs` +
  `Tripwire.EveryByteOfAByteHashedStructBelongsToAMember`), both confirmed RED
  against the unfixed tree first -- the witness named `MonsterLists` byte 257,
  the same offset the failing sim diverged at.

  `check_stale_counts` + `check_doc_links` clean; design section 11 v0.1.10
  entry; audit v4 entry. **All six presets green** -- debug / asan / release via
  `wsl_run.sh`, win-debug / win-asan / win-release via clang-cl.

- **S2.21** `[x]` ∥ City normals I — Chosen (27), Byrd (28), Shelled
  Parasite (29), Spheric Guardian (30); `powers.yaml` **Hex (93)** and
  **Flight (94)** — the only two NEW power rows the batch needed.
  *Corrected from the original block text, which listed
  "Hex/Flight/PlatedArmor/Barricade/Malleable-family power rows".* Plated
  Armor (17) and Barricade (48) were **already registered** and needed no
  row: this batch only routes them — Plated Armor gained an
  `on_power_removed` binding (its `onRemove` had never been modelled) and
  Barricade's monster-side presence test was already live in
  `apply_pre_turn_logic`, so only its id-48 provenance line, which read
  player-only, was amended. **Malleable is S2.22's row (95), not this
  task's** — the design doc and the Wave-2 allocation table both say so,
  and this line was the stale parenthetical they overrode.
  **Deps:** S2.01 **Acceptance:** per-monster move/stat tables pinned
  against every ascension branch read in full; encounter compositions
  spawn-order-exact; six presets green.
  **Log:** `Hook::ON_POWER_REMOVED = 14` added as framework, dispatched
  from `remove_slot_at` — the single choke point every destruction path
  (`REMOVE_POWER`, `REDUCE_POWER`-to-zero, `REMOVE_DEBUFFS`) reaches, and
  the first hook that fires ONE body (the removed power's own) rather than
  fanning out. It exposed a real defect: Plated Armor's native body
  decremented its own slot and zeroed `power_id` in place, which never
  reached that choke point, so the Shelled Parasite's ARMOR_BREAK stun
  could never fire. Rerouted through a queued `REDUCE_POWER`, which is
  also what `PlatedArmorPower.java:58` does — the in-place write was a
  timing deviation too. RED-first evidence recorded: with the pre-fix body
  restored, `CityNormalsI.PlatedArmorRunningOutStunsTheShelledParasite`
  fails with `move_history[0] == 1 (FELL)` instead of `4 (STUNNED)`.
  Opcode **66 `VAMPIRE_DAMAGE`** (single-target monster-sourced lifesteal;
  the heal is applied inline, which is exact — the Java `addToTop`s it
  ahead of anything the hit queued at the front). Monster-flag bits
  **0x8000** (Byrd `isFlying`) and **0x10000** (Spheric `secondMove`) —
  fresh bits, argued in-row against reuse; the third granted bit (Chosen
  `usedHex`) proved unnecessary at A20 and is RELEASED. `MonsterId` **31**
  released unspent (permanent gap). All six presets green (`ctest -N` for
  the current suite size).
- **S2.22** `[x]` ∥ City normals II — Mugger, Snake Plant, Snecko,
  Centurion + Healer (2 Thieves / Snake Plant / Snecko / Centurion and
  Healer / 3 Cultists / Cultist and Chosen groups); `powers.yaml`
  **Malleable (95)** — the batch's whole `PowerId` grant, spent.
  **Deps:** S2.01 **Acceptance:** as S2.21.
  **Log:** `MonsterId` **32–36** all spent (Mugger, Snake Plant, Snecko,
  Centurion, Healer); id **31 stays S2.21's permanent gap**. `PowerId`
  **95 MALLEABLE** spent. Opcode **67 `BLOCK_RANDOM_MONSTER`** (the
  Centurion's Protect): an opcode rather than a BLOCK step because the
  recipient AND whether any `ai_rng` draw happens at all are execute-time
  facts — an empty valid list spends ZERO draws, and the exclusion reads
  the TELEGRAPHED `Intent.ESCAPE`, not the escaped flag, so a thief that
  has merely announced its exit is already skipped while still fighting.
  Opcode **`HEAL` (39) extended** with the monster branch
  (`AbstractMonster.heal`, AbstractMonster.java:383-399 — a genuinely
  different method from `AbstractCreature.heal`, with no relic pass and no
  `isEscaping` test), and the `interp_damage.cpp` comment that justified
  the old no-op was DELETED rather than amended: it was a
  "prerequisite has not arrived" comment and the prerequisite arrived
  (conventions §8). New **`MonsterDieFn`** dispatch slot, fired at both
  monster-death edges strictly BEFORE `dispatch_on_death` (the subclass
  body runs before `super.die()`); its one entry is the Mugger, whose
  `playDeathSfx` draws a SEEDED `aiRng.random(2)` where the Looter's
  identically-shaped method rolls unseeded MathUtils. Same divergence on
  the per-attack `playSfx`, and the Mugger's talk gate fires on the SECOND
  Mug (`slashCount == 1`) against the Looter's first.

  **Stolen gold: the deviation is GONE.** `settle_stolen_gold` now
  reconstructs the game's per-steal clamp instead of summing then
  clamping. The order needs no new state: both thieves steal on
  consecutive turns from turn 1, so a thief's k-th steal is its turn k and
  the interleaving follows from the steal counts plus slot order. The two
  models agree on the TOTAL and disagree on ATTRIBUTION, which is why only
  a two-thief group could expose it; RED-first evidence recorded in-test —
  purse 30, one steal each, the MUGGER killed returns **10** where the old
  body returned 20. The LOOTER filter is generalised to an
  `is_thief` / `thief_gold_amount` / `thief_stolen_gold` interface
  (`monster_looter.hpp`), and the two `goldAmt` constants stay SEPARATE
  because the Java fields are.

  **Confusion's slot amount: adjudicated by EVIDENCE, not by consistency.**
  The corpus has it — `tests/golden/oracle_corpus/act1_a20_50` carries
  `{"amount": -1, "name": "Confusion", "id": "Confusion"}`. So the engine
  was wrong: it wrote 1, from a `relics_boss.cpp` comment asserting a
  "default 1" the Java does not have (the 3-arg `ApplyPowerAction`
  forwards `powerToApply.amount`, and `ConfusionPower` never assigns one,
  so it is `AbstractPower`'s `-1` field initializer). Fixed in place for
  BOTH producers — Snecko Eye and the Snecko's GLARE — through the shared
  `kConfusionAppliedAmount`, plus `AbstractPower.stackPower`'s
  `amount == -1` early return in `op_apply_power`, which those two
  producers together make reachable. Behaviourally inert, oracle-visible.
  **No S2.43 note was needed**: the evidence branch of the adjudication
  fired.

  **One divergence found and FIXED rather than recorded.** The Centurion's
  `aliveCount` and the Healer's `needToHeal` are the first getMoves to read
  the GROUP during `init()`, and this engine folds each monster's ctor and
  init into one call while the game constructs every member first and
  `MonsterGroup.init()`s them second (MonsterGroup.java:31-33,62-66). A
  Centurion at slot 0 would have decided its OPENING telegraph against a
  still-zeroed slot 1 and could never have opened on PROTECT.
  `spawn_group` / `spawn_group_trace` now pre-mark the group's slots as
  constructed-and-alive before running any init; every init overwrites its
  own slot, so no existing monster's behaviour and no fixture moves.

  Released / not spent: **ZERO new `MonsterState.flags` bits** — the
  Mugger's `slashCount` reuses `pad0` as the Looter's does, and the
  Snecko's `firstTurn` needs no storage at all (it is consumed on the init
  rollMove, the Chosen `usedHex` precedent), so its granted `pad0` slot is
  RELEASED — and **no new `MonsterIntent`** (`ESCAPE` 13 exists; 14 stays
  reserve). `last_move_before_is` was PROMOTED out of
  `monster_gremlin_nob.cpp` to `monster_dispatch.hpp` (rule of two,
  conventions §7 — the Snake Plant's A17 arm is the second reader). Two
  schema limitations are recorded rather than worked around: the Snecko's
  A17-only Weak step and the Healer's per-member fan-out are PRESENCE and
  COUNT facts an effect list cannot express, so both rows author the exact
  amounts and the module owns the shape, through the new per-step form of
  the existing helper (`queue_monster_move_effect`). The Healer's
  `ENC_NAME "HealerTank"` is dead content and is deliberately unregistered;
  the Centurion's third `playSfx` branch is unreachable and unseeded.
  All six presets green.
- **S2.23** `[x]` ∥ City elites — Gremlin Leader (minion mechanics +
  spawnGremlin), Slavers (Taskmaster + S1 slavers), Book of Stabbing;
  `powers.yaml` **Minion (96)** and **Painful Stabs (97)** — the batch's
  whole `PowerId` grant, spent.
  **Inherited:** the stage-b Gremlin move-99 escape row (see Deferred
  obligations).
  **Deps:** S2.01, S2.2F **Acceptance:** as S2.21, plus escape-trigger tests and
  the stage-b row discharged in the same commit.
  **Log:** `MonsterId` **37–39** all spent (GREMLIN_LEADER, TASKMASTER
  with `game_id "SlaverBoss"`, BOOK_OF_STABBING); id **31 stays S2.21's
  permanent gap** and 40–48 are untouched. `PowerId` **96 MINION** and
  **97 PAINFUL_STABS** spent. `kMonstersCount` 34 → 37, `kPowersCount`
  56 → 58; every count-guard site (six in `monster_dispatch.cpp`, one in
  `interp_block.cpp`, three in `interp_damage.cpp`) answers its own
  question in-comment, and all four power guards took the move
  **caseless** — Minion has no override past `updateDescription`, and
  Painful Stabs' `onInflictDamage` returns nothing and cannot move a
  damage number.

  **Encounters needed ZERO edits.** Ids 35 "Gremlin Leader", 36
  "Slavers", 37 "Book of Stabbing" and the EVENT-pool 42 "Colosseum Nobs"
  were already landed by S2.01 and resolve their `EMIT` targets as game-id
  strings against `monsters.yaml` at spawn time, so registering the three
  init fns un-parked all four with **no `encounters.yaml` and no
  `run_advance.cpp` change** (the B3.16 precedent). **S2.32's Colosseum is
  unblocked by this batch** — "Colosseum Nobs" is Taskmaster + Gremlin Nob
  and both rows now exist.

  **The Taskmaster draws `monster_hp_rng` TWICE, and nothing else in the
  roster does.** The `super(...)` argument list literally contains
  `monsterHpRng.random(54, 60)` (Taskmaster.java:50), which Java evaluates
  before the constructor body, and then `setHp(57,64)` draws again
  (:52-56). The first value is overwritten and invisible in the HP; the
  only thing it does is move the stream — and the Taskmaster sits at group
  index 1 of "Slavers", so the Red Slaver's roll shifts. Landed as
  registry DATA (row `SUPER_ARG_HP`, timing S2.2F's
  `CONSTRUCTOR_BEFORE_HP`, range the FLAT literal `(54,60)` at every
  ascension — only the `setHp` under it is branched), so
  `burn_unspawned_ctor_rolls` orders it correctly too. Pinned by
  `CityElites.TaskmasterExtraDrawShiftsTheRestOfTheSlaversGroup`, which
  re-derives the Red Slaver's HP by hand off the same seed.

  **`GremlinLeader.getMove` RECURSES with fresh seeded draws** (:163,
  :174), and termination is probabilistic, not bounded: a re-drawn 80 on
  the `num >= 80 && lastMove(STAB)` arm re-enters the SAME arm. Modelled
  as a re-draw loop, not an unroll. Both recursive arms have directed
  tests, the second driven by a seed searched to force a second recursion.

  **A RALLY turn's stream order is the batch's most fragile fact, and it
  is fixture-pinned.** `SummonGremlinAction`'s CONSTRUCTOR runs at
  `addToBottom`, so BOTH summons' `aiRng` pool picks and BOTH gremlins'
  `monster_hp_rng` `setHp` draws happen at QUEUE time; the children's
  `init()` rolls happen at resolve; and the leader's own trailing
  `RollMoveAction` lands **third** on `ai_rng`, because both spawns were
  queued ahead of it while the Minion/Angry items each spawn appends land
  behind it. The **summon pool is `ai_rng`** while S2.01's
  identically-membered **encounter pool is `misc_rng`** — same eight
  entries, two different streams, deliberately not unified.

  **`gremlins[3]` needed NO storage — zero flag bits, zero `pad0`.**
  `identifySlot`'s answer is a pure function of which of
  `GremlinLeader.POSX = {-366,-170,-532}` currently has a LIVE record, and
  `MonsterState.draw_x` (S2.2F) already stores exactly that key. The
  alternative — three record indices in the leader — was rejected because
  record indices SHIFT on every insertion, so a stored map would need
  remapping mid-summon; the derivation cannot go stale, and it is EXACT
  because a slot is only reassigned when its occupant is null-or-dying and
  a dying record never revives. **Book of Stabbing's `stabCount` is the
  batch's only `pad0` user**, and it SATURATES at 255 rather than wrapping
  (the Guardian shift-count precedent).

  **The spawn path gained the summon pattern S2.24/S2.27 consume**, as two
  `SPAWN_MONSTER` `flags` bits plus a field, all on the existing opcode:
  bit 17 `kSpawnApplyMinion` (queue `ApplyPowerAction(m, m, MinionPower)`
  at amount −1, BEFORE the pre-battle bit's items — the Java's order, and
  observable as `[Minion, Angry]` on a summoned Warrior) and bits 18–31 a
  signed 14-bit `draw_x`. The `draw_x` operand corrects a
  `monster_dispatch.hpp` claim rather than working around it: the position
  key is per-SPAWNER-per-SLOT, not per-TYPE, so a shared spawn-at-hp init
  (the five gremlins share theirs) cannot know it — the header's "WHO SETS
  `draw_x`" paragraph is amended in place. **Zero is the identity**, so
  both large-slime split sites are byte-unchanged.

  **All five S1 gremlins became mid-combat spawnable** (five new
  `MonsterSpawnAtHpFn` entries; the `default: nullptr` and the "nothing
  spawns them mid-combat" paragraph for the slavers both stay true and are
  extended rather than contradicted). Their HP arrives PRE-DRAWN even
  though the Java runs the full gremlin constructor, because that
  constructor runs inside the summon action's constructor.

  **Minion un-parked two sites that named this landing verbatim.**
  `op_damage_feed` and `op_damage_greed` both carried
  `!(!isDying && currentHealth > 0 || halfDead || hasPower("Minion"))`
  with the Minion term documented inert; both now test it through a shared
  `monster_has_minion_power`, and the comments were REPLACED, not amended
  (conventions §8), in `interp_damage.cpp`, `interp.hpp`, `cards.yaml`
  (Feed + Hand of Greed provenance) and `stsgen/vocab.py`. Killing a
  Gremlin Leader minion now grants no max HP and pays no gold. The
  `cards.yaml` provenance edits forced the documented
  `cards_sidetable.json` regeneration — a hash-line-only diff, as that
  file's own `_provenance.regenerate` predicts.

  **Painful Stabs is the FIRST binder of `Hook::ON_INFLICT_DAMAGE` (17)**,
  which S2.2F landed with its dispatch site and no producer. Native for
  the `!= THORNS` guard only (the Rage/Hex precedent); the effect is an
  ordinary `MAKE_CARD` Wound into DISCARD. Per HIT, so a `stabCount` of 5
  makes five Wounds. The `damageAmount > 0` half of the Java guard is the
  dispatcher's and the body re-tests it anyway, so the split is checkable.

  **`MonsterDieAfterFn` gained its first entry** (Gremlin Leader), and all
  three elites' `die()` overrides are spelled as explicit cases: the
  Taskmaster's and the Book's are UNSEEDED `MathUtils` sounds on the
  post-super side, so both are `nullptr` in `monster_die_fn` with the
  reading recorded.

  Released / not spent: **ZERO new `MonsterState.flags` bits** (the
  leader's slot map derives from `draw_x`; the Book's counter reuses
  `pad0`), **ZERO new opcodes** (the summon pattern is two bits and a
  field on the existing `SPAWN_MONSTER`), **ZERO new hooks** (S2.2F's 17
  was the grant and this batch is its binder), **ZERO new intents**
  (`UNKNOWN`/`ATTACK`/`DEFEND_BUFF`/`ATTACK_DEBUFF`/`ESCAPE` all exist).
  Two schema limitations are recorded rather than worked around, both the
  S2.22 shape: the Taskmaster's A18 self-Strength and the Book's
  `stabCount` repetition are ascension-PRESENCE and per-instance-COUNT
  facts an effect list cannot express, so the rows author the exact
  amounts and the modules own the shape. `GremlinLeader.ENC_NAME`
  ("Gremlin Leader Combat") is dead content and is deliberately
  unregistered (the Healer `HealerTank` precedent); the Taskmaster's
  `damage.get(0) == 4` (WHIP) is dead and no move row carries it. Every
  `A_2_*` constant in this batch names a branch that is not `>= 2` — the
  columns follow the branch, and `a3`/`a8` parse with no schema change.
  All six presets green.
- **S2.24** `[x]` ∥ City bosses — Bronze Automaton (+ BronzeOrb, Stasis
  model), The Champ, The Collector (+ TorchHead). A2/3/4-A19 columns per
  boss.
  **Deps:** S2.01, S2.2F, S2.23 (MINION row + spawn pattern) **Acceptance:** as S2.21, plus boss-flag typing
  (Pantograph-style consumers) and A13 gold tests.
  **Log:** 2026-08-09. `MonsterId` **40–44** all spent (BRONZE_AUTOMATON,
  BRONZE_ORB, CHAMP, THE_COLLECTOR, TORCH_HEAD — the block exact; 45–48
  stay UNISSUED, not this batch's to backfill) and `PowerId` **98 STASIS**
  spent. Opcode **71 APPLY_STASIS** spent as allocated and **72 — the
  contingency the grant said to release — SPENT instead, as
  `STASIS_RETURN`**: the give-back is a queued action with a runtime
  pool-index operand and a queue-time destination bit, which no existing
  opcode carries (`MAKE_CARD` builds a fresh library copy and would drop
  the stolen instance's upgrade/misc/permanent-cost state that
  `makeSameInstanceOf` preserves — the engine moves the ORIGINAL pool row
  out of limbo instead). Zero new `MonsterState.flags` bits (the four
  type-scoped latches — Champ `thresholdReached`, Orb `usedStasis`,
  Collector `initialSpawn`/`ultUsed` — REUSE the Hexaghost/S2.28 four
  0x0800–0x4000, now a THREE-way share: one boss encounter per combat, so
  an Act-2 boss record can co-occur with neither owner), zero new intents,
  zero new hooks, no schema bump and no `PUBLIC_VIEW_VERSION` bump — the
  stolen card parks in the LIMBO pile, which the view already projects in
  engine order, and the pool index rides the Stasis slot's schema-6
  `counter` (+1-biased) through the existing APPLY_POWER counter operand.
  `kMonstersCount` 50 → 55, `kPowersCount` 69 → 70, `kTotalCount` 566 →
  572; every count-guard site moved together, all four power guards
  caseless (StasisPower overrides only updateDescription + onDeath).

  **Encounters needed ZERO edits** (the S2.23 precedent, one act later):
  "Automaton" 38 / "Collector" 39 / "Champ" 40 were S2.01's rows, and
  registering the three boss init fns un-parked the Act-2 boss ROOM —
  `BossVictory.TheActTwoBossRoomEntersARealCombatAndPaysA13ScaledGold`
  drives the real act transition and pins both the un-park and the batch's
  A13 share: the Act-2 boss reward's gold item is the FIRST x0.75-scaled
  payout a player can actually claim, so **a20.yaml row 13 flipped to
  IMPLEMENTED** (the Act-3 draw-and-discard half was S2.28's; the
  a20_modifiers_test prefix pin moved with the row) and the >= 13 / < 13
  boundary is pinned per-seed in combat_rewards_test. Boss-flag typing:
  `enemy_type: BOSS` on exactly the three bosses — the two minions stay
  NORMAL, and the Pantograph test pins that the heal keys on the BOSS
  record found past a NORMAL minion, never on the minion.

  **The Minion apply is NOT the S2.23 spawn bit, and the shared-header
  claim that said it would be was amended in place.** SpawnMonsterAction
  with isMinion=true (both summoners' spawner, SpawnMonsterAction.java:
  67-69) applies its MinionPower **addToTop at the spawn's own resolve**,
  where `kSpawnApplyMinion` queues addToBottom (SummonGremlinAction's
  order). Both modules therefore leave the bit CLEAR and queue an explicit
  APPLY_POWER item immediately BEHIND each spawn item — the spawn's
  resolution queues nothing ahead of itself, so "next item" IS the
  addToTop position — and interp.hpp's bit-17 paragraph, which named the
  orbs and torch heads as future users, now says why they are not
  (conventions §8: the claim was corrected where it lives). The brief's
  other guess died the same way: the Automaton's Artifact is a FLAT 3 at
  every ascension (BronzeAutomaton.java:103 — no branch), while HYPER_BEAM
  does tier (45/50 at >= 4, the same ladder as FLAIL/strAmt, not a
  beam-only one), and BOOST is the roster's first move whose two steps
  tier on DIFFERENT boundaries (block >= 9, Strength >= 4) — both pinned.

  **A SPAWN turn's stream order is the Gremlin Leader's, minus the pool
  picks**: both minion ctors run in the SpawnMonsterAction ARGUMENT LIST
  at addToBottom time, so monster_hp_rng sees super-arg (flat, the
  registry `SUPER_ARG_HP` rows — S2.2F's CONSTRUCTOR_BEFORE_HP timing,
  read from the same rows `burn_unspawned_ctor_rolls` walks) then tiered
  setHp, per minion, in spawn order, ALL at queue time; each spawn's ONE
  ai_rng init roll lands at resolve and the summoner's own trailing
  RollMoveAction THIRD, at a pre-computed post-insertion index (the
  queue_rally local-simulation shape, one copy per summoner — smart
  positioning reads only `draw_x`, which nothing in the window mutates).
  Layouts fall out of the POSX tables: `[orb(-300), boss(-50), orb(200)]`
  and `[torch2(-470), torch1(-285), collector(60)]`, spawn-order-pinned.

  **The Collector's revive map is the Gremlin Leader derivation, re-run.**
  `enemySlots` is a HashMap<Integer,·> whose keys 1,2 iterate in bucket
  order 1 then 2 (Integer.hashCode == value, 16 buckets) — so REVIVE
  constructs slot 1's replacement before slot 2's — and the map itself
  derives from `draw_x`: slot k spawned-at-least-once == any TORCH_HEAD
  record at x_k, its newest occupant dying == no LIVE record at x_k
  (exact because a slot is only re-filled when its occupant died and dead
  records never revive). The replacement inserts BEFORE the corpse it
  replaces — strict `>` stops at an equal draw_x — pinned. REVIVE has no
  once-per-combat latch; `kMonsterCap` 23 absorbs the growth (S2.2F's
  budget note named this boss). `initialSpawn`/`ultUsed` are
  takeTurn-time writes (:133,:159), NOT decision-time ones — the reason
  they hold real bits while the Automaton's `firstTurn` is consumed on
  the init roll (the Snecko precedent, no storage) and the Orb's
  `usedStasis` latches inside getMove at DECISION time.

  **Stasis end to end.** APPLY_STASIS is authored in the orb's move row
  (the BLOCK_RANDOM_MONSTER shape — `src` is the only operand) and its
  body is all execute-time: both piles empty → done before ANY draw and
  before the power exists; the DRAW pile is preferred and the DISCARD
  used only when it is empty; the pick is the RARE → UNCOMMON → COMMON →
  unfiltered cascade of `getRandomCard(cardRandomRng, rarity)`, each
  NON-EMPTY filtered view costing ONE draw over its Collections.sort()ed
  membership — **cardID compare, a STABLE sort, so two Strikes keep pile
  order** — and each empty view returning null for FREE; the unfiltered
  fallback indexes PILE ORDER, unsorted. That cascade forced rarity out
  of documentation: the rows' `rarity:` column is now a GENERATED,
  loader-validated `card_rarity(CardId)` table (vocab CARD_RARITIES, the
  full Java enum — a BASIC Strike matches no pass, every status is
  COMMON, Ascender's Bane is SPECIAL and only the fallback can take it).
  The theft calls `knowledge_on_remove_known` — the identity is
  player-visible (ShowCardAction) and an exact-prefix position excludes
  the stolen card having sat inside it, so the existing removal semantics
  are exactly sound. The give-back models BOTH hand reads: onDeath picks
  HAND vs DISCARD at QUEUE time (`hand.size() != 10`) and the HAND arm
  re-checks the cap at RESOLVE (MakeTempCardInHandAction's spill), each
  with a named test. Both summoners' die() sweeps are post-super
  (`monster_die_after_fn`, the Reptomancer shape) with the 1-arg
  SuicideAction's relicTrigger TRUE — so killing the Automaton runs each
  orb's full death edge and every stolen card comes home while the
  victory queue drains, pinned. The Champ registers explicit nullptrs in
  BOTH die slots (unseeded sound coin + achievements on the post-super
  side). If an orb survives the combat the card is simply lost with the
  combat copies — the master deck never held the instance.

  **Storage spent:** Automaton `numTurns` in pad0 (only TWO of five
  getMove arms increment it — the ++ sits below three returns); Champ
  `numTurns`/`forgeTimes` as pad0 nibbles (++ on EVERY call including
  the init roll — the opposite counter discipline, both pinned) with the
  A19-widened forge bound (30 vs 15) and the roster's first boss
  `lastMoveBefore` read (EXECUTE every third decision, not every other);
  Collector `turnsTaken` in pad0 (increments outside the switch). Torch
  Head is the roster's only CTOR-telegraphing monster: its history reads
  [TACKLE, TACKLE] after spawn (ctor setMove + init's re-push), it spends
  ONE ai_rng draw in its whole life, and its turn re-telegraphs through a
  queued SET_MOVE — no roll fn, the Transient's registration shape.
  All six presets green (`ctest -N` for the current suite size);
  `check_stale_counts` + `check_doc_links` clean.
- **S2.25** `[x]` ∥ Beyond normals I — Darkling (Regrow/revival), Orb
  Walker, Repulsor/Exploder/Spiker (3/4 Shapes, Sphere and 2 Shapes).
  **Deps:** S2.01, S2.2F **Acceptance:** as S2.21.
  **Log:** `MonsterId` **49–53** all spent (Darkling, Orb Walker,
  Repulsor, Exploder, Spiker); **45–48 stay unissued** and were not
  backfilled. `PowerId` **99 REGROW / 100 EXPLOSIVE /
  101 GENERIC_STRENGTH_UP** spent exactly. **Zero new opcodes, zero new
  `MonsterState.flags` bits, zero new `MonsterIntent`s** — the Darkling's
  rolled `nipDmg` and the Spiker's `thornsCount` both reuse `pad0` (the
  Louse / Mugger precedent, the Spiker's saturating because its only
  reader tests `> 5`), the Exploder's `turnCount` needs no storage at all
  (it is `lastMove(BLOCK) || lastTwoMoves(ATTACK)`, exact because move 2
  is absorbing), and the half-death bit is S2.2F's granted global 25
  `kMonsterFlagHalfDead`, whose first producer this is.

  **Registered, not invented: `REGROW`'s `game_id` is `"Life Link"`**
  (`RegrowPower.java:16`) — the class name is the misleading part, and the
  oracle joins on the id. `powers/ResurrectPower.java` declares the SAME
  `POWER_ID` and has zero construction sites anywhere in the tree; it is
  dead content and is recorded in-row so a later batch does not add a
  second row for it. Regrow itself has **no hooks at all** — the whole
  revival machine is in `Darkling.damage` / `getMove` / `takeTurn`, not in
  the power.

  **Two boundaries that read like typos and are not.** The Darkling gates
  HP on `>= 7` and its damage block on `>= 2` in the same constructor,
  even though the HP constants are named `A_2_HP_MIN/MAX`
  (`Darkling.java:50-53,77-88`) — the branch is transcribed, not the
  constant name. The Spiker's A17 Thorns arm is `startingThorns + 3`
  reading the already-tiered A2 field (`Spiker.java:64,76`), so it
  **composes**: seven at A20, not six and not four.

  **The draw-count surface is the bit-exactness surface.** The Orb
  Walker's `super(...)` argument is itself a `monsterHpRng.random(90, 96)`
  evaluated before the constructor body and immediately overwritten by the
  tiered `setHp` — **two draws, the first discarded**, and it is
  tier-INDEPENDENT while the second is not, so `"2 Orb Walkers"` costs
  four. The Exploder's sub-A7 `setHp(30, 30)` is degenerate and **still
  draws** (`Random.random` is `start + nextInt(end - start + 1)` with an
  unconditional `++counter`), which is the exact opposite of the Spheric
  Guardian's zero-draw init (setHp is never *called* there) — both columns
  are authored so the two are never conflated. The Exploder's `getMove`
  reads `num` on no branch and still spends one `ai_rng` draw per decision
  (the Guardian precedent); the Darkling's spends **one, two or more**,
  because `getMove` re-enters on a fresh draw at two sites with
  *different* bounds (`random(40, 99)` at `:166`, `random(0, 99)` at
  `:181`). `burn_unspawned_ctor_rolls` and the live init read the same
  registry roll rows, so range and order cannot drift.

  **First exercise of the S2.2F die() VETO, and it found two live
  defects.** `Darkling.die()` suppresses `super.die()` while the room's
  `cannotLose` latch its own `usePreBattleAction` set is up, and
  `Darkling.damage()` re-fires the power/relic fan-outs by hand — so a
  Darkling at 0 HP is *half dead*, not dying, and `isDying` stops being
  `hp <= 0`. (1) `op_heal`'s early-out was a bare `hp <= 0`, so
  REINCARNATE's `HealAction(this, this, maxHealth / 2)` would have
  silently no-opped and the Darkling would have sat at 0 HP forever; the
  guard is now `hp <= 0 && !halfDead`, which is what `combat_state.hpp`
  already spelled out. (2) Gremlin Horn's "some other monster is still
  alive" test was a bare `hp > 0` where the Java reads
  `areMonstersBasicallyDead` (`isDying || isEscaping`) — wrong in both
  directions, and only *unreachable* until now (no Act-1 group pairs an
  escapee with a sibling whose death matters); a three-Darkling fight is
  three half-deaths and then a three-member `die()` sweep, so it is very
  much reachable. Both fixed in place, both pinned.

  **The revival's fine print, all reproduced rather than corrected:** the
  half-death telegraph pushes move 4 **twice** (the synchronous `setMove`
  *and* the queued `SetMoveAction`), so a revived Darkling's first
  decision reads `[4, 4, <pre-death move>]`; `powers.clear()` is something
  the base `die()` never does, which is why the power walk does not fire
  again in the group sweep while the relic walk **does** (Gremlin Horn and
  friends fire twice per Darkling); `firstMove` is consumed on the init
  roll and is *not* reset by revival; the all-dead test ignores
  non-Darkling members entirely; and `this.halfDead = false` at
  `Darkling.java:227` is deliberately **not** transcribed at that point —
  in this engine's `isDying` model, clearing the bit a line early would
  make the record look already-dying to the sweep and swallow its second
  fan-out, so the sweep clears it per member exactly where the Java sets
  `isDying`. `getMove` also reads the monster's own **slot parity**
  (`monsters.lastIndexOf(this) % 2 == 0`): the middle Darkling of a group
  of three structurally can never CHOMP.

  `dispatch_on_spawn_monster_relics` was **promoted** out of
  `monster_dispatch.cpp`'s anonymous namespace to the header (rule of two,
  conventions §7): REINCARNATE runs the same `onSpawnMonster` loop inline
  in `takeTurn`, synchronously and uncapped, so a revival re-grants
  Philosopher's Stone's +1 Strength every time — a revival is not a spawn,
  but the game fires the spawn hook for it. `ExplosivePower` binds
  S2.2F's `Hook::DURING_TURN` (the second binder will be S2.26's Fading):
  `applyTurnPowers` runs synchronously right after `takeTurn`, so the
  Exploder attacks on the very turn it self-destructs, the `SuicideAction`
  resolves *before* the 30 THORNS-typed unscaled blast, and its 1-arg
  constructor defaults `triggerRelics` to **true** (unlike the large-slime
  split). No new encounter rows: all six beyond groups this un-parks were
  landed by S2.01, and `"Sphere and 2 Shapes"` needed only these three
  shapes to join S2.21's Spheric Guardian. Dead content deliberately
  unregistered: `OrbWalker.DOUBLE_ENCOUNTER`, and the three shapes'
  `ENCOUNTER_NAME` / `ENCOUNTER_NAME_W`. All six presets green.
- **S2.26** `[x]` ∥ Beyond normals II — Spire Growth, Transient, Maw, Jaw
  Worm Horde (variant-ctor deferred row), Writhing Mass (Reactive +
  master-deck Parasite).
  **Deps:** S2.01, S2.2F **Acceptance:** as S2.21, plus the master-deck Parasite
  fold-back test.
  **Log:** `MonsterId` **54–57** all spent (Spire Growth, Transient, Maw,
  Writhing Mass); the batch's FIFTH monster, the Jaw Worm Horde, adds **no
  row** — it is three `JawWorm(x, y, true)` actors reusing id 1. `PowerId`
  **102–105** all spent (CONSTRICTED, FADING, SHIFTING, REACTIVE — the last
  with `game_id "Compulsive"`, because the class is `ReactivePower` and the
  join key is the ID literal, the NoBlock shape of mismatch). **No new
  opcode, no new `Hook`, no new `MonsterIntent`, no new `SCHEMA_VERSION`**:
  the batch is the first CONSUMER of two S2.2F framework grants — opcode
  **68 `OBTAIN_CARD`** (the Writhing Mass's Parasite) and **`Hook::DURING_TURN`
  (15)** (Fading, its first binder anywhere). `MALLEABLE` (95, S2.22) and
  `SHACKLED` (78) are reused byte-for-byte; only Malleable's "applied only
  by" sentence was amended in place.

  **The sharpest fact in the batch is the TWO-TWO fixed-HP split.** All four
  monsters have a flat HP sheet, and exactly two of them cost a
  `monster_hp_rng` draw — because exactly two call `setHp` at all. Spire
  Growth (:53-57) and Writhing Mass (:60-64) use the ONE-ARG `setHp`, which
  is `setHp(hp, hp)` (AbstractMonster.java:777-779) and calls
  `monsterHpRng.random(min, max)` unconditionally, advancing the stream over
  a degenerate range — the Hexaghost precedent. Transient and Maw never call
  it: the ctor hands `maxHealth` to super and `AbstractMonster`'s ctor
  assigns `currentHealth = maxHealth` with no RNG — the Spheric Guardian
  precedent, and the modules SKIP the call rather than rolling a degenerate
  range. The YAML spelling is identical in both cases (`{min: N, max: N}`),
  so the distinction lives in the rows' comments and is pinned by
  `FixedHpMonstersSplitTwoTwoOnTheHpDraw` and `SpawnGroupHpDrawsFollowTheSetHpSplit`.

  **`move_id: 0` is now admissible, argued rather than renumbered.**
  `WrithingMass.BIG_HIT` is `(byte) 0` (:48), which is also
  `move_history`'s empty-slot sentinel, and `emit/monsters.py` rejected
  `< 1` for exactly that reason. `move_id` **is** the game's byte id and the
  file says so, so the rule was re-derived instead of worked around: the
  loader now rejects only what is unconditionally wrong (non-integer, bool,
  negative, `> 255`) and the 0-vs-empty-history question is answered **in
  the row**, where the evidence is — the Writhing Mass's `lastMove(0)` sits
  behind a `firstMove` branch that returns unconditionally, and the class
  never calls `lastTwoMoves` or `lastMoveBefore`, so the collision is
  unreachable for it. A negative id stays a hard error with its own
  generator test.

  **A second onAttacked walk, not a widened one.** `ShiftingPower` is the
  first `ON_ATTACKED` binder whose body declares NO damage-type guard and NO
  `info.owner != null` guard (ShiftingPower.java:33) — the Java's loop is
  unconditional and every previous binder carried its own guards, which is
  what made hoisting them to `dispatch_on_attacked` correct until now. So a
  THORNS reflect or an HP_LOSS onto a Transient really does swing its
  Strength, and the hoisted gate alone would miss it. Widening the shared
  gate would push six landed powers onto call paths they are excluded from
  for free; instead `dispatch_on_attacked_type_tolerant` walks the
  COMPLEMENT (non-NORMAL, and self-sourced damage) over a closed, enumerated
  admitted set — today exactly `SHIFTING`, with the reason each other binder
  is OUT written out line by line. The union of the two walks is the Java's
  single loop, and it is a no-op unless the victim holds an admitted power,
  so every fixture and corpus replay is byte-identical.

  **One divergence found in the inherited tree and FIXED: FadingPower's
  `else` covers TWO cases, not one.** The death arm's condition is a
  CONJUNCTION (`amount == 1 && !isDying`), so an owner at amount 1 that is
  already dying falls to the `else` and queues the reduce — and that is the
  only path on which a Fading slot can reach zero by decrement and touch
  `ReducePowerAction`'s removal arm. The body had returned instead, behind a
  header and a provenance paragraph both asserting the removal path
  unreachable "because the arm is only reached at amount >= 2" — a claim
  that was wrong about the Java in exactly the way that made the missing arm
  look deliberate. Both claims are corrected in place (conventions §8) and
  the behaviour is pinned by
  `FadingDoesNotSuicideAnAlreadyDyingOwnerButStillReduces`. A second, smaller
  one: the Spire Growth's defensive CONSTRICT guard `return`ed past the
  trailing `ROLL_MOVE`, which sits outside the Java's switch — it now skips
  only the apply, because a stuck-move state is strictly worse than the
  missing step it guards.

  **`MonsterState.flags`: ONE bit, and it is a REUSE.** `kMonsterFlagMawRoared`
  takes **0x0004**, the value the large slimes' `splitTriggered` holds — the
  first deliberate reuse under the type-scoped policy and the point of it.
  Chosen over the two lower bits because those are consumed by a *power's*
  native body and are therefore scoped to every type that can own Ritual or
  Curl Up; `splitTriggered` is read only by `monster_slime_large.cpp`, so
  the question is the narrow "can one record be both a large slime and a
  Maw?", and nothing splits into, spawns or transforms a Maw. Everything
  else needs no bit: the Maw's `turnCount`, the Writhing Mass's `firstMove`
  and `usedMegaDebuff`, and the Jaw Worm's hardMode marker all live in
  `pad0`, which each type owns and subdivides.

  **The Jaw Worm Horde's deferred row is DISCHARGED in the same commit** —
  see the Deferred obligations table for the full argument. No new
  `MonsterId`, no schema change: `jaw_worm_init_hard` plus an encounter-key
  branch at the combat-start site, the Lagavulin precedent, with the one
  difference (encounter key vs. event-variant enum) flagged in-header
  against a future rule of two. Checked, and it will not fire from the
  Act-2/3 roster: `Cultist(x, y, boolean talk)` is purely presentational.

  **The master-deck Parasite folds back through the real door.** MEGA_DEBUFF
  accrues into `CombatState.pending_obtain` via OBTAIN_CARD and
  `run_advance` drains it through `add_card_to_master_deck` — the single
  acquisition door, which is what makes Omamori's curse gate and the
  `onObtainCard` / `onMasterDeckChange` relic fan-outs apply without a
  second implementation. Pinned end-to-end through `enter_event_combat` by
  `WrithingMassParasiteReachesTheRunMasterDeck`, with
  `OmamoriBlocksTheParasiteAndSpendsACharge` as the other half. A Reactive
  re-roll can select MEGA_DEBUFF, so a Writhing Mass can be made to grant
  its Parasite during the PLAYER's turn.

  **Citation hygiene:** every Java line reference in this batch's rows,
  headers and modules was re-derived against the decompile file by file
  after the first pass drifted 1–3 lines in ~90 places; the numbers now
  match the files under `SlayTheSpireDecompiled` exactly. Released / not
  spent: **zero** — every granted `MonsterId` and `PowerId` in the block was
  used, so the block leaves no gap. `check_stale_counts` +
  `check_doc_links` clean. All six presets green — debug / asan / release
  via `wsl_run.sh`, win-debug / win-asan / win-release via clang-cl.
- **S2.27** `[x]` ∥ Beyond elites — Giant Head, Nemesis (Intangible +
  Burn), Reptomancer (+ SnakeDagger spawns).
  **Deps:** S2.01, S2.2F, S2.23 (MINION row + spawn pattern) **Acceptance:** as S2.21.
  **Log:** 2026-08-09. `MonsterId` **58–61** all spent (GIANT_HEAD,
  NEMESIS, REPTOMANCER, SNAKE_DAGGER with `game_id "Dagger"` — the class
  is SnakeDagger and the ID literal is not); 45–48 and 66 stay UNISSUED
  and were not backfilled. `PowerId` **106 SLOW** and **107
  INTANGIBLE_MONSTER** spent. `kMonstersCount` 50 → 54, `kPowersCount`
  69 → 71, `kTotalCount` 566 → 572. Every count-guard site answers its
  own question in-comment: the six in `monster_dispatch.cpp`, and on the
  power side ONE of the four took a case (`at_damage_receive` gained
  SLOW) while `at_damage_final_receive` gained INTANGIBLE_MONSTER beside
  the existing INTANGIBLE, leaving `at_damage_give` and
  `interp_block.cpp` caseless.

  **Encounters needed ZERO edits, and so did `run_advance.cpp`.** Ids 55
  "Giant Head", 56 "Nemesis" and 57 "Reptomancer" landed with S2.01 and
  resolve their `EMIT` targets as game-id strings at spawn time, so
  registering the four init fns un-parked all three rooms — the S2.23 /
  B3.16 precedent, and it holds because the implemented-member gate asks
  `monster_init_fn`, never a list. All three compositions are
  fixed (zero `misc_rng` draws) and the Reptomancer's is spawn-order-exact
  at Dagger / Reptomancer / Dagger.

  **TWO FRAMEWORK DEFECTS, both found by reading SpawnMonsterAction
  against the comment that cited it, and both fixed rather than worked
  around.** (1) `smart_position_for`'s header claimed SummonGremlinAction
  and SpawnMonsterAction run the *same* smart-positioning loop. They do
  not: `SummonGremlinAction.java:92-99` **breaks** at the first record the
  newcomer is not strictly right of, while `SpawnMonsterAction.java:50-56`
  **continues** — a COUNT over the whole list. The two disagree on any
  list that is not sorted by `drawX`, which `MonsterHelper` is free to
  build. Split into `smart_position_for` (break, the Gremlin Leader) and
  `smart_position_for_spawn_action` (count, every SpawnMonsterAction
  summoner), with the citation corrected in place and a directed test
  driving an unsorted list where the two answers differ (0 vs 2). The
  Reptomancer's own list happens to be sorted ascending and insertion
  preserves that, so the divergence is unobservable *in this encounter* —
  which is exactly why it would have survived. The two large-slime split
  sites hand-derive their slots and carry `draw_x == 0`, so they are
  untouched and no fixture moves. (2) The Minion application's PLACEMENT
  differs too: `SpawnMonsterAction.java:68` is `addToTop` where
  `SummonGremlinAction.java:114` is `addToBot`. Landed as `flags` bit
  **31 `kSpawnMinionAtTop`**, which required narrowing the `draw_x`
  operand from 14 signed bits to **13** (−4096..4095, still an order of
  magnitude wider than every `offsetX` in Acts 1–3; the extremes are the
  Gremlin Leader's −532 and the Slime Boss layout's +254). Zero stays the
  identity, so no landed caller moves, and `city_elites_test`'s
  round-trip pin now names the new bounds.

  **The Reptomancer is the roster's SECOND double-`monster_hp_rng`
  drawer**, after the Taskmaster and for the identical reason: the
  `super(...)` argument list contains `monsterHpRng.random(180, 190)`
  (Reptomancer.java:64), Java evaluates it before the constructor body,
  and `setHp` draws again. Landed as registry DATA (row `SUPER_ARG_HP`,
  timing S2.2F's `CONSTRUCTOR_BEFORE_HP`, range the FLAT literal at every
  ascension) so `burn_unspawned_ctor_rolls` orders a discarded candidate
  the same way. It sits at group index 1, so the offset moves the SECOND
  dagger's HP — pinned by a test that re-derives all three rolls by hand
  off the seed and carries a negative control proving the difference is
  real. **The SnakeDagger is the mirror image and the grant was wrong
  about it:** the Wave-3 table lists it as a `CONSTRUCTOR_BEFORE_HP`
  consumer, but its super-argument draw `monsterHpRng.random(20, 25)`
  (SnakeDagger.java:46) is its ONLY draw — the class declares no `setHp`
  at all — so the ordinary `hp` column expresses it exactly and the row
  carries no `rolls` entry. Nothing is spent or gapped by that; the timing
  value is S2.2F's and has another consumer.

  **The Giant Head's A18 pre-battle decrement lands AFTER the opening
  rollMove, not before it.** `count` is a field initializer 5 (:53), the
  ctor+`init()` phase runs before `use_pre_battle_actions` (design §5.2),
  and `getMove`'s ordinary arm decrements *before* reading `num` — so at
  A20 the opening decision leaves 4 and the pre-battle takes it to 3. Both
  orders produce a legal-looking telegraph and no test would have caught
  the wrong one, so each step is asserted separately. The IT_IS_TIME ramp
  is arithmetic (`damage.get(1 - count)`, index-clamped at 7, count
  floored at −6) and the module READS the row's single tiered
  `startingDeathDmg` column rather than re-authoring 30/40 in code, so the
  A3 branch has one home; both clamps are driven far enough to reach them.

  **Slow is native for its RESET, and amount ZERO is a real state.**
  `atEndOfRound` is `this.amount = 0` — not a removal and not a
  `ReducePowerAction`, which would delete the slot — and the Giant Head
  applies the power at amount 0 in the first place
  (`new SlowPower(this, 0)`, GiantHead.java:82). The stack-up half IS
  data-expressible (one `APPLY_POWER` of SLOW at 1 onto SELF) and is
  written natively anyway so the pair reads as the single class it is.
  The hook is **ON_AFTER_USE_CARD (16)**, not ON_USE_CARD: only the
  `update()` fan-out reaches monster powers at the right moment
  (UseCardAction.java:79-88). `atDamageReceive` multiplies by
  `1 + 0.1*amount` through the float pipeline before the single
  `mathutils_floor`, so 13 at three stacks is 16, not 13 + 3.

  **The Nemesis's Intangible is TWO ROWS' worth of difference and TWO cap
  sites.** `IntangiblePower` ("Intangible") is not
  `IntangiblePlayerPower` ("IntangiblePlayer", id 29): different POWER_ID
  literal, hence a different oracle join key; it decays at
  **atEndOfTURN** (the monster pass of `dispatch_at_end_of_round`) where
  id 29 decays at atEndOfROUND; it carries a `justApplied` latch (in
  `PowerSlot.counter`, the Draw Reduction shape, written on
  op_apply_power's new-slot path); and it spells a REMOVE arm for
  amount 0 that id 29's row collapses. The cap likewise lives twice:
  `atDamageFinalReceive` caps NORMAL damage, and `Nemesis.damage`
  (:120-131) caps `info.output` before `super.damage()` with NO
  DamageType test, so THORNS and HP_LOSS are capped too. That second site
  is keyed on `MonsterId::NEMESIS` rather than on "any monster with
  Intangible", because it is a class method override and not a property of
  the power — invisible today (nothing else grants a monster Intangible)
  and the statement that survives Act 4. All three damage types are
  pinned. Burn generation is **DISCARD on both ascension arms** (5 at
  A18, else 3): `MakeTempCardInDiscardAction` is the only card-making
  action in the class, so the "discard vs draw per move" the brief flagged
  is not a real split.

  **`getMove` draw counts are the Nemesis's other native reason.** Three
  arms spend an extra `aiRng.randomBoolean()` on top of the rollMove draw,
  and the `>= 65` arm's is `randomBoolean() && scytheCooldown <= 0` —
  Java evaluates the LEFT operand first, so THE DRAW IS SPENT EVEN WHEN
  THE COOLDOWN THEN REFUSES THE SCYTHE. Reordering that conjunction to
  test the cheap integer first is the natural "optimisation" and it
  silently desynchronises the shared stream; the body forces the
  evaluation order through a local and a named test counts the draws on
  both sides. `scytheCooldown` lives in `pad0` FLOORED AT ZERO, which is
  exact because every reader is `<= 0` or `> 0`.

  **`daggers[4]` needed NO storage**, the Gremlin Leader's `gremlins[3]`
  derivation verbatim: "slot i is occupied" is exactly "some record with
  `draw_x == POSX[i]` is not dead-or-escaped", and `draw_x` (S2.2F)
  already stores that key. It stays exact under RECYCLING — the dead
  record keeps its `draw_x` and the new live one joins it, and the live
  one is what both the Java pointer and the predicate see — which is a
  named test. The two ENCOUNTER daggers' positions are written by the
  Reptomancer's `usePreBattleAction`, the one place that can know them
  (they are an encounter property, not a type property), riding along with
  the Minion walk exactly as the leader's does.

  **The spawn turn's stream order is the batch's most fragile fact and is
  test-pinned end to end:** both `SnakeDagger` constructors — and
  therefore both `monster_hp_rng` draws — run at QUEUE time, both
  children's `init()` rolls at RESOLVE, and the Reptomancer's own roll
  lands THIRD on `ai_rng` behind them, with its trailing ROLL_MOVE naming
  the POST-insertion index (pending queue items are not remapped across a
  spawn). Two independent caps are reproduced and separately tested: the
  POSITION cap (four POSX slots; with all four held the turn spawns
  nothing, draws nothing and still rolls) and `canSpawn`'s GROUP cap
  (`aliveCount <= 3` over every record that is neither `this` nor dying),
  which gates the DECISION only — a telegraphed SPAWN_DAGGER still fires
  if the group filled up in between. `getMove` recurses on two arms with
  fresh seeded draws, the second (`aiRng.random(65)`) widening the range
  back down, so it is a re-draw loop with a depth assert rather than an
  unroll.

  **`MonsterDieAfterFn` gained the entry its own header comment names.**
  The Reptomancer's suicide sweep runs after `super.die()`, has no
  `m == this` term, and skips the Reptomancer ONLY because `super.die()`
  already set `isDying` — run it pre-super and it suicides itself in an
  infinite regress. Both pushes are `addToTop`, so the suicides resolve in
  REVERSE list order, and the 1-arg `SuicideAction` sets triggerRelics
  TRUE (unlike the slime splits), so every surviving dagger pays a full
  death edge. `HideHealthBarAction` is presentation and dropped, which
  does not move that order. The other three declare presentation-only
  die()s and take explicit `nullptr` cases with the reading recorded —
  including the Nemesis's, whose `playDeathSfx` IS on the pre-super side
  and still does not qualify, because its `MathUtils.random(1)` is
  UNSEEDED (the Looter half of the Mugger/Looter split).

  Released / not spent: **ZERO new opcodes** (the summon pattern is bits
  on the existing `SPAWN_MONSTER`; the dagger's self-kill is the existing
  `LOSE_HP`), **ZERO new hooks** (S2.2F's `ON_AFTER_USE_CARD` gains a
  second binder), **ZERO new `MonsterIntent`s**, **ONE type-scoped
  `MonsterState.flags` bit** — 0x0800, spent THREE times over for the
  Nemesis / Reptomancer / SnakeDagger `firstMove` latches, deliberately
  REUSING the Hexaghost bits exactly as S2.28 did and for the same
  reason (six concurrent batches cannot share an append cursor); bits
  17–23 stay free. The Reptomancer and the dagger DO co-occur, which is
  fine and is the Donu/Deca adjudication: no single RECORD is ever two
  types at once. `pad0` carries the Giant Head's `count` (biased by 6) and
  the Nemesis's `scytheCooldown`. Dead content recorded rather than
  registered: `SpawnDaggerAction` has NO caller anywhere in the tree
  (grepped, not assumed) and `SnakeDagger.damage.get(2)` — a 25 of type
  HP_LOSS — is never read, so no move row carries it. `check_stale_counts`
  + `check_doc_links` clean. All six presets green at 2408 tests —
  debug / asan / release via `wsl_run.sh`, win-debug / win-asan /
  win-release via clang-cl.
- **S2.28** `[x]` ∥ Beyond bosses — Awakened One (two phases, Curiosity/
  Unawakened, Void insertion, Cultist adds), Time Eater (TimeWarp/
  DrawReduction/Slimed), Donu and Deca.
  **Deps:** S2.01, S2.2F **Acceptance:** as S2.21, plus phase-transition and
  TimeWarp turn-economy tests.
  **Log:** 2026-08-09. `MonsterId` **62–65** and `PowerId` **108 CURIOSITY /
  109 UNAWAKENED / 110 TIME_WARP / 111 DRAW_REDUCTION** all spent — both
  blocks exact, no gaps. `MonsterIntent` **15 DEFEND_DEBUFF** spent on the
  **Time Eater's Ripple** (TimeEater.java:194,203) — NOT on Donu, whom the
  dispatching brief assigned it to: Circle of Protection telegraphs plain
  `Intent.BUFF` (Donu.java:129) and Deca's Square telegraphs DEFEND /
  DEFEND_BUFF on an A19 switch (Deca.java:139-141, the roster's first
  per-tier INTENT — the module owns both arms, the row carries the live A20
  one). S2.2F's opcodes **69 CLEAR_CARD_QUEUE / 70 END_PLAYER_TURN** consumed
  as allocated; no new opcode (steps.py only admits the existing
  REMOVE_DEBUFFS / CAN_LOSE into `MONSTER_MOVE_OPS`). Fuzz **`MoveCat` 32
  RELEASED unspent**: the double-boss crossing offers NO player decision — no
  reward screen exists at either Act-3 boss (AbstractRoom.java:327), so there
  is nothing to claim and no map to pick, and the transition fires straight
  out of `finish_combat_after_action`. **Zero new `MonsterState.flags` bits**:
  the four type-scoped bits (form1 / firstTurn / usedHaste / isAttacking —
  ONE shared bit for the Donu+Deca pair, per-record) deliberately REUSE the
  Hexaghost's 0x0800–0x4000, reversing S2.21's append-not-reuse call because
  six concurrent batches sharing "next 17" is a collision engine while an
  Act-1 boss and an Act-3 boss can never share a combat.

  **`PUBLIC_VIEW_VERSION` 4 → 5** — additive, case 1: `second_boss_reserved`
  populated with the revealed A20 second boss (`boss_list[1]`), carried from
  the double-boss transition onward and surviving into RUN_OVER, where it is
  the only record of which second boss decided the run. No offset moved;
  `twins_v1.bin` regenerated for the stamp alone. No `SCHEMA_VERSION` bump.
  The run layer grew the pieces the route needed: `boss_cursor` now
  increments on boss-room EXIT (meaning "boss rooms completed"; the Java pops
  `bossList` on ENTRY, so the gate reads
  `boss_list_count - (boss_cursor + 1) == 2` — the entry-pop offset spelled
  out rather than reduced away), `condition_boss_list` gained the observed
  prefix length `keep` (cursor plus one while standing in a Boss room, the
  same rule as the monster/elite prefixes) and `resample_hidden` passes it,
  so the hidden twin agrees with the truth the moment the second boss is on
  screen. Kill #2 is the run's only terminal and pays the +1 exactly once;
  the Act-2 A20 negative and the below-A20 single-boss terminal are named
  tests (`run_advance_test.cpp` BossVictory). a20.yaml row **20** flipped to
  IMPLEMENTED (ownership history S2.12 → S2.28 recorded in-row) and row 13's
  S2.28 share (both Act-3 boss rooms draw-and-discard their own boss gold) is
  recorded landed; the four a20_modifiers_test pins moved with their rows.

  **Framework edges the bosses forced, all engine-wide rather than
  special-cased:** `op_apply_power` gained ApplyPowerAction's resolve-time
  `isDeadOrEscaped` early-out (the safety net under the game's three
  UNGUARDED queue-time walks — Donu's Circle, Deca's Square, Time Warp's
  Strength fan-out); `op_block` gained GainBlockAction's `isDying/isDead`
  recipient read, which is NOT the same predicate — a half-dead monster gains
  block but takes no power, and a named test tells the two guards apart;
  `op_heal`'s guard is now `hp <= 0 && !halfDead` (isDying, not "at zero"),
  which is what lets Rebirth's full heal land on a 0-HP boss, and the heal
  clears the half-dead bit as the invariant maintainer — the ONE recorded
  deviation: the Java clears it one action earlier in `changeState`, and
  nothing can observe the difference (argued at both sites).
  `game_hand_size()` derives Draw Reduction by PRESENCE (the balanced
  onInitialApplication/onRemove pair), so a second Head Slam stacks to 2
  while the hand shrinks by one card, exactly the Java's arithmetic. The
  registry loader's `move_id >= 1` bound relaxed to `0..255`: Donu and Deca's
  BEAM is a REAL move id 0 colliding with the move-history empty-slot
  sentinel — harmless because neither getMove reads history, and the loader
  note says what a future move-0 monster with a history read would need.

  **Verification of the inherited tree (a prior agent stopped mid-task with
  the implementation uncommitted and untested):** all four Java classes plus
  the four power classes re-read in full against every line of the dirty
  tree. Defects found and fixed: a task id in a `run_advance.cpp` comment
  (NoTaskIds violation), fabricated getMove line citations in
  `emit/monsters.py` (Donu.java:337-343 / Deca.java:505-512 — the files are
  146/158 lines long; corrected to :125-131 / :135-143), the missing audit
  version-log v5 entry and the training-contract still quoting version 4,
  and four test files still pinning the pre-S2.28 world (public-view version
  stamp, twin fixture, the Act-3 terminal at A20, the a20 row statuses).
  Time Warp binds **ON_AFTER_USE_CARD (16), not ON_USE_CARD (1)** — the two
  fan-outs differ in moment and participant list — and its 12th-play turn
  end executes the END_PLAYER_TURN opcode body SYNCHRONOUSLY so queued plays
  die before the Strength fan-out queues, matching
  `callEndTurnEarlySequence`. The Awakened One's death fan-outs pay ONCE at
  the half-death (MonsterDieFn veto) and TWICE at the real death (super.die
  plus the damage() override's re-fire) — Gremlin Horn is the named witness
  for both counts. Every ctor draws exactly one DEGENERATE `monster_hp_rng`
  roll (single-arg `setHp` is `setHp(hp, hp)`; the scout dossier's "no draw"
  claim is corrected in the yaml block header). `kPowersCount` 56 → 60 and
  `kMonstersCount` 34 → 38, every count-guard site moved together. All six
  presets green (`ctest -N` for the current suite size);
  `check_stale_counts` + `check_doc_links` clean.

## Phase S2.3 — Events closure (B4.11–B4.13 pattern; ∥ across disjoint batches once S2.02 + S2.13 land)

- **S2.31** `[x]` ∥ City events I (non-combat): Addict, Back to Basics,
  Beggar, Cursed Tome, Drug Dealer, Forgotten Altar, Ghosts, Nest.
  **Deps:** S2.02, S2.13, S2.03 (payout rows) **Acceptance:** per-event
  option/gate/A15 audit against the source read in full; payout rows
  (relics/cards/curses) acquisition-tested; six presets green.
  **Log:** 2026-08-09 — landed. Eight `implemented: true` rows (events.yaml
  ids 32, 33, 34, 36, 37, 38, 39, 41) with audited `options`/`a15` blocks
  and a per-row body citation, one new engine TU
  (`src/engine/events/city_events_i.cpp`) and 29 tier-2 tests
  (`tests/city_events_i_test.cpp`). Exactly as S2.13 promised, no
  `build_event_pool` / `build_shrine_pool` / membership-mask line was
  touched: an Act-2 `?` already selected the right id, and the bodies only
  filled `event_dialog_impl`. **Ids spent: none new** — the eight EventId
  rows and every payout row (relics BLOODY_IDOL / ENCHIRIDION /
  NILRYS_CODEX / NECRONOMICON / MUTAGENIC_STRENGTH / CIRCLET, cards
  APPARITION / JAX / RITUAL_DAGGER, curses SHAME / DECAY) were already
  issued by S2.02 and S2.03. One enum value: `EventGridKind` gains
  `TRANSFORMABLE_ANY` (4).

  **THE A15 AUDIT IS THE HEADLINE, and it is four-of-eight, not
  eight-of-eight.** Addict (the 85-gold price is a compile-time constant,
  Addict.java:24), Back to Basics, Beggar (GOLD_COST 75, Beggar.java:27 —
  what ascension moves for this event is its *draw* gate, which is
  `getEvent`'s and not the body's) and Drug Dealer contain **no
  `ascensionLevel` read at all**, and each is pinned with an explicit
  same-behaviour-at-A0-and-A15 test rather than left unasserted, so a
  future ascension branch appearing in any of them is a red test.
  The four that do branch: Cursed Tome's finish cost 10 → 15
  (CursedTome.java:58 — the three page damages 1/2/3 and the stop-reading 3
  do **not** move), Forgotten Altar's HP-loss percent 0.25 → 0.35 (:50, the
  +5 max HP does not move), Nest's gold 99 → 50 (:35, the dagger's 6 damage
  does not move), and **Ghosts, which is the trap**: its ctor's ascension
  read only picks which OPTION STRING is drawn (Ghosts.java:37-41), while
  the actual branch is in `becomeGhost` (:86-89, `amount = 5; if
  (ascensionLevel >= 15) amount -= 2`) — so A15 moves the APPARITION COUNT
  5 → 3 and the max-HP price stays `ceil(maxHealth * 0.5f)` at every
  ascension. S2.03 had flagged exactly this for S2.31 and it is confirmed
  from source, not inherited.

  **Option-tree findings, each of which would have been wrong if guessed
  from the payout manifest:**
  * **Beggar has THREE screens, not two.** Paying 75 does not open the
    purge grid; it opens a page whose single button opens it (:63-70,
    :79-85), and `update()` then removes the card and calls `openMap()`
    ITSELF (:46-57) — there is no post-purge page. That last half exposed a
    framework fact worth recording: **`run_advance` discards
    `choose`'s return value on a GRID pick** (it only honours `FINISHED`
    on a MENU pick), so a body that ends the event from inside a grid must
    install the transition itself and report `TRANSITIONED`. Beggar is the
    batch's one such body; the shared `finish_to_map` helper names it.
  * **Nest's entry screen has exactly ONE button** (:34) and the offer pair
    is BUILT by pressing it: `setDialogOption` appends the Ritual Dagger at
    index 1 and `updateDialogOption(0, ...)` then rewrites index 0 into the
    gold offer (:43-45). Index 0 is gold, index 1 is the dagger — the
    reverse of the order the two payouts are usually listed in.
  * **Forgotten Altar's Bloody-Idol arm is the opposite of the obvious
    guess.** `gainChalice` (:99-115) swaps the Golden Idol for a Bloody
    Idol *in its own relic slot* — but only when a Bloody Idol is NOT
    already held; when one is, the payout is a plain Circlet **and the
    Golden Idol stays** (:106-109).
  * **Addict/Beggar/Forgotten Altar's "two options" are one greyed-out
    option.** Each ctor has two `setDialogOption` arms that differ only in
    TEXT and pass the identical `isDisabled` expression
    (`gold < 85` / `gold < 75` / `!hasRelic("Golden Idol")`), so the menu
    width never changes with the gate. Addict's `if (gold < 85) break`
    (:45) is the dead defensive twin of its grey-out.
  * **Cursed Tome does not instant-obtain its book.** `randomBook` clears
    the room rewards, adds ONE relic row and opens the combat reward screen
    (:158-161) — the screen IS the exit, the Lab / Woman in Blue shape. Its
    page damages are taken on LEAVING each page and are all
    `DamageInfo(null, n, HP_LOSS)`: a NULL owner, so Torii's `onAttacked`
    never runs (AbstractPlayer.java:1427-1434 gates the whole `onAttacked`
    block on `info.owner != null`) while Tungsten Rod's owner-independent
    `onLoseHpLast` still does. Nest's dagger cost is `DamageInfo(null, 6)`
    — NORMAL, not HP_LOSS — and takes the same two relics the same way;
    both are pinned with a Torii and a Tungsten Rod witness.

  **Two engine surfaces the batch had to add, both narrow and both named at
  the door rather than special-cased in the body:**
  1. `EventGridKind::TRANSFORMABLE_ANY` — **Drug Dealer is the ONE event
     grid in the game that does not exclude bottled cards**
     (`gridSelectScreen.open(player.masterDeck.getPurgeableCards(), 2, ...)`,
     DrugDealer.java:128, with no `getGroupWithoutBottledCards` wrapper,
     unlike Living Wall's Change and Transmogrifier). The
     `event_grid_card_legal` comment that predicted this exact need is now
     discharged. Its TWO picks stay the body's business: the single-pick
     `event_grid_transform_card` door is deliberately NOT extended to the
     new kind, because `DrugDealer.update` (:104-122) fires only at
     `selectedCards.size() == 2` and then does both removals and both
     `transformCard` draws before both appends — a door that transforms and
     closes in one call would silently halve it.
  2. `swap_relic_in_place` (relic_pools) — `AbstractRelic.instantObtain(p,
     slot, callOnEquip=false)` applied to an OCCUPIED slot, the
     `p.relics.set(slot, this)` arm (AbstractRelic.java:230-249). It is not
     `lose_relic` + `acquire_relic`: the incoming relic's `onEquip` is
     skipped and the outgoing relic's index is preserved, so trap 8's
     trigger order survives the swap (pinned with a Tiny Chest sitting
     after the idol and keeping both its index and its counter). Written
     narrowly on purpose — no Circlet-stacking arm (Forgotten Altar's
     Circlet takes the ordinary obtain door) and an empty `onUnequip`
     fan-out, which is a fact about Golden Idol (GoldenIdol.java:12-30 has
     no override) rather than a shortcut; the note `lose_relic` already
     carries about the Bottled trio is repeated at the new door.

  **One documented defensive deviation, new:** Beggar's GAVE_MONEY screen
  opens its grid unconditionally (:80, `canCancel` false) with no emptiness
  test, unlike Back to Basics (:70). A deck with nothing removable would
  strand the Java on a grid it cannot satisfy; this port falls through to
  the leave page instead of emitting a phase with no legal action. Reaching
  it needs 75 spent gold and a deck of nothing but Ascender's Bane / bottled
  cards — barely reachable, not impossible, and recorded at the code.

  **Test-ownership move:** the `ActEventLists` City-list test asserted "no
  Act-2 row is implemented", which this task necessarily falsifies.
  Rather than editing a snapshot into it, that loop
  now asserts the INVARIANT (`native` always; `implemented ⇒ screen_count >
  0`; `!implemented ⇒ both counts zero`) and the by-name roster moved to
  `CityEventsI.RegistryMarksExactlyTheEightNonCombatRowsImplemented`, which
  lists all thirteen City rows with their expected flags — so S2.32
  flipping one of its five without updating the roster is a failing test.
  *(Exactly that happened at integration, 2026-08-09: the S2.32 union merge
  tripped the roster as designed, and the integrator flipped the five rows
  and renamed the test to
  `CityEventsI.RegistryMarksAllThirteenCityRowsImplemented`.)*
  **S2.33 will hit the identical assertion in the Beyond loop; it is left
  untouched here so the edit lands with the batch that needs it.**

  **One deferral opened rather than inherited.** S2.03 had pencilled the
  non-acquisition BODIES of six relics and three cards onto S2.31 (Bloody
  Idol's `onGainGold`, Enchiridion / Nilry's Codex / Necronomicon /
  Necronomicurse, Mutagenic Strength, Ritual Dagger's `misc`-growth
  program). This task's acceptance is acquisition-only and it declines
  them — but the events make them REACHABLE, so leaving the pencil mark on
  a `[x]` row would have been drift. Every one of those registry rows had
  its `owner S2.31` string amended in place, and a Deferred-obligations
  row now carries the set with the granting event named per entry. The one
  that looks like a one-liner and is not: Bloody Idol's heal sits on
  `AbstractPlayer.gainGold`'s fan-out, which also fires IN combat, where
  the heal takes the phase-gated Magic Flower path `gain_gold` does not
  model.

  Six presets green (`ctest -N | tail -1` for the current suite size);
  `check_stale_counts` + `check_doc_links` clean.
- **S2.32** `[x]` ∥ City events II: The Library, The Mausoleum, Vampires,
  Colosseum + Masked Bandits (combat embeds), Knowing Skull, The Joust,
  N'loth, Designer, Duplicator (act-gated one-timer bodies).
  **Deps:** S2.02, S2.13, S2.01 (event encounter groups) **Acceptance:**
  as S2.31, plus combat-embed flow tests (two-fight Colosseum sequence).
  **Log:** 2026-08-09 — landed. Ten bodies (five TheCity eventList rows +
  five act-gated one-timers), each Java file read in full; all ten
  `implemented: true` with options/a15 metadata in events.yaml, dispatch via
  the generated macro (`events/city_events_ii.cpp`, `events/city_one_timers.cpp`).
  **Ids spent:** `MonsterId` **45–47** (BanditPointy game_id "BanditChild",
  BanditLeader, BanditBear — claimed in stage-b "S2 Wave-3 allocations" out of
  the unissued 45–48; **48 stays unissued**). No new PowerId/opcode/intent/
  RunPhase/MoveCat: the trio is DAMAGE/BLOCK/APPLY_POWER + SET_MOVE chains
  (zero lifetime ai_rng draws past init — the Torch Head shape; the Leader's
  A17 `lastTwoMoves(1)` re-slash and the Bear's 2→3→1→3 alternation are
  queue-time native), and the Bear's negative-Dexterity BEAR_HUG rides the
  existing `negative_stat_flip` Artifact predicate. `kMonstersCount` 59→62.
  **The Colosseum is the reopen seam, and it is ONE event by proof:**
  `grep -rn "public void reopen" com/megacrit/cardcrawl/events/` has exactly
  two hits (the empty base + Colosseum.java:100-110), and Colosseum.java:55 is
  the game's only `rewardAllowed` writer. finish_combat_after_action's
  survivor path (victory, Smoke Bomb, mug alike) asks `event_combat_reopens`
  — event kept alive across the Slavers fight, screen != LEAVE — and
  `event_combat_reopen` replicates the battle-over tail the game runs with NO
  screen: dropReward (EMPTY for an EventRoom, AbstractRoom.java:454-455),
  addPotionToRewards' chance roll + blizzard ratchet with the rolled item
  discarded (`roll_event_potion_drop_unopened`, factored out of
  assemble_combat_rewards' block (3) so the two cannot drift), then reopen's
  preBattlePrep: ONE unconditional shuffleRng.randomLong
  (drawPile.initializeDeck) that the Nobs fight inherits through
  preserve_floor_streams, the atPreBattle relic pass against the folded-back
  mirror, and rewards.clear(). Fight 2 pre-stocks RARE + UNCOMMON relic pops
  + 100 gold, sets eliteTrigger (the Dead Adventurer precedent), clears
  rc.event (reopen is a no-op at LEAVE) and takes the ordinary event-combat
  reward flow. Pinned by the Colosseum suite incl. the Smoke Bomb reopen.
  **Masked Bandits' one recorded deviation is COUNTER-ONLY:** the event ctor
  installs the encounter at ENTRY (MaskedBandits.java:43) so the game draws
  the trio's three monster_hp_rng ctor rolls at the dialog, the sim at the
  FIGHT button — same stream, same counter start, identical values, and
  nothing else draws monster_hp_rng on an event floor, so only the PAY path's
  end-of-floor counter differs (floor reseed erases it). Recorded at the body
  head for S2.43's differ. stealGold is unseeded-MathUtils VFX + loseGold(all).
  **deathReact — the re-pointed obligation — discharged as a verified
  negative** (see the deferred-obligations row above; pinned by
  `CityEventsII.BearDeathReactIsPresentationOnly`).
  **PUBLIC_VIEW_VERSION 5→6 (breaking, the v4 shape):** The Library's read is
  a twenty-card one-pick board (TheLibrary.java:66-91), so `kEventOptionCap`
  and `kEventBoardCap` went 12→20 — PvEvent.board sits mid-record and
  `can_choose_event_option` is embedded in the mask channel, 8932→8988 bytes;
  audit version log + training-contract §1 updated, `twins_v1.bin`
  regenerated by its generator. **Match and Keep got its own
  `kMatchBoardSize` (12)** so the cap's spare slots can never enter its deal,
  menu, pick bound, or the resampler's hidden-slot permutation — and the
  committed 50-seed oracle corpus is what CAUGHT the one build where M&K
  still shuffled over the widened cap (STS71011 diverged at its floor-2
  board; `OracleCorpusReplay.FiftySeedCorpusReplaysZeroDiff` is the control
  working as designed, and it is green again — verified against a
  base-commit build of the replay tool to prove the divergence was
  branch-introduced, then fixed at the source).
  **The Library rolls are getCard(rollRarity()), not getRewardCards:** per
  attempt one `cardRng.random(99) + cardBlizzRandomizer` against the
  EventRoom 3/37 thresholds WITH the alternation pass and NO blizzard
  mutation, then one pool index; duplicates re-roll both. The alternation
  pass is **N'loth's Gift's body, landed at the reward-rarity seam**:
  `reward_card_rarity_with_relics` (rare ×3 per held gift, elite 10→30, boss
  rooms exempt — MonsterRoomBoss.getCardRarity returns RARE without
  thresholds), consumed by roll_card_reward_item AND the Library loop; the
  relic row stays hook-free by design. **Red Mask's body landed native**
  (`relic_native_red_mask`): per-slot player-sourced Weak 1 with
  isSourceMonster=false — the Gremlin Mask boolean fanned over the group;
  ApplyPowerAction's own dead-target drop is op_apply_power's liveness
  refusal, so no filter was added.
  **Knowing Skull:** damage FIRST (HP_LOSS, null owner — no Torii, Tungsten
  Rod applies, Fairy/Lizard revives inside apply_event_damage), then the
  bought cost ++es; Sozu short-circuits BEFORE getRandomPotion (no draw); a
  full belt loses the potion but spends the draw; a lethal buy still grants
  (the game keeps executing). Its CARD buy forced the colorless question:
  **the live colorlessCardPool order is now real state**
  (`RunController.colorless_order`, HIDDEN in byte_class; init at run_begin +
  act crossing = the game's own initializeCardPools reset; shared
  `event_draw_colorless_uncommon` replaces shrines.cpp's local-copy shuffle,
  whose recorded deviation row in stage-b is DISCHARGED — first-draw bytes
  unchanged, so no golden moved).
  **Vampires:** ceil(maxHP*0.3f) clamped to maxHP-1, STARTER_STRIKE = exactly
  Strike_R in the Ironclad scope removed BACK TO FRONT, five Bites through
  the obtain door; the Blood Vial option exists only while held and costs
  the vial instead; NO ascension branch in the file (the S2.03 flag
  confirmed). **Mausoleum:** the miscRng boolean is drawn even at A15 (then
  overwritten true); Writhe goes through the Big Fish Omamori order —
  ShowCardAndObtainEffect's ctor spends the charge before the relic obtain.
  **Joust:** both bets pay 50 at EXPLANATION; ownerWins =
  miscRng.randomBoolean(0.3f) rolled ONE SCREEN LATER (the PRE_JOUST
  continue); 250/100/0 payout matrix. scratch1 is masked in PublicView (the
  second Dead-Adventurer-shaped parked realization; the Joust moved out of
  the 0x0F group to 0x01). **Designer:** two ctor randomBoolean draws pick
  wording AND mechanic; costs 40/60/90/3 → 50/75/110/5 at A15; the option
  gates read the UNBOTTLED WHOLE-DECK count (curses included) while the
  grids filter to purgeable-unbottled — reproduced, with a DEFENSIVE
  DEVIATION where the game would open a mandatory empty grid and soft-lock
  (gold spent, dialog completes; noted at each arm). The two-pick transform
  is the new `EventGridKind::TRANSFORM_PAIR_SECOND` (legality =
  TRANSFORMABLE minus scratch3's first pick); both randomLong shuffles are
  drawn even over an empty upgradable list, exactly as Collections.shuffle
  is. **Duplicator:** the new `EventGridKind::DUPLICATE` — the WHOLE master
  deck, no purge filter, no bottle exclusion; the copy is
  makeStatEquivalentCopy (upgrade count kept, bottle flags cleared) through
  the ordinary door, so a copied curse meets Omamori.
  **Tests retired/reshaped as the S2.13 Log ordered:**
  `ActOneUnreachableSpecialsAreExactlyTheUnimplementedOnes` →
  `EverySpecialBodyIsLandedExceptTheSecretPortalPin`;
  `AnActTwoQuestionMarkRoomSelectsACityRowAndParks` now checks dispatch
  against the registry's implemented column; the FuzzGuard seed-116 pin
  widened to "clean end at a named reason" (the ALWAYS_EVENT trajectory now
  dies fighting in Act 2 instead of parking); the S2 relic inertness test
  names Red Mask's binding. 44 new tests across
  `city_events_ii_test.cpp` / `city_one_timers_test.cpp` (option/gate/A15
  audits, payout acquisitions, the two-fight Colosseum flow, the bandit
  move graphs, the deathReact pin, the colorless persistence pin). All six
  presets green; `check_stale_counts` + `check_doc_links` clean.
- **S2.33** `[x]` ∥ Beyond events: Falling, Mind Bloom (boss re-fight +
  miscRng shuffle — trap 6), The Moai Head, Mysterious Sphere, Sensory
  Stone, Tomb of Lord Red Mask, Winding Halls; SecretPortal pinned per
  trap 5; the `Lab` listing resolved (deferred row).
  **Deps:** S2.02, S2.13, S2.01 **Acceptance:** as S2.31, plus Mind
  Bloom's Act-1-boss re-fight replays zero-diff in a directed capture.
  **Log:** 2026-08-09. All seven TheBeyond bodies landed in ONE new TU
  (`src/engine/events/beyond_events.cpp`), each Java class re-read in full;
  events.yaml ids **45–51** flipped `implemented: true` with options/a15
  metadata (screen counts 3/2/2/3/2/2/3; A15 rows exactly Moai Head ×1 and
  Winding Halls ×2 — Winding Halls' A15 heal goes DOWN, 0.25→0.2, while its
  damage goes up; Mind Bloom's 25/50 split is **A13 boss-gold economy**, so
  it lives in a20.yaml row 13 — that row's Mind Bloom share is now LANDED —
  not in the a15 column). No new ids of any kind spent; no schema or
  PublicView change.
  **Mind Bloom (trap 6), the batch's hard part:** ONE
  `miscRng.randomLong()` seeds a JDK `Collections.shuffle` over the three
  Act-1 boss KEYS in add order and the fight is `list.get(0)`
  (MindBloom.java:66-71) — twin-pinned through the same
  `JdkRandom`/`jdk_shuffle` pair at three floors. The re-fight is an
  **EventRoom combat**: `enter_event_combat` keeps RoomType::Event (no
  monsterList pop on exit), eliteTrigger stays false (unlike Dead
  Adventurer), so the win pays base card odds — not MonsterRoomBoss's
  all-RARE row — and Proceed goes to the MAP, never a boss chest. Rewards
  in Java order: fixed 25/50 gold (NO misc draw — pinned by counting the
  shuffle's one draw), then an immediate `returnRandomRelic(RARE)` pop
  (AbstractRoom.java:541-543 — the NON-screenless variant, vs Mysterious
  Sphere's screenless one; both twin-pinned against the pool). The
  floor-keyed third option (`floorNum % 50 <= 40`, gold 999 + 2×Normality
  vs full heal + Doubt) is pinned at the 40/41 boundary; "I am Awake"
  upgrades every `canUpgrade()` card (Searing Blow keeps stacking) then
  grants Mark of the Bloom. **The capture half of the acceptance is
  DEFERRED to S2.43 with its own deferred-obligations row** — the bridge
  cannot run from this seat (and two sibling batches held the install);
  the sim half is fully pinned, including a forced-victory walk through
  the preserved reward rows to the map.
  **Engine seams this batch had to open, none body-local:** (1)
  `events::heal` now routes `apply_on_player_heal_out_of_combat` — the
  Mark-of-the-Bloom suppressor S2.13 wrote for the rest heal applied to NO
  event heal, which was invisible only while the granting event had no
  body; with it landed, Bonfire/Moai/Winding/Mind-Bloom heals after the
  grant must all heal 0 (pinned per body here; Act-1 behaviour
  byte-unchanged — the fan-out is identity without the relic). (2)
  `roll_colorless_card_reward_item` (combat_rewards.cpp) —
  `getColorlessRewardCards` (AbstractDungeon.java:1381-1421) gained its
  first live consumer via Sensory Stone: per card ONE
  `cardRng.randomBoolean(0.3f)` (NOT rollRarity — no pity READ), but a
  RARE result still RESETS `cardBlizzRandomizer` to +5, shifting later RED
  reward rarities; same-rarity no-dupe redraws; **no upgrade pass**;
  Question Card/Busted Crown apply. `kColorlessRareChance` moved
  shop.hpp→combat_rewards.hpp (two consumers, one definition). Sensory
  Stone's memory draw consumes ONE `miscRng.randomLong` whose OUTPUT is
  cosmetic text — the draw is the state — and the 5/10 HP_LOSS lands
  AFTER the card rolls, so a lethal memory still moved cardRng (pinned).
  (3) **Mysterious Sphere constructs its encounter in the event ctor**
  (MysteriousSphere.java:39): four monsterHpRng draws (2× super-arg + 2×
  setHp, monster_orb_walker.hpp) are spent whichever way the dialog ends.
  The fight path defers construction to `enter_event_combat`
  (stream-exact — nothing between reads that stream); the leave path pays
  via `burn_unspawned_ctor_rolls`, and the two exits are pinned to equal
  counters. The one residue is transient mid-dialog counter lag,
  unobservable at every dump point. **Falling** draws through
  `getGroupWithoutBottledCards` on BOTH the presence test and the pick
  (CardHelper.java:88-103): a deck whose only attack is bottled counts as
  attack-LESS — no draw, disabled option (pinned); draw order
  attack/skill/power differs from display order skill/power/attack.
  **Tomb of Lord Red Mask** buys at 0 gold (no gate; `loseGold(0)` no-op).
  **Both deferred rows resolved in place:** `Lab` — DISCHARGED
  (ProceedButton.java:110-121 is reward-screen plumbing: the list is
  "events whose COMBAT_REWARD Proceed opens the map"; Lab qualifies via
  `GenericEventDialog.hide()` + room COMPLETE + 3-potion reward screen,
  Lab.java:49-58, exactly what `open_potion_reward_screen` has modelled
  since B4.13); SecretPortal — pin re-affirmed and the row DISCHARGED as
  an obligation (SecretPortal.java re-read in full: nothing but the
  playtime half of the gate separates the sim from the event; no body, no
  row flip; trap 5 + `SecretPortalIsPinnedFalseInEveryActIncludingTheBeyond`
  carry it permanently). 26 tier-2 tests in `beyond_events_test.cpp` +
  the Beyond block of `act_event_lists_test.cpp` rewritten for the flip
  (a15 counts per row) + the a20 row-13 pin updated to the LANDED wording.
  Six presets green; `check_stale_counts` + `check_doc_links` clean.

- **S2.34** `[x]` **Payout relic/card bodies (the S2.31 deferral, discharged
  before the gate).** The seven combat/run bodies the deferred-obligations
  row "S2.31 payout relics/cards are now REACHABLE with their bodies still
  deferred" enumerates, each currently pinned INERT by tier-2 (this task
  retires those pins by making them assert the body instead): **Bloody
  Idol** `onGainGold` heal 5 — run-layer `gain_gold`
  (relics/relic_pickup.hpp) is the reader; the non-trivial half is that
  `AbstractPlayer.gainGold`'s fan-out fires in combat too, where `heal`
  takes the Magic Flower / phase-gated path `gain_gold` does not model;
  Ectoplasm composes by returning BEFORE the fan-out
  (AbstractPlayer.java:719-724). **Enchiridion** `atPreBattle`
  (Enchiridion.java:30-39): needs a POWER-typed pool sibling of
  `kIroncladAttackPool`/`kIroncladSkillPool`; ONE cardRandomRng draw,
  this-turn-only cost zeroing (X-cost stays X), MakeTempCardInHand;
  AT_PRE_BATTLE ordering (before the opening draw — the Snecko Eye
  distinction). **Nilry's Codex** `onPlayerEndTurn` → `CodexAction` —
  read CodexAction.java IN FULL before authoring; its choice screen is
  rng-visible. **Necronomicon** `onUseCard` once-per-turn replay of the
  first costForTurn ≥ 2 attack (or X-cost with energyOnUse ≥ 2, not
  freeToPlayOnce) + `atTurnStart` re-arm (Necronomicon.java:60-85); the
  `activated` latch is private state, NOT counter. **Necronomicurse**
  `triggerOnExhaust` → fresh copy to hand (Necronomicurse.java:43-49);
  there is no ON_EXHAUST `CardTrigger` today — a new member is a shared-
  namespace claim (stage-b-tasks.md table first). **Mutagenic Strength**
  `atBattleStart` — three addToTop calls, so RESOLUTION order is cosmetic,
  LoseStrength 3, Strength 3; native body, not a `hooks:` list (the row
  says why). **Ritual Dagger** — the bespoke kill-conditional
  `misc`-growth opcode (RitualDaggerAction.java:34-58: on-kill, not
  halfDead, not Minion-holder → misc += magicNumber on the master-deck
  card by uuid AND every in-battle instance; `CardInstance.misc` gains its
  first writer; damage re-seeds from misc). Opcode + trigger values are
  shared-namespace claims. Registry rows flip `hooks:`/`native:` with
  bodies in the SAME commit (link-error discipline).
  **Deps:** S2.31 (landed) **Acceptance:** every body pinned tier-2
  against its cited Java re-read in full; the eight-row inertness sweep in
  relic_boss_special_test updated to assert exactly the still-inert rows;
  ritual-dagger and necronomicurse inertness pins retired; no
  SCHEMA_VERSION / PUBLIC_VIEW_VERSION movement (none is granted); Stage-A
  fixtures byte-identical; six presets green; the deferred-obligations row
  marked DISCHARGED pointing here.
  **Log:** 2026-08-09 — landed; the deferred-obligations row above is
  DISCHARGED in place with the per-body summary. Namespace values spent
  (claimed in stage-b-tasks.md "S2.34 allocations" FIRST): opcode **73**
  `RITUAL_DAGGER` (CARD_CONTEXT, the DAMAGE_RAMPAGE source-index-stamp
  shape), opcode **74** `CODEX` (ENGINE_EMITTED), `CombatState.flags` bit
  **6** `kCombatFlagNecronomiconUsed` (INVERTED so zero-init == armed; the
  observable counter stays −1), and RANDOM_ATTACK_TO_HAND's previously-zero
  `flags` as a pool selector (0 = ATTACK byte-identical, 1 = POWER for
  Enchiridion). NO new RelicHook, NO new ChoiceKind (Codex rides the
  DISCOVERY choice surface + item packing with its own opcode: always
  skippable, ZERO wasted regens — CodexAction.java:33-36 generates INSIDE
  the open-tick branch, unlike DiscoveryAction.java:47 — and the pick goes to
  the draw pile at a random spot via op_make_card's DRAW_RANDOM arm at
  registry cost), and NO new CardTrigger: the brief's premise that
  Necronomicurse needed an ON_EXHAUST trigger was STALE — the `on_exhaust:`
  program column (Sentinel, B3.6) IS the triggerOnExhaust seam, dispatched on
  every exhaust path. Registry-schema additions (recorded with the claims):
  cards.yaml `initial_misc:` (CardDef.initial_misc — ctor misc seed AND the
  run-persistent fold-back marker) and `on_exhaust_bottom: true`
  (CardDef.on_exhaust_add_to_bottom — Necronomicurse addToBot's where
  Sentinel addToTop's; the exhaust dispatch now honours direction and
  performs the MAKE_CARD pile split). Findings/corrections beyond the brief:
  (1) **Bloody Idol's in-combat path IS reachable** — Hand of Greed's
  GreedAction calls player.gainGold at the kill (GreedAction.java:38; the
  whole-tree grep found no other in-combat gainGold in Acts 1–3 scope,
  FameAndFortune is Watcher) — so op_damage_greed now runs the gainGold seam
  at the kill: Ectoplasm's early return (nothing accrues) and the onGainGold
  fan-out (heal 5 through heal_player_with_relics, at combat time, where it
  is lethality-relevant); the fold-back settle became a RAW += so the
  fan-out cannot double-fire (run_advance_test's Ectoplasm settle test
  rewritten to the new contract). (2) **Mark of the Bloom's in-combat
  onPlayerHeal half was a GAP** — S2.33 landed the acquisition while
  heal_player_with_relics knew only Magic Flower; closed here (row 150
  provenance corrected: run half S2.12, combat half S2.34). (3) The brief's
  "3-of-colourless" description of CodexAction was wrong — CodexAction.java:54
  calls the NO-ARG returnTrulyRandomCardInCombat(), the RED combat pool
  (kIroncladCombatPool); the Java won. (4) Two stale "deferred" comments
  corrected in place (relics_special.hpp/.cpp said Warped Tongs was this
  tier's deferred empty body — opcode 64 landed it long ago; conventions §8
  "comment asserting X"). (5) enter_combat now seeds pool misc FROM the
  master row (makeSameInstanceOf copies misc — byte-identical for every deck
  without a persistent-misc card) and fold_back_combat writes it back for
  initial_misc-marked rows only, so Rampage's combat-scratch misc never
  folds. Documented deviation (registry row 131 + op_ritual_dagger): a kill
  scored by a same-uuid REPLAY COPY (Double Tap / Necronomicon on a
  cost-raised dagger) grows only the transient copy — the engine's replay
  copies carry no uuid link and adding one is CombatState storage no schema
  grant covers; the copy's DAMAGE is right (misc copied at replay time),
  only the growth's persistence is short. Necronomicon threading:
  resolve_card_play now derives X-cost energyOnUse ONCE (hoisted, without
  Chemical X's repetition boost — the relic boosts reps, not the field) and
  passes it with action.target through dispatch_on_use_card into
  RelicHookContext; the relic replay fires AFTER Double Tap's, per
  UseCardAction.java:41-64. Tests: relic_boss_special_test +9
  (MutagenicStrengthResolvesLoseStrengthThenStrength,
  EnchiridionPreBattleDrawsOneFreePowerIntoHand,
  RandomToHandFlagsZeroStillMeansTheAttackPool,
  NilrysCodexEndTurnQueuesTheCodexScreen,
  CodexOfferIsTheRejectionSamplerOverTheRedCombatPool,
  CodexPickInsertsAtARandomDrawPileSpotAtRegistryCost,
  CodexIsAZeroDrawNoOpWhenMonstersAreBasicallyDead,
  NecronomiconReplaysTheFirstTwoCostAttackOncePerTurn /
  ...IgnoresCheapFreeAndNonAttackPlays / ...XCostArmReadsEnergyOnUse,
  BloodyIdolHealsFiveOnAHandOfGreedKill /
  ...HealRoutesThroughTheCombatHealSeam, MarkOfTheBloomZeroesInCombatHeals);
  s2_event_content_test rewritten pins (S2RelicHookBindingsMatchTheLanded-
  Bodies replaces EveryS2RelicIsInertUntilItsBodyTaskLands;
  RitualDaggerProgramIsItsOpcodeAndItsMiscSeed /
  ...AcquisitionSeedsMiscFifteen / ...DealsMiscDamageAndGrowsOnlyOnARealKill /
  ...PaysNothingForHalfDeadOrMinionKills replace the empty-program pin;
  NecronomicurseIsUnplayableAndRespawnsOnExhaust /
  ExhaustingNecronomicurseReturnsACopyToHandAddToBot replace the inert-curse
  pin); run_advance_test EctoplasmSuppressesTheAccrualAtTheKill (rewritten)
  + RitualDaggerKillFoldsItsGrownMiscToTheMasterDeck. Six presets green;
  Stage-A fixtures byte-identical; check_stale_counts + check_doc_links
  clean.

### S2-G1 `[x]` **Gate: S2 rules complete** — tag `s2-g1-content`
**Deps:** all S2.0x, S2.1x, S2.2x, S2.3x
Checked literally per design §6 S2-G1: registry closure vs the §2
inventory; 100 % tier-2 per the manifest; every §5 trap named-tested;
a20 rows IMPLEMENTED; ≥ 10M-action three-act fuzz soak clean; six presets
green; Stage-A fixtures byte-identical. Then: update CLAUDE.md "Current
state".
**Log:** 2026-08-09 — TAGGED, all five items checked literally on the
final master (S2.34 + S2.41 + the A5/A12 status flips landed). (1)–(2)
Registry closure and 100 % tier-2 are enforced inside the green suite
(manifest + codegen-determinism tests; 2569 tests). (3) a20.yaml: 19 rows
IMPLEMENTED, one N/A-FOR-S1 (A16 shop prices — shops outside S1 *and* S2
scope, reason in-row; not in the gate's named set). The gate sweep itself
flipped A5 and A12, whose S2.12-landed shares had deferred the one-row
edit to successors that never carried it. (4) Gate soak
(`s2g1-gate_20260809_144041`, release + 1 % asan sample, S2.41 tooling):
600,000 cases (24,000 seeds × 5 policies × 5 reps, A20),
**55,144,852 counted actions** (110.5M stepped incl. replay-twice),
**failures 0** — zero nondeterminism, zero asserts, zero
`room_unimplemented`, zero `no_legal_moves`/`livelock`/`no_progress`;
per-act witness: Act 1 600,000 cases / 18,718 boss fights / 666 kills,
Act 2 666 cases / 4 boss fights, Act 3 unentered. Read per S2.41's
measured finding: the soak bar is BREADTH + DETERMINISM with per-act
witnesses — `victories = 0` is a *policy* result (E0 heuristics lose
~×30 per act; no practical soak volume witnesses a three-act win), and
depth is the oracle driver's job (S2.42 §8, S2.43). The registry-row
census reads 130/132 cards, 42/62 monsters, 146/150 relics, 33/33
potions, 59/72 powers — the never-seen rows are exactly the deep-act and
policy-shadowed ones, stated not inferred. (5) All six presets green at
2569 on the final master; Stage-A 20 fixtures byte-identical (fixture
hash suites green in every preset). Also in the gate commit: the stale
`run_advance.hpp` "parks at ROOM_UNIMPLEMENTED" scope note rewritten to
the landed truth (deferred from S2.41, which could not touch the engine
while S2.34 held it). CLAUDE.md "Current state" updated. Soak artifacts
uncommitted under `SpeedTheSpire-campaigns/fuzz/` per convention.

## Phase S2.4 — Verification campaigns + S2 exit

- **S2.41** `[x]` ∥ **Three-act fuzz soak extension.** B5.1 machinery over
  Acts 1–3: new MoveCats claimed for the boss-relic phase, coverage
  report extended per act; the S2-G1 soak is this task's tooling run at
  gate time.
  **Deps:** S2.11, S2.12 (runs incrementally as content lands)
  **Acceptance:** soak sweep with zero nondeterminism/asserts at
  S2-G1-scale volume; shard/resume paths proven.
  **Log:** 2026-08-09 — landed. **No namespace claimed:** S2.11's MoveCat
  28–31 grant already covers the boss-relic phase end to end, and the audit
  that checked it found the real gap — S2.11 spent the four values and
  enumerated the moves, but `move_score` never grew an arm for them, so all
  four fell through its final `return 0` and the whole room was one uniform
  tie-break for every heuristic. A 300-seed probe read `boss_chest_open`
  legal twice / **taken zero**, with `pick` and `skip` never legal at all —
  content that looks unreachable when what is missing is a preference. (GCC
  said so as `-Wswitch`; the project promotes only the conversion pair to
  errors.) Fixed with per-policy weights: the depth policies open then pick,
  `hoard_gold` walks past *without* opening (trap 3's live case — the three
  relics burn at room entry either way), and `greedy_block` scores SKIP
  *equal* to PICK, never above, because skip is a reversible screen close
  that re-advertises `open` and a stateless policy scoring it higher parks in
  that 2-cycle forever. Equal is bounded, not hopeful: 3 picks against 1
  skip, so 4/3 opens per chest in expectation. Post-fix soak: open 111 /
  pick 199 / skip 3 / proceed 132 — all four categories taken.

  **Per-act coverage** (`act_cases` / `act_rooms` / `act_boss_fights` /
  `act_boss_kills` / `max_act`, merged, kv-round-tripped, printed as its own
  report block ahead of the depth histograms). The room split is a
  *partition* of the act-blind table, asserted to sum; `act_boss_kills[3]`
  and `victories` are two independent probes of one event (combat outcome vs
  `run_is_victory`) and the report shouts if they disagree. The kill probe is
  the combat OUTCOME, not `BOSS_TREASURE`, because the act-3 boss opens no
  chest at all. NEVER REACHED now names unentered acts and unfought/unkilled
  act bosses first, so an absence is a printed line rather than an inference.
  Also fixed while in there: the run-layer events block overran its 256-byte
  buffer at soak-scale counter widths and silently lost its last field —
  the probe printed `relics  reward claims by kind:` with the relic total and
  the newline gone.

  **Soak (release preset, 4 shards + merge): 100,000 cases / 200,391 engine
  runs / 9,246,766 counted actions (18,527,368 stepped), failures 0** —
  0 `no_legal_moves`, 0 `no_progress`, 0 `livelock`, 0 `room_unimplemented`,
  0 reproducers, 0 surviving in-flight journals. Replay-twice hashing on
  every case, pass C sampled. Per act: act 1 100,000 cases / 3,155 boss
  fights / 108 kills; **act 2 108 cases (0.11 %) / 0 boss fights**; act 3
  **0 cases**; `victories` **0**. Shard/resume proven as file comparisons,
  not spot checks: a 3-way split's merged report is byte-identical to the
  same sweep in one process, and a re-run shard reproduces its summary byte
  for byte across different `--threads` (a shard is the restartable unit;
  there is no checkpoint file and there should not be one).

  **The `victories`-reads-0 residue is DISCHARGED as a content question and
  reopened as a policy one, with the number that decides it.** Nothing parks
  any more — the wall S2.42 recorded is gone — but the E0 heuristics lose
  roughly ×30 per act (3.2 % reach the act-1 boss, 0.11 % kill it, 0 of those
  108 crossings reach the act-2 boss), so an act-2 kill needs order 10^7
  cases and a victory order 10^9, against a gate soak sized at 10^7 *actions*.
  **No practical soak volume witnesses a three-act run with these policies**,
  and S2-G1's soak bar should be read as breadth + determinism accordingly —
  the per-act block is what makes that statement checkable instead of
  assumed. Depth stays the driver's job (S2.42 §8's verdict, now measured on
  both sides of the act boundary). Escalating to a deeper sim policy was
  deliberately NOT done: the ledger routes that through a measurement, and
  this is the measurement, not the decision.

  **Deferred obligation "Act-2 / Act-3 measured sim-side reach numbers":
  re-run half discharged.** `seed_scan`'s §1 command re-run verbatim, release
  preset, 50,000 rows, `determinism_mismatches=0`, `failures=0`: Act-1
  reproduced to the row (1,406 fights / 59 kills / 59 cohort triples over 55
  seeds — the control proving it is the same experiment), Act-2/3 cells now
  **measured 0** rather than *pending content*, `room_unimplemented` 8 → 0,
  and four City events appear in the census. Recorded in
  [verification/s242-deep-reach.md](verification/s242-deep-reach.md) §11 with
  the fuzz soak's per-act numbers as an independent cross-check (different
  tool, different seed range, different probe; they agree). Double-boss
  detection remains unbuilt, as §10.2 says.

  7 tests added (per-act partition/invariants over a 200-case sweep, the
  seed-116 crossing read through the per-act tables, the report's act
  witness + NEVER REACHED lines, per-act kv round-trip and `max_act`
  strictness, the boss-chest preference, shard equivalence, shard resume).
  `fuzz_soak` build id gains `-s241act1` so pre-S2.41 summaries cannot merge
  with post-S2.41 ones. Six presets green;
  `check_stale_counts.sh` / `check_doc_links.sh` clean.
- **S2.42** `[x]` ∥ **Deep-reach scripted drivers + sim pre-scan.** The
  design §6 driver-risk mitigation: extend the TE.1 external-policy
  family for three-act survival (act-aware heuristics; boss-relic pick
  rule; potion discipline), plus the sim pre-scan tooling that selects
  (seed, policy, policy-seed) triples reaching Act-2/Act-3 bosses; if
  measured reach is insufficient for the S2-G2 depth bars, escalate to
  the sim-consulting scripted driver (shallow rollout behind the same
  STS-POLICY-IO seam) — still deterministic and weight-free.
  **Deps:** S2.12 (three-act sim runs) **Acceptance:** measured reach
  report (per-act boss-fight and boss-kill rates per policy at scanned
  scale) committed; drivers replay deterministically; the S2-G2 depth
  cohorts are demonstrably schedulable from the scan output.
  **Log:** 2026-08-07. Report:
  [verification/s242-deep-reach.md](verification/s242-deep-reach.md).
  Two instruments plus one report, in very different states of readiness —
  and the report says so rather than averaging over the difference.

  **Driver `b1.6.0` → `b1.7.0` (buildable AND measurable now — the driver
  runs against the real game, which has all three acts).** The thing that
  blocked it was the driver's own Act-1 terminal, not content:
  `is_boss_combat_reward` gated on `act == 1` and `_claim_boss_reward`
  deliberately refused the `proceed` that opens the boss chest (design §1.1
  "Out", S1 scope). Both gates removed; a run's terminal is now the game's
  own GAME_OVER, death or **victory**. Consequence worth knowing: a
  GAME_OVER screen *can* walk back to the menu, so deep runs no longer force
  an orchestrator relaunch — and `orchestrator.py` needed no change, because
  it has always acted on the driver's durable request rather than on its own
  model of where a run ends.

  New rules, each with its evidence line in the R1/R2/R3 tradition.
  **R4, the boss-relic pick** (`greedy_policy._score_boss_reward`): the
  screen offers three BOSS relics plus `skip`, and G2-2 needs BOTH a take and
  a skip witnessed — so it is a **cohort selection, not a coin flip**.
  `BOSS_RELIC_SKIP_MODE` is a plain numeric constant, so
  `policy_bossrelic_take.json` / `policy_bossrelic_skip.json` /
  `policy_survival_act.json` are three SHA-pinned campaign identities over
  one binary. Five BOSS relics are never taken on a **checkable** criterion —
  each invalidates a rule this module owns (Sozu→R2, Runic Dome→R3 via
  `attacker_count`, Snecko Eye→the cheap-utility term and the side table's
  cost column, Pandora's Box→R1's deck-attack gate, Calling Bell→the b1.5.3
  modal-screen path) — rather than "is a bad relic", which is not.
  **`ACT_PROFILES`**: per-act overlays over the same ALL-CAPS constants
  (`MAP_ELITE` raised in Acts 2/3 but gated on `ELITE_APPETITE_HP_FRACTION`
  so it degrades to Act-1 avoidance when hurt; `DECK_ATTACK_TARGET`/
  `DECK_SIZE_CAP` widened together so R1's gate keeps its shape;
  `POTION_LOW_HP_FRACTION`; `POTION_HIGH_STAKES_FROM_ACT`). **Act 1 is
  byte-identical to b1.6.0 by construction** — no key `1`, and an act-less
  dump resolves to 1 — so TE.1's measured 31.0 % stays reproducible in
  behaviour. Two traps closed on the way: (a) `apply_constants` now records
  configured names in `greedy_policy.CONFIG_PINNED` and `_const` yields to
  them, because an overlay consulted after the setattr would make every
  cohort config a silent no-op in Acts 2/3 — a cohort labelled with a policy
  it did not run, the exact failure the strict config validation exists to
  prevent; (b) a skipped boss-relic pick is a REVERSIBLE screen close
  (`relicSkipLogic` → `chest.close()`, which does not clear the offers) and
  `getChestRoomChoices` re-advertises `open` the instant `isOpen` goes false,
  so open/skip is a legal 2-cycle alternating between two screens — invisible
  to the stuck detector, the same shape as the b5.2 GRID-cancel trap.
  `_boss_chest_reopen_filter` drops the second and later open of one chest;
  it costs nothing (a reopen offers the same three) and cannot empty the
  candidate set (`proceed` is always advertised there). `seeds_done` gained
  `boss_fight_acts` / `boss_kill_acts` / `boss_relic_acts` / `max_act` /
  `victory`, all additive and all OUTSIDE
  `validate_artifacts.STRICT_DONE_KEYS` on b1.6.0's terms; pipeline
  `b5.3.0` → `b5.4.0` aggregates them. README's stale "Current capture
  driver: `b1.5.3`" (against a `b1.6.0` tree) fixed.

  **Planner (`seed_scan`).** The brief's premise that S2.11 had added a
  `BOSS_TREASURE` arm to the planner was **false** — S2.11 added it to
  `tools/fuzz/src/policy.cpp` and `replay/command_map.hpp`, never here; the
  scan's whole vocabulary was Act-1-only (`boss_reached` one act-agnostic
  bool, no kill observation, no act column, seed-list not triples). Added
  `max_act`, `boss_reached_acts` / `boss_killed_acts` bitmasks, `victory`,
  per-act `boss_ids[]`, the matching filters (`--min-act`,
  `--need-boss-act`, `--need-boss-kill-act`, `--need-victory`,
  `--need-boss-id`), per-act × per-policy `ScanSummary` tables, and
  `--cohort-list`. **The kill probe is exact, not inferred**: the boss chest
  is entered only through the boss reward's `proceed`, and Acts 1–2 both end
  in one while Act 3 opens no chest — so `BOSS_TREASURE` at act N IS the
  act-N kill for N ∈ {1,2}, and act 3's is `run_is_victory()`. The live
  driver runs the same pair against the protocol dump, deliberately, so the
  two instruments cannot disagree unattributably. Columns are appended AFTER
  `fail_kind` and `boss` keeps its old meaning, so a pre-S2.42 `cut -f10`
  still selects the boss column. `ScanLimits::max_actions` 4000 → **12000**:
  4000 was an Act-1 budget, a truncated deep run ends as `ACTION_CAP`, and
  that reads as a policy failure while being the tool's own truncation — so
  the cap count is now printed next to the reach numbers.

  **Measured (release preset, 5,000 seeds × 5 policies × 2 policy seeds =
  50,000 rows, `--verify-determinism`, `determinism_mismatches=0`,
  `failures=0`):** Act-1 boss FIGHT 2.81 % overall (`always_event` 12.03 %,
  `greedy_damage` 1.01 %, `greedy_block` 0.61 %, `hoard_gold` 0.40 %,
  `random` 0.01 %); Act-1 boss KILL **0.12 %** overall — a genuinely new
  number, since TE.1 measured reach and nobody had measured sim-side kill.
  The 23× gap between reach and kill is what the S2-G2 depth bars live on.
  The three comparable E0 heuristics bracket TE.1's 0.80 % cross-check, no
  drift finding. Cohort demo: `--need-boss-kill-act 1 --cohort-list` gave
  **59 triples over 55 distinct seeds**, covering all three Act-1 registry
  bosses (Slime Boss 29 / Hexaghost 25 / The Guardian 5); all three Act-2
  boss IDENTITIES are already observable from the 59 act-2 crossings, so the
  identity dimension of a G2-2 cohort is schedulable now even though the
  reach dimension is not.

  **Act-2/3 reach is 0 by construction and is recorded as pending content,
  not estimated.** An Act-2/3 combat room parks at
  `RunPhase::ROOM_UNIMPLEMENTED` and the first row of every act is a forced
  Monster row, so no sim run can take one step into Act 2 until S2.23/S2.24
  (and Act 3 until S2.27/S2.28); the 8 `room_unimplemented` rows in the
  census are that wall, directly witnessed. Deferred-obligations row added.
  Double-boss detection was deliberately **left unbuilt** rather than shipped
  as an always-false column — a field hard-wired false under a comment naming
  a future task is the shape conventions §8 calls a bug signal — and belongs
  with whichever run-layer flag S2.28 lands.

  **No new sim-side `PolicyKind`.** That would have edited
  `tools/fuzz/include/sts/fuzz/policy.hpp`, the one file S2.41 is
  concurrently editing. The cohort triple therefore names a SIM policy that
  the oracle campaign cannot execute; S2.42 adopts the honest reading — the
  triple is **provenance for a reachability claim**, not an instruction — and
  says so in the cohort file's own `#` header, the planner README and the
  report, because the gate bar's credibility rests on it. Relatedly,
  `--min-hit-count` INVERTS for depth cohorts (a deep line is fragile, and
  the capture replays the exact triple), which the README now states where a
  reader will meet it, so nobody "fixes" a depth scan up to 2 and throws the
  Act-3 cohort away.

  **PROTOCOL.md §3.8 `BOSS_REWARD.screen_state.relics`, ownerless until now.**
  It was dispositioned `I (S2 scope)` on a reason that b1.7.0 falsified, and
  an `I` field is never diffed — so S2-G2 item 2's *zero-diff* boss-relic
  pick was unachievable and no S2 row owned fixing it. S2.42 took the
  **contained half**: promoted to `S`, `fr.ignore` → `fr.defer`, ids still
  registry-joined so an unknown boss relic fails translation loudly, pinned
  by two new translator tests. The **storage** half is not contained — the
  offers live in transient `RunController.boss_chest` while the translator
  emits `RunState`/`CombatState`, so it needs a `SCHEMA_VERSION` bump (which
  is stop-the-line outside planned sites, conventions §5) plus trace-container
  and oracle-adapter changes. Deferred-obligations row names **S2.43** with
  that evidence.

  **Escalation verdict: not yet decidable, and deliberately not pre-empted.**
  The ledger row makes the sim-consulting driver conditional on a
  measurement, and the number that decides it is the DRIVER-side Act-2/3
  reach under b1.7.0 — S2.43's live capture. Sim reach alone demonstrably
  cannot carry a depth cohort (0.12 % Act-1 kill; Acts 2–3 structurally 0),
  so the depth cohorts will be scheduled from driver reach plus the scan's
  identity/seed dimensions.

  **Green:** all six presets (`debug`/`asan`/`release` under WSL-GCC,
  `win-debug`/`win-asan`/`win-release` under clang-cl), zero failures.
  Python driver + pipeline + report suites green (`oracle_campaign_python_test`,
  `oracle_campaign_pipeline_python_test`, `verify_report_python_test`).
  `check_stale_counts.sh` and `check_doc_links.sh` clean. New C++ tests:
  `SeedScanActMask.*`, `SeedScanActDepth.*`, `SeedScanCohort.*`,
  `SeedScanOutput.DepthColumnsAreAppendedAfterFailKind`,
  `SeedScanOutput.SummaryCarriesPerActAndPerPolicyDepth`,
  `SeedScanLimits.TheActionCapIsAThreeActBudget`,
  `Translator.BossRewardRelics*`. New Python test classes:
  `ActProfileTest`, `BossRelicPickTest`, `BossChestSequencingTest`,
  `PerActReachFieldsTest`. Re-derive counts with `ctest -N | tail -1`.
- **S2.43** `[x]` **Oracle campaigns, breadth + depth.** The §6 S2-G2
  evidence: ≥ 2,000 distinct mixed-policy A20 attempts; Act-2 boss-reward
  + boss-relic-pick cohort; Act-3 kill + double-boss cohort
  (simulator-selected seeds sanctioned); event-depth coverage join; all
  triage per the Stage B process, zero untriaged/open.
  **Inherited:** the stage-b "fork redeploy + bottle-taking capture" row
  (see Deferred obligations).
  **Deps:** S2-G1, S2.42, S2.47, S2.48, S2.49 **Acceptance:** deterministic dashboard
  reopening every artifact; per-bar numbers meeting design §6 S2-G2
  items 1–4; dispositions exact, no wildcards.
  **Pre-work landed 2026-08-26 (sim-side only; no game launched — the
  owner directed "sim-side prep only" for the session):** the two scoring
  blockers this task carried are discharged in place — the translator's
  act-local FIRED derivation (cross-record accumulation in `--replay`,
  `fired_accum.hpp`; the masking alternative rejected on the shrine-bit
  aliasing argument) and `seed_scan`'s one-word planner decode (uint64
  through the four signatures; both engine words observed), each row's
  full disposition in the Deferred-obligations table above. Rode with a
  same-day engine repair the first Act-2 scored capture would otherwise
  have tripped: the FIRED hi word's `(id-33)` shift UNDERFLOWED at id 32
  (formally UB; x86 masking parked it on bit 31 and every pin mirrored
  the accident), fixed to the intended `(id-32)` with two more latent UB
  finds beside it (`floor_stream`/`map_stream` signed overflow at the
  golden set-5 Long.MAX_VALUE seed — now defined-wrap, bit-identical to
  Java; a MAP_CHOICE mask OOB `map[]` read on off-nominal (act, floor)
  pairs — now guarded like `map_edge_connects`), all three surfaced by
  making UBSan NON-RECOVERABLE in the asan presets
  (`-fno-sanitize-recover=undefined`, Sanitizers.cmake) — recoverable
  diagnostics had been scrolling past ctest unread, so "asan green" now
  actually asserts UB-freedom. Still owed by the campaign itself:
  everything in the Acceptance line, the Mind Bloom directed capture, the
  fork-redeploy/bottle row, and the trap-5 playtime recording duty.
  **Campaign launched 2026-08-26 (owner go-ahead; breadth wave complete,
  triage in progress).** Pre-flight: trap-5 playtime anchor landed in the
  fork + translator (`oracle.playtime`, disposition `oracle`; pinned) and
  the bottle row verified already-satisfied (Track C, see the corrected
  row). BREADTH (S2-G2 item 1 evidence): 2,000 distinct sequential A20
  seeds — `s243_breadth_rand` 500 @ random-legal, `_take` 750 + `_skip`
  750 @ survival external with the S2.42 boss-relic TAKE/SKIP cohort
  configs, policy-seed 1234 — 2,000/2,000 seeds completed, 0 failed,
  ~236k captured actions at ~38 actions/s aggregate. Reach, measured:
  Act-1 boss fights 221/750 take + 212/750 skip (29.5 %/28.3 % — TE.1's
  31 % holds at scale), 19 Act-1 boss KILLS (8 take + 11 skip), 19 Act-2
  entries, deepest floor 27, **0 Act-2 boss fights** — the §8 escalation
  number: breadth-policy depth alone cannot fill G2-2/G2-3, so the depth
  cohorts need either the sanctioned sim-consulting driver (S2.V2, named
  by design §6's driver-risk paragraph — the ledger task does not exist
  yet and would need opening) or seed-targeted directed captures once
  `seed_scan --need-boss-kill-act 2` cohorts exist; decide at the next
  session. TRIAGE (Stage B discipline, zero-wildcard): 94/2,000
  state_divergence as captured; worked to SIX root causes, all landed
  same-day — the replay command map's boss-chest seam (every one of the
  19 Act-2 crossers; all 19 replay clean through boss kill → chest →
  both relic branches → act transition), the FiredAccum boss_ids fold,
  the stale live-gold projection, the fairy live-belt burn, the Addict
  one-click leave, and Unceasing Top's deferred onRefreshHand (first
  live witness) — leaving 68/94 replay-clean at their run terminals plus
  18 Smoke-Bomb escape-window capture artifacts (the fork's readiness
  held ready through the 2.5 s escape animation; closed at the source in
  two rounds — the isEscaping hold, then the endBattle settle-lag hold,
  pin `370CBFA8…` — all 18 recaptured CLEAN, zero race records, the
  class fully discharged) and **7 open residuals**, each with a per-run lead
  (Gremlin Leader minion slots, Runic Pyramid, Ancient Tea Set, a
  floor-3 room disagreement, Ritual-Dagger-adjacent misc, a potion-slot
  case, a combat-over case) held with evidence in the data root's
  `s243_triage_notes.md`. The re-run of `--replay` over the full
  divergent set is the retest instrument; classifications regenerate
  with the next `postprocess` pass.
  **Residual closure (2026-08-26, second wave): the open queue is ZERO.**
  All seven dug to root cause with the `--combat` instrument; six were
  engine defects, fixed same-day with named regression tests, and every
  fixed capture replays CLEAN to its run terminal: the Ritual Dagger
  master-misc mid-action write (`sync_run_persistent_misc`, the live-purse
  discipline extended — the game writes masterDeck INSIDE
  RitualDaggerAction, so a run that dies pre-fold carries the grown row;
  STS432354 345/345), the Entropic Brew in-combat Fairy obtain leaving the
  armed-fairy mirror stale (the live-belt fix's one regression — the burn
  ate the fresh potion at its own step boundary; arming per PLACED Fairy
  also closed the downstream We-Meet-Again belt-conditional miscRng gold
  tail; STS432580 236/236), Blood Potion's inline heal (now a queued HEAL
  item per BloodPotion.java:44-45 — observable behind ColorlessPotion's
  open DISCOVERY screen; STS432663 138/138), Dropkick's follow-ups queued
  add_to_bottom where DropkickAction addToTop's all three (the played card
  must stay in LIMBO across its own empty-deck reshuffle — 18 cards, not
  19; STS432630 220/220; the same-class Second Wind ordering fixed as a
  rider, observable through Juggernaut's per-gain random-target draws),
  the victory-terminal survivor set widened to the game's FOUR-arm
  clearPostCombatActions allowlist (`survives_clear_post_combat`:
  + GainBlockAction + actionType==DAMAGE, per-opcode Java-class audited —
  DropkickAction is ActionType.BLOCK and FiendFireAction WAIT, both
  correctly cleared; victory-scoped, S2.49's dying-owner cancel intact —
  the Guardian's Sharp Hide THORNS retaliation behind the lethal blow now
  lands; STS431342 284/284, the Runic Pyramid lead falsified), and the
  Gremlin Wizard ctor x-offset (`x - 35.0f`, GremlinWizard.java:48 — the
  smart-position key whose absence rotated the monster array after a dead
  Wizard and sent every positional replay target to the wrong minion;
  STS431071 270/270, all four capture rallies' insertion indices
  reproduce). The seventh, STS430130, is NOT a defect: Cauldron's
  documented-deferred onEquip (registry row 107) — the "floor-3 room
  disagreement" was the shop's brew screen, and the live capture measured
  a previously unrecorded HIDDEN RNG burn (combatRewardScreen.open()'s
  setupItemReward constructs a full 3-card reward, +9 cardRng + blizz,
  which Cauldron then deletes from view) — row-107 provenance now carries
  the full account for the equip-plumbing owner, and the replay harness's
  reward-row stop now NAMES the deferred relic
  (`deferred_reward_screen_owner`) instead of reading as a mapping
  defect. Retest: `s243_resweep5.log` over the full 94-run divergent set
  = **75 CLEAN + 19 PART, and the PART set is exactly {STS430130} ∪ the
  18 recaptured escape-window artifacts** (set-verified against
  `s243_recap_seeds.txt`) — zero open, zero wildcards. The collided
  first-round recap postprocess was re-run in a clean window (all 10
  workers exit 0, zero untriaged). Six presets green.
  **2026-08-27 — STS430130's PART is discharged: the seventh residual is
  now SCOREABLE, not merely dispositioned.** The equip-plumbing owner this
  row handed Cauldron to has landed (`equip-trio`): Orrery, Dolly's Mirror
  and Cauldron are implemented on the existing `on_equip_screen` surface
  (`relic_pickup_shop.cpp`), including the hidden `setupItemReward` burn
  this row measured — and two things it did not, both found by reading the
  Java rather than the capture: Orrery's offer is FIVE card rows (its four
  `addCardToRewards` plus `setupItemReward`'s own, which nothing deletes),
  and a reward roll made in the merchant's room uses the SHOP's rarity
  table, 9/37 with no `alterCardRarityProbabilities` pass
  (`ShopRoom.java:35-36`, `:52-55`). STS430130 replays **CLEAN to its run
  terminal, 88 records compared** (it stopped at 37 with a PART before).
  The full 94-run divergent set re-swept with the release replay tool is
  now **76 CLEAN + 18 PART, and the PART set is exactly the 18 recaptured
  escape-window artifacts** — set-verified against `s243_recap_seeds.txt`,
  with STS430130 no longer in it. The `deferred_reward_screen_owner` stop
  is retained but can no longer fire: the harness's deferred-onEquip list
  is empty.
  **2026-08-27 — the §8 escalation decision is TAKEN: S2.V2.** The
  deciding number is measured, not assumed: 0 Act-2 boss fights in 2,000
  breadth attempts under the b1.7.0 TE.1 family, and sim-side Act-2/3
  reach structurally 0 for the E0 policies — so a
  `seed_scan --need-boss-kill-act 2` cohort over existing policies has an
  EMPTY candidate set and seed-targeted directed captures have nothing to
  target from. The alternative named at the last session therefore
  collapses to the sanctioned escalation, and **S2.V2 is opened** (row
  before the gate). What this task still owes — the item-2/3 depth
  cohorts, the Mind Bloom directed capture (deferred row above), and the
  §7.4 event-depth coverage join — schedules from S2.V2's scan output;
  the deterministic dashboard is dispatched now over the breadth
  artifacts and extends to the depth cohorts when they exist.
  **2026-08-27 — the Acceptance-line dashboard LANDED**, over the breadth
  evidence: `tools/verify_report/generate_s2_report.py`, the B5.4 sibling,
  writing [verification/s243-dashboard.md](verification/s243-dashboard.md)
  plus `.json`, `s243-event-coverage.csv` and `s243-artifact-manifest.csv`
  from one no-argument run whose defaults name today's cohorts; the raw
  captures stay uncommitted (§7.3) and the manifest's per-artifact SHA-256
  is what pins them. It reopens and hashes **2,023 artifacts / 4.03 GB**
  (three breadth groups + the four recapture groups), and regeneration over
  unchanged inputs is byte-identical — checked, plus a roll-up hash over the
  sorted (campaign, seed, sha256) triples. Verdicts as generated: item 1
  UNMET, item 2 UNMET, item 3 UNMET, item 4 UNMET, every one a *literal
  shortfall*, never an inference. Item 1 reads 2,000 distinct seeds, three
  pinned policy identities, 0 untriaged and 0 open from the classifications
  AS READ (report.json 1,906 clean + 94 state_divergence, layered under
  `s243_resweep5.log`'s 94 verdicts → 1,981 clean + 19 part), with all 19
  dispositioned exactly in `s243_dispositions.json` (24 items, zero
  wildcards): STS430130 → Cauldron row-107 standing deviation, and each of
  the 18 escape-window artifacts → a NAMED recapture the tool re-reads from
  its own evidence set and refuses to accept unless the seed matches and its
  own classification is clean. **Two numbers this task's prose did not carry
  and the instrument surfaced:** the breadth cohort holds **1,998** full-run
  attempts, not 2,000 — STS432031 and STS432655 ended `noop_wedge`, both in
  the escape-window class and STS432031 still wedging after recapture (the
  B5.4 rule that a non-gameplay terminal is not a full-run attempt, applied
  here as an exclusion + a listed shortfall rather than B5.4's abort); and
  the 13 round-1 recaptures still carry 1–2 replay-recognized capture-race
  records each, only the five round-2 (`370CBFA8…`) ones reaching zero, so
  the "zero race records" reading belongs to the settle-lag jar specifically
  — the dashboard prints the count per named recapture instead of asserting
  it. Item 4's coverage join is real evidence, not a placeholder: of the 40
  Act-2/3 registry event rows, **15 are sighted in act 2 or 3 inside clean
  runs** (11 Act-2 EVENT bodies plus Purifier/Transmorgrifier/Wheel of
  Change and The Woman in Blue) and **25 are OWED**, the five rare specials
  (Designer, Duplicator, Knowing Skull, N'loth, The Joust) among them —
  each with a nonzero sim-side census count, so they are scan-schedulable
  rather than unreachable; an Act-1 draw of a cross-act row is
  reported in a separate any-act column and deliberately does not satisfy
  the bar. Items 2/3 render per-BOSS-row zeroes with the missing rows named;
  double-boss detection is a real artifact-side detector (two distinct Act-3
  `act_boss` identities in one run, since `boss_kill_acts` is a set and
  cannot express it) pinned by synthetic fixtures and reported as
  unexercised by live data rather than shipped as a hard-wired false.
  Tests: `verify_report_s2_python_test` (`tools/verify_report/test_s2_report.py`,
  synthetic temp-tree fixtures only — the committed suite never reaches into
  `_oracle_data`). WSL `debug`/`asan`/`release` green;
  `check_stale_counts.sh` and `check_doc_links.sh` clean.
  **2026-08-27 — the S2.V2 depth campaign's FIRST divergence is root-caused
  and fixed in the engine: a double-tapped Rampage under-damaged by its own
  growth step.** Seed STS100009, A20 Ironclad, floor 1, Cultist. The scripted
  line and the live game played identically through the turn-2 Double
  Tap → Rage → Rampage sequence and then parted: the game was on
  COMBAT_REWARD (the Cultist dead) while the sim played on for two more
  steps. The capture decides it arithmetically — its per-action `state_json`
  shows the Cultist at **21/53 HP** with the play about to be issued (turn 1:
  Bludgeon 53 → 21, then Battle Trance and Rage; turn 2: Double Tap, Rage,
  then `play 1 0` = Rampage), and base Rampage under one Double Tap charge
  deals **8 then 13 == 21 exactly**. The sim dealt 8 + 8 = 16 and left it on
  5. ROOT CAUSE: Rampage's accumulator is per-**uuid**, not per-instance.
  `Rampage.use` queues DamageAction then `ModifyDamageAction(this.uuid,
  magicNumber)` (Rampage.java:36-39), and `ModifyDamageAction.update` writes
  every card `GetAllInBattleInstances.get(uuid)` returns
  (ModifyDamageAction.java:26-33) — a walk over cardInUse plus **all five
  piles, limbo included** (GetAllInBattleInstances.java:12-38).
  `DoubleTapPower.onUseCard` builds its replay with `makeSameInstanceOf`
  (DoubleTapPower.java:50), which copies the stats **and the uuid**
  (AbstractCard.java:819-823), and parks it in limbo (:51) — so the replay
  reads the value the original's own ModifyDamageAction just wrote, and the
  replay's write lands back on the original (which is in the discard by
  then). The engine's replay copy was a fresh pool row that SNAPSHOTTED
  `misc` at copy time, and copy time is strictly before the original's growth
  resolves (`resolve_card_play` queues the program at step 4 and fires
  ON_USE_CARD at step 5, mirroring AbstractPlayer.useCard:1369-1370). FIX:
  `CardFlag::REPLAY_MISC_LINK` (bit 15, the last free bit of the existing
  instance flags word) marks a replay copy whose `misc` is a link to the row
  owning the uuid group's counter, and `misc_group_row` (interp.hpp)
  redirects DAMAGE_RAMPAGE's and RITUAL_DAGGER's reads and writes through it.
  **No CombatState field, no `sizeof` move, no schema event** — the
  AUTOPLAY_X_ENERGY precedent (a transient purge copy's misc repurposed), and
  the link is stored already-resolved so a copy of a copy points at the same
  root. NECRONOMICON SHARED THE DEFECT and shares the fix: its replay is the
  same `op_play_card(kPlayCardCopy | kPlayCardPurge | kPlayCardQueueFront)`
  call (Necronomicon.java:70-77). The fix also DISCHARGES S2.34's standing
  deviation at `op_ritual_dagger` ("a kill scored by a same-uuid replay copy
  grows only the transient copy") — the growth now lands on the original,
  which is a master-deck row, so `sync_run_persistent_misc` carries it into
  the run; registry rows 45 and 131 are rewritten accordingly and
  `cards_sidetable.json` re-generated (source hash only). EVIDENCE: the
  regenerated `seed_scan --policies sim_search --policy-seeds 0` line for
  STS100009 now reads `i=10 play Rampage` → `i=11 COMBAT_REWARD claim GOLD`,
  where it previously read `i=11 end` → `i=12 play Bash`; the combat ends on
  the Rampage play, exactly where the game ended it.
  `replay_run_diff --replay` over the capture stays CLEAN (12/12 records,
  0 diffs). Tests: `DoubleTap.ReplayedRampageReadsTheGrownMisc` (the capture's
  21-HP kill, pinned at 8 then 13),
  `DoubleTap.ReplayedRampageGrowthPersistsOnTheOriginalInstance`
  (the write-back half — a later play of the original opens at 8+10),
  `Necronomicon.ReplayedRampageReadsTheGrownMisc`,
  `DoubleTap.ReplayedRitualDaggerKillGrowsTheOriginalInstance`, and the
  negative `DoubleTap.ReplayedNonAccumulatingAttacksAreUnlinkedAndDoNotGrow`
  (Strike and a Searing Blow+2 replay carry no link and grow nothing);
  `CardLimbo.PurgedReplayCopyLeaksItsStampedPoolRowByDesign` was pinning the
  old 8+8 reading and now pins 8+13 with the copy's link. The two other S2.V2
  cohort lines that play Rampage — `STS100038__sim_search__ps0` (5 Rampage
  plays) and `STS108107__sim_search__ps153` (21) — were already exact and are
  unaffected: neither line plays Double Tap at all (checked, 0 occurrences in
  each), and Necronomicon's gate is `card.costForTurn >= 2`
  (Necronomicon.java:62), which a cost-1 Rampage cannot meet unmodified — so
  in those runs the uuid group is the single played instance and the per-row
  counter was already the per-uuid one. Plain repeated Rampage plays never
  needed the redirect for the same reason, which is why
  `CardUncommonRampage.BaseInstanceScalesFiveAfterEachPlay` (8 then 13 on one
  instance) has been green throughout. Six-preset parity unchanged; no
  committed trace moved.
  **2026-08-27 — the STS100075 Neow-potion divergence is a PHANTOM: the
  engine was right and the SCRIPT EMITTER's claim ordinal was wrong.** The
  S2.V2 line stopped at step 1 with "reward screen has no #1 POTION row (id
  'Strength Potion')" while the game offered three POTION rows, which reads
  as a potion-identity divergence and is not one. Derived from the Java, not
  the capture: `NeowReward.activate()` case `THREE_SMALL_POTIONS`
  (`NeowReward.java:268-283`) makes three `PotionHelper.getRandomPotion()`
  calls — the NO-ARG flat overload, `potions.get(potionRng.random(size-1))`,
  one draw each, no tier gate and none of `AbstractDungeon
  .returnRandomPotion`'s rejection sampling (`AbstractDungeon.java:825-850`,
  the *other* overload, which belongs to combat drops) — over
  `PotionHelper.getPotions(IRONCLAD,false)` (`PotionHelper.java:86-153`, 33
  entries = `registry/potions.yaml` id order), with no de-duplication.
  `potionRng` is `new Random(Settings.seed)` at `generateSeeds`
  (`AbstractDungeon.java:398-407`) and nothing spends it before floor 0, so
  STS100075's blessing is draws 1..3: indices **10, 8, 27 → [Weak Potion,
  Strength Potion, CultistPotion]**, three DISTINCT ids — exactly what the
  sim produced (`potion_rng.counter == 3`, rows in that order), so the trio
  never differed and 159a7bf (`equip-trio`) is innocent: its only `neow.cpp`
  hunk is an unreachable `GRID_DUPLICATE` assert arm, and it cannot reach a
  potion draw that precedes the card roll it changed. The real defect is in
  `tools/oracle_bridge/planner/src/script.cpp` `put_claim`, which emitted
  `ord` as an ordinal among rows of the same KIND while emitting an `id`
  beside it — and the live matcher (`driver/script_policy_cmd.py`
  `_match_claim`/`_reward_row_matches`) filters on rtype AND id and takes
  the ord-th survivor, as `_match_take_card`, `hand_ordinal` and
  `deck_ordinal` all do for cards. Claiming POTION row 1 of three distinct
  ids therefore asked for the *second* Strength Potion on a screen holding
  one. Fixed to an identity ordinal (rows with no payload identity — GOLD,
  STOLEN_GOLD, CARD, the keys — emit no `id`, so the kind IS the identity
  and their ordinals are unchanged). Pinned by
  `NeowPayout.STS100075ThreePotionTrioMatchesTheHandRunJava` (literal trio +
  pool indices, the roll-independent half the existing
  `ThreeSmallPotionsAlsoRollAndDiscardTheSetupItemCardRow` cannot see) and
  `SimSearchScript.ClaimOrdinalIsAnIdentityOrdinalNotAKindOrdinal` (both
  distinct-id rows, a forced duplicate still rising, GOLD unchanged).
  **Emitter-only: no sim trajectory, `final_hash` or committed trace moves.**
  Scope of re-emission — of the 14 emitted lines exactly TWO carry a
  POTION/RELIC claim with `ord > 0` and so need a re-emit for the corrected
  field: `STS100075__sim_search__ps0` (step 1) and
  `STS108173__sim_search__ps0` (step 275, a two-relic treasure chest —
  `[Red Skull, Oddly Smooth Stone]`, the same latent stop one act deeper).
  An `ord` of 0 is provably unaffected in either convention.
  **Depth-wave finding closed 2026-08-27 — STS100038's "Match and Keep!"
  stop was an EMITTER index-space bug, not an engine divergence.** The
  scripted line (`sim_search`, ps0) played 389 steps live with zero
  desyncs, through both boss kills into Act 3, and stopped at step 390 on
  the floor-36 board: the emitter derived `choose 10` for a screen
  offering ten candidates. The flip-by-flip comparison of the capture's
  last eight PLAY records against script steps 382–389 is what decided
  the layer. The live game's `choice_list` walked `card6, card10` (miss),
  `card7, card1` — **which MATCHED and left the list**, taking it from 12
  entries to 10 — then `card9, card2` and `card9, card3` (misses,
  revealing Feed/Headbutt/Bash); the sim, from the same deal, re-picked
  `opt 7` and `opt 1` at steps 386/387, which its own `match_menu` would
  have refused had that pair matched, and emitted `index 10` for `opt 10`
  at step 390, i.e. it still saw twelve enabled slots with nothing taken.
  Two boards that disagree about which pair matched are two boards
  addressed in different index spaces — and the space is exactly the one
  `mk_board.hpp` already documents. Nothing about the DEAL is anomalous
  on either side: the live board's six identities read Feed (RARE),
  Sever Soul (UNCOMMON), Headbutt (COMMON), Clumsy (curse), Bash
  (`getStartCardForEvent`) and the unnamed matched pair (the second
  curse), which is exactly `initializeCards()`'s ascension-≥15 arm
  (`GremlinMatchGame.java:63-92`), and the five still-hidden slots are
  precisely those five identities' second copies. `getOrderedCards()`
  (`GremlinMatchGamePatch.java:24-29`) sorts `cards.group` by the screen
  position stored at construction — `target_x = i % 4; target_y = i % 3;
  position = target_x + 4 * target_y`, which is `placeCards`' own
  arithmetic (`GremlinMatchGame.java:278-285`), giving
  `[0,5,10,3,4,9,2,7,8,1,6,11]` — then drops what is no longer selectable
  (`removeIf(c -> !c.isFlipped)`; a matched pair has already left
  `cards.group` at `GremlinMatchGame.java:221-222`). The engine body is
  RIGHT: `match_menu` publishes twelve options and enables
  `board[i].taken == 0 && scratch1 != i`, the same SET one permutation
  away. The follower is right too — `_match_event` sends `choose
  <index>` because the emitter owns that space. What was wrong is
  `script.cpp`'s derivation, which counted enabled options in BOARD-SLOT
  order for every event including this one. **The failure is silent while
  the numbers stay in range**: on a full board every slot is enabled, so
  the emitted index IS the slot and the live driver flips the card at
  that SCREEN POSITION instead — only the permutation's six fixed points
  (0, 3, 4, 7, 8, 11) survive that. STS100038's live walk was therefore
  flipping the wrong card from its FIRST flip (`opt 6` → screen position
  2), and only surfaced at step 390 once the live board had shrunk by a
  pair the sim never matched. Corrected in passing:
  `mk_board.hpp`'s header said "eight of the twelve positions are wrong
  under the identity mapping" — it is six, and a test now counts them. Fixed in the emitter (`event_live_choose_index`), which
  ranks by `sts::replay::match_screen_position` on the play board (the
  `replay_mk_board` INTERFACE target is now linked PRIVATE into
  `seed_scan_core` rather than the permutation being restated) and keeps
  the plain enabled-only count everywhere else. Pinned by
  `SimSearchScriptMatchAndKeep.*` (4): the full board (all twelve slots,
  with an explicit assertion that six of them move — the case the old
  derivation got wrong yet never out of range), the STS100038
  shrinking board (the ten offered slots in position order, `opt 10 →
  index 5`, no emitted index reaching past a ten-candidate screen, and
  `-1` for a card that left the board), the face-up-card compaction
  composing with it in position order, and a no-regression pin on the
  one-button pages plus The Addict's gold-gated generic path. **Sim
  trajectories and scan rows do NOT move** — only the emitted `index`
  field changes — but every already-emitted script containing a Match and
  Keep! play screen is wrong and must be RE-EMITTED, STS100038's
  included; its live capture is evidence of the bug, not of the seed.
  The end-to-end confirmation this cannot supply offline is a directed
  recapture of STS100038 against the re-emitted line: it should now walk
  the whole board and past floor 36. One unrelated repair rode along
  because it made the gate unreadable: `oracle_campaign_pipeline_python_test`
  was RED on the base commit — b1.7.1 added `run_orchestrator`'s
  `args.boss_reward_via_policy` read (`campaign_pipeline.py:761`) without
  giving the hand-built `SimpleNamespace` in
  `test_run_orchestrator_forwards_external_policy_arguments` the field, so
  it raised `AttributeError`; the fixture now carries the flag's
  `store_true` default.
  **2026-08-27 — depth-wave finding: the spawn pre-pass never reached the run
  layer (`act2-hp-offset`).** Capture STS108173 (A20 Ironclad, policy
  `sim_search_skip`) showed a constant 21-hp player deficit from seq 246, the
  first record after the first monster turn of the floor-22 "Centurion and
  Healer" fight, with every other run-level field equal. The `--combat`
  instrument put the fork thirteen records earlier, at the combat's FIRST record
  (seq 243): `monsters[0].move_history[0] 2 → 3` — the sim's Centurion had
  telegraphed FURY where the game telegraphed PROTECT, which the run-level differ
  cannot see because it compares `RunState`, and `RunState` carries no monsters.
  Root cause: `spawn_group_trace` — the ONLY spawn the run layer reaches a combat
  through (`run_advance.cpp` combat-begin step 6) — published `monster_count`
  slot by slot (`state.monster_count++` per init), while `spawn_group`, which
  every test in the repo uses, published it up front. `mark_group_constructed`'s
  construct-all-then-init-all placeholder records were therefore INVISIBLE: every
  group-reading `getMove` bounds its walk by `monster_count`, so a member at slot
  k saw only slots [0, k] and only the LAST member of a group saw the group at
  all. The Centurion is slot 0 of 2 (`MonsterHelper.java:498-500`), so its
  opening `rollMove` read `aliveCount == 1`, took `Centurion.java:143-144`'s
  alone-arm and spent the monster turn on 3 × 7 FURY damage instead of the
  20-block `GainBlockRandomMonsterAction` (`Centurion.java:92-93`), also running
  one `ai_rng` draw short because that action's recipient roll never happened —
  the game builds every member before `MonsterGroup.init()` runs any of them
  (`MonsterGroup.java:31-32`, `:62-64` → `AbstractMonster.init` `:712-714` →
  `rollMove` `:465-467`). Fixed by publishing the kept count before the init
  loop, so the two entry points agree by construction; the other three
  init-time group readers (`need_to_heal`, `gremlin_leader_num_alive_gremlins`,
  `reptomancer_alive_count`) are latent beneficiaries — of the live encounters
  only the Centurion's is a wrong answer today, the Gremlin Leader being last in
  its group and the Reptomancer's opener bypassing `canSpawn`. Tests:
  `MonsterFramework.SpawnTraceMatchesSpawnGroupWhenTheMaskKeepsEverything`,
  `MonsterFramework.SpawnTracePublishesTheKeptCountBeforeAnyInitRuns`,
  `CityNormalsII.CenturionAtSlotZeroOpensOnProtectThroughTheSpawnTrace` (the
  pair-vs-solo column, which the old code makes identical for all 200 seeds) —
  all three verified RED on the pre-fix engine. STS108173 now replays **CLEAN,
  255/255 records compared**, and its remaining `--combat` deltas are only the
  documented translator conventions (`intent` carries `move_id`,
  `translate.cpp:514,527`; `monster_attacks_queued` and
  `cards_played_this_turn` unfilled). Scored against the other three
  depth-campaign stops, measured before and after: STS128113 (floor 27) and
  STS101166 (floor 20) were ALREADY replay-clean to their artifact ends — their
  campaign stops are follower/policy-side, not divergences — and are byte
  unchanged by the fix; STS103364's terminal-only divergence is byte-IDENTICAL
  before and after (seq 509, 14 fields) and is **NOT** explained: its floor-35
  combat is `--combat`-clean through record 508, and at the terminal the live
  game is GAME_OVER at 0 hp while the sim is in COMBAT_REWARD at 8 hp
  (= 2 + Burning Blood's 6) with relic counters reset and a full reward roll
  (card_rng +9, treasure_rng +1, potion_rng +1) — the live death is a Spiker's
  9 Thorns retaliating against a Bite played at 2 hp into a 3-hp Spiker, so the
  open lead is the lethal-THORNS / combat-over adjudication (the S2.49 +
  `survives_clear_post_combat` family), not the spawn pre-pass. WSL
  `debug`/`asan`/`release` green; `check_stale_counts.sh` and
  `check_doc_links.sh` clean.
  **Depth-wave finding closed 2026-08-27 — The Library's read pick is a
  GRID screen, not an event page** (STS100009 ps0, step 224, floor 20,
  Act 2). The re-emitted line — now clean through the floor-1 fight the
  Rampage repair above corrected — played 224 steps live and stopped
  with `script
  expects an EVENT screen, game shows 'GRID'`. Both halves were behaving:
  The Library hosts its twenty-card read on `GridCardSelectScreen`
  (`gridSelectScreen.open(group, 1, OPTIONS[4], false)`,
  `TheLibrary.java:91`), so CommunicationMod reports `screen_type: GRID`
  with the twenty cards in `screen_state.cards`
  (`GameStateConverter.getGridScreenState` ← `ChoiceScreenUtils.getGridScreenCards`,
  which hands back `targetGroup.group` unsorted) and `choose 0..19`, and
  the follower's `_match_event` refused the screen kind exactly as the
  stop contract asks. The RUN LAYER models the same pick as twenty
  ordinary event options over the event BOARD (`library_menu`,
  `city_events_ii.cpp`) with no `grid_kind`, so `script.cpp`'s generic
  event arm claimed it and emitted `{"k":"event","event":"The
  Library","opt":7,"index":7}`. Fixed the way the schema says — an option
  space whose members are dynamically rolled CARDS carries a card
  identity, never a bare sim index — and that is also the only safe
  derivation, because a "corrected" index would have had to be REVERSED:
  the Java builds the group with `group.addToBottom(card)` in roll order
  (`TheLibrary.java:83`) and `addToBottom` is a PREPEND (`group.add(0,
  c)`, `CardGroup.java:459-461`), nothing between there and
  `getGridScreenCards` sorts it (`GridCardSelectScreen.open` only assigns
  `targetGroup`), so the live grid runs in reverse roll order — the same
  fact `command_map.hpp`'s Library arm already proved positionally
  against capture STS432432. The emitter now writes
  `{"k":"grid","ctx":"library","event":"The Library","card":…,"up":…,"ord":…,"opt":…}`;
  the same-identity `ord` is counted in the LIVE (reversed) order the
  follower's `_nth_index` walks, and it is always 0 because the read pile
  is unique by card id by construction (`TheLibrary.java:69-78`).
  **The follower needed NO extension**: `_match_grid` already joins
  `screen_state.cards` by (id, upgrades, ordinal), and a live GRID dump
  from the committed Act-1 corpus (`STS70001`, keys `any_number /
  cards / confirm_up / for_purge / for_transform / for_upgrade /
  num_cards / selected_cards`, each card carrying `id` + `upgrades`)
  confirms the field name — the corpus holds no Library dump because The
  Library is a `TheCity` (Act-2) event. AUDIT of the sibling risk: the
  engine has exactly TWO event option spaces whose members are cards
  (`EventDialogState::board`) — Match and Keep's twelve and The Library's
  twenty. Match and Keep stays an `event` step BY DESIGN (its live screen
  really is the event page and its cards are face DOWN, so there is no
  identity to emit; gap 3's screen-position `index` is the whole answer),
  and every other event card screen is a master-deck grid
  (`EventGridKind`), already emitted as `grid` + `card`/`up`/`ord`. No
  third case exists. **Sim trajectories and `final_hash` do NOT move** —
  verified by re-emitting STS100009 with the built release binary and
  diffing against the archived line: 475/475 lines, header identical
  (`steps` 474, `final_hash` `8b6edea301d288c1`, `max_act` 3, `max_floor`
  41), exactly ONE line changed, step 224, and the card it now names is
  `Rage` — which is row 12 of the live `choice_list` the divergence
  record archived, i.e. the reversal confirmed against live evidence, and
  proof the old `index 7` would have taken `Body Slam`. Pinned by
  `SimSearchScriptLibrary.*` (4: the identity emit with no `index` field,
  every slot naming its own card, the reversed-order ordinal, and the
  intro/leave pages plus the Match and Keep board staying `event` steps)
  and `LibraryGridTest.*` (4, `test_script_policy.py`: the identity join
  landing on `choose 12`, every row reachable by identity, an absent card
  still stopping, and the old bare-index event step still refused).
  RE-EMISSION: any line whose Library visit takes the READ branch —
  among the archived cohort that is STS100009 ps0 alone (STS101166 ps0
  and STS111111 `_skip` ps0 both Sleep, `opt 1`, and are byte-unchanged).
  **2026-08-27 — the Act-3 event-roll divergence class is ONE root cause,
  and it is trap 5's own reproducer: SecretPortal's wall-clock gate.**
  Two depth witnesses (STS108107 ps153, stop step 506, script `Upgrade
  Shrine` vs live `The Woman in Blue`; STS153269 ps174, stop step 533,
  script `Designer` vs live `Fountain of Cleansing`) were derived to the
  same arithmetic. `getShrine` draws
  `tmp.get(rng.random(tmp.size() - 1))` (AbstractDungeon.java:1937), so an
  entry the engine omits does not go merely UNSEEN — it shortens the list,
  changes the drawn INDEX, and returns a different event. SecretPortal's
  gate is `!(CardCrawlGame.playtime >= 800.0f) || !id.equals("TheBeyond")`
  (:1929-1933) and the engine hardcoded it false, so **every Act-3 `?`
  room past 800 s of wall clock resolved to the wrong event**, whether or
  not SecretPortal itself was the draw. Player state at each roll, read
  off the capture's own `oracle` block: STS108107 floor 36, gold 568, hp
  104, 11 relics, deck curse Injury, playtime 924.34705 s, six shrines +
  eleven eligible specials → the game drew index 13 of 14 (The Woman in
  Blue) where the engine drew index 5 of 13 (Upgrade Shrine); STS153269
  floor 38 out of a shop, gold 197, hp 97, 12 relics, deck curse Writhe,
  playtime 960.92236 s → index 10 of 15 (Fountain of Cleansing) vs index 8
  of 14 (Designer). FIX: playtime is now an **explicit input** with a
  default of zero rather than a hardcoded `false` —
  `build_shrine_pool`/`generate_event` take `playtime_seconds`
  (`kUnmodelledPlaytimeSeconds`, `kSecretPortalPlaytimeSeconds`),
  `RunController::playtime_seconds` carries it (a 4-byte carve out of the
  zeroed `pad_lists_align`, so no offset, no `sizeof`, no schema bump and
  no hash moves), and only `--replay`/`--event` set it, from the capture's
  `oracle.playtime` (still dispositioned `oracle`, still never diffed).
  **The trap-5 pin therefore STAYS INTACT for the simulator**: every
  in-engine caller passes 0.0f, so no sim trajectory, fixture or cohort
  line moves, and `SecretPortalIsPinnedFalseInEveryActIncludingTheBeyond`
  passes unmodified. An act-only approximation was CHECKED AND REFUTED
  rather than skipped: captured Act-3 arrivals run from ~520 s up, and
  STS111111 `_skip` ps0 reaches its floor-36 `?` at 710.1448 s, where the
  game's gate is legitimately shut — forcing it open there draws Golden
  Shrine instead of the live The Woman in Blue. Pinned by
  `S243SecretPortalPlaytimeGate.*` (6: both halves of the Java test with
  the inclusive 800.0f boundary; the engine default still being the pin,
  `run_begin` included; each witness asserted BOTH ways — the live event
  at the captured clock and the old divergence at zero; the sub-threshold
  control unchanged at either clock; and the act-only pin failing that
  control). Live verification: `replay_run_diff --replay` is CLEAN on both
  witness captures — "518 records compared (84 on reward screens), 0
  library-order-only, 0 obtain-race, 0 escape-race, 0 preview-race; stop:
  artifact exhausted … first divergence: none" (STS108107) and the same
  verdict at 543 records (79 on reward screens) for STS153269 — and
  `--event` scores every Act-3 sighting zero-diff across the sub-threshold
  and above-threshold captures alike (STS111111 7/7 including the floor-36
  Woman in Blue, STS105835 7/7, STS100009 9/9, STS193303 8/8, STS107575
  7/7). RE-EMISSION: **none** — the emitted cohort lines are a function of
  the simulator, whose playtime is zero, so no trajectory and no
  `final_hash` moves; the two witness lines re-emit byte-identical and
  their live divergence is closed on the replay side, not the script side.
  OWNER CALL LEFT OPEN: the 2026-08-10 ratification of trap 5 rested on
  "the event is avoidable and essentially never optimal", which is true of
  SecretPortal and NOT true of the index shift documented here; whether
  the SIMULATOR should also model a clock (and so need a SecretPortal
  body, still unlanded per S2.33) is a policy question this task
  deliberately did not decide.

  **2026-08-27 — the capture side now MIRRORS trap 5's pin instead of
  diverging from it: the fork holds SecretPortal's wall clock at 0.**
  The paragraph above fixed the REPLAY direction (`--replay`/`--event`
  feed the capture's `oracle.playtime` to `RunController::playtime_seconds`,
  so a recorded clock scores correctly either way). It did not fix the
  SCRIPTED direction: a script the sim emits is computed at playtime 0, so
  a live capture crossing 800 s still desynced from its own script at the
  next Act-3 `?` room and turned every later record into noise (the two
  witnesses were STS108107 at 924 s and STS153269 at 961 s). So the ORACLE
  moved. `patches/OraclePlaytimePinPatch` is a `@SpireInstrumentPatch` on
  `AbstractDungeon.getShrine(Random)` whose `ExprEditor` replaces the ONE
  `getstatic CardCrawlGame.playtime` inside that method
  (AbstractDungeon.java:1930) with `effectivePlaytime()`, pinned to `0.0f`
  = the engine's own `kUnmodelledPlaytimeSeconds`, under the new
  default-on config flag `oraclePlaytimePin`. This is the established
  **patched-fork oracle contract** — the contract is the patched fork, not
  the retail client — precedent: the Discovery wasted-regens boundary and
  the Explosive-Potion THORNS boundary. THE ANCHOR STAYS TRUTHFUL:
  `oracle.playtime` is emitted from that same `effectivePlaytime()` helper
  (`GameStateConverter.getOracleState`), so a capture records the
  EFFECTIVE value the gate saw rather than a wall clock the gate never
  consulted — gate and anchor are one function and cannot disagree, and
  the replay's gate input therefore stays exactly the game's gate input.
  The field is still dispositioned `oracle` and still never diffed, and
  the translator is untouched (no new key, so no schema drift).
  WHY A READ-SITE PATCH AND NOT A ZEROED ACCUMULATOR: all nine
  `CardCrawlGame.playtime` readers in the 12-18-2022 tree were audited
  (PROTOCOL.md §5.4 carries the table) and one of them is not presentation
  — `AbstractMonster.java:1063`'s `playtime <= 1200.0f` SPEED_CLIMBER
  unlock — so holding the
  FIELD at zero would have fired it spuriously; the instrument patch leaves
  the accumulator (AbstractDungeon.java:2001), the save file, metrics, the
  achievement and every screen reading the true clock. VERIFIED WITHOUT
  LAUNCHING THE GAME: ModTheSpire's `InstrumentPatchInfo.doPatch` is
  `Method.invoke(null)` → `(ExprEditor)` → `CtBehavior.instrument`, and
  running that same sequence with the patch's own `Instrument()` against
  the real `AbstractDungeon` bytecode out of `desktop-1.0.jar` gives 1
  playtime read in `getShrine` before, `instrumentedReads == 1`, 0 reads
  and 1 `effectivePlaytime()` call after, `update`'s read/write pair
  unchanged, and a class that still compiles; `effectivePlaytime()` at
  STS108107's captured 924.34705 s returns 0.0f, i.e. the gate predicate is
  false. Sim side: **no change at all** — this is a fork + docs patch, and
  the engine's pin (`SecretPortalIsPinnedFalseInEveryActIncludingTheBeyond`,
  `S243SecretPortalPlaytimeGate.*`) is what the fork is now matching.
  DEPLOYMENT: the new jar pin is
  `ABD95268462FA31E7F7498B45BA4539E3731CC38E59850B547D03AE6F372A4C1`
  (`build_fork.ps1 -CheckDeterminism`, deployment itself left to the
  orchestrator). Captures taken before it keep their real recorded clock
  and keep replaying correctly, so the two cohorts stay distinguishable by
  the artifact header's `fork_jar_sha256`. Measured in passing, flagged and
  NOT changed here: the jar actually on disk in the game install hashes
  `370CBFA8…`, which is what this task's base commit's fork source rebuilds
  to byte-for-byte — so the `9BC4BF6A…` figure the fork-redeploy row and the
  S2.43 dashboard cite for the 2026-08-26 redeploy does not describe the
  deployed artifact.

  **2026-08-27 — STS103364's terminal divergence is closed: a HEAL resolved
  past the player's death and un-killed him (`thorns-terminal`).** The lead
  above named the right family and the wrong mechanism. It is NOT a mutual
  kill and not a combat-over adjudication: at seq 508 the field holds THREE
  monsters (Spiker 3 hp, Spiker 36 hp, Exploder 21 hp) and only the first
  dies. The queue after the Bite is
  `[DAMAGE(thorns 9 → player), HEAL(2), USE_CARD(Bite)]`; the Thorns landed
  correctly and took the player 2 → 0, and then Bite's own `HealAction`
  resolved anyway, because `survives_clear_post_combat` returned `true` for
  `USE_CARD`/`HEAL` at EVERY terminal — a survivor set inherited from before
  the terminals were told apart and never derived for the death one. The
  player came back at 2, so the pump's own re-reading of `player_hp` made the
  terminal a VICTORY with two monsters standing, Burning Blood paid its 6, and
  the reward roll followed. **The Java: the death is latched inside the hit
  that lands it** — `AbstractPlayer.damage` sets `isDead = true` and
  constructs `new DeathScreen(...)` in the same statement pair (:1500-1501),
  the ctor assigns `AbstractDungeon.screen = DEATH` (DeathScreen.java:86), and
  that screen's arm of `AbstractDungeon.update` (`case 17: { deathScreen
  .update(); break; }`, :2092-2095; ordinal 17 == DEATH,
  `AbstractDungeon$1.java:172`) is the one arm that does NOT call
  `currMapNode.room.update()`. `AbstractRoom.update` is the only caller of
  `actionManager.update` in the game (`AbstractRoom.java:231`, `:265`, `:364`,
  all three inside that one method), so the queue FREEZES on the killing
  item — no HealAction, no UseCardAction, nothing. `clearPostCombatActions` was
  never called on this path either (all 20 sites are gated on
  `areMonstersBasicallyDead()`). Fix: the resolver now takes a
  `TerminalKind{kVictory,kDefeat,kEscape}` instead of a `victory` bool; the
  DEFEAT arm resolves NOTHING, and — this is the second half, found by the
  regression test — the victory arm's survivors drain ONE AT A TIME with a live
  `player_hp <= 0` check between them, so **the true mutual kill is a DEFEAT
  too**: the last monster's Thorns kills the player from inside the survivor
  drain, and everything queued behind it is abandoned rather than flipping the
  outcome back (the game's tie-break is structural — the monster's
  `endBattle()` is deathTimer-gated ~2 s of frames out,
  `AbstractMonster.java:866-871`, the player's death is gated on nothing). The
  ESCAPE terminal is deliberately untouched (its Java shape is a third thing —
  no clear at all, and `AbstractRoom.java:277` then drains the queue in FULL —
  and no capture has witnessed the difference). Rode with it: **Burning Blood
  was missing its own guard.** `BurningBlood.onVictory` heals behind
  `if (p.currentHealth > 0)` (BurningBlood.java:34), exactly like Black Blood
  (BlackBlood.java:33); the engine body omitted it on a comment asserting the opposite, and
  `AbstractCreature.heal`'s `isDying` early-out (:391-393) does not cover the
  player (nothing sets `isDying` on him outside `SpireHeart.java:171`). Both
  comments corrected, the guard added. Tests — four verified RED on the pre-fix
  engine, one green-both-ways by design:
  `BeyondNormalsI.SpikerThornsKillsThePlayerAndTheQueuedHealCannotUndoIt` (RED;
  the capture's own shape, field deliberately non-empty so the terminal can
  only be the death),
  `BeyondNormalsI.MutualKillByThornsIsADefeatNotAVictory` (RED),
  `GuardianSharpHide.DefeatTerminalResolvesNothingBehindTheKillingHit` (RED;
  renamed from `…KeepsTheNarrowSurvivorSet`, whose name asserted the bug — its
  BLOCK assertion is unchanged and it now also pins HEAL and USE_CARD),
  `RelicHooks.BurningBloodDoesNotHealAPlayerAtZero` (RED), and the negative
  control `BeyondNormalsI.SurvivableSpikerThornsStillLandsAndTheHealStillResolves`
  — GREEN before and after, which is the point: at the VICTORY terminal the
  retaliation still lands AND the heal still resolves (20 − 7 + 2 = 15). S2.49's
  `DamageAttackerCancel.*`,
  `GuardianSharpHide.RetaliationQueuedBehindTheLethalBlowStillLands` and
  `GuardianSharpHide.DyingOwnersNormalHitStillCancelsAtTheVictoryTerminal` were
  re-run against the pre-fix engine with the new tests beside them and pass
  unchanged in both states. **Verdict: STS103364 replays CLEAN, 510/510
  records compared, 0 library-order-only / obtain-race / escape-race /
  preview-race, "first divergence: none — every compared record was
  zero-diff"** — the S2.V2 depth
  wave's last open combat divergence. Regression evidence: all **2,061**
  campaign artifacts under `_oracle_data/campaigns` re-replayed — 2,036 CLEAN,
  24 PART, 1 ERROR (a `Spire Heart` translator id, Act 4, out of scope) — and
  every one of those 25 non-clean verdicts is BYTE-IDENTICAL to the same run on
  the pre-fix engine, so nothing regressed. **Cohort impact, measured by
  re-emitting all 21 S2.V2 lines:** 18 are byte-identical, 2 (STS108173
  `sim_search_skip` ps0, STS163083 ps359) did not qualify before the fix
  either, and exactly ONE moves — **STS103364's own line, which no longer
  qualifies for `--need-event MindBloom --min-act 3`**: it now dies on floor 35
  where the live game did, `{"steps":501,"final_hash":"2baa4221451f764a",
  "end_reason":"run_over","victory":false,"max_act":3,"max_floor":35}` against
  the old `{"steps":671,"final_hash":"1efe0d8a6ee15602",…,"max_floor":48}`, and
  its last step is the `play Bite` at the Spiker that kills him. Its scan row
  loses `MindBloom|Winding Halls` from `events`, so the Mind Bloom pair needs a
  replacement seat beside STS101166 (unchanged: hash `205938794e73a158`,
  max_floor 44). WSL `debug`/`asan`/`release` green; `check_stale_counts.sh`
  and `check_doc_links.sh` clean.
  **Two more depth-wave divergences closed 2026-08-27, from unrelated
  families: a held EGG upgrades the reward OFFER, and a halfDead target is
  not a DYING target.**
  **(B) STS193303 ps106, step 430, floor 38.** The line takes `Power
  Through`+0 from a CARD_REWARD the live game rendered `power through+ /
  flex+ / clothesline` — both SKILLs upgraded, the ATTACK not, with `Toxic
  Egg 2` held. NOT the `cardUpgradedChance` roll, which the engine already
  read right (`TheBeyond.java:81`, `asc >= 12 ? 0.25f : 0.5f`, so 0.25 at
  A20 — and S2.44's per-act tier-4 row passes because the RATE was never
  wrong); a rate cannot split an offer by card TYPE. ROOT CAUSE: the game
  previews a card reward through every owned relic's `onPreviewObtainCard`
  at ASSEMBLY, at two sites the engine had neither of —
  `AbstractDungeon.getRewardCards`'s tail (`AbstractDungeon.java:1474-1476`,
  the else-arm of the upgrade branch, reached by every card the roll did not
  take) and the `RewardItem(CardColor)` constructor
  (`RewardItem.java:152-162`), which walks the whole item unconditionally and
  is the ONLY preview the COLORLESS flavour gets since
  `getColorlessRewardCards` has no upgrade pass at all.
  `AbstractRelic.onPreviewObtainCard` (`:471-472`) is an empty default with
  exactly THREE overrides in the whole source — `FrozenEgg2` / `MoltenEgg2` /
  `ToxicEgg2`, each `onPreviewObtainCard(c) { onObtainCard(c); }` — so the
  observable net is that an egg owner's reward SCREEN shows its
  matching-type offers already upgraded. The engine had an egg preview pass,
  but only the OTHER direction: `apply_egg_preview_to_open_offers`, the
  claim-site walk for an egg acquired while a screen is already open
  (wave3-followup, discharging the "Egg trio onEquip reward-screen preview
  pass" row) — one of the game's two sites, and the rarer one. FIX:
  `apply_offer_previews` (`combat_rewards.cpp`) at the tail of BOTH
  `roll_card_reward_item` and `roll_colorless_card_reward_item`, egg-gated by
  a shared `relic_previews_obtain_card` the claim-site gate now delegates to,
  replaying the guarded `onObtainCard` bodies over a scratch `CardInstance`
  so preview and obtain-time upgrade cannot drift. It is **egg-gated, not
  generic over `onObtainCard`**: Ceramic Fish and Darkstone Periapt override
  that method but not the preview, and a generic pass would pay +9 gold and
  +6 max HP for cards nobody has taken. Consumes NO RNG. **The RUN STATE was
  already right and no trajectory moves** — `reward_take_card` walks
  `add_card_to_master_deck`, whose `on_obtain_card` fan-out upgraded the card
  at OBTAIN time, so only the OFFER's `card_upgrades` bits were wrong (a
  `RunState` group the differ compares whenever a CARD_REWARD screen is
  attested, and what the live script matcher joins on). Registry rows 57 / 58 /
  59 amended (the Frozen Egg row carries the derivation; the other two
  point at it). Tests:
  `RewardOfferPreview.AHeldEggUpgradesItsTypeInTheOFFERAndSpendsNoRng`
  (three eggs x 32 seeds against an egg-less control: identities and every
  stream position byte-equal, upgrade set exactly to the egg's CardType, with
  a per-egg vacuity guard),
  `RewardOfferPreview.ColorlessOffersTakeTheEggPreviewToo`, and the negative
  `RewardOfferPreview.CeramicFishAndPeriaptDoNotPreviewTheOffer`.
  **(C) STS105835 ps317, seq 489, floor 35** — the full 682-action line, the
  Awakened One killed live, differ verdict `state_divergence` on exactly ONE
  field: `relics[6].counter` (**Velvet Choker**) reading live 2 / sim 1 from
  seq 489 and staying exactly one behind for the remaining ~190 actions. The
  play at seq 488 is `Sever Soul+` into Darkling slot 0 at 28 hp; 31 damage
  half-kills it, and **Necronomicon** (slot 4) then replays the copy into the
  now-halfDead Darkling. Live counted the replay; the sim did not. ROOT
  CAUSE: `AbstractCard.cardPlayable`'s first conjunct
  (`AbstractCard.java:855`) reads **`m.isDying`, the bare field**, not
  `isDeadOrEscaped()`. A halfDead Darkling is at 0 hp with `isDying` FALSE
  (`Darkling.java:201-231` sets `halfDead` and never calls `super.die()`), so
  `canUse` says YES, and `GameActionManager.getNextAction` runs the ENTIRE
  onPlayCard fan-out — player powers, monster powers, RELICS, stance,
  blights, the three pile walks, `++cardsPlayedThisTurn` (`:214-247`) —
  before its OWN dead-target block at `:263-282` notices `isDeadOrEscaped()`,
  pulls the card out of limbo and skips `useCard`. The engine's `card_can_use`
  rejected on `hp <= 0`, which swallows the whole fan-out one step too early.
  This is the THIRD liveness sense, and it had no predicate: `combat_state.hpp`
  now spells `monster_is_dying` (`hp <= 0 && !half_dead`) beside
  `monster_dead_or_escaped` (targeting, halfDead counts DEAD) and
  `monster_basically_dead` (in-the-fight, halfDead counts ALIVE, now expressed
  as `is_dying || escaped`). **`legal_actions` is unaffected** — its per-target
  row is separately gated on `!monster_dead_or_escaped`, the game's reticle
  test — so the only surface that moves is `resolve_card_play`'s dequeue-time
  revalidation, i.e. baked-target autoplays: Necronomicon, Double Tap,
  Distilled Chaos, Havoc, Mayhem. Tests:
  `CardLimbo.HalfDeadTargetPassesCanUseAndStillRunsTheFanOut` and
  `CardLimbo.NecronomiconReplayIntoItsOwnHalfDeathStillCountsThePlay` (the
  witnessed shape end to end, Clothesline half-killing a Darkling and its own
  replay landing on the corpse), both verified RED on the pre-fix predicate
  (counter 1, `cards_played_this_turn` 1) and GREEN after (2 / 2).
  **EVIDENCE.** `replay_run_diff --replay`: STS105835 goes from 5 diff
  records to ONE — 680 of 681 compared records zero-diff, the whole seq
  489-492 `relics[6].counter` block gone; what remains is seq 680, the
  terminal `screen 'COMPLETE'` record the run layer does not model
  (`command_map.hpp:1795`), byte-identical to the pre-fix log and therefore
  untouched by either fix. STS193303 replays **CLEAN, 444/444 records** to
  its artifact end. **RE-EMISSION: one line, and it is presentation-only.**
  All 19 archived cohort lines re-emitted on the fixed engine: every
  `final_hash` is unchanged and 18 files are byte-identical;
  `STS193303__sim_search__ps106` changes on exactly TWO lines — step 430
  `Power Through` and step 510 `Impervious`, both SKILLs, both `up` 0 → 1 —
  which is the stopped step reading the live screen's answer. (The two
  NO-QUALIFY rows in that sweep, `STS108173 sim_search_skip ps0` and
  `STS163083 sim_search ps359`, are pre-existing: their rescan rows are byte
  identical to the archived ones and `lines_v2/` already lacked both files.)
  **LEAD D is NOT explained and stays open**, checked rather than assumed:
  `STS128113__sim_search__ps27` and `STS101166__sim_search__ps0` re-emit BYTE
  IDENTICAL and both still replay clean to their artifact ends, so neither
  fix touches them. What the captures do pin, for whoever takes them: (i)
  STS101166 ps0 step 325 — the sim plays a `Bash+` that the live game holds
  at cost 2 with 0 energy. It is the card the `Dark Embrace` draw pulls back
  after the Armaments exhaust, out of a reshuffle of an 8-card discard, and
  `Mummified Hand` had set it to cost 0 while it was in hand. The live reset
  is `Soul.update`'s DRAW_PILE and DISCARD_PILE arms (`Soul.java:199-220`),
  which call `clearPowers()` → `AbstractCard.resetAttributes`
  (`:2035-2045`, `costForTurn = cost`) on **every** move into either pile,
  mid-turn included; the engine's `reset_cost_for_turn` (`piles.cpp:67`) is
  wired only to the end-turn sweep and to exhaust, so a card discarded
  mid-turn keeps its per-turn cost. Fixing that moves pile-move semantics and
  wants its own task. (ii) STS128113 ps27 step 328 — under Snecko-applied
  Confusion the live `Power Through+` costs 2 with 1 energy left, so the sim
  is one energy up or one cost down on a hand whose five cards were all drawn
  fresh that turn; note `ConfusionPower.onCardDraw` writes nothing at all when
  the roll EQUALS the base cost, so a stale `costForTurn` survives it — the
  same reset gap in (i) is the first thing to rule in or out.

  **2026-08-27 — LEAD D is CLOSED, and it was TWO root causes in one family:
  card cost state that the engine wrote where the game does not, and did not
  write where the game does.**
  **(1) The pile-move `resetAttributes` (witness i).** Confirmed exactly as
  the lead read it, and the live capture pins it INSIDE the artifact rather
  than only past its end: STS101166 ps0 floor 20 shows `Bash+(cost 0)` in the
  discard pile at seq 330 and `Bash+(cost 2)` at seq 331 — no turn boundary
  between them, the one-record lag being the Soul's animation — and the same
  0 → 2 step is visible twice more on `Clothesline` (seq 317→318, 321→322).
  `Soul.update` (`Soul.java:193-231`) switches on the destination
  CardGroup's type and calls `clearPowers()` → `resetAttributes`
  (`:2035-2045`) in the DRAW_PILE (case 2, `:205-212`) and DISCARD_PILE
  (case 3, `:213-221`) arms; the recovered switch-map `Soul$2` pins the
  labels (MASTER_DECK 1, DRAW_PILE 2, DISCARD_PILE 3, EXHAUST_PILE 4), so the
  master deck and the Soul-less exhaust pile get nothing from this seam.
  `reset_cost_for_turn` is now wired at every Soul-routed mover —
  `moveToDiscardPile` (`CardGroup.java:836-841`, i.e. `UseCardAction:126` and
  `DiscardAction:89`), `moveToDeck` (`:892-896`), `moveToBottomOfDeck`
  (`:898-902`) and EmptyDeckShuffleAction's per-card `souls.shuffle`
  (`:55-58`) — and **deliberately NOT** at the `MakeTempCard*` sites, which
  add through `ShowCardAndAddToDiscardEffect` / `ShowCardAndAddToDrawPile-
  Effect` (`:37` / `:46-50`) with a bare `addToTop`/`addToBottom`/
  `addToRandomSpot` and no Soul at all. `moveToHand` (`:864-890`) calls
  applyPowers, never clearPowers, so a card returning to the HAND keeps its
  this-turn cost. The combat-PERSISTENT writers are untouched by
  construction, since `resetAttributes` restores `costForTurn` FROM `cost`:
  Corruption/Blood for Blood/Confusion all move `cost` itself and carry no
  this-turn marker.
  **(2) `upgrade_instance` clobbered live cost state (witness ii, and half
  of witness i).** Derived, not assumed: instrumenting the sim's hand at each
  step shows STS128113 ps27 floor 27 holding `Power Through+ cost 1 flags 0`
  where the live hand at seq 336 reads `Power Through+(c2)`. The player has
  **Warped Tongs**, so the card was drawn (Snecko's Confusion rolled it to 2
  — `costForTurn = cost = newCost`, `ConfusionPower.java:43`, PERMANENT) and
  then upgraded; `AbstractCard.upgrade()` reaches the cost ONLY through
  `upgradeBaseCost` (`:725-735`), and `PowerThrough.upgrade`
  (`PowerThrough.java:40-45`) is upgradeName + upgradeBlock, so live kept 2
  while the engine re-seeded `cost_now` from the registry row and got 1. The
  same line explains the OTHER half of witness i: at STS101166 ps0 step 318
  the engine showed `Bash+ cost 2` where the capture (seq 325) reads
  `Bash+(c0)` — Armaments upgrading a Mummified-Handed Bash. The blind
  re-seed also wiped the whole per-instance flag word (freeToPlayOnce,
  purgeOnUse, exhaustOnUseOnce, the cost bookkeeping). Now: the base cost
  moves only when the card really calls upgradeBaseCost — exactly when the
  registry cost differs across the two levels, a call with an unchanged
  argument being a behavioural no-op — the Java body is transcribed
  (difference preserved, re-applied only to a positive `costForTurn`, clamped
  at zero), Blood for Blood keeps its relative arm (`:45-57`,
  `upgradeBaseCost(this.cost - 1)`), and only the AUTHORED half of the flag
  word (`kAuthoredCardFlagMask`, types.hpp — Apparition+ dropping ETHEREAL is
  the live case) is re-read.
  **CONFUSION CORNER, pinned the way the Java reads it:** `onCardDraw` writes
  NOTHING when the roll equals `card.cost`, so a this-turn cost would indeed
  survive a redraw — but with (1) in place no card in the draw or discard
  pile can still carry one, so the corner is now unreachable through a pile
  move and the engine's existing equality behaviour is left exactly as it is.
  **EVIDENCE.** Both captures `--replay` **CLEAN** — STS101166 332/332
  records, STS128113 338/338, to their artifact ends, unchanged from before
  the fix (they were clean before too: `--replay` compares RunState, and the
  CombatState walk is `--combat`, a triage print the tool itself calls "never
  a pass/fail signal", so neither verdict could ever have carried this
  defect). The decisive evidence is therefore the re-emitted lines plus the
  tests. **RE-EMISSION** (whole 21-row cohort, pre-fix vs post-fix on this
  worktree, written to scratch — `_oracle_data` untouched): 16 rows
  BYTE-IDENTICAL, **5 rows move** — `STS128113 ps27` fd97df15032dd833 →
  4c3ce5bed4257125 (victory → max_floor 36, **no longer `--need-victory`**),
  `STS128113 ps47` 7966b9fc7b734778 → 5c9aa99e871aa428 (→ floor 44, no longer
  `--need-victory`), `STS101166 ps0` 205938794e73a158 → bc258b4ad4124933 (→
  act 2 floor 25, no longer `--need-event MindBloom --min-act 3`),
  `STS105835 ps317` d19125596ef21091 → 9cc317d55f73c9a6 (→ act 2 floor 33, no
  longer `--need-boss-act 3`), and `STS181259 ps674` 71c879c633fbff46 →
  846c53b414cfbe8c (still qualifies, floor 51). That is the E0 policy losing
  the free damage it was buying with costs the game never offered — a policy
  ceiling, not a content regression. **Three further cohort rows drift from
  their ARCHIVED lines but are NOT this fix**: `STS103364 ps0`
  (1efe0d8a6ee15602 archived vs 2baa4221451f764a on the pre-fix base sha, and
  it already fails the Mind Bloom filter there), plus the two already-known
  pre-existing NO-QUALIFY rows `STS108173 sim_search_skip ps0` and
  `STS163083 ps359`, both byte-identical pre and post. Stage-A fixtures, the
  twin suites and the whole tree are green and unmoved on all three presets
  (2,690/2,690 each). Tests, nine of them verified RED on the pre-fix engine:
  `PilesCostReset.MummifiedHandCostZeroIsLostOnTheReshuffle`,
  `.DeepBreathStyleFullReshuffleResetsToo`,
  `.MidTurnDiscardOfThePlayedCardResetsItsThisTurnCost`,
  `.EndOfTurnHandDiscardResetsAsThePileMoveNotOnlyTheSweep`,
  `CardSkillsWarcry.PutBackOnTheDrawPileResetsTheThisTurnCost`,
  `CardUpgradeInCombat.ACombatPermanentCostSurvivesAnUpgradeThatKeepsTheCost`,
  `.AThisTurnZeroSurvivesAnUpgradeThatKeepsTheCost`,
  `.UpgradeBaseCostCarriesTheThisTurnDifferenceAcross`,
  `.PerInstanceRuntimeFlagBitsSurviveTheUpgrade`; and the two NEGATIVES that
  are green on both sides,
  `PilesCostReset.CombatPersistentCostReductionSurvivesTheSameReshuffle` and
  `CardUpgradeInCombat.BloodForBloodUpgradeStaysRelativeToItsReducedCost`.
  **OPEN FOR THE OWNER:** the five moved lines are archived artifacts of a
  now-superseded engine; re-capturing them (or retiring the four that no
  longer qualify) is a campaign decision, not an engine one. And the
  `--replay` blind spot named above is worth its own row: no acceptance
  surface compares in-combat card COSTS against the capture, which is why a
  whole cost-state family reached the depth wave undetected.
  **2026-08-27 — the LAST un-modelled seam is closed: the COMPLETE screen at
  the A20 double-boss handoff, and it hid a real engine defect behind it.**
  The two deep witnesses stopped one record short of their terminals on
  `screen 'COMPLETE' is not modelled by the run layer` (`command_map.hpp`),
  STS128113 (the double-boss VICTORY, Time Eater first) at seq 655 with 3
  fields and STS105835 (death to the second boss) at seq 680 with 4. **Those
  fields were not a defect**: the capture holds the first Act-3 boss room's
  bare proceed button (AbstractRoom.java:327 denies a non-endless TheBeyond
  boss its reward screen, so ChoiceScreenUtils :80-83 labels the dump
  COMPLETE) while the engine runs ProceedButton's `goToDoubleBoss` (:210-220)
  inline off the kill — S2.28's deliberate collapse, no player decision — so
  the sim is one crossing ahead and the diffs are exactly what the crossing
  writes: `floor` +1, Maw Bank's +12 `gold` (MawBank.java:31-34), Pantograph's
  +25 `hp` (Pantograph.java:31-40) and the counter relics latching -1 → 0 at
  the second fight's atBattleStart. **FIX, replay layer:** a COMPLETE arm with
  the two producers the label has in S2 scope — the handoff `proceed` is a
  NOOP under a structural gate (`is_double_boss_handoff`: A20, act 3,
  MonsterRoomBoss, sim in COMBAT in a Boss room, `boss_cursor == 1`, sim floor
  == capture floor + 1), and the finished Act-3 victory's `proceed` is the run
  TERMINAL (:104-105's goToVictoryRoomOrTheDoor is the S3 keys surface the run
  layer ends the run instead of entering). The handoff record is **compared
  SHIFTED, not skipped** — against the capture's own post-proceed record, so
  the line is a zero-diff assertion; what that cannot see is only state the
  crossing itself overwrites, which no later record can depend on.
  **AND THE SEAM WAS HIDING AN ENGINE GAP** that only became comparable once
  the walk continued: `goToDoubleBoss`'s first line is `bossKey =
  bossList.get(0)` (:211) — the same single field `setBoss` writes at act
  construction (AbstractDungeon.java:349-350), fed to
  MonsterHelper.getEncounter by `getBoss()` (:1992-1995) and persisted by
  SaveFile (:246) — and nothing mirrored it into `boss_ids[act-1]`, so both
  runs diverged on `boss_ids[2]` from the crossing to the terminal (capture
  `Donu and Deca` 60, sim 59 / 58). The FIGHT was never wrong —
  `on_player_entry` reads `boss_list[boss_cursor]`, not the mirror — which is
  why S2.28 could not see it. `finish_combat_after_action`'s double-boss
  branch now assigns `boss_list[boss_cursor + 1]` (the Java's `get(0)`, given
  that the cursor lags the entry pop by one) ahead of the transition, exactly
  as :211 sits ahead of :218. `fired_accum.hpp`'s claim that "a slot never
  changes within an act" was falsified by this and is corrected in place; its
  nonzero-wins rule is unaffected and is what let the capture's live value
  win. **EVIDENCE.** Both captures now replay CLEAN to their true terminals:
  STS128113 `667 records compared (112 on reward screens) … stop: run
  terminal` + `5 post-victory ending record(s) skipped` + `1 A20 double-boss
  handoff record(s) compared against the capture's own post-proceed record`,
  and STS105835 `683 records compared (110 on reward screens) … stop: run
  terminal` + the same handoff line — `first divergence: none` on both.
  Tests: `BossVictory.TheA20DoubleBossHandoffMovesTheActsBossIdToTheSecondBoss`
  (verified RED on the pre-fix engine: `boss_ids[2]` 19, want 18) with the
  negative `BossVictory.AnActTwoBossVictoryLeavesTheActsBossIdWhereItWas`
  (green on both sides), and five command-map pins —
  `ReplayCommandMap.TheDoubleBossHandoffProceedIsElidedBecauseTheEngineCrossedAlready`,
  `.TheDoubleBossHandoffGateNeedsEveryTermOfProceedButtonsPair`,
  `.TheFinishedActThreeVictoryProceedIsTheRunTerminal`,
  `.ADeathParkedAtRunOverIsNotACompleteScreenTerminal`,
  `.ACompleteScreenCommandOtherThanProceedIsNotModelled`. The new summary line
  is deliberately NOT spelled `…-race`: the pipeline's strict accounting
  scrapes `N <name>-race` as capture artifacts and this is not one.
  **2026-08-27 — CLOSED. The dashboard now reads the depth cohorts, and all
  four §6 S2-G2 bars are MET.**
  [verification/s243-dashboard.md](verification/s243-dashboard.md) (plus
  `.json`, `s243-event-coverage.csv`, `s243-artifact-manifest.csv`)
  regenerates from one no-argument
  `tools/verify_report/generate_s2_report.py` run and is byte-identical over
  unchanged inputs — checked, alongside the roll-up hash `b7713a68…` over the
  sorted (campaign, seed, sha256) triples. It reopens and hashes **2,066
  artifacts / 4.29 GB** across **46 cohorts in five roles** (breadth 2,002
  runs, recapture 23, depth 17, iteration 22, preflight 2). **The numbers, as
  generated.** *Item 1 MET*: 2,002 distinct breadth seeds and **2,000
  full-run attempts** — the shortfall the instrument itself found (1,998, two
  `noop_wedge` escape-window seeds) closed by the `s243_breadth_top2` top-up,
  which is folded into the breadth role rather than excused; three pinned
  policy identities; 0 untriaged, 0 open. *Item 2 MET*: all **3 of 3** Act-2
  registry BOSS rows carry a zero-diff boss-reward claim, a boss chest, a
  boss-relic PICK and an act-2→3 transition — Automaton 4 fights / 3 take /
  1 skip, Champ 8 / 7 / 1, Collector 4 / 4 / 0 — with take witnessed for all
  three rows and skip for two, which is what the design bar asks (per-row
  claim+pick+transition, cohort-wide take AND skip). *Item 3 MET*: all **3 of
  3** Act-3 BOSS rows witnessed killed (Awakened One 1, Donu and Deca 3, Time
  Eater 4) and **3 completed A20 double-boss victories over 2 distinct
  first-boss identities** (`s2v2_db47_b`/STS128113 Time Eater first;
  `s2v2_dbv_103509a`/ps347 and `s2v2_dbv_103509b`/ps472, both Donu and Deca
  first), with 2 further runs that reached the second boss and lost reported
  and deliberately not counted. *Item 4 MET with dispositions*: of the 40
  Act-2/3 registry event rows, **31 are sighted in act 2 or 3 inside clean
  runs** (up from 15) and **9 carry an exact per-row `reachability-argument`**
  — Upgrade Shrine, FaceTrader, Fountain of Cleansing, Knowing Skull, N'loth,
  NoteForYourself, SecretPortal, The Joust, Colosseum — **0 OWED**. Two of
  those nine are stronger than "rare": NoteForYourself is structurally absent
  from the A20 special list (`isNoteForYourselfAvailable` is false at
  ascension ≥ 15, and every captured `oracle.specialOneTimeEventList` in this
  evidence omits it), and SecretPortal is now pinned unreachable on BOTH
  sides (trap 5 in the engine, `OraclePlaytimePinPatch` in the fork). The
  other seven each pair a nonzero sim-side census count with the fact that
  `seed_scan --need-event` demonstrably schedules directed captures — proven
  live by the two Mind Bloom captures in this report's own cohorts, not
  asserted. **Retest instrument: `s243_resweep7.log`** (`71a546f4…`), the
  full-corpus replay on the landed engine — **2,043 CLEAN / 23 PART / 0
  ERROR** over all 2,066 artifacts, the 23 PART being exactly the 18
  escape-window breadth captures and the 5 round-1 recaptures, every one
  dispositioned; **zero s2v2 artifacts non-clean**, and zero retest verdicts
  naming a campaign outside the consumed cohorts (printed, so "the sweep and
  the cohort selection cover the same corpus" is a number rather than an
  assumption). Dispositions: **23 run items, 9 event-row items, 22
  campaign-row items, zero wildcards.** Three instrument changes the depth
  evidence forced, each fail-loud: (i) a **campaign-level** disposition list,
  because what a divergence-stopped wave lost is a whole cohort *seat*, not a
  (seed, classification) pair — every `iteration` cohort must carry one, and
  a `superseded-by-recapture` must name a consumed cohort, other than itself,
  all of whose captures read clean today; (ii) run rows read from
  `campaign_progress.json` when the driver stopped a campaign mid-seed, with
  the artifact set enumerated from the worker DIRECTORY so the capture the
  driver died on is inventoried and classified too (26 such captures);
  (iii) **Act-3 kills read artifact-side**, because `3 in boss_kill_acts` is
  true of every run that merely crossed into Act 3 — the captures show the
  Act-2 boss chest's trailing MAP record already carrying `act: 3` — so a
  kill is witnessed by a LATER Act-3 `act_boss` identity (only the
  double-boss handoff produces one) or by a victory, and only a victory
  counts toward the double-boss bar. The Cauldron standing deviation
  (STS430130) was REMOVED from the dispositions rather than left to render as
  "no longer exercised": it replays clean since the `equip-trio` landing. The
  day's depth wave landed **eleven fixes** — 5174b7b (replay-copy misc link),
  a1821cd (Neow claim ordinal, emitter), 039598c (Match and Keep index space,
  emitter), 0300d4b (spawn pre-pass), 8c4cb7a (The Library GRID identity,
  emitter), e7338a4 + e61b358 (SecretPortal's wall-clock gate, engine and
  fork), 45f9528 (HEAL past the death terminal), bd2dc55 (egg reward-OFFER
  preview + halfDead `canUse`), 37b543e (pile-move `resetAttributes` +
  upgrade cost clobber) and 3481c08 (double-boss COMPLETE seam + stale
  `bossKey`) — several carrying two root causes each. Tests:
  `verify_report_s2_python_test` grew twenty cases (progress-sourced rows,
  the mid-seed capture, the unclassifiable-artifact abort, the missing-capture
  abort, the complete-campaign requirement for breadth/preflight, the
  campaign-status vocabulary, `translation_drift`, the six campaign-
  disposition cases, the preflight no-disposition rule, the sweep-scope pair,
  the two Act-3 kill/double-boss cases, the follower take/skip axis pair and
  the Act-4 `Spire Heart` allowlist) — still synthetic temp-tree fixtures
  only, never reaching into `_oracle_data`. WSL `debug`/`asan`/`release`
  green; `check_stale_counts.sh` and `check_doc_links.sh` clean.
- **S2.44** `[x]` ∥ **Tier-4 additions.** Pre-registered hypotheses per
  design §6 item 6 (act pools + exclusion effects, per-act upgrade
  chance, boss shuffle + double-boss conditioning, one-time-pool
  depletion, canSpawn-gate pool-cursor effects), Holm-corrected family.
  **Deps:** S2-G1 **Acceptance:** suite green at B5.3 scale with α
  discipline unchanged; negative-control mutant rejected.

  **Log:** `tools/dist_check/dist_check_s2` — a **new family alongside**
  B5.3's sixteen, not an extension of them: that set was registered,
  corrected and reported as closed, and reopening it would retroactively
  move every threshold it was judged against. 13 hypotheses, family-wise
  α **0.01**, Holm-Bonferroni — B5.3's numbers and B5.3's justification
  (strong control under arbitrary dependence), which this family needs
  more, since six of its rows read one `generate_monster_lists` call per
  seed. Scale is B5.3's: `--seeds` refuses below 10,000, the acceptance
  run is 20,000. Registration lives in the tool README; `s2_run.sh` is
  the entry point beside `run.sh`.

  Every row is a **joint** law wherever one exists, because that is what
  turns an exclusion into an EXACT support assertion instead of a soft
  frequency claim: an impossible cell has probability 0 and one
  observation in it returns p = 0. So the act-2 first-strong row forbids
  the game's only two-key exclusion (Chosen → Chosen and Byrds + Cultist
  and Chosen), the act-3 row forbids 3 Darklings' self-exclusion **while
  requiring Orb Walker's self-exclusion to remove nothing** (it is
  weak-only; the first-strong loop can never roll it), the pair rows
  forbid the immediate repeat, and the boss rows forbid a repeated boss.

  The **analytic half is a library with its own tests**
  (`dist_check_s2_expect`, `DistCheckS2Expect.*` — seven tests pinning
  the laws against hand-derived numbers) because a wrong expectation and
  an engine defect are indistinguishable on a campaign report line.

  **Two harness defects found during bring-up, both real, both fixed.**
  (1) The event chain's second draw replayed the first draw's split roll
  verbatim: `generate_event` reads a THROWAWAY copy and leaves
  `event_rng` byte-identical (the run layer's +1 comes from
  EventHelper.roll in `nextRoomTransition`, not from generateEvent), so a
  chain conditioned on an act-1 shrine could only ever take the shrine
  branch again. It presented as χ² ≈ 3.8e4 on BOTH event rows — the
  harness, not the engine. (2) The two canSpawn strata were seeded 12,079
  apart while each consumed 20,000 seeds, so at the acceptance scale 40 %
  of the "independent" swapped stratum replayed the held stratum's pool
  shuffle; the correlation was inflating that row's fit (p 0.176 → 0.060
  once disjoint).

  **The canSpawn row is stratified, and the brief's premise needed
  correcting.** Black Blood's gate is `hasRelic("Burning Blood")` — the
  starter is what LETS it spawn, so a fresh Ironclad has exactly ONE
  gated act-2 boss row (Ectoplasm's `actNum <= 1`, §5 trap 9), not two.
  Both act-2 bodies are covered by sampling two independent strata (fresh
  Ironclad, and the Neow boss-swap line that traded Burning Blood away →
  two gated rows) as one 5-cell mixture. The law is the front-scan
  negative-hypergeometric, which is the right shape only because BOTH the
  pop and the rejection reroute are `remove(0)` for BOSS tier.

  **Double-boss conditioning is read off the PUBLIC surface**, not the
  list: `encode_public_view` at `act == kFinalAct, boss_cursor == 1`
  publishes `boss_prefix[0]` beside `second_boss_reserved`, so the
  sampled pair is what a player standing in the second room can see, and
  design §4.4's "bossList[1] of the same shuffle, not a re-draw" is the
  hypothesis. The Act-2 negative is an exact check. Driving real A20
  three-act runs to two boss kills was **not** attempted — S2-G1's soak
  measured `victories = 0` under E0 heuristics, so a run-driven cohort is
  S2.43's oracle problem, not a distributional one.

  **The first acceptance run flagged, and the flag was triaged to a
  FALSE POSITIVE rather than re-seeded away.**
  `s2.encounter.act3_weak_pair` came in at **p = 6.750359e-04** against
  its **7.692308e-04** Holm threshold — the α tail a 13-row family at
  α 0.01 is built to produce about one run in a hundred. Three
  independent lines close it, none of them a re-seed:

  - the registered law is uniform 1/6 over the six off-diagonal cells,
    and at **2,000,000 seeds** the same sampler scores **χ² = 3.61 on
    df 5** against it — the null is true, and a real bias would have
    grown with n instead of shrinking (χ² 21.42 → 7.71 → 5.67 → 3.61 at
    20k / 100k / 500k / 2M on the *same* seed base);
  - the pool roll's band edges are uniform to one grid point in 2^24
    (0.33333334f / 0.6666667f land on exact 24-bit boundaries), and
    populateMonsterList's rejection is a re-roll, so the conditional is
    exactly 1/2 — there is no mechanism that could bias the second weak
    entry;
  - ten independent 20,000-seed blocks at other bases score χ² 1.7–8.8,
    all retained.

  The registered seed base, sample size, α, correction and expectations
  were **left exactly as written** — re-seeding until green is the
  discipline violation this instrument exists to prevent.

  **The principled fix, adopted as a permanent protocol change:
  REPLICATE BEFORE FLAGGING.** A row Holm-retained at stage one is final
  and its replicate is **never run** (a contract `confirm_by_replicate`
  enforces, not an optimisation). A row rejected at stage one triggers
  exactly ONE confirmatory replicate at the **same per-row threshold**;
  it is finally flagged only if BOTH stages reject, else it reports
  `RETAINED-AFTER-REPLICATE` with both p-values. The replicate block is
  each sweep's own block **XOR `kReplicateSeedSalt`** — the ASCII bytes
  `'S' '2' '4' '4'` in the high word, derived rather than picked and
  fixed before any replicate was run; every stage-one base is below 2³²
  and no sweep carries into bit 32, so the two stages are disjoint by
  construction. The rule applies to the controls too, so the power claim
  is tested *under* the rule. Family-wise consequence, registered in the
  README: a true null must land in its own α tail **twice on independent
  blocks**, so the false-flag rate is **~α² per row** instead of α, while
  power is essentially unchanged — a true bias rejects both stages. **No
  other threshold, seed, α, sample size or expectation moved.** Logic is
  `sts::dist_check::confirm_by_replicate` (stats.hpp), pinned by five
  `HolmReplicate.*` tests.

  **Family verdict under the rule — `RESULT PASS` at both scales.** At
  20,000: 12 rows PASS at stage one; `s2.encounter.act3_weak_pair` is
  `RETAINED-AFTER-REPLICATE` (stage-one p 6.750359e-04, **replicate
  p = 7.098214e-01** — a non-replication by three orders of magnitude).
  Smallest p among the other twelve 9.098567e-03. At the registered
  minimum 10,000 all thirteen PASS at stage one outright
  (`act3_weak_pair` p = 1.105656e-03) and no family row replicates. Both
  runs are reported rather than the greener one.

  **Power is asserted, not assumed** (the T0.6 precedent). Four
  deliberately-wrong samplers run through the identical machinery on
  every campaign run and must clear the family's strictest Holm threshold
  **in both stages**: `mutant.first_strong_ignores_exclusions`,
  `mutant.double_boss_repeats_first_boss`,
  `mutant.special_one_time_returns_next_act` (the crossing rebuilds the
  one-time pool too) and `mutant.can_spawn_rejection_returns_relic`. All
  four two-stage rejected at p = 0 **and** replicate p = 0, at both
  scales — the first three by landing on impossible cells, the fourth at
  χ² = 1.02e4 on df 4. A survivor fails the run. Because the controls
  reject by construction, the replicate stage runs on every campaign;
  the per-row contract is what keeps a retained row from being
  re-examined.

  **Green:** all six presets (`debug`/`asan`/`release` under WSL-GCC,
  `win-debug`/`win-asan`/`win-release` under clang-cl), zero failures;
  `win-debug`'s cache carries `/EHsc`. The family runs clean under ASan +
  UBSan, `encode_public_view` path included, and the clang-cl release
  build reproduces **every p-value byte-identically** to WSL-GCC. B5.3's
  own 16-hypothesis family re-run unchanged at 20,000 seeds (RESULT PASS,
  smallest p 2.185602e-01 — its landed number).
  `check_stale_counts.sh` and `check_doc_links.sh` clean. New tests:
  `DistCheckS2Expect.*` (7) and `HolmReplicate.*` (5). Re-derive counts
  with `ctest -N | tail -1`.
- **S2.45** `[x]` ∥ **Throughput re-baseline.** B5.5 methodology over
  three-act runs: per-step and per-combat floors must hold unchanged; new
  whole-machine three-act run rate recorded with methodology as the S3
  baseline (expected lower per run — not a regression; design §6 item 7).
  **Deps:** S2-G1 **Acceptance:** release-preset numbers recorded;
  per-step/per-combat floors green.
  **Log:** 2026-08-10 — landed. Full report:
  [verification/s245-throughput.md](verification/s245-throughput.md).
  **All three floors HOLD**, release preset, B5.5's methodology and its
  frozen `run_throughput.sh` discipline unchanged (`--benchmark_min_time=2s`,
  one metric per binary invocation, exactly one `items_per_second` per run),
  median of three whole-wrapper invocations: batch **13,517,300**
  steps/sec/core (floor 50,000), complete combats **60,323.0**/sec/core with
  1,424,080 `combat_steps`/sec (floor 300), complete A20 runs **195,311**/sec
  whole-machine with 9,185,780 `run_steps`/sec (floor 0.4). Every invocation
  printed PASS; the report checks the floors against the *worst* of five
  readings (12,175,500 / 49,780.2 / 148,261 — margin ≥ ×166), because the
  run-to-run spread is ±12 % with no covariate this task could isolate.

  **Design §6 item 7's premise is falsified, and that is the finding.** Item 7
  expects a drop proportional to run length; run length did not move —
  **47.04** actions/run against B5.5's 46.93 (+0.2 %) — because under the E0
  random policy **no run in the frozen corpus leaves Act 1**. The benchmark now
  prints that rather than leaving it inferred: `act2_runs=0 act3_runs=0` over
  ~35,000 measured runs, with `terminal_act_sum == runs_counted` exactly (mean
  terminal act **1.000**) as the positive control that separates "nothing
  reached act 2" from "the probe is dead". Cross-checked by a *different* tool
  over the *same* corpus — `fuzz_soak --seeds 1000 --policies random --reps 1`
  is the benchmark's case set case-for-case (`fuzz_policy_seed_for` ==
  `policy_seed_for(seed, RANDOM, 0)`) — which reads 47.05 actions/case, act-2
  cases 0, and **act-1 boss rooms entered 0**, deepest floor 13 of 16. So the
  recorded number is a re-baseline against three-act *content*, not three-act
  *trajectories*, and it is comparable to B5.5's. Same policy ceiling S2.41
  measured and [verification/s242-deep-reach.md](verification/s242-deep-reach.md)
  §11 records: no weight-free policy in this repo produces a three-act
  trajectory to time. The S3 baseline is therefore recorded as a **pair** —
  195,311 runs/sec (corpus-conditional) plus **9,185,780 run-steps/sec, which
  is length-independent** and is the number a future three-act length should be
  projected through. Quoting the runs/sec as "three-act runs per second" is
  exactly the claim the report refuses.

  **The S2.48/S2.49 risk is answered: no.** Run length being constant makes
  every B5.5 ratio a per-step cost ratio. `run_steps`/sec is ×0.956 of B5.5 —
  and the run-level step is precisely where S2.48 put `sync_live_gold` on every
  in-combat advance, so at this resolution it costs nothing measurable.
  Combat stepping is ×0.712 and the 10k-state batch ×0.498 across a whole
  content stage; the batch's leading candidate is state size, not the
  interpreter (`sizeof(CombatState)` 3,896 → **8,088** B, so the batch working
  set went ~39 MB → ~81 MB against a 96 MiB L3, and that benchmark is a
  bandwidth-bound sweep). **Deliberately not attributed** — these are absolute
  one-build floor checks and the sanctioned comparison instrument is the
  interleaved `tools/bench_ab.sh` over two binaries; an A/B against the B5.5
  tree would compare two content sets, not two implementations. The narrow
  follow-up that would settle it is named in the report §6: A/B `d57e077`
  against `646bd18`, the two commits either side of S2.48 + S2.49.

  **Harness extension** (`benchmarks/bench_throughput.cpp`, the only source
  change; no engine, registry, fixture, golden or schema touched): four plain
  counters on the whole-run benchmark — `runs_counted` (the denominator a rate
  line cannot supply), `terminal_act_sum`, `act2_runs`, `act3_runs` — the act
  read **once per run** from `RunController::run.act` after `RUN_OVER`, so
  there is no per-step cost. No three-act *variant* was needed: the benchmark
  runs to `RUN_OVER` and the engine has been act-general since S2.12, so it
  already measured against three-act content; what it could not do was say how
  far it got. `run_throughput.sh` untouched, no floor attached to the new
  counters, `benchmarks/README.md` records them. Six presets green (WSL
  `debug`/`asan`/`release` all with `-DSTS_BUILD_BENCHMARKS=ON`, so the new
  benchmark source is compiled and warning-clean under GCC;
  `win-debug`/`win-asan`/`win-release` at their standard settings, `/EHsc`
  verified in the `win-debug` cache). `check_stale_counts.sh` and
  `check_doc_links.sh` clean.
- **S2.46** `[x]` **Verification report + CI corpus + proactive audit.**
  B5.4 pattern: aggregated report with literal S2-G2 shortfalls; curated
  compressed corpus extended with three-act traces incl. one double-boss
  run; `g7_proactive_manifest` extended with S2-discovered families and
  the executable audit re-run.
  **Deps:** S2.43, S2.44 **Acceptance:** report committed under
  `docs/verification/`; CI replay of the extended corpus green in every
  preset; audit green.

  **Log (2026-08-27).** All three deliverables landed; **no S2-G2 item is
  UNMET and none is pending.**

  **(1) The aggregated report** is
  [verification/s2-verification.md](verification/s2-verification.md), the
  seven §6 items answered one at a time by the instrument that OWNS each
  number, with what that instrument did not say recorded beside it. Items
  1–4 are cited from the S2.43 dashboard rather than restated — it
  regenerates byte-identically over unchanged inputs, so a second copy of
  its counts here would be a staler copy of a number with one owner; what
  is copied is the verdict and the generation date. Item 5 is this task's
  own audit, item 6 S2.44's registered family (`RESULT PASS` at both
  scales, all four negative controls two-stage rejected — cited, not
  re-run; the re-run command is in the report's §10), item 7 S2.45. **The
  one literal shortfall is item 7's baseline half**, and it is the item's
  premise rather than the engine: run length did not grow, because no run
  in the benchmark corpus leaves Act 1 under a weight-free policy
  (`act2_runs=0 act3_runs=0` over ~35,000 runs, `terminal_act_sum ==
  runs_counted` as the positive control), so the S3 baseline stands as a
  *pair* — corpus-conditional runs/sec plus length-independent
  run-steps/sec — and "three-act runs per second" remains unquotable. The
  report's §9 carries five standing limits stated plainly rather than
  argued around: Act 4 out of scope (a victory's Spire Heart tail is a
  named replay skip and a translator refusal), `--replay` comparing
  `RunState` and therefore **not** in-combat card COSTS (the blind spot
  that let a whole cost-state family reach the depth wave), the five
  archived S2.V2 lines the pile-move/upgrade fix moved, the open
  SecretPortal owner question, and the unattributed ×0.712 / ×0.498
  per-step ratios.

  **(2) The CI corpus gained an Acts 1–3 sibling**, not an edit: the
  committed `act1_a20_50` archive and manifest are **byte-unchanged**.
  `tests/golden/oracle_corpus/three_act_a20_5.tar.gz` is **5 curated
  whole-run captures, 977 KB compressed** (`9572f301…`) in a new
  `STS-ORACLE-CI-CORPUS v2` — `s2v2_dbv_103509a`/STS103509 (Donu and Deca
  → Time Eater, a completed double-boss VICTORY carrying the whole item-3
  shape), `s2v2_db47_b`/STS128113 (the second first-boss identity, Time
  Eater first, also a victory), `s2v2_awk_105835`/STS105835 (the Awakened
  One killed, then a death to the second boss — the LOSING double-boss
  shape no victory can exercise), `s2v2_mb_102529`/STS102529 (the Mind
  Bloom Act-1-boss re-fight) and `s2v2_skip_b`/STS111111 (the boss-relic
  SKIP axis; every other pick runs the take config). Three format
  departures, each forced rather than chosen: **provenance is per entry**
  (two fork pins are represented on purpose, and one aggregate pin would
  hide exactly what the cohort table exists to record;
  `pipeline_version` is null precisely when the driver stopped a campaign
  before postprocess, and null on a `complete` campaign is fatal); **a
  translated trace is optional with a stated reason** (STS128113's
  translation aborted on the Act-4 `Spire Heart` id, STS111111's wave
  never reached postprocess — both captures replay clean, which is what
  the corpus asserts, and a missing trace carrying no reason is refused);
  and **the stored classification is not a selection input at all** —
  STS128113 and STS105835 were `translation_drift` / `state_divergence`
  on capture day and are clean on the landed engine, which is precisely
  why they belong in a regression corpus, so `--three-act` requires
  `--replay-bin` and RE-REPLAYS every pick, refusing anything not
  zero-diff to its run terminal with zero capture-race records. The smoke
  enforces the corpus's own contract in CI (every entry act-3, ≥ 1
  *completed* double-boss run, both boss-relic axes present, the axis read
  from the SHA-pinned policy config and never a directory name), because
  a curated corpus that quietly lost its double-boss run would still
  replay green and would no longer be the evidence the report cites. Two
  new ctest cases beside the Act-1 pair —
  `OracleCorpusReplay.ThreeActCorpusReplaysZeroDiff` and
  `.ThreeActInjectedSyntheticDivergenceFailsLoud` — ~13 s under `debug`.
  `build_ci_corpus.py`'s pick list is pinned independently of the moving
  dashboard defaults, the B5.4 rule; `ci_corpus_smoke.py` reads both
  formats and takes `--expect-entries`, asserted rather than read off the
  manifest it is checking. Seven new `test_verify_report.py` cases
  (`verify_report_python_test`, 23 total): the committed corpus's
  contract, the two fail-loud contract rules, the shallow-run and
  missing-pin rejections, the entry-count assertion, the unstated-trace
  refusal, the policy-axis derivation and the provenance rule.

  **(3) The proactive audit went 6 families → 21, 26 regressions → 101**,
  all registered and passing, `VERDICT: PASS`
  ([verification/g7_proactive_audit.md](verification/g7_proactive_audit.md)
  + `.json`, regenerated from a full debug ctest run). The fifteen added
  families cover the whole S2 campaign harvest, not only the 2026-08-27
  wave: `s2-replay-copy-shared-instance-state`,
  `s2-spawn-prepass-group-visibility`, `s2-combat-terminal-adjudication`,
  `s2-card-cost-state-lifecycle`, `s2-reward-offer-preview`,
  `s2-liveness-senses`, `s2-double-boss-handoff`,
  `s2-unmodelled-clock-inputs`, `s2-cross-act-capture-derivation`,
  `s2-live-run-state-projection`, `s2-deferred-hook-boundaries`,
  `s2-positional-monster-identity`,
  `s2-screen-arm-shape-and-ownership`, `s2-emitter-index-space-identity`
  and `s2-act-two-three-replay-seams`. The six G7-era families are
  unchanged and were verified rather than re-authored. Two judgement
  calls worth recording: each family carries the fix's own **negative
  control** where one exists (a family pinned by a single assertion is one
  a plausible refactor deletes in a single edit), and the boss-chest
  replay seam — the root cause every one of the breadth wave's 19 Act-2
  crossers hit, which never got a unit test of its own — is pinned by the
  new three-act corpus replay itself, which is a registered ctest name and
  therefore auditable evidence rather than prose. `sources` gained
  `docs/s2-tasks.md` and `verification/s243-dashboard.md`; the checker
  fails loud if a source path stops existing.

  WSL `debug`/`asan`/`release` green, corpus replay included in each;
  `check_stale_counts.sh` and `check_doc_links.sh` clean. No committed
  trace moved.
- **S2.47** `[x]` **Boss-relic offer storage (the S2.43 unblock).** Discharge
  the "BOSS_REWARD.screen_state.relics — schema storage" deferred row: durable
  `RunState` storage for the three boss-chest offers plus the reveal bits
  (`seen`/`chose_relic`), so the translator can emit a BOSS_REWARD dump's
  offers and the differ can score design §6 S2-G2 item 2's *zero-diff*
  boss-relic pick. **This row is the conventions-§5 planned site for the
  `SCHEMA_VERSION` bump** (RunState's remaining declared pads are scattered
  single bytes — no legal carve holds 3×uint16; if the implementer finds a
  legal carve after all, taking it instead is in scope and the bump is
  simply not spent). Scope: the `RunState` storage with
  `RunController.boss_chest` (`BossChestState`, boss_chest.hpp) re-seated on
  it so there is exactly ONE source of truth; the trace-container follow
  (diff_harness `kTraceFormatV2` tracks `engine::SCHEMA_VERSION`,
  trace.hpp:77-78); translator emit (translate.cpp's `fr.defer`red
  `BOSS_REWARD.screen_state.relics`, registry-join preserved); differ
  compare; `byte_class.hpp` classification of every new/moved byte with the
  offers-public-only-once-`seen` gating carried over; fixtures regenerated
  exactly once via their checked-in generators.
  **Deps:** S2-G1 **Acceptance:** translator round-trips a BOSS_REWARD dump's
  three offers into the new storage and a mismatched offer REDs in the
  differ (both directions pinned); byte-classification tripwire and the GT0
  leak gates green (hidden-twin equality unchanged before `seen`); Stage-A
  fixtures + `twins_v1.bin` regenerated exactly once with the bump
  accounted; six presets green.
  **Log:** 2026-08-10 — landed, schema **v8**. The MOVE was taken, not the
  mirror: `BossChestState` (16 B, unchanged layout) left `RunController` and
  became `RunState.boss_chest`, a pure TAIL APPEND after `neow_rng` —
  `sizeof(RunState)` 2184→2200, every pre-v8 offset held (two new
  `static_assert`s in run_state.hpp prove append + no tail padding;
  `sizeof(RunController)` is net unchanged), so there is exactly ONE source of
  truth and no sync point to pin. The pad-carve alternative was checked and
  rejected as the row predicted (largest legal contiguous declared pad is 3
  bytes; 3×u16+3 flags need 9+). Struct/enum definitions moved to
  run_state.hpp; boss_chest.hpp keeps provenance + room-flow functions.
  Translator: `BOSS_REWARD.screen_state.relics` now EMITS (seen=1,
  screen=RELIC_SELECT, chose_relic=0 — what a live screen attests per
  BossRelicSelectScreen.java:353/:101-108), registry join kept fail-loud, and
  a count ≠ 3 aborts as drift (BossChest.java:37). Differ:
  `diff_run_states` compares the group by name. Replay tool: new paired
  `neutralize_unattested_boss_chest` — only a BOSS_REWARD dump attests the
  offers, so records whose capture side has `seen == 0` neutralize the group
  on both sides (the `keys` shape, made conditional); applied at all four
  translated-vs-sim pair sites. Byte class: `kBossChestTable` sub-row moved
  RunController→RunState verbatim (offers HIDDEN until `seen`); fuzz
  `hash_controller` dropped its separate boss-chest update (the bytes are in
  `hash_state(rc.run)` now) and `h.treasure` is the ordinary chest alone.
  kTraceFormatV2 →8. Fixtures: `twins_v1.bin` regenerated ONCE via
  `gen_twin_fixtures` (win-debug per its header) — old vs new differ at
  EXACTLY ONE byte, header offset 12 = `engine_schema_version` 7→8; all 18
  cases + view payloads byte-identical (PublicView unchanged, v4 stays). The
  20 Stage-A combat fixtures are NOT regenerated — `sizeof(CombatState)` is
  untouched and the v1 loader checks only that size and the v1 tag, so they
  were never invalidated (the brief's "regenerate if invalidated" resolved to
  NO). Found against the brief: docs/public-view-audit.md had S2.11-era
  drift — `boss_relic_choice_reserved` still said "reserved / still zero in
  v2" though S2.11 populated it, and §5's RunController table never carried a
  `boss_chest` row at all; both fixed in place (conventions §4). The S2.42
  fail-loud pin had to be re-tamped from a 1-element to a 3-with-one-fake
  list so the new count check cannot mask the join check. Two boss-chest
  no-op pins (`SkipClosesTheChestWithoutBurningAnything`,
  `FiresNoRelicChestHooks`) now normalize the two legitimately-moved bytes
  (`screen`/`seen`) before their whole-struct memcmp. Named tests:
  `Translator.BossRewardRelicsAreEmittedIntoBossChestStorage`,
  `Translator.BossRewardOffersRoundTripThroughTheDifferBothWays` (match →
  empty; mismatch → names `boss_chest.relics[1]`),
  `Translator.BossRewardRelicsRejectAnyCountButThree`,
  `Translator.BossRewardRelicsStillJoinTheRegistryAndFailLoud` (kept),
  `RunDifferBossChest.EveryMemberNamedSeparately`,
  `StateLayout.RunStateHasNoImplicitPadding` (walk extended),
  `MonsterFramework.SchemaVersionAndCap…` (pin follows to 8), the `Tripwire.*`
  and `TwinFixture.*` suites, and the whole `BossChest.*` reveal-timing/twin
  block re-seated on `rc.run.boss_chest`. PROTOCOL.md §3.8 row updated
  I→S→stored-and-diffed. Six presets green; counts re-derived by
  `ctest -N | tail -1` at land time, not restated here.

- **S2.48** `[x]` **Stolen-gold settlement ordering vs in-combat gold gains
  (owner-directed fix).** Close the standing-deviation class the G7 campaign
  carries as ~110 of its 150 dispositions: `fold_back_combat` banks in-combat
  gold (Hand of Greed's `GreedAction` is the one in-scope producer) before
  `settle_stolen_gold` on both combat-end paths, which matches the game only
  when the Greed kill preceded the steal — a steal-first line with the purse
  below the steal amount over-credits the thieves (recorded at
  `run_advance.cpp` beside `settle_stolen_gold` and in stage-b-tasks.md's
  obligation row). Owner decision 2026-08-10: cover the ordering, don't
  carry it. Model the game's purse semantics at steal time against the live
  purse, keeping the fold-back layering for everything else; both
  combat-end paths covered. This intentionally changes landed Act-1
  behavior on the affected lines — owner-authorized; regenerate any moved
  fixture via its checked-in generator with the meaning-diff stated.
  **Deps:** — (must land BEFORE S2.43 scores its campaign, so the ~110
  standing-deviation dispositions become retestable exact matches instead
  of accumulating further) **Acceptance:** directed tests for the four
  orderings (steal before/after the Greed kill × purse above/below the
  steal amount) on both combat-end paths, each pinned against the cited
  Java re-read in full; the stage-b obligation row and the dispositions
  carrier note updated; fixtures regenerated only if actually moved; six
  presets green.

  **Log:** the purse is LIVE, and the fix needed NO schema change — the
  brief's stop-the-line and the old row's "CombatState layout change"
  premise both dissolved on the KnowledgeState precedent: bookkeeping for
  what is already charged lives in a new combat-scoped transient
  `RunController.stolen_live` (`StolenGoldLive`), not in either frozen
  schema. `sync_live_gold` (run_advance.cpp, published) runs after every
  in-combat step and again inside `fold_back_combat` (all four end sites:
  reward entry, defeat, Act-3 terminal, Colosseum reopen): new steals
  charge `min(goldAmt, RunState.gold)` round-robin in slot order FIRST,
  then the unbanked `combat_gold` remainder banks — the game's in-step
  order, since a same-step greed gain can only be a start-of-next-turn
  play (Mayhem) resolving after the monster phase (Looter$1/Mugger$1 +
  `DamageAction.stealGold` :98-114; `GreedAction.java:37-38`).
  `settle_stolen_gold` no longer moves the purse: it catch-up-syncs and
  sums the dead thieves' `stolen_live.taken` — the direct-call attribution
  suite (`CityNormalsII.TwoThieves*`) passed unchanged. One landed Act-1
  pin moved as intended
  (`RunEscape.KilledLooterReturnsStolenGoldThroughTheScreen` now asserts
  the live mid-combat purse). Layout: `stolen_live`
  sits at the controller tail; the former `pad_tail[2]` bytes became
  `pad_live_align[2]` ahead of it (tripwire rows + audit §5 updated), and
  the struct now ends flush. Fixtures: the 20 Stage-A combat fixtures
  stand (CombatState untouched — full suite replay green with no
  regeneration); `twins_v1.bin` regenerated once via its checked-in
  generator (meaning-diff: the header's `run_controller_size` stamp
  11480→11600; payloads re-verified by replay, 18 cases). Named tests:
  the eight `RunStolenGoldOrdering.Victory*`/`.Defeat*` quadrants —
  {StealBeforeGreedKill, GreedKillBeforeSteal} × Purse{Below, Above} on
  both end paths; steal-first×below is the quadrant the old model
  over-credited (return 10 where it said 20) —
  `RunStolenGoldOrdering.SyncChargesSameStepStealsBeforeSameStepGreed`,
  `RunStolenGoldOrdering.SyncBanksEarlierGreedBeforeALaterStealBoundary`.
  Stage-b obligation row DISCHARGED in place; the ~110 stolen-gold
  disposition carrier notes re-pointed at this fix (retestable for
  S2.43's re-triage, rows preserved). Six presets green; counts re-derived
  by `ctest -N | tail -1` at land time.
- **S2.49** `[x]` **Attacker-side cancel of queued multi-hit attacks
  (owner-directed fix).** `DamageAction.update` (DamageAction.java:69-73)
  cancels a queued hit whose owner is dying or half-dead, so a monster
  killed (or half-killed) partway through its own multi-hit attack loses
  the remaining hits; the engine has no attacker guard at all and lands
  every queued DAMAGE item. Named at `interp_damage.cpp`'s KNOWN GAP block
  as residue for "whichever batch lands the first halfDead producer" —
  those batches (S2.25/S2.28) landed without it and the gap was in no
  deferred table until now; this row is that accounting made good. Owner
  decision 2026-08-10: fix both halves (`isDying` and `halfDead`) together,
  per the code comment's own terms. Changes landed Act-1 behavior (any
  monster killed mid-multi-hit, e.g. by thorns) — owner-authorized; Stage-A
  fixtures move only if a fixture trace actually contains such a line —
  prove which, regenerate via `tools/fixture_gen` with zero-diff-in-meaning
  shown for the rest.
  **Deps:** — (land BEFORE S2.43 for the same disposition-hygiene reason as
  S2.48; ∥ with S2.48 only if their edits stay disjoint — both touch the
  combat-end/damage seams, so serialize if in doubt) **Acceptance:**
  directed tests: a monster killed mid-multi-hit loses its remaining hits;
  a halfDead transition (Darkling / Awakened One) cancels identically; the
  guard reads the OWNER of the queued hit, not the current actor; negative:
  already-resolved hits stay resolved; fixtures accounted as above; six
  presets green.

  **Log:** live as `damage_attacker_cancelled` (`interp_damage.cpp`,
  declared in `interp_damage.hpp`), called from `execute_opcode`'s
  plain-DAMAGE case ONLY — plain DAMAGE is the opcode that models a
  queued `DamageAction`, and the actions behind the other damage opcodes
  carry no owner guard and must not inherit one
  (`DamageAllEnemiesAction.update` :48-83 and `VampireDamageAction.update`
  :29-45 have neither the guard nor `shouldCancelAction`; the remaining
  op_damage callers are player-sourced card actions — all re-read in
  full). The Java re-read sharpened the brief in two ways, both encoded:
  (1) the owner test appears TWICE in `DamageAction.update` — the
  first-tick `info.type != THORNS && (info.owner.isDying ||
  info.owner.halfDead)` (:69-73) AND the every-tick `shouldCancelAction`
  preamble's `source != null && source.isDying` term (:65-68;
  `AbstractGameAction.java:81-83`, `setValues` copies `source =
  info.owner`), which collapse into one resolve-time condition under this
  engine's atomic resolution; (2) the owner guard does NOT test
  `isEscaping` — escape is a TARGET-side term only
  (`AbstractCreature.isDeadOrEscaped` :780-790) — so an escaped owner's
  queued hit still lands, pinned rather than assumed. The THORNS
  exemption is load-bearing content: `ExplosivePower` queues
  `SuicideAction` BEFORE its own THORNS-typed explosion (:47-57), so the
  Exploder is always dying when it resolves and only the exemption lets
  it land (the landed Exploder suite polices it). Engine encoding of
  `isDying || halfDead`: `hp <= 0 || kMonsterFlagHalfDead`, spelled as
  the disjunction even though halfDead implies hp == 0 today. The
  player-owner half of the Java guard is deliberately not encoded:
  `pump_step`'s top-of-step terminal check retires the queue at
  `player_hp <= 0` before any later item resolves, `try_player_revive`
  runs synchronously inside the dropping op, and the player is never
  halfDead — derivation in the predicate's comment. Named tests
  (damage_pipeline_test.cpp, `DamageAttackerCancel.*`):
  `MonsterKilledMidMultiHitLosesRemainingHits` (isDying term),
  `HalfDeadTransitionCancelsIdentically` (the REAL Awakened One
  die-veto/`on_damaged` latch path; also proves a
  `monster_basically_dead`-shaped guard would not cancel),
  `GuardReadsTheQueuedHitsOwnerNotTheCurrentActor` (interleaved owners),
  `AlreadyResolvedHitsStayResolved` (negative),
  `DyingOwnersThornsReflectionIsExemptAndLands`,
  `EscapedOwnersQueuedHitStillLands` (rules out a
  `monster_dead_or_escaped`-shaped guard; with the half-dead test the
  pair forces exactly `hp <= 0 || halfDead`). Fixtures: NONE moved —
  `FixtureOracle.AllFixturesReplayWithZeroDiffs` (the 20 Stage-A traces)
  and `TwinFixture.ReplayingEveryCommittedCaseReproducesItsStoredView`
  (`twins_v1.bin`) green unchanged on all six presets, so no committed
  trace contains a monster-sourced non-THORNS hit resolving after its
  owner's death; no regeneration, `twins_v1.bin` stands. S2.48
  interaction: none — the Looter/Mugger steal accrual is
  queue-time-synchronous and Mug/Lunge are single-hit, so no steal rides
  a cancellable trailing hit; the `RunStolenGoldOrdering.*` suites passed
  unchanged. The interp_damage.cpp KNOWN GAP block is rewritten in place
  as the implemented-guard derivation, and S2.2F's "Named residue, not
  stubbed" inventory paragraph re-pointed at the discharge. Six presets
  green; counts re-derived by `ctest -N | tail -1` at land time, not
  restated here.

- **S2.V2** `[x]` **Sim-consulting scripted driver + depth pre-scan (the
  sanctioned §8 escalation).** Opened 2026-08-27 on the measured condition
  design §6's driver-risk paragraph names: S2.43's breadth wave put the
  b1.7.0 TE.1 family at 0 Act-2 boss fights in 2,000 A20 attempts
  (29.5 %/28.3 % Act-1 boss-fight reach, 19 Act-1 kills, 19 Act-2
  entries, deepest floor 27) — insufficient for the S2-G2 depth bars, the
  exact trigger the design paragraph names. Scope, per §6 item 3's
  sanctioned shape (sim pre-scan chooses (seed, policy, policy-seed)
  triples whose scripted line reaches the target; the oracle then
  confirms the full run zero-diff): (a) a deterministic, weight-free
  **sim-consulting policy** in the engine's tooling family — turn-local
  search / shallow rollout / 1-ply lookahead over engine snapshots
  (memcpy snapshot+restore is the instrument; no wall-clock, no
  randomness outside the declared policy-seed); (b) **`seed_scan`
  extended** to run it at scale and select triples through the existing
  S2.42 filters (`--need-boss-act`, `--need-boss-kill-act`,
  `--need-victory`, `--need-boss-id`), emitting a per-triple **scripted
  action line**; (c) a driver-side **script-following policy** behind the
  STS-POLICY-IO v1 seam (its own SHA-pinned binary in the
  `survival_policy_cmd` mould) that plays a scripted line against the
  live game and, on any desync between the game's advertised choices and
  the script's next step, STOPS and marks the run divergent for Stage-B
  triage rather than improvising — a desync is capture evidence, not a
  failure to route around.
  **Deps:** S2.42 (planner + b1.7.0 family), S2.43 breadth (the
  escalation measurement) **Acceptance:** measured sim-side reach report
  at scanned scale (per-act boss-fight / boss-kill / double-boss rates
  for the sim-consulting policy) committed under `docs/verification/`;
  scripted lines replay deterministically in the sim
  (`--verify-determinism`, zero mismatches); the S2-G2 item-2/3 depth
  cohorts **demonstrably schedulable from the scan output** — a concrete
  triple list covering every Act-2 and Act-3 registry BOSS row, boss-relic
  take AND skip lines, ≥ 3 double-boss lines over ≥ 2 distinct first-boss
  identities, and ≥ 1 Act-3 line drawing Mind Bloom (`--need-boss-id` +
  the event filter) for the deferred-row capture; the script-following
  policy_cmd unit-tested against recorded protocol dumps WITHOUT
  launching the game; six presets green.
  **Log:** 2026-08-27 — landed, and the escalation instrument delivers all
  but ONE cell of the depth bars. Report:
  [verification/s2v2-sim-reach.md](verification/s2v2-sim-reach.md).
  (a) **`PolicyKind::SIM_SEARCH`/`SIM_SEARCH_SKIP`** (values 5–6, appended;
  PolicyKind is not in the stage-b shared-namespace table, recorded here) —
  `tools/fuzz/src/policy_search.cpp`: combat is a 1-ply search over
  `RunController` snapshots completed by a deterministic threat-aware static
  tail to COMBAT END (a one-turn horizon was measured first and could not
  see a power's payoff), 2-ply through a boss fight's opening under hard
  breadth/turn/board gates; map nodes and event options are scored by a
  one-floor rollout (the Python's label word-buckets have no sim-side
  labels to read); the rest is the b1.7.0 `greedy_policy.py` port — R1
  (widened by an Act-2+ standout clause, two-screens invariant preserved by
  reading the same rewards item at both screens), R2 as evaluation
  hold-values, `ACT_PROFILES`, R4's never-take list with the `_skip`
  identity differing in exactly that one rule, and `boss_chest.seen`
  breaking the open/skip 2-cycle a deterministic argmax would otherwise
  livelock in (first smoke scan measured exactly that, two floor-17
  LIVELOCK rows). A run-layer NO-OP GUARD proves the winning candidate
  mutates the controller on a snapshot before the tie-break — found live by
  the scan itself: Drug Dealer's two-pick grid advertises the first-picked
  card and re-picking it is a documented engine no-op
  (`city_events_i.cpp`), which a uniform-random policy escapes by luck and
  an argmax repeats forever (STS90069/ps0, NO_PROGRESS at step 216).
  Integer arithmetic only, every bound a constant, one tie-break draw per
  decision. (b) **`seed_scan --script-dir`** + the **STS-SCRIPT v1**
  emitter (`sts/planner/script.hpp`/`script.cpp`, schema normative in the
  planner README): every filter-hitting triple's pass-A trajectory
  re-decoded against the states it was taken in — screen kind + stable
  identity (card game id + upgrades + same-identity ordinal, reward-row
  kind + payload id, map column + symbol, the event `choose` index
  translated into the game's ENABLED-ONLY space per command_map's
  two-index-space note) — and refused unless the replay reproduces the
  row's `final_hash`; `--verify-determinism` compares trajectories too.
  (c) **`script_policy_cmd.py`**, the STS-POLICY-IO v1 follower in the
  `survival_policy_cmd` mould (config-driven script dir, SHA-pinned like
  every external policy): match-first with non-consuming glue rules
  (confirmation-only screens; the GRID pick-then-confirm seam; and — added
  2026-08-27 when the first live campaign stopped on
  `divergence_STS100009_ps0` at Neow's opening `talk`, exactly the stop
  contract doing its job — the collapsed one-click dialog whose sole
  candidate is a single `choose`, the vestigial-click class the engine
  deliberately collapses; plus the mirror-seam SKIP rule from the same
  run's step 2, a scripted `proceed`/`confirm` with neither alias legal
  consumed without emitting where the game auto-advances), and
  on ANY other mismatch it writes a `script_divergence` record and exits 3
  — the driver's FatalEnvironmentDrift path stops the campaign; a desync is
  capture evidence, never routed around. **Measured, frozen build, 107,424
  rows / 32.96M actions** (release, A20): stage-1 breadth 40,000 fresh
  seeds — Act-1 boss kill **37.23 %** (E0 0.12 %, live b1.7.0 2.5 %),
  Act-2 fight 3.83 %, Act-2 kill 0.36 % covering all three registry bosses;
  re-seeded depth pass **19.6 % Act-2 kill**; skip cohort 51 Act-2 kills;
  and **three complete A20 double-boss victories** — STS128113/ps27,
  STS128113/ps47 (Time Eater first), STS108107/ps153 (Donu and Deca first)
  — the first three-act wins any instrument in this repo has produced.
  Determinism: 2,000-row `--verify-determinism` sweep over both policies
  plus per-triple verification of all 14 cohort scripts, zero mismatches.
  The 14-triple cohort (report §5) covers: every Act-2 boss killed with
  relic TAKE and act-2→3 crossing (two lines each), one SKIP kill per
  Act-2 boss, the three victories (≥3 double-boss over ≥2 first-boss
  identities — met), and two Act-3 Mind Bloom lines for the deferred
  directed capture. **The one unschedulable cell, reported not weakened:
  Awakened One — 0 kills in 553 Awakened-first Act-3 boss fights** (43,648
  dedicated re-seed rows) against 3/585 on the Time-Eater/Donu-and-Deca
  pair; report §6 carries the mechanism hypothesis (Curiosity feeds on the
  policy's own power-taking) and the options for the next session.
  *(Superseded by the dated note at the end of this Log: that 0 was the
  VICTORY probe, which at A20 requires the second boss too — the cell is
  discharged and seven kill triples are scheduled.)* Known
  limit carried in §7: 4.7 % of rows livelock in an optional-hand-select
  toggle oscillation (reproducer STS100007/ps0), documented at the CONFIRM
  tie-bias in policy_search.cpp. Named tests: `SimSearch.*` +
  `SimSearchScript.*` (seed_scan_test), the re-seated
  `FuzzPolicy.BossChestPreferenceIsScoredRatherThanLeftToTheTieBreak`
  (E0 loop bounded at SIM_SEARCH; the sim pair pinned on a staged
  RELIC_SELECT with REAL actions, because the no-op guard correctly
  demotes fake ones), and `test_script_policy.py` — 14 Python tests, the
  centerpiece round-tripping ≥400 recorded decisions from ≥8 committed
  corpus runs (`tests/golden/oracle_corpus/act1_a20_50.tar.gz`) through
  derived identity steps back to the exact captured commands, plus
  synthetic PROTOCOL §3.8 BOSS_REWARD states, divergence/exit-3/record
  tests, glue-rule cursor pins, and strict-config pins — game never
  launched (registered as `oracle_script_policy_python_test`). Six presets
  green; counts re-derived by `ctest -N | tail -1` at land time, not
  restated here.
  **2026-08-27 (later) — the Awakened One cell is DISCHARGED, and the
  shortfall above was a PROBE artifact, not a reach failure.** Report §6 is
  rewritten in place as the discharge account. `--need-victory` /
  `--need-boss-kill-act 3` is `run_is_victory`, which at A20 means BOTH
  Act-3 bosses; a line that kills the Awakened One and then dies to the
  second boss on the same HP pool is invisible to it. The exact witness for
  the FIRST Act-3 boss is `max_floor == 51` — the Act-3 map ends at floor 50
  (`act_floor_base(3) == 34` + 16) and floor 51 exists only as
  `goToDoubleBoss`'s synthetic node, entered through the `++floorNum` room
  transition off the boss room's proceed. Against that probe the UNMODIFIED
  `sim_search` kills the Awakened One **22 times in 1,929 dedicated fights**
  over 7 distinct seeds; **seven kill triples are scheduled in report §5**,
  each `--verify-determinism` clean and each script's own floor-50/51 play
  targets naming `AwakenedOne` then the second boss. The report's suggested
  fix was also built and then REJECTED on measurement: the Awakened-aware
  hold-Powers rule is `PolicyKind::SIM_SEARCH_HOLD` (value 7, appended) —
  while a live monster owns `PowerId::CURIOSITY` a POWER play costs
  `amount × SS_AMT(4) × 20 × 300` (CuriosityPower.java:42-47;
  AwakenedOne.java:146 for the A20 amount 2, :89 for SS_AMT, :302-308 for
  the Rebirth purge that switches the rule off) — and on the SAME
  110-seed × 1,024-policy-seed grid it scored **5 kills against
  `sim_search`'s 22**.
  *(Confirmed live 2026-08-27: S2.43's depth capture `s2v2_awk_105835`
  (STS105835, policy-seed 317) kills the Awakened One in the game and replays
  CLEAN through its death terminal to the second boss, which is the item-3
  Awakened-One kill witness in
  [verification/s243-dashboard.md](verification/s243-dashboard.md) — the
  scheduled cell is not merely schedulable but captured.)* The search's preview is an exact omniscient engine
  advance and Curiosity is native inside it, so the rollout already charges
  the tax and the rule double-counts it. Kept as its own kind so
  `sim_search` is provably untouched and the falsifying A/B is re-runnable.
  Campaign: **325,280 rows / 101.06M actions**, release preset. All nine §5
  Act-2 cohort triples replay to their exact committed `final_hash`. New
  tests: `SimSearchCuriosityHold.*` (fuzz_test, 4 cases) plus the extended
  `SimSearch.PolicyNamesRoundTripAndAreAppended`. Still unwon and reported
  as such: **0 victories in 3,858 Awakened One fights** across both arms —
  the A20 double-boss follow-on, not the Awakened One, is what is unbeaten.

### S2-G2 `[x]` **Gate: S2 verified (unblocks training Phase T4)** — tag `s2-g2-verified`
**Deps:** S2.41–S2.49, S2.V2, S2-G1
The design §6 S2-G2 bar, checked literally, every item with linked
evidence. Then: update CLAUDE.md "Current state"; notify the training
ledger (T4.1's `Deps: S2` is this tag); S3 planning opens as its own fresh
exercise (not claimed here) — **opened 2026-09-03 as
[s3-design.md](s3-design.md) + [s3-tasks.md](s3-tasks.md)**.
**Log:** 2026-08-27 — GATE TAKEN, tag `s2-g2-verified`. The bar's
item-by-item answer is
[verification/s2-verification.md](verification/s2-verification.md)
(S2.46): items 1–4 MET on the S2.43 dashboard's regeneration-stable
numbers (breadth 2,000 full-run attempts / 0 untriaged / 0 open; all
three Act-2 BOSS rows claim+pick+transition zero-diff with take AND skip
witnessed; all three Act-3 rows witnessed killed and THREE complete A20
double-boss victories over two first-boss identities, every capture
CLEAN; 31/40 event rows sighted + 9 exact dispositions, 0 OWED), item 5
MET (proactive audit: twenty-one families, one hundred one named
regressions, executable audit PASS), item 6 MET (S2.44's registered
tier-4 family, RESULT PASS), item 7 MET with its premise's falsification
recorded (S2.45: per-step and per-combat floors hold; the S3 baseline is
the runs/sec + run-steps/sec pair). The depth evidence was produced by
the S2.V2 sim-consulting scripted driver against the playtime-pinned
fork (pin `ABD95268…`), and the campaign's divergence harvest — eleven
engine/emitter root causes, six follower/driver seams, one fork
contract pin, all landed 2026-08-27 with named regressions — is the
gate's real product beside the bar. CLAUDE.md updated; T4.1's Dep
annotated in the training ledger. S3 planning NOT claimed here.

## Parallelism map

```
Wave 1 (now):  S2.01 ∥ S2.02 ∥ S2.03 ∥ S2.04
S2.01 ─▶ S2.12 ─▶ S2.13 ; S2.11 (∥ with S2.12)
S2.01 ─▶ S2.21 … S2.28 (batches ∥)
S2.02 + S2.13 ─▶ S2.31 ∥ S2.32 ∥ S2.33 ; S2.31 ─▶ S2.34
all S2.0x/1x/2x/3x ─▶ S2-G1
S2.12 ─▶ S2.42 ; S2.11+S2.12 ─▶ S2.41
S2-G1 ─▶ S2.47 ∥ S2.44 ∥ S2.45 ; S2.47 ─▶ S2.43 (needs S2.42)
S2.48 ∥ S2.49 (owner-directed 2026-08-10) ─▶ S2.43
S2.43(breadth) ─▶ S2.V2 ─▶ S2.43(depth cohorts + coverage join)
S2.43+S2.44 ─▶ S2.46
S2.41–S2.49 + S2.V2 ─▶ S2-G2
```

## Change log

- 2026-08-03 — ledger created by TE.2 with Phases S2.0–S2.4, gates
  S2-G1/S2-G2, Wave-1 id blocks, and the inherited-obligation rows;
  scope denominator is [s2-design.md](s2-design.md) v0.1.0.
- 2026-08-10 — orchestrator opened S2.47 (boss-relic offer storage), the
  planned `SCHEMA_VERSION` site the S2.42 deferral row demands; deferred
  row re-pointed, S2.43's Deps extended, gate Deps now S2.41–S2.47.
- 2026-08-10 — owner dispositions on the deviation inventory: the
  SecretPortal pin (s2-design §5 trap 5, ratification recorded there), the
  Neow mini-blessing absence, and Prismatic Shard's inert reward
  (implement at S4 with the other characters) are ratified as settled;
  emerald-key rewards assigned to S3 planning (new deferred-obligations
  row); **S2.48** (stolen-gold settlement ordering) and **S2.49**
  (attacker-side multi-hit cancel) opened as owner-directed behavior
  fixes, both Deps of S2.43; gate Deps extended to S2.41–S2.49.
- 2026-08-27 — orchestrator took the §8 escalation decision on S2.43's
  measured breadth numbers (0 Act-2 boss fights / 2,000 attempts; E0
  sim reach structurally 0 past Act 1): **S2.V2 opened** as the design
  §6 driver-risk paragraph's sanctioned sim-consulting driver, with the
  scan-emitted scripted-line architecture item 3 already sanctions; gate
  Deps extended to include S2.V2.
