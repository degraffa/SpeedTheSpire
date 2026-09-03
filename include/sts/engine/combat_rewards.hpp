#pragma once

// combat_rewards.hpp -- post-combat reward assembly + claim (design §5.6
// "Rewards"). This is the run layer's model of the game's battle-over reward
// block and the COMBAT_REWARD / CARD_REWARD screens. Assembly happens ONCE, at
// the moment the reward screen opens (all stream draws for gold / relic tier /
// potion / cards are consumed there, exactly as the game consumes them when
// AbstractRoom.update's battle-over block runs and CombatRewardScreen.open()
// calls setupItemReward); claiming afterwards is stream-free except for relic
// onEquip bodies (which may consume miscRng, e.g. War Paint / Whetstone).
//
// Provenance (every method read in full from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * AbstractRoom.update battle-over block (AbstractRoom.java:277-357): gold
//     first (boss :286-298 / elite :302-318 / plain monster :319-326), then
//     dropReward() (:329), then addPotionToRewards() (:330), then the screen
//     opens (:334-341) -- setupItemReward is what rolls the cards.
//   * AbstractRoom.getCardRarity (AbstractRoom.java:148-177): thresholds are
//     `roll < rareCardChance` then `roll < rareCardChance + uncommonCardChance`
//     -- the widths 3/37 (fields :108-109) produce thresholds `< 3` / `< 40`.
//     Coding the widths as thresholds is wrong by 3 points on every reward.
//     MonsterRoomElite overrides the WIDTHS to 10/40 (MonsterRoomElite.java:
//     34-35) -> elite thresholds `< 10` / `< 50`; MonsterRoomBoss.getCardRarity
//     ignores the roll and always returns RARE (MonsterRoomBoss.java:40-42).
//   * AbstractDungeon.getRewardCards (AbstractDungeon.java:1423-1479): count 3
//     (+relic changeNumberOfCardsInReward pass :1426-1428), per card one
//     rollRarity -> pity switch -> no-dupe re-roll loop, then the makeCopy +
//     upgrade/preview pass (:1465-1477).
//   * AbstractDungeon.rollRarity (AbstractDungeon.java:1597-1619): the roll is
//     `cardRng.random(99) + cardBlizzRandomizer`. TRAP 13: rollRarity(Random
//     rng) IGNORES its parameter and always draws cardRng (:1598).
//   * Pity: cardBlizzRandomizer starts +5, COMMON steps it -1, floor -40, RARE
//     resets to +5, UNCOMMON leaves it (AbstractDungeon.java:1437-1451 with the
//     constants at :2773-2775: cardBlizzStartOffset=5, cardBlizzGrowth=1,
//     cardBlizzMaxOffset=-40).
//   * The dupe re-roll: getCard(rarity) = pool.getRandomCard(true) =
//     group.get(cardRng.random(size-1)) (CardGroup.java:502-506) -- a PURE
//     indexed read, one cardRng draw per attempt, re-rolled while the id
//     collides with an already-picked reward card (AbstractDungeon.java:
//     1452-1461). Nothing is removed from the pool.
//   * The upgrade pass (AbstractDungeon.java:1469-1477): `c.rarity != RARE &&
//     cardRng.randomBoolean(cardUpgradedChance) && c.canUpgrade()`. In Act 1
//     cardUpgradedChance = 0.0f (Exordium.java:107) so no card ever upgrades,
//     but the randomBoolean draw (Random.java:83-86 -- one nextFloat, counter++)
//     STILL HAPPENS for every non-RARE card; only `c.rarity != RARE`
//     short-circuits it. Getting this wrong desyncs cardRng.
//   * The OFFER PREVIEW pass, the SAME loop's else-arm (:1474-1476): every card
//     the upgrade roll did NOT take runs `for (r : player.relics)
//     r.onPreviewObtainCard(c)`, and the RewardItem(CardColor) constructor
//     (RewardItem.java:157-161) repeats it over the whole item. The only three
//     overrides of that method in the game are the eggs, so the net is: an egg
//     owner's card reward SHOWS its matching-type offers already upgraded.
//     Consumes no RNG; modelled by apply_offer_previews (combat_rewards.cpp).
//   * Gold: boss = 100 + miscRng.random(-5,5), MathUtils.round(x0.75f) at A13+
//     (AbstractRoom.java:291-296); elite = treasureRng.random(25,35) (:316);
//     plain monster = treasureRng.random(10,20) (:324). TRAP 18: boss gold is
//     miscRng, elite/normal gold is treasureRng -- stream attribution is spec.
//   * Potion: chance = 40 + blizzardPotionMod for elite / plain-monster (no
//     escape) / event-room combats, White Beast Statue forces 100, >= 4 items
//     already assembled forces 0 (AbstractRoom.addPotionToRewards :580-608).
//     The potionRng.random(0,99) roll is UNCONDITIONAL -- it happens even at
//     chance 0 -- and the +/-10 ratchet moves on every roll (:601-607).
//     Identity via returnRandomPotion(false) (trap 14 rejection sampling).
//   * Elite relic: one relicRng.random(0,99) tier roll (<50 C / >82 R / else U,
//     MonsterRoomElite.java:100-112 == relic_tier_for_roll) then
//     returnRandomRelic(tier) -- a pool FRONT pop + canSpawn recheck at
//     ASSEMBLY time (AbstractDungeon.java:676-679, 757-808). Black Star adds a
//     second tier roll + returnRandomNonCampfireRelic (MonsterRoomElite.java:
//     81-92; AbstractDungeon.java:690-697 -- rejected Peace Pipe / Shovel /
//     Girya pops are CONSUMED from the pool, not put back).
//   * Claim: RewardItem.claimReward (RewardItem.java:255-336) -- gold through
//     player.gainGold, potion through obtainPotion (Sozu discards it first,
//     :276-279), relic through instantObtain, CARD opens cardRewardScreen.
//     Golden Idol's +25% is computed against the reward at construction
//     (applyGoldBonus, RewardItem.java:110-129, MathUtils.round(tmp * 0.25f)).
//   * Card claim: CardRewardScreen (CardRewardScreen.java:380-470): pick a card
//     (acquireCard -> FastCardObtainEffect -> masterDeck add + every relic's
//     onObtainCard, FastCardObtainEffect.java:46), or SKIP (item stays
//     claimable until the screen is left), or Singing Bowl's +2 max HP
//     (SingingBowlButton.onClick -> increaseMaxHp(2,true) which also heals 2 --
//     AbstractCreature.java:199-209 -- and removes the item).
//   * Stolen gold: DamageAction.stealGold deducts the player's gold AT STEAL
//     TIME, clamped per hit, writing this.target.gold directly -- NOT through
//     gainGold, so no relic hook sees the theft (DamageAction.java:98-114).
//     Looter.die() hands its clamped accrual to addStolenGoldToRewards
//     (Looter.java:170-172; AbstractRoom.java:619-626), which constructs
//     RewardItem(gold, true): applyGoldBonus's theft branch takes NO Golden
//     Idol bonus (RewardItem.java:104-129). Since die() runs DURING combat,
//     the STOLEN_GOLD item PRECEDES every battle-over item in the rewards
//     list. Claim is player.gainGold, body-identical to the GOLD case
//     (RewardItem.claimReward, RewardItem.java:255-273). An ESCAPED thief's
//     accrual is returned by nothing: the gold is simply gone.
//   * Escape gates: MonsterGroup.haveMonstersEscaped (MonsterGroup.java:
//     124-130) is true iff EVERY monster escaped -- a dead monster is NOT
//     escaped, so any kill in the group keeps it false. It (not the mugged
//     flag) gates the plain-monster gold roll (AbstractRoom.java:319) and
//     zeroes the potion CHANCE (:585-589; the roll and ratchet still run, and
//     White Beast Statue's 100 overrides even the zero, :594-596). The
//     mugged/smoked room flags pick only the SCREEN (:334-341, mugged first):
//     a mugged openCombat still calls setupItemReward
//     (CombatRewardScreen.java:280-285); a smoked one never does (:286-288).
//
// COLORLESS IS UNREACHABLE FROM A *COMBAT* REWARD: the
// combat reward pool is RED-only -- Ironclad.getCardPool calls only
// CardLibrary.addRedCards (Ironclad.java:138-150; CardLibrary.java:1152-1161).
// The sole caller of AbstractDungeon.getColorlessRewardCards() is
// RewardItem(CardColor) (RewardItem.java:155), whose only constructor call site
// is SensoryStone.java:121 -- the Act-3 Sensory Stone event, live since S2.33,
// which is why roll_colorless_card_reward_item below exists. It is an EVENT
// reward row, never a battle-over one: assemble_combat_rewards still never
// emits colorless. (NeowReward's colorless options use its OWN
// getColorlessRewardCards(boolean), NeowReward.java:309 -- a different method
// on a different stream.)
//
// THE TWO KEY ROWS ARE MODELLED FROM S3.11 (they were the first two entries of
// the "deliberately not modelled" list below, and the reasons that justified
// their omission -- no RNG, no relic, an out-of-scope node flag -- were reasons
// the OMISSION was cheap, never reasons it was right):
//   * EMERALD_KEY, appended by MonsterRoomElite.addEmeraldKey (:94-98) from
//     dropReward (:90) AFTER the relic and after Black Star's second relic,
//     under `isFinalActAvailable && !hasEmeraldKey && !rewards.isEmpty() &&
//     getCurrMapNode().hasEmeraldKey`. The node flag was always stored
//     (map_rooms.hpp emerald_x/emerald_y); assemble_combat_rewards now takes it.
//   * SAPPHIRE_KEY, appended by AbstractChest.open (:95-97) ->
//     AbstractRoom.addSapphireKey (:545-547) under `isFinalActAvailable &&
//     !hasSapphireKey`, LINKED both ways to the relic row it follows. Its two
//     claim branches are mutually destructive (RewardItem.java:298-301 vs
//     :317-326) and `removeOneRelicFromRewards` takes the pair (:549-559).
//     The S1 model -- "an ignored linked row that costs no RNG or state
//     parity" -- stayed exactly correct for a run that claims the RELIC, which
//     is why it was safe; that behaviour is now one of the two branches.
//
// Deliberately NOT modelled here, each with the reason:
//   * Prismatic Shard's any-color draw (AbstractDungeon.java:1455) -- Prismatic
//     Shard is a documented deliberate no-op with a live pool slot (see its
//     registry/relics.yaml row);
//     the reward draw uses the red pools regardless of ownership.
//   * N'loth's Gift's changeRareCardRewardChance x3 (NlothsGift.java:27) -- not
//     an S1 registry row (Act-2 event relic); no other relic overrides the
//     rarity-chance hooks, so alterCardRarityProbabilities is an identity pass
//     in S1 (AbstractRoom.java:179-186).

#include <cstdint>
#include <type_traits>

#include "sts/engine/map_rooms.hpp"   // RoomType
#include "sts/engine/rng_stream.hpp"  // RngStream
#include "sts/engine/run_state.hpp"   // RunState
#include "sts/engine/types.hpp"       // RelicId / PotionId / CardId

namespace sts::engine {

// --- Reward-screen storage (transient; lives in RunController, NOT RunState) --
// RunState is the frozen save-parity schema and this task adds no storage to it
// ("no new storage and no schema bump"). The game likewise
// derives the reward screen (AbstractRoom.rewards) rather than saving it; a
// POST_COMBAT save re-rolls the cards from the saved cardRng counter.

inline constexpr int kRewardItemCap = 8;  // S1 max is 6: gold + elite relic +
                                          // Black Star relic + potion + cards +
                                          // Prayer Wheel cards. S3.11's two key
                                          // rows do not raise it: the burning
                                          // elite's worst case is gold + relic +
                                          // Black Star relic + EMERALD_KEY +
                                          // potion + cards == 6 (and the potion
                                          // is then forced off by the >= 4 gate,
                                          // AbstractRoom.java:597-599), and a
                                          // chest's is gold + Matryoshka relic +
                                          // base relic + SAPPHIRE_KEY == 4.
inline constexpr int kRewardCardCap = 4;  // 3 base + Question Card's +1.

enum class RewardItemKind : uint8_t {
    NONE = 0,
    GOLD = 1,         // RewardType.GOLD
    POTION = 2,       // RewardType.POTION
    RELIC = 3,        // RewardType.RELIC (drawn from the pool at assembly)
    CARDS = 4,        // RewardType.CARD (the 3-card pick screen)
    STOLEN_GOLD = 5,  // RewardType.STOLEN_GOLD (a killed thief's return; no
                      // Golden Idol bonus -- the applyGoldBonus theft branch)
    // S3.11. The two key rows. Both claim arms of RewardItem.claimReward return
    // true unconditionally (:317-332), so neither can bounce on capacity.
    EMERALD_KEY = 6,   // RewardType.EMERALD_KEY -- FREE-STANDING. The EMERALD
                       // arm of RewardItem(RewardItem, RewardType) assigns no
                       // relicLink (RewardItem.java:91-95), so the constructor's
                       // linked argument is discarded and nothing else moves
                       // when it is claimed (case 7, :327-332).
    SAPPHIRE_KEY = 7,  // RewardType.SAPPHIRE_KEY -- LINKED, both ways
                       // (RewardItem.java:85-90). See the link invariant below.
};

// The count of RewardItemKind values, INCLUDING NONE. Append-only: a new kind
// takes the next value and bumps this (S3 ledger, "Registry id blocks granted").
inline constexpr int kRewardKindCount = 8;
static_assert(static_cast<int>(RewardItemKind::SAPPHIRE_KEY) + 1 ==
                  kRewardKindCount,
              "kRewardKindCount must name the last RewardItemKind value + 1");

// THE SAPPHIRE LINK IS ADJACENCY, and that is a property of the producer, not a
// shortcut. `AbstractChest.open` calls `addSapphireKey(rewards.get(rewards.size()
// - 1))` (AbstractChest.java:95-97) and `addSapphireKey` immediately appends the
// new row (AbstractRoom.java:545-547), so the key is ALWAYS created directly
// after the row it links to, and nothing in the game ever inserts a row into the
// middle of a reward list -- every producer appends. Removal compacts (the
// engine's remove_item; the Java's `isDone` sweep), which preserves relative
// order, so the pair stays adjacent for as long as both rows live. The Java
// itself relies on exactly this in `removeOneRelicFromRewards`, which tests
// `rewardItem.relicLink != i.next()` -- the *next* element -- rather than
// searching (AbstractRoom.java:549-559). So: a RELIC row is linked iff the row
// after it is SAPPHIRE_KEY, and a SAPPHIRE_KEY row's partner is the row before
// it. No link field is stored, and RunRewardItem keeps its 24-byte layout.

struct RunRewardItem {
    int32_t gold;                            // base amount (GOLD / STOLEN_GOLD)
    int32_t bonus_gold;                      // Golden Idol +25% (GOLD only;
                                             // always 0 on STOLEN_GOLD)
    uint16_t id;                             // RelicId (RELIC) / PotionId (POTION)
    uint16_t card_ids[kRewardCardCap];       // CARDS: the offer
    uint8_t card_upgrades[kRewardCardCap];   // CARDS: offer upgrade counts (0 in
                                             // Act 1 -- chance 0.0f; kept for
                                             // fidelity of the encoding)
    uint8_t kind;                            // RewardItemKind
    uint8_t card_count;                      // CARDS: number offered
};

static_assert(std::is_trivially_copyable_v<RunRewardItem>);
static_assert(sizeof(RunRewardItem) == 24, "keep RunRewardItem tightly packed");

// No CARD item is open on the reward screen.
inline constexpr uint8_t kNoOpenCardReward = 0xFF;

struct RewardScreen {
    RunRewardItem items[kRewardItemCap];
    uint8_t count;
    uint8_t open_card_item;  // index of the CARD item whose pick screen is up,
                             // or kNoOpenCardReward.
    uint8_t pad[2];
};

static_assert(std::is_trivially_copyable_v<RewardScreen>);

// --- Constants (each cited at the top-of-file provenance block) ---------------

inline constexpr int kCardRewardBaseCount = 3;          // AbstractDungeon.java:1425
inline constexpr int kCardBlizzStartOffset = 5;         // AbstractDungeon.java:2773
inline constexpr int kCardBlizzGrowth = 1;              // AbstractDungeon.java:2774
inline constexpr int kCardBlizzMaxOffset = -40;         // AbstractDungeon.java:2775
inline constexpr float kExordiumCardUpgradedChance = 0.0f;  // Exordium.java:107

// AbstractDungeon.cardUpgradedChance -- the ONE per-act reward constant
// (s2-design §2.5). initializeLevelSpecificChances is otherwise byte-identical
// across the three dungeons: the shop/rest/treasure/event/elite quotas, the
// 50/33/17 chest and relic tables and colorlessRareChance 0.3 are all shared
// (Exordium.java:96-108, TheCity.java:70-85, TheBeyond.java:67-82).
//
//   Act 1 (Exordium.java:107)   0.0            -- unconditional
//   Act 2 (TheCity.java:84)     A12+ ? 0.125 : 0.25
//   Act 3 (TheBeyond.java:81)   A12+ ? 0.25  : 0.5
//
// The randomBoolean(chance) draw is consumed for every non-RARE reward card in
// EVERY act, including Act 1 where the chance is 0.0 (trap 2,
// AbstractDungeon.java:1469-1477) -- S1 already had the stream POSITION right,
// so this function only makes the OUTCOME live. That is also what stops A12
// being a no-op from S2 on (its a20.yaml freeze was act-scoped, s2-design §2.5).
//
// Act 4 (TheEnding) is out of S2 scope and deliberately falls through to Act 1's
// 0.0f rather than guessing a value nothing here has read; S3 owns that row.
[[nodiscard]] constexpr float card_upgraded_chance(int act,
                                                   int ascension) noexcept {
    if (act == 2) {
        return ascension >= 12 ? 0.125f : 0.25f;
    }
    // ACT 4 SHARES ACT 3's ROW, and it is the only one of TheEnding's thirteen
    // level-specific chances that is BOTH different from Exordium's and LIVE:
    // `cardUpgradedChance = ascensionLevel >= 12 ? 0.25f : 0.5f`
    // (TheEnding.java:159) is character-for-character TheBeyond's, and its one
    // consumer (AbstractDungeon.java:1470) runs in the Act-4 shop and the Act-4
    // elite's card reward. Falling through to Exordium's 0.0 here would have
    // silently un-upgraded every Act-4 reward card -- s3-design §2.6, §5 trap
    // 10 (the OTHER eleven constants are dead in Act 4 and are frozen dead).
    if (act >= 3) {
        return ascension >= 12 ? 0.25f : 0.5f;
    }
    return kExordiumCardUpgradedChance;
}

inline constexpr int kBaseRareCardChance = 3;           // AbstractRoom.java:108
inline constexpr int kBaseUncommonCardChance = 37;      // AbstractRoom.java:109
inline constexpr int kEliteRareCardChance = 10;         // MonsterRoomElite.java:34
inline constexpr int kEliteUncommonCardChance = 40;     // MonsterRoomElite.java:35

// A SHOP ROOM has its own table, and it is reachable from a reward roll: the
// ShopRoom constructor raises baseRareCardChance to 9 (ShopRoom.java:35-36),
// and rollRarity reads `getCurrRoom().getCardRarity(roll)`
// (AbstractDungeon.java:1597-1603) -- so any getRewardCards() that happens
// WHILE THE PLAYER IS STANDING IN THE MERCHANT'S ROOM rolls against 9/37, not
// 3/37. Orrery and Cauldron are the two producers (their onEquip bodies roll
// reward cards at the purchase site, relic_pickup_shop.cpp); the merchant's own
// stock reads the same numbers through shop.hpp's aliases below.
inline constexpr int kShopRoomRareCardChance = 9;       // ShopRoom.java:35
inline constexpr int kShopRoomUncommonCardChance = 37;  // ShopRoom.java:36

inline constexpr int kBasePotionDropChance = 40;        // AbstractRoom.java:583
inline constexpr int kBlizzardPotionModStep = 10;       // AbstractRoom.java:101

// The rolled reward-card rarity (pool selector). Values match the pity-switch
// semantics; this is NOT a registry enum.
enum class RewardCardRarity : uint8_t { COMMON = 0, UNCOMMON = 1, RARE = 2 };

// AbstractRoom.getCardRarity(roll) for the room kinds that roll reward cards.
// `roll` is the ALREADY-BIASED value cardRng.random(99) +
// cardBlizzRandomizer. Thresholds, not widths (see the provenance block).
//
// The SHOP arm is not a combat-reward room but is reachable all the same:
// rollRarity asks the CURRENT room, and Orrery / Cauldron roll reward cards
// while the player stands in the merchant's (kShopRoomRareCardChance above).
//
// NOTE this constexpr form carries NO relic pass: it is getCardRarity(roll,
// false)'s threshold arithmetic. Every LIVE reward roll goes through the
// rs-aware overload below, which is getCardRarity(roll, true) -- the
// alterCardRarityProbabilities pass (AbstractRoom.java:152-186) whose one
// registered binder is Nloth's Gift (changeRareCardRewardChance -> x3,
// NlothsGift.java:26-29; no relic in scope overrides the uncommon hook).
// The relic-less form stays because dist_check and the boundary tests pin the
// raw thresholds through it.
[[nodiscard]] constexpr RewardCardRarity reward_card_rarity(int roll,
                                                            RoomType room) noexcept {
    if (room == RoomType::Boss) {
        return RewardCardRarity::RARE;  // MonsterRoomBoss.java:40-42
    }
    int rare = kBaseRareCardChance;
    int uncommon = kBaseUncommonCardChance;
    if (room == RoomType::Elite) {
        rare = kEliteRareCardChance;
        uncommon = kEliteUncommonCardChance;
    } else if (room == RoomType::Shop) {
        rare = kShopRoomRareCardChance;
        uncommon = kShopRoomUncommonCardChance;
    }
    if (roll < rare) {
        return RewardCardRarity::RARE;
    }
    if (roll < rare + uncommon) {
        return RewardCardRarity::UNCOMMON;
    }
    return RewardCardRarity::COMMON;
}

// getCardRarity(roll, true): the base thresholds with the relic alternation
// pass applied. rareCardChance is multiplied x3 per held Nloth's Gift (a
// fan-out over the relic LIST in the Java, so a hand-built duplicate would
// stack -- reproduced by multiplying per instance); uncommonCardChance has no
// binder in scope. MonsterRoomBoss overrides getCardRarity wholesale
// (MonsterRoomBoss.java:40-42, RARE without thresholds), so the boss arm never
// sees the pass. Consumers: roll_card_reward_item and The Library's
// twenty-card rollRarity loop (city_events_ii.cpp).
[[nodiscard]] RewardCardRarity reward_card_rarity_with_relics(
    const RunState& rs, int roll, RoomType room) noexcept;

// The battle-over potion drop for an EVENT-ROOM combat that opens NO screen:
// Colosseum's Slavers fight sets rewardAllowed = false (Colosseum.java:55),
// and AbstractRoom's battle-over block STILL runs addPotionToRewards
// (AbstractRoom.java:330-341 -- dropReward and the potion roll sit ahead of
// the rewardAllowed test) before reopen()'s rewards.clear() throws the item
// away. So: one potionRng chance roll at 40 + blizzard mod (the EventRoom arm,
// :590-592; White Beast Statue forces 100, >= `items_assembled` >= 4 forces 0),
// the +/-10 ratchet, and on a hit the returnRandomPotion identity draws --
// value discarded, stream movement and ratchet kept.
void roll_event_potion_drop_unopened(RunState& rs,
                                     uint8_t items_assembled) noexcept;

// --- Gold rolls (trap 18: boss = miscRng, elite/normal = treasureRng) --------

// Boss: 100 + miscRng.random(-5, 5); MathUtils.round(tmp * 0.75f) at A13+
// (AbstractRoom.java:291-296). `misc_rng` is the floor-scoped miscRng.
[[nodiscard]] int roll_boss_gold(RngStream& misc_rng, int ascension) noexcept;

// Elite: treasureRng.random(25, 35) (AbstractRoom.java:316).
[[nodiscard]] int roll_elite_gold(RngStream& treasure_rng) noexcept;

// Plain monster: treasureRng.random(10, 20) (AbstractRoom.java:324).
[[nodiscard]] int roll_normal_gold(RngStream& treasure_rng) noexcept;

// --- Assembly ------------------------------------------------------------------

// Does the run own `id`? (player.hasRelic; acquisition-order scan.)
[[nodiscard]] bool run_has_relic(const RunState& rs, RelicId id) noexcept;

// HOW the combat ended, as the reward layer needs to know it. This is THE
// reward gate: "the pump reported combat over" does not imply a kill, and a
// kill is not the only shape that reaches this screen.
//   KILLED           -- full assembly + the card reward(s). Also the shape of
//                       a MUGGED combat whose other members were killed, and
//                       of a mugged-then-smoked one: the mug screen assembles
//                       items (openCombat -> setupItemReward,
//                       CombatRewardScreen.java:280-285), and the gold/potion
//                       gates read haveMonstersEscaped, not the mug flag.
//   PLAYER_ESCAPED   -- Smoke Bomb: the battle-over block still runs (gold
//                       roll, elite relic tier + pop, the unconditional potion
//                       roll + ratchet -- monsters did NOT escape, so the
//                       plain-monster gold gate passes), but
//                       openCombat(label, true) never calls setupItemReward:
//                       no card roll, nothing claimable.
//   MONSTERS_ESCAPED -- EVERY monster escaped (MonsterGroup.
//                       haveMonstersEscaped, MonsterGroup.java:124-130; in Act
//                       1 exactly the solo Looter's exit): NO plain-monster
//                       gold roll (AbstractRoom.java:319), potion CHANCE zero
//                       while the roll and +/-10 ratchet still run (:585-607)
//                       and White Beast Statue still forces 100 (:594-596) --
//                       and the cards ARE still rolled and offered, because
//                       the mugged openCombat calls setupItemReward. Named for
//                       the gate the Java reads; the mug flag itself picks
//                       only the (unmodelled) banner. NOTE the earlier sketch
//                       here had a STOLEN_GOLD item joining this shape -- the
//                       source says otherwise: die() alone adds STOLEN_GOLD
//                       (Looter.java:170-172), so the return rides a KILLED
//                       thief, never an escaped one.
enum class RewardOutcome : uint8_t {
    KILLED = 1,
    PLAYER_ESCAPED = 2,
    MONSTERS_ESCAPED = 3,
};

// Assemble the post-combat rewards for a combat that ended with `outcome` in
// `room`, consuming the exact stream draws the game consumes when the
// battle-over block runs and the reward screen opens. `misc_rng` is the
// floor-scoped miscRng (boss gold; also the stream relic onEquip bodies
// consume at claim). For PLAYER_ESCAPED, `out.count` is left 0 -- the stream
// movement is the point; a Smoke Bomb is not a stream no-op.
//
// `stolen_gold_return` is a KILLED thief's clamped accrual (what Looter.die()
// fed addStolenGoldToRewards), already deducted from rs.gold by the caller's
// settlement; > 0 adds a STOLEN_GOLD item AHEAD of every battle-over item
// (die() ran during combat, so its item is first in the game's list and
// counts toward the >= 4 potion-suppression threshold). 0 means no dead thief
// stole anything -- die() adds nothing for a zero accrual (Looter.java:170).
//
// `node_has_emerald_key` is `AbstractDungeon.getCurrMapNode().hasEmeraldKey`
// for the node this combat is being fought on -- the burning elite
// setEmeraldElite chose (map_rooms.hpp emerald_x/emerald_y). It is the ONLY
// input addEmeraldKey needs that this layer cannot derive; the other three
// conjuncts of its guard (kFinalActAvailable, `!(rs.keys & kKeyEmerald)`,
// `!rewards.isEmpty()`) are read here. False on every non-elite room.
void assemble_combat_rewards(RunState& rs, RngStream& misc_rng, RoomType room,
                             RewardOutcome outcome, RewardScreen& out,
                             int32_t stolen_gold_return = 0,
                             bool preserve_existing = false,
                             bool node_has_emerald_key = false) noexcept;

// AbstractRoom's event bodies can pre-seed gold/relic rows before enterCombat.
// These doors preserve list order and RewardItem.incrementGold's merge +
// Golden Idol recomputation. Generic battle-over assembly appends potion/card
// rows after them when preserve_existing is true.
[[nodiscard]] bool add_event_combat_gold_reward(const RunState& rs,
                                                RewardScreen& out,
                                                int32_t gold) noexcept;
[[nodiscard]] bool add_event_combat_relic_reward(RewardScreen& out,
                                                 RelicId id) noexcept;

// Dream Catcher uses AbstractDungeon.getRewardCards directly from a RestRoom,
// then opens CardRewardScreen without an outer reward-item screen
// (CampfireSleepEffect.java:71-76). Roll the ordinary RestRoom rarity table,
// including Question Card / Busted Crown count modifiers, cardRng pity,
// no-dupe and upgrade draws, and leave that sole CARD item directly open.
// Returns false only when a malformed stacked modifier list drives the offer
// count to zero.
[[nodiscard]] bool open_rest_card_reward(RunState& rs,
                                         RewardScreen& out) noexcept;

// One pool-indexed reward-card draw: getCard(rarity) ->
// CardGroup.getRandomCard(rng) -> group.get(rng.random(size - 1))
// (AbstractDungeon.java:1500-1517; CardGroup.java:498-500). The RED reward
// pools are the same three lists whichever stream indexes them, which is why
// this takes the stream: combat rewards pass cardRng, Neow's card blessings
// pass NeowEvent.rng (NeowReward.getCard, NeowReward.java:377-391).
[[nodiscard]] CardId draw_card_from_pool(RngStream& rng,
                                         RewardCardRarity rarity) noexcept;

// Its COLORLESS sibling: getColorlessCardFromPool(rarity)
// (AbstractDungeon.java:1579-1595) -> CardGroup.getRandomCard(true, rarity)
// (CardGroup.java:509-524), which filters the colorless pool to `rarity`,
// SORTS the filtered view (AbstractCard.compareTo == cardID compare) and
// indexes it. UNCOMMON and RARE are the only poolable colourless rarities, so
// the two generated views cover the whole domain.
//
// UNLIKE draw_card_from_pool the stream is not really the caller's choice: the
// Java hard-codes `AbstractDungeon.cardRng` inside getRandomCard(boolean,
// rarity), so BOTH S1 callers -- Neow's colourless blessings
// (NeowReward.java:309-330, whose RARITY roll is on NeowEvent.rng) and the
// shop's two colourless slots (Merchant.java:84-85) -- pass cardRng. It stays
// a parameter so the draw is visible at the call site rather than reaching
// into a RunState.
[[nodiscard]] CardId draw_colorless_card_from_pool(
    RngStream& card_rng, RewardCardRarity rarity) noexcept;

// Exordium.java:106 == TheCity.java:83 == TheBeyond.java:80 -- the
// initializeLevelSpecificChances trio assigns the same value in every act.
inline constexpr float kColorlessRareChance = 0.3f;

// AbstractDungeon.getColorlessRewardCards (AbstractDungeon.java:1381-1421),
// ONE RewardItem(CardColor.COLORLESS) row (RewardItem.java:152-158) -- i.e.
// one Sensory Stone memory's card reward, rolled AT CONSTRUCTION exactly as
// the Java rolls the cards in the RewardItem constructor. Per card:
//   * rollRareOrUncommon(colorlessRareChance) (:1621-1626) -- ONE
//     cardRng.randomBoolean(0.3f); there is NO random(99) + pity read here,
//     unlike rollRarity.
//   * a RARE result writes cardBlizzRandomizer = cardBlizzStartOffset
//     (:1396) -- the colourless roll does not READ the red pity counter but a
//     rare DOES RESET it, which shifts every later red reward rarity roll.
//   * getColorlessCardFromPool via draw_colorless_card_from_pool above; the
//     no-dupe loop (:1407-1412) redraws FROM THE SAME rarity, one cardRng
//     draw per attempt, compared against everything already in this item.
//   * NO upgrade pass: getColorlessRewardCards has no cardUpgradedChance
//     block, so the ROLL never upgrades a colourless offer in any act.
//   * ...but the OFFER PREVIEW still runs. RewardItem(CardColor) does not
//     merely call the generator: its body (RewardItem.java:152-162) then walks
//     every card of the item through every owned relic's onPreviewObtainCard,
//     and it does so for the COLORLESS arm too. A Molten/Toxic/Frozen Egg
//     owner's Sensory Stone therefore offers its matching-type memory card
//     already upgraded. Modelled by apply_offer_previews (combat_rewards.cpp);
//     it consumes no RNG.
// numCards = 3 through every changeNumberOfCardsInReward (:1383-1386):
// Question Card +1, Busted Crown -2, same overrides as the red roller.
// Adds nothing when the relic-modified count is <= 0.
void roll_colorless_card_reward_item(RunState& rs, RewardScreen& s) noexcept;

// ONE `new RewardItem()` card row (RewardItem.java:145-150 -> a full
// getRewardCards() roll), appended when the offer is non-empty. The Java spells
// this in two places and they are the same code:
//
//   * CombatRewardScreen.setupItemReward's UNCONDITIONAL row
//     (CombatRewardScreen.java:72-96) -- every open() from a room that is not a
//     TreasureRoom, not a RestRoom and whose event does not set
//     noCardsInRewards appends one. Neow's three-potion blessing opens that
//     screen from the NeowRoom (whose NeowEvent leaves noCardsInRewards at its
//     false default, AbstractEvent.java:63), so the roll HAPPENS -- cardRng
//     draws and the pity counter move -- and the row is then explicitly deleted
//     again (NeowReward.java:273-283). Cauldron does the same at a shop
//     (Cauldron.java:36-44). Skipping the roll would desync cardRng for the
//     rest of the run.
//   * AbstractRoom.addCardToRewards (AbstractRoom.java:573-578) -- the same
//     `new RewardItem()` + non-empty guard, called four times by Orrery's
//     onEquip (Orrery.java:28-30) before its screen opens.
//
// `room` is the room the roll is made IN, which is what rollRarity's threshold
// table keys on (reward_card_rarity) -- RoomType::Shop for the two relic
// bodies, kNeowRewardScreenRoom for the blessing.
void roll_setup_item_card_reward(RunState& rs, RoomType room,
                                 RewardScreen& s) noexcept;

// Delete the first CARDS row, preserving the order of the rest -- the loop at
// NeowReward.java:275-283. Returns false when the screen holds no CARDS row
// (the Java's `remove == -1` break).
[[nodiscard]] bool remove_first_card_reward_item(RewardScreen& s) noexcept;

// AbstractRoom.addSapphireKey (AbstractRoom.java:545-547): append the linked key
// row after `s.items[s.count - 1]`, which AbstractChest.open has just made the
// base relic. Returns false (nothing appended) when the screen is empty or full;
// the caller's chest-open transaction treats that as an abort, exactly as it
// does a failed relic push. The link itself is adjacency -- see the invariant
// beside RewardItemKind.
[[nodiscard]] bool add_sapphire_key_reward(RewardScreen& s) noexcept;

// Is row `index` a RELIC whose linked SAPPHIRE_KEY row sits immediately after
// it? (`RewardItem.relicLink != null` on a RELIC row, RewardItem.java:298.)
[[nodiscard]] bool reward_relic_row_is_key_linked(const RewardScreen& s,
                                                  uint8_t index) noexcept;

// --- Claim -----------------------------------------------------------------

// Legality of claiming item `index` right now (RewardItem.claimReward's
// failure cases plus the engine's explicit fixed capacities): a POTION needs a
// free slot below rs.potion_slots unless Sozu is owned (Sozu
// claims-and-discards, RewardItem.java:276-279), and a RELIC must fit through
// acquire_relic (including Circlet's stack-at-full exception).
[[nodiscard]] bool reward_claim_legal(const RunState& rs,
                                      const RewardScreen& s,
                                      uint8_t index) noexcept;

struct RelicEquipContext;  // relic_pools.hpp (which fwd-declares RewardScreen
                           // from THIS header; the reference param below keeps
                           // the two headers acyclic).

// Claim item `index`. GOLD/POTION/RELIC apply immediately and remove the item;
// CARDS opens the pick sub-screen (s.open_card_item = index) and removes
// nothing. Returns false (no mutation) on an illegal claim.
//
// A RELIC row whose id carries an `on_equip_screen` body goes through the
// plain 3-argument acquire_relic in the first overload and is therefore
// REFUSED (NEEDS_EQUIP_CONTEXT: the item stays on the screen, nothing
// mutated). Every run-layer claim dispatch uses the second overload, which
// passes the RelicEquipContext through acquire_relic so such a relic CAN be
// claimed -- the game's obtain runs onEquip at the claim site whatever the
// room (AbstractRelic.instantObtain/obtain) -- and the body's screen request
// (in S1 only the Bottled trio's GRID_BOTTLE; the five boss on_equip_screen
// relics are BOSS-tier and never sit on an S1 claim screen) is read back by
// the caller from equip_ctx.screen.
[[nodiscard]] bool claim_reward(RunState& rs, RngStream& misc_rng,
                                RewardScreen& s, uint8_t index) noexcept;
[[nodiscard]] bool claim_reward(RunState& rs, RngStream& misc_rng,
                                RewardScreen& s, uint8_t index,
                                RelicEquipContext& equip_ctx) noexcept;

// Whether open_card_item names an in-bounds CARDS item on a structurally valid
// reward screen. This is the shared authority for card-pick masks and
// take/skip/Singing Bowl mutation paths.
[[nodiscard]] bool reward_card_item_open_legal(
    const RewardScreen& s) noexcept;

// Whether card `card_index` of the OPEN card item can currently pass through
// add_card_to_master_deck. Used by both ordinary combat rewards and Dream
// Catcher's direct screen so a fixed-cap failure is never advertised as legal.
[[nodiscard]] bool reward_take_card_legal(const RunState& rs,
                                          const RewardScreen& s,
                                          uint8_t card_index) noexcept;

// Take card `card_index` of the OPEN card item into the master deck through
// add_card_to_master_deck (the onObtainCard door -- Ceramic Fish, the eggs,
// Darkstone Periapt fire there), then remove the item and close the screen.
// Returns false (no mutation) when reward_take_card_legal is false.
[[nodiscard]] bool reward_take_card(RunState& rs, RewardScreen& s,
                                    uint8_t card_index) noexcept;

// Singing Bowl: +2 max HP (and the increaseMaxHp companion heal of 2,
// AbstractCreature.java:199-209) INSTEAD of a card; removes the open card item.
// Returns false when no card item is open or the bowl is not owned.
[[nodiscard]] bool reward_sing(RunState& rs, RewardScreen& s) noexcept;

// Skip button: close the pick screen; the card item STAYS claimable
// (CardRewardScreen skip does not call takeReward).
void reward_skip_card(RewardScreen& s) noexcept;

}  // namespace sts::engine
