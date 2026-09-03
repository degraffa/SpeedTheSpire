# S3 Scope + Verification Design — keys, Act 4, the Corrupt Heart (v0.1.0)

**Status:** authored as the S3 planning exercise opened by the S2-G2 gate Log
([s2-tasks.md](s2-tasks.md), "S3 planning opens as its own fresh exercise").
It becomes the binding scope document for the S3 ledger in
[s3-tasks.md](s3-tasks.md) when this change lands.
[stage-a-design.md](stage-a-design.md) and
[stage-b-design.md](stage-b-design.md) remain **frozen** and in force for
everything they cover; [s2-design.md](s2-design.md) likewise stands for Acts
2–3. This document *extends* them to Act 4 and the keys and never overrides
them — where it corrects a **scoping** statement one of them made about S3
content, that is recorded in §9 and in the losing document's own change log
(conventions §4). [conventions.md](conventions.md) is binding on every task
under it. Amendable only via the change log below.

InitialPlan A.5 names the tier: **S3 = keys + Act 4 + the Heart**
([../InitialPlan.md](../InitialPlan.md)); the training program's **T5 phase —
the headline A20 Heart-kill objective — is blocked on "engine S3 verified"**
per [training-plan.md](training-plan.md) §4.3, and §7 names engine content
(G7 → S2 → S3) as the whole program's critical path.

Content facts below were extracted from the decompiled tree at
`D:\STS_BG_Mod\SlayTheSpireDecompiled` for this document (every claim cites
`File.java:line`); the stage-a §1 rule still applies at task time — **numbers
enter the registry only after the implementing task re-reads the cited lines
in full**. This document is the ledger's denominator, not a substitute for
provenance. Anything not verifiable from in-repo sources during this exercise
is marked `UNVERIFIED — needs decompile check` and is owned by a named row in
[s3-tasks.md](s3-tasks.md).

---

## 1. S3 scope boundary

- **In:** everything S1 and S2 froze, plus —
  1. **The three keys as obtainable content.** The emerald key's reward row on
     the burning elite, the sapphire key's linked reward row on ordinary
     chests with its real claim semantics, and the consequences of *holding* a
     key (§3). The ruby key's grant is already live (the campfire Recall arm,
     s2-design §4.5); what is new is what a key is *for*.
  2. **The Act-3 terminal room.** `goToVictoryRoomOrTheDoor`
     (ProceedButton.java:199-208) → `VictoryRoom(EventType.HEART)` →
     the `Spire Heart` event (SpireHeart.java), its four-click dialog, its
     key gate (SpireHeart.java:151), the **Act-3-stop** terminal
     (SpireHeart.java:170-177) and the **Door** (SpireHeart.java:94-98 →
     DoorUnlockScreen.java:143-161).
  3. **Act 4 (TheEnding)** in full: the hand-built special map
     (TheEnding.java:72-139), its four playable rooms (rest / shop / elite /
     boss), its constants (TheEnding.java:145-160), its fixed monster and
     boss lists (TheEnding.java:162-196), and the `TrueVictoryRoom` terminal
     (TrueVictoryRoom.java, ProceedButton.java:189-197).
  4. **The Act-4 content**: the `Shield and Spear` elite (SpireShield.java,
     SpireSpear.java) and `The Heart` boss (CorruptHeart.java) with the four
     Act-4-only powers (§2.3).
  5. **Every A20 modifier with an Act-4 effect** — the per-monster A3/A4/A8/A9/
     A18/A19 branches of the three new classes (§4.6) — and the A20 negatives
     Act 4 creates (§5 trap 8).
  6. **The true-victory terminal**, i.e. a three-valued run outcome
     (death / Act-3 stop / Heart kill) replacing today's boolean
     `run_is_victory` (§4.5).
- **Run termination (new, frozen by this document):** an S3 run has **three**
  terminals, not two. (a) Death, anywhere, unchanged. (b) The **Act-3 stop** —
  the `Spire Heart` dialog's DEATH arm, taken when any key is missing
  (SpireHeart.java:151,160-177): the game builds `new DeathScreen(null)` and
  `GameOverScreen.isVictory` is nevertheless TRUE because the current room is
  a `VictoryRoom` (DeathScreen.java:99), and the metrics row is
  `victory=true, trueVictor=false` (DeathScreen.java:291-299 →
  Metrics.java:107). (c) The **Heart kill** — the Act-4 boss room's proceed
  routes to `TrueVictoryRoom` (ProceedButton.java:107-109, :189-197), whose
  cutscene opens `VictoryScreen` (Cutscene.java:122-125), metrics
  `victory=true, trueVictor=true` (VictoryScreen.java:254-269). **S2's
  terminal is (b) with the whole dialog collapsed**; S3 makes the collapse
  explicit and adds (c).
- **Out (S4+):** save-file loading (`AbstractDungeon` load constructors,
  `SaveFile`, `CardCrawlGame.java:846-849` key restore); other characters —
  which includes SpireShield's `FocusPower` branch (§2.3, orb-gated and
  therefore Ironclad-unreachable) and the character-specific `Cutscene`;
  Endless mode (`Settings.isEndless` stays pinned false — it is the *only*
  thing that would give the Act-3 and Act-4 bosses a reward screen,
  AbstractRoom.java:327); daily runs; `Settings.isDemo`'s
  `goToDemoVictoryRoom` (ProceedButton.java:222-229, unreachable);
  the `DoorUnlockScreen`'s **meta** arm (`eventVersion == false`,
  DoorUnlockScreen.java:147-151 — a main-menu profile screen, not a run-flow
  room); every cosmetic Act-4 branch enumerated in §5 trap 10.
- **Environment assumption (unchanged):** fully-unlocked profile, so
  `Settings.isFinalActAvailable` is TRUE for the whole run
  (Settings.java:642, set at run start by `setFinalActAvailability`,
  CardCrawlGame.java:661) and the engine keeps pinning it
  (`kFinalActAvailable`, `rest_sites.hpp`). **S3 is the stage where that pin
  stops being decorative**: it is the outer conjunct of all four key gates
  (CampfireUI.java:94, AbstractChest.java:95, MonsterRoomElite.java:95,
  AbstractDungeon.java:543) and of the Door itself (SpireHeart.java:151).

## 2. Content inventory — the S3 ledger's denominator

All facts read from the decompiled tree during this exercise. Registry ids are
**append-only**; current maxima are re-derived from `registry/*.yaml` at
allocation time, never from this document (conventions §5; the stage-b
ledger's shared-namespaces section governs allocation). The headline is how
*small* the content is and how *large* the run layer is: **10 new registry
rows in four domains, and nothing else** — S3 is a run-layer and verification
stage wearing a content stage's name.

### 2.1 Encounters — 2 new registry rows

TheEnding's `generateMonsters` (TheEnding.java:162-172) is **not a pool**. It
adds the literal string `"Shield and Spear"` three times to `monsterList`
(:164-167) and three times to `eliteMonsterList` (:168-171); every
`generateWeakEnemies` / `generateStrongEnemies` / `generateElites` /
`generateExclusions` override is an **empty body** (:174-189). `initializeBoss`
adds `"The Heart"` three times with **no shuffle** (:191-196). Consequently
**Act-4 construction consumes ZERO `monsterRng`** — the single most
bit-exactness-relevant fact in this section (§5 trap 3).

- `Shield and Spear` — `MonsterGroup(new SpireShield(), new SpireSpear())`,
  Shield at index 0 (MonsterHelper.java:599-601). Registry pool `ELITE`, act 4.
- `The Heart` — `MonsterGroup(new CorruptHeart())` (MonsterHelper.java:596-598).
  Registry pool `BOSS`, act 4.

`getMonsterForRoomCreation` / `getEliteMonsterForRoomCreation`
(AbstractDungeon.java:1846-1862) read `list.get(0)` and **never remove**, so
the lists never deplete and the empty refill overrides are never entered. The
Act-4 map contains no `MonsterRoom` at all (§4.4), so `monsterList`'s three
entries are dead storage; the ledger authors the two rows the *reachable*
consumers need and does not author a normal-pool duplicate (the S1 precedent
for unreachable keys — "4 Byrds", "Snecko and Mystics", encounters.yaml).

### 2.2 Monsters — 3 new classes

`monsters/ending/` holds exactly three classes.

| Class | `game_id` | Type | HP | Provenance |
|---|---|---|---|---|
| SpireShield | `"SpireShield"` | ELITE | 110, **125 at A8+** | SpireShield.java:36,49-59 |
| SpireSpear | `"SpireSpear"` | ELITE | 160, **180 at A8+** | SpireSpear.java:37,50-61 |
| CorruptHeart | `"CorruptHeart"` | BOSS | 750, **800 at A9+** | CorruptHeart.java:49,66-76 |

All three call `setHp(int)` with a **fixed** value — no `min,max` overload —
so **Act 4 consumes no `monsterHpRng` at all** (§5 trap 4). None of the three
has a `rolls:` column, and `MONSTER_ROLL_TIMINGS` needs no new value.

Per-move detail the batch tasks re-read in full:

- **SpireShield** (SpireShield.java:42-46,79-137). Moves `BASH=1`,
  `FORTIFY=2`, `SMASH=3`; damage `12/34`, **`14/38` at A3+** (:60-66);
  `FORTIFY_BLOCK = 30`. `usePreBattleAction` (:69-77) applies
  **Surrounded to the PLAYER** (:71 — the Shield is the only source) and
  `ArtifactPower(this, 1)`, **2 at A18+** (:72-76). BASH (:82-92) is
  ChangeState → Wait → `DamageAction(damage[0])` → then a branch (:86-91)
  whose `aiRng.randomBoolean()` is **short-circuited behind
  `!player.orbs.isEmpty()`** and therefore *never consumed* by an Ironclad,
  so the arm is always `StrengthPower(player, -1)` (:90). FORTIFY (:93-98)
  is `GainBlockAction(m, this, 30)` for **every** monster in the group,
  itself included. SMASH (:99-108) is `DamageAction(damage[1])` then
  `GainBlockAction(this, this, 99)` at A18+, else block equal to
  `damage.get(1).output` — the **post-power** output, not the base (:107).
  `getMove` (:113-137) ignores `num`, cycles `moveCount % 3`: case 0 is a
  coin-flip `aiRng.randomBoolean()` between FORTIFY and BASH (:116-123);
  case 1 is `!lastMove(1) ? BASH : FORTIFY` (:124-131); case 2 is SMASH,
  unconditional (:132-134); `++moveCount` at :136.
- **SpireSpear** (SpireSpear.java:43-47,82-140). Moves `BURN_STRIKE=1`,
  `PIERCER=2`, `SKEWER=3`; damage `5/10` with `skewerCount = 3`,
  **`6/10` with `skewerCount = 4` at A3+** (:62-70). `usePreBattleAction`
  (:73-80) is Artifact only — **no Surrounded**. BURN_STRIKE (:85-97) is two
  `DamageAction(damage[0], FIRE)` hits then **2 Burns**: at A18+
  `MakeTempCardInDrawPileAction(new Burn(), 2, false, true)` — the draw pile
  (:92) — else `MakeTempCardInDiscardAction(new Burn(), 2)` (:95).
  PIERCER (:98-103) gives **+2 Strength to every monster**, itself included.
  SKEWER (:104-111) is `skewerCount` × `DamageAction(damage[1])`.
  `getMove` (:116-140) ignores `num`: case 0 `!lastMove(1) ? BURN_STRIKE(×2) :
  PIERCER`; case 1 unconditional SKEWER; case 2 a coin-flip
  `aiRng.randomBoolean()` between PIERCER and BURN_STRIKE; `++moveCount`.
- **Both guards' `die()`** (SpireShield.java:164-176, SpireSpear.java:171-183)
  are byte-identical: for each still-living monster, **remove `Surrounded`
  from the player** and **remove `BackAttack` from the survivor**, flipping
  the player's facing toward it. Whichever guard dies first therefore ends the
  back-attack mechanic for the rest of the fight.
- **CorruptHeart** (CorruptHeart.java:55-63,105-200). Moves `BLOOD_SHOTS=1`,
  `ECHO_ATTACK=2`, `DEBILITATE=3`, `GAIN_ONE_STRENGTH=4`; damage `40/2` with
  `bloodHitCount = 12`, **`45/2` with `bloodHitCount = 15` at A4+** (:77-85).
  `usePreBattleAction` (:88-103) applies **`InvinciblePower(300)`, 200 at
  A19+** (:93-96,:101) and **`BeatOfDeathPower(1)`, 2 at A19+** (:97-100,:102)
  — **no Artifact at battle start**. DEBILITATE (:108-119) is
  Vulnerable/Weak/Frail 2 each (`isSourceMonster = true`) then **five status
  cards shuffled to RANDOM draw-pile positions** — Dazed, Slimed, Wound, Burn,
  Void, in that order (:113-117; the 7-arg
  `MakeTempCardInDrawPileAction(card, 1, true, false, false, x, y)`, i.e.
  `randomSpot = true`). The buff move (:120-151) first **negates any negative
  Strength** (`additionalAmount = -strength` when strength < 0, :121-124),
  applies `StrengthPower(additionalAmount + 2)` (:127), then a `buffCount`
  ladder — 0 → `ArtifactPower(2)`; 1 → `BeatOfDeathPower(1)`; 2 →
  `PainfulStabsPower`; 3 → `StrengthPower(10)`; **≥ 4 → `StrengthPower(50)`,
  forever** (:128-148) — and `++buffCount` (:149). BLOOD_SHOTS (:152-162) is
  `bloodHitCount` × `DamageAction(damage[1])`; ECHO_ATTACK (:163-166) is one
  `DamageAction(damage[0])`. `getMove` (:171-200): **the first roll always
  returns DEBILITATE and returns EARLY, so `moveCount` is not incremented on
  it** (:173-177) — then `moveCount % 3`: case 0 a coin-flip between
  BLOOD_SHOTS and ECHO_ATTACK (:179-186); case 1 `!lastMove(2) ? ECHO :
  BLOOD_SHOTS` (:187-194); case 2 unconditional buff (:195-197).
- Every move roll still spends one `aiRng.random(99)` through
  `AbstractMonster.rollMove` (AbstractMonster.java:465-467) even though all
  three classes ignore `num` — the S1/S2 convention, restated because all
  three are `num`-ignoring and it is the easy draw to lose.

**No new `MonsterIntent` value is needed.** The six the three classes
telegraph — ATTACK (1), ATTACK_DEFEND (3), BUFF (4), ATTACK_DEBUFF (6),
STRONG_DEBUFF (8), DEFEND (11) — are all registered (vocab.py
`MONSTER_INTENTS`).

### 2.3 Powers — 4 new registry rows

| Power | `game_id` | Owner | Semantics |
|---|---|---|---|
| Surrounded | `"Surrounded"` | the **player** | Pure flag, `amount = -1`, no hooks (SurroundedPower.java:13-23). Read by `AbstractMonster.applyBackAttack` (:1015-1017). |
| Back Attack | `"BackAttack"` | a **monster** | Pure marker, `amount = -1`, no hooks (BackAttackPower.java:17-33). Auto-applied by `AbstractMonster.applyPowers` (:998-1002), removed by `removeSurroundedPower` (:1019-1023). |
| Beat of Death | `"BeatOfDeath"` | the Heart | `onAfterUseCard` → one `DamageAction(player, amount, THORNS)` **per card played** (BeatOfDeathPower.java:40-44). Binds the existing `on_after_use_card` hook (16). |
| Invincible | `"Invincible"` | the Heart | `onAttackedToChangeDamage` caps the hit at the remaining pool and drains it; `atStartOfTurn` restores `amount = maxAmt` (InvinciblePower.java:31-48). `priority = 99` (:28). |

`PainfulStabsPower` is already registered (id 97, S2.23); the Heart is a
second producer. `ArtifactPower`, `StrengthPower`, `VulnerablePower`,
`WeakPower`, `FrailPower` are S1/S2 rows. `FocusPower` is **not** registered
and stays out: its only Act-4 site is SpireShield.java:87, behind
`!player.orbs.isEmpty()`, which no Ironclad can satisfy — S4, with the Defect.

**The 1.5× back-attack multiplier is not in either power.** It is hard-coded
twice in `AbstractMonster`: in `calculateDamage` for the intent number
(:982-984) and in `applyPowers` for the real hit (:998-1013), both gated on
`applyBackAttack()`. The player's facing flips when a target is chosen
(AbstractPlayer.java:1291-1293) and is re-evaluated on every hand layout
(CardGroup.refreshHandLayout, CardGroup.java:204-223).
`UNVERIFIED — needs decompile check`: whether the *engine* must model facing
at all, or whether the S3 collapse "the monster the player is not facing takes
1.5×, and there are exactly two of them" is exact — resolve at the S3.42 task
by reading `refreshHandLayout`, `AbstractPlayer.java:1285-1300` and
`AbstractMonster.java:998-1023` in full, and pin the answer either way.

### 2.4 Events — 1 new registry row

`Spire Heart` (`SpireHeart.java:46`, `ID = "Spire Heart"`) is an
`AbstractEvent` that appears in **no** `eventList` and **no** `shrineList`: it
is constructed directly by `VictoryRoom.onPlayerEntry` (VictoryRoom.java:30-33).
It needs a registry row anyway, for three reasons that are all already
observed facts, not predictions: the translator **aborts** on the id today
(`translate.cpp` unknown-content-id path; one S2 CI-corpus entry carries no
trace for exactly that reason, [verification/s2-verification.md](verification/s2-verification.md)
§8 and §9 limit 1), the replay differ **skips** its records as a named
post-victory exception (`main.cpp`, "the Spire Heart cinematic — out of S2
scope"), and the engine needs an `EventId` to carry the dialog through
`RunPhase::EVENT_DIALOG`. The row must be marked as **not a member of any act's
draw list** — a new registry property, not a new pool.

Act 4 adds **no other events**: `TheEnding.initializeEventList` (:198-200) and
`initializeShrineList` (:211-213) have **empty bodies**, `dungeonTransitionSetup`
has already cleared both (AbstractDungeon.java:2576-2577), and there is no
`?` room on the Act-4 map, so `getEvent`/`getShrine` are unreachable in Act 4.
`specialOneTimeEventList` is still carried by reference across the crossing and
is simply never drawn from again.

### 2.5 Cards, relics, potions — no new rows

- **Cards — 0 new rows.** Every card any Act-4 actor produces is registered:
  `Burn` (SpireSpear.java:92,95; CorruptHeart.java:116), `Dazed` (:113),
  `Slimed` (:114), `Wound` (:115 and PainfulStabsPower.java:42), `Void`
  (:117 — the Awakened One is the existing producer, s2-design §2.4). **No
  curse is added by any Act-4 actor.**
- **Relics — 0 new rows.** Act 4's relic surfaces are the elite's
  `dropReward` (MonsterRoomElite.java:80-92) and the shop, both drawing from
  the run pools built once in Act 1. What *changes* is reachability: the
  floor-gated `canSpawn` family (`floorNum < 48/40/52`, s2-design §5 trap 9)
  is now rejecting almost everywhere, because Act-4 floors are 51+ (§4.3).
- **Potions — 0 new rows.** `initializePotions()` re-runs in the Act-4
  constructor chain (AbstractDungeon.java:298) and `blizzardPotionMod` resets
  to 0 at the crossing (:2581), exactly as at the other two crossings.
- **`a20.yaml` — 0 new rows**, six rows' notes refreshed (§4.6).

### 2.6 Act-4 per-act constants (TheEnding.java:145-160)

| Constant | Act 1 | Act 2 | Act 3 | **Act 4** | Provenance |
|---|---|---|---|---|---|
| `shop/rest/treasure/event/eliteRoomChance` | .05/.12/0/.22/.08 | same | same | **same, and DEAD** | TheEnding.java:147-151 |
| `small/medium/largeChestChance` | 50/33/17 | same | same | **0/100/0, DEAD** | TheEnding.java:152-154 |
| `common/uncommon/rareRelicChance` | 50/33/17 | same | same | **0/100/0, DEAD** | TheEnding.java:155-157 |
| `colorlessRareChance` | 0.3 | 0.3 | 0.3 | **0.3** | TheEnding.java:158 |
| `cardUpgradedChance` | 0.0 | 0.25 (A12+ .125) | 0.5 (A12+ .25) | **0.5 (A12+ .25)** | TheEnding.java:159 |
| `mapRng` seed | `seed + actNum` | `+ actNum*100` | `+ actNum*200` | **`+ actNum*300` = `seed + 1200`** | TheEnding.java:49 |

"DEAD" is a claim with a proof, not an assumption. The room chances feed only
`generateRoomTypes`, which `generateSpecialMap` never calls. The chest chances
feed only `AbstractDungeon.getRandomChest` (:499-508), whose only caller is
`TreasureRoom.onPlayerEntry` — and Act 4 has no treasure room. The relic-tier
chances feed only `AbstractDungeon.returnRandomRelicTier` (:810-819), whose
every caller is an **event** (Addict, The Mausoleum, Big Fish, Dead Adventurer,
Scrap Ooze, Gremlin Wheel Game, We Meet Again) — and Act 4 has no events. The
elite's own tier roll uses its **hard-coded** thresholds instead
(MonsterRoomElite.java:100-112, `<50` COMMON / `>82` RARE / else UNCOMMON) and
the shop uses `ShopScreen.rollRelicTier` (ShopScreen.java:418-428). Only
`colorlessRareChance` (ShopScreen.java:601) and `cardUpgradedChance`
(AbstractDungeon.java:1470) are live in Act 4, and both equal Act 3's. **This
is a negative freeze**: pin the dead constants so nobody "wires them up" later
(§5 trap 10).

## 3. The keys — acquisition, holding, and what they gate

The three keys are `Settings.hasRubyKey` / `hasEmeraldKey` / `hasSapphireKey`
(Settings.java:65-67), cleared at dungeon reset (CardCrawlGame.java:471-473)
and set **only** by `ObtainKeyEffect.update` when its 0.33 s animation expires
(ObtainKeyEffect.java:54-77, cases 1/2/3 → ruby/emerald/sapphire). The effect
**consumes no RNG** and touches no other state; the flags are persisted only
by the ordinary save path (SaveFile.java:236-238), which is out of scope. The
engine already carries them as `RunState::keys` with `kKeyEmerald` /
`kKeyRuby` / `kKeySapphire` (run_state.hpp:215-219, :386-392) — **no schema
change is needed to store a key.**

Because the flag is set on animation completion rather than at claim time,
`AbstractDungeon.java:1731` deliberately exempts `ObtainKeyEffect` from the
`topLevelEffects` purge at `nextRoomTransition`, so a key claimed on the last
frame before leaving a room still lands. The engine's synchronous set is
therefore faithful; the only way to observe the lag would be a second
key-gated read inside 0.33 s of the claim, which no route produces.
Recorded as a deviation-with-argument at the S3.11 task, not a modelling gap.

### 3.1 Ruby — the campfire Recall (already live)

`CampfireUI.initializeButtons` appends `RecallOption` under
`Settings.isFinalActAvailable && !Settings.hasRubyKey` with **no act test**
(CampfireUI.java:94-96); taking it runs `CampfireRecallEffect`
(:39-53) → `ObtainKeyEffect(RED)` (CampfireRecallEffect.java:43). The engine
has modelled the whole thing since S1 (`RestOptionKind::RECALL`,
`rest_sites.cpp`, the `keys |= kKeyRuby` arm in `run_advance.cpp`) — s2-design
§4.5's resolution note. **S3 owes it nothing but the consequence** (§3.4).

### 3.2 Emerald — the burning elite

`AbstractDungeon.setEmeraldElite` (:542-556) runs as the **last statement of
`generateMap`** (:539), i.e. once per procedurally generated act. Under
`Settings.isFinalActAvailable && !Settings.hasEmeraldKey` (:543) it collects
every `MonsterRoomElite` node **row-major** (:544-550), spends **one**
`mapRng.random(0, eliteNodes.size()-1)` (:551) and sets
`chosenNode.hasEmeraldKey = true` (:552).

The engine already models the draw **and** the chosen node — `emerald_x` /
`emerald_y` in `RoomAssignment` and `RunController` (map_rooms.hpp:226-243,
:414-435) — and already applies the entry buff (`run_advance.cpp` step (9),
the `mapRng.random(0,3)` four-arm fan-out of MonsterRoomElite.java:39-68).
**This corrects the S2 deferred-obligations row 89, which says "only the node
flag is missing": the flag is stored; what is missing is the reward row and
the `!hasEmeraldKey` gate.** The row's premise was accurate when written
against `combat_rewards.hpp:107-112` and has been overtaken by the S1 map work;
the correction is recorded in the S3 ledger's deferred table rather than by
editing the S2 row's history.

What S3 adds is `MonsterRoomElite.addEmeraldKey` (:94-98), called from
`dropReward` **after** the relic (and after Black Star's second relic) at :90.
Guard: `isFinalActAvailable && !hasEmeraldKey && !rewards.isEmpty() &&
getCurrMapNode().hasEmeraldKey`. It appends
`new RewardItem(rewards.get(rewards.size()-1), RewardType.EMERALD_KEY)` —
and the **EMERALD arm of that constructor discards the linked argument**
(RewardItem.java:91-95, no `relicLink` assignment), so the emerald row is
free-standing. Claiming it (`claimReward` case 7, :327-332) queues the effect
and removes the row; nothing else moves. Reward-list order at a burning elite
is therefore **GOLD, relic(s), EMERALD_KEY, [potion]** — gold is added at
AbstractRoom.java:302-318, `dropReward()` then `addPotionToRewards()` at
:328-331, and the potion roll's ">= 4 items already assembled forces 0" branch
(:597-599) becomes **reachable for the first time** because of the extra row
(§5 trap 6).

### 3.3 Sapphire — the chest's linked row

`AbstractChest.open` (:62-102) appends the key **last**, linked to the relic
row it just added, under `isFinalActAvailable && !hasSapphireKey` (:95-97) →
`AbstractRoom.addSapphireKey` (AbstractRoom.java:545-547). The SAPPHIRE arm of
`RewardItem(RewardItem, RewardType)` sets the link **both ways**
(RewardItem.java:85-90). Claim semantics are symmetric and mutually
destructive:

- claim the **key** → `relicLink.isDone = true; relicLink.ignoreReward = true`
  (RewardItem.java:321-322): the relic row is removed **unrewarded**;
- claim the **relic** → the same two writes on the key row
  (RewardItem.java:298-301): the key row is removed and, because the effect is
  behind `!this.ignoreReward` (:318), **no `ObtainKeyEffect` fires**.

`AbstractRoom.removeOneRelicFromRewards` (:549-559) removes the partner too.
The `BossChest` never reaches any of this: `BossChest.open` fully overrides
`AbstractChest.open` with no `super` call (BossChest.java:49-63) — already
verified and pinned by S2.11 (`BossChest.NeverAppendsTheSapphireKeyRow`).

The S1/S2 model — "an ignored linked row that costs no RNG or state parity"
(stage-b-design §1.1, §11 v0.1.6) — stays **exactly correct for a run that
claims the relic**, which is why it was safe. S3 replaces it with the real
two-row model, and the S1 behaviour becomes one of its two branches.

### 3.4 Holding a key changes later acts

This is the part that is easy to miss and is the reason keys are a run-layer
feature rather than three booleans:

1. **`setEmeraldElite` stops consuming its `mapRng` draw.** The guard at
   AbstractDungeon.java:543 wraps the *whole* body. Once `hasEmeraldKey` is
   true, every **subsequently generated** act's map skips the draw, so its
   `mapRng` counter and `(s0,s1)` differ from today's engine — and no node is
   marked. The engine draws unconditionally today (map_rooms.hpp:425-439,
   `if (elite_nodes >= 1)`), which is right only while no key exists.
   **This is the single highest-risk change in S3** (§5 trap 1).
2. **The sapphire row stops being appended** on every later chest
   (AbstractChest.java:95). No RNG moves, but the reward list shape does, and
   the ">= 4 items" potion branch stops being reachable that way.
3. **The Recall option disappears** from every later campfire
   (CampfireUI.java:94), changing the rest-site menu's option set and
   therefore the legal-action mask and every ordinal downstream of it.
4. **The Door opens.** `SpireHeart.buttonEffect` case 3 tests all three flags
   plus `isFinalActAvailable` (SpireHeart.java:151); `AbstractMonster
   .onFinalBossVictoryLogic` reads the same predicate to decide whether to
   stop the clock (:1058-1062).

All four are *observable to a policy*, which makes the keys a genuine
sequential decision and not bookkeeping: taking a key costs a chest relic, a
rest, and a fight, and changes the maps of the acts that follow.

## 4. Run-layer mechanics S3 must add

### 4.1 The Act-3 terminal: `VictoryRoom` and the `Spire Heart` dialog

Leaving the (last) Act-3 boss room with `!Settings.isEndless` runs
`goToVictoryRoomOrTheDoor` (ProceedButton.java:104-106, :199-208). **It has no
key branch**: it always builds the synthetic off-grid `MapRoomNode(-1, 15)`
with `new VictoryRoom(VictoryRoom.EventType.HEART)` and calls
`nextRoomTransitionStart()`. The name is a lie about where the branch is; the
branch is inside the event.

Because `isDungeonBeaten` is still false at that point, `updateFading` runs a
**full `nextRoomTransition`** (AbstractDungeon.java:2317-2325) — so the
VictoryRoom is a real floor: `++floorNum`, the trap-7 five-stream reseed
(:1747-1751), and the relic `onEnterRoom` / `justEnteredRoom` fan-outs (Maw
Bank pays its 12 gold here, MawBank.java:29-35), exactly as S2.11 established
for the boss chest. `VictoryRoom` itself has `phase = RoomPhase.EVENT`, hides
the proceed button and constructs the event (VictoryRoom.java:21-33). **It
grants nothing** — no reward, heal, relic, gold or RNG.

`SpireHeart` is a four-click dialog with one always-available option
(SpireHeart.java:70-72 clears `roomEventText` and adds exactly one option; the
`buttonEffect` switch at :118-188 rewrites its label per screen):

| Click | From | To | Effect |
|---|---|---|---|
| 1 | INTRO | MIDDLE | body → the character's Spire-Heart text (:120-125) |
| 2 | MIDDLE | MIDDLE_2 | the score-readout slash VFX (:126-149) |
| 3 | MIDDLE_2 | **GO_TO_ENDING** if `isFinalActAvailable && hasRubyKey && hasEmeraldKey && hasSapphireKey`, else **DEATH** (:150-168) |
| 4a | DEATH | *terminal* | `player.isDying/isDead = true`, `new DeathScreen(null)` (:170-177) |
| 4b | GO_TO_ENDING | the Door | `goToFinalAct()` → `screen = DOOR_UNLOCK`, `doorUnlockScreen.open(true)` (:178-184, :94-98) |

The constructor (:64-92) is entirely score/leaderboard/publisher bookkeeping
and changes no run state. `UNVERIFIED — needs decompile check`: the ordinal
order of the stripped inner enum `SpireHeart$CUR_SCREEN`, which CFR rendered as
bare integer case labels. The mapping above is *derived* from the arms' bodies
and is unambiguous behaviourally (INTRO=1, MIDDLE=2, MIDDLE_2=3, DEATH=4,
GO_TO_ENDING=5); recovering the enum itself is a mechanical run of the
`RECOVERED-INNER-CLASSES.md` §2 procedure and is owned by an S3 row.

**The Door is `DoorUnlockScreen.open(true)`, and it is the only writer of
`nextDungeon = "TheEnding"`** (DoorUnlockScreen.java:143-161, the
`eventVersion` arm at :152-160). There is no Door *room* and no Door node; the
`!eventVersion` arm at :147-151 is the main-menu profile screen and is out of
scope. Its exit sets `getCurrRoom().phase = COMPLETE`, `fadeOut()` and
**`isDungeonBeaten = true`** — the same mechanism the boss-chest crossing uses,
so **the Act-3 → Act-4 crossing adds no floor** (§4.3).

The `Spire Heart` dialog is a genuine player-decision surface: the S3 model
rides `RunPhase::EVENT_DIALOG` and the registry event body (§2.4), so no new
`RunPhase` value is expected — but see §7 for the contingency grant. Note the
engine's landed convention of collapsing a no-state-change dialog click
(shrines.cpp / beyond_events.cpp, accepted at G7): clicks 1 and 2 are exactly
that shape, click 3 is the one that branches, and click 4 is the terminal.
**Whether S3 collapses clicks 1–2 or models them is a capture-parity decision,
not a taste one** — the follower's glue rule 3 already answers collapsed
one-click dialogs (s2v2 §7), so either choice is capture-compatible; make it
once, pin it, and record it, because the differ compares record counts.

### 4.2 The Act-3 stop is a *victory*, and the engine already produces it

Today `run_is_victory` (run_advance.hpp:975-1000) is a boolean keyed on
"RUN_OVER ∧ act == kFinalAct ∧ room == Boss ∧ outcome == KILLED". That is
precisely the Act-3 stop with the VictoryRoom collapsed, and it stays correct
as one of three outcomes. S3 replaces it with a **three-valued run outcome**
(§4.5) so that `victory` and `trueVictor` — the two independent booleans the
game's own metrics carry (Metrics.java:82,107; DeathScreen.java:291-299 vs
VictoryScreen.java:254-269) — are separately readable by training and by the
differ.

### 4.3 The Act-4 crossing, and Act-4 floor numbers

`CardCrawlGame.getDungeon` constructs `new TheEnding(p, theList)`
(CardCrawlGame.java:1114-1116). Its constructor (TheEnding.java:40-52) runs
`super(...)` — hence the whole frozen `AbstractDungeon` chain of s2-design
§4.2, `dungeonTransitionSetup` included — then sets the scene/fade colours,
`initializeLevelSpecificChances()` (:48), `mapRng = new Random(seed + actNum *
300)` (:49) and `generateSpecialMap()` (:50). Everything s2-design §4.2 says
about the crossing therefore applies **unchanged** at the Act-3 → Act-4
boundary: `++actNum` to 4 (AbstractDungeon.java:2563), the cardRng counter
snap to 250/500/750 (:2564-2570), path clear, `EventHelper.resetProbabilities`
(:2575), the event/shrine/monster/elite/boss list clears (:2576-2580),
`blizzardPotionMod = 0` (:2581) and **the A5 heal — 75 % of missing HP at A5+,
else a full heal (:2582-2586)**. Two act-4 specifics: `TheEnding` does **not**
set `currMapNode = new MapRoomNode(0,-1)` in its constructor the way TheCity
does (TheCity.java:49-50), and it calls `generateSpecialMap()` where the others
call `generateMap()` — so **`setEmeraldElite` never runs in Act 4** (its only
call site is AbstractDungeon.java:539, inside `generateMap`) and Act 4 has no
burning elite.

Floors. The Act-3 boss is floor 50 (s2-design §4.2). Each of the following is
a full `nextRoomTransition` (a `nextRoomTransitionStart` with `isDungeonBeaten`
false), so each costs +1 floor; the crossing itself costs 0 because
`DoorUnlockScreen.exit` sets `isDungeonBeaten` first:

| | below A20 | **at A20 (double boss)** |
|---|---|---|
| Act-3 boss | 50 | 50 (first), **51** (second) |
| `VictoryRoom` / `Spire Heart` | **51** | **52** |
| Act 4 **constructed at** | 51 | 52 |
| Act-4 rest (3,0) | 52 | 53 |
| Act-4 shop (3,1) | 53 | 54 |
| Act-4 elite (3,2) | 54 | 55 |
| Act-4 boss (3,3) | 55 | 56 |
| `TrueVictoryRoom` (3,4) | 56 | 57 |

**Act 4's floor base is not a constant** — it is 51 below A20 and 52 at A20,
because the A20 second boss room is a real floor. That breaks the
`act_floor_base(act) = (act-1) * kActFloorSpan` identity (run_advance.hpp:843-857),
which is exact only for acts 1–3. The S3 model must carry the Act-4 base as
**run state established at the crossing**, not as a compile-time function of
the act — and `run_cur_row` (`floor − base − 1`) must read it. Getting this
wrong reproduces exactly the mistake s2-design §4.2's floor row existed to
prevent, one act later. Both halves need SEPARATE witnesses (§6.0): a
capture of the `Spire Heart` floor and a capture of the first Act-4 room, at
both ascension bands, because a single number that happens to match on one
band hides the pair.
`UNVERIFIED — needs decompile check`: the above assumes only the transitions
enumerated here occur between floor 50 and the Act-4 rest; confirm against a
live capture's `floor` sequence at the S3.32 task (the oracle answers this
directly and cheaply once the fork emits an Act-4 dump).

Stream lifetimes are unchanged: `mapRng` is the only per-act reseed
(`seed + 1200`), the floor-scoped five (`monsterHpRng, aiRng, shuffleRng,
cardRandomRng, miscRng`) keep `seed + floorNum` and reseed inside each
transition (:1747-1751), and everything else continues run-wide. Because the
Act-4 construction observes the *pre-transition* floor, the floor-scoped five
still carry `seed + 51` / `seed + 52` throughout it, exactly as 17/34 did.

### 4.4 The Act-4 map

`generateSpecialMap` (TheEnding.java:72-139) builds the map by hand: a
5-row × 7-column grid (`map.add` ×5 at :128-132) in which only the `x == 3`
column carries rooms —

```
y=4   . . . V . . .     TrueVictoryRoom   (no inbound edge)
y=3   . . . B . . .     MonsterRoomBoss   -- The Heart
y=2   . . . E . . .     MonsterRoomElite  -- Shield and Spear
y=1   . . . $ . . .     ShopRoom
y=0   . . . R . . .     RestRoom
```

— with one-directional edges rest→shop→elite (`connectNode`, :86-87, :141-143)
and an explicit `MapEdge` elite→boss (:88). `victoryNode` has **no inbound
edge** and is never entered through the map: `goToTrueVictoryRoom` builds a
*fresh* `MapRoomNode(3, 4)` (ProceedButton.java:191-192). The 28 non-column-3
nodes have `room == null` and no edges. `firstRoomChosen = false` (:137), so
the Act-4 entry is a first-row choice with exactly one real candidate.

Four consequences: **no `MapGenerator`, no `RoomTypeAssigner`, no
`Collections.shuffle`, no room quotas, and no `mapRng` consumption at all
beyond the seeding** — the Act-4 `mapRng` is created and then never drawn from
(§5 trap 2). The map is a constant. The engine's 15×7 `MapNode map[]`
(run_state.hpp:135-149, :209) holds it with rows 5–14 `None`, so **no schema
change is needed for the map itself**.

`UNVERIFIED — needs decompile check` (and cheaply answered by the oracle): the
Act-4 first-row map choice. `MapRoomNode.update`'s first-room arm (:254-279)
gates only on `y == 0` and hover, so the six empty `y == 0` nodes have live
hitboxes; whether the game actually offers them (and what CommunicationMod's
`ChoiceScreenUtils` reports) decides whether the Act-4 map choice has 1 or 7
legal actions. Resolve against the fork before the mask is authored — an
over-wide mask is a leak-gate and a differ problem, not a cosmetic one. Owned
by the S3.32 row.

Boss entry: `DungeonMap.java:68` enables the boss icon when `y == 14` **or**
(`id.equals("TheEnding")` and `getCurrMapNode().y == 2`), so from the Act-4
elite the boss is reachable by the boss button *as well as* by the :88 edge.
Which action kind the engine emits (`MAP_BOSS` vs `MAP_NODE`) is a
capture-parity question with the same answer source; pin it with the choice
above.

### 4.5 The Act-4 rooms and the true-victory terminal

- **Rest (3,0).** An ordinary `RestRoom`; the only `TheEnding` branches are
  cosmetic (`RestRoom.java:34`, `:58` skip the BGM silence/unsilence). The
  campfire's **Recall option is absent** in Act 4 — reaching Act 4 requires
  the ruby key, so `!Settings.hasRubyKey` is false (CampfireUI.java:94-96).
  Every other option, veto and relic interaction is the S1 model.
- **Shop (3,1).** An ordinary `ShopRoom`; `ShopRoom.java:45` only skips the
  shop BGM, and `ShopScreen.java:132-136` / `Merchant.java:86-90` only change
  the idle barks. Prices, `merchantRng`, the purge-cost ramp (which never
  resets, ShopScreen.java:100,281) and `colorlessRareChance` are all the S1
  model at Act-3 values.
- **Elite (3,2).** An ordinary `MonsterRoomElite` running `Shield and Spear`.
  It **does** drop rewards (AbstractRoom.java:327's suppression names only
  `MonsterRoomBoss`): gold `treasureRng.random(25,35)` (:316), a relic at the
  elite's own tier thresholds (MonsterRoomElite.java:80-92,:100-112), the
  potion roll (:330) and the card reward (:334-341). It carries **no emerald
  key** and **no emerald buff** — both gate on `getCurrMapNode().hasEmeraldKey`,
  which no Act-4 node ever has (§4.3). The elite-slain counter falls to
  `elitesModdedSlain` (AbstractRoom.java:309-311), which is cosmetic.
  `eliteTrigger` is true, so elite-keyed relics (Preserved Insect and the rest)
  fire — subject to their floor gates at floor 54/55.
- **Boss (3,3).** `MonsterRoomBoss` running `The Heart`.
  `AbstractRoom.java:327` suppresses `dropReward()` and
  `addPotionToRewards()` for a `MonsterRoomBoss` in `TheEnding`, and no reward
  screen opens — **but the boss gold add at :286-297 is NOT suppressed** (its
  guard is only `this instanceof MonsterRoomBoss`), so the Heart kill still
  spends one `miscRng.random(-5, 5)` on `100 + roll`, ×0.75 rounded at A13+.
  That draw is the terminal surface, exactly as the Act-3 boss's is
  (s2-design §1). The victory jingle **does** play (the :282 guard names only
  `TheBeyond`) — cosmetic.
- **Proceed from the Act-4 boss** → `goToTrueVictoryRoom`
  (ProceedButton.java:107-109, :189-197): a fresh `MapRoomNode(3,4)` +
  `TrueVictoryRoom`, `nextRoomTransitionStart()`. `TrueVictoryRoom`
  (TrueVictoryRoom.java:20-32) sets `phase = INCOMPLETE`, builds a
  character `Cutscene`, hides the proceed button and sets
  `screen = NO_INTERACT` — **the run is non-interactive from here**. The
  cutscene's last panel opens `VictoryScreen` (Cutscene.java:108-125), which
  sets `AbstractDungeon.is_victory = true` (VictoryScreen.java:79) and submits
  `victory=true, trueVictor=true` metrics (:254-269). The engine's terminal is
  therefore the `TrueVictoryRoom` entry; the cutscene is presentation and is
  the S3 analogue of S2's post-victory Spire-Heart skip.

**Run outcome (the frozen model).** Replace the boolean with
`RunVictoryKind { NONE = 0, ACT3_STOP = 1, HEART = 2 }`, written once at the
terminal and readable from `RunController`, `PublicView` and the differ.
`run_is_victory()` keeps its name and its meaning (`kind != NONE`) so no
existing consumer breaks; `trueVictor` is `kind == HEART`. §7 records the
schema site.

### 4.6 A20 and the ascension ladder in Act 4

No new `a20.yaml` rows. Six existing rows gain Act-4 content, all of it
per-monster columns read at the S3.42 / S3.43 batch tasks:

| Row | Act-4 content | Provenance |
|---|---|---|
| A3 | Shield damage 12/34 → 14/38; Spear damage 5→6 and `skewerCount` 3→4 | SpireShield.java:60-66; SpireSpear.java:62-70 |
| A4 | Heart damage 40→45, `bloodHitCount` 12→15 | CorruptHeart.java:77-85 |
| A8 | Shield HP 110→125, Spear HP 160→180 | SpireShield.java:55-59; SpireSpear.java:57-61 |
| A9 | Heart HP 750→800 | CorruptHeart.java:72-76 |
| A18 | both guards' Artifact 1→2; Spear's Burns go to the **draw pile** instead of the discard; Shield's SMASH block becomes a flat 99 | SpireShield.java:72-76,:103-105; SpireSpear.java:75-78,:91-96 |
| A19 | Heart's Invincible 300→200 and Beat of Death 1→2 | CorruptHeart.java:93-102 |

Two A20 **negatives** to pin (§5 trap 8): the A1 elite-quota ×1.6 has no Act-4
effect (no `generateRoomTypes` runs), and the **A20 double boss does not fire
in Act 4** even though `bossList.size() == 2` after the entry pop — the branch
is gated on `AbstractDungeon.id.equals("TheBeyond")` (ProceedButton.java:101-104)
and the `TheEnding` arm at :107-109 is a plain `else if`. The A5 between-act
heal, by contrast, **does** fire at the Act-3 → Act-4 crossing (§4.3), which is
the row's third live site. Both are witnessed by the Act-4 entry capture:
one Heart fight follows the Act-4 boss room, and HP jumps at the crossing.

## 5. Bit-exactness trap list — S3 additions

Continuing stage-b-design §10 and s2-design §5. **Under the 2026-09-03
evidence rule (§6.0) each trap below is a capture obligation, not a unit
test**: it is discharged by a real-run capture whose replay would diverge if
the trap were got wrong, named in the owning task's Log with its seed and its
`replay_run_diff` verdict. Where the trap's effect is combat-internal the
witness is a `--combat` (or `--vitals`) replay of that fight; where it is
run-layer the witness is a `--replay` of the whole run. A trap for which no
capture yet exists is **UNVERIFIED-until-captured** and its row says so.

1. **Holding the emerald key removes a `mapRng` draw from every later act.**
   `setEmeraldElite`'s guard wraps its whole body (AbstractDungeon.java:543),
   so once `hasEmeraldKey` is true no later `generateMap` spends
   `mapRng.random(0, eliteNodes.size()-1)` and no node is marked. The engine
   draws unconditionally today (map_rooms.hpp:425-439). A sim that keeps
   drawing produces a **different Act-2/Act-3 map** for any run that took the
   key in an earlier act — the divergence appears floors later, at map
   generation, not at the claim. **Witness:** a capture that takes the emerald
   key in Act 1 or 2 and then crosses into the next act, replaying zero-diff
   through the crossing — the map and `mapRng` are compared by the differ, so a
   kept draw is a hard RED. The negative control is the paired
   key-not-taken capture on the same seed.
2. **Act-4 construction consumes no `mapRng` beyond its seeding.**
   `generateSpecialMap` (TheEnding.java:72-139) calls no generator, no
   assigner and no shuffle. **Witness:** any Act-4 entry capture — the fork's
   `oracle.streams` carries `mapRng` and the differ compares it, so an Act-4
   construction that spent a draw diverges on the first Act-4 record.
3. **Act-4 construction consumes no `monsterRng`.** No pool draws, no
   exclusion re-rolls, no `Collections.shuffle` in `initializeBoss`
   (TheEnding.java:162-196). Every other act's construction spends
   `monsterRng` heavily; Act 4 spends none, and a shared code path that
   "just runs generateMonsters" would silently shift the stream for the shop
   and everything after it. **Witness:** the same Act-4 entry capture — the
   `mapRng`/`monsterRng` pair is emitted by the fork and compared by the
   differ on the first Act-4 record.
4. **Act-4 monsters consume no `monsterHpRng`.** All three call `setHp(int)`
   (SpireShield.java:55-59, SpireSpear.java:57-61, CorruptHeart.java:72-76).
   The registry rows carry no `rolls:` column. **Witness:** the
   Shield-and-Spear and Heart combats replayed with `--combat`, where a spent
   `monsterHpRng` draw shows up both as an HP diff and as a stream diff.
5. **The Heart kill still spends one `miscRng.random(-5,5)`.**
   AbstractRoom.java:286-297's gold add is gated only on
   `this instanceof MonsterRoomBoss`; the `TheEnding` suppression at :327
   covers `dropReward`/`addPotionToRewards`/the reward screen and **not** the
   gold. A sim that suppresses "all Act-4 boss rewards" loses a draw at the
   terminal. **Witness:** the Heart-kill capture's terminal gold and `miscRng`
   counter, on the last compared record.
6. **The emerald key row makes `addPotionToRewards`' four-item branch
   reachable.** AbstractRoom.java:597-599 forces the potion chance to 0 when
   four reward items are already assembled; the S1 comment
   (`combat_rewards.cpp`) calls that branch unreachable *without the key
   item*. With GOLD + relic + Black Star relic + EMERALD_KEY it is reachable,
   and the roll still runs with its ±10 ratchet (:601-607). **Witness:** a
   directed burning-elite capture on a Black Star run — a four-row reward
   screen whose potion chance is 0 and whose ratchet still moved.
7. **The two guards share one `Surrounded` and one `BackAttack` lifetime.**
   Only SpireShield applies `Surrounded`, to the **player**
   (SpireShield.java:71); `BackAttack` is applied by
   `AbstractMonster.applyPowers` itself (:998-1002); and **either** guard's
   `die()` removes both from everyone still standing
   (SpireShield.java:164-176 == SpireSpear.java:171-183). Kill order is
   therefore observable in the surviving guard's damage. **Witness:** two
   Act-4 elite captures, one per kill order, replayed `--combat` zero-diff
   (S3-G2 item 4 requires both anyway).
8. **A20 has no double boss in Act 4** (ProceedButton.java:101-109) and A1 has
   no elite quota there (no `generateRoomTypes`) — negatives, frozen, and
   witnessed by the Act-4 entry capture (one boss room, one elite node), for
   the same reason s2-design §5 trap 10 froze the shop/rest/chest ones.
9. **`Invincible` resets at the *monster's* turn start, and is a
   damage-modifier, not block.** `atStartOfTurn` restores `amount = maxAmt`
   (InvinciblePower.java:44-48) and `onAttackedToChangeDamage` caps and drains
   (:31-42) at `priority = 99`. In the engine's damage pipeline that stage is
   the `apply_buffer` site (`interp_damage.cpp`, between `decrementBlock` and
   the `onAttacked` fan-out) — **not** `atDamageFinalReceive`. Putting it in
   the wrong pass changes the interaction with block, Buffer, Torii and
   Tungsten Rod. **Witness:** a Heart capture in which a single hit exceeds
   the pool and a later turn's hit lands after the restore, replayed
   `--combat` zero-diff on the Heart's HP per record.
10. **The Act-4 constants are dead and must stay dead** (§2.6): room chances,
    chest chances and relic-tier chances have no Act-4 consumer, and the only
    two live ones equal Act 3's. **Witness:** the Act-4 shop and elite
    captures — a wired-up chest or relic-tier chance would move `treasureRng`
    or `relicRng` and diverge there.
11. **The `Spire Heart` dialog costs a floor and fires relic room-entry
    hooks.** `goToVictoryRoomOrTheDoor` → `nextRoomTransitionStart` →
    `updateFading`'s `!isDungeonBeaten` arm (AbstractDungeon.java:2317-2325):
    `++floorNum`, the five-stream reseed, Maw Bank's +12 gold. The S2 engine
    ends the run one room earlier, so this floor has never been modelled. The
    Door itself then adds **no** floor (`isDungeonBeaten = true`,
    DoorUnlockScreen.java:159) — the same pair-of-numbers trap as
    s2-design §4.2, and it is A20-dependent (§4.3). **Witness:** the
    `Spire Heart` capture's `floor` and Maw Bank gold, compared
    record-by-record, on both branches.

## 6. S3 verification gates (the exit bar)

Two gates, named in the S3 ledger ([s3-tasks.md](s3-tasks.md)): **S3-G1 "S3
rules complete"** (the S2-G1 analogue) and **S3-G2 "S3 verified"** (the S2-G2
analogue; unblocks training-ledger **T5**, the headline). Gate ids stay
ledger-local, as S2's did.

Design constraint carried from [training-plan.md](training-plan.md) §4.4/§7:
the bars must be **coverage-cohort-based and satisfiable by scripted drivers**,
with trained checkpoints an **accelerant, never a precondition**. S2 proved the
form works at three acts — the S2.V2 sim-consulting driver produced the first
three-act A20 wins any instrument in this repo has made
([verification/s2v2-sim-reach.md](verification/s2v2-sim-reach.md)) — and S3
inherits both the instrument and its escalation discipline.

### 6.0 The evidence rule (owner directive, 2026-09-03) — binding on all of S3

**From 2026-09-03 this project does not write or run unit tests. The marker of
truth is oracle / real-run replay.** Every acceptance claim in S3 is a build
plus real-run evidence: the relevant live-game captures replaying **zero-diff**
through `replay_run_diff --replay` (with `--vitals` / `--combat` where the drift
is combat-internal), the committed three-act CI corpus staying zero-diff, and
the reach/cohort reports the scan and campaign tools emit. Phrases like "tier-2
table test", "unit tests green" and "`ctest`" do not appear in the S3 ledger's
Acceptance blocks.

Four consequences, stated here because they are the parts that bite:

1. **A registry row lands with the capture that witnesses it**, not with a
   table test. Where s2-tasks.md said "registry YAML is code: entries land with
   their tier-2 tests in one commit", s3-tasks.md says the row lands with the
   capture — or with the corpus file the capture was promoted into.
2. **Content whose behaviour has no capture yet must obtain one.** The
   sanctioned instrument is a *directed capture*: a `seed_scan` /
   `sim_search` line chosen because it reaches the behaviour, run live through
   the STS-POLICY-IO follower, scored by the differ. This is not a new
   mechanism — it is what S2.43 did for Mind Bloom and the boss-relic axes.
3. **If a directed capture is not yet obtainable, the row is marked
   `UNVERIFIED-until-captured`**, in the ledger, by name, with the reason and
   the blocking prerequisite. It is a first-class status, not a silence: an
   S3-G1 row may carry it, and **S3-G2 may not close while any row still
   does** (that is what item 9 below is).
4. **The reach problem therefore precedes the content problem.** Nothing in
   Act 4 can be witnessed before a keyed three-act win exists, so the
   key-aware driver and the Act-4-aware fork redeploy are *early* tasks, not
   verification-phase ones. §6.1 is the design for that, and the ledger orders
   the phases accordingly.

What remains from the old bar and is unaffected: the build itself must be
green on all six presets (a build is not a test), the Stage-A fixture bytes and
the golden-vector hashes are **replay artifacts**, not unit tests, and they
stay, and `check_stale_counts.sh` / `check_doc_links.sh` stay.

**S3-G1 (content complete):**

1. Every §2 inventory row landed in the registry and **witnessed by at least
   one real-run capture that replays zero-diff**, or explicitly marked
   `UNVERIFIED-until-captured` with its blocking prerequisite named. The
   act-keyed encounter/codegen tables extend to act 4 and are exercised by the
   same captures.
2. Every §5 trap discharged by its named witness capture (§5's per-trap
   **Witness** clauses), or carrying `UNVERIFIED-until-captured`.
3. Every `a20.yaml` row whose S3 status changes (§4.6's six, plus the two
   Act-4 negatives) is IMPLEMENTED with provenance re-read and with the A20
   capture that exercises it named in its row.
4. Sim-side fuzz soak extended across the Act-4 boundary: ≥ 10M actions of
   four-act A20 runs, zero nondeterminism/asserts, zero unimplemented parks
   (replay-twice hashing, B5.1 machinery), with **per-key and per-act-4-room
   coverage counters** so "we never got there" and "we got there and did not
   count it" stay distinguishable (the fuzz `kActBuckets` comment's own rule).
   The soak is a determinism and reach instrument, not a correctness one — it
   proves the engine does the same thing twice, and the captures prove it is
   the right thing.
5. All six presets **build** green; the 20 Stage-A fixtures and the golden
   vectors byte-identical (schema bumps accounted); the committed Act-1 and
   three-act CI corpora still replay zero-diff in every preset;
   `check_stale_counts.sh` and `check_doc_links.sh` clean.
6. The information layer extended at the new `PUBLIC_VIEW_VERSION`, with the
   GT0 leak gates re-run as the **replay-and-compare** instruments they are —
   hidden-twin byte equality in every phase including the Act-4 ones, the
   total-byte classification tripwire, the sampler distributional suite, the
   omniscient-boundary grep ([training-contract.md](training-contract.md)).

**S3-G2 (verified — the T5 unblock):**

1. **Breadth:** ≥ 2,000 distinct full-run A20 Ironclad oracle attempts under
   mixed policies (random-legal + scripted), zero untriaged findings, zero open
   dispositions (Stage B triage process unchanged).
2. **Key depth:** for **each** of the three keys, ≥ 1 zero-diff capture of its
   acquisition *and* ≥ 1 zero-diff capture of the run continuing past it into
   the **next act's map generation** — that last clause is what tests §5 trap 1,
   which is invisible in the claim record itself. Plus, for the sapphire row,
   **both** claim branches (key-taken and relic-taken) witnessed zero-diff.
3. **Act-4 reach:** ≥ 1 zero-diff capture of the `Spire Heart` dialog on **each**
   branch — the Act-3 stop (a key deliberately not taken) and the Door — and
   ≥ 1 zero-diff Act-4 entry continuing through the rest, the shop and the
   elite.
4. **Act-4 depth:** the `Shield and Spear` elite witnessed **killed** zero-diff
   with its full reward set (including a four-item potion-suppression case,
   §5 trap 6) and with **both guard kill orders** witnessed (§5 trap 7); the
   **Heart witnessed killed** zero-diff to the `TrueVictoryRoom` terminal, at
   A20, i.e. **≥ 1 complete A20 Heart-kill victory**. Simulator-selected seed
   cohorts are explicitly sanctioned, on the S2-G2 item-3 precedent: the sim
   pre-scan chooses (seed, policy, policy-seed) triples whose scripted line
   reaches the target, and the oracle then confirms the full run zero-diff.
5. **Defect-family audit:** the g7/S2 proactive manifest extended with every
   S3-campaign-discovered defect family, each retaining multiple named passing
   regressions; the executable audit re-run green
   ([verification/g7_proactive_audit.md](verification/g7_proactive_audit.md)).
6. **Distributional (tier-4) additions:** pre-registered hypotheses for the
   emerald-gate map divergence (trap 1), the Act-4 monsters' `aiRng` cycles
   (Shield case 0, Spear case 2, Heart case 0 — three independent coin flips
   with a deterministic surround), the Heart's `buffCount` ladder, and the
   Act-4 shop/elite reward draws under the floor-gated `canSpawn` family —
   Holm-corrected family, the B5.3/S2.44 α discipline, replicate-before-flagging.
   These are **run-generating experiments over the engine, not unit tests**:
   the instrument is `dist_check`, which plays runs and measures, and the
   directive above does not touch it.
7. **Throughput:** the S2.45 methodology re-run, per-step and per-combat floors
   holding unchanged, and — because the S2 baseline was recorded as an
   explicitly *unquotable* "three-act runs/sec"
   ([verification/s245-throughput.md](verification/s245-throughput.md)) — the
   **first honestly measurable whole-run rate over a policy that actually
   finishes runs**, recorded with methodology. This item also discharges the
   S2 per-step **attribution** obligation (the ×0.712 / ×0.498 A/B).
8. **Contract:** [training-contract.md](training-contract.md) updated and its
   field-by-field completeness audit re-run against the new fields, so the
   training repo can be written against the document rather than the headers.
9. **Zero `UNVERIFIED-until-captured` rows remain** (§6.0 consequence 3):
   every S3 registry row, trap and behaviour claim carries a named capture, or
   an explicit, owner-visible disposition with a recorded reachability
   argument. No wildcard dispositions — the S2-G2 item-4 accounting instrument
   (the coverage join) extended to S3's rows is what closes this.

### 6.1 How Act-4 reach is produced — the design decision

**The precondition is brutal and must be stated first: an Act-4 capture
requires a live run that wins two A20 Act-3 boss fights *while carrying all
three keys*, and there is no shortcut.** The oracle contract is (seed,
action-prefix) from run start — CommunicationMod can `start` a run and drive
it, and save-file loading is out of scope — so Act 4 cannot be entered by
injection, only by playing there. S2 measured what that costs without keys:
three complete double-boss victories out of ~430,000 scanned sim rows, and
they only existed at all because of the sim-consulting driver
([verification/s2v2-sim-reach.md](verification/s2v2-sim-reach.md) §0, §5).
Keys make it strictly harder — the emerald costs a burning-elite fight, the
sapphire costs a chest relic, the ruby costs a campfire.

The design answer is a **key-aware sim-consulting driver plus a re-seeding
scan**, in three sanctioned steps with a pre-registered escalation:

1. **Extend the instrument, not the bar.** `sim_search` gains a key-seeking
   variant that (a) prefers the burning-elite node when HP allows and claims
   the `EMERALD_KEY` row, (b) claims `SAPPHIRE_KEY` at the first chest, (c)
   spends exactly one campfire on `RECALL`; `seed_scan` gains `--need-keys`,
   `--need-act 4` and `--need-heart-kill` filters and its `kMaxActs` goes 3→4
   (`seed_scan.hpp:221-229`, today a hard "the scan cannot answer act 4"
   refusal at `planner/src/main.cpp:285-300`). Scan breadth, then **re-seed on
   the surviving seeds** — the S2.V2 wave structure, where a (seed,
   policy-seed) pair is the cohort unit and one seed yields many deterministic
   lines.
2. **Confirm live, zero-diff, through the existing seam.** Emitted scripts run
   against the game through `script_policy_cmd.py`'s STS-POLICY-IO follower
   with its stop-on-desync contract; a desync is capture evidence, never
   routed around. Two follower/fork prerequisites are known now and are S3
   tasks, not surprises: the **fork must be redeployed** with an Act-4-aware
   emitter (the `Spire Heart` event id, the Act-4 map shape, the key flags and
   the reward-row key types), and `replay_run_diff` must **stop skipping**
   post-victory ending records — today it counts and discards them by name.
3. **Trained checkpoints are an accelerant, explicitly.**
   [training-plan.md](training-plan.md) §4.4 permits a trained Act-1–3 agent
   to drive S3 verification cohorts and forbids the gate from *depending* on
   one. S3 adopts that literally: if a T4-era checkpoint exists when the depth
   wave runs, it may be used behind the same external-policy seam under its own
   SHA-pinned cohort identity, and every bar above must remain reachable
   without it. **Pre-registered escalation, decided now so it is not decided
   under pressure:** if the key-aware scan cannot produce an Act-4 line at
   feasible scale, the next lever is more policy-seed budget on seeds already
   known to win, then a deeper search ply at boss floors only — and *not* rule
   handicaps, difficulty reduction, or a weakened bar. A handicap-assisted
   state is a legal engine state for *training* (training-plan §4.3, T2) and is
   **not** admissible as zero-diff oracle evidence, because the game it
   describes is not the game the oracle plays.

## 7. Registry, namespace and schema impacts

- Ids append after the current per-domain maxima **re-derived at allocation
  time**; the stage-b ledger's "Shared namespaces" section remains the
  allocation authority, and the S3 ledger records every block it grants. Gaps
  stay legal and permanent — including the whole unspent tail of S2's granted
  blocks.
- `encounters.yaml` gains `act: 4` rows and the codegen gains act-4 pool
  tables that are **constants, not pools** (§2.1) — additive to the emitted
  table shapes, as the act dimension itself was.
- `events.yaml` gains one row that belongs to **no** act list (§2.4); the
  loader needs a way to say so without breaking the "event rows land in Java
  insertion order so enum order stays auditable against pool bit positions"
  rule. It appends after the Act-3 block and is excluded from every
  membership bitset — which also keeps `event_flags` / `event_flags_hi`
  byte-comparable across the crossing.
- New `RoomType` values are needed for the two terminal rooms (the
  `VictoryRoom` and the `TrueVictoryRoom`); `kRoomTypeCount` and the fuzz
  coverage array follow, with the `static_assert` S2.11 added.
- `engine::kFinalAct` moves **3 → 4** (run_advance.hpp:848) and every reader
  is a site to audit: `run_is_victory` (:992), the fuzz `kActBuckets` sizing
  (`coverage.hpp:80-88`, which already reserves the act-4 slot on purpose),
  `event_framework.hpp:233-240`'s per-act event-list fall-through, and the
  planner's `kMaxActs` (`seed_scan.hpp:221-229`). Changing the constant without
  the audit is how an act-4 run silently reads Act-1 tables.
- **`SCHEMA_VERSION` 8 → 9 is planned, once, in S3** (`schema.hpp:187`). Its
  contents: the run-outcome kind (§4.5), the Act-4 floor base (§4.3), and the
  reward-row key kinds plus the sapphire link (§3.3, `RewardItemKind` gains
  `EMERALD_KEY` and `SAPPHIRE_KEY`, so `kRewardKindCount` moves 5 → 7). A
  pad-carve is preferred where one exists and a tail append is the fallback,
  on the S2.47 precedent; the ledger names the single owning task so no other
  task bumps it, and the 20 Stage-A fixtures regenerate exactly once there via
  the checked-in generator.
- **`PUBLIC_VIEW_VERSION` 6 → 7**, additive tail. `keys_reserved` and
  `act_reserved` are **already populated** (public_view.hpp:438-446;
  training-contract §1's reserved-field table), so the keys and act index cost
  nothing new — S3 spends only the run-outcome kind, the Act-4 map shape and
  the reward-row key rows. The legal-action mask is an observation channel
  (training-plan §2.1), so the key reward rows and the Act-4 map choice enter
  the hashed public serialization and sit under the same twin tests.
- `resample_hidden`'s contract (training-plan §2.4, training-contract §5a) is
  extended by exactly one fact — Act 4's content is **public and constant**, so
  a fake Act-4 future is deterministic. That *narrows* the posterior rather
  than widening it, which is the safe direction, and the declared coarsenings
  are untouched.
- Contingency grants (`RunPhase`, fuzz `MoveCat`, opcode, power `Hook`) are
  listed in the ledger's "Registry id blocks granted" section with the
  expectation that most are released unspent; the analysis in §2–§4 predicts
  **no** new `MonsterIntent`, **no** new `CardTrigger`, and **no** new
  `ChoiceKind`.

## 8. Deliberately deferred (deferral ≠ drift)

- **Save-file loading** — the key flags' only persistence
  (SaveFile.java:236-238, CardCrawlGame.java:846-849) and the whole
  `AbstractDungeon(p, saveFile)` constructor family, `TheEnding.java:54-70`
  included. Out since S1; S3 does not change that.
- **Other characters** — the Silent/Defect/Watcher content, `FocusPower`
  (§2.3), `AbstractPlayer.getSpireHeartText` / `getSpireHeartSlashEffect` /
  `getWinStreakKey` and the per-character `Cutscene`. S4.
- **Endless mode and daily runs** — pinned false/absent, as S1/S2. Endless is
  the only switch that would give either final boss a reward screen
  (AbstractRoom.java:327), which is why the pin is load-bearing rather than
  decorative.
- **Score, leaderboards and publisher stats** — `GameOverScreen.calcScore`,
  `Metrics`, `StatsScreen.updateFurthestAscent`, the `SpireHeart` constructor's
  whole body (SpireHeart.java:64-92). The sim's objective is the run outcome,
  not the score; the metrics *booleans* are modelled (§4.5), the numbers are
  not.
- **The `DoorUnlockScreen` meta arm and every cosmetic Act-4 branch** — the
  seventeen `TheEnding` hits that only change BGM, VFX, map rendering, scroll
  limits, act-title text and merchant barks (§5 trap 10's inventory).
- **Training-side consumers** — the T5 program itself, and the PublicView
  *encoder* work in the training repo. The simulator publishes; the training
  repo consumes ([training-tasks.md](training-tasks.md)).

## 9. Change log

- 2026-09-03 — v0.1.0 created as the S3 planning exercise the S2-G2 gate Log
  opened: scope inventory, the keys model, the run-layer mechanics, the trap
  list, the two gates and the Act-4 reach design, from a dedicated decompile
  extraction pass over `TheEnding`, `SpireHeart`, `VictoryRoom`,
  `TrueVictoryRoom`, `DoorUnlockScreen`, `ProceedButton`, `ObtainKeyEffect`,
  `RewardItem`, `MonsterRoomElite`, `AbstractChest`, `SpireShield`,
  `SpireSpear`, `CorruptHeart` and the four Act-4 powers. Inherited-obligation
  dispositions and the id blocks are in [s3-tasks.md](s3-tasks.md).
- 2026-09-03 — **§6.0 added: the owner's evidence directive.** From this date
  the project writes and runs no unit tests; the marker of truth is oracle /
  real-run replay. §5's traps became capture obligations with per-trap
  **Witness** clauses, the S3-G1 bar was rewritten to build + capture evidence,
  `UNVERIFIED-until-captured` became a first-class row status, and S3-G2 gained
  item 9 (no such rows may remain at the gate). The structural consequence is
  recorded in §6.0 consequence 4 and is reflected in the ledger's phase order:
  the key-aware driver and the Act-4-aware fork redeploy move **ahead of** the
  Act-4 content, because nothing in Act 4 can be witnessed before a keyed
  three-act win exists.
- 2026-09-03 — **scoping correction recorded against the S2 deferred-obligations
  row for keys** (s2-tasks.md, "Keys as obtainable content"): its premise that
  "only the node flag is missing" is stale. `setEmeraldElite`'s chosen node
  **is** stored (`emerald_x`/`emerald_y`, map_rooms.hpp:226-243) and the entry
  buff **is** applied (`run_advance.cpp` step (9)); what is missing is the
  `EMERALD_KEY` reward row, the sapphire claim semantics, and — the item the
  row does not mention at all — the `!hasEmeraldKey` gate that removes the
  `mapRng` draw from every act generated after the key is taken (§3.4, §5
  trap 1). The S2 row is left as written (it is history); this document and
  the S3 ledger's deferred table carry the correction.
