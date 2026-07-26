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
//
// COLORLESS IS UNREACHABLE FROM A COMBAT REWARD: the
// combat reward pool is RED-only -- Ironclad.getCardPool calls only
// CardLibrary.addRedCards (Ironclad.java:138-150; CardLibrary.java:1152-1161),
// and the sole caller of AbstractDungeon.getColorlessRewardCards() is
// RewardItem(CardColor) (RewardItem.java:155), whose only constructor call site
// is SensoryStone.java:121 -- an Act-3 event. (NeowReward's colorless options
// use its OWN getColorlessRewardCards(boolean), NeowReward.java:309 -- a
// different method on a different stream.) Colorless reaches an S1 player only
// through the shop's two colorless slots and Neow.
//
// Deliberately NOT modelled here, each with the reason:
//   * STOLEN_GOLD / the mugged screen (Looter) -- blocked with the parked
//     Looter remainder (the liveness predicate); the reward side gains a
//     RewardItemKind value when the Looter lands.
//   * The EMERALD_KEY reward item -- the emerald-elite node flag itself is out
//     of S1 scope (map_rooms.hpp's setEmeraldElite note: the mapRng DRAW is
//     modelled, the chosen node's key flag is not stored), so the reward layer
//     cannot know which elite carries it. Follows that documented scoping.
//   * SAPPHIRE_KEY (chest-linked) -- S2 (design §1.1).
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
                                          // Prayer Wheel cards.
inline constexpr int kRewardCardCap = 4;  // 3 base + Question Card's +1.

enum class RewardItemKind : uint8_t {
    NONE = 0,
    GOLD = 1,    // RewardType.GOLD
    POTION = 2,  // RewardType.POTION
    RELIC = 3,   // RewardType.RELIC (drawn from the pool at assembly)
    CARDS = 4,   // RewardType.CARD (the 3-card pick screen)
};

struct RunRewardItem {
    int32_t gold;                            // base amount (GOLD)
    int32_t bonus_gold;                      // Golden Idol +25% (GOLD)
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

inline constexpr int kBaseRareCardChance = 3;           // AbstractRoom.java:108
inline constexpr int kBaseUncommonCardChance = 37;      // AbstractRoom.java:109
inline constexpr int kEliteRareCardChance = 10;         // MonsterRoomElite.java:34
inline constexpr int kEliteUncommonCardChance = 40;     // MonsterRoomElite.java:35

inline constexpr int kBasePotionDropChance = 40;        // AbstractRoom.java:583
inline constexpr int kBlizzardPotionModStep = 10;       // AbstractRoom.java:101

// The rolled reward-card rarity (pool selector). Values match the pity-switch
// semantics; this is NOT a registry enum.
enum class RewardCardRarity : uint8_t { COMMON = 0, UNCOMMON = 1, RARE = 2 };

// AbstractRoom.getCardRarity(roll) for the three room kinds that give combat
// rewards. `roll` is the ALREADY-BIASED value cardRng.random(99) +
// cardBlizzRandomizer. Thresholds, not widths (see the provenance block).
[[nodiscard]] constexpr RewardCardRarity reward_card_rarity(int roll,
                                                            RoomType room) noexcept {
    if (room == RoomType::Boss) {
        return RewardCardRarity::RARE;  // MonsterRoomBoss.java:40-42
    }
    const int rare = room == RoomType::Elite ? kEliteRareCardChance
                                             : kBaseRareCardChance;
    const int uncommon = room == RoomType::Elite ? kEliteUncommonCardChance
                                                 : kBaseUncommonCardChance;
    if (roll < rare) {
        return RewardCardRarity::RARE;
    }
    if (roll < rare + uncommon) {
        return RewardCardRarity::UNCOMMON;
    }
    return RewardCardRarity::COMMON;
}

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
// kill is not the only shape that reaches this screen. The three battle-over
// shapes the game distinguishes (AbstractRoom.update:334-341):
//   KILLED         -- every monster dead: full assembly + the card reward(s).
//   PLAYER_ESCAPED -- Smoke Bomb: the battle-over block still runs (gold roll,
//                     elite relic tier + pop, the unconditional potion roll +
//                     ratchet -- monsters did NOT escape, so the plain-monster
//                     gold gate passes), but openCombat(label, true) never
//                     calls setupItemReward: no card roll, nothing claimable.
//   (MUGGED)       -- the Looter left with the gold. NOT a value yet: it lands
//                     with the Looter itself (the escape liveness predicate).
//                     When it does, it is a THIRD case here, not a KILLED
//                     variant: haveMonstersEscaped() suppresses the plain-
//                     monster gold roll and zeroes the potion CHANCE (the roll
//                     and ratchet still happen), setupItemReward still rolls
//                     the cards, and a STOLEN_GOLD item joins the screen.
enum class RewardOutcome : uint8_t {
    KILLED = 1,
    PLAYER_ESCAPED = 2,
};

// Assemble the post-combat rewards for a combat that ended with `outcome` in
// `room`, consuming the exact stream draws the game consumes when the
// battle-over block runs and the reward screen opens. `misc_rng` is the
// floor-scoped miscRng (boss gold; also the stream relic onEquip bodies
// consume at claim). For PLAYER_ESCAPED, `out.count` is left 0 -- the stream
// movement is the point; a Smoke Bomb is not a stream no-op.
void assemble_combat_rewards(RunState& rs, RngStream& misc_rng, RoomType room,
                             RewardOutcome outcome, RewardScreen& out) noexcept;

// --- Claim -----------------------------------------------------------------

// Legality of claiming item `index` right now (RewardItem.claimReward's
// failure cases): a POTION needs a free slot below rs.potion_slots unless Sozu
// is owned (Sozu claims-and-discards, RewardItem.java:276-279); everything
// else is claimable while it exists.
[[nodiscard]] bool reward_claim_legal(const RunState& rs,
                                      const RewardScreen& s,
                                      uint8_t index) noexcept;

// Claim item `index`. GOLD/POTION/RELIC apply immediately and remove the item;
// CARDS opens the pick sub-screen (s.open_card_item = index) and removes
// nothing. Returns false (no mutation) on an illegal claim.
[[nodiscard]] bool claim_reward(RunState& rs, RngStream& misc_rng,
                                RewardScreen& s, uint8_t index) noexcept;

// Take card `card_index` of the OPEN card item into the master deck through
// add_card_to_master_deck (the onObtainCard door -- Ceramic Fish, the eggs,
// Darkstone Periapt fire there), then remove the item and close the screen.
// Returns false (no mutation) when no card item is open or the index is bad.
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
