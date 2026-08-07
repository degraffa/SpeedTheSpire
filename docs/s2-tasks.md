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
| Gremlin move-99 escape (`EscapeAction` body + `deathReact`/`escapeNext` trigger) | B3.16 (stage-b table: "UNASSIGNED — Act-2 owner") | S2.23 | Reachable in Act 2 via Gremlin Leader minions; land both halves together and mark the stage-b row DISCHARGED in the same commit |
| `JawWorm(..., true)` constructor variant semantics | TE.2 scope pass | S2.26 | Jaw Worm Horde constructs the variant (MonsterHelper.java:549-551); UNVERIFIED — needs decompile check what the boolean changes (stats? starting Strength?) before the row's tier columns are trusted |
| Rest-site Recall option surface at Acts 2–3 (`isFinalActAvailable`, ruby key) | TE.2 scope pass (s2-design §4.5) | S2.13 | UNVERIFIED — needs decompile check of CampfireUI/rest-option construction for a key-gated option; if present it is on-screen at every Act-2/3 rest and must be modeled as a visible row (grant stubbed to S3) or the oracle diverges on the menu |
| Boss chest + sapphire-key row interaction | TE.2 scope pass (s2-design §4.5) | S2.11 | **DISCHARGED — NO** (2026-08-07, S2.11). `BossChest.open(boolean)` (BossChest.java:49-63) FULLY OVERRIDES `AbstractChest.open` with **no `super` call**, so the `isFinalActAvailable && !hasSapphireKey` append at AbstractChest.java:95-97 is unreachable from the boss chest — and so are `randomizeReward`'s treasureRng roll, gold, the curse, `addRelicToRewards`, `onChestOpenAfter` and `combatRewardScreen.open`. Pinned by `BossChest.NeverAppendsTheSapphireKeyRow` and `BossChest.FiresNoRelicChestHooks` |
| `Lab` in ProceedButton.java:115's combat-event list with no encounter | TE.2 scope pass (s2-design §2.3) | S2.33 | UNVERIFIED — needs decompile check why it is listed; suspected reward-screen plumbing only |
| Exact Act-2/3 entry floors (17/34 assumption) | TE.2 scope pass (s2-design §4.2) | S2.12 | **DISCHARGED** (2026-08-07, S2.12). The answer is a PAIR per act, and conflating its halves was the whole risk: **17/34 are the CONSTRUCTION floors** — what `dungeonTransitionSetup`, the constructor chain, `generateMap`, `setEmeraldElite` and the BGM draw observe, and what the un-reseeded floor-scoped five still carry (`seed+17` / `seed+34`) — while **18/35 are the first PLAYABLE rooms**. Span = 17 = 15 map rows + boss + boss chest; the crossing itself adds **no** floor, because `isDungeonBeaten = true` (ProceedButton.java:249-250) is exactly what makes `updateFading` skip `nextRoomTransition` (:2317-2326). Table in s2-design §4.2; engine constants `kActFloorSpan` / `act_floor_base`, with `run_cur_row = floor − base − 1` replacing the Act-1-only `floor − 1` |
| `generateStrongEnemies(12)` regeneration on an exhausted `monsterList` | S2.11 (the boss-exit pop it added) | S2.12 | **DISCHARGED — UNREACHABLE, no body written** (2026-08-07, S2.12). Re-derived for Acts 2–3, which call `generateWeakEnemies(2)`: SUPPLY is weak + 1 first-strong + 12 strong = **15** (Act 1 = 16); DEMAND is at most **14** — one walked path visits 15 rooms, one per map row, of which the act-independent generator forces row 8 Treasure and row 14 Rest, leaving 13 `monsterList`-consuming rooms (a ? room that rolls MONSTER is one of those 13, not an extra) plus the one pop that leaving the boss room performs. Margin 2 in Act 1, **1** in Acts 2–3. The loud `assert` in `next_room_transition_impl` stays and now carries that arithmetic in full; writing untestable machinery for an unreachable arm would be worse than an assert that names why it cannot fire |
| Fork redeploy + bottle-taking capture (stage-b table row, "next capture-campaign owner") | wave-runlayer S3 (stage-b) | S2.43 | S2.43 is the next capture campaign; validate the `in_bottle_*` boundary end-to-end and mark the stage-b row DISCHARGED there |

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
  the call site and pinned by a test that moves with S2.13.
- **S2.13** `[ ]` **?-rooms, one-time pool, and rest sites across acts.**
  Per-act event/shrine list rebuild + the one-time pool's cross-act
  depletion semantics; the act-gated one-time draw filters
  (design §2.3); EventHelper pity reset wiring; the Recall-option probe
  (deferred row) resolved and either modeled or pinned absent.
  **Deps:** S2.02, S2.12 **Acceptance:** draw-gate tests per gated row
  (Designer/Duplicator/FaceTrader/Knowing Skull/N'loth/Joust/
  SecretPortal-pinned-false per trap 5); cross-act depletion test (Act-1
  draw removes for Act 2); six presets green.

## Phase S2.2 — Monster batches (each = YAML rows + engine bodies + tier-2, the B3.13–B3.22 pattern; ∥ across disjoint batches once S2.01 lands)

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
- **S2.23** `[ ]` ∥ City elites — Gremlin Leader (minion mechanics +
  spawnGremlin), Slavers (Taskmaster + S1 slavers), Book of Stabbing.
  **Inherited:** the stage-b Gremlin move-99 escape row (see Deferred
  obligations).
  **Deps:** S2.01 **Acceptance:** as S2.21, plus escape-trigger tests and
  the stage-b row discharged in the same commit.
- **S2.24** `[ ]` ∥ City bosses — Bronze Automaton (+ BronzeOrb, Stasis
  model), The Champ, The Collector (+ TorchHead). A2/3/4-A19 columns per
  boss.
  **Deps:** S2.01 **Acceptance:** as S2.21, plus boss-flag typing
  (Pantograph-style consumers) and A13 gold tests.
- **S2.25** `[ ]` ∥ Beyond normals I — Darkling (Regrow/revival), Orb
  Walker, Repulsor/Exploder/Spiker (3/4 Shapes, Sphere and 2 Shapes).
  **Deps:** S2.01 **Acceptance:** as S2.21.
- **S2.26** `[ ]` ∥ Beyond normals II — Spire Growth, Transient, Maw, Jaw
  Worm Horde (variant-ctor deferred row), Writhing Mass (Reactive +
  master-deck Parasite).
  **Deps:** S2.01 **Acceptance:** as S2.21, plus the master-deck Parasite
  fold-back test.
- **S2.27** `[ ]` ∥ Beyond elites — Giant Head, Nemesis (Intangible +
  Burn), Reptomancer (+ SnakeDagger spawns).
  **Deps:** S2.01 **Acceptance:** as S2.21.
- **S2.28** `[ ]` ∥ Beyond bosses — Awakened One (two phases, Curiosity/
  Unawakened, Void insertion, Cultist adds), Time Eater (TimeWarp/
  DrawReduction/Slimed), Donu and Deca.
  **Deps:** S2.01 **Acceptance:** as S2.21, plus phase-transition and
  TimeWarp turn-economy tests.

## Phase S2.3 — Events closure (B4.11–B4.13 pattern; ∥ across disjoint batches once S2.02 + S2.13 land)

- **S2.31** `[ ]` ∥ City events I (non-combat): Addict, Back to Basics,
  Beggar, Cursed Tome, Drug Dealer, Forgotten Altar, Ghosts, Nest.
  **Deps:** S2.02, S2.13, S2.03 (payout rows) **Acceptance:** per-event
  option/gate/A15 audit against the source read in full; payout rows
  (relics/cards/curses) acquisition-tested; six presets green.
- **S2.32** `[ ]` ∥ City events II: The Library, The Mausoleum, Vampires,
  Colosseum + Masked Bandits (combat embeds), Knowing Skull, The Joust,
  N'loth, Designer, Duplicator (act-gated one-timer bodies).
  **Deps:** S2.02, S2.13, S2.01 (event encounter groups) **Acceptance:**
  as S2.31, plus combat-embed flow tests (two-fight Colosseum sequence).
- **S2.33** `[ ]` ∥ Beyond events: Falling, Mind Bloom (boss re-fight +
  miscRng shuffle — trap 6), The Moai Head, Mysterious Sphere, Sensory
  Stone, Tomb of Lord Red Mask, Winding Halls; SecretPortal pinned per
  trap 5; the `Lab` listing resolved (deferred row).
  **Deps:** S2.02, S2.13, S2.01 **Acceptance:** as S2.31, plus Mind
  Bloom's Act-1-boss re-fight replays zero-diff in a directed capture.

### S2-G1 `[ ]` **Gate: S2 rules complete** — tag `s2-g1-content`
**Deps:** all S2.0x, S2.1x, S2.2x, S2.3x
Checked literally per design §6 S2-G1: registry closure vs the §2
inventory; 100 % tier-2 per the manifest; every §5 trap named-tested;
a20 rows IMPLEMENTED; ≥ 10M-action three-act fuzz soak clean; six presets
green; Stage-A fixtures byte-identical. Then: update CLAUDE.md "Current
state".
**Log:** —

## Phase S2.4 — Verification campaigns + S2 exit

- **S2.41** `[ ]` ∥ **Three-act fuzz soak extension.** B5.1 machinery over
  Acts 1–3: new MoveCats claimed for the boss-relic phase, coverage
  report extended per act; the S2-G1 soak is this task's tooling run at
  gate time.
  **Deps:** S2.11, S2.12 (runs incrementally as content lands)
  **Acceptance:** soak sweep with zero nondeterminism/asserts at
  S2-G1-scale volume; shard/resume paths proven.
- **S2.42** `[ ]` ∥ **Deep-reach scripted drivers + sim pre-scan.** The
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
- **S2.43** `[ ]` **Oracle campaigns, breadth + depth.** The §6 S2-G2
  evidence: ≥ 2,000 distinct mixed-policy A20 attempts; Act-2 boss-reward
  + boss-relic-pick cohort; Act-3 kill + double-boss cohort
  (simulator-selected seeds sanctioned); event-depth coverage join; all
  triage per the Stage B process, zero untriaged/open.
  **Inherited:** the stage-b "fork redeploy + bottle-taking capture" row
  (see Deferred obligations).
  **Deps:** S2-G1, S2.42 **Acceptance:** deterministic dashboard
  reopening every artifact; per-bar numbers meeting design §6 S2-G2
  items 1–4; dispositions exact, no wildcards.
- **S2.44** `[ ]` ∥ **Tier-4 additions.** Pre-registered hypotheses per
  design §6 item 6 (act pools + exclusion effects, per-act upgrade
  chance, boss shuffle + double-boss conditioning, one-time-pool
  depletion, canSpawn-gate pool-cursor effects), Holm-corrected family.
  **Deps:** S2-G1 **Acceptance:** suite green at B5.3 scale with α
  discipline unchanged; negative-control mutant rejected.
- **S2.45** `[ ]` ∥ **Throughput re-baseline.** B5.5 methodology over
  three-act runs: per-step and per-combat floors must hold unchanged; new
  whole-machine three-act run rate recorded with methodology as the S3
  baseline (expected lower per run — not a regression; design §6 item 7).
  **Deps:** S2-G1 **Acceptance:** release-preset numbers recorded;
  per-step/per-combat floors green.
- **S2.46** `[ ]` **Verification report + CI corpus + proactive audit.**
  B5.4 pattern: aggregated report with literal S2-G2 shortfalls; curated
  compressed corpus extended with three-act traces incl. one double-boss
  run; `g7_proactive_manifest` extended with S2-discovered families and
  the executable audit re-run.
  **Deps:** S2.43, S2.44 **Acceptance:** report committed under
  `docs/verification/`; CI replay of the extended corpus green in every
  preset; audit green.

### S2-G2 `[ ]` **Gate: S2 verified (unblocks training Phase T4)** — tag `s2-g2-verified`
**Deps:** S2.41–S2.46, S2-G1
The design §6 S2-G2 bar, checked literally, every item with linked
evidence. Then: update CLAUDE.md "Current state"; notify the training
ledger (T4.1's `Deps: S2` is this tag); S3 planning opens as its own fresh
exercise (not claimed here).
**Log:** —

## Parallelism map

```
Wave 1 (now):  S2.01 ∥ S2.02 ∥ S2.03 ∥ S2.04
S2.01 ─▶ S2.12 ─▶ S2.13 ; S2.11 (∥ with S2.12)
S2.01 ─▶ S2.21 … S2.28 (batches ∥)
S2.02 + S2.13 ─▶ S2.31 ∥ S2.32 ∥ S2.33
all S2.0x/1x/2x/3x ─▶ S2-G1
S2.12 ─▶ S2.42 ; S2.11+S2.12 ─▶ S2.41
S2-G1 ─▶ S2.43 (needs S2.42) ∥ S2.44 ∥ S2.45 ; S2.43+S2.44 ─▶ S2.46
S2.41–S2.46 ─▶ S2-G2
```

## Change log

- 2026-08-03 — ledger created by TE.2 with Phases S2.0–S2.4, gates
  S2-G1/S2-G2, Wave-1 id blocks, and the inherited-obligation rows;
  scope denominator is [s2-design.md](s2-design.md) v0.1.0.
