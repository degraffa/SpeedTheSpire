# Stage B Task Ledger

Execution tracker for [stage-b-design.md](stage-b-design.md) (the frozen Stage
B spec — **this file never overrides it**; on any conflict, the design doc wins
and this file gets fixed). [stage-a-design.md](stage-a-design.md) remains
frozen and in force for everything it covers. Target milestones: **M2** (oracle
bridge live, gate G4), **M3** (S1 rules complete, gate G6), **M4** (S1
verified, gate G7) — InitialPlan's milestone table, S1 = Ironclad / Act 1 /
A20.

**This file holds only what is still open.** Completed task blocks live
verbatim in [stage-b-log.md](stage-b-log.md) and appear here as one-line index
entries. The rules every task obeys live once in
[conventions.md](conventions.md). Picking up an open task means reading
`conventions.md` + this file's entry — not the archive.

## Orchestrator protocol

- Statuses: `[ ]` todo · `[~]` in progress · `[x]` done · `[!]` blocked.
  Update the checkbox and the **Log** line of a task when its state changes.
  When a task lands, its full block moves to
  [stage-b-log.md](stage-b-log.md) under an `<a id="…">` anchor and is
  replaced here by a one-line index entry; any forward-looking obligation its
  Log leaves behind goes into **Deferred obligations** below **and** into the
  owning task's `**Inherited:**` line — never only into the archived Log.
- A task is **done only when its Acceptance block passes** — run the commands,
  don't infer. Tests land in the same change as the code they verify.
  Registry YAML is code: entries land with their tier-2 tests in one commit.
- Respect `Deps:`. Tasks with disjoint deliverables and satisfied deps may run
  in parallel (parallel-safe groups are marked ∥).
- **Gates** (G4–G7, continuing Stage A's numbering) are stop-the-line: nothing
  past a gate starts until the gate task is `[x]`. Phase B3/B4 content work
  additionally requires **both** G4 and G5 (design §3.2: bridge first, then
  registry migration, then mass content).
- Build/test commands, provenance rules, the precedence chain, git discipline
  and hygiene: [conventions.md](conventions.md). Nothing in this file
  overrides it.
- Layout: engine as in Stage A; registry sources `registry/*.yaml`; codegen
  `tools/registry_gen/`; bridge `tools/oracle_bridge/` (fork source under
  `communicationmod-oracle/`, driver under `driver/`, translator under
  `translator/`); distributional tests `tools/dist_check/`; reports
  `tools/verify_report/` + `docs/verification/`.

## Working agreements

Moved, in full, to **[conventions.md](conventions.md)** — the single
authoritative copy of statuses, git discipline, the canonical reference
reading order, the precedence chain and its live-override evidence bar,
hygiene, and the build commands. It is binding on every task in this ledger.
The only ledger-local specifics are the protocol bullets above.

## Deferred obligations

Forward-looking obligations that completed tasks recorded in their Logs and
handed to a future task. **This table is the live carrier** — the archived
Logs are not read during normal execution, so an obligation that is not here
is invisible. When you land a task, discharge every row that names it (or
re-own the row explicitly) **by marking it DISCHARGED in place, in the same
commit** — discharged rows stay in the table as its historical record. (This
sentence used to say "delete the row"; observed practice across every
discharge since B4.x has been to mark in place, which is strictly better —
a deleted row erases the evidence that an obligation was ever tracked — so
the header now blesses the practice rather than contradicting it. Settled at
the Wave-C integration, which the stage-2 report asked to reconcile the two.)

Owners marked `[x]` are tasks that have **already landed without recording a
discharge** — they need re-owning by the orchestrator, not silent closure.
`UNASSIGNED` means the deferring Log named no owner at all.

| Obligation | Deferred by | Owner task | Detail |
|---|---|---|---|
| Meal Ticket `justEnteredRoom` shop heal, including ?→Shop | B4.10 | B4.8 `[x]` | **DISCHARGED by B4.8.** `dispatch_just_entered_room_relics` (`shop.hpp`) is the shared entry effect, called from `on_player_entry` for every non-Event room and, for a ?, only AFTER the roll has replaced the room — which is where the game calls it (`AbstractDungeon.java:1763-1789`), so a ?→Shop and a static ShopRoom are the same room by then. Both paths are named tests (`ShopFlow.MealTicketHealsOnAStaticShopRoomEntry`, `ShopFlow.MealTicketAlsoHealsWhenAQuestionMarkResolvesToAShop`). The heal is out of combat, so Magic Flower's `onPlayerHeal` (combat-only, `MagicFlower.java:31-37`) cannot scale it and the fan-out is named rather than written, as `rest_apply_heal` already does |
| Maw Bank `onEnterRoom` outside original EventRooms | B4.10 | wave-runlayer S2 | **DISCHARGED by Wave-C track 2 stage 2.** `dispatch_event_room_entry_relics` is generalised into `dispatch_on_enter_room_relics(RunState&, RoomType)` (`event_framework.hpp`), called ONCE per transition from `on_player_entry` — so every static monster / elite / rest / shop / treasure entry pays 12 through the Ectoplasm-aware `gain_gold` door, and so does the BOSS entry (`DungeonMap.java:77-87` assigns `nextRoom` a MonsterRoomBoss and runs the same `nextRoomTransition`, whose relic loop at `AbstractDungeon.java:1755-1757` has no room-type guard beyond `nextRoom != null`). **`onEnterRoom` is NOT `justEnteredRoom`**: it fires pre-`?`-roll and pre-`setCurrMapNode`, exactly once, so the run layer's `?` recursion re-enters through `on_player_entry_impl(..., fire_on_enter_room=false)`. A `bool` parameter, not a `room != Event` test — that predicate is the one the recursion breaks, and wiring the hook at the `justEnteredRoom` site would pay a `?`→Shop twice (proven RED before the fix). Ssserpent Head keeps its `room instanceof EventRoom` gate (`SsserpentHead.java:29-35`) now that the fan-out is room-aware; `RoomType::None` is the Java's `nextRoom == null` and fires nothing |
| In-combat card-CHOOSE potion bodies: Elixir, Attack/Skill/Power/Colorless Potion, Gambler's Brew, Liquid Memories | B3.23 | B3.4 `[x]` | B3.23 named B3.4 as the verb owner; B3.4 landed CHOOSE-in-combat but recorded no potion un-deferral. **PARTIALLY DISCHARGED** on `discharge` (Blessing of the Forge, commit `d710d50`) — it needed only the already-live `CHOOSE_CARD{upgrade}` kind, so it left this row; the potions still listed above need real hand-select-screen / `MAKE_CARD` work and stay open. As of the same branch they are also **fail-loud**: `potion_use_implemented` keeps every still-deferred potion off the legal-action mask (the potion-legality commit on `discharge`, immediately following `d710d50`) instead of letting a USE silently burn the slot. **FURTHER DISCHARGED by `wave-combat` (Track 1 stage 4), and the row's premise was wrong for six of its seven potions — none of them needed `MAKE_CARD` work.** Elixir needed no new machinery at all: it is Purity's `CHOOSE_CARD{exhaust, optional: true}` with `ExhaustAction`'s literal amount 99 (`ExhaustAction.java:56-58`). The four discovery potions needed none either — only a pool selector and a copy count, carried in the already-live `DISCOVERY` item's unused `src`/`tgt` bytes, plus the generated `kIroncladPowerPool`, which was the one thing genuinely missing. Liquid Memories and Gambler's Brew each did need one new `ChoiceKind` — `DISCARD_TO_HAND_FREE` (11) and `HAND_TO_DISCARD_THEN_DRAW` (12), the latter shared with the Gambling Chip relic. **THE ROW IS NOW FULLY DISCHARGED**: all seven potions have bodies and the in-combat card-CHOOSE deferral group is empty |
| `DiscoveryAction` may regenerate its three-card offer on EVERY update tick | `wave-combat` (Track 1 stage 4) | **DISCHARGED** — fix-discovery-duplication (082a1b4), 2026-07-28. **RESOLVED BY CAPTURE — it does, 1 + 5 times.** Seven witnesses across both g6 campaigns (cardRandomRng prev/open/close): STS00220 s61 pick 0/3/19, STS00221 s53 skip 0/4/19, STS00425 s38 pick 0/3/19, STS00610 s104 skip 0/3/18, STS01372 s39 pick 0/3/19, STS01861 s31 skip 0/3/18 and s90 skip 0/3/19 (a duplicate retry INSIDE a wasted regeneration). Derivation: `tickDuration` subtracts `getDeltaTime()` (`AbstractGameAction.java:74-79`), pinned at STEP = 0.043 by the fork's `stripAnimationCollapse`; the action is frozen while the screen is up; from `ACTION_DUR_FAST = 0.25` the close runs exactly five post-open ticks. Model: open-tick latch kept; `advance()`'s CHOOSE dispatch burns `kDiscoveryWastedRegens = 5` full rejection-sampled regenerations on pick AND skip, Java order. In an unpatched client the count is frame-rate-dependent — recorded on the constant (`interp.hpp`) as an oracle-contract model boundary, the Explosive-Potion THORNS precedent. Same change, same class (a): the TYPED discovery screen is SKIPPABLE (`customCombatOpen(..., cardType != null)`, `DiscoveryAction.java:49` / `CardRewardScreen.java:485-500`) — `ActionMask.can_skip_choice` + `CHOOSE(kChooseSkipCard)` consumes the item, creates nothing, refunds nothing; the Discovery card and Colorless Potion stay non-skippable. STS00221's seq-52 soft-lock gone: 72 → 105 records to run terminal, both campaigns | `DiscoveryAction.update` computes `generatedCards` at **`DiscoveryAction.java:47`, outside the duration branch**, and only the FIRST tick's list reaches the screen (`:49`). If `update` runs again before `retrieveCard`, `generateCardChoices` runs again and its `cardRandomRng` draws are spent for nothing — a real, observable stream cost. Whether it happens, and how many times, depends on how many frames the action is updated for, which is animation-driven and **cannot be settled from the source**. Modeled as generate-once-and-latch (`discovery_choice_prepared`, `interp.hpp`), which is what the already-landed Discovery CARD does — so if the game really double-rolls, **the Discovery card is wrong too, not just the four potions**, and this is a stop-the-line question rather than a potion-only one. Needs an oracle capture: G4 is live. Sites: `prepare_discovery_choice` (`src/engine/interp/interp_cards.cpp`), where the ambiguity is also noted inline |
| Mummified Hand onUseCard POWER → cardRandomRng 0-cost | B3.25 | B3.7 `[x]` | no POWER CardType at B3.25 time; B3.7 landed the POWER cards but recorded no discharge. **DISCHARGED** on `discharge`, commit `add41ed` — implemented with the cardQueue exclusion and the just-played-card exclusion, no draw on the empty-candidate path |
| Frozen Egg's POWER-card upgrade-on-obtain branch (documented inert) | B3.25 | B3.7 `[x]` | same cause, same gap. **DISCHARGED** on `discharge`, commit `dc6f626` |
| Stolen-gold clamp vs in-combat gold ordering | B3.11 | UNASSIGNED — B5.2 verification, or whoever models mid-combat gold timing | `fold_back_combat` settles the Hand of Greed accumulator through `gain_gold` before `settle_stolen_gold` runs, so a Looter's min(total, purse) clamp reads a purse that already contains greed gold. Diverges from the game only when the steal preceded the greed kill AND the purse was below the accrued steal (a Looter and a Hand of Greed kill in one combat, purse ≤ the steal amount); documented at the `settle_stolen_gold` site. Deliberately chosen to preserve exactly-once settlement on every combat-end path. **DECISION 2026-07-28 (orchestrator, on a scoped options brief): the deviation STANDS.** The faithful fix is a steal-time signed purse delta, which is a `CombatState` layout change → schema bump → all 20 fixtures regenerated → owner approval, and must preserve exactly-once settlement on four end paths — not a wave-sized task; the tempting cheap reorder (settle before fold-back) is a trap because the live class-(c) evidence (STS00462's mid-combat `gold: 110→130`) is a DURING-combat read no end-of-combat reordering can touch. Re-scope the faithful fix as its own owner-approved task alongside the Act-2 Mugger work the site already flags. **Two stale comments must be fixed by that owner** (or any earlier passer-by): `monster_looter.hpp:50` and the `settle_stolen_gold` header both still claim "CombatState carries no gold field" — `CombatState.combat_gold` has existed since schema 6 (`combat_state.hpp`, `schema.hpp`). **The two stale comments are FIXED (wave2-engine stage 3b, 2026-07-28, comments only):** both sites now say `combat_gold` exists but is the Hand-of-Greed GAIN accumulator, not a purse mirror, and both point at this row's standing DECISION — which this pass deliberately did NOT reopen; the faithful fix remains re-scoped as above |
| **Dead Branch** `onExhaust` | B3.26 | `wave-combat` (Track 1 stage 3) | **DISCHARGED, and the blocker did not exist.** Citation corrected to **`DeadBranch.java:24-31`** (the file is 43 lines; the row carried `:259-266`). The row said the draw is *unfiltered* over the whole colour pool and therefore needs a **second generated combat pool**. It is not: `returnTrulyRandomCardInCombat()` (`AbstractDungeon.java:938-962`) concatenates srcCommon + srcUncommon + srcRare **excluding `CardTags.HEALING`** and takes one `cardRandomRng.random(list.size() - 1)` -- which is exactly the existing generated 70-row `kIroncladCombatPool` that Discovery already consumes. No new pool, no new opcode: `Opcode::MAKE_CARD` (9) into `CardPile::HAND` is the action, hand-cap spill included. (The genuinely-new pool is Pandora's Box's `returnTrulyRandomCard()`, which does NOT exclude HEALING -- 72 rows, still unbuilt.) **The id is drawn at QUEUE time**, because `returnTrulyRandomCardInCombat()` is an ARGUMENT of the `MakeTempCardInHandAction` constructor, so the pick is spent while the exhaust fan-out runs -- the Magnetism shape, and rolling it at resolve instead would move the stream. Gated on `areMonstersBasicallyDead` (`MonsterGroup.java:90-95`), i.e. `monster_dead_or_escaped`, which is tested for BOTH the all-dead and the escaped case |
| **Gambling Chip** `atTurnStartPostDraw` | B3.26 | `wave-combat` (Track 1 stage 4) | **DISCHARGED, together with Gambler's Brew, because they are one body.** Citation corrected: the file is **48 lines**, not the `:426-453` range this row carried — `GamblingChip.java:18` is the private `activated` field, `:30-32` is `atBattleStartPreDraw` clearing it, `:34-42` is the hook. **The row was also wrong about the count**: it said "the count is the relic's, not the choice's". `GamblingChip.atTurnStartPostDraw` queues `new GamblingChipAction(AbstractDungeon.player)` and `GamblersBrew.use` queues `new GamblingChipAction(player, true)` — the SAME action, and `notChip` selects only a prompt string (`GamblingChipAction.java:42-46`). The count is `selectedCards.group.size()` read at confirm for **both**. Landed as `ChoiceKind::HAND_TO_DISCARD_THEN_DRAW` (**12**) behind one shared builder, `queue_gambling_chip_choice`, that both consumers call — a test asserts the two queued items are byte-identical, so a future fork is caught. **ONCE PER COMBAT, not per turn**: the `activated` latch is the relic, carried in `RelicSlot.counter` (the −1 unset default IS "not activated"). The real per-consumer differences are at the CALL sites: Gambler's Brew has an empty-hand guard on the potion (`GamblersBrew.java:38`) and the relic has none. Two Java calls have no S1 counterpart and are named at the ChoiceKind rather than invented: `c.triggerOnManualDiscard()` (binders are Silent's Reflex/Tactician and Watcher's Sands of Time) and `GameActionManager.incrementDiscard(false)` (nothing reads a discard-this-turn counter; no such field exists) |
| **Sling of Courage** `atBattleStart` | B3.26 | `wave-combat` (Track 1 stage 3) | **DISCHARGED.** `Sling.atBattleStart` (**`Sling.java:30-37`** -- the file is 44 lines; this row and `registry/relics.yaml` both carried `:1030-1038`, off by exactly 1000, and both are corrected) grants Strength 2 when `getCurrRoom().eliteTrigger` is set. The marker is now `kCombatFlagEliteRoom` (`combat_state.hpp`, CombatState.flags bit 20), produced by `enter_combat` for `RoomType::Elite` and by the Dead Adventurer event (`DeadAdventurer.java:116`) through a new defaulted `elite_trigger` argument on `enter_event_combat` -- the event path is a real Act-1 producer and a room-type test alone would have missed it. Storage cost is ZERO: bit 20 was previously zero, so no offset, no `sizeof`, no `SCHEMA_VERSION`, and `enter_combat`'s fresh `CombatState s{}` is the per-combat reset (the Centennial Puzzle precedent). **This row was NOT the twin of the Slaver's Collar row below:** `MonsterRoomBoss` never sets `eliteTrigger` (`MonsterRoomBoss.java:22-24`), so a boss room does not fire Sling, and the collar ORs an `EnemyType.BOSS` scan on top of the same flag. A standalone `combat_begin` combat has no room and leaves the bit clear |
| **Orange Pellets** `onUseCard` | B3.26 | `wave-combat` (Track 1 stage 3) | **DISCHARGED.** Citation corrected: the file is **65 lines** -- `atTurnStart` **:34-39**, `onUseCard` **:41-58**; this row and `relics.yaml` both carried `:1218-1250`, which does not exist. **The row's claim that "the three latches and their `at_turn_start` clear are live" was FALSE** -- `relic_native_orange_pellets` was a completely empty body, so all of it landed here: three `CombatState.flags` latches (bits **21-23**), the `atTurnStart` clear, and `Opcode::REMOVE_DEBUFFS` (**63**). The latches are flags bits and not `RelicSlot.counter` because they are `private static` in the Java (combat-global, the Centennial Puzzle shape) and the counter is oracle-visible at -1. Two behaviours the row did not mention and that are now tested: it **fires more than once per turn** (the latches are cleared ON FIRE, so three more cards re-arm it), and the game does **not** clear them at `atPreBattle` -- only `atTurnStart` does, so a value-initialised `CombatState` is equivalent only because turn 1's `atTurnStart` precedes any card play (`AbstractRoom.java:253`). `REMOVE_DEBUFFS` enumerates at RESOLVE time (`RemoveDebuffsAction.java:23-30`) with the **live-instance** DEBUFF predicate, so a negative Strength stack is removed and a positive one is not (`StrengthPower.java:81-89`), and Shackled goes with its pending restoration (`GainStrengthPower.java:29`) -- reproduced, not corrected. `addToTop` per power gives reverse power-list removal order |
| Ten `energyMaster` relics (Fusion Hammer, Velvet Choker, Runic Dome, Cursed Key, Busted Crown, Ectoplasm, Sozu, Philosopher's Stone, Coffee Dripper, Mark of Pain) **and Snecko Eye's `masterHandSize += 2`** | B3.27 | `wave-combat` (Track 1 stage 2) | **DISCHARGED.** Both numbers are DERIVED, not stored: `energy_master(const CombatState&)` and `game_hand_size(const CombatState&)` (`action_queue.hpp` / `.cpp`) scan the combat relic mirror, and the recharge/draw lines inside `start_of_turn` are their only consumers. No `CombatState` field, no `SCHEMA_VERSION` bump, no fixture regeneration. Deriving is faithful rather than a shortcut: the game reads each field exactly once per combat (`EnergyManager.prep`, `EnergyManager.java:20-23`; `preBattlePrep`, `AbstractPlayer.java:1579`) and the relic mirror is likewise written once at combat construction, so a per-turn scan of it IS that snapshot. **The row's stated rationale was wrong and is corrected rather than repeated:** `++energyMaster` and `masterHandSize += 2` draw no RNG at all, so no partial could have desynced `miscRng` or `relicRng`; and the batch was never deferred *whole* — eight of the eleven relics already had live, tested halves. Ice Cream composes with the master, not the base (`EnergyManager.java:31` adds `this.energy`, prep()'s copy), and Snecko Eye's +2 reaches the opening hand as well as every later turn. **Slaver's Collar is NOT discharged** — see its own row; `energy_master` is the single site it attaches to |
| `dispatch_relics_at_pre_battle` at the **run** entry (`run_advance.cpp` `enter_combat`) | B3.27 | `wave-combat` (Track 1 stage 2) | **DISCHARGED.** `enter_combat` step (8b), between the relic-mirror copy and `begin_first_turn` — the slot is forced from both sides (`player_relics` reads the mirror; `atPreBattle` must precede the opening `DrawCardAction`), and it is the identical call `combat_begin` makes, so the two combat-construction paths cannot drift. Snecko Eye is the only registered relic binding the hook, and `NoRegisteredRelicBindsThePreDrawHook`'s sibling assertion keeps that visible. **RNG-visible from here on:** every run-layer combat with Snecko Eye now spends one `cardRandomRng` `random(3)` per drawn card with `cost >= 0`, starting with the opening hand — which is what `atPreBattle` exists to cause. No combat fixture holds a relic, so `tests/golden/combat_fixtures/*.trace` are unmoved; soak/campaign coverage identities and recorded oracle replays that hold Snecko Eye will move |
| Slaver's Collar `beforeEnergyPrep` | B3.27 | `wave-combat` (Track 1 stage 3) | **DISCHARGED, and DERIVED -- the row's own premise was wrong.** `SlaversCollar.beforeEnergyPrep` (`SlaversCollar.java:46-57`), called by name from `AbstractPlayer.preBattlePrep` (`:1589-1590`), `++energyMaster` when `eliteTrigger` is set **or** any monster is `EnemyType.BOSS`; `onVictory` (`:59-65`) undoes it while `pulse`. It lands inside `energy_master(const CombatState&)` (`action_queue.cpp`) -- the stage-2 seam -- as `kCombatFlagEliteRoom || any MonsterDef::is_boss()`, with **no stored state at all**. **The claimed leak does not exist:** this row (and the stage-2 seam note repeating it) said a fled or lost combat carries the increment forward because `onVictory` fires only on victory. Reading the escape path shows otherwise -- `beginLongPulse` sets `pulse = true` (`AbstractRelic.java:849-852`), a Smoke Bomb's only exit is `AbstractPlayer.updateEscapeAnimation` -> `getCurrRoom().endBattle()` (`AbstractPlayer.java:2281-2292`), and `AbstractRoom.endBattle` calls `player.onVictory()` unconditionally (`AbstractRoom.java:413-421`). The one path that skips the `--` is `AbstractPlayer.onVictory`'s `if (!this.isDying)` guard (`:1951-1964`), i.e. player death, which ends the run. So the between-combat delta is provably 0 and a derivation is exact, not approximate. The row stays a pure MARKER row (no `native:`, no hooks), as `MarkerRowsCarryNoCombatHooks` asserts |
| Warped Tongs | B3.27 | `wave-combat` (Track 1 stage 3) | **DISCHARGED** as `Opcode::UPGRADE_RANDOM_CARD` (**64**). Citation corrected: `WarpedTongs.atTurnStartPostDraw` is **`WarpedTongs.java:28-33`** (the file is 40 lines; the row carried `:24-29`). The row's reasoning was right and is now stronger: `UpgradeRandomCardAction.update` (`:28-50`) spends **one `shuffleRng.randomLong()`** seeding a JDK Fisher-Yates over the **pre-filtered** hand subset (the no-arg `CardGroup.shuffle`, `CardGroup.java:561-563`), where `CHOOSE_CARD{RANDOM, UPGRADE}` draws `cardRandomRng` once over the WHOLE hand -- different stream, different set. **The load-bearing stream fact is the ZERO-draw paths:** an empty hand returns at `:31-34` and the shuffle sits INSIDE `if (upgradeable.size() > 0)` at `:40-45`, so a hand with nothing upgradeable costs nothing; both branches are pinned. The filter is `canUpgrade()`, not `upgrade == 0` -- `SearingBlow.canUpgrade` returns true unconditionally (`SearingBlow.java:58-60`) -- and reuses the shared `can_upgrade_instance` so Armaments/Apotheosis/this cannot drift. The subset is built in HAND ORDER (`CardGroup.addToTop` is an append, `:455-457`). Reachable in S1 today: tier SPECIAL, sourced only by the live Accursed Blacksmith shrine. `RelicBossSpecial.DeferredNativeBodiesQueueNothingAndTouchNoRng` lost its Warped Tongs case, which is what failed first |
| Sacred Bark potency at EVENT acquisition/use sites | B3.27 | UNASSIGNED — no remaining S1 event site; re-own if an Act-2/3 event batch creates one | **The five BOSS `onEquip` bodies (Pandora's Box / Tiny House / Astrolabe / Empty Cage / Calling Bell) formerly sharing this row are DISCHARGED by Wave-C track 2 (`wave-runlayer` S1):** all five are live `pickup: on_equip_screen` bodies (relics/relic_pickup_boss.cpp; the RelicEquipContext design is documented at relic_pools.hpp), reachable via Neow's boss swap, with the plain `acquire_relic` refusing them loudly (`NEEDS_EQUIP_CONTEXT`) at any site that cannot present screens. Note the row's original framing carried two errors, corrected at the registry rows in the same change: Pandora's Box needs the UNFILTERED 72-row `returnTrulyRandomCard` pool (`kIroncladTrulyRandomPool`), NOT "the same generated all-red combat pool Dead Branch is waiting on" (that one — HEALING-filtered, 70 rows — already existed as `kIroncladCombatPool`); and Calling Bell's three draws are pool-consuming but relicRng-counter-NEUTRAL, not "relicRng-consuming". **Sacred Bark stays DEFERRED and is demonstrably out of S1 event scope:** Lab and The Woman in Blue are the only S1 events that hand out potions, and both do it as reward-screen rows claimed through `claim_reward`, which grants a potion *identity* into a slot and never reads potency — no event USES a potion, so no potency site exists. (The full B4.11/B4.12/B4.13 event-site discharge history this row used to carry is archived in those tasks' Logs, stage-b-log.md.) |
| `colorlessCardPool` is shuffled IN PLACE by `returnColorlessCard` | B4.13 | UNASSIGNED — but the named consumer turned out not to be one | `AbstractDungeon.returnColorlessCard(rarity)` (`AbstractDungeon.java:1100-1113`) JDK-shuffles the persistent `colorlessCardPool.group` before picking, so the new ORDER survives into the next reader of that list. Match and Keep is the only Act-1 caller and the port shuffles a local copy. **B4.8 corrects this row's forward-looking half:** it named "a shop with colorless slots" as the consumer that would observe the persisted order, and the shop now exists and does **not**. `getColorlessCardFromPool` reaches `CardGroup.getRandomCard(true, rarity)` (`CardGroup.java:509-524`), which filters the group into a local `tmp` and **`Collections.sort`s it** before indexing, so the source order is discarded on every read. Nothing in Act 1 observes the persisted order at all — `transformCard`'s COLORLESS branch reads the untouched `srcColorlessCardPool` (`:998-1014`). The row stays open only against a future reader of the UNSORTED whole-pool view. |
| Fusion Hammer / Coffee Dripper campfire-option locks | B3.27, B4.9 | `wave-combat` (Track 1 stage 2) | **DISCHARGED**, in the same wave as the shared `energyMaster` half B4.9 was waiting on. `build_rest_menu` now runs the real veto sweep (`CampfireUI.initializeButtons`, `CampfireUI.java:87-93`): the whole button list is built first, then every button is offered to every relic in acquisition order and the first refusal clears its `usable`. Sweeping the full list — not just the two base buttons — is the Java's shape and is what keeps a future refusing relic correct. The relics' own `updateUsability(false)` calls (`SmithOption.java:24-27`, `RestOption.java:43-48`) are **cosmetic** and are deliberately not modelled as the disable. **The consequence nobody had modelled is also landed:** when no button is usable the game completes the room on the spot (`CampfireUI.java:97-104`, `waitTimer = 0`, `phase = COMPLETE`), and that is reachable in S1 with a *single* relic — Coffee Dripper plus a deck with nothing upgradeable — so rest-room entry checks it and returns to `MAP_CHOICE` rather than parking the run on a menu with no legal action. `RecallOption` is appended after the sweep and is Act-4-gated, so it is absent rather than modelled as always-off |
| Philosopher's Stone `onSpawnMonster` | B3.27 | `wave-combat` (Track 1 stage 2) | **DISCHARGED.** The row's Detail cell was empty and its named owner already existed — `Opcode::SPAWN_MONSTER` and the three Exordium splits have been landed since B3.27. A hand-written fan-out inside `spawn_monster_at_slot` (`monster_dispatch.cpp`) applies `+1 Strength` to the spawned monster directly, not queued, exactly as the relic's `atBattleStart` sibling does — `PhilosopherStone.onSpawnMonster` (`PhilosopherStone.java:50-54`) is a synchronous `addPower`. **Not** a registry `RelicHook`: this relic is the game's only `onSpawnMonster` implementor (`AbstractRelic.java:498` is the empty base), so a new hook id would move `kRelicHookCount` for one row; Velvet Choker's bespoke `canPlay` veto is the precedent. It runs *after* the child's init rather than before it as `SpawnMonsterAction.java:44-50` does — forced (the monster record does not exist earlier, and the spawn-at-hp init zeroes `power_count`) and equivalent (`rollMove` does not read Strength, so the `aiRng` draw is unchanged, and this engine bakes no telegraphed damage: monster damage is computed when the move's effects are queued). Zero namespace spend |
| Purged replay copies leak a card-pool row | B3.8 | **RE-ASSESSED and CLOSED as the deliberate model** — wave2-engine stage 3a, 2026-07-28 | The leak stays, but for CURRENT reasons, not the recorded one. **The recorded race is no longer constructible**: every `DAMAGE_RAMPAGE` is queued by `resolve_card_play`'s program pass, strictly BEFORE that same card's filing `USE_CARD` in the one FIFO action ring (`card_play.cpp` queues the program then the filing, in order), and both replay verbs create their copy SYNCHRONOUSLY inside the onUseCard fan-out (`power_double_tap.cpp` / `power_duplication.cpp` call `op_play_card` directly — nothing queues a pool-index-carrying `PLAY_CARD` item: Mayhem and Distilled Chaos queue the FromDrawTop form with `amount = 0`). So freeing at the purge filing could not orphan a queued stamp — B3.11's value-keyed queued reduce/remove (the POWER-slot compaction analog) never applied here at all. **Why the leak is still the right trade**: freeing buys nothing observable — the game's purged `AbstractCard` simply becomes unreferenced by every pile, which is exactly what an occupied-but-pile-less row models — while making later free-slot scans reuse rows, a byte-visible (CombatState), play-invisible churn; and the bound is structural: one row per replayed play, replays of replays refused by the Java's own `!card.purgeOnUse` gate (DuplicationPower.java:40 / DoubleTapPower.java:44), pool exhaustion a defensive no-op (`op_play_card` slot<0 return). Pinned as documentation by `CardLimbo.PurgedReplayCopyLeaksItsStampedPoolRowByDesign`, whose stamped-`misc` assertion is the in-test proof the stamp resolves before the filing |
| Windows CI job | build effort | UNASSIGNED | a proposed workflow exists but is **unverified** (Actions cannot run locally). **Pin the LLVM version**: the googletest `/WX-` workaround exists because clang 22 added a warning gtest trips over, and a newer runner clang could add another |
| `replay` generalized to seed a sim replay from any translated `RunState` | B1.6 | UNASSIGNED — narrowed to ONE thing by the potion-belt/grid-buffer fix | **NARROWED TO THE MID-RUN RESUME, AND NOTHING ELSE.** `tools/oracle_bridge/replay/replay_run_diff` (B4.5) does the rest: its default mode seeds the engine from a translated `RunState` and re-drives one reward screen from there, and its `--replay` mode re-drives a whole captured run from `run_begin` with a screen-driven `action_command` mapping, diffing every record. The **room-coverage half of this row is closed** — see [Landed non-task work](#landed-non-task-work). The grid buffering `--neow` and `--shop` had is now shared code in `command_map.hpp` and `--replay` uses it, so `cancel` is a mapped command; the out-of-combat `potion discard` gained a real run-layer door (`ActionVerb::DISCARD_POTION` + `can_discard_potion[]`), so it is no longer the command with no analogue; and a capture that drives a grid the sim never opened now stops with the DEFERRED BODY named instead of an index complaint. Across all eleven b45 runs (both campaigns; campaign 1 triaged at [`b45c1_replay_triage.md`](../tools/oracle_bridge/driver/b45c1_replay_triage.md)) `--replay` now leaves **no stop attributable to the harness**: STS00044/47/48/49/50 reach their terminals `CLEAN`, STS00051 replays `CLEAN` too since the `kEventTransformRedPool` order fix, and every remaining stop named a documented deferred body. **Re-checked 2026-07-28 on `wave-combat` after the relic-tail stage:** STS00043 is now **CLEAN** to its terminal (67 records, zero divergence — it previously ran to terminal *while* diverging from seq 15), and STS00042 is **zero-diff over 38 records** (up from 33, first divergence gone) but now stops at seq 37 on the FIRST class-(b) harness gap either b45 campaign has produced: `--replay`'s command map has no `SHOP_ROOM` arm, although the run layer models shops (`RunPhase::SHOP`, B4.8 — which drove them through the separate `--shop` spot-diff mode instead). **THE MAPPING ARM IS DISCHARGED (`wave2-harness` stage 2).** `command_map.hpp` gained a `SHOP_ROOM` branch (merchant click = NOOP; `proceed` = the menu's `kChooseProceed`; a repeat once the sim has left = UI bounce, phase-discriminated exactly as the EVENT branch's exit page is) and a `SHOP_SCREEN` branch (`leave` = NOOP; `choose i` through the capture's `choice_list` and then through IDENTITY to the sim's unsold slot -- NOT the capture's array position, which drifts the moment the game deletes a bought row while the run layer keeps fixed slots). `resolve_shop_choice` moved out of `main.cpp` so `--shop` and `--replay` share it. STS00042 goes from 38 records / stop seq 37 to **CLEAN at its terminal, 85 records**; no `SHOP_ROOM` stop remains in either corpus, and the G6 main campaign goes from 4 clean to 18 clean of 30 with STS01221 replaying 200 records to its boss-reward terminal. Verdicts and the full triage of what the arm exposed (a pre-existing rest-site `proceed` bounce, fixed; `RecallOption` as the new class-(c) frontier; two sim-side (a)-candidates reported and NOT patched) are in [`wavec_track2_replay_triage.md`](../tools/oracle_bridge/driver/wavec_track2_replay_triage.md). The MID-RUN RESUME half of this row is untouched and remains open. STS00045/46 Empty Cage and STS00052 Astrolabe still stop on the boss `onEquip` row. **What remains of this row is the general mid-run resume alone**: restoring a `RunController` from an ARBITRARY translated `RunState` without re-driving the prefix. The run layer still has no door for it — map cursors, encounter lists and their cursors are transient and would have to be re-derived — which is also why `--shop` drives `ShopState` directly rather than parking a controller in `RunPhase::SHOP`. Folding the three spot-diff modes into `--replay` is no longer part of the gap: they now share the mapping table and its grid session, and differ only in what they seed from |
| Archived soak kv summaries predating the `victories` counter no longer parse | fix-postboss-shop | UNASSIGNED — next soak-tooling owner | `coverage_from_kv` is strict about the field set, so kv summaries written by pre-6d7efc4 `fuzz_soak` binaries fail to parse (missing `victories`) — loud by design, but any tooling that re-ingests ARCHIVED campaign summaries (e.g. `--merge` across old shards, B5.4's report join) must regenerate them from the runs instead. **DISCHARGED by `wave2-harness` stage 3.** Regenerating an archived summary from the runs means re-running the whole campaign it summarises, so the recorded resolution's second half is what landed: an EXPLICIT, opt-in legacy read. `coverage_from_kv_legacy` accepts a summary missing only keys in `legacy_optional_kv_keys()` (today: `victories` alone), defaults them to 0 and REPORTS which; `fuzz_soak --merge --allow-legacy-summaries` routes through it and is loud in TWO places, because either can be lost — a per-file `LEGACY SUMMARY … NOT measured` line on stderr, and a `provenance: REGENERATED FROM ARCHIVE` line in the report itself, which is the part that gets pasted into a log. The strict read is unchanged and still rejects a missing counter, and a key outside the vintage list stays fatal with or without the flag (both pinned by `FuzzCoverage` tests). The `fuzz_repro` exit-code caution is written up under `tools/fuzz/README.md` § "Exit codes — the one that reads backwards", beside the triage commands a CI job would wire. Related caution, same branch: `fuzz_repro` exits 1 on NOT-REPRODUCED — which is the PASS verdict on a fixed build — so any CI wiring that equates exit 0 with success will misread it |
| Run-level relic tests seed counters by hand, not from the registry | fix-centennial-counter's read-out | **DISCHARGED** — wave2-engine stage 3c, 2026-07-28 | `set_run_relics` now seeds each slot from the registry row's `initial_counter` (the same read `acquire_relic` performs, `relic_pools.cpp` `slot.counter = def->initial_counter`), with an in-helper ASSERT on an unknown id. The row's warning was right and the perturbation was found by running, not mass-editing: exactly ONE expectation silently relied on the hardcoded 0 — `QuestionMarkRoom.MawBankPaysExactlyTwelveOnceAcrossEveryResolvedKind` asserted `counter == 0` after entry, where the acquisition-correct value is the AbstractRelic default **−1** (MawBank's ctor sets no counter and the registry row has no `initial_counter`; `onEnterRoom` never writes it; −2-via-`onSpendGold` is the only writer). Re-derived to −1 with the justification inline; NOT a live behavior difference — the engine's usedUp gate is `counter != -2` (`event_framework.cpp`), so −1 vs 0 was invisible to every production read. Every other set_run_relics consumer was re-run green unchanged |
| Matryoshka (chest relic) | B3.25 | B4.7 `[x]` | **DISCHARGED:** two-use non-boss hook, 75/25 relicRng branch, reward insertion, counter `2→1→-2`, and boss no-op are live and tested |
| The Courier (shop relic) | B3.25 | B4.8 → **UNASSIGNED for the restock half**; see the blocker | **PRICE HALF DISCHARGED by B4.8:** the `x0.8` discount is applied at shop init in the Java's order (and is therefore overwritten, not compounded, by a Membership Card at that call site — reproduced, not corrected), and its purge-cost branch is live in both `shop_purge_cost_at_init` and `shop_purge_cost_after_purge`, the latter with the `0.8f * 0.5f` product the Java spells there. **The RESTOCK half stays deferred, and it is BLOCKED, not merely unscheduled:** `ShopScreen.purchaseCard`'s replacement draws `getCardFromPool(rollRarity(), type, false)` — `useRng=false` means `MathUtils.random`, libGDX's **unseeded global**, not `cardRng` (`ShopScreen.java:615-617`), so the replacement card's identity is not reproducible from a seed at all. The rarity roll before it, and the relic/potion restocks (`StoreRelic.java:105-112`, `StorePotion.java:86-89`), ARE seeded; whoever re-owns this should decide what a deterministic simulator does about an unseeded identity before writing any of it. B4.8's runbook §4 asks the operator to capture a Courier shop specifically to measure what the restock costs the seeded streams |
| Eternal Feather (rest-room heal) | B3.25, B4.9 | wave-runlayer S2 | **DISCHARGED by Wave-C track 2 stage 2.** `EternalFeather.onEnterRoom` (`EternalFeather.java:29-35`) heals `masterDeck.size() / 5 * 3` — integer division FIRST, so a 14-card deck heals 6, not 8. It rides the shared `dispatch_on_enter_room_relics` fan-out (`event_framework.hpp`), NOT `rest_apply_heal`: the hook is `AbstractDungeon.java:1755-1757`, which runs before `setCurrMapNode` (`:1783`) and long before `RestRoom.onPlayerEntry` (`RestRoom.java:33-43`, reached at `:1800`) builds the CampfireUI — so the HP is already there when the menu opens, and it lands whether or not the player then rests. HP goes through the new `heal_out_of_combat` door (`relics/relic_pickup.hpp`), which names the two identity fan-outs (Magic Flower's `onPlayerHeal` is gated on `RoomPhase.COMBAT`, `MagicFlower.java:30-37`, and cannot scale a room-entry heal — pinned by a named test; powers' `onHeal` reads a list `resetPlayer` cleared at `:1671`) and carries the note the still-deferred Red Skull `onNotBloodied` row needs (`AbstractCreature.java:403-408`). The ledger's citation `EternalFeather.java:29-35` was verified correct, NOT the +1000-offset kind. **Annotated 2026-07-28 (`wave-integrate`, Red Skull body):** that note has been consumed — the door now *calls* `dispatch_relics_on_not_bloodied_out_of_combat`, and the cross's exact line span is `:404-408` (`:403` is the closing brace of the clamp above it) |
| Translator `screen_state` content (shop / grid / map screens and future event variants) | B1.5, B4.3 | B4.8 | **EVENT slice content-validation DISCHARGED by B4.11:** `event_id` joins through the generated registry and option `disabled` / `choice_index` fields are type-checked; the content intentionally remains storage-less because translation outputs `RunState`/`CombatState`, not transient `RunController::event`. **The reward slice was discharged by B4.5.** **The NEOW slice was discharged by B4.14:** Neow arrives as an `EVENT` screen with the hard-coded id `"Neow Event"` (GameStateConverter.getEventState :343-355), which is recognised as a sentinel rather than joined — it is deliberately not an `events.yaml` row, because Neow is in no act's event/shrine/special pool and an `EventId` for it would place a non-pool entry into the three membership bitsets that pool ids index; the option list still gets the ordinary EVENT validation, and a near-miss id (`"Neow"`) is still refused. **The SHOP slice was discharged by B4.8** on the same terms: potion ids join through the registry (an unknown one fails loud / tallies under id-tolerance, as the reward potion already did), every `price` on a card / relic / potion row is type-checked, `purge_cost` must be an integer and `purge_available` a boolean — and it stays storage-less deliberately, because a merchant is derived state the game rebuilds from `(seed, merchantRng.counter)` and the one piece it DOES persist (the ramping purge cost) already has a `RunState` field fed from the oracle block. GRID and MAP content and any new event-variant fields remain with their owning tasks. |
| `b14_accept2` obtain-race capture-fidelity triage | B1.3 | B5.2 | flagged explicitly by B1.3; B1.4's acceptance is unaffected. **Now has a named reproducer and a machine classification** (2026-07-28): `b14_accept` STS00009 floor 4 Living Wall — the transform removes a Defend_R immediately (miscRng 0→1) but `ShowCardAndObtainEffect` grants Dark Embrace only when its animation completes, so seq 40-47 show deck 11 and seq 48 (next floor) shows 12. `replay_run_diff --event` recognises the shape NARROWLY (every differing field must be `master_deck_count` or a `master_deck[i]` at/past the seeded deck's end) and reports `RACE` instead of a divergence, so no stream/pool/`event_flags` diff can hide behind it |
| Infernal-Blade-generated Blood for Blood cost model (`cost_now` only; end-of-turn reset restores 4, not the game's reduced base) | B3.6 | G7 | judged unreachable — "revisit if G7 ever hits it" |
| Bottled trio bottling at acquisition (run-layer acquisition-choice machinery + a per-master-deck-instance innate flag) | B3.25 | wave-runlayer S3 | **DISCHARGED by Wave-C track 2 stage 3.** The per-instance flag is three bits of the previously-virgin master-deck `CardInstance.flags` — a MASTER-DECK-ONLY namespace (`run_deck.hpp` carries the full encoding decision), deliberately NOT a `CardFlag`, so `CardFlag` bit 15 (the last free combat flag bit) stays unspent and no `SCHEMA_VERSION` moves (previously-zero bits of an existing field, the `CombatState.flags` bit-4 precedent); combat builders render it as `CardFlag::INNATE` (initializeDeck's placeOnTop is ONE list with `isInnate`, CardGroup.java:933-941), including the :951-954 `preTurnActions` overflow draw when the innate+bottled collection exceeds masterHandSize (`queue_innate_overflow_draw`, both builders). The acquisition choice rides stage 1's `pickup: on_equip_screen` surface as `RelicEquipScreen::GRID_BOTTLE`, presented by the phase-independent pending-bottle overlay (`RunController.pending_bottle`) at EVERY site that can grant a bottle — reward claims (elites, chests, event reward rows) and shop purchases; the plain acquire/claim/purchase doors refuse whole and fail-loud (`NEEDS_EQUIP_CONTEXT`; the shop refusal is before gold moves), and the screenless event draw already excludes the trio. The hidden scope the row never mentioned — `getGroupWithoutBottledCards`'s live Act-1 grid consumers — landed as `master_card_purgeable_unbottled` (event PURGE/TRANSFORMABLE grids + gates, shop purge grid + service gate, Peace Pipe Toke grid + option gate), with the NON-excluding surfaces (Smith, Living Wall Grow, Neow grids, Astrolabe/Empty Cage) pinned un-excluded per their Java. The oracle boundary is closed source-side: the fork's `convertCardToJson` emits `in_bottle_*` (absent == false, so every existing capture translates unchanged) and the translator maps them on the deck walk only — the redeploy validation is the row below |
| Fork redeploy + a bottle-taking capture (validate the `in_bottle_*` boundary end-to-end) | wave-runlayer S3 | **next capture-campaign owner** | the fork SOURCE change (`GameStateConverter.convertCardToJson`, PROTOCOL §3.13) and the translator mapping are landed and unit-tested, but no deployed jar emits the field yet and no existing capture takes a bottle, so the master-deck bottle bits have never been compared against the live game. Needs: rebuild + redeploy the fork (blocked on the game install, owned by the G6 campaign agent at stage time), then one capture that claims a Bottled relic and picks a card (an elite/chest reward or a shop purchase), replayed through `--replay` — note the grid session maps bottle-grid rows in REVERSE master-deck order (`command_map.hpp`; getCardsOfType's addToBottom is a prepend, CardGroup.java:1052-1058 → :459-461), which only a real capture can prove |
| Akabeko (Vigor power row) | B3.24 | `wave-combat` (Track 1 stage 3) | **DISCHARGED, and the row was MISCLASSIFIED.** It read as a card-batch leftover ("no S1 Ironclad card grants Vigor, so no card batch will pick the power row up") -- true, and beside the point: **Akabeko is a live COMMON relic at `pool_order: 5`**, reachable from the very first combat reward, and it is Vigor's only S1 source. `Akabeko.atBattleStart` (`Akabeko.java:30-35`) is an unconditional addToTop `ApplyPowerAction(player, player, VigorPower(player, 8), 8)`. Landed as `PowerId::VIGOR` (id **87**), `type: BUFF`, default priority 5 (the ctor sets none), additive stacking, with `atDamageGive` = `damage += (float)amount` on NORMAL only (`VigorPower.java:41-47`) and an ATTACK-gated addToBot self-removal (`:49-55`) -- addToBot, so every hit of a multi-hit attack is boosted before it leaves, and a SKILL/POWER play does not consume it. The relic's inertness line left `RelicHooks.NonCombatAndDeferredRelicsAreNoOps`, which is what failed first |
| Pen Nib double-damage `PenNibPower` | B3.24 | `wave-combat` (Track 1 stage 3) | **DISCHARGED.** `PowerId::PEN_NIB` (id **88**), `type: BUFF`, **`priority: 6`** (`PenNibPower.java:36`), `stack: none`, `atDamageGive` = `damage * 2.0f` on NORMAL only (`:51-57`) plus an ATTACK-gated addToBot self-removal (`:39-44`). **The priority is the whole point and is why this landed in the same commit as Vigor:** 6 sits between the priority-5 addends (Strength, Vigor) and Frail(10)/Intangible(75)/Weak(99), so the game computes `((base + Str + Vigor) * 2) * 0.75`. Stage 1's `sort_powers_like_the_game` makes that fall out of slot ORDER rather than a special case in the damage walk; the reachable wrong answer without it is Pen Nib applied BEFORE Strength (base 6 + Str 2: 16 sorted, 14 appended), and that case is now pinned by test. The RELIC half was also wrong in the tree and is corrected: `PenNib.onUseCard` grants at `counter == 9` and merely RESETS at 10 (`PenNib.java:36-52`), so the TENTH attack is the empowered one; and `atBattleStart` (`:54-62`) re-grants at `counter == 9` without touching the counter, which is reachable because the counter is run-persistent -- that hook was not even bound on the row before and is now. Citations corrected: `PenNib.java:36-52` / `:40-43` / `:54-62` (the row and ledger carried `:40-56` / `:44-47`) |
| Boot (`onAttackToChangeDamage` DAMAGE-pipeline modifier) | B3.24 | `wave-combat` (Track 1 stage 3) | **DISCHARGED, and the deferral note had the SITE backwards.** `Boot.onAttackToChangeDamage` (`Boot.java:30-38`): `if (info.owner != null && info.type != HP_LOSS && info.type != THORNS && damageAmount > 0 && damageAmount < 5) return 5;` (`THRESHOLD = 5`, `:19`; the description's 4 at `:27` is display text). The hook's NAME says attacker-side and everyone read that as pre-block; **both call sites run `decrementBlock` FIRST** (`AbstractMonster.damage:639-643`, `AbstractPlayer.damage:1399-1403`), so the number Boot sees is the **unblocked residue** -- a 4-damage hit into 2 block deals **5**, and a 3-damage hit fully soaked by 3 block deals **0** because `damageAmount > 0` fails. That is also what the relic's own "unblocked attack damage" text says. Landed as `apply_boot` in `op_damage`'s INTEGER tail (`interp/interp_damage.cpp`), the `apply_torii` shape: the frozen float pipeline (`compute_damage`) is untouched. It is the FIRST of that tail's four modifiers -- before Buffer (`onAttackedToChangeDamage`), Thorns/Torii (`onAttacked`) and Tungsten Rod (`onLoseHpLast`) -- and it is attacker-side where those are victim-side, so on the player's own turn they never see the same hit. Gate is the enclosing `if (info.owner == AbstractDungeon.player)`, i.e. `src == kActorPlayer`. **`RelicHook::ON_ATTACK` (12) stays allocated and UNFIRED** -- no dispatcher, and `RelicHookContext` carries no damage in/out channel -- so the row's `on_attack: []` binding is documentation of which Java hook the bespoke site reproduces (the Magic Flower / Torii / Tungsten Rod precedent), and the empty native body stays because the generated dispatch odr-uses a handler for every `native:` row |
| Red Skull relic body (entry grant + `onNotBloodied` −3 heal-cross) | B3.24 | **DISCHARGED** — adjudicated from the shipped jar, 2026-07-28 | **BODY LANDED on `wave-integrate`; what stays open is confirming it against a capture.** The old premise, "needs a heal-cross hook that does not exist yet", was wrong twice over. (1) `RedSkull.onNotBloodied` is **fully decompilable** (`RedSkull.java:54-63`) — the CFR hole is the `atBattleStart` action at `:38`, not this. (2) No hook id was needed: Red Skull is the game's only *mechanical* `onNotBloodied` implementor (`MeatOnTheBone`'s is `stopPulse()`, `:47-50`), so it rides a hand-written fan-out — the Philosopher's Stone `onSpawnMonster` precedent — called from `heal_player_with_relics` in combat (`AbstractCreature.heal:404-408`, reached via `AbstractPlayer.heal:1544-1552`) and from `heal_out_of_combat` on the run layer, where the −3 is phase-gated off (`:56`) but the `isActive = false` at `:61` still runs. `kRelicHookCount` is unmoved. The −3 is a NEGATIVE Strength application through the ordinary `APPLY_POWER` door, so **Artifact needed no engine change at all**: `interp_powers.cpp`'s `negative_stat_flip` already types a non-positive Strength as a DEBUFF (`StrengthPower.java:37` → `:81-89`) and `apply_power_blocked_by_artifact` already spends the stack and drops the application (`ApplyPowerAction.java:131-138`, `ArtifactPower.java:33-40`) — charge consumed, Strength stays, and crossings are therefore CUMULATIVE DELTAS (a blocked −3 followed by another cross-down leaves +6). `RedSkull.onVictory` (`:66-69`) is deliberately **not** bound: the `at_battle_start` re-seed already re-derives the latch from HP. **WHAT REMAINS OPEN IS NOW THE WHOLE ROW:** the +3 on an already-bloodied ENTRY — the body of the anonymous action `atBattleStart` queues at `:38` — is **OWNER-SPECIFIED** (project owner, 2026-07-28), not derived; only its `addToBot` queue end is Java-pinned. A Red Skull run capture was planned to confirm it — and was **not needed**: `desktop-1.0.jar` (same build; `RedSkull.class` byte-identical with the decompile-source jar) retains `RedSkull$1`, decompiled in full in `tools/oracle_bridge/driver/redskull_capture_runbook.md`. The owner spec was right on the OUTCOME (already-bloodied entry grants +3 and latches `isActive`) and wrong on the TIMING: the action re-tests `!isActive && player.isBloodied` when it RESOLVES, at the bottom of the battle-start drain, after Blood Vial's/Pantograph's `addToTop` heals settle — and it grants via a DIRECT `AbstractCreature.addPower` (`:506-527`, stack-or-append, no sort, no `ApplyPowerAction` interception). **Fixed on `final-integrate` (fix-forward after the Stage-B union merge):** the hook now clears the latch synchronously (`:37`) and addToBots a DECIDING item — `Opcode::RED_SKULL_ENTRY` (65, claimed below) — whose execute body is `RedSkull$1`; entering at exactly half HP with a battle-start healer grants nothing and spends no Artifact charge, in both relic acquisition orders. RED-first: `RelicHooks.RedSkullEntryDecidesAfterBattleStartHealsSettle`, `RelicHooks.RedSkullEntryHealCrossWithArtifactSpendsNoCharge`, `CombatStart.RedSkullEntryDeciderResolvesAfterHealsInTheSharedBlock` |
| Art of War / Ancient Tea Set (cross-turn / cross-room energy flags) | B3.24 | `wave-combat` (Track 1 stage 3) | **DISCHARGED. Neither needed "state beyond `RelicSlot.counter`", and the two need OPPOSITE answers.** **Ancient Tea Set** (`AncientTeaSet.java:49-61`, `:63-66`, `:76-81`): `RelicSlot.counter` **IS** the cross-room state, exactly -- `-2` armed, `-1` spent, which is the GAME'S OWN encoding (the relic writes `this.counter` directly and CommunicationMod reports it), so nothing had to move out of it. Its real gap was the **producer**: `onEnterRestRoom` fires from `RestRoom.onPlayerEntry` (`RestRoom.java:38-42`) at `AbstractDungeon.java:1800`, AFTER the `onEnterRoom`/`justEnteredRoom` fan-outs. Landed as `dispatch_relics_on_enter_rest_room` (`relics/relic_pickup.hpp`) plus **one call** at rest-room entry -- **flagged as a Track-2 collision**, deliberately kept to a single line. **Art of War** (`ArtOfWar.java:52-82`) is the opposite: its counter is NEVER written in the game and stays `-1`, so its latch may NOT live there (the Centennial Puzzle class). It takes **one** `CombatState.flags` bit (**24**), not two -- `firstTurn` is `s.turn == 0` (the hook runs before `start_of_turn`'s `++s.turn`), and `gainEnergyNext` is stored INVERTED as "an ATTACK was played this turn" so the value-initialised default already means `atPreBattle`'s `gainEnergyNext = true`. The load-bearing detail is the Java's LINE ORDER: `gainEnergyNext = true` is the LAST statement of `atTurnStart`, after the check -- clearing before testing grants every turn |
| Preserved Insect (elite-room HP scaling) | B3.24 | `wave-combat` (Track 1 stage 3) | **DISCHARGED.** `PreservedInsect.atBattleStart` (**`PreservedInsect.java:30-41`**) on the same `kCombatFlagEliteRoom` marker as Sling of Courage. It is a **current-health CLAMP, not an HP-init change**: `maxHealth` is untouched, so the large slimes' `maxHealth/2` split threshold, Lagavulin's wake and every other max-HP-relative gate still read the unscaled max, and monsters spawned later (splits) are never scaled. The clamp is one-directional -- the Java `continue` means it never raises health. The float product is written exactly as the Java writes it, a C-style truncation of `(float)maxHealth * 0.75f` (`MODIFIER_AMT = 0.25f`, `:19`), NOT `maxHealth * 3 / 4`; the `-ffp-contract=off` pipeline discipline applies and the odd-multiple cases are pinned by test. Applied synchronously in the relic body rather than through a new monster-HP opcode, because the Java writes `m.currentHealth` directly inside `atBattleStart` |
| Fire Potion `applyEnemyPowersOnly` / THORNS typing | B3.23, B3.2 | `wave-combat` (Track 1 stage 4) | **DISCHARGED, and it was a live WRONG ANSWER rather than a missing feature** — the row read as a deferral. `registry/potions.yaml` authored the DAMAGE step with no `damage_type`, i.e. NORMAL, so the full `applyPowers` pipeline scaled it by player Strength/Weak and target Vulnerable. `FirePotion.use` (**`FirePotion.java:43-47`**) builds `new DamageInfo(player, potency, DamageType.THORNS)` and calls `applyEnemyPowersOnly`, and TWO independent facts each pin the number at a flat 20: the THORNS TYPE (every `atDamageReceive`/`atDamageGive` hook is `if (type == NORMAL)`-gated, `VulnerablePower.java:62-73`), and `applyEnemyPowersOnly` itself (`DamageInfo.java:102-120`), which never runs owner powers and whose loops pass the never-reassigned `this.output` rather than the running `tmp`. Fixed with ONE YAML token, `damage_type: THORNS` — every piece it needed already existed. The row's own comment claimed enemy Vulnerable DID scale it; that is corrected in place. `applyEnemyPowersOnly`'s last-power quirk is deliberately UNMODELED and named at the row: the only `atDamageFinalReceive` overriders (Flight, Forcefield, the two Intangibles) are out of Act-1 scope, and THORNS typing gives the identical answer until one arrives. **ANNOTATED at wave2-engine stage 1 (2026-07-28), which re-read `FirePotion.use` (FirePotion.java:43-47) and `DamageInfo.applyEnemyPowersOnly` (:102-120) in full:** the flags-word home this row anticipated now exists (`kDamagePure`/`kDamageNullSource`, interp.hpp bits 8-9), and Fire Potion deliberately sets NEITHER bit — its DamageInfo has a REAL owner (the player), and `applyEnemyPowersOnly` is not the pure skip: it consults the TARGET's receive hooks (with the `this.output` quirk) at USE time, before the DamageAction is even queued. Its exact S1 semantics are already what plain THORNS typing lands (flat potency: owner powers never consulted, receive hooks NORMAL-gated identity on a THORNS info, final-receive overriders out of scope). The Act-2 owner of the first `atDamageFinalReceive` power should model the quirk as a use-time precompute — not by setting `pure` on this row; the decision and its reasoning are recorded at the yaml row |
| Snecko Oil cost-randomization potion body | B3.23 | `wave-combat` (Track 1 stage 4) | **DISCHARGED** as `Opcode::RANDOMIZE_HAND_COST` (**60**), after which the potion is a two-step DATA program (`DRAW 5`, then the new op) and the `potion_use_implemented` gate re-opened by itself. The row's premise that the potion randomizes *the drawn cards* was wrong: `SneckoOil.use` (**`SneckoOil.java:41-46`**) is two `addToBot`s, so the draw resolves FIRST and `RandomizeHandCostAction` (**`RandomizeHandCostAction.java:26-38`**) walks the POST-draw hand — it re-costs the WHOLE hand. The write is `costForTurn = cost = newCost`, i.e. PERMANENT. Landing it also forced a correction to already-green content: `power_native_confusion` performed no `card.cost != newCost` test at all and so destroyed a live this-turn cost modification on an equal roll (`ConfusionPower.java:38-48` writes nothing on equality). The two Java bodies are now ONE shared helper, `randomize_card_cost`, differing by the single boolean that is their only real difference (`card.freeToPlayOnce = false`, outside the equality `if`) |
| `--replay` lacks `--event`'s obtain-race recognition | replay-neow-exit's read-out | UNASSIGNED — next replay-fidelity owner | **DISCHARGED by `wave2-harness` stage 2.** `is_obtain_race` moved above the whole-run driver and both modes call it. Its parameters now name the ROLE (`ahead` / `behind`) rather than the source, because the two modes see the race from OPPOSITE sides: `--event` seeds from the pre-entry record so the CAPTURE is ahead, while `--replay` diffs before applying record *k* so the SIM has already obtained the card the mid-animation dump does not show. Same narrowness -- every differing field must be `master_deck_count` or a `master_deck[i]` at or past the SHORTER deck's end -- and the verdict line now carries an `obtain-race` count beside the library-order one. Still 0 across the 42 artifacts re-run at the discharge, which is the row's own "not hit by the current corpus"; the asymmetry is what is gone |
| STS00509 seq-135 Louse-turn 5-HP residual | fix-starter-upgrades' frontier re-sweep | **DISCHARGED** — final-integrate, 2026-07-28 | **The row was MISNAMED: the Louse implementation was never at fault** — every one of the fight's nine monsterHpRng draws and every bite value reproduces exactly. The divergence was EXPLOSIVE_POTION's damage TYPING: the game's `ExplosivePotion.use` (`ExplosivePotion.java:52`) is `DamageAllEnemiesAction(null, DamageInfo.createDamageMatrix(potency, true), NORMAL)` — the PURE matrix skips `applyPowers` (`DamageInfo.java:126-134`) and the NULL source fails Curl Up's `info.owner != null` gate (`CurlUpPower.java:38`) — while the registry row's untyped step ran the full NORMAL pipeline (target Vulnerable ×1.5 at `interp_damage.cpp` and a wrongly-consumed Curl Up via the on-attacked dispatch). In the floor-11 triple-Louse fight that killed a Louse the game left at 3 HP, losing its bite — the 5-HP residual. Fixed on `final-integrate`: the row is `damage_type: THORNS` (deliberate model substitution — see the NEW row below), provenance corrected (`:52`, not the stale `:46`); RED-first `Potions.ExplosivePotionIgnoresTargetVulnerable` / `IgnoresPlayerStrength` / `DoesNotTriggerCurlUp`; STS00509 CLEAN to terminal in both g6 campaigns |
| Explosive Potion THORNS model substitution | final-integrate (STS00509 discharge) | **DISCHARGED** — wave2-engine stage 1, 2026-07-28 | **The faithful fix landed: `kDamagePure` (bit 8) + `kDamageNullSource` (bit 9) in the DAMAGE item's flags word (interp.hpp), and the registry row is re-typed to the game's literal shape** — a NORMAL step with `pure: true, null_source: true` (`ExplosivePotion.use`, ExplosivePotion.java:52). op_damage skips `compute_damage` entirely for a pure item (no float op runs, so the FP contract on surviving paths is untouched) and STILL dispatches ON_ATTACKED for a null-source NORMAL hit — the game's power loop is unconditional (AbstractPlayer.damage:1425-1426 / AbstractMonster.damage:667) and the `info.owner != null` gates live in the power BODIES, now expressed via `HookContext::source_null` in Curl Up / Thorns / Flame Barrier / Angry (+ Plated Armor's wasHPLost and the Boot/Torii relic sites, whose Java gates also test the owner). Behavior-identical today, and CHECKED: debug+asan+release green with the 20 fixtures byte-unchanged (no fixture throws a potion), and STS00509 re-replayed CLEAN to terminal in BOTH g6 campaigns after the retype. **Two corrections to this row's old text**: (1) Act-2 Malleable is NOT owner-ungated — `MalleablePower.onAttacked` tests `info.owner != null` (MalleablePower.java:64), and a whole-tree sweep of every live `onAttacked` overrider found NONE that distinguishes the THORNS stand-in from the pure matrix (each either owner-gates, or — ShiftingPower.java:32-38 — ignores owner AND type and behaves identically under both), so the retype is exactness, not a live fix; the first genuinely-distinguishing registrant would be an owner-INsensitive NORMAL-gated body, which the shipped game does not contain. (2) The models' divergence is pinned by `DamagePure.PureFlagSkipsModifiersButKeepsOnAttackedDispatch` (a pure hit with a REAL owner triggers Curl Up — inexpressible under THORNS) and `DamagePure.NullSourceFailsEveryRegisteredOwnerGate`, with `UnflaggedNormalDamageStillScalesAndDispatches` as the negative control |
| Distilled Chaos potion body | registry deferral, surfaced by the G6 campaign audit | **DISCHARGED** — fix-discovery-duplication (ce9efcd), 2026-07-28. **The row's premise was wrong: no "recursive play" opcode was ever needed** — `PLAY_CARD` + `kPlayCardFromDrawTop` IS `PlayTopCardAction`, landed with Mayhem (whose own header says it was reconstructed from `DistilledChaosPotion.java:41`). The real difference from Mayhem: `use` (`DistilledChaosPotion.java:38-43`) evaluates `getRandomMonster(null, true, cardRandomRng)` as a constructor ARGUMENT — all `potency` (3; Sacred Bark doubles the PLAY count) target rolls spend at USE time before any play resolves — so the native body rolls `roll_random_target` per iteration at use and bakes each target into its queued item; `exhausts = false`. Capture pin: +3 draws exactly per drink (STS01857 s20, STS02110 s31, STS01314 s49/69, both campaigns). Live: STS01857 zero-diff to run terminal (both), STS02110 clean (both), STS01314 11 → 1 diff records, STS00353 improved in all 13 supplementary boss campaigns |
| Duplication Potion + `DuplicationPower` (the g6b translation abort) | greedy-b1.5.0's boss-reward capture (aa2c11d) | **DISCHARGED** — fix-discovery-duplication (2103bc1), 2026-07-28. `PowerId` **92** `DUPLICATION` (game_id `"DuplicationPower"` — the id STS01221's translation aborted on at record 105): `onUseCard` (`DuplicationPower.java:39-62`) is Double Tap's synchronous front-of-queue replay with NO CardType filter; `atEndOfRound` (`:65-71`) decays one per round (1-from-1 removes) instead of Double Tap's end-of-turn removal — reproduces the g6b witness (drunk s103, amount 1 at s104, duplicated nothing, gone by s105). The potion row is now a plain data APPLY_POWER program (potency 1; Sacred Bark doubles the charge count); **the deferred-potion set is EMPTY** and the refusal-gate traps repoint to FAIRY_POTION (the out-of-combat revive, still legitimately deferred). Live: zero translation errors corpus-wide; every STS01221 artifact translates fully and replays zero-diff to the class (b) `--replay` SHOP_ROOM mapping frontier — the boss-reward records sit beyond that documented harness limit, no longer behind an abort |
| Fairy in a Bottle revive (the row said "out-of-combat"; it is BOTH) | B3.23 | `wave-combat` (Track 1 stage 4) | **DISCHARGED, and the row's premise was wrong.** The trigger site is `AbstractPlayer.damage` (**`AbstractPlayer.java:1482-1497`**) — the ORDINARY damage path, which serves in-combat damage, self-inflicted HP loss and event damage alike. It is never USED (`canUse()` is `return false`, `FairyPotion.java:47-50`), so `potion_use_implemented` still answers false for it and `combat_potion_legal` still rejects it by name; discarding one stays legal. `potions.hpp`'s deliberate layer boundary means `CombatState` has no belt, and a run-layer-only revive would be unfaithful because `damage()` RETURNS and the player is alive for the rest of the same action — so the ARMED COUNT is mirrored into `CombatState.flags` bits 16–18 at `enter_combat`, consumed by `try_player_revive`, and the real slots are burned at `fold_back_combat` (leftmost first). Additive bits, so no `SCHEMA_VERSION` move. Order: Mark of the Bloom (no S1 row) blocks both sources; a held Fairy BEATS a Lizard Tail and, being an `else if` on `hasPotion`, leaves the tail unconsulted. Exactly one fairy per lethal event. The out-of-combat twin is `apply_event_damage` (`event_framework.cpp`), which is the ONLY out-of-combat lethal site in S1 — every other run-layer HP write is a heal or a max-HP change, and Neow's `hp - hp/10*3` drawback is never lethal. A mid-combat DISCARD of a fairy disarms the mirror, which is the only other belt mutation a combat can see |
| Translator: power `misc` fields other than player-owned Combust | B3.7 | UNASSIGNED | **RE-SCOPED, not implemented — `wave2-harness` stage 3, from measured evidence.** The fork's `misc` is a FIVE-WAY UNTAGGED UNION: `GameStateConverter` emits whichever of `basePower`/`maxAmt`/`storedAmount`/`hpLoss`/`cardsDoubledThisTurn` is present first (PROTOCOL §3.14), with no field telling a reader which one arrived — the Combust case works only because the power id disambiguates it. Two things then have to be true before another power can join, and NEITHER is today. (1) **No producer exists.** Across the 30-run G6 main campaign, 4369 power instances were emitted with exactly `{id, name, amount, just_applied}` — `misc` appears ZERO times, and so does `damage`. There is nothing to validate an implementation against. (2) **There is no schema home.** `PowerSlot.counter` is already spoken for (`damage`, for Panache and The Bomb), so a second per-power scalar is a `PowerSlot` layout change and a `SCHEMA_VERSION` bump — stop-the-line per conventions §5, not a translator edit. **Blocker: a capture that actually emits a non-Combust power `misc`, plus a schema decision if it does. Owner: next capture-campaign owner.** |
| Translator: `monster_move_history` beyond 3 entries | B1.5, B4.3 | UNASSIGNED | **HALF IMPLEMENTED, half re-scoped — `wave2-harness` stage 3.** The row's premise was that nothing was mapped; in fact the fork already emits the FULL `AbstractMonster.moveHistory` per monster in room order (PROTOCOL §5), up to 14 entries in the corpus where stock's own JSON gives 2 — so no fork change was ever needed and the field was simply deferred whole. **Now mapped: the newest THREE, most-recent-first**, into `MonsterState.move_history[3]`. Direction is load-bearing and is tested: `moveHistory` is appended, so the newest move is the LAST element, and reading the head would store a monster's opening moves as its latest — a wrong-but-plausible value no differ can flag. The positional join is CHECKED against the ids the combat block already parsed (a misalignment would attribute one monster's history to another) and holds across all 30 G6 artifacts. **BEYOND THREE is re-scoped, and is now a schema question rather than a translator one:** `CombatState` stores three, nothing the run layer models reads more than two moves back (stock's `lastTwoMoves`), and widening it is a `CombatState` layout change plus a `SCHEMA_VERSION` bump — stop-the-line per conventions §5. **Blocker: a monster body that needs a fourth. Owner: whoever adds one.** |
| Translator: real `act_boss` | B1.5, B4.3 | UNASSIGNED | **DISCHARGED by `wave2-harness` stage 3.** The join is the ENCOUNTER registry's own `encounter_by_game_id`, not a table of spellings: `AbstractDungeon.bossKey` is the same string `encounters.yaml` keys on ("The Guardian" / "Hexaghost" / "Slime Boss", `Exordium.initializeBoss`, `MonsterHelper.getEncounter`), so an unknown key — or a real encounter that is not a BOSS row — aborts like every other id join. The stored value is the **EncounterId**, because that is the space the run layer already speaks (its `boss_list[]` holds encounter game_ids and `enter_combat` takes one); a MonsterId would not survive a two-monster boss. **HONEST LIMIT, and a new obligation:** nothing in `src/engine` writes `RunState.boss_ids` — the run layer keeps the act boss in `RunController.lists.boss_list[]` and never mirrors it — so `--replay` neutralizes the field the way it already neutralizes `map[]`, and it comes back the moment `run_begin` records `boss_list[0]`. That mirror is a RUN-LAYER change and is re-owned to the run-layer owner, not closed here |
| Bit-exact oracle for the raw monsterRng monster / elite / boss lists | B3.12 | UNASSIGNED | B3.12 pinned the algorithm + determinism, not a golden list; B4.4's floor-0 triple pins stream state only. Natural home is B5.2's campaign automation |
| `RETAIN` `CardFlag` end-of-turn sweep | B3.1 | UNASSIGNED — "first content consumer" | ETHEREAL (B3.5/B3.6) and INNATE (B3.9) discharged; no S1 Ironclad card uses Retain |
| Gremlin move-99 escape (`EscapeAction` body **and** the `deathReact`/`escapeNext` trigger, landed together) | B3.16 | UNASSIGNED — Act-2 owner | unreachable in Act 1: `escapeNext()` has no caller in the decompiled tree; the only `deathReact()` call is `BanditBear.java:131` |
| `CardDef` has no upgraded-**target** column | B3.10 scope read | **DISCHARGED** — wave2-engine stage 2, 2026-07-28 | **The scope read was right: the old "ActionMask deviation, not a state one" claim was WRONG, and the state divergence is REACHABLE — it was constructed and reproduced RED before the fix.** The route is Distilled Chaos: all `potency` targets are rolled and BAKED at use time (ctor-argument `getRandomMonster`, `DistilledChaosPotion.java:38-43`), so a Strike played first can kill the monster a queued Blind+ was baked onto; the engine's base-kind `ENEMY` reads then CANCELLED the play (card_can_use dead-target rejection) while the game — where Blind+ is live `CardTarget.ALL_ENEMY` (`Blind.java:48`, `Trip.java:53`) and `GameActionManager.java:264-283` suppresses exact ENEMY only — plays it and Weakens the survivors. RED test: `CardColorlessUncommonsBlind.UpgradedPlaysThroughItsDeadBakedTarget` (fails pre-fix on exactly the missing Weak; the Strike-kill expectation doubles as the in-test control), mask half `UpgradedTakesNoTargetInTheActionMask`. Fix as this row prescribed, all three consumers at once: generator `upgraded_target:` column (emit/cards.py; defaults to the base target, REFUSES a random_target change; generated-header-only, no serialized struct, no schema concern) + `cards.yaml` rows for Blind/Trip + upgrade-aware reads via new `cards.hpp` `card_target_kind`/`card_needs_target` in (1) advance.cpp's mask row shape, (2) card_can_use's dead-target rejection, (3) resolve_card_play's :264-283 suppression. Bounded forever: a whole-tree grep re-confirmed Blind and Trip are the ONLY cards that reassign `this.target` in `upgrade()`. cards.yaml provenance edits forced the documented `cards_sidetable.json` regeneration (hash-line-only diff, as its `_provenance.regenerate` predicts) |
| **Nine pre-existing out-of-range Java citations, repo-wide** | integration-15 citation audit | UNASSIGNED | `RupturePower`, `DexterityPower`, `FrailPower`, `Clash`, `HeavyBlade`, `Torii`, `TungstenRod`, `LizardTail`, `MagicFlower` each carry a `File.java:line` citation that does not resolve in `D:\STS_BG_Mod\SlayTheSpireDecompiled`. Found while fixing the two citations integration-15's own merge broke, and **deliberately left alone** — they predate that integration and are a separate repo-wide condition, not something those branches introduced. Fixing them is comment/provenance-only and needs each cited method **re-read in full**, not line-shifted: the two integration-15 fixed both had *correct prose and wrong numbers*, so a mechanical offset would have looked right and been wrong |
| `a20.yaml` row 4 cites `tackleDmg = 10`, a dead `DamageInfo` | integration-11 | B4.15 follow-up | literally accurate (`SlimeBoss.java:94-96`) but `tackleDmg` is `damage.get(0)` and SlimeBoss never reads it — only `damage.get(1)` at `:137`/`:144`. `monsters.yaml` is right to omit it. Not a document conflict |
| Egg trio onEquip reward-screen preview pass | wave2-engine stage 3d (stale-comment re-derivation) | UNASSIGNED — needs an equip-plumbing decision first | **A "does not exist yet" comment whose prerequisite arrived, hiding a REAL (narrow) gap.** `FrozenEgg2.onEquip` (FrozenEgg2.java:31-38; Molten/Toxic identical) walks the OPEN combat-reward screen's card offers and upgrades matching-type cards in place (`onPreviewObtainCard` → `onObtainCard`). The engine's three egg `on_equip` bodies are no-ops whose old justification — "the reward screen does not exist yet" — expired: `RewardScreen`/`RunRewardItem.card_upgrades` exist, and an elite reward screen holds a RELIC and a CARDS item at once, so claiming an egg with an offer still open is reachable in S1. **Scope of the divergence**: the still-open OFFER's upgrade bits only (a reward-screen observation the run differ compares); the MASTER DECK converges regardless — the pick lands through `onObtainCard` with the egg then owned, and the Java's `!upgraded` gate makes preview-then-obtain and obtain-only end identical. **Why not fixed in the sweep**: the plain `on_equip` dispatch receives `(RunState, miscRng, slot)` — the reward screen travels only in `RelicEquipContext` (`on_equip_screen` bodies), and the eggs cannot move to that surface because the ctx-less `acquire_relic` door (event grants in `exordium_events_*`, the shop fallback) refuses `on_equip_screen` ids by design (`NEEDS_EQUIP_CONTEXT`). The fix needs a deliberate decision about how every equip site sees the screen (e.g. an optional `RewardScreen*` on the plain surface, or threading ctx everywhere) — an API change across all acquisition sites, materially larger than the stale-comment sweep that found it. Sites annotated: `relic_pickup_uncommon.cpp` (all three bodies), Frozen Egg's yaml provenance |

## Shared namespaces — allocation now in force

A conventions §7 **rule-of-two** observation, recorded here rather than inside
any one task block because both namespaces are contended across concurrent
worktrees: contention for them **stopped one task dead** and produced a
**silent off-limits edit** in another. A worktree cannot see its siblings, so
"take the next free number" is not a safe local decision — the orchestrator
allocates first.

**1. Registry ids are append-only, and gaps are legal.** The loader
(`tools/registry_gen/stsgen/loader.py`) requires an id that is an integer,
`>= 1`, unique and appended — it does **not** require contiguity, and no
emitted table is a dense id-indexed array, so a gap costs nothing at runtime.
Several gaps are live today and every one of them is correct: `MonsterId` 14 is
unallocated (B3.18 took 12/13, B3.19 took 15), and `PowerId` ids have long run
ahead of the row count (`ANGER` is 33, `ANGRY` is 40). **Never renumber to close
a gap.** Renumbering an existing id is stop-the-line (conventions §5, design
§4.4) and would cost a design-doc change-log entry plus a schema-version bump —
never worth a cosmetic tidy. Re-derive the live id list from `registry/*.yaml`,
never from this paragraph. (Note the one place contiguity *is* required and is a
different thing: `relics.yaml` `pool_order` within a tier.)

**`PowerId` 47, 54–58 and 60–72 are PERMANENT gaps.** 47 was reserved by B3.21,
which took `MODE_SHIFT` 45 and `SHARP_HIDE` 46 and then did not need a third;
**B3.8 correctly refused to backfill it** and appended its six powers at 48–53.
54–58 were left behind in the same way — B3.27 appended `CONFUSION` at 59 rather
than filling them. **60–72 are the third instance**: B3.15 appended `ENTANGLE`
73 / `SPORE_CLOUD` 74 out of the block allocated to it (the rationale is
recorded in `powers.yaml`'s own B3.15 header comment) rather than reaching down
into the free window below it. Every task holding a reservation in these three
windows has landed, so none of them will ever be issued. Filling a permanent gap
is an id renumber in all but name: it changes which power a stored
`PowerSlot.power_id` byte means, which is exactly what the append-only rule
exists to prevent. Leave them empty. **This list was re-derived 2026-07-28 and
had gone stale — the full permanent-gap set is now 30–32, 34–39, 41–44, 47,
54–58, 60–72, 76, 79–80, 85–86 and 89–90** (76 the Looter batch's unissued
reserve, 79–80 and 85–86 B3.11's, 89–90 Wave-C's relic-tail remainder — a
`PowerId` left unspent GAPS, it is never "released"; that word is reserved for
the reusable scarce namespaces, `ChoiceKind` values and flag bits). Re-derive
from `registry/*.yaml` on use, never from this paragraph.

**Allocated blocks belong in this file, not only in a brief.** All three gaps
above exist because a block was handed to a task that then needed fewer ids than
it reserved — which is correct and costs nothing. What is *not* free is leaving
the allocation only in the dispatching brief: a worktree cannot see its siblings,
the brief is gone once the wave lands, and the next reader is left with an
unexplained thirteen-wide hole and no way to tell a reservation from an error.
Record the block here when it is allocated.

**2. `MonsterIntent` values in `tools/registry_gen/stsgen/vocab.py` are a
shared namespace and need orchestrator allocation before use.** The generator
rejects an unknown intent, so a monster task needing a new telegraph *must*
edit `vocab.py` — a file otherwise off-limits to content tasks. B3.19 hit
exactly that and took the edit. Claim the number in the task brief; do not
pick one locally.

| Intent | Value | Owner |
|---|---|---|
| `SLEEP` | 9 | B3.19 — landed |
| `STUN` | 10 | B3.19 — landed |
| `DEFEND` | 11 | B3.16 — landed (also The Guardian's Charge Up: one Intent constant, two users) |
| `ATTACK_BUFF` | 12 | B3.21 — landed |
| `ESCAPE` | 13 | **allocated — B3.15 remainder** (Looter's `Intent.ESCAPE` telegraph) |
| *(reserve)* | 14 | **allocated — B3.15 remainder**, contingency only; report it if used |
| *(free)* | 15–16 | **unallocated.** Claim one in a task brief before use |

**3. Engine `RunPhase` and fuzz `MoveCat` are shared namespaces too — claim
values here before use.** Neither was in this section when B4.9 and B4.7 ran
concurrently, and **both tasks independently took `MoveCat` 14** — exactly the
collision this section exists to prevent (a wrong `MoveCat::COUNT` is not
cosmetic: it sizes the fuzz coverage arrays, so an enumerator at or above COUNT
is an out-of-bounds write). As everywhere in this section, **gaps are legal and
values are never renumbered** — `RunPhase` is replay-visible and `MoveCat`
keys stored soak-coverage identities.

| Namespace (defined in) | Taken | Reserved / free |
|---|---|---|
| `RunPhase` (`include/sts/engine/run_advance.hpp`) | **0–10** (`NONE`..`SHOP`; 7 `REST_SITE` B4.9, 8 `TREASURE_ROOM` B4.7, 9 `EVENT_DIALOG` B4.10, 10 `SHOP` B4.8 — all landed, each claiming its reservation) | 11+ free — claim here first (Wave-C track 2's 11–12 allocation was RELEASED unspent; see the Wave-C table) |
| fuzz `MoveCat` (`tools/fuzz/include/sts/fuzz/policy.hpp`) | **0–26** (14–20 rest-site B4.9, 21–22 treasure B4.7, 23 `EVENT_OPTION` B4.10, 24 `EVENT_GRID` B4.11, 25 `CHOICE_CONFIRM` B3.10c, 26 `SHOP` B4.8 — **spent**; `COUNT = 27`) | 27+ free — claim here first and bump `COUNT` past every enumerator (Wave-C track 2's 27–29 allocation was RELEASED unspent; see the Wave-C table) |

### Wave-C allocations — 2026-07-28, two concurrent tracks (deferred-bodies wave)

Two track branches off `wave-defer-base` (`master` frozen for the G6 gate):
`wave-combat` (track 1: combat-layer files — action_queue, interp_*, damage
pipeline, potions, combat-side registry rows) and `wave-runlayer` (track 2:
run-layer files — run_advance, rest/room entry, Neow/relic-acquisition grids,
pool emitters). Same rules as every wave: a block is exclusive to its stage; a
stage needing MORE stops and asks the orchestrator. **`ChoiceKind` is a scarce
4-bit namespace with only five values left before this wave — unspent
`ChoiceKind` values and `CombatState.flags` bits are RELEASED, not gapped**
(the B3.11 scarce-namespace precedent); unspent opcodes/PowerIds/CardIds gap
as usual.

| Namespace | Track 1 potions stage | Track 1 relic-tail stage | Track 1 energyMaster stage | Track 2 |
|---|---|---|---|---|
| Opcode | **60–62** — SPENT **60** `RANDOMIZE_HAND_COST` (Snecko Oil); **61–62 released unspent** | **63–66** — SPENT **63** `REMOVE_DEBUFFS`, **64** `UPGRADE_RANDOM_CARD`; **65–66 released unspent** | **67** (contingency) — **RELEASED unspent** (the derivation needed no opcode) | **68** (reserve) — **RELEASED unspent** |
| `PowerId` | — | **87–90** — SPENT **87** `VIGOR`, **88** `PEN_NIB`; **89–90 released unspent** | — | — |
| `ChoiceKind` | **11–13** — SPENT **11** `DISCARD_TO_HAND_FREE` (Liquid Memories), **12** `HAND_TO_DISCARD_THEN_DRAW` (Gambler's Brew AND Gambling Chip, one shared body); **13 RELEASED unspent**. Elixir and the four discovery potions needed NO new kind — Elixir is the already-live optional `EXHAUST`, and the discovery four ride opcode 50 with a pool selector and a copy count in the queue item's unused `src`/`tgt` bytes | **14–15** — **BOTH RELEASED UNSPENT.** The relic tail needed no new choice kind: Gambling Chip is the only item in it that wanted one and belongs to the potions stage | — | — |
| `CombatState.flags` bits | **16–19** — SPENT **16–18** as the armed Fairy-in-a-Bottle COUNT (a count, not a bit: multiple fairies are legal and exactly one is consumed per lethal event); **19 RELEASED unspent** | **20–25** — SPENT **20** elite room, **21–23** Orange Pellets ATTACK/SKILL/POWER latches, **24** Art of War "attack played this turn"; **25 released unspent**. Slaver's Collar cost ZERO bits (see row 78) | — | — |
| `CardId` | — | — | — | **127–128** — SPENT **127** `CURSE_OF_THE_BELL`; **128 left a permanent gap** |
| `RunPhase` | — | — | — | **11–12** — **BOTH RELEASED UNSPENT**: the grids reuse `NeowState` (`kNeowGridPickCap` 2→3 + `NeowGridMode::TRANSFORM_UPGRADE`) and the bottle overlay is phase-independent (`pending_bottle`), so no phase was needed |
| fuzz `MoveCat` | — | — | — | **27–29** — **ALL RELEASED UNSPENT** (`COUNT` stays 27): the new grids ride the existing mask-driven NEOW/SHOP/REWARD_CLAIM buckets |

**Integration audit (2026-07-28, `wave-integrate`):** every spend/release above
was RE-DERIVED from the union tree, not from branch claims — `vocab.py` OPCODES
(60/63/64 present; 61–62, 65–68 absent), `interp.hpp` `ChoiceKind` (11/12
present; 13–15 absent), `combat_state.hpp` flag constants (16–18 Fairy count,
20 elite, 21–23 Orange Pellets, 24 Art of War; 19 and 25 untouched-zero),
`powers.yaml` (87/88; 89–90 absent), `cards.yaml` (127; 128 absent),
`run_advance.hpp` `RunPhase` (max 10), `policy.hpp` `MoveCat` (`COUNT == 27`).
Zero discrepancies against the tracks' stage reports.

### final-integrate allocation — 2026-07-28 (Red Skull entry-decider fix-forward)

| Namespace | Claim |
|---|---|
| Opcode | **65** `RED_SKULL_ENTRY` — the battle-start deciding action `RedSkull.atBattleStart` addToBots (`RedSkull.java:38`; body = `RedSkull$1`, recovered from the shipped jar — see the Red Skull row in Deferred obligations). 65 had been released unspent by Wave-C's relic-tail block; claimed here per the append-only rule. The three-file change (vocab.py `OPCODES`, steps.py `GENERAL_OPS`, cards.hpp drift `static_assert`) landed together |

### Closing-wave allocations — 2026-07-28 (two concurrent worktrees, reconciled at integration)

| Namespace | Claim |
|---|---|
| `PowerId` | **91** `REGENERATE_MONSTER` (branch `add-regenerate-monster`) — arm 3 of the emerald-elite entry roll (`MonsterRoomElite.java:60-64`), game_id `"Regenerate"`, DISTINCT from `REGEN`/"Regeneration" (id 18). **92** `DUPLICATION` (branch `fix-discovery-duplication`) — the Duplication Potion's power, game_id `"DuplicationPower"`, the id STS01221's translation aborted on. 89–90 stay permanent gaps (see the corrected permanent-gap paragraph above). Both branches independently moved the six `kPowersCount` guard sites to 52; the integrator reconciled the union to **53** (`kTotalCount` 452) per the re-derive rule |


### Wave-A allocations — 2026-07-26, three concurrent worktrees

Published here rather than only in the dispatching briefs, per the rule above.
**Every block is exclusive to its task**; a task that needs fewer ids than it
holds leaves the rest a permanent gap (that is fine and costs nothing — see the
`PowerId` paragraph). A task that needs *more* stops and asks the orchestrator;
it does **not** take the next free number, because its siblings are invisible to
it.

| Namespace | B3.15 remainder (`b315-looter`) | B4.5 (`b45-rewards`) | B3.10a | B3.10b |
|---|---|---|---|---|
| `CardId` | — | — | **92, 93, 95, 97, 99, 100, 102, 103, 105, 106, 107, 108, 110, 111** | **94, 96, 98, 104** |
| `PowerId` | **75–76** | — | **77** | **78** (+79–80 reserve) |
| `MonsterId` | **26** (LOOTER) | — | — | — |
| `MonsterIntent` | **13** (+14 reserve) | — | — | — |
| Opcode | **40–42** | **43–44** | **45–48** | **49–52** |
| `ChoiceKind` | — | **6–7** | — | — |

**The `CardId` blocks are deliberately interleaved, not contiguous.** B3.10's
twenty cards are numbered 92–111 in `CardLibrary.addColorlessCards` **library
(alphabetical) order** (`CardLibrary.java:799-834`), and that order is worth
preserving because the colorless pool B3.10b needs is emitted from it — getting
it right here avoids the documented interim deviation `kIroncladAttackPool`
still carries. So the split left **interior gaps** that B3.10b and B3.10c filled
later — and that worked exactly as intended: `loader.py` enforces id
**uniqueness**, never monotonicity, so the holes cost nothing while they were
open. The last two, **101** (Forethought) and **109** (Purity), were filled by
B3.10c on 2026-07-27; the 92–111 block is now dense.

**What was actually spent** (recorded 2026-07-26, after the wave landed — the
unspent ids below are now **permanent gaps and must never be backfilled**):

| Task | Spent | Left as a permanent gap |
|---|---|---|
| B3.15 remainder | `MonsterId` 26, `PowerId` 75 `THIEVERY`, `MonsterIntent` 13 `ESCAPE`, opcode 40 `ESCAPE` | `PowerId` 76, `MonsterIntent` 14, opcodes 41–42 |
| B4.5 | *(none — needed no new registry id)* | opcodes 43–44, `ChoiceKind` 6–7 |
| B3.10a | 14 `CardId`s, `PowerId` 77 `NO_BLOCK`, opcodes 45–48 `DAMAGE_DRAW_PILE`/`CONDITIONAL_DRAW`/`RESHUFFLE_ALL`/`MADNESS` | — (spent its block exactly) |
| B3.10b | `CardId`s 94/96/98/104, `PowerId` 78 `SHACKLED`, opcodes 49–52 `DARK_SHACKLES`/`DISCOVERY`/`ENLIGHTENMENT`/`RANDOM_COLORLESS_TO_HAND` | `PowerId` 79–80 |
| card-limbo | opcode 53 `USE_CARD` | — |
| B3.11 | `CardId`s 112–126 (all fifteen), `PowerId`s 81–84 `MAYHEM`/`MAGNETISM`/`PANACHE`/`THE_BOMB`, opcodes 54–57 `UPGRADE_ALL`/`RANDOM_CARD_TO_DRAW`/`DRAW_PILE_FETCH`/`DAMAGE_GREED`, `ChoiceKind` 9 `DRAW_TO_HAND`, `ChoiceSource` `DRAW`=4, two default-0 flag bits on opcode 52, `SCHEMA_VERSION` 5→6 | `PowerId`s 85–86, opcodes 58–59, `ChoiceKind` 10. Fuzz `MoveCat` 26 and `CardFlag` bit 15 were unused **contingencies** and are **released to free, not gapped** — bit namespaces are scarce, nothing ever encoded either value, and the permanent-gap rule's "costs nothing" rationale does not hold for bits (recorded in the B3.11 Log) |
| B3.10c | `CardId`s 101/109, `ChoiceKind` 8 `PUT_ON_DRAW_BOTTOM`, `ActionVerb` 4 `CONFIRM`, fuzz `MoveCat` 25 `CHOICE_CONFIRM` (`COUNT` 25→26), `CardFlag` bit 14 `FREE_TO_PLAY_ONCE`, plus two previously-zero `CHOOSE_CARD` `extra` bits it owns outright (14 = optional, 16–19 = the runtime selected-card count) | — (spent its block exactly; no opcode and no `SCHEMA_VERSION` bump were needed) |

`USE_CARD` did not consume the numerically earlier 49–52: B3.10b spent that
exclusive block exactly. Nor did it backfill 41–44, which became permanent
gaps when their owners landed. `PowerId` 79–80 are likewise permanent gaps.

**B3.10c shared allocations — SPENT 2026-07-27, all four, exactly as reserved.**
`CardId` 101/109 (reserved by the original B3.10 split), append-only
`ChoiceKind` **8** `PUT_ON_DRAW_BOTTOM`, `ActionVerb` **4** `CONFIRM`, fuzz
`MoveCat` **25** `CHOICE_CONFIRM` (`COUNT` bumped to 26) and `CardFlag` bit
**14** `FREE_TO_PLAY_ONCE`. `ChoiceKind` 6–7 remain B4.5's permanent gaps; kind
8 extended the packed kind encoding rather than backfilling them. B3.10b owns
`CardFlag` bit 10 plus bits 11–13 as a private three-bit saved-base cost
payload; bit 14 was disjoint. No new opcode was needed — the work extended the
existing `CHOOSE_CARD` machinery — and no `SCHEMA_VERSION` bump: the optional
bit (`extra` bit 14) and the runtime selected-card count (`extra` bits 16–19)
both fit previously-zero bits of the queue item's existing `flags` word.
`CardFlag` bit 15 stays free.

### Wave-B allocation — 2026-07-27, B3.11 (single task, staged pipeline in one worktree)

**SPENT 2026-07-27 — see the spent table above.** Kept for the record of what
was allocated; the spent table is authoritative for what landed and what
gapped. Note for B3.10c: B3.11 stage B landed the ≥ 8 choice-kind packing
(high bit at `extra` bit 9, card-type filter bits 10–12, temp-group latch bit
13) and fixed the old `((kv >> 2) != 0)` packing to `((kv & 0x4) != 0)` —
kind 8 builds on that landed packing rather than re-deriving it.

| Namespace | B3.11 block |
|---|---|
| `CardId` | **112–126**, mapped in `CardLibrary.addColorlessCards` alphabetical order: 112 APOTHEOSIS, 113 CHRYSALIS, 114 HAND_OF_GREED, 115 MAGNETISM, 116 MASTER_OF_STRATEGY, 117 MAYHEM, 118 METAMORPHOSIS, 119 PANACHE, 120 SADISTIC_NATURE, 121 SECRET_TECHNIQUE, 122 SECRET_WEAPON, 123 THE_BOMB, 124 THINKING_AHEAD, 125 TRANSMUTATION, 126 VIOLENCE |
| `PowerId` | **81–86**: 81 MAYHEM, 82 MAGNETISM, 83 PANACHE, 84 THE_BOMB; 85–86 reserve |
| Opcode | **54–59**: 54 UPGRADE_ALL, 55 RANDOM_CARD_TO_DRAW (pool-parameterized, permanent-0-cost, random draw-pile spot), 56 DRAW_PILE_FETCH (Violence), 57 DAMAGE_GREED; 58–59 reserve (e.g. if extending opcode 52's flags for Transmutation proves unclean) |
| `ChoiceKind` | **9** DRAW_TO_HAND (+10 reserve); kind 8 stays B3.10c's |
| `ChoiceSource` | **DRAW = 4** (append after GENERATED=3) |
| fuzz `MoveCat` | **26** contingency only (25 stays B3.10c's); report if used |
| `CardFlag` | bit **15** contingency only; report if used |
| `SCHEMA_VERSION` | **5 → 6**, owner-approved 2026-07-27: `PowerSlot` counter field + non-merging (instanced) power support + `CombatState` in-combat gold accumulator, one bump, one `tools/fixture_gen/` regeneration, one design §11 entry |

**`MonsterState.flags` is a two-region `uint32_t` — allocate from the right
region.** It was a `uint16_t` and ran out (`kMonsterFlagEscaped` took the last
bit); it was widened to 32 bits on 2026-07-26, owner-approved, as
`SCHEMA_VERSION` 4 → 5 (design §11 v0.1.5). **The width was the smaller half of
the fix.** Bits had been handed out **linearly**, a fresh bit per monster type,
though no monster is two types at once — nine Act-1 types burned all sixteen
bits while the worst single type (The Guardian) needs five. Widening alone would
have re-exhausted the word in Act 2. The rule now:

- **Bits 0–23 — TYPE-SCOPED and reusable.** A bit means whatever the owning
  monster type says. Two types may share a bit **only** if no single monster is
  both. Existing constants keep their historical values; do not re-base them.
- **Bits 24–31 — GLOBAL, scarce, orchestrator-allocated.** For flags that
  *type-agnostic* code reads. Today there is exactly one: `kMonsterFlagEscaped`
  (bit 24), read by `monster_dead_or_escaped()` from the queue, interpreter,
  card-play, damage, power-hook, advance and Spore Cloud paths.
- **The power caveat.** A bit consumed by a *power's* native body is scoped to
  that power's possible **owner set**, not to one monster type — the CurlUp and
  Ritual latch sites key on `PowerId`, not `monster_id`. A type reusing such a
  bit must also never be able to own that power.

**When widening a flag field, hunt the truncating writes.** The u16→u32 change
found several `static_cast<uint16_t>` flag writes that would have **silently
cleared the whole global region** — quietly erasing `Escaped` — on every write.
The compiler cannot help: narrowing through an explicit cast is what the cast
asks for. They were removed across eight source files and two tests.

**Three shared-file rules this wave, two of which a brief got wrong once:**

1. **`tools/registry_gen/stsgen/vocab.py` holds two different namespaces and they
   have different rules.** `MONSTER_INTENTS` is **exclusive to the B3.15
   remainder** this wave. `OPCODES` (`vocab.py:17-119`) is **shared and
   append-only within each task's allocated block** — every wave-A task holds an
   opcode block, so a blanket "vocab.py is off limits" makes the allocation
   unspendable. Name the *namespace*, never the file.
2. **Adding an opcode is a three-file change, not one.** Besides `vocab.py`'s
   `OPCODES`, it requires `tools/registry_gen/stsgen/steps.py` — whose
   `assert _GROUPED == set(OPCODES)` hard-fails on an unclassified opcode, so
   **every** opcode-holding task touches it — and `include/sts/engine/cards.hpp`'s
   drift `static_assert`. Brief all three together (the B3.17 precedent).
3. **`registry/powers.yaml` is shared**, append-only within disjoint blocks — the
   B3.8 ∥ B3.27 precedent. The hazard is not the YAML, which appends at different
   places: it is the **count guards**, which every adding branch must move to
   compile, so each carries a *different* correct-for-itself value and the merge
   can land either silently. That is integration-14's defect exactly. The
   integrator **re-derives the union from the regenerated manifest**; no branch's
   number is authoritative.

**Count-guard site inventory** — re-derived 2026-07-28 (the 2026-07-26 table
had gone stale in every line number AND was missing a member of both families).
Move every site in a family together, and **re-grep this list at dispatch
time** — it goes stale exactly like the values do:

| Constant | Sites (2026-07-28) |
|---|---|
| `kPowersCount` | `interp_block.cpp:78`, `interp_damage.cpp:100`, `:174`, `:242`, `tests/registry_gen_test.cpp:487`, **and the DERIVED `kTotalCount` at `tests/registry_gen_test.cpp:570`** — its comment names itself a member of both families even though it quotes neither constant |
| `kCardsCount` | `tests/registry_gen_test.cpp:453`, `tests/registry_gen_standalone.cpp:38`, `src/engine/relic_pools.cpp:278`, **and `kTotalCount` at `tests/registry_gen_test.cpp:570`** |

`relic_pools.cpp:207`'s assert message asks a **real question** — whether the new
rows change Bottled Tornado's gate — so it is answered, not bumped. (For B3.10's
twenty: none is BASIC or POWER-type, so the gate is unaffected.)

## Landed non-task work

- **Wave-C integration: the two-track union proven on `wave-integrate`** `[x]`
  — merges `03c681d` (`wave-runlayer`, 10 commits) and `0f6708a`
  (`wave-combat`, 20 commits) onto master `7df27ab`, plus the consolidation
  commit `5f87ac6` and the `<sstream>` portability fix `8d6cbae`. NOT yet on
  `master` — the G6 gate agent owns it; landing is the orchestrator's.
  **The two couplings a conflict-free merge would have got wrong**, both
  RED-first on the union: (1) `queue_innate_overflow_draw` now thresholds on
  `game_hand_size` — `CardGroup.initializeDeck:951-953` reads
  `player.masterHandSize`, the field Snecko Eye enlarges (`SneckoEye.java:31`)
  and `preBattlePrep` snapshots (`AbstractPlayer.java:1579`) — pinned by
  `RunCombatBottle.SneckoEyeRaisesTheInnateOverflowThresholdWithTheDraw` (+ the
  8-top-placed overflow-by-one twin); (2) rest-room entry composes Feather
  (onEnterRoom fan-out, `AbstractDungeon.java:1755-1757`) → Tea Set arming
  (`RestRoom.java:39-41` at `:1800`) → empty-campfire auto-complete last
  (`CampfireUI.java:97-104`), pinned by
  `RestSites.FeatherHealsAndTeaSetArmsEvenWhenTheCampfireAutoCompletes` — both
  hooks fire even on a campfire the player never sees. Three-way surfaces
  reconciled: `emit/cards.py` (master's `upgraded:` guard + both new pools),
  `command_map.hpp` (master's G6 grid-stop classifier kept, its deferred-whole
  `onEquip` set RE-DERIVED for the union as ORRERY / DOLLYS_MIRROR / CAULDRON —
  the five boss bodies are live, so master's five-boss list would have been the
  misattribution its own fix removed), `kTotalCount` re-derived as 450 (neither
  branch's number was authoritative). Landed the two file-contention
  consolidations the stages deferred (heal-clamp trio → `heal_out_of_combat`;
  Toke bottled gate folded into `build_rest_menu`). **Verification**: WSL
  `debug` / `asan` / `release` all PASS, 0 failed (clean per-preset builds in a
  fresh worktree); `fuzz_soak --seeds 300` failures: 0; the full 12-artifact
  offline replay corpus reproduces the better of its per-track verdicts with
  zero divergence everywhere — STS00042/43's track-2-era `energyMaster`
  divergences are GONE on the union, and the three remaining stops are the
  documented `SHOP_ROOM` mapping frontier (verdict table:
  [`wavec_track2_replay_triage.md`](../tools/oracle_bridge/driver/wavec_track2_replay_triage.md)).
  `win-debug` builds clean after `8d6cbae`; its six remaining test failures
  reproduce byte-for-byte at the fork commit `09f8847` (pre-existing win-preset
  rot, reported, not chased). **RESOLVED 2026-07-28 on `wave2-harness` stage 1
  — and one of the six was not rot.** Five were the host-shell family
  (`std::system` + POSIX quoting + `>/dev/null` + WSL's `bash` under `cmd.exe`),
  eliminated into `tests/host_shell.hpp` per §7 of
  [conventions.md](conventions.md); fixing the quoting then exposed a REAL hang
  (`fuzz_soak`'s deliberate abort waiting on a Windows Error Reporting dialog),
  fixed by `sts::fuzz::make_crashes_headless()`. The sixth,
  `Translator.RoundTripDeterministic`, was **not environmental**: `RunState`
  carried two undeclared alignment gaps, and neither `RunState{}` nor a
  memberwise copy is required to write them — a latent defect under every
  `memcmp` of the struct that Linux's zeroed pages had been hiding. Both win
  presets now pass the full suite. **PARKED, honestly** — nothing below is silently
  dropped: Red Skull `atBattleStart`'s anonymous-inner-class action (needs an
  oracle capture; row open) — **annotated 2026-07-28: un-parked as far as it
  can be without a capture. The relic's whole body landed on this branch under
  an OWNER-PROVIDED spec (project owner, 2026-07-28), the `onNotBloodied` half
  of it straight from `RedSkull.java:54-63`, which turns out to be perfectly
  decompilable — the parking was over-broad. The `:38` action's `+3` on an
  already-bloodied entry is the one piece still resting on the owner spec, so
  the obligations row stays open, narrowed to capture VALIDATION** — the
  Courier restock (decision memo + capture-first,
  B4.8 runbook §4; row open), the Discovery tick-regen question (row filed by
  track 1 stage 4, owned by the next capture-campaign owner), the fork-redeploy
  + bottle-taking capture (row filed by track 2 stage 3d, same owner), the
  `SHOP_ROOM` `--replay` mapping arm (ledger row "replay generalized" owns it;
  master's `badc58f` did not add it — **DONE, `wave2-harness` stage 2**), and the
  six pre-existing `win-debug` test failures (**DONE, stage 1 above**) plus four
  pre-fork stale "does not exist yet" comments
  (`shrines.cpp:97`, `monster_looter.hpp:68`, `run_state.hpp:177`, Frozen Egg's
  provenance) left for a repo-hygiene pass — all four predate `09f8847`.

- **Wave-C track 1, stage 1: three live divergence fixes** `[x]` — branch
  `wave-combat` (commits `239885c`, `b4faf1c`, `f9fab90`), each RED-first with
  the failing tests named in its commit body. Orchestrator-sanctioned
  stop-the-line fixes; no design section asserted any of the old behaviours
  (checked stage-a §5.2/§5.5 and stage-b), so no design change-log entry.
  - **Power-list ordering** (`239885c`): `ApplyPowerAction.java:167` runs
    `Collections.sort(target.powers)` after every NEW power lands — stable,
    priority-major (`AbstractPower.compareTo` :366-368, default 5 :66) — and
    the engine appended without sorting, so a player Weakened before gaining
    Strength computed `(base×0.75)+S` where the game computes `(base+S)×0.75`.
    `PowerDef` now carries `priority` (mirrored ctor overrides: WEAK 99,
    FRAIL 10, INTANGIBLE 75, CONFUSION 0 — the complete override set for
    registered powers) and `op_apply_power`'s new-slot path ends with the
    stable re-sort. The stacking branch does not re-sort and
    `AbstractCreature.addPower` ports (Philosopher's Stone) are unaffected,
    matching the Java. **Fixture impact: none** — the 20 frozen fixture
    traces replay unchanged (no captured trace holds an out-of-order pair).
    This is the ordering prerequisite the Pen Nib obligations row needs
    (`PenNibPower.priority = 6` sits between the default-5 powers and
    Frail/Intangible/Weak).
  - **Red Skull entry semantics** (`b4faf1c`): the game pre-seeds
    `isBloodied = currentHealth <= maxHealth / 2` in `preBattlePrep`
    (`AbstractPlayer.java:1575`), so a combat ENTERED at/below half HP never
    fires the damage-side onBloodied cross; and `RedSkull.atBattleStart`
    resets `isActive = false` every combat (`RedSkull.java:37`). The engine
    granted +3 Strength on the first HP loss of an entered-bloodied combat,
    and its suppression latch persisted across combats. RED_SKULL's row now
    binds `at_battle_start: []` and the native body seeds the latch from
    starting HP. **NOT taken:** the anonymous action `atBattleStart` queues
    (`RedSkull.java:38`) stays the recorded undecompilable deferral, and the
    `onNotBloodied` heal-cross obligations row stays open. The row's stale
    citations (`:48-58`/`:60-67`/`:41-45`) were corrected in the same commit.
    > **Annotated 2026-07-28 (Red Skull body, this branch).** Both "NOT taken"
    > clauses have since been taken, and the second one should never have been
    > parked: `onNotBloodied` (`:54-63`) is fully decompilable, so it needed no
    > new evidence — only a caller, which is now a hand-written fan-out off
    > `heal_player_with_relics` / `heal_out_of_combat`. The `:38` action's body
    > is still not decompilable and is now implemented from an OWNER-PROVIDED
    > spec (project owner, 2026-07-28): entering combat already bloodied grants
    > the same +3. The fix this bullet records — no spurious grant on the first
    > HP loss of an entered-bloodied combat — is unchanged and still pinned;
    > its test now reads "exactly one grant" rather than "no grant", which is
    > the property the damage-side suppression was always protecting.
  - **Entropic Brew** (`f9fab90`): `EntropicBrew.use` checks Sozu BEFORE any
    roll out of combat (`EntropicBrew.java:43-45`), rolls `limited=false`
    out of combat (`:46-48`, the no-arg `returnRandomPotion`,
    `AbstractDungeon.java:825-827`), and in combat rolls `limited=true`
    un-gated with each obtain Sozu-suppressed at resolve
    (`ObtainPotionAction.java:29-38`). The engine had no Sozu check and
    hard-coded `limited=true` in both phases — RNG-visible, since the
    limited loop spends a different draw count. Surgical to
    `use_entropic_brew`; the rest of `run_advance.cpp` untouched (Track 2's
    surface). Caution recorded for future potion tests: the two `limited`
    flags OFTEN coincide on a given seed (a tier-mismatched candidate makes
    the limited discard overlap the rarity rejection) — the replacement test
    hunts a distinguishing seed before asserting, because the old test's
    fixed seed pinned the wrong flag without noticing.

- **`--replay` triage of b45 campaign 1 (STS00042-46)** `[x]` — branch
  `triage-sts00042`. Discharges the **"STS00042 replay stop at seq 32 —
  untriaged"** obligation row; full read-out in
  [`b45c1_replay_triage.md`](../tools/oracle_bridge/driver/b45c1_replay_triage.md).
  - **The row asked the wrong question, because the stop is not the frontier.**
    STS00042 diverges at **seq 18**, fourteen records before the seq-32 message,
    and the cause was already on this table: Neow's boss swap hands the run
    **Philosopher's Stone**, whose `onEquip` is
    `++AbstractDungeon.player.energy.energyMaster`
    (`PhilosopherStone.java:55-58`) — one of the ten rows in `relics.yaml`'s
    shared ENERGY MASTER deferral. The relic's other half is LIVE and matches:
    `atBattleStart`'s +1 Strength (`:41-48`) gives the sim's Cultist the same
    `Strength 1` the capture shows. The fingerprint is `player.energy: 4 -> 3`
    on the FIRST combat record (seq 4, before a card is played; Ironclad's base
    is 3), which makes seq 6's 2-cost Bash unaffordable in the sim — so the
    Cultist keeps 8 HP and its Vulnerable, the sim's floor-1 fight never ends,
    and seq 32 is merely the first multi-option event page handed to a
    controller still parked in COMBAT. **There is no event/combat-boundary
    defect, and none is ruled in either**: the sim never left floor 1, so the
    artifact does not exercise that boundary at all.
  - **Five-run frontier table** (`b45_rewards_oracle_20260727T204809Z_claude01`):
    STS00042 Philosopher's Stone, 33 records, first diff seq 18, deferred
    `energyMaster`; STS00043 Fusion Hammer (`FusionHammer.java:47-49`), 67
    records to its terminal, first diff seq 15, same deferral; **STS00044
    CLEAN** — zero-diff to terminal, and the control that makes the attribution
    an observation rather than a story (same campaign and policy, no
    `energyMaster` relic); STS00045 and STS00046 both stop at seq 2 on **Empty
    Cage**'s deferred `onEquip` grid, with seq 0-2 zero-diff so the acquisition
    is proved. **No real engine divergence and no stop attributable to the
    mapping table** — the same place the six-run campaign-2 table landed.
  - **Harness, reporting only.** A stop reason now names the sim's phase instead
    of a bare enum ordinal (`phase_name` moved from `main.cpp` into
    `command_map.hpp`, shared with the `DIFF` line) — the row above had to gloss
    "3 [COMBAT]" before it could ask anything. And `--replay`'s summary now
    prints the **first divergence** beside the stop, because "why did the replay
    end" is a different question from "where did the two sides first disagree",
    and reading the first as the second is what produced this row. It says
    `none` out loud too, which is what separates STS00045/46 (stopped early,
    zero divergence — a coverage limit) from STS00043 (ran to terminal, diverged
    at seq 15). Named tests in `replay_command_map_test`:
    `AnEventDesyncStopNamesTheSimsPhaseRatherThanItsOrdinal`,
    `AnUnsimulatedGridStopAlsoNamesThePhaseRatherThanItsOrdinal`,
    `EveryRunPhaseHasAName`. The campaign-2 six-run table is byte-for-byte
    unmoved.
  - **Nothing was added to this table.** Every divergence found resolves to an
    existing row (the ten `energyMaster` relics; the five boss `onEquip`
    bodies), so there is no new obligation and no stop-the-line.
  - A mapping nuance is recorded in the read-out, deliberately left: the EVENT
    branch's one-button-page elision also swallows records when the sim is
    desynced into COMBAT. Tightening it to `MAP_CHOICE` would truncate desynced
    replays earlier and cost the per-record diffs this triage depended on; the
    new first-divergence line removes the reason it mattered. The next mapping
    owner decides it deliberately.
  - **Capture-planning note:** at A20 under `random-legal`, 3 of these 5 runs
    took a Neow boss swap into a deferred-body relic (two `energyMaster`, two
    Empty Cage). A campaign meant for `--replay` fidelity should expect roughly
    half its runs to be un-replayable for that reason alone — steer the policy
    or the seed list around the boss swap.

- **Centennial Puzzle: the counter that never was** `[x]` — branch
  `fix-centennial-counter`. Discharges the **"Centennial Puzzle carries a
  persistent `counter` the game does not"** obligation row. `relics.yaml`'s
  `CENTENNIAL_PUZZLE` row drops `initial_counter: 0` and takes the `-1`
  default, matching `AbstractRelic`'s untouched counter — `CentennialPuzzle.java`
  never writes `this.counter` (`:21, 33-49`), so translated `RunState` now
  matches a capture at every point and the STS00068 signature
  `relics[1].counter: -1 -> 0` cannot recur from any acquisition source. The
  once-per-combat flag moved to `kCombatFlagCentennialPuzzleUsed`,
  previously-zero bit 4 of `CombatState.flags` (`combat_state.hpp`) — the exact
  analogue of the game's **static** `usedThisCombat`, whose `atPreBattle` reset
  (`:33-34`) is reproduced structurally by `enter_combat`'s fresh
  value-initialized `CombatState`, so a second combat in one run re-arms (it
  never did before: there was no reset at all). The draw is **3 cards**
  (`NUM_CARDS = 3`, `:20, 44`). No struct grew; `SCHEMA_VERSION` untouched (the
  B3.10c previously-zero-bits precedent). The row's own provenance was also
  corrected: it claimed a "`this.counter` gate", which is what the defect was
  built on. Tests, all five verified RED against the old behavior:
  `RelicAcquisition.CentennialPuzzleKeepsAbstractRelicsMinusOneCounter`,
  `RelicHooks.CentennialPuzzleDrawsThreeOnFirstHpLoss` /
  `.CentennialPuzzleDoesNotFireTwiceInOneCombat` /
  `.CentennialPuzzleReArmsWhenTheCombatStateIsFresh`,
  `RunCombatWasHpLost.CentennialPuzzleReArmsInASecondCombat`. One nuance
  recorded: `CombatState.flags` is walked by the replay tool's opt-in combat
  triage print (never a pass/fail signal, `replay/src/main.cpp`), same as the
  existing Mugged/CannotLose/PlayerEscaped bits — no new class of noise. The
  test-helper gap this fix exposed is filed as its own row above.

- **Event transform pool order: the second rendering is gone, not corrected**
  `[x]` — branch `fix-event-transform-pool`. Discharges the
  **"`kEventTransformRedPool` is emitted in registry-iteration order"**
  obligation row. Fixing the emitted order would have left two independent
  expressions of one Java method (`returnTrulyRandomCardFromAvailable`,
  `AbstractDungeon.java:1016-1045`), which is the drift that produced the bug —
  B4.14 fixed `neow.cpp`'s copy and the emitter's copy stayed wrong. So
  `emit/events.py` no longer emits `kEventTransform{Red,Colorless,Curse}Pool`
  or `event_transform_color` at all: their membership was already exactly
  `kIronclad{Common,Uncommon,Rare}Pool` / `kColorlessPool` / `kPoolableCurses`,
  only in a third order. `neow.cpp`'s `transform_card` moved to
  `card_pools.hpp` as the ONE authority (split into `transform_card_list` + the
  draw so the order is pinnable without a seed) and `event_grid_transform_card`
  calls it; only the `Random` differs between the two call sites. **The
  ready-made check passed: STS00051 of
  `b45_rewards_oracle2_20260727T204809Z_claude01` now replays `CLEAN` to its
  terminal** — `69 records compared (6 on reward screens), 0
  library-order-only`, where it read `19 library-order-only` before; the
  19-record `master_deck[11].card_id: Havoc(8) -> Iron Wave(18)` diff and its
  downstream floor-4 `hp` gap are both gone, and `--replay` over all eight
  replayed b45 runs reports 0 library-order-only everywhere. Pinned by
  `CardPoolLibraryOrder.TransformCardListIsCommonsThenBothSrcPoolsBackwards`
  (seed-free, all three blocks position by position) and
  `LivingWallCapture.STS00051FloorTwoProducesHavoc` (the capture's own
  `miscRng` state, one draw, Havoc). **A second order bug of the same kind was
  forced out by the removal and fixed with it:** `shrines.cpp`'s
  `draw_colorless_uncommon` was using the deleted colorless array as a stand-in
  for the LIVE `colorlessCardPool` that `returnColorlessCard` shuffles
  (`AbstractDungeon.java:1100-1113`) — `addColorlessCards` fills that pool with
  the APPENDING `addToTop` (`:1203-1210`), so it is plain library order, and
  the draw now reads `kColorlessPool` (the `src*` twin) BACKWARDS. Membership
  unchanged; Match and Keep is the only consumer.

- **G6 replay-harness class (b) gaps** `[x]` — branch `replay-neow-exit`
  (badc58f). Closed the five class-(b) findings the G6 campaign filed against
  `tools/oracle_bridge/replay/` (`g6_campaign_spotdiff.md` §8.2/§8.3/§8.4/
  §8.7/§8.8-STOLEN_GOLD); §8.4 proved to be TWO distinct index-space gaps
  (disabled event buttons occupy `options[]` ordinals but not `choice_list`;
  Match-and-Keep picks index screen positions, not board slots), so six fixes
  landed, each with a RED-first test. Highlights: Neow's potion-reward
  `proceed` maps to two `kChooseProceed`s (one game frame crosses
  ITEM_REWARD→DONE→MAP, RNG-free, both captures confirm); the Match-and-Keep
  board invariant now admits the LEGAL double-curse quadruple
  (`GremlinMatchGame.java:70-71` draws `returnRandomCurse()` twice with no
  dedup) while two quadruples stay impossible; a grid stop can no longer
  blame the starting Burning Blood (the deferral claim is made only for the
  five whole-deferred boss bodies); and the reward mode seeds a Looter's
  stolen-gold amount from the capture's own row — one number seeded, named
  and counted separately in the output, everything else still proved. The
  runbook's §8.3 frontier misattribution corrected in the same change
  (conventions §4). Corpus sweeps byte-identical; g6 `--replay` 28→27 not
  clean, `--event` 47/47 with 12/12 deals, reward mode 6→3 failing files.
  STS01372's floor-7 `treasure_rng` residual stays unattributed in the
  runbook (escaped-thief branch suspected, unconfirmed).

- **The closing wave: discovery honesty, the last two potions, arm 3** `[x]` —
  branches `fix-discovery-duplication` (082a1b4 → ce9efcd → 2103bc1) and
  `add-regenerate-monster` (5e3ae13), reconciled at integration (`PowerId`s
  91 + 92, guards to the union 53/452). Discharged the discovery
  per-tick-regeneration ambiguity BY CAPTURE (1 + 5 generates, deterministic
  under the fork's pinned frame step — an oracle-contract model boundary),
  added the typed-discovery SKIP path (ending the corpus's last class-(a)
  soft-lock, STS00221), landed Distilled Chaos (no new opcode — the "later
  opcode" premise was dead) and Duplication (emptying the deferred-potion
  set), and un-parked the emerald-elite arm 3 with `REGENERATE_MONSTER`.
  Full evidence in the four discharge rows above and the branch commit
  messages. Corpus after the wave: zero translation errors, zero class-(a),
  every stop classified.

- **The starter cards that never upgraded** `[x]` — branch
  `fix-starter-upgrades` (fabb9d0). The G6 campaign's one live class-(a)
  divergence: `cards.yaml` ids 1-5 carried no `upgraded:` block, so
  `cards.hpp`'s fallback returned the BASE program for an upgraded instance —
  Strike+ dealt 6 not 9 (`Strike_Red.java:57-62`), Defend+ blocked 5 not 8
  (`Defend_Red.java:43-48`), Bash+ 8/2 not 10/3 (`Bash.java:54-60`), Shrug It
  Off+ 8 not 11 (`ShrugItOff.java:43-48`), Pommel Strike+ 9/1 not 10/2
  (`PommelStrike.java:44-52`). The justifying comment ("until CardDef gains
  the upgrade dimension (a later task)") predated the dimension 106 of 126
  rows already used — the conventions §8 stale-comment trap, third instance
  this stage. Every upgrade taken in all 30 campaign runs landed on one of
  the five. Fixed from the Java with RED-first `CardTableUpgraded.*` tests
  (incl. the STS01068 arithmetic reproducer); three tests that PINNED the
  wrong behavior fixed RED-first; and the class is closed structurally — the
  generator now fails loud on any non-STATUS/CURSE row without `upgraded:`
  (exemption set = `AbstractCard.canUpgrade`, `AbstractCard.java:672-680`;
  STATUS/CURSE may still author one, as Burn does). Live A/B: STS02041
  zero-diff to its documented stop; STS01068 → the documented Liquid
  Memories frontier; **STS02002's unexplained 1-HP diff proved to be the
  same bug via a third route** (Blessing of the Forge's in-combat Defend
  upgrades — master deck stays base, which is why triage missed it) and is
  now CLEAN to terminal. Zero class-(a) frontier rows remain; the one new
  residue (STS00509) is filed above. Campaign re-run under a new id still
  required before ticking the G6 leg.

- **Capture-driver heartbeat hardening** `[x]` — branch `driver-heartbeat`
  (32f4d7a, DRIVER_VERSION b1.4.7). The G6 campaign twice lost a seed
  attempt to the orchestrator killing a HEALTHY game at exactly
  `stall_timeout`: an unreadable heartbeat sample fell back to
  `now - launch_started`, making the staleness guard compare the same
  expression — one bad read looked like a stall since launch. The heartbeat
  is now written atomically (temp+fsync+rename, mirroring
  `campaign_progress.json`), and an unreadable sample only counts toward a
  kill after 3 CONSECUTIVE bad polls (a readable sample resets the streak;
  the true-stall path fires unchanged). RED-proven: the lone-bad-sample kill
  reproduced verbatim against the old code. Full defect narrative in
  `g6_campaign_spotdiff.md` §9/§13.

- **The victory terminal and the shop that was never counted** `[x]` — branch
  `fix-postboss-shop` (6d7efc4 + 690585a). Two defects surfaced by a 300-seed
  `always_event` fuzz probe, the first fuzz coverage ever to reach the Act-1
  boss (B5.1's acceptance predates every depth improvement).
  - **Act-1 completion was unimplemented.** `step_one`'s COMBAT_REWARD proceed
    branch unconditionally routed to `MAP_CHOICE`; after a boss win the boss
    column has no outgoing edges, so `legal_actions` returned an all-false
    mask in a non-terminal phase (`no_legal_moves`). Contract established from
    the frozen design + the Java before coding: stage-b-design §1.1 ends the
    run when the Act-1 boss's rewards are claimed (boss chest / act transition
    are S2 — the game's same press goes to `goToTreasureRoom`,
    `ProceedButton.java:111-113, 179-187`, never the map). Fix: proceed from a
    Boss-room reward screen → `RunPhase::RUN_OVER` with `terminal=true,
    reward=+1.0f`; victory is `combat_outcome=KILLED && room_type=Boss`,
    spelled once as `run_is_victory()` (`run_advance.hpp`). **No new RunPhase
    value was spent — the namespace table stays 0-10.** Coverage gained a
    `victories` counter so wins aren't filed as deaths (see the new
    obligations row for the archived-kv consequence).
  - **The "shop reachability regression" was a counting bug.** Shops were
    entered all along — the probe's own move table shows `shop 506/506` moves
    taken, and SHOP moves only enumerate inside `RunPhase::SHOP` — but
    `execute()`'s room-entry accounting (`tools/fuzz/src/fuzz_run.cpp`) was
    never taught B4.8's phase: pre-B4.8 shops were counted via the
    ROOM_UNIMPLEMENTED arm, which is exactly why the regression window
    matched B4.8's landing. Fix: SHOP added to the phase list. Post-fix
    probe: shop entered 196/300 runs, `deaths 299 victories 1`, failures 0.
  - RED-first (4/1422 failed pre-fix, all new: `BossVictory.*` ×2,
    `FuzzGuard.Seed116AlwaysEventReachesTheVictoryTerminal`,
    `FuzzCoverage.ShopEntryCountsInTheRoomsTable`); the seed-116 reproducer
    now replays NOT-REPRODUCED (the fixed-build pass verdict, exit 1 by
    design); probe trajectories byte-identical through step 131 pre/post
    (28259 actions both runs) — the fixes alter no RNG or policy stream, only
    the terminal and the bookkeeping. The stale `EVENT_DIALOG` comment row
    was discharged in the same branch (690585a) by its named owner
    assignment. Three-preset suite green on the branch; re-derive counts from
    `ctest -N`, never from this entry.

- **`seed_scan`: the capture seed pre-scanner** `[x]` — branch `seed-prescan`.
  Campaigns picked seeds blindly (`STS%05d` sequential), so rare capture
  targets were a lottery. Event selection is a pure function of `RunState`
  (`generate_event`, `event_framework.cpp:359-395`, fired record at `:392`),
  so a full A20 run costs microseconds and the question "can this seed's Act 1
  contain X" is answerable before the game is driven at all. New
  `tools/oracle_bridge/planner/` — `seed_scan_core` + the `seed_scan`
  executable: scans (seed range) × (policies) × (policy seeds) through
  `fuzz::run_case`, recording per case the seed string + int, policy, policy
  seed, end reason, max floor, decoded `event_flags`, treasure entry and boss
  reach; TSV/JSONL results plus a capture-ready seed list under
  `--need-event` / `--need-treasure` / `--need-boss` / `--min-floor` /
  `--min-hit-count`. **`--min-hit-count` is the design point**: the capture
  runs a DIFFERENT policy from the scan, so a single-combination hit means
  "reachable", not "will be reached" — Match and Keep! qualifies 152 → 91 →
  36 → 9 seeds at k = 1..4. Acceptance run: STS00100-STS05099 ×
  {random, greedy_damage} × {0,1} = 20,000 full runs in 6.58 s; Match and
  Keep! on 1.44 % of rows; **91 candidate seeds at k=2** (plus 128 treasure
  k=2 and 28 boss-reach k=1), lists under
  `D:\STS_BG_Mod\_oracle_data\seed_scan\`. `--verify-determinism` zero
  mismatches and a cross-process re-run byte-identical. The run loop was NOT
  forked: `fuzz::run_case` gained one optional pass-A-only `StepObserver`
  (passes B/C never call it, so an observer cannot perturb the determinism
  comparator), and a `static_assert` ties the planner's event-name table to
  the generated `kEventTable` size so a new `events.yaml` row fails
  compilation rather than reporting unknown. Two port disagreements recorded
  in `SeedString.CampaignDriverVectorSTS12345` (the C++ codec already existed
  in `seed_string.hpp`, contrary to the dispatching brief — the test adds the
  cross-port pin `STS12345 → 1790052133945` and notes Python folds
  out-of-alphabet to index 0 where the Java/C++ use −1, unreachable for real
  seeds). The stale `EVENT_DIALOG` comment it caught is filed as its own row
  above. `seed_scan` is single-threaded on purpose: ~3,000 rows/s makes a
  100k-seed sweep ~2 minutes, not worth the determinism surface threading
  would add.

- **Greedy live-capture policy + script argument validation** `[x]` — branch
  `greedy-driver-policy`. Added `--policy greedy`
  (`tools/oracle_bridge/driver/greedy_policy.py`, plumbed through
  `campaign_driver.py` and `orchestrator.py`): the same `expand_legal_actions`
  expansion as random-legal, ranked by a pure scorer that mirrors
  `tools/fuzz/src/policy.cpp` `move_score`/`score_card` in combat (lethal
  first; `damage*4+block` unthreatened, `block*4+damage` under an attack
  intent; focus fire +2; `end` last) and **inverts** its map weights
  (non-combat > monster > elite, boss when offered) because depth, not fight
  variety, is the goal. Screen defaults claim relic/gold/potion and never a
  `SAPPHIRE_KEY` row — `RewardItem.claimReward` (`RewardItem.java:255-330`)
  case 6 sets `relicLink.isDone/ignoreReward`, retiring the RELIC row
  ungranted. Per-card numbers live in the committed `cards_sidetable.json`,
  derived from `registry/cards.yaml` by the committed `gen_cards_sidetable.py`
  (registry_gen writes into the build tree, is never committed, and needs
  PyYAML, which the stdlib-only Windows-host driver cannot have);
  `CardSideTableTest` re-derives it and fails on drift. Added `cmd_args_ready`
  beside `cmd_verb_ready`: script mode now ends as the named divergence
  `cmd_arg_invalid` instead of firing a mis-indexed `choose`/`play` into the
  8-error budget; `choose <name>` and absent collections pass through.
  `DRIVER_VERSION` b1.4.4 → b1.4.5 (in-flight campaigns refuse to resume, by
  design — greedy campaigns start under a new `--campaign-id`). Acceptance:
  `python -m unittest test_oracle_campaign` on the Windows host — 93 tests, 0
  failures/errors/skips, including a replay harness that pushes all 1626
  in-game states of the 30 `b47_treasure_oracle_20260727T204809Z_claude01`
  runs through the policy and asserts every emitted command is legal on that
  state, plus coverage-floor asserts so it cannot pass vacuously. **No live
  leg run yet** — first greedy leg must raise `--max-actions` /
  `--campaign-timeout` for deep runs, and the never-enter-shop /
  never-take-card defaults should be re-checked against the depth they
  actually buy (a greedy campaign yields no SHOP_SCREEN captures, by design).

- **NoteForYourself's NOTE_CARD / NOTE_UPGRADE pin: confirmed by direct
  profile inspection, not a capture** `[x]` — discharges the
  **"NoteForYourself `NOTE_CARD` / `NOTE_UPGRADE` player-profile pin"**
  obligation row on stronger evidence than the capture it asked for.
  `CardCrawlGame.playerPref = SaveHelper.getPrefs("STSPlayer")`
  (`CardCrawlGame.java:221`), and **no file in the reference install's whole
  preferences directory contains a `NOTE_CARD` or `NOTE_UPGRADE` key**
  (checked 2026-07-27 across every file in
  `D:\SteamLibrary\steamapps\common\SlayTheSpire\preferences\`), so the Java
  defaults — `getString("NOTE_CARD", "Iron Wave")`,
  `getInteger("NOTE_UPGRADE", 0)` (`NoteForYourself.java:98, 103`) — apply,
  which is exactly the engine's pin (`one_time_specials.cpp`). A capture could
  never have read this out anyway: `isNoteForYourselfAvailable` is false at
  `ascensionLevel >= 15` (`AbstractDungeon.java:1360-1379`;
  `event_framework.hpp` collapses it to `ascension < 15` under the pinned
  profile), the capture pipeline is A20-only, and S1 itself is A20 — so the
  event is unreachable in both game and sim within S1's domain, and the only
  `NOTE_*` write site (`AbstractDungeon.java:1709-1710`, the note-giving path)
  can never execute in an S1 run. No modelled write is needed; B4.13's pending
  capture leg shrinks to Match and Keep's deal alone.

- **Replay harness: the potion belt, the grid buffer, and a grid the sim never
  opened** `[x]` — discharges the **STS00052 shop screen `potions[0]
  FearPotion` diff** obligation row (it was never a stock divergence) and closes
  the last three `--replay` gaps the B1.6 row named. All six b45 reward runs now
  either reach their terminal or stop with a **named deferred body**; none stops
  on a missing mapping any more.
  - **The STS00052 verdict: a harness artifact, and not on the screen the row
    named.** `replay_run_diff --shop` on STS00052 reports **`STOCK OK`** — the
    whole floor-2 merchant reproduces: all seven card ids, prices and upgrades,
    three relics, **all three potions in order (Gambler's Brew, Explosive
    Potion, Fear Potion)**, the purge cost, `purge_available`, the
    independently-inferred sale slot, and `merchantRng 0→16` / `cardRng 9→21` /
    `potionRng 3→10` with the relic pools and `cardBlizzRandomizer` alongside.
    Seven `potionRng` draws for three potions is the RARITY-GATED
    `AbstractDungeon.returnRandomPotion` (`:824-850`) that
    `ShopScreen.initPotions` calls — three d100 tier rolls plus four
    `PotionHelper.getRandomPotion` picks, one of them a trap-14 rejection — and
    not the flat `getRandomPotion` an event site uses, which would have taken
    three. Both the gate and the draw count reproduce exactly, so the merchant's
    potion machinery is not implicated at all.
    The `potions[0]` the row quotes is not a shelf slot at all — it is the
    PLAYER's belt, and the diff surfaced in the **purchase walk**, at seq 50:
    `potions[0]: NONE(0) -> FearPotion(12)`. The run bought that Fear Potion at
    seq 43 (`choose 9`, 56 gold) and **threw it away at seq 49** with `potion
    discard 0`, on the map screen over the shop room. The walk's MAP branch
    treated every non-`choose` map command as an ignorable UI bounce, so the
    discard was skipped and the belt disagreed for the rest of the visit.
    **Verdict: category (b), a harness gap — and specifically the
    out-of-combat potion discard the B1.6 row already carried. B4.8's `[x]` is
    untouched; nothing about stock generation is implicated.** It is now fixed
    rather than merely classified, and the b47 read-out B4.8's `[x]` rests on
    gets *stronger* with it: all five merchants of STS00054 / STS00057 /
    STS00074 now walk **end to end** clean (5/5 stock, **5/5** purchase walks,
    previously 3 walks clean and 2 stopped at this same discard).
  - **The run layer gained a discard door, because it genuinely had none.**
    `ActionVerb::DISCARD_POTION` (appended, value 5) plus
    `RunActionMask.can_discard_potion[]`, dispatched beside `USE_POTION` ahead
    of the phase switch — the belt is RunState-owned inventory the top panel
    exposes on every screen, so it belongs to no single phase. The body is one
    line, and that is the whole of the Java:
    `CommandExecutor.executePotionCommand` runs `potion.use` and the relic
    `onUsePotion` fan-out on the USE branch **only**, and both branches end at
    `topPanel.destroyPotion(slot)` = `potions.set(slot, new PotionSlot(slot))`
    (`TopPanel.java:529-531`). No RNG, no stream, no hook — pinned by
    `RunPotionDiscard.ToyOrnithopterDoesNotHealForAPotionThrownAway`. Legality
    is `AbstractPotion.canDiscard` (`AbstractPotion.java:398-400`) in full: any
    occupied slot, in combat or out, refused only inside a We Meet Again dialog.
    Two consequences worth naming: it is **wider than USE** (a still-deferred
    potion body is discardable, because a discard never runs the body) and it is
    **not restricted to the non-combat phases** `can_use_potion` is.
  - **Grid `cancel` cost nothing to honour once the picks were buffered.**
    `GridCardSelectScreen` selects on click and commits on a button; the run
    layer's `CHOOSE` does both at once and can undo neither, which is why
    `cancel` was mapped as "no run-layer analogue (a grid pick cannot be
    undone)" and ended STS00047 four records in. But nothing needs undoing if
    nothing was applied. The `GridSession` `--neow` and `--shop` already used —
    accumulate picks, flush when the capture confirms, snapshot the index space
    at open because CommunicationMod's `choice_list` is the **unshrunk** filtered
    deck — moved into `command_map.hpp` beside the table, and `--replay` now uses
    it too. STS00047's Neow removal grid is exactly the shape that needed it:
    `choose 2`, `cancel`, `choose 0`, `proceed`.
  - **A grid the sim never opened is a deferred BODY, and now says so.**
    STS00052 and STS00045/46 all stopped at seq 2 with `grid choose index has no
    legal master-deck slot` — a mapping-shaped message for a condition that is
    not a mapping defect. Each took Neow's boss-relic blessing; the relic's
    `onEquip` opens a grid, and Astrolabe's (STS00052) and Empty Cage's
    (STS00045/46) `onEquip` bodies are two of the five deferred BOSS bodies in
    the obligations table. The **acquisition** is right — relic in the list, pool
    popped, every stream matched — so the honest outcome is a stop that names the
    body, which is what `sim_grid_open` + `unsimulated_grid_reason` now produce:
    *"the capture opens a master-deck grid the sim never opened (sim phase 1):
    the most recently acquired relic is Astrolabe, whose onEquip body is
    deferred"*. `sim_grid_open` knows all four phases that own a master-deck grid
    (Neow, campfire Smith/Toke, event grids, the shop purge grid), because a
    phase it did not know would misreport a live grid as a deferred body.
  - **Oracle proof**, `replay_run_diff --replay` over all six b45 reward runs.
    **STS00047: `PART` at seq 3 → `CLEAN`** to its terminal, 26 records.
    **STS00049: `PART` at seq 46 → `CLEAN`** to its terminal, 58 records (was
    47). **STS00048 and STS00050 unmoved and still `CLEAN`.** **STS00052:** still
    stops at seq 2, now **classified** — Astrolabe's deferred `onEquip`, not an
    index-mapping failure. **STS00051** reaches its terminal as before, and its
    library-order-only record count moves **20 → 19**: the seq-21 record was
    being compared against a sim that had ALREADY applied the Living Wall
    transform, because the old mapping committed a grid pick on the `choose`
    rather than on the `proceed` the capture had not issued yet. Buffering
    removes that off-by-one-record and the comparison is now clean; the 19 that
    remain, the 28 `hp`-plus-`card_id` records after them and the whole
    `kEventTransformRedPool` verdict are unchanged. **No new real divergence
    appeared anywhere.** `--neow` over all eleven runs is **byte-identical**.
    `--shop` differs only where it should: STS00052's walk goes `PURCHASE DIFF`
    → `PURCHASE OK`, 10 in-room records compared. The default reward spot-diff
    moves one line — STS00049's claim walk ends at seq 47 instead of 46, because
    the discard at seq 46 is now applied instead of ending the walk, a strictly
    later and strictly stricter checkpoint, still `CLAIM OK`.
  - Named regressions. In `replay_command_map_test`, five that could not even
    COMPILE before the fix, because the kinds and the verb they name did not
    exist: `APotionDiscardIsTheSameMappingOnEveryScreen`,
    `AGridChooseIsBufferedRatherThanAppliedImmediately`,
    `AGridCancelDropsThePendingSelectionInsteadOfStopping`,
    `AGridProceedIsTheCommitAndNotAScreenExit` and
    `EveryPhaseWithAMasterDeckGridIsRecognised`; plus
    `AGridTheSimNeverOpenedNamesTheDeferredRelicBody`, RED on the old reason
    string, and `APotionUseIsStillTheCombatScreensTargetedVerb`, which pins that
    the new screen-independent entry did not swallow the targeted one. In
    `run_advance_test`, five on the new door:
    `RunPotionDiscard.DiscardingOutOfCombatEmptiesTheSlotAndMovesNothingElse`
    (a whole-`RunState` memcmp against the pre-discard state with only that slot
    changed), `ADeferredPotionBodyIsStillDiscardable`,
    `ToyOrnithopterDoesNotHealForAPotionThrownAway`,
    `AnEmptySlotAndAnOutOfRangeSlotAreBothRefused` and
    `WeMeetAgainConfiscatesTheBelt`.
  - **A frozen document had to move with it, and it did.** stage-a design §7
    enumerates the `ActionVerb` set, so appending one makes that list wrong —
    exactly the condition conventions §4 calls stop-the-line. It is fixed in
    this change, in the shape `ActionVerb::CONFIRM` already set: §7's comment
    now lists `DISCARD_POTION(slot)` and stage-a §12 carries its change-log
    entry, including why it is a verb of its own rather than a mode of
    `USE_POTION` (it is legal in strictly MORE situations, so a sentinel arg
    would put two legality rules inside one verb).
  - No schema, fixture, golden, registry id, opcode, Steam/game deployment or
    oracle artifact changed; both campaign directories were read only. `Action`'s
    encoding is untouched — `DISCARD_POTION` is an appended enumerator, so no
    existing verb number moved.
  - Java provenance: `TopPanel.destroyPotion` (`TopPanel.java:529-531`),
    `AbstractPotion.canDiscard` / `.canUse` (`AbstractPotion.java:398-410`),
    `AbstractDungeon.returnRandomPotion` (`AbstractDungeon.java:824-850`),
    `PotionHelper.getRandomPotion` (`PotionHelper.java:164-172`),
    `ShopScreen.initPotions` (`ShopScreen.java:373-384`),
    `AbstractEvent.openMap` (`AbstractEvent.java:120-123`). The `potion discard`
    command itself is CommunicationMod's, not the game's:
    `CommandExecutor.executePotionCommand`
    (`tools/oracle_bridge/communicationmod-oracle/src/main/java/communicationmod/CommandExecutor.java:255-313`).

- **Replay harness: an event's own `[Leave]` is not Neow framing** `[x]` —
  discharges the **STS00048 stalls in `EVENT_DIALOG` on floor 2** obligation row
  the end-of-turn-curse fix opened, and closes STS00051's identically-shaped
  stall with it. **The engine was innocent; the harness was wrong.** Both event
  bodies exit correctly on their own terms — `fountain_choose` /
  `living_wall_choose` return `FINISHED` from their result screen and
  `run_advance.cpp`'s `EVENT_DIALOG` case clears `rc.event` and hands back to
  `MAP_CHOICE` — and the sim never got the chance to run either, because
  `--replay` never delivered the press.
  - **What the capture actually shows.** STS00048's floor-2 event is **The
    Divine Fountain** (`Fountain of Cleansing`): `EVENT choose 1` (Leave) at seq
    24, then a one-button `[Leave]` page — `EVENT choose 0` (25), `MAP return`
    (26), `EVENT choose 0` (27), `MAP choose 0` (28). STS00051's floor-2 event is
    **Living Wall**: `choose 1` (Change), the transform grid, then the same
    one-button page pressed **three** times with a `MAP return` between each.
    Every field of the captured `RunState` is identical across those repeats.
  - **Why the repeats are free, from the Java.** `AbstractEvent.openMap`
    (`AbstractEvent.java:120-123`) does two things and no more: it sets the
    CURRENT ROOM's phase to `COMPLETE` and calls `dungeonMapScreen.open(false)`.
    The `false` is load-bearing — `doScrollingAnimation == false` sets
    `dismissable = true` (`DungeonMapScreen.java:287`), and
    `DungeonMapScreen.close()` (`:316-320`) hides the map and touches nothing
    else. The event object, its room and its dialog panel all stay mounted, so a
    map `return` drops straight back onto the same page and the driver's
    random-legal policy presses `[Leave]` again. Each repeat re-enters
    `buttonEffect` at the unchanged `screenNum` and calls `openMap` again
    (`FountainOfCurseRemoval.java:73-79`, `LivingWall.java:116-119`), so every
    press after the first is state-free. It is the **same** bounce the harness
    already documents for a reward screen's `proceed`.
  - **The defect.** The `EVENT` mapping recognised a single-option `[Leave]`
    page by its LABEL and treated it as Neow's closing screen: `CHOOSE(proceed)`
    when the phase was `NEOW`, and **a plain no-op otherwise**. For every
    ordinary event that no-op swallowed the real exit, so the simulator sat in
    `EVENT_DIALOG` from floor 2 to the end of the artifact while the capture
    walked on — `floor: 3 -> 2` on every later record, which reads exactly like
    an engine divergence and is not one.
  - **The fix.** The discriminator is the **simulator's phase**, not the button's
    label: while the run layer is still in `EVENT_DIALOG` the press is the
    event's own proceed and is applied; once it has left for `MAP_CHOICE`, every
    later press is the UI bounce and is elided, exactly as the reward screen's
    `proceed` is. Neow keeps its two framing screens (`[Talk]`, and `[Leave]`
    → `CHOOSE(kChooseProceed)` while in `NEOW`) because the run layer models its
    blessing menu as its own phase and neither framing page as anything. The
    elision is deliberately narrow — a **one-button** page — and an EVENT
    command with real choices on it arriving while the sim is in no event now
    STOPS with a reason instead of being handed to whatever phase is live; in
    `MAP_CHOICE` that `CHOOSE` would have picked a map node and moved the run.
  - **The table is now testable.** A wrong entry in a screen-relative command
    mapping is indistinguishable from an engine divergence when a whole artifact
    is replayed, and until now the only way to see one was to re-drive a
    campaign by hand. `map_command` and its `ScreenInfo` moved out of the tool's
    `main.cpp` into `tools/oracle_bridge/replay/src/command_map.hpp`, published
    by the replay directory as the `replay_command_map` INTERFACE target. It
    carries no JSON dependency by design — the JSON pass that FILLS a
    `ScreenInfo` stayed behind — so the new `replay_command_map_test` needs no
    artifact and no data root and runs in every preset.
  - **Oracle proof**, `replay_run_diff --replay` over all six b45 reward runs,
    before and after. **STS00048: `PART` at seq 29 → `CLEAN`**, 47 records
    compared, zero diffs, replay reaches the run's terminal — the run is now
    end-to-end clean. **STS00051: `PART` at seq 28 → the run's terminal**, 69
    records compared, with the whole remaining divergence a **single** field
    (`master_deck[11].card_id: Havoc -> Iron Wave`) plus its one downstream `hp`
    consequence at floor 4. That is the already-filed
    `kEventTransformRedPool`-order obligation and nothing else: every stream and
    both pity counters match at the transform and stay matched to the terminal,
    so the `miscRng` draw is right and only the pool's ORDER is wrong. **No new
    real divergence appeared in any run**, and the row has been narrowed from
    "stated from the Java, not measured" to measured, with STS00051 as its
    end-to-end check. The other four are **unmoved, byte for byte**, and all
    four stops are the known B1.6 harness gaps: STS00047 at seq 3 (grid
    `cancel`), STS00049 clean to its seq 46 out-of-combat `potion discard`,
    STS00050 `CLEAN` end to end, STS00052 at seq 2 (a Neow grid index with no
    legal master-deck slot, because `--replay` does not open that grid).
  - Named regressions, in the new `replay_command_map_test`:
    `ReplayCommandMap.AnEventsFinalLeavePageIsTheRealExitWhileTheDialogIsLive`
    and `ReplayCommandMap.AMultiOptionEventPageOutsideAnEventStopsInsteadOfGuessing`
    were both RED before the fix. Seven more pin what must not move with it:
    `ALeavePressedAfterTheSimAlreadyLeftTheEventIsAUiBounce`,
    `NeowsOpeningTalkPageHasNoRunLayerEffect`,
    `NeowsClosingLeaveIsTheRunLayersProceed`,
    `NeowsBlessingMenuChoiceIsIndexedStraightThrough`,
    `NeowsLeaveRepeatedAfterItsMapIsUpIsAUiBounce`,
    `AMapReturnIsAPureUiDismissal` and
    `ARewardScreenProceedIsDeferredToTheMapChoiceThatMoves`.

- **Combat: end-of-turn curses play themselves out of the hand** `[x]` —
  discharges the **Two-slime floor-1 card-flow divergence (STS00048)**
  obligation row. One defect, one capture: run **STS00048** of
  `b45_rewards_oracle2_20260727T204809Z_claude01`, floor 1, Spike Slime (S) +
  Acid Slime (M). `--replay` diverged at seq 14 with the sim's Spike Slime alive
  on 11 HP where the game's had died. The slimes were innocent: **the Slimed
  cards, the two-monster damage order, Acid Slime M's move table and its A18+
  history rerolls all reproduce exactly.** What differed was a **Shame**.
  - **`trigger: end_of_turn` was modelled as "queue this program", but the game
    PLAYS the card.** `Shame.triggerOnEndOfTurnForPlayingCard`
    (`Shame.java:37-42`) sets `dontTriggerOnUseCard` and appends the card to
    `AbstractDungeon.actionManager.cardQueue`; the self-effect is not queued
    there at all, it is the body of `Shame.use()` (`:29-33`), guarded by that
    same flag. `GameActionManager.getNextAction` (`:194-300`) then dequeues and
    plays it: `canUse` is bypassed by the `dontTriggerOnUseCard` clause (`:214`),
    every `onPlayCard` / `onUseCard` / `triggerOnCardPlayed` fan-out and the
    `cardsPlayedThisTurn` increment are skipped (`:220-249`,
    `UseCardAction.java:41-64`), and `AbstractPlayer.useCard` (`:1358-1384`)
    runs `use()`, queues `UseCardAction`, and **removes the card from the hand**
    (`:1373`). So the curse reaches the **discard pile at the trigger stage** —
    ahead of `DiscardAtEndOfTurnAction`, which is only queued later, by
    `AbstractRoom.endTurn` (`:393-396`), once the card queue has drained. Burn,
    Decay, Doubt and Regret are the same shape (`Burn.java:45-56`,
    `Decay.java:41-52`, `Doubt.java:41-52`, `Regret.java:28-39`).
  - **Why that is worth a whole fight.** The sim left the curse in the hand for
    the end-of-turn sweep, which files from `getTopCard` down
    (`DiscardAction.java:52-62`), so Shame landed **last** instead of **first**.
    Capture evidence, turn 2 of the floor-1 fight: hand `[Shame, AscendersBane,
    Strike]`, pre-existing discard 8 cards. The game's pre-reshuffle discard was
    `[…8…, Shame, Strike, Slimed]`; the sim built `[…8…, Strike, Shame,
    Slimed]`. `Collections.shuffle` is content-independent, so **one
    transposition in equals one transposition out**: with the identical
    `shuffleRng` seed (`9032670848953727813`, hand-checked against a Python
    model of `java.util.Random` — `jdk_shuffle` is byte-exact) the game's turn-3
    draw pile read `[…, Shame@2, …, Strike@6, …]` and the sim's the reverse. On
    turn 4 the game's `play 3 0` hit a Strike and killed the 5-HP Spike Slime;
    the sim's index 3 was the unplayable Shame, the action was a no-op, and
    every later record drifted.
  - **The fix, at the true root cause.** `dispatch_card_end_of_turn`
    (`src/engine/card_play.cpp`) is now the game's two stages: pass 1 walks
    `hand.group` and records which cards trigger (with the hand still whole),
    pass 2 is the card-queue drain — per card, in hand order, queue its program,
    move it hand → LIMBO, queue its `USE_CARD`. `op_use_card` then files it, so
    the destination comes from the instance flags rather than an assumption
    (none of the five is `exhaust`, so all five discard). Bottom-queueing both
    halves per card reproduces the game's serialization exactly, because the
    game only services its `cardQueue` with an **empty** action queue, which is
    also why the four `addToTop` bodies are indistinguishable from `addToBot`
    there.
  - **Regret's hand-size read had to move with it.** `Regret.java:35-39` locks
    `magicNumber = baseMagicNumber = player.hand.size()` **inside the trigger**,
    before any card leaves the hand; the sim read `hand_count` when
    `LOSE_HP_PER_HAND` executed, which was only equal by accident and is now
    short by the number of curses that triggered. The opcode keeps its number
    and its registry authoring; `dispatch_card_end_of_turn` stamps its `amount`
    with the pass-1 hand size. Corrected at `interp.hpp`, `interp.cpp` and the
    `registry/cards.yaml` Regret row, all three of which asserted the
    execute-time read was equivalent.
  - **No fixture impact.** All twenty committed combat fixtures replay
    byte-identical: none holds a `trigger: end_of_turn` card in hand at a turn
    boundary, so the generator was not re-run and no schema field moved.
  - **Oracle proof.** `replay_run_diff --replay` over all six b45 reward runs,
    before and after. **STS00048**: first divergence **seq 14, floor 1 → seq 29,
    floor 3**; the whole floor-1 combat, its reward screens and its post-combat
    `RunState` are now **zero-diff** (seq 5-28 all `ok`, 47 records compared,
    replay reaches the run's terminal). The other five are **unmoved**, byte for
    byte: STS00047 stops at seq 3 on the grid `cancel`, STS00049 is clean to its
    seq 46 out-of-combat `potion discard`, STS00050 is CLEAN end to end,
    STS00051's frontier stays at seq 28 floor 3, STS00052 stops at seq 2 on a
    grid index. STS00048's **new** frontier is a different gap and is filed as
    its own obligation row: the sim is still parked in `EVENT_DIALOG` on floor 2
    when the capture is fighting on floor 3. **That row is now discharged** by
    the event-exit mapping fix directly above, which found the cause on the
    harness side, not in the run layer.
  - Named regressions, in `status_curse_test`:
    `StatusCurses.EndOfTurnCurseIsDiscardedBeforeTheGetTopCardHandSweep` (the
    captured `[Shame, AscendersBane, Strike]` hand, RED before the fix),
    `StatusCurses.TwoEndOfTurnCursesFileInHandOrderAheadOfTheSweptHand`, and
    `StatusCurses.RegretLocksTheHandSizeBeforeTheEndOfTurnCursesLeaveTheHand`.
    The two pre-existing directed scripts,
    `StatusCurses.EndTurnEffectsPrecedeEtherealAndHandDiscard` and
    `StatusCurses.DecayDoubtAndShameRunAtEndOfTurn`, were both RED on the old
    behaviour and now pin the interleaved
    `[effects of card k, card k filed, …, sweep]` order.

- **Combat: pre-turn monster block clear + debuff duration ticks** `[x]` —
  discharges both combat-layer obligation rows B4.5's oracle replay left behind.
  Two independent defects, one capture: run **STS00051** of
  `b45_rewards_oracle2_20260727T204809Z_claude01`, floor 1, two Louse — the
  game's Louse entered the player's turn 3 at 0 block and Vulnerable 1 and died
  to a 9-damage Strike; the simulator's kept its Curl Up block and its
  Vulnerable 2, absorbed the Strike and lived on 7 HP.
  - **Monster block was never cleared at the monster's turn start.**
    `MonsterGroup.applyPreTurnLogic` (`MonsterGroup.java:98-105`) — clear block
    unless the monster `hasPower("Barricade")`, then
    `applyStartOfTurnPowers()` — was believed to be dead code because its only
    caller, `MonsterStartTurnAction`, is referenced by nothing in the decompiled
    tree. That was a **decompiler artifact**: `AbstractRoom.endTurn` ends with
    `addToBottom((AbstractGameAction)new /* Unavailable Anonymous Inner Class!! */)`
    at `AbstractRoom.java:409`, because CFR dropped the anonymous class. Pinned
    in bytecode instead — `AbstractRoom.endTurn (bytecode AbstractRoom$1, javap)
    -- CFR-dropped anonymous class`. `javap -c` (JDK 8, against the game's own
    `desktop-1.0.jar`, read-only) shows `endTurn` at offsets 167-178 constructing
    `AbstractRoom$1` and handing it to `GameActionManager.addToBottom`, and
    `AbstractRoom$1.update()` as `addToBot(EndTurnAction)` /
    `addToBot(WaitAction(1.2f))` / `if (!skipMonsterTurn) addToBot(
    MonsterStartTurnAction)` / `monsterAttacksQueued = false`;
    `MonsterStartTurnAction.update()` then calls
    `getCurrRoom().monsters.applyPreTurnLogic()`. Those actions drain before
    `GameActionManager`'s `!monsterAttacksQueued` branch, so the walk sits
    between the end-of-turn discard and `queueMonsters` — `pump_step` **step 4**,
    which is where `apply_pre_turn_logic` now runs. `applyStartOfTurnPowers` is
    implemented with it (`dispatch_monster_at_start_of_turn`); no S1
    monster-ownable power binds that hook, so it is inert today.
  - **`Vulnerable` and `Weak` never ticked down.** Both rows bound no hooks at
    all. Their EFFECT is the native damage pipeline, but their DURATION is
    `atEndOfRound` (`VulnerablePower.java:44-53`, `WeakPower.java:44-53`) — the
    same six lines as `FrailPower.java:40-52`, which was already native. All
    three are now `native: true` with an `at_end_of_round` binding over one
    shared body (`src/engine/powers/power_duration_debuff.*`): consume a
    `justApplied` latch if set, else queue a one-stack `REDUCE_POWER`, which
    removes at zero (`ReducePowerAction.java:45-51`). The ctors' latch
    conditions differ and are reproduced exactly — Vulnerable needs
    `turnHasEnded && isSourceMonster` (`:36-38`), Weak and Frail
    `isSourceMonster` alone (`:35-37` / `:32-34`). A **full audit of every
    DEBUFF row** against its Java found no other gap: `LOSE_STRENGTH`,
    `LOSE_DEXTERITY`, `SHACKLED`, `NO_DRAW` and `ENTANGLE` already self-remove at
    end of turn, `NO_BLOCK` already reduces at end of round, and `CONFUSION` is
    permanent in the Java too.
  - **Storage, and a retired flag bit.** The latch is the slot's own
    `PowerSlot.counter`, per instance. Frail's latch **moved there** from the
    player-only `CombatState.flags` bit, which by construction could not describe
    a monster-owned instance and could not have described Vulnerable or Weak at
    all (six actors can hold one at once). `CombatState.flags` **bit 0 is
    retired**, not reused, and the symbol is deleted so a stale reader is a
    compile error rather than a silent "not just applied". No schema version, no
    POD layout change: the latch is always 0 at a `WAITING_ON_USER` boundary
    (set during the monster phase, consumed by `dispatch_at_end_of_round` in the
    same pump), so it never reaches a snapshot, a state hash or an oracle diff.
  - **The Lagavulin armour claim was wrong and is corrected.** A sleeping
    Lagavulin's block is cleared at the top of its own turn, one full phase
    before Metallicize re-grants 8 at `applyEndOfTurnPowers` time, so the armour
    **holds at 8** and never stacks to 16 / 24. Corrected at
    `include/sts/engine/monster_lagavulin.hpp` and
    `src/engine/monster_lagavulin.cpp`, in the B3.19 ledger row and change-log
    entry above, and as a dated addendum on the archived B3.19 Log (append-only,
    so the original text stands and the correction is appended). Named test:
    `LagavulinSleep.ArmourHoldsAtEightEachRoundAndIsGoneOnceTheShellOpens`, plus
    `CombatStart.LagavulinArmourTicksOncePerCompletedRound` through the run layer.
  - **Fixtures regenerated, not hand-edited.** Ten of the twenty committed combat
    fixtures changed and were rewritten by the checked-in generator
    (`tools/fixture_gen/gen_combat_fixtures`), whose independent model gained the
    same two rules from the same Java. No schema change — content regeneration.
    Two scripts moved, and only because the fix invalidated a coverage claim
    rather than a number: `fixt18_r0_reshuffle_overlap` is renamed
    `fixt18_r0_reshuffle_stale_block` (Bash-then-Bellow can no longer leave both
    powers standing at a recorded boundary, since the round that ends with the
    Bellow also runs `atEndOfRound`; what it now pins is a Bellow's block
    outliving the player's next turn), and `fixt19_r9_long_block` gains a turn-4
    Bash so Bellow-then-Bash carries the Strength+Vulnerable concurrency the
    ledger's A6.2 requirement asks for. Both deaths still occur, checked against
    `--dumpall`; the derivation notes and their coverage table are updated with
    them.
  - **Oracle proof.** `replay_run_diff --replay` over all six b45 reward runs,
    before and after. **STS00051**: first divergence was floor 1, seq 15, the
    sim still in COMBAT while the game was on its reward screen (12 fields:
    `hp 66 -> 60` plus every reward stream at its pre-assembly value); now the
    whole floor-1 combat and its post-combat `RunState` are **zero-diff**, and
    the replay runs on to floor 3 before stopping. **STS00049**: first divergence
    was the same shape at floor 1, seq 26; now **no divergence anywhere** in the
    47 records it compares. **STS00050**: clean before and after. The three
    remaining stops are unrelated and named per-run in the commit body.
  - Named regressions: `PreTurnLogic.*` (four, in `action_queue_test`),
    `DurationDebuffs.*` (seven, in `power_hooks_test`), and
    `LouseCurlUp.BlockDoesNotSurviveIntoThePlayersNextTurn`, which reproduces the
    captured two-Louse arithmetic without needing the artifact.

- **B4.14-integration incident: red merge pushed; two fix-forwards; stale
  build-tree trap eliminated** `[x]` — commits `97d350f` + `09e103d` (merged
  after `09df37f`, the B4.14 union merge that was pushed while red — an
  orchestrator process error: the matrix verdict was piped away and the landing
  chained on the wrong exit code). Two distinct causes untangled: **(1) real** —
  parallel branches each added byte-equivalent `lose_gold` /
  `get_random_potion` helpers, ambiguous only in the union (the two-definitions
  class the integration lessons already name); deduplicated in favor of the
  single doors. **(2) false red** — four objects in master's `build/debug`
  carried **truncated `.ninja_deps` records** (`#deps 0`), so no header edit
  could ever dirty them again; three monster objects still had the schema-5
  layout (`CombatState` 3928/`PowerSlot` 4) inside a schema-6 archive, and the
  Cultist's init writing at stale offsets produced the fuzz `no_legal_moves`
  dead-end that looked like an emergent three-branch defect. The union
  **sources were green all along** (clean-build proof on all three presets).
  Eliminated per conventions §7: `tools/check_ninja_deps.sh` (+ its own
  compiler-free fixture test) now runs with `--repair` inside `wsl_run.sh`
  between configure and build, so the sanctioned entry point cannot hand out a
  silently stale binary; conventions §6 gained the trap entry. Named
  regression: `FirstCombatEntry.NeowPayoutWalkOntoFloorOneLeavesALiveCombatMask`
  pins the seed-42 four-press walk onto a live floor-1 combat mask. Residual
  caution recorded: builds that bypass `wsl_run.sh` (bare `cmake --build`, an
  IDE, future CI) bypass the guard.

Changes on `master` that are **not** ledger tasks, so they have no task block to
archive and no `#log` anchor. They are indexed here for the same reason task
entries are: conventions §2 requires that history and this ledger never
disagree, and a fix or a toolchain change that nothing records is invisible to
the next session.

- **Queued-card dequeue revalidation and dead/escaped split** `[x]` — commit
  `9484f70` identified the missing pre-hook gate but its hp-only implementation
  and Log were superseded by the immediate independent-audit fix-forward. The
  resolver and `legal_actions` now share the full in-scope `canUse` authority
  (status/curse relic escape hatches, turn, energy/autoplay, Entangle, Normality,
  Velvet Choker, Clash and `cardPlayable`). Existing YAML now generates the
  exact CardTarget kind: only `ENEMY` takes the successful-gate post-hook
  null/dead/escaping suppression; `SELF_AND_ENEMY` proceeds. Failed autoplay
  receives exact no-trigger `UseCardAction` semantics, including at terminal.
  Autoplay no longer mutates `cost_now`; X-cost draw-top plays preserve energy
  and a Double Tap purge copy carries its captured `energyOnUse` transiently
  without touching persistent card misc. No YAML/ID/opcode/state-layout,
  fixture, or golden change. Frozen §5.3 and §12 were mechanically corrected.
  [Archive log.](stage-b-log.md#card-dead-target)
- **Run-layer escape obligation + Smoke Bomb flag-parity audit** `[x]` —
  commit `82d497a` plus its immediate fix-forward (this ledger change).
  Discharges and deletes both deferred rows that named the next
  `run_advance.cpp` owner: the run-layer half of escape (outcome, stolen-gold
  settlement, and `live_target`) and the stale file-header monster roster.
  `RunCombatOutcome::MUGGED = 4` is append-only over values 0–3; value 5 stays
  deliberately reserved for the currently unreachable un-mugged monster-escape
  shape. The reward layer distinguishes the screen choice from
  `MonsterGroup.haveMonstersEscaped`: mugged outranks smoked for the screen
  (`AbstractRoom.update`, AbstractRoom.java:335-338), while an all-escaped
  plain combat suppresses its gold and zeroes its potion chance but still rolls
  cards. A killed Looter's clamped accrual returns as a first-position
  `STOLEN_GOLD` item; an escaped Looter's does not
  (`Looter.die`, Looter.java:159-174; `DamageAction.stealGold`,
  DamageAction.java:98-114; `AbstractRoom.addStolenGoldToRewards`,
  AbstractRoom.java:619-626; `RewardItem.applyGoldBonus` /
  `RewardItem.claimReward`, RewardItem.java:112-129,255-273).
  Independent review caught one rejected-branch defect before landing:
  `step_potion` opened the right MUGGED/SMOKE_BOMB screen but bypassed
  `SmokeBomb.use`, leaving `kCombatFlagPlayerEscaped` clear. The fix calls the
  native body before relic hooks and slot destruction, matching
  `PotionPopUp.updateInput`'s `potion.use` → `relic.onUsePotion` →
  `destroyPotion` order (PotionPopUp.java:234-239) and
  `SmokeBomb.use`'s smoked/isEscaping writes (`SmokeBomb.java:37-48`).
  Named regression assertions cover both ordinary Smoke Bomb and the
  mugged+smoked two-flag state while preserving MUGGED screen precedence.
  No persistent-state layout, registry id, opcode, fixture, or golden-vector
  change; this is deferred-obligation non-task work, so there is no task block
  to archive.
- **Combat start: turn 1 must not run the end-of-round pass** `[x]` — commit `821bffd`, merged at `9dea548`, landed in `56248c5`. `combat_begin` and `enter_combat` both primed turn 1 with `turn_has_ended = 1` and pumped, routing through `start_of_turn` → `dispatch_at_end_of_round` **before the player's first turn**, so every end-of-round hook on a power present at combat start fired once for free. **The game cannot reach that branch**: `AbstractRoom.java:236-243` sets the flag and then queues `GainEnergyAndEnableControlsAction`, which clears it (`:35`) — the queue is never empty while that item is pending, so the step-6 test is false by the time it is reached. **Measured**: a sleeping Lagavulin had **16 block on turn 1 instead of 8** (at the time monster block was never cleared, so it also gained +8 every later turn; the turn-1 defect this entry fixes is unaffected, but see the 2026-07-27 correction under [Landed non-task work](#landed-non-task-work) — the armour now holds at 8 from turn 2 on rather than accumulating). Fixed at **both** entry points via a shared `begin_first_turn` that reuses the same `start_of_turn` with a `TurnStart` parameter; **two by-construction guards scan both files**, so a one-sided regression fails rather than drifts. All 20 committed fixtures replayed **zero-diff**, proving the spurious pass was inert for every piece of landed content. The post-draw *powers* twin was reported and deliberately left — it is the `fix-postdraw-gate` row in the obligations table.
- **`MonsterState.flags` widened u16 → u32, schema v4 → v5** `[x]` — commit `2684548`, landed in `a32e84c`. **Owner-directed** (2026-07-26), recorded in the frozen spec's change log as design §11 **v0.1.5**. The width was the smaller half of the fix: bits had been allocated **linearly**, one fresh bit per monster type, though **no monster is two types at once** — nine Act-1 types consumed all sixteen bits while the worst single type (The Guardian) needs five, so widening alone would have re-exhausted the word in Act 2. The two-region policy is in **Shared namespaces** above; `kMonsterFlagEscaped` moved bit 15 → **24**, every type-scoped bit kept its historical value. **A latent hazard the widening exposed:** several flag writes were `static_cast<uint16_t>`, which after widening would have **silently cleared the entire global region** — quietly erasing `Escaped` — on every write; the compiler cannot catch it, because narrowing through an explicit cast is exactly what the cast asks for. Removed across eight source files and two tests. `sizeof(MonsterState)` 112 → 116 and `sizeof(CombatState)` 3896 → 3928, **measured by compiled `offsetof`/`sizeof` probes, not predicted**, under the 8192 ceiling v0.1.4 raised. **All 20 combat fixtures regenerated — the single sanctioned exception to this project's never-modify-a-committed-fixture rule**, owner-approved; nothing under `tests/golden/` outside `combat_fixtures/` moved. The B3.12/B4.3 single-zero-run-insertion proof shape **was not available and was not faked**: widening an *interior* field moves every later offset and deletes an old alignment pad, so an equivalent per-field proof was produced from probe-derived offsets over 20 fixtures / 112 records.
- **Combat start: turn 1 must not run the post-draw power pass** `[x]` — commit `f05ad8a`, merged at `a5afbf9`, landed in `06c4fa0`. The **sibling** of the end-of-round gate above and the second half of the same divergence, discharging the obligation integration-14 left. `start_of_turn`'s end-of-round pass, Ice Cream energy branch and block decay were all gated to `kSubsequentTurn`; `dispatch_at_start_of_turn_post_draw` was not, so it also ran while priming turn 1. **The game has no counterpart there**: `AbstractRoom.update`'s turn-1 block calls `applyStartOfTurnRelics` (`:253`), `applyStartOfTurnPostDrawRelics` (`:254`), `applyStartOfTurnCards` (`:255`), `applyStartOfTurnPowers` (`:256`) and `applyStartOfTurnOrbs` (`:257`) — and **no `applyStartOfTurnPostDrawPowers` line at all**; `GameActionManager.java:363` (step 6) is the whole game's only caller. The two halves are **not** a pair — the relic half really is on both sides, which is why only the power half moved. Inert for all landed content (Brutality and Demon Form are the only binders, and both require playing their card), so **no test could see it**: the two new `combat_start_test` cases construct the state with the power already present, and the fix was **demonstrated RED before green** — re-arming the dispatch failed exactly those two and nothing else. Fixtures replayed unchanged.
- **Citation audit + Pantograph's inverted DEFERRED marker** `[x]` — commit `39876f0`, in `06c4fa0`. Comment/provenance only: no executable line, registry value or generated-table value changed. Two defects, **both invisible to git because neither branch conflicted** — the conventions §8 class, and a direct instance of "a conflict-free merge is not evidence of correctness". (1) `AbstractMonster.die()` was cited as `:741-750` at nine new on-death sites; `die(boolean)` is at `:925` and `:741-750` is render code, so **the merge replaced master's already-correct `:933-937` with a wrong one** — re-read and corrected to `isDying` `:927` / powers' `onDeath` `:928-932` / relics' `onMonsterDeath` `:933-937`. (2) `EntanglePower` was cited as `:50-53` at eight sites in a **47-line file**, i.e. resolving to nothing — corrected to `:15-47` / `:17` / `:20-29` / `:31-46`. The described *behaviour* was right throughout in both cases; only the line numbers were wrong. (3) `relics.yaml`'s Pantograph row carried a `DEFERRED` marker **on live, tested code** — the same bug signal running the other way — retired after verifying the native body and its four tests; both premises of the deferral are dead (`enemy_type` column + `MonsterDef::is_boss()`, and three BOSS rows exist). The other ~39 `DEFERRED` markers in that file are legitimate and were not touched.
- **Build / toolchain effort** `[x]` — commits `44c1e11` (FP contract), `b481db2` + `c568cad` (ctest parallelism, job gate, shared ccache), `93e7a7d` + `c6913f2` (native Windows), merged at `28eab81`, landed in `8235477`. **The floating-point contract is pinned** (`-ffp-contract=off`, `cmake/StsFloatingPoint.cmake`): the frozen oracles were **not** captured under contraction only because baseline x86-64 has no FMA instruction — one `-march=native` away from silently changing damage numbers — and `fp_contract_test` now fails if contraction returns. `ctest` runs parallel; a machine-wide job gate bounds concurrent build parallelism across worktrees; ccache is shared across worktrees via `CCACHE_BASEDIR` (measured 0 % → 50 % cross-worktree hit rate). **A native Windows target was added** — clang-cl 22.1.8, presets `win-debug` / `win-release` / `win-asan` — and it is **byte-identical to Linux**: the 20 fixture traces hash to `ccdc4432…` under GCC 13.3, Clang 18.1.3 and clang-cl alike (debug, LTO release, asan). **WSL is therefore optional, including for sanitizers**: ASan *and* UBSan both work under clang-cl. `cl.exe` silently ignores `/fsanitize=undefined` (warning D9002, exit 0), which is why clang-cl is **required** here rather than merely preferred. The Windows CI job is proposed but unverified — see the obligations table.

---

## Phase B0 — Bridge groundwork

- **B0.1** `[x]` CommunicationMod source pin + protocol survey — upstream CommunicationMod pinned at v1.2.1 (`70ca84b1`), MIT, vendored source-only; `PROTOCOL.md` gives all 141 `GameStateConverter` fields a disposition; `start` seed proven to be the base-35 display string · [log](stage-b-log.md#b01)
- **B0.2** `[x]` Stock-jar bridge bring-up + environment audit — `echo_driver.py` + scriptable ModTheSpire launch; 205-state seeded A20 capture replays clean; `start` == the game's own seeded-run UI byte-identically; ~0.36 states/s stock baseline; profile-unlock audit PASS (do not “fix” `IRONCLADUnlockLevel`) · [log](stage-b-log.md#b02)

---

## Phase B1 — The oracle fork, driver, translator (Gate G4 = M2)

- **B1.1** `[x]` Fork build pipeline — `build_fork.ps1`: JDK-8, no-Maven, deterministic jar (sha `6AB875C8…`); fork == stock on 206/206 normalized states; modid `CommunicationMod-oracle` · [log](stage-b-log.md#b11)
- **B1.2** `[x]` Oracle state block — the `oracle` JSON block behind fork flag `oracleBlock`, carrying 14 RNG streams `{counter,s0,s1}` + 2 pity + 3 event-pity floats + `purgeCost` + 3 remaining-pool lists + 5 relic-pool orders + move history; `relicRng.counter`==5 at init; floors 1-3 == `floor_stream` bit-for-bit · [log](stage-b-log.md#b12)
- **B1.3** `[x]` Rendering-strip / fast-forward patches — 3 individually-toggleable strip families; 20 seeds / 996 records A/B: 12/20 byte-identical, 8/20 differ only in enumerated presentation fields, 0 semantic leaks; sustained **32.0 act/s** (≈89× baseline) · [log](stage-b-log.md#b13)
- **B1.4** `[x]` Campaign driver — `campaign_driver.py` + `orchestrator.py` + `validate_artifacts.py`; unattended 10-seed A20 campaign 10/10, 574 actions, survived an induced mid-campaign game kill; artifacts schema-valid · [log](stage-b-log.md#b14)
- **B1.5** `[x]` Translator (JSON → binary schema) — `tools/oracle_bridge/translator/` (nlohmann, tools-only) JSONL → RunState/CombatState with a fail-loud disposition table; 7 `translator_test` cases; 147/147 · [log](stage-b-log.md#b15)
- **B1.6** `[x]` Diff-harness run-level + oracle adapter — trace format v2 (`state_kind` discriminator), `SCHEMA_VERSION` 1→2 with v1 compat-read (zero fixture regeneration), `diff_run_states` field groups, `CommunicationModOracleAdapter`; +16 tests, 163/163 ×3 · [log](stage-b-log.md#b16)
- **G4** `[x]` **Gate: oracle bridge live (M2)** — tag `g4-bridge-live` — 20 seeds / 996 records: **zero unknown-FIELD errors** (94 unknown content ids tallied), 75 floor-entry RNG cross-checks vs `floor_stream`/`map_stream`, strip equivalence + 32.0 act/s re-confirmed on the final build (sha `04477E4E…`); 166/166 ×3 · [log](stage-b-log.md#g4)

---

## Phase B2 — Registry system + skeleton migration (Gate G5) — ∥ with Phase B1

- **B2.1** `[x]` Registry schema + codegen tool — `registry/*.yaml` for all 8 domains + `tools/registry_gen/gen.py` (PyYAML) emitting id enums / effect-program tables / game_id tables / manifest; deterministic; 137/137 · [log](stage-b-log.md#b21)
- **B2.2** `[x]` Skeleton migration onto the registry — hand tables deleted (no dual system), engine re-exports the generated enums/tables; Stage A's 131 tests + all 20 fixtures pass with **zero test edits and zero fixture regeneration**; 140/140 · [log](stage-b-log.md#b22)
- **G5** `[x]` **Gate: registry live** — tag `g5-registry-live` — 140/140 through the generated path in debug+asan+release; manifest == the skeleton counts (9 rows); CI installs `python3-yaml` and runs the generator · [log](stage-b-log.md#g5)

---

## Phase B3 — Combat content closure (all tasks additionally gated on G4 + G5)

Each content task ships: registry entries with provenance (every cited
`use()`/ctor/`getMove` **read in full** before encoding), any new
powers/opcodes those entries need (append-only numbering), tier-2 table tests
per entry, a directed tier-3 script added to the campaign script library, and
— once the run layer exists — at least one oracle spot-diff exercising the
batch. Until run-level replay lands (B4.4), oracle spot-diffs for combat
content use combat-state comparison inside bridge runs whose context the
translator can seed (B1.6), or are deferred to the batch's campaign debut and
noted in the Log.

- **B3.1** `[x]` Interpreter/card-mechanics extensions — opcodes `MAKE_CARD`=9 / `SET_COST`=10, ALL_ENEMY+RANDOM_ENEMY targeting, `CardFlag` bits (EXHAUST/ETHEREAL/INNATE/UNPLAYABLE/RETAIN/XCOST), X-cost, two-row upgrade plumbing; +17 tests, 191/191 ×3 · [log](stage-b-log.md#b31)
- **B3.2** `[x]` Power-hook framework completion — 14 hook points dispatched in the frozen §5.2-5.5 per-hook source order, `powers.yaml` `hooks:`/`native:` schema, opcode `LOSE_HP`=11, framework powers ids 4-12; +12 tests, 213/213 ×3 · [log](stage-b-log.md#b32)
- **B3.3** `[x]` Red commons — attacks — 13 red common attacks, card ids 11-23, + Wound (id 24, first STATUS); opcodes `DAMAGE_BLOCK`=15 / `DAMAGE_STR_MULT`=16 / `DAMAGE_PER_STRIKE`=17; +35 tests, 308/308 ×3 · [log](stage-b-log.md#b33)
- **B3.4** `[x]` Red commons — skills — 5 skills, card ids 6-10, + LoseStrength power 13; CHOOSE-in-combat lives in the action queue — opcodes `CHOOSE_CARD`=12 / `PLAY_TOP_DRAW`=13 / `REMOVE_POWER`=14; +18 tests · [log](stage-b-log.md#b34)
- **B3.5** `[x]` Red uncommons — attacks — 11 uncommon attacks, card ids 40-50, opcodes 21-24; Searing Blow encodes its multi-upgrade in the existing `CardInstance.upgrade` count (no schema change); +18 tests, 405/405 ×3 · [log](stage-b-log.md#b35)
- **B3.6** `[x]` Red uncommons — skills — 17 uncommon skills (source enumeration moved **Rage** here from B3.7), card ids 51-67, powers `NO_DRAW`=24 / `FLAME_BARRIER`=25, opcodes 30-33, `ChoiceKind::DUPLICATE`=4, `CardType::POWER`=4; 4 stop-the-line Java corrections fixed forward; +35 tests, 464/464 ×3 · [log](stage-b-log.md#b36)
- **B3.7** `[x]` Red uncommons — power cards — 8 uncommon power cards, card ids 68-75, powers 26-27, POWER-card play path (apply + remove from every pile, no discard); plus a fix-forward importing Combust `hpLoss` from oracle power `misc`; 526/526 debug + asan · [log](stage-b-log.md#b37)
- **B3.8** `[x]` ∥ Red rares — 16 rare cards, ids 76-91; powers 48-53 (**Corruption needed no row** — id 11 already existed, and `PowerId` 47 was left the gap B3.21 reserved); opcodes 34-39 incl. **`PLAY_CARD`, the general recursive-play verb** (B3.11's Mayhem reuses it unchanged); `ChoiceKind::EXHAUST_TO_HAND`=5, `CardFlag::PURGE_ON_USE`; discharges `healing: true` on Feed/Reaper (`kIroncladAttackPool` 25 → 28, both heals through `heal_player_with_relics`), Barricade's block-decay branch on the `kSubsequentTurn` side, and the recursive-play opcode · [log](stage-b-log.md#b38)
- **B3.9** `[x]` Status + curses — 4 statuses + 11 curses, card ids 25-39; opcodes `LOSE_HP_PER_HAND`=18 / `DISCARD_HAND`=19 / `REDUCE_POWER`=20, native `FRAIL`=21; end-of-turn order rewritten; all 20 combat fixtures regenerated from the checked-in generator; 368/368 · [log](stage-b-log.md#b39)

### B3.10 — **SPLIT 2026-07-26 into mandatory B3.10a / B3.10b / B3.10c**
The enumeration was verified against source before the split and is **exactly
the twenty the original entry listed** — no card moved between tasks the way
B3.6 gained Rage. `CardLibrary.addColorlessCards` (`CardLibrary.java:799-834`)
orders them alphabetically, and ids 92–111 map 1:1 onto that order.

The split is a **scope** finding, not a scoping preference: ten of the twenty
need no new machinery, and the other ten each need a *distinct* verb. Conventions
§5 says a task whose real scope turns out materially larger than its ledger entry
gets the split proposed first, so the dispatched agent stopped without
committing rather than take unallocated ids or land a fragment. That was correct.

- **B3.10a** `[x]` ∥ Colorless uncommons — the fourteen reachable without new choice machinery, card ids 92-111 **sparse** (interior gaps pinned empty by a named test), opcodes 45-48, power 77 `NO_BLOCK`. Split out after the whole-B3.10 agent read every `use()` and **stopped without committing** — ten of the twenty need a distinct verb each, ~2.5× the allocated budget. **`NO_BLOCK` overrides `modifyBlockLast`, not `modifyBlock`** — the game’s only overrider of that hook — so `interp_block.cpp` gained a genuine **second pass**, demonstrated RED before green and pinned by one named test (the Log says so, because the other seven block cases pass under the wrong shape). Deep Breath exposed the **card-in-limbo** divergence; the local compensation and general deferral recorded at landing are now discharged by `card-limbo` below · [log](stage-b-log.md#b310a)
- **card-limbo** `[x]` Played-card limbo / queued `UseCardAction` filing — discharges B3.10a's engine-wide obligation, removes the Headbutt / Deep Breath / Havoc local compensations, allocates engine-emitted opcode 53 without consuming B3.10b's 49–52 reservation, and uses append-only `CardFlag` bit 8 for Java's one-play `exhaustOnUseOnce` lifetime · [log](stage-b-log.md#card-limbo)

- **B3.10b** `[x]` Colorless uncommons — Dark Shackles, Discovery,
  Enlightenment and Jack of All Trades; generated RED/colorless combat pools,
  unique persisted Discovery choice, opcodes 49–52 and power 78 `SHACKLED`;
  corrected the losing Discovery source claim and made unsupported card
  `native:` fail loudly · [log](stage-b-log.md#b310b)

- **B3.10c** `[x]` Colorless uncommons — mandatory optional-selection closure —
  Purity (109) and Forethought (101), both live; the **zero-to-N CHOOSE with an
  explicit confirm**, which is a public `ActionMask` change: `ActionVerb` 4
  `CONFIRM`, `ChoiceKind` 8 `PUT_ON_DRAW_BOTTOM`, fuzz `MoveCat` 25
  `CHOICE_CONFIRM` (`COUNT` 25 → 26), `CardFlag` bit 14 `FREE_TO_PLAY_ONCE`, no
  new opcode and **no schema change** — the picks live as the hand's trailing
  suffix with a four-bit count in the open item's flags, exactly the game's own
  `p.hand` / `selectedCards` split, so pick order (exhaust order, draw-bottom
  order) is carried by the array itself. `ActionMask` gains `choice_optional` /
  `can_confirm_choice` / `choice_selected_count`, all three compared by the
  four-span overload's mask-equality assert. Pinned against the Java: Purity's
  `anyNumber` guard means a small hand still opens the screen rather than
  auto-exhausting, and its `isRandom == false` means it spends no RNG on any
  path; Forethought's forced single-card path bills **no** `cardRandomRng`, and
  its `cost > 0` grant reads the combat BASE cost. Discharges the translator's
  deferred `can_pick_zero` — `HAND_SELECT` is mapped, with the un-serialized
  manipulation kind stated rather than invented and every unmodelled shape
  refused. Colorless UNCOMMON block 92-111 now dense; `kCardsCount` 124 → 126;
  both rows join `kColorlessCombatPool` · [log](stage-b-log.md#b310c)

- **B3.11** `[x]` ∥ Colorless rares — all 15 live (none deferred, none inert), ids 112-126 in `addColorlessCards` alphabetical order; powers 81-84 MAYHEM/MAGNETISM/PANACHE/THE_BOMB — **The Bomb is the first instanced (non-merging) power**, with value-keyed queued reduce/remove because compaction makes queued slot indices stale; opcodes 54-57 `UPGRADE_ALL`/`RANDOM_CARD_TO_DRAW`/`DRAW_PILE_FETCH`/`DAMAGE_GREED`; `ChoiceKind::DRAW_TO_HAND`=9 over new `ChoiceSource::DRAW`=4 with a `requires_draw_pile_type` legality column; `kIroncladSkillPool` pinned as the in-order SKILL subsequence of the combat pool; owner-approved **schema 5→6** (`PowerSlot` +`counter`, instanced powers, combat-gold accumulator settled once through `gain_gold` at fold-back) with a mechanical layout-transform fixture proof, byte-identical over all 20 fixtures; fix-forwards: Sadistic's missing Shackled exclusion, PutOnDeck forced-path `cardRandomRng` billing (Warcry), the ≥8 choice-kind packing; Mayhem's CFR-unavailable anonymous body reconstructed from `DistilledChaosPotion.java:41` + `Havoc.java:31` and declared in provenance; discharges both B3.2-inherited rows; one new obligation row (stolen-gold clamp ordering corner) · [log](stage-b-log.md#b311)

- **B3.12** `[x]` Multi-monster combat + encounter framework — `encounters.yaml` with 20 Act-1 encounters and their miscRng composition programs; `resolve_composition`/`generate_monster_lists`/`spawn_group`/`dispatch_monster_turn`; **schema 3→4** (`kMonsterCap` 5→7, `sizeof(CombatState)` 3672→3896); 20 fixtures regenerated with a byte-level zero-diff-in-meaning proof; +13 tests, 286/286 ×3 · [log](stage-b-log.md#b312)
- **B3.13** `[x]` Monsters: Cultist + louses — monster ids 2-4 (Cultist, LouseNormal, LouseDefensive) + power `CURL_UP`=20; committed independent XS128 fixtures, 32 seeds × 20 turns per monster; 359/359 · [log](stage-b-log.md#b313)
- **B3.14** `[x]` Monsters: small/medium slimes — monster ids 5-8 (Spike/Acid Slime S+M) + `MonsterIntent::ATTACK_DEBUFF`=6; XS128 fixtures ×4; 413/413 integrated · [log](stage-b-log.md#b314)

- **B3.15** `[x]` Monsters: slavers + Looter + Fungi Beast — landed in **two deliberate halves**. Half one (`f24b8db`): monster ids 23-25 (Blue/Red Slaver, Fungi Beast), powers `ENTANGLE`=73 / `SPORE_CLOUD`=74, the first `Hook::ON_DEATH` dispatch; **Entangled is a legality predicate, not a power hook**, and Spore Cloud's `isDying`-before-the-power-walk ordering means the first of two Fungi Beasts releases 2 Vulnerable and the last releases none. The Looter was **withheld rather than improvised**: the engine's `hp > 0` liveness signal could not express the game's `isDying || isEscaping`, and `pump_step` recomputes the phase from it every iteration so there was no alternative seam — four obligation rows that looked like four problems were **one**. Half two (`a2a60df`): monster id 26, `PowerId` 75 `THIEVERY`, `MonsterIntent` 13 `ESCAPE`, opcode 40 `ESCAPE`, and **every** "in the fight" read converted against its Java citation across eight files — **no `CombatState` field, no schema bump**, 20 fixtures unchanged. Un-parked its groups by construction with no `run_advance.cpp` edit · [log](stage-b-log.md#b315)

- **B3.16** `[x]` ∥ Monsters: gremlin gang — monster ids 16-20, `PowerId::ANGRY`=40, `MonsterIntent::DEFEND`=11; six independent 32-seed × 20-turn XS128 fixtures incl. a 4-gremlin battery pinning the Tsundere's `aiRng` block-target pick; **move 99 (ESCAPE) is unreachable in Act 1** and left unmodelled with both halves recorded for Act 2; Angry's `damageAmount > 0` guard reads **post-block** damage; registering the five init fns un-parked the encounter with no `run_advance.cpp` edit · [log](stage-b-log.md#b316)
- **B3.17** `[x]` Monsters: large slimes + split — monster ids 9-10 (large slimes), `PowerId::SPLIT`=22, opcodes 25-29 (`CANNOT_LOSE`/`CAN_LOSE`/`SUICIDE`/`SPAWN_MONSTER`/`SET_MOVE`); the Java-exact split framework; 441/441 ×3 · [log](stage-b-log.md#b317)
- **B3.18** `[x]` Elites: Gremlin Nob + Sentries — monster ids 12 GREMLIN_NOB / 13 SENTRY (both ELITE) + `PowerId::ANGER`=33; Artifact needed **no** new row (B3.2's id 4, the nullify already at the APPLY_POWER site — Sentry only grants the stack); 3 independent 32-seed × 20-turn fixtures pin the Nob's A18 history tree and the Sentry in an even and an odd slot; registering the two init fns un-parked the Gremlin Nob and 3 Sentries encounters by construction; Sentry's animation-only `damage()` is an explicit empty `on_monster_damaged` case, not a `default:`; union 641/641 ×3 · [log](stage-b-log.md#b318)
- **B3.19** `[x]` Elite: Lagavulin — monster id 15 LAGAVULIN (ELITE), native sleep/wake machine; **no new power id** (Metallicize was already id 5 — now the first MONSTER-owned power to bind an end-of-turn hook, and the generator's duplicate-name check caught the re-add); `MonsterIntent` SLEEP=9 / STUN=10; `on_monster_damaged` gains `hp_lost` so absorbed damage cannot wake it; ~~armour stands at 8/16/24 because monster block never decays in this build~~ — **corrected 2026-07-27, see [Landed non-task work](#landed-non-task-work): the armour holds at 8. `MonsterStartTurnAction` looked uncalled only because CFR dropped the anonymous class that queues it; the bytecode has it, and the pre-turn block clear now runs**; un-parked the Lagavulin encounter by construction; union 641/641 ×3 · [log](stage-b-log.md#b319)
- **B3.20** `[x]` Boss: Slime Boss — monster id 11 + `MonsterIntent::STRONG_DEBUFF`=8; fixed 150 HP, Goop→Prep→Slam cycle, exact-half split chaining into B3.17's large slimes; 521/521 debug + asan · [log](stage-b-log.md#b320)
- **B3.21** `[x]` ∥ Boss: The Guardian — monster id 21, native offensive/defensive mode machine, powers MODE_SHIFT=45 / SHARP_HIDE=46 (**47 deliberately unused, never to be backfilled**), intents DEFEND=11 / ATTACK_BUFF=12; the mode threshold **grows by 10 at every Defensive-Mode entry** (40, 50, 60 … at A20 — driven for three real flips, not computed), and mode state is **not** derived from `ModeShiftPower.amount` because `isOpen` is set synchronously while the power is only queued; found and fixed en route: **Spot Weakness paid out nothing against a telegraphed `ATTACK_BUFF`** · [log](stage-b-log.md#b321)
- **B3.22** `[x]` ∥ Boss: Hexaghost — monster id 22; **the orbs are not entities**, so the only combat-relevant state is the scalar `orbActiveCount` (0-6) in three spare `MonsterState.flags` bits with a fourth for `burnUpgraded` — **no `CombatState` field, so no `SCHEMA_VERSION` bump and no fixture regeneration**; Divider = `player.currentHealth / 12 + 1` locked at the ACTIVATE turn; `getMove` never reads its rolled `num`, so the draw count is pinned; no new powers, intents or `cards.yaml` edit. **All three Act-1 BOSS encounters are now live** · [log](stage-b-log.md#b322)
- **B3.23** `[x]` Potions — `potions.yaml` with 33 Ironclad-pool rows in pool order; `potions.hpp/.cpp` `use_potion`; trap-14 rejection-sampling identity roll (draw-count pinned); `potion_slot_count(A20)`==2; +19 tests, 232/232 · [log](stage-b-log.md#b323)
- **B3.24** `[x]` Relics: starter + commons — 34 relic rows (Burning Blood + 33 commons), ids 1-34; a distinct `RelicHook` framework dispatching in **acquisition order** (trap 8) with DATA/native bindings; +18 tests, 250/250 · [log](stage-b-log.md#b324)
- **B3.25** `[x]` Relics: uncommons — 30 rows, ids 36-65; the canonical pre-shuffle UNCOMMON `pool_order` recovered by inverting the JDK shuffle against 3 live captures; `RelicHook` `ON_MONSTER_DEATH`=14 / `ON_SHUFFLE`=15; power `NEXT_TURN_BLOCK`=23; Paper Phrog retires stage-a A4.1's “unreachable” note; 454/454 ×3 · [log](stage-b-log.md#b325)
- **B3.26** `[x]` ∥ Relics: rares + shop — 46 rows (28 RARE + 17 SHOP + Odd Mushroom SPECIAL), ids 66-111; powers BUFFER=28 / INTANGIBLE=29, `RelicHook::ON_BLOCK_BROKEN`=16, an `at_turn_start_post_draw` relic dispatch phase, and one `heal_player_with_relics` seam so Magic Flower cannot be forgotten at a heal site; RARE/SHOP `pool_order` recovered by **inverting the JDK shuffle** against ten live captures and validated by reproducing B3.25's UNCOMMON order; discharges Odd Mushroom ×1.25, Calipers, Ice Cream (no-relic path byte-identical) and the RARE+SHOP `pool_order` rows; **Prismatic Shard is a deliberate no-op** with an exact, live pool slot · [log](stage-b-log.md#b326)
- **B3.27** `[x]` ∥ Relics: boss (Neow pool) + event-specials — 31 rows (22 BOSS ids 112-133 + 9 Act-1 SPECIALs ids 134-142), power CONFUSION=59; BOSS `pool_order` recovered by the same shuffle inversion and **validated, not fitted** — the same inversion reproduces the committed COMMON/UNCOMMON/RARE/SHOP orders id-for-id; Snecko Eye drove a new `AT_PRE_BATTLE` relic dispatch with its `cardRandomRng` accounting pinned against an independently derived stream; `gain_gold` is now the single run-layer gold door; discharges the BOSS `pool_order` rows and the translator's all-tier `relicPools` un-deferral · [log](stage-b-log.md#b327)

---

## Phase B4 — The run layer (Gate G6 = M3)

- **B4.1** `[x]` Map path generation — header-only `map_gen.hpp` re-expressing MapGenerator bit-for-bit on `mapRng` (incl. the H5 `getCommonAncestor` bug, proven load-bearing); edges match the oracle **node-for-node for all 20 seeds**; stop-the-line finding: `setEmeraldElite` DOES fire in S1; 174/174 ×3 · [log](stage-b-log.md#b41)
- **B4.2** `[x]` Room-type assignment — header-only `map_rooms.hpp`: quotas (elite ×1.6 at asc≥1), trap-12 raw-XS128 `Collections.shuffle`, placement rules, fixed rows, the emerald draw; room symbols **and** the post-`generateMap` `{counter,s0,s1}` triple match the oracle for all 20 seeds; 201/201 ×3 · [log](stage-b-log.md#b42)
- **B4.3** `[x]` RunState population + additive fields (schema v2) — `sizeof(RunState)` 1648→2184, **schema 2→3**; pity floats/purgeCost/potion slots/membership bitsets/relic-pool storage added; map reoriented to game-native 15×7 (rename only); combat relic mirror (`sizeof(CombatState)` 3504→3672); 20 fixtures regenerated with a byte-level insertion proof; 273/273 ×3 · [log](stage-b-log.md#b43)
- **B4.4** `[x]` Run-level advance + room lifecycle — `RunController` + `run_begin` / `next_room_transition` (floor++ then reseed, trap 7); NEOW/MAP/COMBAT/REWARD/RUN_OVER phases in one heterogeneous batch; USE_POTION at both layers; combat spawn + fold-back; +19 tests, 387/387 ×3 · [log](stage-b-log.md#b44)
- **B4.5** `[x]` Combat rewards — assembly (gold / elite relic / potion / 3-card pick with pity, the no-dupe re-roll and the Act-1 upgrade draws) plus the transient `RewardScreen` claim flow, **no schema bump**; **oracle spot-diff PASSED** on campaigns `b45_rewards_oracle_20260727T204809Z_claude01` (STS00042/43) and `b45_rewards_oracle2_20260727T204809Z_claude01` (STS00048/49/51/52): **13 combat reward screens over 6 runs, all 13 zero-diff** on gold / potions[] / master_deck[] / both pity counters / cardRng+treasureRng+potionRng+relicRng; the CardLibrary HashMap **library order is now computed** in `emit/cards.py` and reproduces **27/27** captured offer identities (registry-id order: 0/27), discharging the B3.6/B3.10b/B3.11 pool-order obligation; 1322/1322 ×3 · [log](stage-b-log.md#b45)
- **B4.6** `[x]` Relic pools + acquisition — `relic_pools.hpp/.cpp`: 5 unconditional relicRng shuffles (JDK-LCG route), front/end pop, 50/33/17 tier roll, canSpawn re-check + Circlet fallback, acquisition in trap-8 order with pickup effects; 3-seed live-oracle pool + `(s0,s1,counter)` match; 428/428 ×3 · [log](stage-b-log.md#b46)

- **B4.7** `[x]` Treasure rooms — chest size roll, single-roll gold+tier
  (trap 16), gold ×(0.9,1.1), relic grant via B4.6, fixed treasure row; strict
  descriptor/capacity authority with fallible copy-commit transactions;
  Matryoshka + Cursed Key + N'loth's Mask chest hooks discharged in code;
  oracle spot-diff `[x]` 2026-07-28: **both captured treasure floors
  zero-diff** via the new `replay_run_diff --treasure` mode (STS00052 floor 5,
  a ?-node that rolled TREASURE, Medium/RARE, skipped; STS00054 floor 9, a `T`
  node, Small/COMMON, opened — a corpus sweep proves these are the only two;
  together they cover both entry routes, two sizes, two tiers, the open and
  the skip). The sapphire-key expected shape held, and the capture claimed the
  KEY — the read-out reproduces the abandoned base relic exactly
  (`RewardItem.java:317-322`). The gold roll is unexercised by any live
  capture and stays on tier-2 · [log](stage-b-log.md#b47) ·
  [read-out](stage-b-log.md#b47-readout)

### B4.8 `[x]` Shop
**Deps:** B4.5, B4.6, B3.23 · **Spec:** design §5.6 · **Provenance:**
ShopScreen.java:130-244, 246-292, 340-428, 592-672, 969-978; Merchant.java:
57-97; ShopRoom.java:29-77; StoreRelic.java:36-120; StorePotion.java:33-101;
AbstractCard.getPrice :1915-1937; AbstractRelic.getPrice :173-201;
AbstractPotion.getPrice :381-394; AbstractDungeon.getCardFromPool :1538-1577
and getColorlessCardFromPool :1579-1595; CardGroup.getRandomCard :498-552;
AbstractRoom.getCardRarity :148-178; AbstractPlayer.loseGold :697-717;
MealTicket.java:31-38; MawBank.java:38-53
**Dependency override — authorised.** B4.5 is `[!]` (code landed,
capture-blocked), so this task's `Deps:` line was not formally satisfied when
it started. The project owner explicitly authorised implementing B4.8 ahead of
B4.5's acceptance on 2026-07-27, with the capture pipeline running
concurrently. Nothing here reads B4.5's blocked leg: the shop's own reward
machinery is the relic/potion/master-deck doors, all of which are `[x]`.
**Deliverables:** stock generation (5 colored + 2 colorless, 3 relics incl.
end-pop + SHOP tier slot, 3 potions), pricing (base × jitter, colorless ×1.2,
A16 ×1.1, sale card /2), purge (75 + 25 ramp, persistent), purchase/purge as
CHOOSE flow, merchantRng draw-order exactness.
**A brief correction — "0.3 rare chance" is not the stock roll.** The
deliverables line inherited that number from design §5.6's Neow-era phrasing;
the shop's two colourless slots are **fixed UNCOMMON then RARE**
(Merchant.java:84-85) and take no chance roll at all.
`AbstractDungeon.colorlessRareChance` is read only by The Courier's colourless
RESTOCK (ShopScreen.java:601), which is the half of that relic this task did
not implement. Recorded here rather than silently dropped.
**Acceptance:** tier-2: full stock + prices for a fixed merchantRng state
match hand-derivation draw-for-draw; purge ramp persists across two shops;
oracle spot-diff of a shop floor (stock, prices, sale index) zero-diff.
- [x] tier-2 draw order — `ShopDrawOrder.SixteenMerchantDrawsInTheJavaOrder-
      ForAFixedState` replays all three streams beside the engine and compares
      every price; the sixteen-draw table is a comment in `shop.hpp` and in the
      test.
- [x] purge ramp across two shops — `ShopPurge.RampPersistsAcrossTwoShops`.
- [x] oracle spot-diff — **five merchants across three seeds, all zero-diff.**
      Read out with `replay_run_diff --shop` over runs STS00054, STS00057 and
      STS00074 of `b47_treasure_oracle_20260727T204809Z_claude01`, the three
      that reached a shop. Per merchant the mode seeds a `RunState` from the
      capture's pre-entry record, calls `generate_shop`, and compares the
      seven card ids and prices, the three relics, the three potions, the
      purge cost and `purge_available` against the captured shelf; the sale
      slot is inferred from the capture alone (the one colored price at about
      half its own base) and checked against `shop.sale_index`; and
      `merchantRng` +16 exactly on all five, `cardRng` +12-or-more (two of them
      spent a dedupe re-roll), `potionRng` +3-or-more, all five relic pools
      and `card_blizz_randomizer` are compared against the first in-room
      record. Then it restarts from that record and walks the visit, diffing
      the whole `RunState` after every purchase. Four of the five have a
      visible shelf (STS00054's floor-2 merchant was built and never opened,
      so only its streams and pools are checkable — and they are clean); the
      set covers two purchases, a potion buy, a 75-gold purge with its ramp to
      100, and two runs holding two merchants each, where the second builds
      off the first's end-popped pools. Three visits walk clean end to end;
      two stop after every purchase is verified, at an out-of-combat potion
      discard the run layer has no door for (B1.6's row). Captures recorded in
      [b48_shop_spotdiff.md](../tools/oracle_bridge/driver/b48_shop_spotdiff.md)
      §6.
**A recorded capture already reproduces one whole merchant.**
`ShopCapture.B13Seed1790050543758Floor3MatchesTheRecordedMerchant` rebuilds a
real A20 shop — b13 sweep run `STS00008`, floor 3 — from that capture's
pre-entry stream triples and relic pools, and matches all seven cards, three
relics, three potions, every price, the sale index and the post-build state of
`cardRng` (9→21), `merchantRng` (0→16) and `potionRng` (3→10). That is not the
acceptance leg (one shop, from another task's campaign, with no purchase), but
it is what pins the three base-price tables — and the acceptance capture added
a second such vector,
`ShopCapture.B47Seed1790050543999Floor3MatchesTheRecordedMerchantAndItsPurchases`,
which does carry the purchases: `sts-classes.jar` carries no inner
classes, so CFR emitted `AbstractRelic.getPrice`'s switch with `$SwitchMap`
indices and no constant names, and the tier→price assignment is not recoverable
from the decompiled source alone.
**Inherited — DISCHARGED:** Meal Ticket's 15-HP `justEnteredRoom` heal, for a
static ShopRoom **and** for a ?→Shop. The fan-out runs after the ?-roll has
replaced the room object and after `setCurrMapNode`
(AbstractDungeon.java:1763-1789), so the two paths are the same room; both are
named tests. Shop `screen_state` translation, discharged on the B4.11/B4.14
terms — registry-joined (potion ids), type-checked (every `price`,
`purge_cost`, `purge_available`), storage-less, because translation outputs
`RunState`/`CombatState` and a merchant is derived state the game rebuilds from
`(seed, merchantRng.counter)`.
**Inherited — PARTIALLY discharged, row updated not closed:** The Courier. Its
`x0.8` price discount and its purge-cost branch are LIVE (they are inside
`ShopScreen.init` and `purgeCard`, which this task implements in full). Its
RESTOCK is not, and the reason is not effort: the card branch draws with
`useRng=false`, i.e. off libGDX's **unseeded** `MathUtils` global rather than
`cardRng` (ShopScreen.java:615-617), so the replacement card's identity has no
reproducible answer. The relic and potion restocks (StoreRelic.java:105-112,
StorePotion.java:86-89) are seeded and could be encoded, but landing half a
relic's behaviour behind a hard blocker is worse than naming the blocker.
**Namespace values taken:** `RunPhase::SHOP = 10` and fuzz `MoveCat::SHOP = 26`
(`COUNT` 26→27), both pre-authorised in the allocation table above.
**A latent run-setup bug this task found and fixed.** `run_begin` never
initialised `RunState.purge_cost`, so a value-initialised run opened its first
merchant offering card removal for **0 gold**. `ShopScreen.purgeCost` is a
STATIC in the game, reset only by the dungeon reset that precedes a new run
(CardCrawlGame.java:478 → ShopScreen.java:241-244) — which is exactly why the
reset exists. Now spelled in `run_begin` beside the other
dungeonTransitionSetup fields.
**Log:** [implementation](stage-b-log.md#b48) ·
[oracle spot-diff read-out](stage-b-log.md#b48-readout)

- **B4.9** `[x]` Rest sites — Java-order campfire menu and CHOOSE flows for Rest/Smith/Lift/Toke/Dig; base 30% + Regal Pillow heal, Dream Catcher direct card reward, Girya/Peace Pipe/Shovel effects and exact RNG/pool order; independent-audit fix-forwards close fixed master-deck/relic-cap and malformed reward-screen legality, including zero-card offers, without changing valid skip/proceed or Circlet stacking; no schema or combat `ActionMask` change; full three-preset suite green · [log](stage-b-log.md#b49)

- **B4.10** `[x]` Event framework + ?-room resolution — the one-committed-draw
  `eventRng` contract (roll straight from the stream; selection on a discarded
  throwaway copy) with a byte-identity regression guard; exact Java-order
  EventHelper.roll (asymmetric table clamps, trap-19 float pity, Tiny Chest
  after-the-draw `== 4` force observed by all pity updates, Juzu conversion
  before the monster-pity reset, leaving-a-shop column zeroing); ? rooms now
  resolve to real monster/treasure/shop rooms (fixing the monster-cursor
  consumption bug that read the static map instead of the resolved room);
  `generate_event` selection over draw-time-filtered pools + both-list removal
  bookkeeping into the B4.3 bitsets; the translator now maps the three oracle
  remaining-list arrays through generated ids, validates canonical
  removal-only order, derives cumulative fired flags, and fails loud on
  wrong-list/duplicate/unknown entries; Ssserpent Head and unused Maw Bank
  original-EventRoom entry hooks fire before resolution through the
  Ectoplasm-aware gold door; `RunPhase::EVENT_DIALOG = 9` +
  `MoveCat::EVENT_OPTION = 23` claimed per the allocation table; dialog
  framework proven through a controller-aware synthetic seam, including
  body-owned transitions — all 31 native bodies stay B4.11-B4.13 and park
  after exact bookkeeping. The B3.27 event-screen relic shares and transient
  screen-content translation pass to B4.11-B4.13 ·
  [log](stage-b-log.md#b410)

- **B4.11** `[x]` Exordium events I — six native Java-order dialog bodies;
  generated registry dispatch; reusable event grid; Dead Adventurer's
  escalating search, three encounters, preserved event rewards and awake
  Lagavulin; Event-room combat lifecycle; EVENT screen validation; directed,
  A15, fuzz/hash and three-preset acceptance ·
  [log](stage-b-log.md#b411)

- **B4.12** `[x]` ∥ Exordium events II — five native Java-order dialog
  bodies; Liars Game's two Agree pages and obtain-before-gold payout; Living
  Wall's arbitrary deck grids plus one-draw same-color transform; Mushrooms'
  preserved-stream three-Fungi combat and ordered event rewards; Scrap Ooze's
  exact 26/100 initial threshold and post-lethal ordering; Shining Light's
  owner-aware NORMAL damage plus unconditional JDK shuffle/random-two; normal
  curse obtains now honor Omamori centrally; tier-2 every option/A15 and full
  three-preset acceptance · [log](stage-b-log.md#b412)

- **B4.13** `[x]` ∥ Shrines + one-time specials — the 6 shrines + the eight
  Act-1-reachable specials (reachable set corrected against getShrine's
  per-key gates; machine-checked in `one_time_specials_test`), with
  transform/remove/upgrade mechanics and per-event stream attribution.
  Acceptance closed 2026-07-28 in two oracle halves: the ARRIVAL sweep (88 of
  88 captured ?-room sightings zero-diff via `--event`, 18 distinct events,
  live gate evidence) and the **Match and Keep DEAL read-out** (6 of 6
  captured interactions DEAL OK — 30 capture-named screen positions
  position-for-position through the fork patch's `(i%4)+4*(i%3)` layout, 30
  attempt outcomes, 60 grid rounds walked, kept-card multisets; cardRng +5,
  miscRng +1, shuffleRng unmoved with floor streams DERIVED not copied).
  NoteForYourself's profile pin discharged by direct profile inspection.
  Honest residue, out of S1 capture reach (A20-only pipeline): the
  `ascension < 15` colorless branch — the only `shuffleRng` consumer — and
  NoteForYourself's pool membership stay tier-2-only ·
  [log](stage-b-log.md#b413) · [read-out](stage-b-log.md#b413-readout)

### B4.14 `[x]` Neow
**Deps:** B4.4, B4.6, B3.27 (boss pool) · **Spec:** design §5.6; §10 trap 17
· **Provenance:** NeowEvent.java:62, 163, 289, 349-371; NeowReward.java:
68-128, 190-368
**Deliverables:** the four-category blessing (fresh `Random(seed)` — trap
17), category tables incl. cat-2 drawback-first roll order, all payout
activations (cards via NeowEvent.rng, relics/potions via relicRng/potionRng
pools), cat-3 boss swap, mini-blessing path flagged out-of-scope if
unlock-gated (fully-unlocked profile ⇒ full blessing; record the check).
**Acceptance:** tier-2: option sets + payouts for fixed seed match
hand-derivation draw-for-draw; oracle spot-diff of the Neow screen across
≥ 10 seeds zero-diff (options AND post-choice state).
- [x] tier-2 — `neow_test`: the blessing roll against two hand-derived seeds,
      the category tables and cat-2's drawback-first order, every payout's
      stream attribution, the drawbacks and both grids.
- [x] oracle spot-diff, ≥ 10 seeds, options AND post-choice —
      **35 of 41 captured seeds zero-diff on all three checkpoints.** Read out
      with `replay_run_diff --neow` over the 41 runs of
      `b45_rewards_oracle_20260727T204809Z_claude01` (5),
      `b45_rewards_oracle2_...` (6) and `b47_treasure_oracle_...` (30), all
      strict-validated. Per seed the mode compares the four option MEANINGS
      against the capture's localized labels, then the whole translated
      `RunState` — `neowRng` and `purge_cost` included, since a floor-0 record
      carries both — at the blessing screen, immediately after the option is
      pressed, and at the first map record. Six seeds are excluded and each is
      named: STS00045/46 (Empty Cage) and STS00052/54 (Astrolabe) take a boss
      relic whose `onEquip` opens a grid the sim defers, so they stop after the
      ACQUISITION comparison, which is clean; STS00076 takes the three-potion
      blessing and then discards a potion out of combat, which the run layer
      has no door for (B1.6's row); STS00068 is the one real divergence and it
      is not Neow's — see the obligations table's Centennial Puzzle row. The
      capture ran the payout table wide: 12 boss swaps, 4 common-relic, 2
      colorless (the cardRng-split trap), 2 three-potion (the pity trap), 2
      transform-two, 1 curse-drawback-plus-colorless, and 18 distinct option
      meanings in all. Read-out details in the Log; captures recorded in
      [b414_neow_spotdiff.md](../tools/oracle_bridge/driver/b414_neow_spotdiff.md)
      §6.
**A real divergence this read-out found and fixed — the transform pool order.**
`AbstractDungeon.returnTrulyRandomCardFromAvailable` (`:1016-1045`), which
Neow's TRANSFORM payouts reach through `transformCard`, builds its candidate
list from `commonCardPool` ++ `srcUncommonCardPool` ++ `srcRareCardPool` — one
LIVE pool and two `src*` COPIES. `initializeCardPools` fills every copy with
`addToBottom`, which is `group.add(0, c)`, a PREPEND (`AbstractDungeon.java:
1180-1199`; `CardGroup.java:459-461`), so the last two blocks are their
rarity's library order REVERSED. `transform_card` walked all three forwards —
correct for the first block only, and indistinguishable from correct until
B4.5 pinned the library order itself. Two captured seeds caught it, on four
draws, all four now reproduced exactly: `NeowCapture.TransformTwoReproducesThe-
CapturedIdentities` freezes them and `NeowGrid.TransformReadsTheSrcPoolsBack-
wards` pins the shape without a seed.
**Mini-blessing verdict — NOT an unlock gate, and unreachable here.** The
branch is `bossCount == 0 && !Settings.isTestingNeow → miniBlessing()`
(NeowEvent.java:178-183, mirrored at :168-173). `bossCount` reads the profile
preference `<CLASS>_SPIRITS` **only** on the `Settings.isStandardRun()` branch
(NeowEvent.java:75-80); otherwise it is `Settings.seedSet ? 1 : 0`. And
`isStandardRun() == !isDailyRun && !isTrial && !seedSet` (Settings.java:
633-634), so **every seeded run — which is every simulated run and every
oracle capture — gets bossCount 1 and the full four-option blessing, with no
reference to the profile at all**. That is stronger than the frozen
fully-unlocked assumption and does not depend on `UnlockTracker`, which the
gate never consults. The mini-blessing is therefore outside the model's
domain and is deliberately not encoded.
**Inherited — DISCHARGED:** Neow `screen_state` translation. NeowRoom reports
as an `EVENT` screen carrying the hard-coded id `"Neow Event"`
(GameStateConverter.getEventState :343-355), which is deliberately **not** an
`events.yaml` row — Neow is in no act's event/shrine/special pool and an
`EventId` for it would put a non-pool entry into the three membership bitsets
that pool ids index. The translator recognises the sentinel and gives the
option list the ordinary EVENT-screen validation; storage-less, like the
reward slices.
**Notes for the integrator — design §5.6 corrected (§11 v0.1.9).** Three
findings contradict the design's one-line summary of §5.6, which said payouts
"consume NeowEvent.rng for cards"; per conventions §4 that losing text is
fixed inline in §5.6 and logged as §11 v0.1.9 in this same change. Each has a
named test: (1) the COLORLESS blessings draw
their card IDENTITIES from `cardRng`, not NeowEvent.rng —
`getColorlessCardFromPool` goes through `CardGroup.getRandomCard(true,
rarity)`, whose `true` means `AbstractDungeon.cardRng` (CardGroup.java:
509-524) — while their rarity rolls stay on NeowEvent.rng; the CURSE
drawback's card is `cardRng` too (`getCardWithoutRng(CURSE)` →
`returnRandomCurse` → `CardLibrary.getCurse()`), and it is drawn AFTER the
payout's own draws because the Java obtains it one `update()` tick later.
(2) The three-potion blessing opens the COMBAT reward screen, whose
`setupItemReward` appends a full `getRewardCards()` row for a NeowRoom
(CombatRewardScreen.java:72-96) that `NeowReward` then deletes — so cardRng
advances and the card-pity counter moves even though no card is offered.
(3) The boss swap's `loseRelic` runs BEFORE its pool draw, so Black Blood
(canSpawn = hasRelic("Burning Blood")) can never be its result. Also: the
colorless RARITY pools are ORDER-EXACT for a different reason than the rest —
`CardGroup` sorts the rarity-filtered view by cardID before indexing it, so they
never depended on the CardLibrary library order B4.5 later pinned.
**Log:** [implementation](stage-b-log.md#b414) ·
[oracle spot-diff read-out](stage-b-log.md#b414-readout)

- **B4.15** `[x]` A20 run-setup modifiers + negative freezes — `registry/a20.yaml` populated to one row per ascension level 1..20 (`id == level`), each IMPLEMENTED or N/A-for-S1-with-reason, machine-checked by `A20Manifest.EveryRowCarriesScopeProvenanceAndAnS1Status`; run-setup order corrected to **A11 → (A5) → A14 → A6 → A10 → starting deck**, so A14's max-HP loss precedes A6's 90 % rewrite and an A20 Ironclad is **68/75, not 72/75** (matches the G4 oracle capture); Ascender's Bane lands at master-deck **index 0**, ahead of the five Strikes, routed through `add_card_to_master_deck`; retires the A6/A10/A14 deferred-obligation row; union 641/641 ×3 · [log](stage-b-log.md#b415)

### G6 `[ ]` **Gate: S1 rules complete (M3)** — tag `g6-s1-content`
**Deps:** all B3.*, all B4.*
Checklist (evidence linked in Log):
- [x] 100 % tier-2 registry coverage: every manifest row has named passing
      tests (scripted check, `tools/verify_report/`). Evidence:
      `tools/verify_report/check_tier2_coverage.sh debug` on the Wave-1c
      integration tree — every manifest row covered (rows re-derived from the
      regenerated manifest; one genuinely uncovered row found and honestly
      closed: encounters/THE_MUSHROOM_LAIR gained a real registry-data test
      case, not checker tuning), `VERDICT: PASS` exit 0; report at
      `build/debug/verify_report/tier2_coverage.{md,json}`; determinism and
      fail-loud both proven (an injected test failure flips the row UNCOVERED
      and the exit code). Re-run at the gate tag per protocol.
- [x] Sim-only soak: 1,000-seed random-policy full Act-1 runs complete (win,
      die, or legal-action exhaustion — never an assert/illegal state) in
      debug + asan. Evidence: `fuzz_soak --seeds 1000 --policies random` on
      the `fix-postboss-shop` tree (6d7efc4) — debug `failures: 0` exit 0,
      asan `failures: 0` exit 0, all 1000 cases `run_over`. Run
      POST-victory-terminal deliberately: an earlier formally-clean 1000-seed
      run on 40cfbc2 was rejected as gate evidence because random play never
      reaches the boss, and a 300-seed `always_event` probe then proved the
      WIN terminal was missing (`no_legal_moves` after the boss-reward
      proceed) — the leg's "win" word was untestable on that tree. The probe
      also re-ran clean post-fix: 300/300 `run_over`, `deaths 299 victories
      1`, shop entered 196. Re-run at the gate tag per protocol.
- [x] Oracle spot campaign: ≥ 20 full-run seeds, Neow through boss reward,
      **zero un-triaged diffs** through the run-level differ. Evidence, all
      strict-validated (`--require-oracle --campaign`): **75 captured runs
      across three greedy campaigns** — `g6_campaign_20260728T053354Z_claude01`
      (30 seeds), `g6_campaign2_20260728T153342Z_claude01` (the same 30 under
      driver b1.4.7), `g6b_boss_ps1234_20260728T163540Z_claude01` (6, greedy
      b1.5.0) plus 13 supplementary boss mini-campaigns (39 runs). **The
      Neow-through-boss-reward span is captured and claimed**: STS01221's
      `act1_boss_reward` terminal (the claim chain seq 184-189 in
      `g6_campaign2_spotdiff.md`'s successor runbook), the first in the
      project's history. **The run-level differ leaves zero un-triaged
      diffs and zero class-(a) divergences**: every stop and every diff in
      all 75 runs carries a class ((b) harness mapping frontier, chiefly the
      documented run-layer SHOP_ROOM limit; (c) named deferred bodies, now
      reduced to FAIRY_POTION and peers; (d) capture artifacts) with capture
      + Java evidence in `g6_campaign_spotdiff.md` / `g6_campaign2_spotdiff.md`
      and the closing-wave commit messages. The campaign work itself found
      and fixed eight real engine divergences (starter upgrades, Stone
      Calendar ordering, emerald-elite roll, Red Skull decider, Explosive
      Potion typing, discovery skip + RNG model, and the transform-pool and
      Centennial fixes that preceded it) — the gate's purpose, served.
- [x] All Stage A tests + fixtures still green (schema bumps accounted).
      Evidence: the closing-union three-preset suite (which includes every
      Stage-A test) green; `SCHEMA_VERSION` unchanged at 6 since B3.11 — all
      20 combat fixtures replay byte-identical, re-verified by every union
      run this session.
Then: update CLAUDE.md "Current state".
**Log:** —

---

## Phase B5 — Verification campaigns + S1 exit (Gate G7 = M4)

- **B5.1** `[x]` ∥ Sim self-replay fuzz soak — deterministic random-legal +
  four E0 heuristic policies, replay-twice per-step/final controller hashing,
  `STSFUZZ v1` mismatch reproducers + pre-crash journals, shardable coverage
  reports, and `soak.sh`; acceptance release sweep **10,808,430 actions /
  10,000 seeds / 250,000 cases**, zero failures, plus ASan-clean **2,500 /
  250,000 cases = 1.00 %** disjoint sample; 959/959 ×3 ·
  [log](stage-b-log.md#b51)

### B5.2 `[ ]` ∥ Oracle campaign automation
**Deps:** B1.4, B4.4 · **Spec:** design §7.1-7.3
**Deliverables:** campaign orchestration (seed sharding, nightly schedule,
resume, artifact root fixed per §7.3, translation + diffing pipeline run as
one command); triage queue (divergence → reproducer → this ledger/change-log
workflow); promoted-reproducer corpus layout.
**Acceptance:** one unattended overnight campaign (≥ 50 seeds) runs
end-to-end (game → JSONL → traces → diff reports) without intervention;
throughput and diff counts land in a generated report.
**Inherited:** triage the `b14_accept2` obtain-race capture-fidelity gap — deferred by
B1.3 (B1.4's acceptance is unaffected).
**Log:** —

### B5.3 `[ ]` ∥ Tier-4 distributional suite
**Deps:** G6 · **Spec:** design §3.4 (analytic-first scope)
**Deliverables:** `tools/dist_check/`: the §3.4 analytic expectation set
(encounter weights, compositions, rarity+pity dynamics, potion ratchet,
chest tables, ?-room pity, relic/potion tier rolls, map quotas) with
chi-square/exact tests over ≥ 10k sim seeds; the ≥ 200-run oracle-harvested
spot comparison.
**Acceptance:** all analytic tests pass at pre-registered significance
(document the correction for multiple comparisons in the tool README —
choose and justify); oracle spot set shows no flagged aggregate; failures
are stop-the-line divergences, not tuning targets.
**Log:** —

### B5.4 `[ ]` Verification report + CI corpus
**Deps:** B5.2 · **Spec:** design §7.4-7.5, §7.1(1)
**Deliverables:** `tools/verify_report/` (diffs per million actions,
divergence inventory, per-registry-row oracle-sighting + tier coverage join
via `game_id`); latest report committed under `docs/verification/`; the
curated 50-seed CI corpus (compressed translated traces +
zero-diff-replay gtest wired into the existing CI matrix).
**Acceptance:** CI runs the 50-seed replay smoke in seconds and fails on an
injected synthetic divergence (proven once, then reverted); report
regenerates deterministically from campaign artifacts.
**Log:** —

### B5.5 `[ ]` Throughput floors
**Deps:** G6 · **Spec:** design §8(4); InitialPlan §0.2 floors
**Deliverables:** benchmark additions: full-combat/sec/core (random policy)
and full-run/sec whole-machine on `bench_advance`'s pattern; methodology
notes (random-policy stand-in for 25-sim MCTS, per design §8).
**Acceptance:** release-preset numbers recorded here: ≥ 50k combat
steps/sec/core, ≥ 300 combats/sec/core, ≥ 0.4 runs/sec whole-machine — or a
stop-the-line design-doc amendment with profiling evidence (fast-but-wrong
is death; slow-but-honest gets a Stage C plan).
**Log:** —

### G7 `[ ]` **Gate: S1 verified (M4)** — tag `g7-s1-verified`
**Deps:** B5.1-B5.5, G6
The design §8 bar, checked literally (evidence linked in Log):
- [ ] ≥ 1,000,000 oracle-diffed actions across ≥ 2,000 seeds, zero
      un-triaged diffs (campaign reports).
- [ ] ≥ 10M sim-side fuzz actions, zero nondeterminism/asserts (B5.1).
- [ ] 100 % tier-2 registry coverage (manifest check, re-run at gate).
- [ ] Every a20.yaml row verified per design §8(3).
- [ ] Throughput floors hold (B5.5 numbers).
- [ ] Tier-4 suite green (B5.3).
Then: update CLAUDE.md "Current state"; open Stage C planning (perf
hardening) and the S2 scope conversation as fresh planning exercises.
**Inherited:** revisit the Infernal-Blade-generated Blood for Blood cost model — B3.6
models it via `cost_now` only, so an end-of-turn reset restores 4 rather than the
game's reduced base; judged unreachable, “revisit if G7 ever hits it”.
**Log:** —

---

## Parallelism map

```
B0.1 ─┬─▶ B1.1 ─▶ B1.2 ─┬─▶ B1.3 ──────────┐
B0.2 ─┘                 └─▶ B1.4 ─▶ B1.5 ─▶ B1.6 ─▶ G4     (B0.1 ∥ B0.2)
B2.1 ─▶ B2.2 ─▶ G5                                          (Phase B2 ∥ Phase B0/B1)

G4 + G5 ─▶ B3.1 ─▶ B3.2 ─┬─▶ B3.3 … B3.11 (card batches ∥)
                         ├─▶ B3.12 ─▶ B3.13 … B3.22 (monster batches ∥;
                         │            B3.17 ─▶ B3.20; B3.14 ─▶ B3.17)
                         ├─▶ B3.23 (potions)
                         └─▶ B3.24 ─▶ B3.25 ∥ B3.26 ∥ B3.27 (relics)

G4 + G5 ─▶ B4.1 ─▶ B4.2 ─▶ B4.3 ─▶ B4.4 ─┬─▶ B4.5 ─▶ B4.8
   (B4.1-B4.3 ∥ with Phase B3)           ├─▶ B4.6 ─▶ B4.7, B4.8
                                         ├─▶ B4.9
                                         ├─▶ B4.10 ─▶ B4.11 ∥ B4.12 ∥ B4.13
                                         ├─▶ B4.14 (also needs B4.6, B3.27)
                                         └─▶ B4.15
all B3.* + all B4.* ─▶ G6
B4.4 ─▶ B5.1 ∥ B5.2 (campaigns start before G6; DoD volume accrues)
G6 ─▶ B5.3 ∥ B5.5 ; B5.2 ─▶ B5.4 ; B5.1-B5.5 ─▶ G7
```

## Change log

- 2026-07-27 — **B4.5 `[!]` → `[x]`: the oracle spot-diff ran and passed, and
  the card-pool library order turned out to be computable rather than
  recoverable.** Two operator-launched campaigns
  (`b45_rewards_oracle_20260727T204809Z_claude01`,
  `b45_rewards_oracle2_...`; seeds STS00042/43/48/49/51/52 reached a reward
  screen, five others died in the floor-1 fight) yielded **13 combat reward
  screens, all 13 zero-diff** on the acceptance's whole field table. The
  read-out is a committed binary, `replay_run_diff`, which seeds the engine from
  the translated `RunState` at combat end rather than re-driving the run — which
  is what made it immune to three of the six runs being un-replayable behind
  deferred Neow boss-relic bodies. The predicted library-order deviation showed
  up on 8 of 13 claims and was then closed **without** empirically recovering
  the order: a Java `HashMap`'s iteration order is a function of the keys, the
  final capacity and insertion order, so `emit/cards.py` computes it, and the
  capture *checks* the computation at **27/27** offered identities (old order:
  0/27). One subtlety earned its own helper: `addToTop` appends but
  `addToBottom` prepends, so the `src*` combat pools are the reward pools
  reversed per rarity and concatenated common-then-uncommon-then-rare. The
  B3.6/B3.10b/B3.11 pool-order obligation row is struck; B1.6's `replay` row is
  narrowed, not discharged; two genuine combat-layer gaps the whole-run replay
  found (monster block never cleared at turn start, Vulnerable/Weak never
  ticking down) are new rows. [Archive log.](stage-b-log.md#b45-spotdiff-readout)
- 2026-07-27 — **B3.10c landed: Purity and Forethought live, and with them the
  engine's first NON-COUNTED choice.** Every prior `CHOOSE_CARD` selected a
  fixed number and ended when it was met; a zero-to-N screen ends on a button,
  so `ActionVerb` 4 `CONFIRM` is a public `advance()`/`ActionMask` addition and
  `CHOOSE` becomes a toggle while such a screen is open. It cost **no schema
  bump and no new opcode**: the picks are held as the hand's trailing suffix
  with a four-bit count in the open item's `flags`, which is the game's own
  `p.hand` / `selectedCards` split rather than a model of it — and it is why
  pick order, the thing the confirm applies in, needs no separate storage.
  `ChoiceKind` 8 `PUT_ON_DRAW_BOTTOM`, fuzz `MoveCat` 25 `CHOICE_CONFIRM`
  (`COUNT` → 26) and `CardFlag` bit 14 `FREE_TO_PLAY_ONCE` are all spent exactly
  as reserved. The translator's deferred `can_pick_zero` is discharged:
  `HAND_SELECT` is mapped, and the one thing the protocol genuinely cannot
  supply — which manipulation the screen was opened for, since the game keeps
  that in the un-serialized action — is stated at the site rather than invented,
  with every unmodelled shape refused. Gambling Chip's obligation row is now
  **unblocked but still unassigned**: this task deliberately did not expand into
  that relic body. `kCardsCount` 124 → 126 and the colorless UNCOMMON block
  92-111 is dense. [Archive log.](stage-b-log.md#b310c)
- 2026-07-27 — **B3.11 landed: all fifteen colorless rares live; the S1
  colorless card set is complete.** Four staged commits in one worktree under
  the Wave-B allocation, serially integrated after an independent full-matrix
  re-run. Owner-approved `SCHEMA_VERSION` 5→6 (`PowerSlot` counter field,
  instanced powers for The Bomb, `CombatState` combat-gold accumulator) with a
  generalized byte-identical fixture proof over all 20 fixtures. Two
  stop-the-line fix-forwards landed with RED-first tests: the native Sadistic
  body's missing Shackled exclusion (reachable only since B3.10b) and the
  PutOnDeck forced-path `cardRandomRng` billing that Warcry had silently
  omitted; the ≥8 choice-kind packing fix is groundwork B3.10c now inherits.
  Both B3.2-inherited obligation rows are discharged; one new row records the
  stolen-gold clamp ordering corner. Unused contingencies (fuzz `MoveCat` 26,
  `CardFlag` bit 15) are released to free rather than gapped — bit namespaces
  are scarce and nothing encoded them. [Archive log.](stage-b-log.md#b311)
- 2026-07-26 — **B3.10b implemented; no colorless card remains deferred from
  S1.** A full source reread corrected the losing brief: Discovery's no-arg
  `DiscoveryAction` samples the non-healing RED common/uncommon/rare combat
  pool, while Jack of All Trades alone samples the non-healing COLORLESS
  uncommon/rare pool. Both pools are generated from `cards.yaml`; Discovery's
  persisted three-card offer is unique and charges `cardRandomRng` once per
  attempt. Dark Shackles/Shackled and both Enlightenment lifetimes are live.
  The old Purity/Forethought deferral is replaced by mandatory **B3.10c** with
  explicit optional-confirm, choice-kind, fuzz and `FREE_TO_PLAY_ONCE`
  allocations, and B3.11 now states that all fifteen rares must land live.
  The documented-but-silent `cards.yaml native:` trap is discharged by making
  codegen reject that unsupported key with a named negative test.
  [Archive log.](stage-b-log.md#b310b)
- 2026-07-26 — **B4.5 runtime re-pin review fixes stale launch-log evidence;
  B4.5 remains `[!]`.** Independent review reproduced a non-fresh resume in
  which the orchestrator restarted numbering at `mts_launch1.log` while the
  driver selected a preserved higher-numbered log from the prior process, so a
  stale sanctioned stack could be copied into a new header. The same persisted
  config could let a later GUI launch reuse that log. Launch logs are now
  append-only across process resume and bound by exact filename plus a one-use
  nonce inherited through the orchestrator-launched game. Redirects and missing
  bindings fail before artifact/policy. Two missed forward provenance comments
  now name the actual 12-18-2022 decompile. The live spot-diff remains the only
  B4.5 blocker.
  [Archive log.](stage-b-log.md#b45-oracle-stack-repin-fix-forward)
- 2026-07-26 — **B4.5 strict evidence second fix-forward closes six
  independent-review findings; B4.5 remains `[!]`.** The first fix-forward's
  new terminal join exposed the driver's pre-existing boss-claim counter
  split; seed-long identity, terminal ordering/counts, timing bodies, resolved
  cleanup containment, and current fork/schema resume identity also needed
  fail-closed contracts. All now have direct adversarial regressions plus a
  happy strict boss-reward campaign. Default non-oracle validation remains
  backward-compatible.
  [Archive log.](stage-b-log.md#b45-oracle-preflight-second-fix-forward)
- 2026-07-26 — **B4.5 oracle-capture preflight fix-forward closes strict
  campaign false accepts; B4.5 remains `[!]`.** Independent review found that
  strict validation still accepted a header-plus-terminal/all-menu artifact
  with no observed in-game oracle state, and that `--fresh` left old run/timing
  artifacts behind. A later failed attempt could therefore appear supported by
  stale files. Strict `--campaign` now binds complete/failure-free progress and
  manifest identity to the ordered seed ledger and exact run/timing artifact
  sets, including per-file campaign/seed/attempt identity; it rejects missing,
  extra, stale, failed, and zero-observation evidence. Driver/orchestrator
  completion is nonzero for any failed seed, resume identity is checked, and
  `--fresh` performs bounded cleanup only for its exact requested seeds.
  The runbook allocates a new preserved id per preflight/reward attempt.
  Default legacy validation remains compatible.
  [Archive log.](stage-b-log.md#b45-oracle-preflight-fix-forward)
- 2026-07-26 — **B4.5 oracle-capture preflight made fail-closed; B4.5 remains
  `[!]`.** The preserved `b45_rewards` campaign exposed two independent false
  confidence paths: stock CommunicationMod shares the fork's config namespace,
  so it spawned the campaign driver and produced normal-looking artifacts with
  `oracle_block_enabled: false`; and artifact version/mod fields are static
  driver provenance, not proof of the runtime selected by ModTheSpire. A
  missing oracle block on the first in-dungeon dump is now a durable
  `fatal_environment_drift` before artifact creation or policy advancement;
  the orchestrator stops rather than relaunching; and explicit
  `--require-oracle` validation plus a one-seed fork/version/hash/oracle
  preflight gates the reward runbook. The invalid external campaign is
  preserved. The observed newer game/MTS/BaseMod stack is not sanctioned, and
  restore-vs-amend remains an owner stop-line decision.
  [Archive log.](stage-b-log.md#b45-oracle-preflight)
- 2026-07-26 — **run-layer escape deferred obligations discharged; rejected
  Smoke Bomb branch fixed before landing.** The implementation commit
  `82d497a` closed the outcome / stolen-gold / liveness seam and replaced the
  stale `run_advance.cpp` roster comment with the dispatch-table mechanism.
  Independent review then found that the run-layer Smoke Bomb interception
  selected the right reward screen without executing the already-live native
  potion body, so the independent player-escaped flag was absent — most visibly
  when mugged and smoked were both true. The fix-forward preserves the Java
  `use` → relic hook → slot destruction order and adds assertions for both
  ordinary smoke and mugged+smoked precedence. The two satisfied obligation
  rows were deleted; the full provenance and non-task archive entry are under
  **Landed non-task work** above.
- 2026-07-26 — **card-limbo independent-audit fix-forward.** Java keeps
  `exhaust` and `exhaustOnUseOnce` separate and clears only the latter after
  `UseCardAction` filing; append-only `CardFlag` bit 8 now carries that
  one-play lifetime for Havoc and Corruption, so a Spoon-saved or Exhumed
  normally non-exhausting card does not exhaust forever. Terminal lethal
  damage keeps `UseCardAction` in `clearPostCombatActions`, so pending filing
  now performs its Strange Spoon RNG and `onExhaust` fan-out before the
  engine's established immediate halt. No schema or fixture/golden change.
  [Archive log.](stage-b-log.md#card-limbo)
- 2026-07-26 — **the played-card limbo obligation is discharged.**
  `resolve_card_play` now moves the source to `CombatState.limbo`, and an
  engine-emitted queued `USE_CARD` action files it only after its own effects,
  matching `AbstractPlayer.useCard` / `UseCardAction`. Opcode **53** is the
  append-only allocation: B3.10b still exclusively owns live reservation
  49–52, while 41–44 remain permanent gaps and are not backfilled. No schema
  version or committed fixture/golden changed. [Archive log.](stage-b-log.md#card-limbo)
- 2026-07-26 — **`MonsterState.flags` widened to `uint32_t` with a two-region
  allocation policy (`master` at `a32e84c`).** Owner-directed after the wave-A
  reconciliation recorded that the `uint16_t` was full. Recorded in the frozen
  spec as design §11 **v0.1.5**; the **Shared namespaces** section's
  "flags is now FULL" paragraph is replaced by the allocation rule.
  - **The diagnosis mattered more than the width.** Widening alone would have
    re-exhausted the word in Act 2, because the exhaustion was caused by
    **linear** allocation — a fresh bit per monster type, when no monster is two
    types at once. Type-scoped reuse (bits 0–23) plus a small scarce global
    region (24–31) makes 32 bits sufficient for the whole game; the worst single
    type needs five.
  - **A hazard the change exposed rather than introduced:** several flag writes
    were `static_cast<uint16_t>`. After widening, each would have **silently
    cleared the whole global region**, erasing `Escaped`. No compiler diagnostic
    is possible — narrowing through an explicit cast is what the cast requests.
    Removed across eight source files and two tests. Generalised in the
    namespaces section as: **when widening a flag field, hunt the truncating
    writes.**
  - **A refinement to the policy, found in implementation:** a bit consumed by a
    *power's* native body is scoped to that power's possible **owner set**, not
    to a single monster type — the CurlUp and Ritual latch sites key on
    `PowerId`, not `monster_id`. Reuse requires the reusing type can never own
    that power.
  - **The fixture proof was honest about not matching precedent.** B3.12 and
    B4.3 both proved their bumps with a single-zero-run insertion; that shape is
    **unavailable** when an *interior* field widens, because every later offset
    moves and an old alignment pad disappears. Rather than force the analogy, an
    equivalent per-field proof was produced from offsets taken by probes compiled
    against both old and new headers, verifying every meaning-carrying byte
    preserved in order across 20 fixtures / 112 records. **This is the one
    sanctioned exception to never modifying a committed fixture** — owner-
    approved, and nothing under `tests/golden/` outside `combat_fixtures/` moved.
- 2026-07-26 — **wave A landed: B3.15 `[x]`, B3.10a `[x]`, B4.5 `[!]`
  (`master` at `e6ec9ce`).** Three tasks dispatched in parallel on disjoint
  files; two integration passes, `integration-16` (B3.15 + B3.10a) and
  `integration-17` (B4.5). Union green on `debug`/`asan`/`release` with **zero
  NOT_BUILT**, manifest **regenerated** rather than summed: cards 105 / powers 44
  / monsters 25 / relics 142 / potions 33 / events 0 / encounters 20 / a20 20 /
  **total 389**. No committed fixture or golden vector was modified, deleted or
  renamed at any point in the wave — only added to.
  - **Design §5.6 ruling: the elite/boss card-rarity widths are an OMISSION, not
    a conflict — no frozen-doc amendment.** B4.5 found that elite widths are
    **10/40** and boss rewards are **unconditionally RARE**, neither of which
    §5.6 mentions, and escalated instead of amending. Ruling, verified against
    the source rather than the report: §5.6's sentence cites
    `AbstractRoom.java:108-109` — literally the **base** fields
    `baseRareCardChance = 3` / `baseUncommonCardChance = 37` — and `:148-177`,
    the base method. It describes the base room and cites the base fields; it
    **nowhere asserts the values are room-invariant**. `MonsterRoomElite`
    overrides those same fields *in its constructor* (`:34-35`) and
    `MonsterRoomBoss` overrides the method (`:40-42`). The doc therefore covers
    less than the game, which is a gap in coverage, not a statement contradicted
    — §4's stop-the-line bar is not met. Worth recording *why* this was nearly
    misread: `MonsterRoomElite.getCardRarity` **looks** like the mechanism and is
    not — it is gated on `ModHelper.isModEnabled("Elite Swarm")`, a daily mod
    irrelevant to S1, and otherwise defers to `super`. Reading the override alone
    gives the wrong answer; the constructor is the mechanism.
  - **integration-16's only defect came from the integration step itself.**
    Everything checked statically was clean — the union counts, a single
    definition of `monster_dead_or_escaped`, the merged two-pass block function
    against `AbstractCard.applyPowersToBlock:2291-2307`, the two independent
    writers of `tgt` (they cannot collide: `RESHUFFLE_ALL` reads a card-pool
    index behind a `>= kCardPoolCap` sentinel, `roll_random_target` yields a
    monster index only for `CardDef.random_target`), and both branches'
    namespace edits. The defect was **task ids written into a header by the
    conflict resolution** — present on neither branch, so no branch's green run
    could have caught it, and invisible to a clean merge. Only running the suite
    on the merged tree found it (`NoTaskIds`, all three presets). A second
    instance was introduced by the orchestrator's own fix for the stale
    `atDamageFinalReceive` claim, and caught the same way. Both now cite the
    mechanism instead: a `File.java:line` goes stale **visibly**, a task id
    silently. Fixed in `b2a3606`, which also discharges that stale-claim
    obligation — both merged branches touched `interp_damage.cpp`, making this
    integration the "next owner" the row named.
  - **integration-17's risk was semantic, not textual.** Only three files
    overlapped by name, but B4.5 added pool *builders* reading `cards.yaml` while
    B3.10a added 14 cards *to* it — an interaction git cannot see. Verified from
    the regenerated table that the pools are correctly RED-gated: **20/36/16
    unchanged**, `kIroncladAttackPool` still 28. That is B4.5's "confirm
    colorless is unreachable from a combat reward" deliverable **proven rather
    than argued**.
  - **B4.5 is `[!]`, not `[x]`.** All its code landed and is green, but its
    Acceptance requires an oracle spot-diff over ≥ 3 bridge runs and the game is
    launched by hand. §1 says done means the Acceptance **passes**, so it stays
    open with the blocker named and a committed runbook. Its agent stopped
    exactly there rather than claiming the task.
  - **Allocation outcome recorded, and `MonsterState.flags` is now FULL.**
    `kMonsterFlagEscaped = 0x8000` took the last bit of that `uint16_t`; the next
    monster needing one faces either a type-scoped overlap or a schema bump, and
    that decision must not be taken locally inside a content task. Unspent
    allocations (opcodes 41–44, `ChoiceKind` 6–7, `PowerId` 76, `MonsterIntent`
    14) are now permanent gaps. Also recorded: **adding an opcode is a
    three-file change** — `vocab.py`, `steps.py`'s partition assert and
    `cards.hpp`'s drift assert — which no brief had said.
- 2026-07-26 — **B3.10 split, and two brief errors the wave's own agents
  caught.** The dispatched B3.10 agent **stopped without committing** and
  proposed a split instead: the enumeration was right, but ten of the twenty
  cards each need a *distinct* new verb, ~2.5× the allocated opcode budget. It
  declined to land the clean fourteen-card subset too, on the grounds that doing
  so would spend all four opcodes on four of the ten verbs and consume interior
  ids under an allocation about to be revised. Both calls were correct and are
  conventions §5's "propose the split first". Split into **B3.10a** (fourteen
  cards, opcodes 45–48, power 77), **B3.10b** (four cards needing the colorless
  pool, opcodes 49–52, power 78), and **Purity + Forethought** deferred to the
  optional-multi-select owner as a new obligation row.
  - **Two orchestrator brief errors, both found by reading the tree.** (1) The
    briefs named `vocab.py` off limits when `OPCODES` lives in it and *every*
    wave-A task holds an opcode block — the allocation was unspendable as
    written. The rule now names the **namespace**, not the file:
    `MONSTER_INTENTS` exclusive, `OPCODES` shared and append-only per block.
    (2) The count guards were briefed as **four** `kPowersCount` sites; there are
    **five** — `tests/registry_gen_test.cpp:407` was missed, and a branch moving
    only four would fail or, worse, merge silently stale. The full inventory for
    both `kPowersCount` and `kCardsCount` is now in the allocations section, with
    the note that `relic_pools.cpp:207`'s assert asks a real question and is
    answered, not bumped. Both corrections were sent to the two agents still
    running.
  - **`NO_BLOCK` overrides `modifyBlockLast`, not `modifyBlock`** — the game's
    only overrider of that hook, and `applyPowersToBlock` runs the two as
    **separate full passes**. The engine's single-pass `modify_block` yields `N`
    where the game yields `0` whenever a Dexterity sits later in the list. This
    turns Panic Button from a data row into a structural `interp_block.cpp`
    change, and is exactly the kind of thing that would have merged green and
    been wrong.
  - Three further rows added from the same read: the deferred Purity/Forethought
    pair; `cards.yaml`'s documented-but-never-read `native:` field (§8's comment
    class **inverted** — docs asserting a capability the code lacks, and cards
    are the one domain with no native path); and `CardDef`'s missing upgraded-
    **target** column, an `ActionMask` deviation that every effect test would
    pass straight through.
- 2026-07-26 — **ledger/history reconciliation after integration-15 (`master` at
  `34c3e96`, pushed).** A conventions §2 incident of the same shape as the
  integration-12/-13/-14 entry below, and a **narrower, more instructive one**:
  the previous reconciliation missed *tasks*, this one missed a **partial
  landing**. Integration-15 landed `f24b8db` (B3.15's three monsters), `f05ad8a`
  (the post-draw gate) and `39876f0` (the citation audit) — none of the five
  commits since `d3500dc` touched `docs/`, so this ledger still showed B3.15
  untouched, the post-draw obligation "IN PROGRESS", and Pantograph deferred to a
  task that had already found it implemented. **The push was correct and the code
  is sound; only the record lagged.**
  - **Verified before writing, not inferred.** A clean `debug` build of
    `34c3e96` (`STS_JOBS=6 tools/wsl_run.sh debug`) reports **100 % passed, zero
    failed**, independently corroborating integration-15's six-preset claim; the
    count itself is deliberately not restated here — re-derive it with `ctest -N
    | tail -1`, per §8. Manifest
    **regenerated** by `tools/registry_gen/gen.py` rather than summed: cards 91 /
    powers 42 / monsters 24 / relics 142 / potions 33 / events 0 / encounters 20
    / a20 20 / **total 372**. The four `kPowersCount` guards (1 in
    `interp_block.cpp`, 3 in `interp_damage.cpp`) were checked against the
    regenerated value and are consistently at 42 — B3.15 moved all four
    together, so this is **not** a repeat of integration-14's silently-stale
    count assert.
  - **B3.15 is recorded as PARTIALLY LANDED and stays `[ ]`.** Its Acceptance
    names "escape terminal state distinct from kill", which no landed code
    satisfies, so §1's "done only when its Acceptance block passes" keeps it
    open; the landed half is recorded in a **Landed so far** block instead of a
    Log. Its `∥` marker is **dropped**: the remainder owns
    `src/engine/action_queue.cpp` and is no longer parallel-safe.
  - **One new obligation row replaces three separate blockers.** The Looter,
    enemy self-escape, Smoke Bomb's combat body and the stolen-gold return were
    three rows pointing at different-sounding problems; B3.15 proved they are
    **one predicate** — `hp > 0` where the Java reads `isDying || isEscaping`
    (`MonsterGroup.java:90-95,117-122`) — with no alternative seam, because
    `pump_step` recomputes the phase from it every iteration. The predicate now
    has its own row at the head of the table and the other three point at it.
  - **Two rows discharged and deleted.** The post-draw gate row (landed as
    `f05ad8a`; moved to **Landed non-task work** with its provenance, alongside a
    new bullet for the citation audit) and **Pantograph's boss heal** — which was
    never deferred at all by the time B3.15 reached it: the body, the
    `native: true` row and four `RelicHooksPantograph` tests were already live,
    and only the `DEFERRED` markers were stale. **A deferral marker sitting on
    live code is conventions §8's bug signal inverted**, and it is the second
    kind this wave produced.
  - **Two rows added, two narrowed.** Added: the liveness predicate; and the
    **nine pre-existing out-of-range Java citations** the audit found and
    correctly left alone. Narrowed: the un-park row (Blue Slaver, Red Slaver, 2
    Fungi Beasts and Exordium Wildlife un-parked by construction — **only Looter
    and Exordium Thugs remain**) and the `run_advance.cpp` stale-roster comment,
    which got staler precisely *because* the un-park gate needs no edit there.
  - **`PowerId` 60–72 are recorded as a third permanent gap.** B3.15 appended
    `ENTANGLE` 73 / `SPORE_CLOUD` 74 out of its allocated block rather than
    reaching into the free window below it — correct, and the same shape as 47
    and 54–58. The **allocation itself was never written down here**, only in the
    dispatching brief, leaving an unexplained thirteen-wide hole for the next
    reader; the namespaces section now says allocated blocks belong in this file,
    which is the rule that hole exists to teach.
- 2026-07-25 — **the `gen.py` `SET_COST` step-authoring obligation is CLOSED**,
  and B3.26's four deliberately-inert relics **get rows**. Orchestrator
  decisions on the two items the reconciliation entry below surfaced rather than
  applied; both were verified against the tree again before being written here.
  - **`SET_COST` row deleted.** The obligation assumed `SET_COST` would
    eventually need YAML step-authoring, with "the first card consumer" as its
    owner. Both named candidates are spent: Corruption (**B3.8**) and Snecko Eye
    (**B3.27**) both landed with **native** cost rewrites —
    `PowerId::CORRUPTION` (`native: true`, `on_use_card` / `on_card_draw`) and
    `PowerId::CONFUSION` (`native: true`, `on_card_draw`). Independently,
    `SET_COST` has sat in `ENGINE_EMITTED_OPS` (`stsgen/steps.py:71-74`) since
    **`b291d8f`** (2026-07-24) — **deliberately unauthorable**, enforced by a
    generation-time assert that partitions every opcode into exactly one
    capability group. The obligation is therefore not merely unowned, it is
    **contrary to a standing design decision**: authoring `SET_COST` from YAML
    is something the generator is built to refuse. **Closed rather than
    re-owned.** If a future card genuinely needs an authorable cost rewrite, the
    change is to move the opcode between groups in `steps.py` — a deliberate
    authorability decision carrying its own assert — **not** this row.
  - **Four rows added for B3.26's inert relics** — Dead Branch, Gambling Chip,
    Sling of Courage, Orange Pellets — each carrying the blocker as the registry
    provenance records it, and each noting that `relic_rares_shop_test` asserts
    the inertness today, so implementing one fails a test rather than silently
    changing behaviour. Code-side asserted inertness is **not** a substitute for
    a row: a passing assertion tells you the relic is still inert, not that
    anyone intends to implement it, and this table's own header says an
    obligation that is not in it is invisible once the deferring block is
    archived. The dangling "same owner as Sling of Courage" reference on the
    Slaver's Collar row was the proof — it named an owner that did not exist.
    **Both** that row and the new Sling of Courage row now point at the shared
    **blocker** (no elite/boss room marker on `CombatState`) and cross-reference
    each other only as twins, so neither defines the other's owner.
- 2026-07-25 — **ledger/history reconciliation after integration-12, -13 and
  -14 (`master` at `8235477`).** Six tasks — **B3.26** (`860ab73`), **B3.16**
  (`574ded0`), **B3.21** (`8b237e8` + `741d90f`), **B3.8** (`603cac2`),
  **B3.22** (`7232d01`) and **B3.27** (`5a9a541`) — were on `master` while this
  ledger still showed them `[ ]`, which conventions §2 calls an incident. All
  six blocks are now archived verbatim in [stage-b-log.md](stage-b-log.md)
  (`#b326`, `#b316`, `#b321`, `#b38`, `#b322`, `#b327`) with their Logs filled,
  and appear here as one-line index entries. Two landed changes that are **not**
  ledger tasks — the combat-start end-of-round fix (`821bffd`, in `56248c5`) and
  the build/toolchain effort (in `8235477`) — get the new **Landed non-task
  work** section, because neither has a task block to archive and neither was
  recorded anywhere the next session would look.
  Master's suite is green on **six** presets: `debug` / `asan` / `release` (WSL,
  GCC 13.3) and `win-debug` / `win-asan` / `win-release` (clang-cl 22.1.8), with
  zero NOT_BUILT lines across 51 test binaries. Landed manifest, **regenerated**
  by `tools/registry_gen/gen.py` rather than summed: cards 91 / powers 40 /
  monsters 21 / relics 142 / potions 33 / events 0 / encounters 20 / a20 20 /
  **total 367**.
  Ten obligation rows were discharged and deleted (Odd Mushroom ×1.25; Calipers;
  Ice Cream / `EnergyManager.recharge`; RARE+SHOP `pool_order`; BOSS
  `pool_order`; translator `relicPools`; `healing: true` on Feed/Reaper;
  Barricade's block-decay branch; the recursive-play opcode for Double Tap; and
  the combat-start end-of-round row). Ten were added, one narrowed and three
  corrected. Three corrections have the **ledger as the losing document**
  (conventions §4):
  - **The un-park row is narrowed to B3.15 alone, not closed.** B3.16, B3.21 and
    B3.22 discharged their share **by construction** — the gate is
    `monster_init_fn(id) == nullptr`, so registering an init fn un-parks the
    group with no shared code site edited. B3.15's slavers / Looter / Fungi
    Beast are the last group still parking.
  - **The `act_boss` row's blocker is gone.** It said a boss registry was needed
    and that "B3.21/B3.22 land Act-1 bosses"; all three Act-1 bosses (B3.20,
    B3.21, B3.22) have landed. The row stays open because it still has no owner,
    but the stated blocker is now false and is corrected.
  - **The shared-namespace paragraph's "28-row power table" was stale**, and a
    bare row count there is the same liability conventions §8 documents for test
    counts. Replaced with the invariant it was trying to state (ids run ahead of
    the row count, re-derive the list from `registry/*.yaml`), plus the new
    permanent-gap record for `PowerId` 47 and 54–58.
- 2026-07-25 — **proposed, NOT applied: close the `gen.py` `SET_COST`
  step-authoring obligation.** *(Superseded: the orchestrator **accepted** this;
  the row is deleted and the closure recorded in the topmost entry above. Left
  here as written, because the proposal and its verification are the record of
  why the obligation stopped existing.)* Recorded here for the orchestrator to accept or
  reject; this pass corrected the row's *facts* but did not delete it, because
  an obligation needs explicit re-owning rather than silent closure. The row
  named two candidates and **both have now landed without needing it**: B3.8's
  Corruption rewrites cost natively in the power (`powers.yaml` CORRUPTION,
  `native: true`, hooks `on_use_card` / `on_card_draw`), and B3.27's Snecko Eye
  does the same through `PowerId::CONFUSION` (`native: true`, `on_card_draw`) —
  so the brief's "B3.27/Snecko Eye remains the candidate" had already become
  false by the time this pass ran, and was not written into the row.
  Independently, `tools/registry_gen/stsgen/steps.py` has classified `SET_COST`
  as **`ENGINE_EMITTED_OPS` — deliberately never authorable from YAML** since
  the codegen refactor `b291d8f` (2026-07-24), because its `src` operand is a
  runtime card-pool index no YAML author can name; a generation-time assert
  partitions every opcode into exactly one capability group, so the decision is
  enforced, not merely documented. If the orchestrator agrees that "authoring"
  is answered by "deliberately unauthorable", the row should be **deleted** with
  that citation. What must **not** happen is the row quietly surviving with a
  candidate that cannot arrive.
- 2026-07-25 — **proposed, NOT applied: split B4.5 into B4.5a / B4.5b.**
  Recorded here for the orchestrator to accept or reject; this ledger pass did
  not split or renumber anything, because a task's real scope turning out
  larger than its entry is a stop-and-surface trigger (conventions §5) and
  renumbering is not a scout's call. The seam is B3.8. **B4.5a** — gold rolls,
  potion drop + the `blizzardPotionMod` ratchet, the elite relic-tier roll,
  reward assembly order, the CHOOSE/skip flow, pity arithmetic, and routing
  acquisition through `add_card_to_master_deck` — is startable **today,
  without B3.8**: none of it draws a card. **B4.5b** — the `getCard(rarity)`
  draw, the three generated pool tables, the CardLibrary library-order pin,
  the no-dupe end-to-end test and the oracle spot-diff — is gated on B3.8 by
  the hard blocker recorded in B4.5's Deps note (an empty `rareCardPool`
  indexed on the first RARE roll). The value of the split is that B4.5a
  unblocks B4.8 (Shop) work that only needs the reward *plumbing*.
- 2026-07-25 — **B4.5 amended from a read-only scout; every citation verified
  against `D:\STS_BG_Mod\SlayTheSpireDecompiled` before it was written here.**
  Six changes, all to an open task's brief, none to a frozen document:
  1. **Deps** — B3.10 and B3.11 removed, B3.3-B3.11 becomes B3.3-B3.9. The
     combat card-reward pool is RED-only (`Ironclad.getCardPool` →
     `CardLibrary.addRedCards`, `CardLibrary.java:1157`), and the only caller
     of `getColorlessRewardCards()` is `RewardItem(CardColor)` →
     `SensoryStone.java:121`, an Act-3 event. This *agrees with* frozen design
     §5.1's "the 72 red non-basics are the reward pool; colorless enters via
     the shop's 2 colorless slots and Neow" — the ledger was the drifted
     document, not the design doc.
  2. **Deps** — B3.8 promoted from a coverage dep to a **hard mechanical
     blocker**: `cards.yaml` has zero RARE rows, so `rareCardPool` is empty
     and `getCard(RARE)` indexes an empty list the first time pity reaches 2.
  3. **Deliverables** — "colorless handling" re-scoped to "confirm colorless
     is unreachable from a combat reward, with the citation".
  4. **Deliverables** — `vs 3/37/60` reworded to thresholds **`< 3` / `< 40`**
     (widths 3/37/60), `AbstractRoom.java:158, 167`, confirmed at
     `AbstractDungeon.java:1606-1615`. Coding the widths as thresholds is
     wrong by 3 points on **every** reward. Design §5.6's "against 3/37/60" is
     the widths and is not contradicted.
  5. **Deliverables** — "upgrade chance 0 in Act 1" now records that the
     `randomBoolean` draw **still happens** (`Random.java:79-82`); only
     `c.rarity != RARE` short-circuits it (`AbstractDungeon.java:1470`). A
     stream-attribution trap, not a value change.
  6. **Acceptance** — "reward screens zero-diff through the differ" is now
     explicitly the **post-claim `RunState`** (gold/potions/deck/pity/
     counters): no new storage, no schema bump. Diffing the *offer* would need
     tools-side differ work or `RunState` growth, and an unplanned
     `SCHEMA_VERSION` bump is stop-the-line (conventions §5).
  Also **discharged by construction and deleted from the Deferred obligations
  table**: *"Card-pool removal bookkeeping storage, if the dupe loop needs
  it"* (deferred by B4.3). The dupe loop re-rolls without mutating, because
  `CardGroup.getRandomCard(boolean)` is a pure indexed read
  (`CardGroup.java:502-508`) — there is nothing to book-keep.
- 2026-07-25 — **ledger/history reconciliation after the three-branch union
  (`5f96ec4`).** B3.18 (`3ce0467`, merged `db9f6a7`), B4.15 (`d13d29e`,
  `372168d`) and B3.19 (`8396190`, `cd3e7fa`) were on `master` while this
  ledger still showed them `[ ]` — history and the ledger disagreeing, which
  conventions §2 calls an incident. All three blocks are now archived verbatim
  in [stage-b-log.md](stage-b-log.md) (`#b318`, `#b319`, `#b415`) with their
  Logs filled, and appear here as one-line index entries. The union built
  green on debug, asan **and** release. Landed manifest, regenerated by
  `tools/registry_gen/gen.py` rather than summed — the three branches' own
  claimed totals of 234 / 251 / 232 were mutually inconsistent: cards 75 /
  powers 28 / monsters 14 / relics 65 / potions 33 / events 0 / encounters 20
  / a20 20 / **total 255**.
  Three corrections ride along, each with the ledger as the losing document
  (conventions §4):
  - **B4.15's Deliverables line gave the wrong run-setup order.** It said
    "(A6 90 % HP, A10 curse, A11 slot, A14 −5 — exact order per
    `dungeonTransitionSetup`)". The real order is **A11 → (A5) → A14 → A6 →
    A10 → starting deck**: `AbstractPlayer.<init>` (`:211-213`) runs before
    the dungeon exists, and inside `dungeonTransitionSetup`
    (`AbstractDungeon.java:2562-2604`) the max-HP loss (`:2591-2593`)
    **precedes** the 90 %-of-max rewrite (`:2594-2596`), so an ascension-20
    Ironclad is **68/75, not 72/75** — confirmed against the committed G4
    oracle capture. Frozen design §6 is a table of modifiers with citations,
    not an application order, and its own line numbers agree; nothing frozen
    needed changing. The archived block keeps the original wording verbatim;
    the correction lives in its Log and here.
  - **The master-deck-door obligation row's "zero production callers" is now
    false.** B4.15 routes the A10 curse through `add_card_to_master_deck`, so
    that function has exactly one production caller (with a named test pinning
    that its `onObtainCard` pass is empty there). `remove_master_deck_card`
    and `dispatch_relics_on_obtain_card` still have none, and the obligation
    on B4.5 is unchanged.
  - **The un-park obligation row is narrowed, not closed.** B3.18 and B3.19
    discharged their share **by construction** — the gate is
    `monster_init_fn(id) == nullptr`, so registering an init fn un-parks the
    group with no shared code site edited. Remaining owners: B3.15, B3.16,
    B3.21, B3.22. The A6/A10/A14 row is deleted outright, discharged by B4.15.
  A new **Shared namespaces** subsection sits under the obligations table,
  recording that registry ids are append-only **with legal gaps that must
  never be renumbered** (`MonsterId` 14 is unallocated; `PowerId::ANGER` is 33
  against a 28-row table), and that `MonsterIntent` in
  `tools/registry_gen/stsgen/vocab.py` is a shared namespace needing
  orchestrator allocation before use — a conventions §7 rule-of-two
  observation, written because contention for it stopped one task dead and
  produced a silent off-limits edit (B3.19's `vocab.py` addition) in another.
- 2026-07-25 — **terminal-outcome coverage is now pinned by a test**,
  `FixtureOracle.CorpusCoversBothTerminalOutcomes`. The corpus must contain at
  least one fight ending in monster death and at least one ending in player
  death, and both are classified from the **replayed terminal `CombatState`** —
  the engine driven through each fixture's own recorded actions — never from a
  file name. Renaming a file cannot satisfy it; neutering a script cannot pass
  it.
  This is the actual lesson of the two fixture corrections below. The zero-diff
  oracle compares two implementations against each other and is silent whenever
  they agree, and two implementations agree very readily about an action that
  does nothing. The coverage claim therefore lived nowhere executable — in a
  file name and a markdown row — and went stale without anything turning red.
  A guarantee that nothing asserts is a guarantee that will drift.
  Two smaller corrections ride along, both found with `--dumpall` and neither
  changing any trace: fixt20's coverage description claimed the monster "gains
  Strength", which r18's Chomp/Thrash/Chomp sequence never does — it never
  Bellows, so it stays Vulnerable-only, and it is now described as the
  Vulnerable-only counterpart to the overlap fixtures. And `--dumpall` is now
  documented in the generator's USAGE block, since the derivation notes name it
  as their source of truth.
- 2026-07-25 — **an out-of-range hand slot is now a hard generation failure**
  in `tools/fixture_gen/gen_combat_fixtures.cpp`, the twin of the affordability
  abort added the day before and the mechanism behind the fixt16 correction
  recorded below. A `Play(n)` whose slot is outside the current hand aborts
  generation, naming the fixture, the action index, the slot and the hand size,
  and writes nothing.
  Worth stating why this needed a check at all, because it is *not* the fixt13
  case. An unaffordable play produced a state the game cannot reach, and the
  `advance()` legality guard eventually contradicted it. An out-of-range play
  produces a state the game reaches perfectly well — `queue_card_play` refuses
  the index, `advance()` no-ops the action, the reference simulator mirrors that
  no-op, and the trace is **correct**. The zero-diff check therefore cannot ever
  fire on one: both implementations agree, accurately, that nothing happened.
  What is wrong is not the trace but the fixture's *claim* — its name, and the
  coverage table's row — and no amount of differential testing looks at those.
  The only place the defect is visible is authoring time, which is where the
  check now lives.
  Safe to add because the corpus has exactly one violator and it was fixed
  first: generation reports **20/20** with the check in place, and every one of
  the 20 traces is byte-identical to before it. Demonstrated by reintroducing
  the old fixt16 script, observing the abort (exit 3, no trace written), and
  reverting.
- 2026-07-25 — **a second frozen Stage A golden vector was corrected** — again
  beyond the zero-diff-in-meaning regeneration conventions §5 sanctions, so it
  is recorded here rather than only in a task Log.
  `tests/golden/combat_fixtures/fixt16_r29_monster_death.trace` was named for
  monster death and **did not kill the monster**. Its script ended
  `… End(), Play(1), Play(1), Play(3)`; turn 2 deals a five-card hand, two plays
  leave three cards, and the final `Play(3)` therefore named an **empty slot**.
  `queue_card_play` (`src/engine/card_play.cpp`) rejects an index `>= hand_count`
  and `advance()` no-ops the action, the reference simulator mirrored that no-op,
  and the trace ended at `WAITING_ON_USER` with the monster alive on 17/43. The
  description was wrong in the same way — it claimed "then 3 Strikes" where the
  realized turn-2 plays were Defend then Strike.
  **Root cause — the same hand-index drift as fixt13, and the same blind spot.**
  The scripts were authored against a generator that did not yet discard the hand
  at end of turn, so every turn-2 slot silently came to name a different card.
  Nothing failed: an out-of-range play is a *documented no-op* on both sides, so
  the two independent implementations agreed that nothing happened and the
  zero-diff check passed. The corpus advertised monster-death coverage it did not
  have — a coverage claim that went stale in silence, which is the more general
  defect here than either individual script.
  **Fix.** fixt16 was re-authored empirically against
  `gen_combat_fixtures --dump`: turn 1 is unchanged (Bash 8 + Vulnerable, then a
  Vulnerable-boosted Strike 9, monster 43→26); turn 2 plays the hand it is
  actually dealt — Pommel 13, whose DRAW 1 pulls the second Strike into hand,
  then Strike 9 and Strike 9 — reaching `COMBAT_OVER` with the monster at 0/43
  inside the 3-energy budget. The killing blow stays a single-effect **Strike**
  so the pump halts with an empty queue. Only fixt16's trace changed; the other
  19 were proven byte-identical by sha256. Two follow-on commits on the same
  branch close the mechanism behind the defect rather than this one instance of
  it — see the two entries above.
  **Also corrected: the derivation notes.** The previous change left a scoped
  accuracy warning naming fixt01, fixt08 and fixt18 as having wrong printed
  arithmetic. Re-deriving every entry against `--dumpall` showed the warning
  itself understated the problem — **fixt12, fixt15, fixt19 and fixt20 were wrong
  too** — and that the coverage table had four independent errors (it credited
  fixt15 with a Pommel Strike it never plays, credited fixt20 with a
  Strength/Vulnerable overlap its monster never reaches, and missed both a Bash
  and three reshuffles). Every entry and every table row is now re-derived from
  the traces and the warning is gone. **No trace was wrong — only the prose
  about it**, which is why this is a notes fix and not a second vector
  correction.
- 2026-07-25 — **a frozen Stage A golden vector was corrected**, which goes
  beyond the zero-diff-in-meaning regeneration conventions §5 sanctions, so it
  is recorded here rather than only in a task Log.
  `tests/golden/combat_fixtures/fixt13_r21_triple.trace` asserted a state the
  game cannot reach. Its script played Bash (2) + Strike (1) + Pommel Strike (1)
  in a single turn — **4 energy against the Ironclad's 3**
  (`new EnergyManager(3)`, Ironclad.java:68) — and the trace duly recorded
  `player_energy == -1`. The game has no path to that value:
  `AbstractCard.hasEnoughEnergy` (AbstractCard.java:888) refuses the third card
  before it is ever used, and `EnergyPanel.useEnergy` clamps at zero
  (EnergyPanel.java:71-74).
  **Root cause — the cross-check was independent in the wrong dimension.**
  `tools/fixture_gen/gen_combat_fixtures.cpp` subtracted the card's cost with no
  affordability test, and the engine's `advance()` did not gate playability
  either, so the reference simulator and the engine agreed on an illegal state.
  The two implementations were genuinely independent in damage math, pile
  mechanics and RNG, and that is what the fixture corpus was built to check —
  but neither expressed the *playability* precondition, so no amount of
  differential testing could see it. The `advance()` legality guard on
  `advance-guard` added that precondition to one side and the disagreement
  surfaced immediately, as a `FixtureOracle` failure.
  **Fix.** The affordability rule is re-derived from the Java **in the
  generator** (not delegated to the engine's `legal_actions()`, which would
  re-couple the oracle to the code under test) and is a **hard generation-time
  abort** naming the fixture, action index, card, cost and available energy — an
  unplayable script now fails at authoring instead of becoming a golden vector.
  All 20 scripts were audited against it: 19 pass, fixt13 was the only failure.
  fixt13 was rescripted to Bash + Pommel on turn 1 and the third attack on turn
  2, preserving all three attacks, all three damage figures and the same final
  monster HP; only fixt13's trace changed and the other 19 were proven
  byte-identical.
  **Two related findings, surfaced and NOT acted on** (both are corpus
  decisions): (1) `fixt16_r29_monster_death` no longer kills the monster — its
  final action names hand slot 3 of a three-card hand, an out-of-range index
  both implementations no-op, so the corpus does not actually cover monster
  death despite the coverage table claiming it; (2)
  `tests/golden/combat_fixtures/derivation_notes.md` still described the
  skeleton as having no end-of-turn hand discard, which is false, so its
  multi-turn narration names cards that are no longer in hand. The false
  mechanic statement and a scoped accuracy warning were fixed in this change;
  re-deriving the affected per-fixture arithmetic (fixt01, fixt08, fixt18) and
  re-authoring fixt16 remain open.
- 2026-07-24 — **document restructure (no mechanic changed).** This ledger was
  2,576 lines; the 33 completed task blocks (146,303 B of Log text) moved
  byte-for-byte to [stage-b-log.md](stage-b-log.md) and are represented here by
  one-line index entries. Forward-looking obligations buried in those Logs were
  swept into the new **Deferred obligations** table and mirrored as
  `**Inherited:**` lines on the 19 open tasks that own them. The "Working
  agreements" section moved in full to [conventions.md](conventions.md), now
  the single authoritative copy of the rules previously duplicated across this
  ledger, `stage-b-orchestrator-prompt.md` and `CLAUDE.md`; where those three
  had drifted, the strictest reading won (each reconciliation is listed in
  `conventions.md`'s own change note). Byte-identity of every archived Log was
  proven mechanically before the commit. No open task's Deps / Deliverables /
  Acceptance text was altered.
- 2026-07-24 — B3.20 deliverable corrected after the execution agent's
  stop-the-line source check. The ledger said Slime Boss's Goop Spray inserts
  Slimed into the hand, but `SlimeBoss.takeTurn()` queues
  `MakeTempCardInDiscardAction` (3 copies below A19, 5 at A19+) and that action
  emits `ShowCardAndAddToDiscardEffect`. The ledger is the losing document;
  "hand insertion" is mechanically corrected to "discard-pile insertion."
- 2026-07-24 — **recorded retroactively** (the correction landed with B3.6 on
  2026-07-24 but no change-log entry was written at the time; added here during
  the Stage A archive pass). B3.6 deliverable corrected after the execution
  agent's stop-the-line source check. Grepping every `cards/red` constructor for
  `CardRarity.UNCOMMON` × `CardType.SKILL` yields **17** members — B3.6's
  deliverable list of 16 **plus Rage**, because `Rage.java:31` is
  `CardType.SKILL`, while this ledger's B3.7 deliverable list misfiled Rage
  under the uncommon POWER cards. The ledger is the losing document; Rage is
  mechanically moved to **B3.6** and struck from B3.7. B3.7's own source
  enumeration then found eight RED/UNCOMMON/POWER cards (Combust, Dark Embrace,
  Evolve, Feel No Pain, Fire Breathing, Inflame, Metallicize, Rupture) and
  skipped Rage, so both Logs agree.
- 2026-07-22 — B1.1 acceptance amended (ledger is the losing document; fixed
  in the same change per the working agreement). "Reproduces B0.2's captured
  run byte-for-byte" is unsatisfiable as written: the B0.2 capture is
  timing-contaminated (queued stepper `end`-resends produced a game history —
  5 monster hits by "turn 3" — that no clean state-paced replay can recreate;
  root cause in the B1.1 Log). Replaced with the controlled equivalent: two
  stock-jar runs of the derived effective script must be byte-identical
  (determinism control), the fork run must be byte-identical to stock on the
  same script, and the run must anchor to the B0.2 capture (Neow floor-0
  state, pre-contamination 18-state prefix, floor/screen trajectory). The
  B1.3 strip-equivalence and G4 acceptance style (fork-vs-fork A/B on
  identical scripts) is unaffected.
- 2026-07-17 — v0.1 created from stage-b-design.md v0.1 (§2-§8 → phases
  B0-B5). Task counts: 57 tasks + 4 gates (G4-G7, continuing Stage A's gate
  numbering; G4=M2, G6=M3, G7=M4). Card/monster/relic batch enumerations
  marked "enumerate at task start" are counted from the CardRarity/RelicTier
  greps recorded in design §5 — the named example lists in B3.x are
  orientation, not authority; the source enumeration at task start wins.
