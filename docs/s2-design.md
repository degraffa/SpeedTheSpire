# S2 Scope + Verification Design — Acts 2–3 (v0.1.0)

**Status:** authored by TE.2 (training ledger,
[training-tasks.md](training-tasks.md)); becomes the binding scope document
for the S2 ledger in [s2-tasks.md](s2-tasks.md) when this change lands.
[stage-a-design.md](stage-a-design.md) and
[stage-b-design.md](stage-b-design.md) remain frozen and in force for
everything they cover — this document *extends* the Stage B spec to Acts 2–3
and never overrides it; [conventions.md](conventions.md) is binding on every
task under it. Amendable only via the change log below. InitialPlan A.5 names
the tier: **S2 = Acts 2–3 + boss-relic swaps** (InitialPlan.md); the
training program's T4 phase is blocked on "engine S2 verified" per
[training-plan.md](training-plan.md) §4.3.

Content facts below were extracted from the decompiled tree at
`D:\STS_BG_Mod\SlayTheSpireDecompiled` for this document (every claim cites
`File.java:line`); the stage-a §1 rule still applies at task time — **numbers
enter the registry only after the implementing task re-reads the cited lines
in full**. This document is the ledger's denominator, not a substitute for
provenance. Anything not verifiable from in-repo sources during this exercise
is marked `UNVERIFIED — needs decompile check`.

---

## 1. S2 scope boundary

- **In:** everything S1 froze (stage-b-design §1.1), plus: the post-boss
  **boss chest / boss-relic pick** after the Act-1 and Act-2 bosses; the
  act-1→2 and act-2→3 **transitions** (§4.2); Act 2 (TheCity) and Act 3
  (TheBeyond) map generation, all room types, full content closure
  (encounters, monsters, elites, bosses, events, the event-granted relics
  and curses of §3.5–§3.6); every A20 modifier with an Act-2/3 effect,
  including the **A20 double boss** (§4.4); and the two Act-2/3 combat
  events plus Mind Bloom's boss re-fight (§3.4).
- **Run termination (frozen):** the run ends when the Act-3 boss kill —
  *both* bosses at A20 — settles its gold. The Act-3 boss opens **no combat
  reward screen and no boss chest** (AbstractRoom.java:327 suppresses
  `dropReward()`/`combatRewardScreen.open()` for a non-endless TheBeyond
  boss; ProceedButton.java:111-113's chest branch requires
  `screen == COMBAT_REWARD`), so the terminal surface is the gold add of
  AbstractRoom.java:286-297. The post-boss routing
  (`goToVictoryRoomOrTheDoor`, ProceedButton.java:199-208 →
  `VictoryRoom(EventType.HEART)`) is **out** — that room is the S3 keys/Door
  surface.
- **Out (S3+):** keys as obtainable content (unchanged from S1: the
  sapphire-key reward row on chests keeps its S1 ignored-linked-row model on
  Act-2 chests; the emerald-elite buff never fires because
  `applyEmeraldEliteBuff` is gated on `hasEmeraldKey`,
  MonsterRoomElite.java:38-68 — but see §4.5 for the mapRng draw and the
  rest-site Recall option, which are **in**), Act 4 / TheEnding, the Heart,
  save-file loading, other characters, Endless mode
  (`Settings.isEndless` branches are pinned false), daily runs.
- **Environment assumption (unchanged):** fully-unlocked profile. Both acts'
  `initializeBoss` special-case unseen bosses via `UnlockTracker.isBossSeen`
  (TheCity.java:161-167, TheBeyond.java:155-160) before the uniform
  `monsterRng.randomLong()` shuffle — the unlocked profile makes the shuffle
  branch the live one, exactly as in S1. The double boss **requires** it:
  the single-boss unlock branch duplicates one boss to size 2, which after
  the first `bossList.remove(0)` (MonsterRoomBoss.java:27-36) is size 1 and
  fails ProceedButton's `bossList.size() == 2` check (§4.4).

## 2. Content inventory — the S2 ledger's denominator

All facts read from the decompiled tree during this exercise. Registry ids
are **append-only**; current maxima are re-derived from `registry/*.yaml` at
allocation time, never from this document (conventions §5; the stage-b
ledger's shared-namespaces section governs allocation).

### 2.1 Encounters — 40 new registry rows

Act 2 (TheCity.java; pool mechanics identical to Exordium's —
`MonsterInfo.normalizeWeights` stable ascending-weight sort, then
`monsterRng.random()` rolls with the repeat-rejection rules of
AbstractDungeon.java:1057-1096):

- **Weak pool** (2 drawn — TheCity.java:88-92; weights all 2.0/10.0,
  :97-102): Spheric Guardian, Chosen, Shell Parasite, 3 Byrds, 2 Thieves.
- **Strong pool** (1 first-strong + 12 drawn — TheCity.java:106-120;
  weights /29): Chosen and Byrds 2, Sentry and Sphere 2, Snake Plant 6,
  Snecko 4, Centurion and Healer 6, Cultist and Chosen 3, 3 Cultists 3,
  Shelled Parasite and Fungi 3. Exclusions keyed on the last weak entry
  (TheCity.java:132-151): Spheric Guardian → Sentry and Sphere; 3 Byrds →
  Chosen and Byrds; Chosen → Chosen and Byrds **and** Cultist and Chosen.
- **Elites** (10 drawn, uniform 1.0/3.0 — TheCity.java:122-130): Gremlin
  Leader, Slavers, Book of Stabbing.
- **Bosses** (TheCity.java:153-182): Automaton, Collector, Champ —
  `Collections.shuffle(bossList, new Random(monsterRng.randomLong()))`
  under the fully-unlocked assumption.
- **Event groups** (MonsterHelper.java:513-521): Masked Bandits,
  Colosseum Nobs, Colosseum Slavers.

Act 3 (TheBeyond.java):

- **Weak pool** (2 drawn — TheBeyond.java:84-99; weights all 2.0):
  3 Darklings, Orb Walker, 3 Shapes.
- **Strong pool** (1 first-strong + 12 drawn — TheBeyond.java:101-115;
  weights all 1.0/8.0): Spire Growth, Transient, 4 Shapes, Maw, Sphere and
  2 Shapes, Jaw Worm Horde, 3 Darklings, Writhing Mass. Exclusions
  (TheBeyond.java:127-145): 3 Darklings → 3 Darklings; Orb Walker → Orb
  Walker (inert — not a strong key); 3 Shapes → 4 Shapes. Note **3
  Darklings appears in both pools** — two registry rows, one `game_id`.
- **Elites** (10 drawn, uniform 2.0 each — TheBeyond.java:117-125):
  Giant Head, Nemesis, Reptomancer.
- **Bosses** (TheBeyond.java:147-176): Awakened One, Time Eater, Donu and
  Deca — same shuffle pattern.
- **Event group** (MonsterHelper.java:582-584): Mysterious Sphere
  (spawns 2 Orb Walkers).

Row count: Act 2 = 5 weak + 8 strong + 3 elite + 3 boss + 3 event = **22**;
Act 3 = 3 weak + 8 strong + 3 elite + 3 boss + 1 event = **18**.

**Composition programs consuming `miscRng`** (bit-exactness-relevant, like
S1's louse/slime rolls): Gremlin Leader — exactly 2 × `miscRng.random(0,7)`
over the 8-slot gremlin pool with duplicates (MonsterHelper.java:507-509,
767-778); 3 Shapes / 4 Shapes — `spawnShapes` draw-without-replacement from
a 6-slot pool (2× Repulsor/Exploder/Spiker), 3 or 4 draws
(MonsterHelper.java:664-692); Sphere and 2 Shapes — 2 × `getAncientShape`
`miscRng.random(2)` (MonsterHelper.java:636-646). Every other Act-2/3
encounter is a fixed spawn list (the Byrd y-offsets are unseeded libGDX
`MathUtils.random`, cosmetic only — MonsterHelper.java:465-467). Jaw Worm
Horde constructs `JawWorm(..., true)` — a constructor variant S1's row never
exercised (`UNVERIFIED — needs decompile check` what the boolean changes).

### 2.2 Monsters — 37 new classes

From the decompiled `monsters/city/` (20 real classes; Mugger$1/$2 are
anonymous inner classes) and `monsters/beyond/` (17):

- **Act 2 (20):** BanditBear, BanditLeader, BanditPointy, BookOfStabbing,
  BronzeAutomaton, BronzeOrb (Automaton summon), Byrd, Centurion, Champ,
  Chosen, GremlinLeader, Healer, Mugger, ShelledParasite, SnakePlant,
  Snecko, SphericGuardian, Taskmaster, TheCollector, TorchHead (Collector
  summon).
- **Act 3 (17):** AwakenedOne, Darkling, Deca, Donu, Exploder, GiantHead,
  Maw, Nemesis, OrbWalker, Reptomancer, Repulsor, SnakeDagger (Reptomancer
  summon), Spiker, SpireGrowth, TimeEater, Transient, WrithingMass.

Act-2/3 encounters also reuse S1 actors (Looter, Cultist, Sentry,
FungiBeast, JawWorm, SlaverBlue/Red, Taskmaster's partners, GremlinNob, the
five gremlins, SphericGuardian in Act 3) — no new rows, but their per-tier
columns must already carry any `>=2/3/4`, `>=7/8/9`, `>=17/18/19` branches;
S1 landed those columns in full, so reuse is free unless a class has an
act-conditional branch (none known; any found at task time is provenance
work, not a schema change). Per-monster A2–A19 stat/behavior branches for
the 37 new classes are read at batch task time exactly as B3.13–B3.22 did.

Summons make spawn-during-combat live content in both acts (BronzeOrb,
TorchHead, SnakeDagger; Darkling revival; Awakened One phase 2) — the
`SPAWN_MONSTER` opcode and Philosopher's-Stone-style hooks exist since
Wave-C; per-monster behavior is batch work.

### 2.3 Events — 20 new rows + per-act gating on 14 existing rows

- **Act 2 eventList** (TheCity.java:184-199, insertion order — 13):
  Addict, Back to Basics, Beggar, Colosseum, Cursed Tome, Drug Dealer,
  Forgotten Altar, Ghosts, Masked Bandits, Nest, The Library, The
  Mausoleum, Vampires.
- **Act 3 eventList** (TheBeyond.java:178-187, insertion order — 7):
  Falling, MindBloom, The Moai Head, Mysterious Sphere, SensoryStone,
  Tomb of Lord Red Mask, Winding Halls.
- **Shrines** (TheCity.java:210-218, TheBeyond.java:198-206): the same six
  rows S1 already carries (Match and Keep!, Wheel of Change, Golden Shrine,
  Transmorgrifier, Purifier, Upgrade Shrine) — list *membership per act* is
  new; no new rows.
- **Shared one-time pool:** the S1 rows stand; what S2 adds is their real
  per-act draw gates (AbstractDungeon.java:1882-1942): Designer (City or
  Beyond, gold ≥ 75), Duplicator (City or Beyond), FaceTrader (City or
  Exordium — **not** Act 3), Knowing Skull (City only, hp > 12), N'loth
  (City only, ≥ 2 relics), The Joust (City only, gold ≥ 50), SecretPortal
  (Beyond only, playtime ≥ 800 s — see §5 trap 5), The Woman in Blue
  (gold ≥ 50, any act), Fountain of Cleansing (cursed), Accursed
  Blacksmith / Bonfire Elementals / Lab / NoteForYourself / WeMeetAgain
  (ungated). The list is built once in Exordium and **carried by reference**
  across acts (CardCrawlGame.java:1102-1119; only call site
  Exordium.java:54).
- **Normal-event floor/state gates** live at AbstractDungeon.java:1944-1990;
  new-to-S2: The Moai Head requires Golden Idol or hp ≤ 50 %; Colosseum
  requires `currMapNode.y > map.size()/2` (top half of the Act-2 map).
- **Combat-embedding events** (ProceedButton.java:115 list): Colosseum
  (Colosseum Slavers then Colosseum Nobs — Colosseum.java:53-76, rewards a
  RARE + an UNCOMMON pool relic :72-73), Masked Bandits
  (MaskedBandits.java:43, win → Red Mask :75-78), Mysterious Sphere
  (MysteriousSphere.java:39, win → RARE pool relic :80), Mind Bloom
  (MindBloom.java:66-80 — shuffles the three *Act-1* boss keys with
  `new Random(miscRng.randomLong())`, fights `list.get(0)`, rewards 50
  gold — 25 at A13+ — plus a RARE pool relic; the Act-1 boss rows get a
  second consumer). `Lab` sits in that ProceedButton list without any
  encounter of its own (reward-screen plumbing;
  `UNVERIFIED — needs decompile check` at the event task).
- **Event payout manifest (verified per event; option trees + A15 branches
  still read in full at task time, the B4.11–B4.13 pattern):** Addict —
  random-tier relic for 85g, or steal → relic + Shame (Addict.java:46-60);
  Cursed Tome — Necronomicon / Enchiridion / Nilry's Codex
  (CursedTome.java:143-159); Drug Dealer — Mutagenic Strength or J.A.X.
  (DrugDealer.java:40-77); Forgotten Altar — Bloody Idol (needs Golden
  Idol) or Decay (ForgottenAltar.java:46-113); Ghosts — 5× Apparition at
  50 % max-HP cost (Ghosts.java:33-91); Nest — Ritual Dagger or gold
  (Nest.java:35-62); The Mausoleum — random-tier relic with a Writhe
  chance (TheMausoleum.java:39-74); Vampires — 5× Bite at 30 % max-HP
  cost, removes an owned Blood Vial (Vampires.java:41-110); Falling —
  forced removal of a random Skill/Power/Attack (Falling.java:52-90);
  Mind Bloom — also Mark of the Bloom, 2× Normality, or full-heal + Doubt
  branches (MindBloom.java:50-126); The Moai Head — surrenders Golden
  Idol for 333g or heal-for-max-HP-loss (MoaiHead.java:35-72); Sensory
  Stone — 1–3 colorless card rewards for HP (SensoryStone.java:83-121);
  Tomb of Lord Red Mask — Red Mask for all gold (TombRedMask.java:38-56);
  Winding Halls — Madness, or heal + Writhe, or max-HP loss
  (WindingHalls.java:54-98); The Library — 1 of 20 random cards or a rest
  heal (TheLibrary.java:39-75); Back to Basics / Beggar — deck/gold only.
  City-gated one-timers with payouts new to live reach: Knowing Skull
  (HP-for-{potion, 90g, card} loop, Sozu-blocked — KnowingSkull.java:
  89-133), The Joust (gold bet 250/100 — TheJoust.java:113-127), N'loth
  (N'loth's Gift for a relic — Nloth.java:41-84), Designer, Duplicator,
  SecretPortal (§5 trap 5).

### 2.4 Relics, cards, potions, powers — deltas only

- **Relics — ~10 new rows.** The five run pools are built and shuffled
  **once per run** (AbstractDungeon.java:1221-1241; only call site
  Exordium.java:38) — Acts 2–3 add **no pool mechanics**, only consumers.
  New S2 reachability: the BOSS pool's primary consumer goes live (§4.1
  boss chest — S1 reached it only via Neow's swap), and the event grants of
  §2.3 land as SPECIAL/EVENT-tier rows. `registry/relics.yaml` already
  carries the audited to-register list in its header commentary (landed
  with the S1 event work): Bloody Idol, Enchiridion, Nilry's Codex,
  Necronomicon, Mutagenic Strength, N'loth's Gift, Red Mask, Mark of the
  Bloom, plus FaceTrader's two unregistered faces (Cultist Mask, N'loth's
  Mask — Face of Cleric, Gremlin Mask and Ssserpent Head are S1 rows
  already; FaceTrader itself is Act-1-reachable). Two dormant S1 gates go
  live: Ectoplasm's `canSpawn: actNum <= 1` (Ectoplasm.java:50-53) now
  actually excludes it from Act-2/3 pops, and the ~20 relics with
  floor-gated `canSpawn` (`floorNum < 48` family — e.g.
  AncientTeaSet.java:84-85, Girya.java:47-50, Matryoshka.java:59-60 ≤ 40,
  PreservedInsect.java:44-45 ≤ 52) start rejecting in late Act 3 — §5
  trap 9.
- **Cards — 5 new rows, no pool changes.** Card pools are rebuilt from the
  same character pool every act with no RNG (AbstractDungeon.java:294,
  1135-1201), so red/colorless pool rows are done. New rows are the
  event/monster-granted specials absent from the S1 registry:
  **Apparition** (Ghosts), **Bite** (Vampires), **J.A.X.** (Drug Dealer),
  **Ritual Dagger** (Nest), and the curse **Necronomicurse** — granted by
  the Necronomicon *relic* on equip (Necronomicon.java:43-44), unremovable
  (CardGroup.java:981, AbstractPlayer.java:744). Madness and every S2
  event-granted curse (Shame, Decay, Writhe, Normality, Doubt, Parasite —
  the last also monster-granted by Writhing Mass, WrithingMass.java:118)
  are already S1 rows; they gain sources, not rows. Pride is unreachable
  in a standard run (blight-only source, GrotesqueTrophy.java:34-47) and
  stays out. Status-card sources added by Act-2/3 monsters (all existing
  rows): Wound — Taskmaster (Taskmaster.java:72, the *only* Act-2 status
  source) and SnakeDagger (SnakeDagger.java:66); Burn — OrbWalker
  (OrbWalker.java:94), Nemesis (Nemesis.java:108-111); Dazed — Repulsor
  (Repulsor.java:68), Deca (Deca.java:118); Slimed — TimeEater
  (TimeEater.java:142); **Void — Awakened One** (AwakenedOne.java:194;
  the only non-Act-4 source in the tree).
- **Potions — no new rows.** `PotionHelper.getPotions` filters by class
  only (PotionHelper.java:88; the file's sole AbstractDungeon reference is
  the `potionRng` draw at :170). `initializePotions()` re-runs per act
  (AbstractDungeon.java:298); tier gate, rejection sampling and
  `blizzardPotionMod` mechanics are unchanged S1 rows, with the ratchet
  **reset to 0 at act transition** (AbstractDungeon.java:2581).
- **Powers — ~20–30 new rows** (count is an output of extraction, as in
  S1). Distinct powers applied by Act-2/3 monsters and not yet in
  `powers.yaml`: Act 2 — PainfulStabs (BookOfStabbing.java:80), Malleable
  (SnakePlant.java:70), Minion (GremlinLeader.java:99), Hex
  (Chosen.java:134), Flight (Byrd.java:103), plus BronzeOrb's Stasis
  mechanism (`ApplyStasisAction`, BronzeOrb.java:73 — an action, not a
  `Power` ctor; model decided at the batch task). Act 3 — Reactive +
  Malleable (WrithingMass.java:82-83), Slow (GiantHead.java:82), Fading +
  Shifting (Transient.java:66-70), Explosive (Exploder.java:66),
  RegenerateMonster + Curiosity + Unawakened (AwakenedOne.java:145-151),
  TimeWarp + DrawReduction (TimeEater.java:107,140), GenericStrengthUp
  (OrbWalker.java:76), Constricted (SpireGrowth.java:85), Regrow
  (Darkling.java:97). Already-registered powers gaining Act-2/3 users
  (Strength, Dexterity, Weak, Vulnerable, Frail, Artifact, Metallicize,
  Plated Armor, Barricade, Confusion, Thievery, Thorns, Intangible) need
  no rows; relic/event-pulled powers surface in the per-batch closure.

### 2.5 Run layer — per-act constants

Both acts pin the same map quotas as Exordium (shop 0.05, rest 0.12,
treasure 0.0, event 0.22, elite 0.08 — TheCity.java:70-85,
TheBeyond.java:67-82), the same chest tables (50/33/17), the same relic-tier
tables (50/33/17), and the same `colorlessRareChance` 0.3. The per-act
deltas that exist:

| Constant | Act 1 | Act 2 | Act 3 | Provenance |
|---|---|---|---|---|
| `cardUpgradedChance` | 0.0 | 0.25 (A12+: 0.125) | 0.5 (A12+: 0.25) | Exordium.java:107; TheCity.java:84; TheBeyond.java:81 |
| `mapRng` seed | `seed + actNum` | `seed + actNum*100` | `seed + actNum*200` | Exordium.java:56; TheCity.java:46; TheBeyond.java:44 |
| weak draws | 3 | 2 | 2 | Exordium.java:110-126; TheCity.java:88-92; TheBeyond.java:84-99 |

The upgrade roll is `cardRng.randomBoolean(cardUpgradedChance)` per non-RARE
reward card and **is consumed even in Act 1 where the chance is 0.0**
(AbstractDungeon.java:1470-1473) — S1's stream position is already correct;
S2 only makes the outcome live. A12 is therefore **no longer a no-op**: the
S1 negative freeze in `a20.yaml` is act-scoped and its row flips to
IMPLEMENTED under S2 (§4.4).

## 3. Sections renumbered — see §2

(§3 intentionally collapsed into §2; kept as an anchor-stable stub so later
amendments do not renumber citations into this document.)

## 4. Run-layer mechanics S2 must add

### 4.1 Boss chest + boss-relic pick (after Act-1 and Act-2 bosses)

`TreasureRoomBoss.onPlayerEntry` constructs the chest and **rolls at room
entry, not pick time** (TreasureRoomBoss.java:56-64): exactly 3 relics via
`returnRandomRelic(RelicTier.BOSS)` (BossChest.java:35-39), each a **front
pop** of the pre-shuffled `bossRelicPool` — for BOSS tier *both*
`returnRandomRelicKey` and `returnEndRandomRelicKey` are `remove(0)`
(AbstractDungeon.java:792-798, 739-745), with the `canSpawn` re-check
recursion at :804-807 and `Red Circlet` on empty. **No RNG is consumed by
the pop itself.** Skip discards all three permanently — `relicSkipLogic`
returns nothing to the pool (BossRelicSelectScreen.java:202-212); leaving
without choosing triggers `noPick()`, metrics only (ProceedButton.java:
231-234, BossRelicSelectScreen.java:240-248). This is the same pool Neow's
category-3 swap front-pops in S1, so pool state composes across both
consumers. `PublicView` already reserves the boss-relic screen fields
(training-plan §2.1); T4.1 (training ledger) populates them once this
lands.

### 4.2 Act transition

`dungeonTransitionSetup` (AbstractDungeon.java:2562-2604) in order:
`++actNum`; **cardRng counter snapping** — the counter is rounded *up* to
250/500/750 if it lies in (0,250)/(250,500)/(500,750)
(AbstractDungeon.java:2564-2570; §5 trap 1); path clear;
`EventHelper.resetProbabilities()` (?-room pity → base values,
EventHelper.java:189-195); event/shrine/monster/elite/boss lists cleared;
`blizzardPotionMod = 0`; **A5 heal** — at A5+ heal 75 % of missing HP, else
full heal (:2582-2586). The A6/A10/A14 block at :2590-2602 is gated
`floorNum <= 1 && dungeon instanceof Exordium` — **run-start only, not
per-act** (the S1 rows are already correct; tier-2 pins the negative).
`floorNum` is never reset — numbering is continuous (floor 17 opens Act 2,
34 opens Act 3, both `UNVERIFIED — needs decompile check` as exact values;
what is verified is only that no reset exists in :2562-2604).

Then the fresh dungeon's constructor chain runs in a **frozen order**
(AbstractDungeon.java:268-308): `dungeonTransitionSetup` →
`generateMonsters` → `initializeBoss` → `setBoss(bossList.get(0))` →
`initializeEventList` → `initializeShrineList` → `initializeCardPools` →
`initializePotions`. Stream lifetimes (all verified from
AbstractDungeon.java:398-412, 1747-1751 and the dungeon constructors):
`mapRng` is the **only** per-act reseed (`seed + actNum*K`, §2.5); the
floor-scoped five (`monsterHpRng, aiRng, shuffleRng, cardRandomRng,
miscRng`) keep their `seed + floorNum` rule across acts; everything else
(`monsterRng, eventRng, merchantRng, cardRng, treasureRng, relicRng,
potionRng`) is a run-lifetime stream that simply continues — Act-2/3
monster-list generation consumes the same `monsterRng` the Act-1 lists did.
Carried vs reset across the boundary: `cardBlizzRandomizer` **carried**;
?-room pity **reset**; `blizzardPotionMod` **reset**; relic pools
**carried, depleted**; card pools rebuilt (no RNG, effectively idempotent);
`specialOneTimeEventList` **carried by reference** with draws removing
entries run-wide.

The belief-sampler consequence (training-plan §2.4) is already provisioned:
`resample_hidden`'s fresh-fake-run-seed rule regenerates fake Act-2/3
futures automatically, and the encounter-suffix Markov row extends to the
per-act lists; the boss-list conditioning on public `boss_list[0]` is
exactly what the A20 double boss needs (§4.4).

### 4.3 Map generation for Acts 2–3

Same generator, same quotas (§2.5). `setEmeraldElite` runs for every
generated act (call site AbstractDungeon.java:539) and consumes its one
`mapRng` draw under the same fully-unlocked gate as S1 — per act now, not
once. The buff itself stays dead without a key (§1). TheEnding's
`generateSpecialMap` is S3.

### 4.4 A20 double boss (Act 3 only)

ProceedButton.java:100-110: leaving the Act-3 boss room with
`ascensionLevel >= 20 && bossList.size() == 2` routes to `goToDoubleBoss()`
(:210-220) — `bossKey = bossList.get(0)`, a synthetic `MapRoomNode(-1,15)`
with a fresh `MonsterRoomBoss`, straight into the second fight.
`MonsterRoomBoss.onPlayerEntry` pops the list (`bossList.remove(0)`,
MonsterRoomBoss.java:27-36), so the second boss is **`bossList[1]` of the
Act-3 shuffle** — public conditioning surface for the sampler. Each boss
adds gold (`100 + miscRng.random(-5,5)`, ×0.75 rounded at A13+ —
AbstractRoom.java:286-297) and neither opens a reward screen (§1). A20
double-boss is Act-3-only: no branch exists for Acts 1–2 in
ProceedButton.java:99-113 (the S1 negative freeze stands for Act 1; S2's
tier-2 pins it for Act 2). `AbstractMonster.onFinalBossVictoryLogic`
(:1058-1085) skips its body between the two bosses — sim-relevant only if a
modeled hook hangs off it (none in S1; re-check at task time).

### 4.5 Rest sites, shops, treasure in Acts 2–3

No act gate exists in the shop, campfire, or chest code beyond the
constants of §2.5 — grep-verified (§5 trap 10): ShopScreen prices/layout,
chest tables, rest heal 30 % are all act-independent S1 rows, and shop
purge cost ramps run-wide. Two S2-specific surfaces: (a) the
sapphire-key reward row fires on Act-2 chest opens exactly as S1's model
already handles (claim-the-relic keeps parity); whether the **boss** chest
also appends one is `UNVERIFIED — needs decompile check` at the §4.1 task
(BossChest routes through the boss-relic screen, not the AbstractChest open
path, so likely no — do not assume). (b) With `isFinalActAvailable` true, a
keyed campfire **Recall** option may appear at Act-2/3 rest sites
(`UNVERIFIED — needs decompile check`: CampfireUI option construction — if
present it is on-screen every Act-2/3 rest and must be modeled as a
visible-but-key-granting option whose take is out of scope, or the oracle
diverges on the menu surface; resolve before the rest-site task, and if the
divergence is real, the S1 rest-menu model needs the row with the key grant
stubbed as S3).

## 5. Bit-exactness trap list — S2 additions

Each becomes a named test, continuing stage-b-design §10:

1. **cardRng counter snapping at act transition** (AbstractDungeon.java:
   2564-2570): the counter is rounded up to the next multiple of 250 (three
   explicit bands) — a sim that just continues the stream diverges on the
   first Act-2 card reward. Snap semantics per band boundary (counter
   exactly 250 does *not* snap to 500 — the bands are open intervals).
2. **The upgrade roll consumes cardRng in every act** (AbstractDungeon.java:
   1470-1473) — including Act 1 at chance 0.0. S1 already models this;
   the tier-2 test moves from "negative" to per-act value assertions
   (0.25/0.125, 0.5/0.25).
3. **Boss-relic pops are RNG-free and permanent** (§4.1): three front pops
   at *room entry*; skip burns them. Pool-cursor divergence surfaces many
   floors later at the next boss chest or Neow swap — the test asserts pool
   state, not just the offer.
4. **First-strong re-roll loop** (AbstractDungeon.java:1057-1062) consumes
   one `monsterRng.random()` per rejected roll — with Act-2's three-way
   exclusion (Chosen excludes two strong keys) rejection is more frequent
   than S1's; the draw-count is part of the contract.
5. **SecretPortal gates on wall-clock playtime** (`CardCrawlGame.playtime
   >= 800.0f`, AbstractDungeon.java:1929-1933) — nondeterministic input.
   Frozen S2 decision: the sim pins the gate FALSE (a sub-13-minute reach
   of Act 3 is not a supported oracle scenario) and the campaign driver
   must record playtime at capture so a violated assumption is detectable,
   not silent. Revisit only with a reproducer.
6. **Mind Bloom consumes `miscRng.randomLong()`** for its boss shuffle
   (MindBloom.java:66-80) — floor-scoped stream, event-order-sensitive.
7. **`specialOneTimeEventList` is cross-act state** (§2.3): a draw in Act 1
   removes the row for Acts 2–3. The one-time pool is already public
   perfect-memory state (training-plan §1); the sim's membership bitsets
   just extend their lifetime semantics across acts.
8. **3 Darklings self-exclusion** (TheBeyond.java:131-134): the same key in
   both pools means the weak→strong exclusion can fire on an
   *identical-name* adjacency — order the registry rows so the stable sort
   reproduces the game's tie order (encounters.yaml id-order rule).
9. **Dormant `canSpawn` gates wake up in Acts 2–3** (§2.4): Ectoplasm's
   `actNum <= 1` and the ~20 floor-gated relics (`floorNum < 48/40/52`
   family) begin rejecting at pop time, driving the
   AbstractDungeon.java:804-807 recursion on pools whose S1 tests never
   saw a rejection from these rows. The tier-2 additions assert the gate
   values per row; a tier-4 hypothesis covers the pool-cursor consequence.
10. **No shop/rest/chest act gates exist** (negative freeze — grep-verified
    over ShopScreen.java, RestRoom.java/CampfireUI.java, the three chest
    classes and ShopRoom.java: the only dungeon checks are `TheEnding`
    ones). Purge cost ramps per run, never resetting at the act boundary
    (ShopScreen.java:100, 281; reset only in the new-run block,
    CardCrawlGame.java:478). Pin the negatives in tier-2 so nobody
    "fixes" them later, exactly like the §6 negative freeze in
    stage-b-design.

## 6. S2 verification gates (the exit bar — G7-style, coverage-directed)

Two gates, named in the S2 ledger ([s2-tasks.md](s2-tasks.md)): **S2-G1
"S2 rules complete"** (content closure, the G6 analogue) and **S2-G2 "S2
verified"** (the G7 analogue; unblocks training-ledger T4). Gate ids are
deliberately ledger-local (the G-series stays reserved for Stage C planning
per G7's closing note; the GT-series is the training ledger's).

Design constraint carried from training-plan §4.4/§7: the bars must be
**coverage-cohort-based and satisfiable by scripted TE.1-class drivers** —
no raw action quotas, and no dependency on trained checkpoints (those are an
accelerant when GT2 produces them, never the gate's precondition). The TE.1
precedent ([verification/te1-survival-cohort.md](verification/te1-survival-cohort.md)):
a scripted survival policy behind the external-policy seam reached 31 % Act-1
boss-fight rate where random play reached ~0 %; the same seam accepts any
deterministic scripted policy, including sim-consulting ones.

**S2-G1 (content complete):**

1. Every §2 inventory row landed in the registry with 100 % tier-2 coverage
   per the manifest check (the §4.3 codegen manifest extended to act-keyed
   encounter pools).
2. Every §5 trap has its named test.
3. Every `a20.yaml` row whose S2 status changes (A5 between-act heal, A12
   per-act upgrade chance, A20 double boss; plus the per-monster A2–A19
   columns of every new monster row) is IMPLEMENTED with provenance re-read
   and tier-2 coverage.
4. Sim-side fuzz soak extended across the act boundaries: ≥ 10M actions of
   three-act A20 runs, zero nondeterminism/asserts (replay-twice hashing,
   B5.1 machinery).
5. All six presets green; 20 Stage-A fixtures byte-identical (schema bumps
   accounted).

**S2-G2 (verified — the training-repo unblock):**

1. **Breadth:** ≥ 2,000 distinct full-run A20 Ironclad oracle attempts under
   mixed policies (random-legal + survival/scripted external policies),
   zero untriaged findings, zero open dispositions (Stage B triage process
   unchanged).
2. **Act-2 depth:** ≥ 1 zero-diff boss-reward claim **and boss-chest
   boss-relic pick** for every Act-2 registry BOSS row (both a take and at
   least one skip witnessed across the cohort), each followed by a zero-diff
   act-2→3 transition into a playable Act-3 floor.
3. **Act-3 depth:** every Act-3 registry BOSS row witnessed killed
   zero-diff, and ≥ 3 completed A20 **double-boss** runs (both bosses in one
   run, gold settlement zero-diff, covering ≥ 2 distinct first-boss
   identities). Simulator-selected seed cohorts are explicitly sanctioned
   (the G7 precedent: sim pre-scan chooses (seed, policy, policy-seed)
   triples whose scripted line reaches the target; the oracle then confirms
   the full run zero-diff).
4. **Event depth:** every Act-2/3 event row sighted in ≥ 1 zero-diff oracle
   run *or* carrying an explicit per-row disposition (directed capture or a
   recorded reachability argument) — the §7.4 coverage join is the
   accounting instrument; no wildcard dispositions.
5. **Defect-family audit:** the g7 proactive manifest extended with every
   S2-campaign-discovered defect family, each retaining multiple named
   passing regressions; the executable audit re-run green.
6. **Tier-4 additions:** pre-registered distributional hypotheses for
   act-2/3 encounter pools (incl. first-strong exclusion effects), per-act
   `cardUpgradedChance`, boss shuffle + double-boss conditioning, and the
   §2.3 one-time-pool cross-act depletion — Holm-corrected family, same
   α discipline as B5.3.
7. **Throughput floors re-baselined honestly:** three-act runs are longer,
   so the S1 whole-machine runs/sec floor is not comparable; S2-G2 re-runs
   the B5.5 methodology, requires the *per-step* and *per-combat* floors to
   hold unchanged, and records a new three-act whole-run number with
   methodology as the S3 baseline (a drop proportional to run length is
   expected and is not a regression; interpreter regressions are what the
   per-step floor catches).

**Driver risk, named now (training-plan §7 risk 1):** Act-3-depth cohorts
need scripted lines that *win* two A20 boss fights. If the TE.1 survival
family's deep-reach rate proves too low even under sim pre-scan at scale,
the sanctioned escalation is a **sim-consulting scripted driver** (shallow
rollout/1-ply lookahead using the engine itself — deterministic, weight-free,
still TE.1-class behind the same seam), not waiting for GT2 checkpoints and
not weakening the bar. The S2 ledger carries this as its own task
(S2.V2) with the pre-scan tooling.

## 7. Registry & namespace notes for the authoring waves

- Ids append after the current per-domain maxima **re-derived at allocation
  time**; the stage-b ledger's "Shared namespaces" section remains the
  allocation authority, and the S2 ledger records every block it grants.
  Gaps stay legal and permanent.
- `encounters.yaml` grows an act dimension: rows carry `act: 2|3`; codegen
  emits per-act pool tables. The existing `act: 1` rows and emitted S1
  tables are untouched (append-only extends to the emitted table *shapes* —
  additive only).
- New `MonsterIntent` vocabulary values, new `RunPhase` values (boss-relic
  screen), new fuzz `MoveCat`s, and new opcodes are all shared-namespace
  claims through the stage-b ledger section, per its existing protocol.
- Event rows land in Java insertion order (events.yaml precedent) so enum
  order stays auditable against pool bit positions; Act-2 block then Act-3
  block.

## 8. Deliberately deferred (deferral ≠ drift)

- The VictoryRoom / SpireHeart / Door surface, keys as obtainable content,
  TheEnding — S3 (with §4.5's Recall-option check the one S2-time probe
  into that boundary).
- Endless-mode branches, daily-run branches: pinned false/absent, as S1.
- The standalone encounter-outcome model and all training-side consumers:
  training repo (T4.1 owns the PublicView S2 extension against the reserved
  fields — explicitly not an S2-ledger deliverable).
- Act-2/3 *content* for other characters: S4.

## 9. Change log

- 2026-08-03 — v0.1.0 created by TE.2 (scope inventory, run-layer
  mechanics, trap list, verification gates), from a dedicated decompile
  extraction pass; see the TE.2 Log in
  [training-tasks.md](training-tasks.md) for the exercise's provenance
  notes and the UNVERIFIED list.
