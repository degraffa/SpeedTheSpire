#include "sts/engine/treasure_rooms.hpp"

#include <iterator>

#include "sts/engine/card_pools.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/run_deck.hpp"

namespace sts::engine {

namespace {

RunRewardItem* try_push_item(RewardScreen& s) noexcept {
    if (s.count >= kRewardItemCap) {
        return nullptr;
    }
    RunRewardItem& item = s.items[s.count++];
    item = RunRewardItem{};
    return &item;
}

bool add_relic_item(RewardScreen& s, RelicId id) noexcept {
    RunRewardItem* const item = try_push_item(s);
    if (item == nullptr) {
        return false;
    }
    item->kind = static_cast<uint8_t>(RewardItemKind::RELIC);
    item->id = static_cast<uint16_t>(id);
    return true;
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

bool add_random_relic_item(RunState& rs, RewardScreen& out,
                           RelicTier tier) noexcept {
    // Do not pop a relic pool unless its reward can be represented.
    if (out.count >= kRewardItemCap) {
        return false;
    }
    return add_relic_item(
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
//
// Returns false when the curse cannot join the master deck. The Java deck is an
// unbounded ArrayList, so kMasterDeckCap is an engine limit, not a game rule:
// the failure is a transaction abort for the enclosing trial-copy rollback (and
// treasure_chest_open_legal preflights it), never a silently dropped curse.
[[nodiscard]] bool cursed_key_obtain(RunState& rs) noexcept {
    const CardId curse = return_random_curse(rs.card_rng);
    return add_card_to_master_deck(rs, curse);
}

// Master-deck slots the non-boss onChestOpen pass will consume: one per Cursed
// Key whose curse Omamori does not block. Mirrors the gate cursed_key_obtain
// applies per key (ShowCardAndObtainEffect.java:31-36): getRelic("Omamori")
// returns the FIRST match, only counter != 0 blocks, and each block spends one
// charge (Omamori.use, Omamori.java:38-46; usedUp leaves counter at 0). So one
// Omamori with counter N blocks the first N keys in acquisition order and then
// stops blocking; a second Omamori is never consulted. Simulated with the same
// first-match lookup and decrement the mutation pass performs, so preflight and
// mutation cannot disagree.
int cursed_key_master_deck_adds(const RunState& rs) noexcept {
    bool has_omamori = false;
    int16_t omamori_counter = 0;
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (static_cast<RelicId>(rs.relics[i].relic_id) ==
            RelicId::OMAMORI) {
            has_omamori = true;
            omamori_counter = rs.relics[i].counter;
            break;
        }
    }
    int adds = 0;
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (static_cast<RelicId>(rs.relics[i].relic_id) !=
            RelicId::CURSED_KEY) {
            continue;
        }
        if (has_omamori && omamori_counter != 0) {
            --omamori_counter;
        } else {
            ++adds;
        }
    }
    return adds;
}

// Preflight for the acquisition-ordered curse obtains against the slots
// actually remaining, so a chest reported legal cannot fail mid-open at the
// deck door. `adds == 0` short-circuits: with no unblocked key the pass never
// touches the deck, so an (unreachable) malformed over-cap count stays the
// no-op it always was.
bool curse_obtains_fit(const RunState& rs) noexcept {
    const int adds = cursed_key_master_deck_adds(rs);
    return adds == 0 ||
           static_cast<int>(rs.master_deck_count) + adds <= kMasterDeckCap;
}

bool dispatch_on_chest_open_impl(RunState& rs, RewardScreen& out,
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
                    if (!add_random_relic_item(rs, out, tier)) {
                        return false;
                    }
                    if (slot.counter == 0) {
                        slot.counter = -2;
                    }
                }
                break;
            case RelicId::CURSED_KEY:
                if (!boss_chest && !cursed_key_obtain(rs)) {
                    return false;
                }
                break;
            default:
                break;
        }
    }
    return true;
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

// The constructible (size, relic_tier) pairs, DERIVED at compile time from the
// single authority treasure_chest_for_rolls by enumerating all 100x100 wrapper
// rolls -- not a second hand-copied table, so it cannot drift from the
// generator. Dimensions cover the full pinned uint8 enum ranges the descriptor
// bytes can index (ChestSize 0..3, RelicTier 0..7).
struct ChestDescriptorDomain {
    bool pair[4][8];
};

constexpr ChestDescriptorDomain derive_chest_descriptor_domain() noexcept {
    ChestDescriptorDomain d{};
    for (int32_t size_roll = 0; size_roll < 100; ++size_roll) {
        for (int32_t contents_roll = 0; contents_roll < 100; ++contents_roll) {
            const TreasureChest c =
                treasure_chest_for_rolls(size_roll, contents_roll);
            d.pair[c.size][c.relic_tier] = true;
        }
    }
    return d;
}

inline constexpr ChestDescriptorDomain kChestDescriptorDomain =
    derive_chest_descriptor_domain();

// Spell out the derived table so a generator edit that moves it fails loudly
// here (the exhaustive test regresses the same property at run time).
constexpr bool chest_pair_valid(ChestSize s, RelicTier t) noexcept {
    return kChestDescriptorDomain
        .pair[static_cast<uint8_t>(s)][static_cast<uint8_t>(t)];
}
static_assert(chest_pair_valid(ChestSize::SMALL, RelicTier::COMMON));
static_assert(chest_pair_valid(ChestSize::SMALL, RelicTier::UNCOMMON));
static_assert(!chest_pair_valid(ChestSize::SMALL, RelicTier::RARE));
static_assert(chest_pair_valid(ChestSize::MEDIUM, RelicTier::COMMON));
static_assert(chest_pair_valid(ChestSize::MEDIUM, RelicTier::UNCOMMON));
static_assert(chest_pair_valid(ChestSize::MEDIUM, RelicTier::RARE));
static_assert(!chest_pair_valid(ChestSize::LARGE, RelicTier::COMMON));
static_assert(chest_pair_valid(ChestSize::LARGE, RelicTier::UNCOMMON));
static_assert(chest_pair_valid(ChestSize::LARGE, RelicTier::RARE));
static_assert(!chest_pair_valid(ChestSize::NONE, RelicTier::COMMON));
static_assert(!chest_pair_valid(ChestSize::SMALL, RelicTier::BOSS));
static_assert(!chest_pair_valid(ChestSize::SMALL, RelicTier::SHOP));

bool exact_unopened_chest_descriptor(const TreasureChest& chest) noexcept {
    if (chest.opened != 0 || chest.has_gold > 1) {
        return false;
    }
    if (chest.size >= std::size(kChestDescriptorDomain.pair) ||
        chest.relic_tier >= std::size(kChestDescriptorDomain.pair[0])) {
        return false;
    }
    return kChestDescriptorDomain.pair[chest.size][chest.relic_tier];
}

bool chest_rewards_fit(const RunState& rs,
                       const TreasureChest& chest) noexcept {
    if (rs.relic_count > kRelicCap) {
        return false;
    }
    int required = 1 + (chest.has_gold != 0 ? 1 : 0);  // base relic + gold
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        const RelicSlot& slot = rs.relics[i];
        if (static_cast<RelicId>(slot.relic_id) == RelicId::MATRYOSHKA &&
            slot.counter > 0) {
            ++required;
        }
    }
    return required <= kRewardItemCap;
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

bool treasure_chest_open_legal(const RunState& rs,
                               const TreasureChest& chest) noexcept {
    return exact_unopened_chest_descriptor(chest) &&
           chest_rewards_fit(rs, chest) && curse_obtains_fit(rs);
}

bool dispatch_relics_on_chest_open(RunState& rs, RewardScreen& out,
                                   bool boss_chest) noexcept {
    if (rs.relic_count > kRelicCap || out.count > kRewardItemCap) {
        return false;
    }

    // The public hook seam can start with an arbitrary reward list and can
    // encounter Cursed Key before a later overflowing Matryoshka. Run the
    // acquisition-ordered Java pass on copies so any late capacity failure
    // rolls back card/relic RNG, pools, counters, deck hooks, and rewards.
    RunState trial_rs = rs;
    RewardScreen trial_out = out;
    if (!dispatch_on_chest_open_impl(trial_rs, trial_out, boss_chest)) {
        return false;
    }
    rs = trial_rs;
    out = trial_out;
    return true;
}

bool dispatch_relics_on_chest_open_after(RunState& rs, RewardScreen& out,
                                         bool boss_chest) noexcept {
    if (rs.relic_count > kRelicCap || out.count > kRewardItemCap) {
        return false;
    }
    RunState trial_rs = rs;
    RewardScreen trial_out = out;
    dispatch_on_chest_open_after_impl(trial_rs, trial_out, boss_chest);
    rs = trial_rs;
    out = trial_out;
    return true;
}

bool open_treasure_chest(RunState& rs, TreasureChest& chest,
                         RewardScreen& out) noexcept {
    if (!treasure_chest_open_legal(rs, chest)) {
        return false;
    }

    // Keep the whole open transaction private until every fixed-capacity
    // insertion succeeds. This also makes an unexpected late failure atomic.
    RunState trial_rs = rs;
    TreasureChest trial_chest = chest;
    RewardScreen trial_out{};
    trial_out.open_card_item = kNoOpenCardReward;

    // AbstractChest.open(false), line order is observable across three streams
    // and the reward list: before-hooks, gold, base relic, after-hooks.
    if (!dispatch_on_chest_open_impl(
            trial_rs, trial_out, /*boss_chest=*/false)) {
        return false;
    }

    if (trial_chest.has_gold != 0) {
        const int base =
            chest_gold_amount(static_cast<ChestSize>(trial_chest.size));
        RunRewardItem* const item = try_push_item(trial_out);
        if (item == nullptr) {
            return false;
        }
        item->kind = static_cast<uint8_t>(RewardItemKind::GOLD);
        item->gold = mathutils_round(random(
            trial_rs.treasure_rng, static_cast<float>(base) * 0.9f,
            static_cast<float>(base) * 1.1f));
        // RewardItem.applyGoldBonus explicitly excludes TreasureRoom, so Golden
        // Idol contributes no bonus here (RewardItem.java:110-129).
        item->bonus_gold = 0;
    }

    if (!add_random_relic_item(
            trial_rs, trial_out,
            static_cast<RelicTier>(trial_chest.relic_tier))) {
        return false;
    }
    dispatch_on_chest_open_after_impl(
        trial_rs, trial_out, /*boss_chest=*/false);
    trial_chest.opened = 1;

    rs = trial_rs;
    chest = trial_chest;
    out = trial_out;
    return true;
}

}  // namespace sts::engine
