#include "sts/engine/relic_pools.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <span>

#include "sts/engine/cards.hpp"
#include "sts/engine/rng_jdk.hpp"

namespace sts::engine {
namespace {

int pool_index(RelicTier tier) noexcept {
    switch (tier) {
        case RelicTier::COMMON: return static_cast<int>(RelicPool::COMMON);
        case RelicTier::UNCOMMON: return static_cast<int>(RelicPool::UNCOMMON);
        case RelicTier::RARE: return static_cast<int>(RelicPool::RARE);
        case RelicTier::SHOP: return static_cast<int>(RelicPool::SHOP);
        case RelicTier::BOSS: return static_cast<int>(RelicPool::BOSS);
        default: return -1;
    }
}

RelicId remove_front(RunState& rs, int p) noexcept {
    const uint8_t count = rs.relic_pool_count[p];
    assert(count > 0);
    const RelicId id = static_cast<RelicId>(rs.relic_pools[p][0]);
    for (uint8_t i = 1; i < count; ++i) {
        rs.relic_pools[p][i - 1] = rs.relic_pools[p][i];
    }
    rs.relic_pools[p][count - 1] = 0;
    rs.relic_pool_count[p] = static_cast<uint8_t>(count - 1);
    return id;
}

RelicId remove_end(RunState& rs, int p) noexcept {
    const uint8_t count = rs.relic_pool_count[p];
    assert(count > 0);
    const uint8_t at = static_cast<uint8_t>(count - 1);
    const RelicId id = static_cast<RelicId>(rs.relic_pools[p][at]);
    rs.relic_pools[p][at] = 0;
    rs.relic_pool_count[p] = at;
    return id;
}

void remove_owned_from_pools(RunState& rs) noexcept {
    for (uint8_t owned = 0; owned < rs.relic_count; ++owned) {
        const uint16_t id = rs.relics[owned].relic_id;
        for (int p = 0; p < kRelicTierCount; ++p) {
            const uint8_t count = rs.relic_pool_count[p];
            for (uint8_t i = 0; i < count; ++i) {
                if (rs.relic_pools[p][i] != id) {
                    continue;
                }
                for (uint8_t j = static_cast<uint8_t>(i + 1); j < count; ++j) {
                    rs.relic_pools[p][j - 1] = rs.relic_pools[p][j];
                }
                rs.relic_pools[p][count - 1] = 0;
                rs.relic_pool_count[p] = static_cast<uint8_t>(count - 1);
                break;
            }
        }
    }
}

void upgrade_random_cards(RunState& rs, RngStream& misc_rng,
                          CardType wanted) noexcept {
    uint16_t candidates[kMasterDeckCap]{};
    uint16_t count = 0;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        const CardInstance& card = rs.master_deck[i];
        const CardDef* def = card_def(static_cast<CardId>(card.card_id));
        if (def != nullptr && def->type == wanted && card.upgrade == 0) {
            candidates[count++] = i;
        }
    }

    // Collections.shuffle evaluates miscRng.randomLong() as the Random ctor arg
    // even when the filtered list has fewer than two cards.
    JdkRandom jdk(random_long(misc_rng));
    jdk_shuffle(std::span<uint16_t>(candidates, count), jdk);
    const uint16_t take = count < 2 ? count : 2;
    for (uint16_t i = 0; i < take; ++i) {
        rs.master_deck[candidates[i]].upgrade = 1;
    }
}

}  // namespace

void initialize_relic_pools(RunState& rs) noexcept {
    for (int p = 0; p < kRelicTierCount; ++p) {
        std::fill_n(rs.relic_pools[p], kRelicPoolCap, uint16_t{0});
        rs.relic_pool_count[p] = 0;
    }

    for (const RelicDef* def : kRelicDefs) {
        if (def->pool_order < 0) {
            continue;
        }
        const int p = pool_index(def->tier);
        assert(p >= 0 && def->pool_order < kRelicPoolCap);
        rs.relic_pools[p][def->pool_order] = static_cast<uint16_t>(def->id);
        const uint8_t needed = static_cast<uint8_t>(def->pool_order + 1);
        if (rs.relic_pool_count[p] < needed) {
            rs.relic_pool_count[p] = needed;
        }
    }

    for (int p = 0; p < kRelicTierCount; ++p) {
        JdkRandom jdk(random_long(rs.relic_rng));
        jdk_shuffle(std::span<uint16_t>(rs.relic_pools[p],
                                       rs.relic_pool_count[p]), jdk);
    }

    if (rs.floor >= 1) {
        remove_owned_from_pools(rs);
    }
}

RelicTier return_random_relic_tier(RunState& rs) noexcept {
    return relic_tier_for_roll(random(rs.relic_rng, 0, 99));
}

bool relic_can_spawn(RelicId id, RelicSpawnContext ctx) noexcept {
    if (ctx.endless) {
        return true;
    }
    switch (id) {
        case RelicId::ANCIENT_TEA_SET:
        case RelicId::CERAMIC_FISH:
        case RelicId::DREAM_CATCHER:
        case RelicId::JUZU_BRACELET:
        case RelicId::MEAL_TICKET:
        case RelicId::OMAMORI:
        case RelicId::REGAL_PILLOW:
        case RelicId::POTION_BELT:
            return ctx.floor <= 48;
        case RelicId::MAW_BANK:
        case RelicId::SMILING_MASK:
            return ctx.floor <= 48 && !ctx.in_shop;
        case RelicId::PRESERVED_INSECT:
            return ctx.floor <= 52;
        case RelicId::TINY_CHEST:
            return ctx.floor <= 35;
        default:
            return true;
    }
}

RelicId return_random_relic_key(RunState& rs, RelicTier tier,
                                RelicSpawnContext ctx) noexcept {
    const int p = pool_index(tier);
    if (p < 0) {
        return RelicId::NONE;
    }
    if (rs.relic_pool_count[p] == 0) {
        switch (tier) {
            case RelicTier::COMMON:
                return return_random_relic_key(rs, RelicTier::UNCOMMON, ctx);
            case RelicTier::UNCOMMON:
                return return_random_relic_key(rs, RelicTier::RARE, ctx);
            case RelicTier::RARE:
                return RelicId::CIRCLET;
            case RelicTier::SHOP:
                return return_random_relic_key(rs, RelicTier::UNCOMMON, ctx);
            case RelicTier::BOSS:
                // Java returns the key "Red Circlet", but RelicLibrary does not
                // register RedCirclet; getRelic therefore defaults to Circlet.
                return RelicId::CIRCLET;
            default:
                return RelicId::NONE;
        }
    }
    const RelicId id = remove_front(rs, p);
    return relic_can_spawn(id, ctx)
               ? id
               : return_end_random_relic_key(rs, tier, ctx);
}

RelicId return_end_random_relic_key(RunState& rs, RelicTier tier,
                                    RelicSpawnContext ctx) noexcept {
    const int p = pool_index(tier);
    if (p < 0) {
        return RelicId::NONE;
    }
    if (rs.relic_pool_count[p] == 0) {
        switch (tier) {
            case RelicTier::COMMON:
                return return_random_relic_key(rs, RelicTier::UNCOMMON, ctx);
            case RelicTier::UNCOMMON:
                return return_random_relic_key(rs, RelicTier::RARE, ctx);
            case RelicTier::RARE:
                return RelicId::CIRCLET;
            case RelicTier::SHOP:
                return return_random_relic_key(rs, RelicTier::UNCOMMON, ctx);
            case RelicTier::BOSS:
                return RelicId::CIRCLET;
            default:
                return RelicId::NONE;
        }
    }
    // The Java boss "end" path is intentionally front-pop too.
    const RelicId id = tier == RelicTier::BOSS ? remove_front(rs, p)
                                                : remove_end(rs, p);
    return relic_can_spawn(id, ctx)
               ? id
               : return_end_random_relic_key(rs, tier, ctx);
}

RelicAcquireResult acquire_relic(RunState& rs, RngStream& misc_rng,
                                 RelicId id) noexcept {
    const RelicDef* def = relic_def(id);
    if (def == nullptr) {
        return RelicAcquireResult::INVALID_ID;
    }

    if (id == RelicId::CIRCLET) {
        for (uint8_t i = 0; i < rs.relic_count; ++i) {
            RelicSlot& slot = rs.relics[i];
            if (slot.relic_id != static_cast<uint16_t>(RelicId::CIRCLET)) {
                continue;
            }
            if (slot.counter == std::numeric_limits<int16_t>::max()) {
                return RelicAcquireResult::COUNTER_CAP_REACHED;
            }
            ++slot.counter;
            return RelicAcquireResult::CIRCLET_STACKED;
        }
    }

    if (rs.relic_count >= kRelicCap) {
        return RelicAcquireResult::RELIC_CAP_REACHED;
    }

    RelicSlot& slot = rs.relics[rs.relic_count++];
    slot.relic_id = static_cast<uint16_t>(id);
    slot.counter = def->initial_counter;

    switch (id) {
        case RelicId::STRAWBERRY:
            rs.max_hp = static_cast<int16_t>(rs.max_hp + 7);
            rs.hp = static_cast<int16_t>(rs.hp + 7);
            break;
        case RelicId::POTION_BELT:
            rs.potion_slots = static_cast<uint8_t>(
                std::min<int>(kPotionCap, static_cast<int>(rs.potion_slots) + 2));
            break;
        case RelicId::WAR_PAINT:
            upgrade_random_cards(rs, misc_rng, CardType::SKILL);
            break;
        case RelicId::WHETSTONE:
            upgrade_random_cards(rs, misc_rng, CardType::ATTACK);
            break;
        default:
            break;
    }
    return RelicAcquireResult::ACQUIRED;
}

}  // namespace sts::engine
