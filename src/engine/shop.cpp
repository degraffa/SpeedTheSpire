#include "sts/engine/shop.hpp"

#include <cassert>
#include <cstddef>

#include "relics/relic_pickup.hpp"  // gain_gold / lose_gold doors
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/interp.hpp"  // mathutils_round
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rest_sites.hpp"  // rest_card_purgeable (the same grid filter)
#include "sts/engine/run_deck.hpp"

namespace sts::engine {
namespace {

// --- Ownership predicates ------------------------------------------------------

// The shop's ownership tests are the game's `hasRelic(...)`; run_has_relic
// (combat_rewards.hpp) is already that predicate.
using sts::engine::run_has_relic;

// --- The nine sorted type-filtered rarity views --------------------------------

struct PoolView {
    const CardId* data;
    int count;
};

// Only COMMON x POWER is empty, and that emptiness is what makes
// getCardFromPool's POWER recursion reachable. Pinning the other eight
// non-empty is what makes the two "cannot happen" exits below genuinely
// unreachable rather than merely untested.
static_assert(kIroncladCommonPowerPoolCount == 0,
              "the Ironclad has no COMMON POWER card; getCardFromPool's POWER "
              "branch recurses to UNCOMMON because of it");
static_assert(kIroncladCommonAttackPoolCount > 0 &&
                  kIroncladCommonSkillPoolCount > 0 &&
                  kIroncladUncommonAttackPoolCount > 0 &&
                  kIroncladUncommonSkillPoolCount > 0 &&
                  kIroncladUncommonPowerPoolCount > 0 &&
                  kIroncladRareAttackPoolCount > 0 &&
                  kIroncladRareSkillPoolCount > 0 &&
                  kIroncladRarePowerPoolCount > 0,
              "every other (rarity, type) view must be non-empty, or "
              "getCardFromPool's fallthrough/curse exits become reachable");

[[nodiscard]] PoolView typed_pool(RewardCardRarity rarity,
                                  CardType type) noexcept {
    switch (rarity) {
        case RewardCardRarity::RARE:
            switch (type) {
                case CardType::ATTACK:
                    return {kIroncladRareAttackPool.data(),
                            kIroncladRareAttackPoolCount};
                case CardType::SKILL:
                    return {kIroncladRareSkillPool.data(),
                            kIroncladRareSkillPoolCount};
                case CardType::POWER:
                    return {kIroncladRarePowerPool.data(),
                            kIroncladRarePowerPoolCount};
                default:
                    return {nullptr, 0};
            }
        case RewardCardRarity::UNCOMMON:
            switch (type) {
                case CardType::ATTACK:
                    return {kIroncladUncommonAttackPool.data(),
                            kIroncladUncommonAttackPoolCount};
                case CardType::SKILL:
                    return {kIroncladUncommonSkillPool.data(),
                            kIroncladUncommonSkillPoolCount};
                case CardType::POWER:
                    return {kIroncladUncommonPowerPool.data(),
                            kIroncladUncommonPowerPoolCount};
                default:
                    return {nullptr, 0};
            }
        case RewardCardRarity::COMMON:
        default:
            switch (type) {
                case CardType::ATTACK:
                    return {kIroncladCommonAttackPool.data(),
                            kIroncladCommonAttackPoolCount};
                case CardType::SKILL:
                    return {kIroncladCommonSkillPool.data(),
                            kIroncladCommonSkillPoolCount};
                default:
                    // COMMON x POWER is the empty view; anything else is not a
                    // shop type.
                    return {nullptr, 0};
            }
    }
}

// --- Merchant card slots -------------------------------------------------------

// AbstractDungeon.rollRarity() (:1597-1620) as a ShopRoom sees it: ONE
// cardRng.random(99), biased by cardBlizzRandomizer, against the ShopRoom's
// 9 / 37 table with alternation OFF (ShopRoom.getCardRarity, ShopRoom.java:
// 52-55). The blizz counter is NOT updated here -- only getRewardCards' switch
// (:1435-1451) moves it, and the shop never calls that.
[[nodiscard]] RewardCardRarity shop_roll_rarity(RunState& rs) noexcept {
    const int roll = static_cast<int>(random(rs.card_rng, 99)) +
                     static_cast<int>(rs.card_blizz_randomizer);
    return shop_card_rarity_for_roll(roll);
}

// Merchant.<init>'s per-slot draw, including the two loops it wraps around it.
// `dedupe_against` is CardId::NONE for the first slot of a type pair and the
// previously drawn id for the second (Merchant.java:65-67, :75-77 compare
// against cards1.get(size - 1), i.e. the slot immediately before).
//
// The `while (c.color == COLORLESS)` guard on every one of the five slots is
// modelled by construction rather than by a loop: the three RED rarity pools
// are the only lists these draws index and none of them holds a colourless
// row, so the guard can never fire. That is a property of
// CardLibrary.addRedCards (CardLibrary.java:1152-1161), not an assumption --
// the generated pools are filtered on `color == RED`.
struct DrawnCard {
    CardId id;
    RewardCardRarity rarity;  // the POOL it came from == the card's own rarity.
};

[[nodiscard]] DrawnCard merchant_card(RunState& rs, CardType type,
                                      CardId dedupe_against) noexcept {
    DrawnCard out{};
    out.id = shop_card_from_pool(rs.card_rng, shop_roll_rarity(rs), type,
                                 &out.rarity);
    while (dedupe_against != CardId::NONE && out.id == dedupe_against) {
        out.id = shop_card_from_pool(rs.card_rng, shop_roll_rarity(rs), type,
                                     &out.rarity);
    }
    return out;
}

// --- Prices --------------------------------------------------------------------

// ShopScreen.initCards' card price (:253-255, :263-265). Java evaluates
// `(float)getPrice(rarity) * merchantRng.random(0.9f, 1.1f)` in FLOAT, applies
// the colourless bump in float too, and only then truncates with a C-style
// (int) cast. Doing the multiply in double would round differently at the
// boundary, so every intermediate here is a float.
[[nodiscard]] int16_t roll_card_price(RngStream& merchant_rng, int base,
                                      bool colorless) noexcept {
    float price = static_cast<float>(base) * random(merchant_rng, 0.9f, 1.1f);
    if (colorless) {
        price *= 1.2f;  // ShopScreen.COLORLESS_PRICE_BUMP
    }
    return static_cast<int16_t>(static_cast<int>(price));
}

// StoreRelic / StorePotion price jitter (ShopScreen.java:368, :380). Same shape
// as the card jitter but a NARROWER band and MathUtils.round instead of a
// truncation. `Settings.isDailyRun` is always false for us, so the jitter is
// unconditional.
[[nodiscard]] int16_t roll_item_price(RngStream& merchant_rng,
                                      int base) noexcept {
    return static_cast<int16_t>(mathutils_round(
        static_cast<float>(base) * random(merchant_rng, 0.95f, 1.05f)));
}

// ShopScreen.applyDiscount (:340-358): every UNSOLD row's price is
// MathUtils.rounded in place, in the Java's list order (relics, potions,
// colored, colorless). Sold rows are skipped because the game has already
// removed them from those lists.
//
// The purge half is the caller's: the two call sites disagree about it (init
// re-reads `purgeCost` per multiplier and so LOSES an earlier one, while
// purgeCard spells the combined product), so it is computed by the two
// shop_purge_cost_* functions rather than folded in here.
void apply_price_discount(ShopState& shop, float multiplier) noexcept {
    auto scale = [multiplier](ShopSlot* slots, int n) noexcept {
        for (int i = 0; i < n; ++i) {
            if (slots[i].sold != 0) {
                continue;
            }
            slots[i].price = static_cast<int16_t>(mathutils_round(
                static_cast<float>(slots[i].price) * multiplier));
        }
    };
    scale(shop.relics, kShopRelicCount);
    scale(shop.potions, kShopPotionCount);
    scale(shop.colored, kShopColoredCount);
    scale(shop.colorless, kShopColorlessCount);
}

// --- Relic / potion helpers ----------------------------------------------------

[[nodiscard]] RelicSpawnContext shop_spawn_context(const RunState& rs) noexcept {
    RelicSpawnContext ctx{};
    ctx.floor = rs.floor;
    ctx.act = rs.act;
    // getCurrRoom() IS the ShopRoom by the time Merchant's constructor runs
    // (setCurrMapNode precedes onPlayerEntry, AbstractDungeon.java:1783 vs
    // :1800), so the four `!(getCurrRoom() instanceof ShopRoom)` canSpawn gates
    // -- Maw Bank, Smiling Mask, The Courier, Old Coin -- are all CLOSED here.
    // This is RNG-visible: a closed gate makes the end-pop discard that id and
    // pop another, which moves the pool.
    ctx.in_shop = true;
    fill_deck_spawn_gates(rs, ctx);
    fill_campfire_relic_count(rs, ctx);
    fill_boss_spawn_gates(rs, ctx);
    return ctx;
}

// StoreRelic.<init> reads `relic.getPrice()` off the relic it was handed, NOT
// off the tier that was requested. Those differ whenever
// returnEndRandomRelicKey took an empty-pool fallback (an exhausted common pool
// answers with an UNCOMMON, an exhausted uncommon with a RARE, an exhausted
// shop pool with an UNCOMMON -- AbstractDungeon.java:704-753), so the price
// must come from the relic's own registry tier.
[[nodiscard]] int drawn_relic_base_price(RelicId id) noexcept {
    const RelicDef* def = relic_def(id);
    return def == nullptr ? 0 : relic_base_price(def->tier);
}

[[nodiscard]] int drawn_potion_base_price(PotionId id) noexcept {
    const PotionDef* def = potion_def(id);
    return def == nullptr ? 0 : potion_base_price(def->rarity);
}

// The eggs' onPreviewObtainCard (MoltenEgg2 / FrozenEgg2 / ToxicEgg2 .java:
// 40-50, each forwarding straight to its own onObtainCard) runs over every
// stocked card as the shop is built (ShopScreen.java:258-260, :268-270), so an
// egg owner's shop DISPLAYS its matching cards already upgraded and the card
// bought is that upgraded instance. No other S1 relic overrides
// onPreviewObtainCard, and none of the three consumes RNG or touches RunState.
[[nodiscard]] uint8_t preview_upgrade(const RunState& rs, CardId id) noexcept {
    const CardDef* def = card_def(id);
    if (def == nullptr) {
        return 0;
    }
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        switch (static_cast<RelicId>(rs.relics[i].relic_id)) {
            case RelicId::MOLTEN_EGG:
                if (def->type == CardType::ATTACK) return 1;
                break;
            case RelicId::TOXIC_EGG:
                if (def->type == CardType::SKILL) return 1;
                break;
            case RelicId::FROZEN_EGG:
                if (def->type == CardType::POWER) return 1;
                break;
            default:
                break;
        }
    }
    return 0;
}

[[nodiscard]] bool slot_buyable(const RunState& rs, const ShopSlot& slot) noexcept {
    return slot.sold == 0 && slot.id != 0 &&
           rs.gold >= static_cast<int32_t>(slot.price);
}

}  // namespace

// --- Pool draw -----------------------------------------------------------------

CardId shop_card_from_pool(RngStream& card_rng, RewardCardRarity rarity,
                           CardType type,
                           RewardCardRarity* drawn_rarity) noexcept {
    RewardCardRarity sink{};
    RewardCardRarity& drawn = drawn_rarity == nullptr ? sink : *drawn_rarity;
    drawn = rarity;
    if (type == CardType::POWER) {
        // AbstractDungeon.java:1556-1565: an empty type view at COMMON returns
        // getCardFromPool(UNCOMMON, POWER, ...) and an empty one at UNCOMMON
        // returns getCardFromPool(RARE, POWER, ...). The empty pool costs NO
        // draw -- getRandomCard(type, useRng) returns null before it indexes
        // anything (CardGroup.java:539-547).
        for (int r = static_cast<int>(rarity);
             r <= static_cast<int>(RewardCardRarity::RARE); ++r) {
            const PoolView v =
                typed_pool(static_cast<RewardCardRarity>(r), type);
            if (v.count > 0) {
                drawn = static_cast<RewardCardRarity>(r);
                return v.data[static_cast<std::size_t>(
                    random(card_rng, v.count - 1))];
            }
        }
        // An empty RARE POWER view would fall through to the UNCOMMON case and
        // recurse forever in the game; the static_assert above forbids it.
        assert(false && "no POWER card in any Ironclad rarity pool");
        return CardId::NONE;
    }

    // Non-POWER: the decompiled switch has no `break` after its error log, so a
    // null RARE FALLS THROUGH to the UNCOMMON case and a null UNCOMMON to the
    // COMMON case -- downwards, the opposite direction from POWER's recursion.
    for (int r = static_cast<int>(rarity);
         r >= static_cast<int>(RewardCardRarity::COMMON); --r) {
        const PoolView v = typed_pool(static_cast<RewardCardRarity>(r), type);
        if (v.count > 0) {
            drawn = static_cast<RewardCardRarity>(r);
            return v.data[static_cast<std::size_t>(
                random(card_rng, v.count - 1))];
        }
    }
    // COMMON's fallthrough lands in the CURSE case; unreachable for the
    // ATTACK/SKILL views the shop asks for (static_assert above).
    assert(false && "no ATTACK/SKILL card in any Ironclad rarity pool");
    return CardId::NONE;
}

// --- Purge cost ----------------------------------------------------------------

int shop_purge_cost_at_init(const RunState& rs, int purge_cost) noexcept {
    // ShopScreen.init (:226-238) runs applyDiscount up to three times and each
    // call recomputes actualPurgeCost from `purgeCost`, never from the value
    // the previous call left. So with BOTH shop relics the Courier's 0.8 is
    // overwritten by the Membership Card's 0.5 rather than compounding with it
    // -- unlike purgeCard's tail below, which spells the product explicitly.
    // Reproduced, not corrected: it is the game's answer.
    if (run_has_relic(rs, RelicId::SMILING_MASK)) {
        return kSmilingMaskPurgeCost;
    }
    if (run_has_relic(rs, RelicId::MEMBERSHIP_CARD)) {
        return mathutils_round(static_cast<float>(purge_cost) * 0.5f);
    }
    if (run_has_relic(rs, RelicId::THE_COURIER)) {
        return mathutils_round(static_cast<float>(purge_cost) * 0.8f);
    }
    return purge_cost;
}

int shop_purge_cost_after_purge(const RunState& rs, int purge_cost) noexcept {
    // ShopScreen.purgeCard (:281-291), which -- unlike init -- writes the
    // both-owned case as a single round of 0.8f * 0.5f applied left to right.
    if (run_has_relic(rs, RelicId::SMILING_MASK)) {
        return kSmilingMaskPurgeCost;
    }
    const bool courier = run_has_relic(rs, RelicId::THE_COURIER);
    const bool membership = run_has_relic(rs, RelicId::MEMBERSHIP_CARD);
    if (courier && membership) {
        return mathutils_round(static_cast<float>(purge_cost) * 0.8f * 0.5f);
    }
    if (courier) {
        return mathutils_round(static_cast<float>(purge_cost) * 0.8f);
    }
    if (membership) {
        return mathutils_round(static_cast<float>(purge_cost) * 0.5f);
    }
    return purge_cost;
}

// --- Stock generation ----------------------------------------------------------

ShopState generate_shop(RunState& rs) noexcept {
    ShopState shop{};
    shop.screen = static_cast<uint8_t>(ShopScreenKind::MENU);
    shop.purge_available = 1;

    // (1) Merchant.<init> -- seven cardRng identities, in list order. Each
    //     draw carries the pool it came from, which IS the card's rarity and
    //     therefore the argument AbstractCard.getPrice is given below; the two
    //     differ whenever the POWER slot's COMMON roll was answered from the
    //     UNCOMMON pool.
    const DrawnCard a0 = merchant_card(rs, CardType::ATTACK, CardId::NONE);
    const DrawnCard a1 = merchant_card(rs, CardType::ATTACK, a0.id);
    const DrawnCard s0 = merchant_card(rs, CardType::SKILL, CardId::NONE);
    const DrawnCard s1 = merchant_card(rs, CardType::SKILL, s0.id);
    const DrawnCard p0 = merchant_card(rs, CardType::POWER, CardId::NONE);
    const DrawnCard colored[kShopColoredCount] = {a0, a1, s0, s1, p0};
    const DrawnCard colorless[kShopColorlessCount] = {
        {draw_colorless_card_from_pool(rs.card_rng, RewardCardRarity::UNCOMMON),
         RewardCardRarity::UNCOMMON},
        {draw_colorless_card_from_pool(rs.card_rng, RewardCardRarity::RARE),
         RewardCardRarity::RARE},
    };

    // (2) ShopScreen.initCards -- five colored jitters, then two colourless
    //     jitters, then the sale index. The colourless bump is inside the
    //     truncation, the sale halving is after it.
    for (int i = 0; i < kShopColoredCount; ++i) {
        ShopSlot& slot = shop.colored[i];
        slot.id = static_cast<uint16_t>(colored[i].id);
        slot.upgrade = preview_upgrade(rs, colored[i].id);
        slot.price = roll_card_price(rs.merchant_rng,
                                     card_base_price(colored[i].rarity),
                                     /*colorless=*/false);
    }
    for (int i = 0; i < kShopColorlessCount; ++i) {
        ShopSlot& slot = shop.colorless[i];
        slot.id = static_cast<uint16_t>(colorless[i].id);
        slot.upgrade = preview_upgrade(rs, colorless[i].id);
        slot.price = roll_card_price(rs.merchant_rng,
                                     card_base_price(colorless[i].rarity),
                                     /*colorless=*/true);
    }
    shop.sale_index =
        static_cast<uint8_t>(random(rs.merchant_rng, 0, kShopColoredCount - 1));
    // `saleCard.price /= 2` is an INTEGER divide of the already-truncated
    // price, and it happens before any discount pass.
    shop.colored[shop.sale_index].price =
        static_cast<int16_t>(shop.colored[shop.sale_index].price / 2);

    // (3) ShopScreen.initRelics -- slots 0 and 1 roll a tier, slot 2 is always
    //     SHOP. Each slot END-pops its pool (trap 15) and then jitters.
    const RelicSpawnContext ctx = shop_spawn_context(rs);
    for (int i = 0; i < kShopRelicCount; ++i) {
        const RelicTier tier =
            i != 2 ? shop_relic_tier_for_roll(random(rs.merchant_rng, 99))
                   : RelicTier::SHOP;
        const RelicId id = return_end_random_relic_key(rs, tier, ctx);
        shop.relics[i].id = static_cast<uint16_t>(id);
        shop.relics[i].price =
            roll_item_price(rs.merchant_rng, drawn_relic_base_price(id));
    }

    // (4) ShopScreen.initPotions -- three returnRandomPotion identities, each
    //     immediately followed by its own merchantRng jitter (the two streams
    //     interleave, which is why this is one loop and not two).
    for (int i = 0; i < kShopPotionCount; ++i) {
        const PotionId id = return_random_potion(rs.potion_rng);
        shop.potions[i].id = static_cast<uint16_t>(id);
        shop.potions[i].price =
            roll_item_price(rs.merchant_rng, drawn_potion_base_price(id));
    }

    // (5) ShopScreen.init's discount tail (:226-238), in the game's order. A16
    //     passes affectPurge=false, so ascension never moves the purge cost.
    if (rs.ascension >= 16) {
        apply_price_discount(shop, 1.1f);
    }
    if (run_has_relic(rs, RelicId::THE_COURIER)) {
        apply_price_discount(shop, 0.8f);
    }
    if (run_has_relic(rs, RelicId::MEMBERSHIP_CARD)) {
        apply_price_discount(shop, 0.5f);
    }
    shop.actual_purge_cost = static_cast<int16_t>(
        shop_purge_cost_at_init(rs, static_cast<int>(rs.purge_cost)));
    return shop;
}

// --- Purchases -----------------------------------------------------------------

bool shop_buy_card_legal(const RunState& rs, const ShopState& shop,
                         uint8_t index, bool colorless) noexcept {
    const int cap = colorless ? kShopColorlessCount : kShopColoredCount;
    if (index >= cap) {
        return false;
    }
    const ShopSlot& slot = colorless ? shop.colorless[index] : shop.colored[index];
    // ShopScreen.purchaseCard (:592-644) gates on gold only; the master-deck
    // capacity guard is ours, and refusing there keeps the transaction atomic.
    return slot_buyable(rs, slot) && rs.master_deck_count < kMasterDeckCap;
}

bool shop_buy_card(RunState& rs, ShopState& shop, uint8_t index,
                   bool colorless) noexcept {
    if (!shop_buy_card_legal(rs, shop, index, colorless)) {
        return false;
    }
    ShopSlot& slot = colorless ? shop.colorless[index] : shop.colored[index];
    // loseGold BEFORE the obtain, exactly as purchaseCard orders it (:596 vs
    // the FastCardObtainEffect that resolves later). in_shop=true is what fires
    // Maw Bank's onSpendGold.
    lose_gold(rs, static_cast<int32_t>(slot.price), /*in_shop=*/true);
    // FastCardObtainEffect.update (:36-53) -> Soul.obtain (Soul.java:145-156)
    // -> masterDeck.addToTop, which is CardGroup.addToTop == group.add(c), an
    // APPEND (CardGroup.java:455-457). add_card_to_master_deck is that append
    // plus the same onObtainCard / onMasterDeckChange fan-outs. It also carries
    // an Omamori door FastCardObtainEffect does not have; unreachable here,
    // because no curse is ever stocked (the shop draws only RED non-basics and
    // poolable colourless cards).
    (void)add_card_to_master_deck(rs, static_cast<CardId>(slot.id), slot.upgrade);
    slot.sold = 1;
    return true;
}

bool shop_buy_relic_legal(const RunState& rs, const ShopState& shop,
                          uint8_t index) noexcept {
    if (index >= kShopRelicCount) {
        return false;
    }
    const ShopSlot& slot = shop.relics[index];
    return slot_buyable(rs, slot) &&
           relic_acquire_legal(rs, static_cast<RelicId>(slot.id));
}

namespace {

// Shared body of the two shop_buy_relic overloads (contract on the
// declarations in shop.hpp).
bool shop_buy_relic_impl(RunState& rs, RngStream& misc_rng, ShopState& shop,
                         uint8_t index,
                         RelicEquipContext* equip_ctx) noexcept {
    if (!shop_buy_relic_legal(rs, shop, index)) {
        return false;
    }
    ShopSlot& slot = shop.relics[index];
    const RelicId id = static_cast<RelicId>(slot.id);
    // Fail-loud BEFORE any mutation: without an equip context an
    // on_equip_screen id (a Bottled trio relic in the shop's tier-rolled
    // stock) would hit acquire_relic's NEEDS_EQUIP_CONTEXT refusal AFTER the
    // gold was spent and the slot marked sold -- a paid-for relic silently
    // not granted. Refuse the purchase whole instead.
    if (equip_ctx == nullptr && relic_on_equip_screen_fn(id) != nullptr) {
        return false;
    }
    // StoreRelic.purchaseRelic (:83-120): loseGold, then instantObtain, and
    // ONLY THEN the two shop-relic side effects -- so a Membership Card bought
    // here discounts the rest of its own shop, and the applyDiscount it calls
    // sees itself already owned.
    lose_gold(rs, static_cast<int32_t>(slot.price), /*in_shop=*/true);
    slot.sold = 1;
    if (equip_ctx != nullptr) {
        (void)acquire_relic(rs, misc_rng, id, *equip_ctx);
    } else {
        (void)acquire_relic(rs, misc_rng, id);
    }
    if (id == RelicId::MEMBERSHIP_CARD) {
        apply_price_discount(shop, 0.5f);
        shop.actual_purge_cost = static_cast<int16_t>(
            run_has_relic(rs, RelicId::SMILING_MASK)
                ? kSmilingMaskPurgeCost
                : mathutils_round(static_cast<float>(rs.purge_cost) * 0.5f));
    } else if (id == RelicId::SMILING_MASK) {
        shop.actual_purge_cost = kSmilingMaskPurgeCost;
    }
    return true;
}

}  // namespace

bool shop_buy_relic(RunState& rs, RngStream& misc_rng, ShopState& shop,
                    uint8_t index) noexcept {
    return shop_buy_relic_impl(rs, misc_rng, shop, index, nullptr);
}

bool shop_buy_relic(RunState& rs, RngStream& misc_rng, ShopState& shop,
                    uint8_t index, RelicEquipContext& equip_ctx) noexcept {
    return shop_buy_relic_impl(rs, misc_rng, shop, index, &equip_ctx);
}

bool shop_potion_slot_available(const RunState& rs) noexcept {
    if (run_has_relic(rs, RelicId::SOZU)) {
        return false;
    }
    for (uint8_t i = 0; i < rs.potion_slots && i < kPotionCap; ++i) {
        if (rs.potions[i] == static_cast<uint16_t>(PotionId::NONE)) {
            return true;
        }
    }
    return false;
}

bool shop_buy_potion_legal(const RunState& rs, const ShopState& shop,
                           uint8_t index) noexcept {
    if (index >= kShopPotionCount) {
        return false;
    }
    // StorePotion.purchasePotion (:74-101) returns on Sozu before the gold
    // check, and a full belt refuses AFTER the gold check but WITHOUT spending
    // (obtainPotion returning false leaves the loseGold un-run, :80-94), so
    // both are "cannot buy" and neither costs anything.
    return slot_buyable(rs, shop.potions[index]) &&
           shop_potion_slot_available(rs);
}

bool shop_buy_potion(RunState& rs, ShopState& shop, uint8_t index) noexcept {
    if (!shop_buy_potion_legal(rs, shop, index)) {
        return false;
    }
    ShopSlot& slot = shop.potions[index];
    for (uint8_t i = 0; i < rs.potion_slots && i < kPotionCap; ++i) {
        if (rs.potions[i] != static_cast<uint16_t>(PotionId::NONE)) {
            continue;
        }
        rs.potions[i] = slot.id;
        break;
    }
    lose_gold(rs, static_cast<int32_t>(slot.price), /*in_shop=*/true);
    slot.sold = 1;
    return true;
}

// --- Purge ---------------------------------------------------------------------

bool shop_purge_legal(const RunState& rs, const ShopState& shop) noexcept {
    if (shop.purge_available == 0) {
        return false;
    }
    if (rs.gold < static_cast<int32_t>(shop.actual_purge_cost)) {
        return false;
    }
    // gridSelectScreen.open over getGroupWithoutBottledCards(getPurgeableCards())
    // (:973): an empty grid has nothing to confirm, so the service is dead even
    // though the game would still open the screen. The bottled exclusion is
    // part of that filter, so a deck whose only purgeable cards are bottled
    // has no purge service either (the sim's grids have no cancel button; an
    // openable-but-empty grid would be a dead end, and the state outcome of
    // open-then-cancel is identical to never opening).
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        if (master_card_purgeable_unbottled(rs.master_deck[i])) {
            return true;
        }
    }
    return false;
}

bool shop_purge_card_legal(const RunState& rs, const ShopState& shop,
                           uint16_t deck_index) noexcept {
    // The grid row filter is the same getGroupWithoutBottledCards pass the
    // service gate above cites (ShopScreen.java:973).
    return shop_purge_legal(rs, shop) && deck_index < rs.master_deck_count &&
           master_card_purgeable_unbottled(rs.master_deck[deck_index]);
}

bool shop_purge_card(RunState& rs, ShopState& shop,
                     uint16_t deck_index) noexcept {
    if (!shop_purge_card_legal(rs, shop, deck_index)) {
        return false;
    }
    // ShopScreen.purgeCard (:278-292) + ShopRoom.updatePurge (:66-77), in the
    // game's order: pay, ramp the RUN-PERSISTENT base by 25, recompute this
    // visit's cost off the ramped base, remove the card, retire the service.
    lose_gold(rs, static_cast<int32_t>(shop.actual_purge_cost),
              /*in_shop=*/true);
    rs.purge_cost = static_cast<int16_t>(rs.purge_cost + kPurgeCostRamp);
    shop.actual_purge_cost = static_cast<int16_t>(
        shop_purge_cost_after_purge(rs, static_cast<int>(rs.purge_cost)));
    (void)remove_master_deck_card(rs, deck_index);
    shop.purge_available = 0;
    shop.screen = static_cast<uint8_t>(ShopScreenKind::MENU);
    return true;
}

// --- Room entry ----------------------------------------------------------------

void dispatch_just_entered_room_relics(RunState& rs, bool in_shop) noexcept {
    if (!in_shop) {
        return;  // Meal Ticket is the only S1 justEnteredRoom body.
    }
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (rs.relics[i].relic_id != static_cast<uint16_t>(RelicId::MEAL_TICKET)) {
            continue;
        }
        // AbstractCreature.heal (AbstractCreature.java:386-418) through the
        // shared out-of-combat door, which owns the derivation of why both
        // fan-outs (onPlayerHeal, powers' onHeal) are identity out here --
        // Wave-C integration consolidated the three hand-spelled copies of
        // this clamp (this one, rest_apply_heal, heal_out_of_combat) into the
        // one door.
        heal_out_of_combat(rs, kMealTicketHeal);
    }
}

}  // namespace sts::engine
