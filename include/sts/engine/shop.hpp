#pragma once

// The merchant: stock generation, pricing, purchases and the card-removal
// (purge) service.
//
// A shop is built ONCE, on room entry, and the whole build is a single
// uninterrupted RNG sequence across three streams. Nothing about it is a
// player decision, so `generate_shop` is one call and the CHOOSE flow above it
// spends only gold.
//
// THE DRAW ORDER, which is the thing this module exists to get right
// (ShopRoom.onPlayerEntry -> new Merchant() -> ShopScreen.init):
//
//   Merchant.<init>            Merchant.java:59-85     cardRng
//     1  colored[0]  rollRarity + getCardFromPool(ATTACK)
//     2  colored[1]  same, re-drawn while the id repeats colored[0]
//     3  colored[2]  rollRarity + getCardFromPool(SKILL)
//     4  colored[3]  same, re-drawn while the id repeats colored[2]
//     5  colored[4]  rollRarity + getCardFromPool(POWER)
//     6  colorless[0] getColorlessCardFromPool(UNCOMMON)
//     7  colorless[1] getColorlessCardFromPool(RARE)
//   ShopScreen.initCards       ShopScreen.java:246-276 merchantRng
//     8  five colored prices, in slot order
//     9  two colorless prices, in slot order
//    10  the sale index, random(0, 4) -- ALWAYS a COLORED slot
//   ShopScreen.initRelics      ShopScreen.java:360-372 merchantRng (+ relic pools)
//    11  slot 0: rollRelicTier(), END-pop that tier, price jitter
//    12  slot 1: rollRelicTier(), END-pop that tier, price jitter
//    13  slot 2: NO tier roll -- always the SHOP tier -- END-pop, price jitter
//   ShopScreen.initPotions     ShopScreen.java:374-384 potionRng + merchantRng
//    14  three returnRandomPotion() identities, each followed by a price jitter
//
// A fresh shop therefore consumes EXACTLY 16 merchantRng draws
// (5 + 2 + 1 + 2 tier rolls + 3 relic jitters + 3 potion jitters), 12 cardRng
// draws when neither dedupe loop re-rolls and the POWER slot needs no extra
// attempt, and 3 + N potionRng draws (one tier roll each plus trap-14
// rejection sampling).
//
// Pricing is applied in a fixed order and every stage rounds, so the stages do
// not commute:
//     card    (int)(base * merchantRng.random(0.9f, 1.1f))   -- TRUNCATION
//     colorless                          * 1.2f before the truncation
//     sale card                          price /= 2          -- INTEGER divide
//     relic / potion  MathUtils.round(base * merchantRng.random(0.95f, 1.05f))
//     then applyDiscount, in ShopScreen.init's order (ShopScreen.java:227-238):
//         A16       x1.1  (does NOT touch the purge cost)
//         Courier   x0.8  (DOES)
//         Membership x0.5 (DOES)
//         Smiling Mask: purge cost pinned to 50, overriding both
// applyDiscount walks relics, potions, colored, colorless in that order and
// MathUtils.rounds each price in place, so a discounted price is a round of an
// already-rounded number, not of the base.
//
// Provenance (read in full from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   ShopRoom.onPlayerEntry / updatePurge      ShopRoom.java:43-77
//   Merchant.<init>                           Merchant.java:57-97
//   ShopScreen.init / resetPurgeCost          ShopScreen.java:130-244
//   ShopScreen.initCards                      ShopScreen.java:246-276
//   ShopScreen.purgeCard / updatePurge        ShopScreen.java:278-305
//   ShopScreen.applyDiscount                  ShopScreen.java:340-358
//   ShopScreen.initRelics / initPotions       ShopScreen.java:360-384
//   ShopScreen.getNewPrice / rollRelicTier    ShopScreen.java:386-428
//   ShopScreen.purchaseCard / setPrice        ShopScreen.java:592-672
//   ShopScreen.purchasePurge                  ShopScreen.java:969-978
//   StoreRelic.<init> / purchaseRelic         StoreRelic.java:36-120
//   StorePotion.<init> / purchasePotion       StorePotion.java:33-101
//   AbstractCard.getPrice                     AbstractCard.java:1915-1937
//   AbstractRelic.getPrice                    AbstractRelic.java:173-201
//   AbstractPotion.getPrice                   AbstractPotion.java:381-394
//   AbstractDungeon.getCardFromPool           AbstractDungeon.java:1538-1577
//   AbstractDungeon.getColorlessCardFromPool  AbstractDungeon.java:1579-1595
//   AbstractDungeon.rollRarity                AbstractDungeon.java:1597-1620
//   AbstractRoom.getCardRarity                AbstractRoom.java:148-178
//   AbstractPlayer.loseGold                   AbstractPlayer.java:697-717
//   MealTicket.justEnteredRoom                MealTicket.java:31-38
//   MawBank.onSpendGold / setCounter          MawBank.java:38-53
//   SmilingMask.onEnterRoom / canSpawn        SmilingMask.java:31-38, :41-43
//   Courier.onEnterRoom / canSpawn            Courier.java:31-38, :41-43
//   MembershipCard MULTIPLIER + onEnterRoom   MembershipCard.java:17-36
// SmilingMask and Courier are byte-identical to each other in both bodies, and
// their canSpawn is byte-identical to OldCoin.java:37-39 as well (the shared
// "not in a shop, floorNum <= 48 unless endless" shop-relic gate).

#include <cstdint>
#include <type_traits>

#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"  // RewardCardRarity (shared rarity enum)
#include "sts/engine/potions.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/relics.hpp"
#include "sts/engine/run_state.hpp"

namespace sts::engine {

// --- Stock shape --------------------------------------------------------------

inline constexpr int kShopColoredCount = 5;    // Merchant.cards1
inline constexpr int kShopColorlessCount = 2;  // Merchant.cards2
inline constexpr int kShopRelicCount = 3;      // ShopScreen.initRelics
inline constexpr int kShopPotionCount = 3;     // ShopScreen.initPotions

// One purchasable row. `id` is a CardId / RelicId / PotionId depending on which
// array holds it; `price` is the FINAL post-discount price the player pays.
//
// SLOTS ARE FIXED, not compacted. The game removes a bought card from its
// ArrayList and a bought relic/potion from its list, so the game's indices
// shift; nothing observable depends on those indices (no RNG and no relic hook
// reads a shop list position -- StoreRelic.slot is a render x-offset), so a
// `sold` flag keeps the CHOOSE arg0 for a given row stable for the whole visit,
// which a shifting index could not. The sale tag likewise stays attached to its
// slot rather than to a list position.
struct ShopSlot {
    uint16_t id;
    int16_t price;
    uint8_t sold;
    // Card slots only: the eggs' onPreviewObtainCard already upgraded the
    // DISPLAYED card as the shop was built (ShopScreen.java:258-260, :268-270;
    // MoltenEgg2.java:40-43 forwards the preview straight to onObtainCard), so
    // the instance the player buys is the upgraded one. Always 0 for relics and
    // potions.
    uint8_t upgrade;
    uint8_t pad[2];
};

static_assert(std::is_trivially_copyable_v<ShopSlot>);
static_assert(sizeof(ShopSlot) == 8);

// --- The Courier's restock -----------------------------------------------------
//
// With The Courier owned, a purchase does not retire its slot: the merchant
// RESTOCKS it (ShopScreen.purchaseCard :598-643, StoreRelic.purchaseRelic
// :105-112, StorePotion.purchasePotion :86-89). Every restock half that the
// game seeds is modelled faithfully; the ONE unseeded value gets a named,
// permanent refusal:
//
//   colored card   rollRarity() is ONE cardRng.random(99) through the
//                  ShopRoom's 9/37 table (the purchase still happens inside
//                  the ShopRoom), then getCardFromPool(rolled, type, false)
//                  -- useRng=FALSE, so the IDENTITY is drawn from libGDX's
//                  unseeded MathUtils.random (ShopScreen.java:615-617) and
//                  has no reproducible answer. The slot therefore restocks
//                  as kShopRestockedUnknownCard: its RARITY (and so its
//                  price) is still seeded -- the pool the draw indexes is a
//                  pure function of (rolled rarity, slot type) -- but the
//                  slot is kept OFF the legal-action mask and
//                  shop_buy_card refuses it whole (the
//                  potion_use_implemented precedent: fail-loud refusal of
//                  the one thing the sim cannot answer, everything around
//                  it exact). The `while (c.color == COLORLESS)` re-roll
//                  guard is dead for the same reason it is dead at shop
//                  init: the three RED rarity pools hold no colourless row.
//   colorless card fully seeded: ONE merchantRng.random() float against
//                  colorlessRareChance (0.3f, Exordium.java:106) picks
//                  UNCOMMON/RARE, getColorlessCardFromPool spends ONE
//                  cardRng draw on the sorted rarity view, and the slot
//                  restocks purchasable.
//   relic          fully seeded: rollRelicTier (ONE merchantRng.random(99))
//                  then returnRandomRelicEnd's END-pop with its in-shop
//                  canSpawn reroute. StoreRelic's own instanceof rejection
//                  loop (Old Coin / Smiling Mask / Maw Bank / The Courier,
//                  StoreRelic.java:106-108) is DEAD code here: all four AND
//                  their canSpawn with `!(getCurrRoom() instanceof
//                  ShopRoom)`, so returnEndRandomRelicKey's own canSpawn
//                  recursion (AbstractDungeon.java:751-753) has already
//                  rerouted past them and the loop condition can never hold.
//   potion         fully seeded: returnRandomPotion() on potionRng (tier
//                  roll + trap-14 rejection sampling), restocked
//                  purchasable.
//
// RESTOCK PRICES ARE NOT INIT PRICES. A restocked card goes through setPrice
// (ShopScreen.java:660-673): one float product of base x merchantRng.random
// (0.9f, 1.1f) x [1.2f colourless] x [0.8f Courier] x [0.5f Membership],
// truncated ONCE -- no sale halving and NO A16 x1.1 (applyDiscount never
// re-runs), so an A20 restock is systematically cheaper than the shelf it
// replaces. A restocked relic/potion goes through getNewPrice (:386-411):
// MathUtils.round(base x merchantRng.random(0.95f, 1.05f)), then a SEPARATE
// round per owned discount relic (0.8f then 0.5f) -- again no A16 pass.
//
// The restocked-slot refusal is this simulator's one permanent, named
// deviation for the merchant: the slot EXISTS (the game's shelf never
// empties while The Courier is owned) but purchasing the unknowable colored
// replacement is refused fail-loud rather than answered with an invented
// card. The capture campaign wave2cap_courier_* measured the seeded stream
// motion this file models (see the Courier rows in
// tools/oracle_bridge/driver/wave2cap_capture_runbook.md).
inline constexpr uint16_t kShopRestockedUnknownCard = 0xFFFF;
// kColorlessRareChance (Exordium.java:106) moved to combat_rewards.hpp
// (included above) when Sensory Stone's colourless reward roll became its
// second consumer (S2.33) -- one definition, one citation.

enum class ShopScreenKind : uint8_t {
    NONE = 0,
    MENU = 1,        // the shop floor: buy anything, or open the purge grid.
    PURGE_GRID = 2,  // gridSelectScreen over the purgeable master deck.
};

// The live shop. Transient screen/room state exactly like RewardScreen and
// TreasureChest: the game rebuilds a shop from (seed, merchantRng.counter) on
// reload, so none of this belongs in the save-parity RunState -- with the one
// exception the game itself persists, ShopScreen.purgeCost, which is already a
// RunState field (RunState.purge_cost).
struct ShopState {
    ShopSlot colored[kShopColoredCount];
    ShopSlot colorless[kShopColorlessCount];
    ShopSlot relics[kShopRelicCount];
    ShopSlot potions[kShopPotionCount];

    uint8_t sale_index;         // merchantRng.random(0, 4); indexes `colored`.
    uint8_t screen;             // ShopScreenKind.
    uint8_t purge_available;    // ShopScreen.purgeAvailable (0/1).
    uint8_t pad;
    int16_t actual_purge_cost;  // ShopScreen.actualPurgeCost for THIS visit.
    int16_t pad2;
};

static_assert(std::is_trivially_copyable_v<ShopState>);

// --- Base prices ---------------------------------------------------------------
//
// AbstractCard.getPrice (AbstractCard.java:1915-1937) and its two siblings are
// switches over the game's nested enums. Those enums are the ONE thing the
// decompiled tree cannot show: sts-classes.jar carries no inner classes, so CFR
// emitted `$SwitchMap[...]` indices with no constant names and the
// index -> tier mapping is not recoverable from the source alone. What IS
// recoverable is each switch's VALUE multiset, and a captured A20 shop pins the
// assignment for every tier a shop can offer -- see the shop_capture test,
// which reproduces a real merchant (Blood Vial 150 COMMON, Question Card 250
// UNCOMMON, Medical Kit 150 SHOP; Iron Wave 50 COMMON, Rage 75 UNCOMMON, Secret
// Weapon 150 RARE; Strength Potion 50 COMMON, Duplication Potion 75 UNCOMMON)
// price-for-price. RARE relic 300 and BOSS relic 300 are the switch's remaining
// pair and are interchangeable there, so the shop's answer is the same either
// way; only BOSS is unreachable from a merchant at all.

[[nodiscard]] constexpr int card_base_price(RewardCardRarity rarity) noexcept {
    switch (rarity) {
        case RewardCardRarity::RARE:
            return 150;
        case RewardCardRarity::UNCOMMON:
            return 75;
        case RewardCardRarity::COMMON:
        default:
            return 50;
    }
}

[[nodiscard]] constexpr int relic_base_price(RelicTier tier) noexcept {
    switch (tier) {
        case RelicTier::UNCOMMON:
            return 250;
        case RelicTier::RARE:
        case RelicTier::BOSS:
            return 300;
        case RelicTier::COMMON:
        case RelicTier::SHOP:
        default:
            return 150;
    }
}

[[nodiscard]] constexpr int potion_base_price(PotionRarity rarity) noexcept {
    switch (rarity) {
        case PotionRarity::RARE:
            return 100;
        case PotionRarity::UNCOMMON:
            return 75;
        case PotionRarity::COMMON:
        default:
            return 50;
    }
}

// --- Purge cost ----------------------------------------------------------------

inline constexpr int kPurgeCostBase = 75;  // ShopScreen.purgeCost initial value.
inline constexpr int kPurgeCostRamp = 25;  // ShopScreen.PURGE_COST_RAMP.
inline constexpr int kSmilingMaskPurgeCost = 50;

// The purge cost this VISIT charges, from the run-persistent
// RunState.purge_cost. There are deliberately TWO of these because the game
// computes it in two places that do not agree:
//
//   at init      ShopScreen.init :226-238 runs applyDiscount once per owned
//                shop relic and each call recomputes actualPurgeCost from
//                `purgeCost`, so with both The Courier and the Membership Card
//                the 0.8 is OVERWRITTEN by the 0.5 rather than compounding.
//                The A16 x1.1 passes affectPurge=false, so ascension never
//                moves the purge cost at all.
//   after purge  ShopScreen.purgeCard :281-291 spells the both-owned case out
//                as a single round of `purgeCost * 0.8f * 0.5f`.
//
// Smiling Mask pins 50 in both, overriding everything else.
[[nodiscard]] int shop_purge_cost_at_init(const RunState& rs,
                                          int purge_cost) noexcept;
[[nodiscard]] int shop_purge_cost_after_purge(const RunState& rs,
                                              int purge_cost) noexcept;

// --- Stock generation ----------------------------------------------------------

// ShopRoom.onPlayerEntry's whole merchant build. Consumes cardRng, merchantRng
// and potionRng in the order documented at the top of this file, END-pops the
// relic pools (trap 15) and applies every price stage. `rs` is mutated only
// through its streams, its relic pools, and nothing else -- the returned
// ShopState is the entire visible result.
[[nodiscard]] ShopState generate_shop(RunState& rs) noexcept;

// AbstractDungeon.getCardFromPool(rarity, type, true) (:1538-1577) collapsed to
// its state-visible result: one cardRng draw into the sorted type-filtered view
// of the rarity pool, with the POWER-only recursion to the next rarity up when
// that view is empty (no draw is spent on the empty pool). Exposed so the
// draw-order test can pin each slot independently of the merchant.
//
// `drawn_rarity` reports the pool the card actually came from, which is NOT
// always the requested one -- the Ironclad's POWER slot asks at COMMON and is
// answered from UNCOMMON. It is the card's own rarity, and therefore the one
// AbstractCard.getPrice is asked for (ShopScreen.java:253 reads
// `coloredCards.get(i).rarity`, not the roll).
[[nodiscard]] CardId shop_card_from_pool(
    RngStream& card_rng, RewardCardRarity rarity, CardType type,
    RewardCardRarity* drawn_rarity = nullptr) noexcept;

// ShopScreen.rollRelicTier (:418-428): merchantRng.random(99) against 48 / 82.
// Pure over the roll so all 100 outcomes can be pinned without seed search.
[[nodiscard]] constexpr RelicTier shop_relic_tier_for_roll(int32_t roll) noexcept {
    return roll < 48 ? RelicTier::COMMON
                     : (roll < 82 ? RelicTier::UNCOMMON : RelicTier::RARE);
}

// AbstractRoom.getCardRarity(roll, false) as a ShopRoom overrides it
// (ShopRoom.java:52-55 forwards with useAlternation=false, over the ShopRoom
// constructor's baseRareCardChance = 9 / baseUncommonCardChance = 37 --
// ShopRoom.java:35-36). useAlternation=false is why no relic can move a shop
// card's rarity, unlike a combat reward's.
inline constexpr int kShopRareCardChance = 9;
inline constexpr int kShopUncommonCardChance = 37;

[[nodiscard]] constexpr RewardCardRarity shop_card_rarity_for_roll(
    int roll) noexcept {
    if (roll < kShopRareCardChance) {
        return RewardCardRarity::RARE;
    }
    if (roll < kShopRareCardChance + kShopUncommonCardChance) {
        return RewardCardRarity::UNCOMMON;
    }
    return RewardCardRarity::COMMON;
}

// --- Purchases -----------------------------------------------------------------
//
// Every one of these is a TRANSACTION: a false return means the purchase was
// not legal and BOTH arguments are byte-stable. Gold leaves through
// lose_gold(..., /*in_shop=*/true) (relics/relic_pickup.hpp), which is where
// the AbstractPlayer.loseGold onSpendGold fan-out lives -- Maw Bank is its only
// S1 consumer and it is used up by the first coin spent in any shop.

[[nodiscard]] bool shop_buy_card_legal(const RunState& rs, const ShopState& shop,
                                       uint8_t index, bool colorless) noexcept;
[[nodiscard]] bool shop_buy_card(RunState& rs, ShopState& shop, uint8_t index,
                                 bool colorless) noexcept;

[[nodiscard]] bool shop_buy_relic_legal(const RunState& rs,
                                        const ShopState& shop,
                                        uint8_t index) noexcept;
// The plain overload REFUSES (false, nothing mutated -- no gold spent, slot
// unsold) a relic id with an `on_equip_screen` body: the shop CAN stock a
// Bottled trio relic (uncommon-tier stock draws see them), and its onEquip
// opens the bottle grid at the purchase site (StoreRelic.purchaseRelic ->
// instantObtain -> onEquip). The run-layer purchase dispatch uses the
// RelicEquipContext overload and reads the grid request back from
// equip_ctx.screen (the pending-bottle overlay, run_advance.hpp).
[[nodiscard]] bool shop_buy_relic(RunState& rs, RngStream& misc_rng,
                                  ShopState& shop, uint8_t index) noexcept;
[[nodiscard]] bool shop_buy_relic(RunState& rs, RngStream& misc_rng,
                                  ShopState& shop, uint8_t index,
                                  RelicEquipContext& equip_ctx) noexcept;

[[nodiscard]] bool shop_buy_potion_legal(const RunState& rs,
                                         const ShopState& shop,
                                         uint8_t index) noexcept;
[[nodiscard]] bool shop_buy_potion(RunState& rs, ShopState& shop,
                                   uint8_t index) noexcept;

// StorePotion.purchasePotion's slot search (:80 -> AbstractPlayer.obtainPotion):
// the first EMPTY slot below rs.potion_slots. Sozu refuses the purchase before
// the gold check (:75-78), so a Sozu owner can never buy a potion.
[[nodiscard]] bool shop_potion_slot_available(const RunState& rs) noexcept;

// ShopScreen.purchasePurge (:969-978): opens the grid when the player can
// afford it. The purge itself is ShopScreen.purgeCard (:278-292) folded
// together with ShopRoom.updatePurge (:66-77) -- pay, ramp RunState.purge_cost
// by 25, recompute this visit's cost, remove the card, and retire the service
// for the rest of the visit (purgeAvailable = false).
[[nodiscard]] bool shop_purge_legal(const RunState& rs,
                                    const ShopState& shop) noexcept;
[[nodiscard]] bool shop_purge_card_legal(const RunState& rs,
                                         const ShopState& shop,
                                         uint16_t deck_index) noexcept;
[[nodiscard]] bool shop_purge_card(RunState& rs, ShopState& shop,
                                   uint16_t deck_index) noexcept;

// --- Room entry ----------------------------------------------------------------

// The AbstractRelic.justEnteredRoom fan-out for the room the player has just
// arrived in (AbstractDungeon.nextRoomTransition :1785-1789). It runs AFTER a ?
// room has been replaced by its resolved room and after setCurrMapNode, so a
// ?->Shop is indistinguishable from a static ShopRoom here -- which is exactly
// why Meal Ticket heals on both. Meal Ticket is the only S1 override; the heal
// is out of combat, so Magic Flower's onPlayerHeal branch (MagicFlower.java:
// 31-37, combat-only) cannot scale it. `in_shop` is the only room predicate any
// S1 justEnteredRoom body reads.
void dispatch_just_entered_room_relics(RunState& rs, bool in_shop) noexcept;

inline constexpr int kMealTicketHeal = 15;  // MealTicket.HP_AMT.

}  // namespace sts::engine
