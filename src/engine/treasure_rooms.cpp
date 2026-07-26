#include "sts/engine/treasure_rooms.hpp"

#include <cassert>

#include "sts/engine/card_pools.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/run_deck.hpp"

namespace sts::engine {

namespace {

RunRewardItem& push_item(RewardScreen& s) noexcept {
    assert(s.count < kRewardItemCap);
    RunRewardItem& item = s.items[s.count++];
    item = RunRewardItem{};
    return item;
}

void add_relic_item(RewardScreen& s, RelicId id) noexcept {
    RunRewardItem& item = push_item(s);
    item.kind = static_cast<uint8_t>(RewardItemKind::RELIC);
    item.id = static_cast<uint16_t>(id);
}

RelicSpawnContext chest_spawn_context(const RunState& rs) noexcept {
    RelicSpawnContext ctx{};
    ctx.floor = rs.floor;
    ctx.act = rs.act;
    fill_deck_spawn_gates(rs, ctx);
    fill_campfire_relic_count(rs, ctx);
    fill_boss_spawn_gates(rs, ctx);
    return ctx;
}

void add_random_relic_item(RunState& rs, RewardScreen& out,
                           RelicTier tier) noexcept {
    add_relic_item(
        out, return_random_relic_key(rs, tier, chest_spawn_context(rs)));
}

// ShowCardAndObtainEffect's constructor checks Omamori synchronously. The curse
// identity argument was evaluated first, so the cardRng draw is never skipped.
// When not blocked, collapse the later visual effect to the sanctioned
// master-deck obtain transaction (eggs/Ceramic Fish/Darkstone/Du-Vu all fire).
// Java resolves that visual after open() finishes; moving its state mutation
// forward is observationally safe here because those obtain handlers cannot
// alter treasureRng, relicRng, a relic pool, or this reward list. The identity
// draw and Omamori use remain at their constructor-time positions.
void cursed_key_obtain(RunState& rs) noexcept {
    const CardId curse = return_random_curse(rs.card_rng);
    RelicSlot* omamori = nullptr;
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (static_cast<RelicId>(rs.relics[i].relic_id) ==
            RelicId::OMAMORI) {
            omamori = &rs.relics[i];  // getRelic returns the first match.
            break;
        }
    }
    if (omamori != nullptr && omamori->counter != 0) {
        --omamori->counter;
        return;
    }
    (void)add_card_to_master_deck(rs, curse);
}

void dispatch_on_chest_open_impl(RunState& rs, RewardScreen& out,
                                 bool boss_chest) noexcept {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        RelicSlot& slot = rs.relics[i];
        switch (static_cast<RelicId>(slot.relic_id)) {
            case RelicId::MATRYOSHKA:
                if (!boss_chest && slot.counter > 0) {
                    --slot.counter;
                    const RelicTier tier =
                        random_boolean(rs.relic_rng, 0.75f)
                            ? RelicTier::COMMON
                            : RelicTier::UNCOMMON;
                    add_random_relic_item(rs, out, tier);
                    if (slot.counter == 0) {
                        slot.counter = -2;
                    }
                }
                break;
            case RelicId::CURSED_KEY:
                if (!boss_chest) {
                    cursed_key_obtain(rs);
                }
                break;
            default:
                break;
        }
    }
}

void remove_first_relic_item(RewardScreen& s) noexcept {
    for (uint8_t i = 0; i < s.count; ++i) {
        if (static_cast<RewardItemKind>(s.items[i].kind) !=
            RewardItemKind::RELIC) {
            continue;
        }
        for (uint8_t j = static_cast<uint8_t>(i + 1); j < s.count; ++j) {
            s.items[j - 1] = s.items[j];
        }
        --s.count;
        s.items[s.count] = RunRewardItem{};
        return;
    }
}

void dispatch_on_chest_open_after_impl(RunState& rs, RewardScreen& out,
                                       bool boss_chest) noexcept {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        RelicSlot& slot = rs.relics[i];
        if (static_cast<RelicId>(slot.relic_id) == RelicId::NLOTHS_MASK &&
            !boss_chest && slot.counter > 0) {
            --slot.counter;
            remove_first_relic_item(out);
            if (slot.counter == 0) {
                slot.counter = -2;
            }
        }
    }
}

int chest_gold_amount(ChestSize size) noexcept {
    switch (size) {
        case ChestSize::SMALL:
            return 25;
        case ChestSize::MEDIUM:
            return 50;
        case ChestSize::LARGE:
            return 75;
        case ChestSize::NONE:
        default:
            return 0;
    }
}

}  // namespace

TreasureChest roll_treasure_chest(RunState& rs) noexcept {
    const int32_t size_roll = random(rs.treasure_rng, 0, 99);
    const int32_t contents_roll = random(rs.treasure_rng, 0, 99);
    return treasure_chest_for_rolls(size_roll, contents_roll);
}

void dispatch_relics_on_chest_open(RunState& rs, RewardScreen& out,
                                   bool boss_chest) noexcept {
    dispatch_on_chest_open_impl(rs, out, boss_chest);
}

void dispatch_relics_on_chest_open_after(RunState& rs, RewardScreen& out,
                                         bool boss_chest) noexcept {
    dispatch_on_chest_open_after_impl(rs, out, boss_chest);
}

void open_treasure_chest(RunState& rs, RngStream& /*misc_rng*/,
                         TreasureChest& chest, RewardScreen& out) noexcept {
    if (chest.opened != 0 ||
        static_cast<ChestSize>(chest.size) == ChestSize::NONE) {
        return;
    }

    out = RewardScreen{};
    out.open_card_item = kNoOpenCardReward;

    // AbstractChest.open(false), line order is observable across three streams
    // and the reward list: before-hooks, gold, base relic, after-hooks.
    dispatch_relics_on_chest_open(rs, out, /*boss_chest=*/false);

    if (chest.has_gold != 0) {
        const int base = chest_gold_amount(static_cast<ChestSize>(chest.size));
        RunRewardItem& item = push_item(out);
        item.kind = static_cast<uint8_t>(RewardItemKind::GOLD);
        item.gold = mathutils_round(random(
            rs.treasure_rng, static_cast<float>(base) * 0.9f,
            static_cast<float>(base) * 1.1f));
        // RewardItem.applyGoldBonus explicitly excludes TreasureRoom, so Golden
        // Idol contributes no bonus here (RewardItem.java:110-129).
        item.bonus_gold = 0;
    }

    add_random_relic_item(
        rs, out, static_cast<RelicTier>(chest.relic_tier));
    dispatch_relics_on_chest_open_after(rs, out, /*boss_chest=*/false);
    chest.opened = 1;
}

}  // namespace sts::engine
