#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "sts/dist_check/stats.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/map_rooms.hpp"
#include "sts/engine/potions.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/shop.hpp"
#include "sts/engine/treasure_rooms.hpp"

namespace {
using namespace sts::engine;
using sts::dist_check::ChiSquareResult;

constexpr int kMinimumSeeds = 10000;
constexpr int kDefaultSeeds = 20000;
constexpr int kPotionRatchetSteps = 20;
constexpr int kEventPitySteps = 12;
constexpr double kFamilyAlpha = 0.01;

struct Campaign {
    int seeds = kDefaultSeeds;
    std::vector<ChiSquareResult> tests;
    std::vector<std::string> exact_failures;
};

template <typename T, std::size_t N>
int index_of(const std::array<T, N>& values, const T& value) {
    const auto it = std::find(values.begin(), values.end(), value);
    return it == values.end() ? -1 : static_cast<int>(it - values.begin());
}

void exact(Campaign& c, bool condition, std::string message) {
    if (!condition &&
        std::find(c.exact_failures.begin(), c.exact_failures.end(), message) ==
            c.exact_failures.end()) {
        c.exact_failures.push_back(std::move(message));
    }
}

void add_test(Campaign& c, std::string name,
              const std::vector<uint64_t>& observed,
              const std::vector<double>& probabilities) {
    c.tests.push_back(sts::dist_check::chi_square(
        std::move(name), observed, probabilities));
}

void encounter_weight_checks(Campaign& c) {
    constexpr std::array<std::string_view, 4> weak = {
        "Cultist", "Jaw Worm", "2 Louse", "Small Slimes"};
    constexpr std::array<std::string_view, 10> strong = {
        "Blue Slaver", "Gremlin Gang", "Looter", "Large Slime",
        "Lots of Slimes", "Exordium Thugs", "Exordium Wildlife",
        "Red Slaver", "3 Louse", "2 Fungi Beasts"};
    constexpr std::array<double, 10> strong_weight = {
        2.0, 1.0, 2.0, 2.0, 1.0, 1.5, 1.5, 1.0, 2.0, 2.0};
    constexpr std::array<std::string_view, 3> elite = {
        "Gremlin Nob", "Lagavulin", "3 Sentries"};
    constexpr std::array<std::string_view, 3> boss = {
        "The Guardian", "Hexaghost", "Slime Boss"};

    std::vector<uint64_t> weak_observed(weak.size());
    std::vector<uint64_t> strong_observed(strong.size());
    std::vector<uint64_t> elite_observed(elite.size());
    std::vector<uint64_t> boss_observed(boss.size());

    for (int i = 0; i < c.seeds; ++i) {
        RngStream rng = from_seed(static_cast<int64_t>(i) + 1);
        MonsterLists lists{};
        generate_monster_lists(1, rng, lists);
        exact(c, lists.monster_list_count == kMaxMonsterList,
              "encounter list did not contain 3 weak + 13 strong rows");
        exact(c, lists.elite_list_count == kMaxEliteList,
              "elite list did not contain 10 rows");
        exact(c, lists.boss_list_count == kMaxBossList,
              "boss list did not contain 3 rows");
        const int wi = index_of(weak, lists.monster_list[0]);
        const int si = index_of(strong, lists.monster_list[3]);
        const int ei = index_of(elite, lists.elite_list[0]);
        const int bi = index_of(boss, lists.boss_list[0]);
        exact(c, wi >= 0 && si >= 0 && ei >= 0 && bi >= 0,
              "generated encounter key fell outside cited Exordium tables");
        if (wi >= 0) ++weak_observed[static_cast<std::size_t>(wi)];
        if (si >= 0) ++strong_observed[static_cast<std::size_t>(si)];
        if (ei >= 0) ++elite_observed[static_cast<std::size_t>(ei)];
        if (bi >= 0) ++boss_observed[static_cast<std::size_t>(bi)];
    }

    add_test(c, "encounter.weak_first", weak_observed,
             std::vector<double>(weak.size(), 1.0 / 4.0));

    // First-strong expectation: the third weak is symmetric over its four
    // equal-weight rows. "2 Louse" rejects 3 Louse (weight 2); "Small Slimes"
    // rejects Large Slime (2) and Lots of Slimes (1); the other two reject
    // nothing. AbstractDungeon.populateFirstStrongEnemy re-rolls from the same
    // normalized pool, so conditional probabilities are weight/(16-excluded).
    std::vector<double> first_strong_probability(strong.size(), 0.0);
    for (int prior = 0; prior < 4; ++prior) {
        double allowed_weight = 0.0;
        for (std::size_t j = 0; j < strong.size(); ++j) {
            const bool excluded =
                (prior == 2 && strong[j] == "3 Louse") ||
                (prior == 3 &&
                 (strong[j] == "Large Slime" ||
                  strong[j] == "Lots of Slimes"));
            if (!excluded) allowed_weight += strong_weight[j];
        }
        for (std::size_t j = 0; j < strong.size(); ++j) {
            const bool excluded =
                (prior == 2 && strong[j] == "3 Louse") ||
                (prior == 3 &&
                 (strong[j] == "Large Slime" ||
                  strong[j] == "Lots of Slimes"));
            if (!excluded) {
                first_strong_probability[j] +=
                    0.25 * strong_weight[j] / allowed_weight;
            }
        }
    }
    add_test(c, "encounter.first_strong_with_exclusions", strong_observed,
             first_strong_probability);
    add_test(c, "encounter.elite_first", elite_observed,
             std::vector<double>(elite.size(), 1.0 / 3.0));
    add_test(c, "encounter.boss_first", boss_observed,
             std::vector<double>(boss.size(), 1.0 / 3.0));
}

int count_member(const ResolvedGroup& group, std::string_view game_id) {
    return static_cast<int>(std::count(
        group.members.begin(), group.members.begin() + group.count, game_id));
}

void composition_checks(Campaign& c) {
    std::vector<uint64_t> large(2);
    std::vector<uint64_t> small(2);
    std::vector<uint64_t> louse2(3);
    std::vector<uint64_t> louse3(4);
    std::vector<uint64_t> lots(10);
    constexpr std::array<int, 10> three_spike_masks = {
        7, 11, 13, 14, 19, 21, 22, 25, 26, 28};

    for (int i = 0; i < c.seeds; ++i) {
        const int64_t seed = static_cast<int64_t>(i) + 0x10001;
        ResolvedGroup group{};

        RngStream rng = from_seed(seed);
        exact(c, resolve_encounter("Large Slime", rng, group),
              "Large Slime encounter missing");
        const bool acid_large = group.count == 1 &&
                                group.members[0] == "AcidSlime_L";
        const bool spike_large = group.count == 1 &&
                                 group.members[0] == "SpikeSlime_L";
        exact(c, acid_large || spike_large,
              "Large Slime composition outside its two variants");
        if (acid_large || spike_large) ++large[acid_large ? 0u : 1u];

        rng = from_seed(seed + 0x10000);
        exact(c, resolve_encounter("Small Slimes", rng, group),
              "Small Slimes encounter missing");
        const bool spike_acid =
            group.count == 2 && group.members[0] == "SpikeSlime_S" &&
            group.members[1] == "AcidSlime_M";
        const bool acid_spike =
            group.count == 2 && group.members[0] == "AcidSlime_S" &&
            group.members[1] == "SpikeSlime_M";
        exact(c, spike_acid || acid_spike,
              "Small Slimes composition outside its two sequences");
        if (spike_acid || acid_spike) ++small[spike_acid ? 0u : 1u];

        rng = from_seed(seed + 0x20000);
        exact(c, resolve_encounter("2 Louse", rng, group),
              "2 Louse encounter missing");
        const int normals2 = count_member(group, "FuzzyLouseNormal");
        exact(c, group.count == 2 && normals2 >= 0 && normals2 <= 2 &&
                     normals2 +
                             count_member(group, "FuzzyLouseDefensive") ==
                         2,
              "2 Louse emitted a non-louse member");
        if (normals2 >= 0 && normals2 <= 2) {
            ++louse2[static_cast<std::size_t>(normals2)];
        }

        rng = from_seed(seed + 0x30000);
        exact(c, resolve_encounter("3 Louse", rng, group),
              "3 Louse encounter missing");
        const int normals3 = count_member(group, "FuzzyLouseNormal");
        exact(c, group.count == 3 && normals3 >= 0 && normals3 <= 3 &&
                     normals3 +
                             count_member(group, "FuzzyLouseDefensive") ==
                         3,
              "3 Louse emitted a non-louse member");
        if (normals3 >= 0 && normals3 <= 3) {
            ++louse3[static_cast<std::size_t>(normals3)];
        }

        rng = from_seed(seed + 0x40000);
        exact(c, resolve_encounter("Lots of Slimes", rng, group),
              "Lots of Slimes encounter missing");
        int mask = 0;
        bool valid = group.count == 5;
        for (int j = 0; j < group.count; ++j) {
            if (group.members[static_cast<std::size_t>(j)] == "SpikeSlime_S") {
                mask |= 1 << j;
            } else if (group.members[static_cast<std::size_t>(j)] !=
                       "AcidSlime_S") {
                valid = false;
            }
        }
        const int mi = index_of(three_spike_masks, mask);
        exact(c, valid && mi >= 0,
              "Lots of Slimes was not a permutation of three Spike/two Acid");
        if (valid && mi >= 0) ++lots[static_cast<std::size_t>(mi)];
    }

    add_test(c, "composition.large_slime", large, {0.5, 0.5});
    add_test(c, "composition.small_slimes", small, {0.5, 0.5});
    add_test(c, "composition.two_louse", louse2, {0.25, 0.5, 0.25});
    add_test(c, "composition.three_louse", louse3,
             {0.125, 0.375, 0.375, 0.125});
    add_test(c, "composition.lots_of_slimes", lots,
             std::vector<double>(lots.size(), 0.1));
}

RewardCardRarity card_rarity_from_id(CardId id) {
    for (const CardId candidate : sts::registry::kIroncladCommonPool) {
        if (candidate == id) return RewardCardRarity::COMMON;
    }
    for (const CardId candidate : sts::registry::kIroncladUncommonPool) {
        if (candidate == id) return RewardCardRarity::UNCOMMON;
    }
    for (const CardId candidate : sts::registry::kIroncladRarePool) {
        if (candidate == id) return RewardCardRarity::RARE;
    }
    return RewardCardRarity::COMMON;  // caller separately records exact failure
}

bool card_id_is_reward_pool_member(CardId id) {
    for (const CardId candidate : sts::registry::kIroncladCommonPool) {
        if (candidate == id) return true;
    }
    for (const CardId candidate : sts::registry::kIroncladUncommonPool) {
        if (candidate == id) return true;
    }
    for (const CardId candidate : sts::registry::kIroncladRarePool) {
        if (candidate == id) return true;
    }
    return false;
}

std::vector<double> rarity_pity_expectation() {
    std::map<int, double> state{{kCardBlizzStartOffset, 1.0}};
    std::vector<double> cells(3 * 3, 0.0);
    for (int step = 0; step < 3; ++step) {
        std::map<int, double> next;
        for (const auto& [pity, state_probability] : state) {
            for (int roll = 0; roll < 100; ++roll) {
                const RewardCardRarity rarity =
                    reward_card_rarity(roll + pity, RoomType::Monster);
                const int ri = static_cast<int>(rarity);
                cells[static_cast<std::size_t>(step * 3 + ri)] +=
                    state_probability / 100.0;
                int new_pity = pity;
                if (rarity == RewardCardRarity::RARE) {
                    new_pity = kCardBlizzStartOffset;
                } else if (rarity == RewardCardRarity::COMMON) {
                    new_pity = std::max(kCardBlizzMaxOffset,
                                        pity - kCardBlizzGrowth);
                }
                next[new_pity] += state_probability / 100.0;
            }
        }
        state = std::move(next);
    }
    for (double& p : cells) p /= 3.0;
    return cells;
}

std::vector<double> potion_ratchet_expectation() {
    std::map<int, double> state{{0, 1.0}};
    std::vector<double> cells(kPotionRatchetSteps * 2, 0.0);
    for (int step = 0; step < kPotionRatchetSteps; ++step) {
        std::map<int, double> next;
        for (const auto& [modifier, state_probability] : state) {
            const int hits = std::clamp(kBasePotionDropChance + modifier, 0, 100);
            const double hit_probability = static_cast<double>(hits) / 100.0;
            cells[static_cast<std::size_t>(step * 2)] +=
                state_probability * (1.0 - hit_probability);
            cells[static_cast<std::size_t>(step * 2 + 1)] +=
                state_probability * hit_probability;
            next[modifier + kBlizzardPotionModStep] +=
                state_probability * (1.0 - hit_probability);
            next[modifier - kBlizzardPotionModStep] +=
                state_probability * hit_probability;
        }
        state = std::move(next);
    }
    for (double& p : cells) p /= static_cast<double>(kPotionRatchetSteps);
    return cells;
}

void reward_dynamics_checks(Campaign& c) {
    std::vector<uint64_t> rarity(3 * 3);
    std::vector<uint64_t> potion_ratchet(kPotionRatchetSteps * 2);

    for (int i = 0; i < c.seeds; ++i) {
        const int64_t seed = static_cast<int64_t>(i) + 0x50001;
        RunState rs{};
        rs.run_seed = seed;
        rs.card_rng = from_seed(seed);
        rs.potion_rng = from_seed(seed + 0x100000);
        rs.treasure_rng = from_seed(seed + 0x200000);
        rs.card_blizz_randomizer = kCardBlizzStartOffset;
        rs.blizzard_potion_mod = 0;
        RngStream misc = from_seed(seed + 1);

        for (int step = 0; step < kPotionRatchetSteps; ++step) {
            RewardScreen rewards{};
            assemble_combat_rewards(rs, misc, RoomType::Monster,
                                    RewardOutcome::KILLED, rewards);
            bool potion = false;
            const RunRewardItem* cards = nullptr;
            for (int j = 0; j < rewards.count; ++j) {
                const auto kind =
                    static_cast<RewardItemKind>(rewards.items[j].kind);
                potion = potion || kind == RewardItemKind::POTION;
                if (kind == RewardItemKind::CARDS) cards = &rewards.items[j];
            }
            ++potion_ratchet[static_cast<std::size_t>(
                step * 2 + (potion ? 1 : 0))];

            if (step == 0) {
                exact(c, cards != nullptr && cards->card_count == 3,
                      "normal combat reward did not contain three card offers");
                if (cards != nullptr) {
                    for (int k = 0; k < cards->card_count; ++k) {
                        const CardId id =
                            static_cast<CardId>(cards->card_ids[k]);
                        exact(c, card_id_is_reward_pool_member(id),
                              "card reward identity not in a cited red rarity pool");
                        const int category =
                            static_cast<int>(card_rarity_from_id(id));
                        ++rarity[static_cast<std::size_t>(k * 3 + category)];
                    }
                }
            }
        }
    }

    add_test(c, "reward.card_rarity_pity", rarity,
             rarity_pity_expectation());
    add_test(c, "reward.potion_drop_ratchet", potion_ratchet,
             potion_ratchet_expectation());
}

int chest_cell(const TreasureChest& chest) {
    const int size = static_cast<int>(chest.size) - 1;
    int tier = -1;
    switch (static_cast<RelicTier>(chest.relic_tier)) {
        case RelicTier::COMMON: tier = 0; break;
        case RelicTier::UNCOMMON: tier = 1; break;
        case RelicTier::RARE: tier = 2; break;
        default: break;
    }
    if (size < 0 || size >= 3 || tier < 0) return -1;
    return (size * 3 + tier) * 2 + static_cast<int>(chest.has_gold);
}

TreasureChest reference_chest(int size_roll, int contents_roll) {
    TreasureChest out{};
    if (size_roll < 50) {
        out.size = static_cast<uint8_t>(ChestSize::SMALL);
        out.has_gold = static_cast<uint8_t>(contents_roll < 50);
        out.relic_tier = static_cast<uint8_t>(
            contents_roll < 75 ? RelicTier::COMMON : RelicTier::UNCOMMON);
    } else if (size_roll < 83) {
        out.size = static_cast<uint8_t>(ChestSize::MEDIUM);
        out.has_gold = static_cast<uint8_t>(contents_roll < 35);
        out.relic_tier = static_cast<uint8_t>(
            contents_roll < 35
                ? RelicTier::COMMON
                : (contents_roll < 85 ? RelicTier::UNCOMMON : RelicTier::RARE));
    } else {
        out.size = static_cast<uint8_t>(ChestSize::LARGE);
        out.has_gold = static_cast<uint8_t>(contents_roll < 50);
        out.relic_tier = static_cast<uint8_t>(
            contents_roll < 75 ? RelicTier::UNCOMMON : RelicTier::RARE);
    }
    return out;
}

void chest_check(Campaign& c) {
    std::vector<uint64_t> observed(18);
    std::vector<double> probability(18);
    for (int size_roll = 0; size_roll < 100; ++size_roll) {
        for (int contents_roll = 0; contents_roll < 100; ++contents_roll) {
            const TreasureChest engine =
                treasure_chest_for_rolls(size_roll, contents_roll);
            const TreasureChest reference =
                reference_chest(size_roll, contents_roll);
            exact(c, engine.size == reference.size &&
                         engine.relic_tier == reference.relic_tier &&
                         engine.has_gold == reference.has_gold,
                  "chest threshold table disagreed with cited constructor tables");
            const int cell = chest_cell(reference);
            exact(c, cell >= 0 && cell < static_cast<int>(probability.size()),
                  "reference chest descriptor outside cited support");
            if (cell >= 0 && cell < static_cast<int>(probability.size())) {
                probability[static_cast<std::size_t>(cell)] += 1.0 / 10000.0;
            }
        }
    }
    for (int i = 0; i < c.seeds; ++i) {
        RunState rs{};
        rs.treasure_rng =
            from_seed(static_cast<int64_t>(i) + 0x60001);
        const TreasureChest chest = roll_treasure_chest(rs);
        const int cell = chest_cell(chest);
        exact(c, cell >= 0 && cell < static_cast<int>(observed.size()),
              "rolled chest descriptor outside cited support");
        if (cell >= 0 && cell < static_cast<int>(observed.size())) {
            ++observed[static_cast<std::size_t>(cell)];
        }
    }
    add_test(c, "chest.joint_size_gold_relic", observed, probability);
}

using PityKey = std::tuple<uint32_t, uint32_t, uint32_t>;

PityKey pity_key(float monster, float shop, float treasure) {
    return {std::bit_cast<uint32_t>(monster), std::bit_cast<uint32_t>(shop),
            std::bit_cast<uint32_t>(treasure)};
}

std::tuple<float, float, float> pity_values(const PityKey& key) {
    return {std::bit_cast<float>(std::get<0>(key)),
            std::bit_cast<float>(std::get<1>(key)),
            std::bit_cast<float>(std::get<2>(key))};
}

std::array<int, 4> reference_event_slot_counts(float monster, float shop,
                                                float treasure) {
    std::array<int, 100> table{};
    table.fill(static_cast<int>(EventRoomResult::EVENT));
    int fill = 0;
    const auto paint = [&](int size, EventRoomResult result, int& cursor) {
        const int begin = std::min(99, cursor);
        const int end = std::min(100, cursor + size);
        for (int i = begin; i < end; ++i) {
            table[static_cast<std::size_t>(i)] = static_cast<int>(result);
        }
        cursor += size;
    };
    paint(static_cast<int>(monster * 100.0f), EventRoomResult::MONSTER, fill);
    paint(static_cast<int>(shop * 100.0f), EventRoomResult::SHOP, fill);
    paint(static_cast<int>(treasure * 100.0f), EventRoomResult::TREASURE, fill);
    std::array<int, 4> counts{};
    for (int result : table) {
        ++counts[static_cast<std::size_t>(result)];
    }
    return counts;
}

std::vector<double> event_pity_expectation() {
    std::map<PityKey, double> state{
        {pity_key(kEventBaseMonsterChance, kEventBaseShopChance,
                  kEventBaseTreasureChance),
         1.0}};
    std::vector<double> cells(kEventPitySteps * 4, 0.0);
    for (int step = 0; step < kEventPitySteps; ++step) {
        std::map<PityKey, double> next;
        for (const auto& [key, state_probability] : state) {
            const auto [monster, shop, treasure] = pity_values(key);
            const auto counts =
                reference_event_slot_counts(monster, shop, treasure);
            for (int ri = 0; ri < 4; ++ri) {
                if (counts[static_cast<std::size_t>(ri)] == 0) continue;
                const double transition =
                    state_probability *
                    static_cast<double>(counts[static_cast<std::size_t>(ri)]) /
                    100.0;
                cells[static_cast<std::size_t>(step * 4 + ri)] += transition;
                const auto result = static_cast<EventRoomResult>(ri);
                const float next_monster =
                    result == EventRoomResult::MONSTER
                        ? kEventBaseMonsterChance
                        : monster + kEventRampMonsterChance;
                const float next_shop =
                    result == EventRoomResult::SHOP
                        ? kEventBaseShopChance
                        : shop + kEventRampShopChance;
                const float next_treasure =
                    result == EventRoomResult::TREASURE
                        ? kEventBaseTreasureChance
                        : treasure + kEventRampTreasureChance;
                next[pity_key(next_monster, next_shop, next_treasure)] +=
                    transition;
            }
        }
        state = std::move(next);
    }
    for (double& p : cells) p /= static_cast<double>(kEventPitySteps);
    return cells;
}

void event_pity_check(Campaign& c) {
    std::vector<uint64_t> observed(kEventPitySteps * 4);
    for (int i = 0; i < c.seeds; ++i) {
        RunState rs{};
        rs.event_rng = from_seed(static_cast<int64_t>(i) + 0x70001);
        rs.event_pity_monster = kEventBaseMonsterChance;
        rs.event_pity_shop = kEventBaseShopChance;
        rs.event_pity_treasure = kEventBaseTreasureChance;
        for (int step = 0; step < kEventPitySteps; ++step) {
            const auto result = event_room_roll(rs, false);
            const int ri = static_cast<int>(result);
            exact(c, ri >= 0 && ri < 4,
                  "?-room roll returned an out-of-scope result");
            if (ri >= 0 && ri < 4) {
                ++observed[static_cast<std::size_t>(step * 4 + ri)];
            }
        }
    }
    add_test(c, "event.question_room_pity", observed,
             event_pity_expectation());
}

void tier_roll_checks(Campaign& c) {
    const auto relic_category = [](RelicTier tier) {
        switch (tier) {
            case RelicTier::COMMON: return 0;
            case RelicTier::UNCOMMON: return 1;
            case RelicTier::RARE: return 2;
            default: return -1;
        }
    };
    const auto potion_category = [](PotionRarity rarity) {
        switch (rarity) {
            case PotionRarity::COMMON: return 0;
            case PotionRarity::UNCOMMON: return 1;
            case PotionRarity::RARE: return 2;
        }
        return -1;
    };

    std::vector<uint64_t> relic(3);
    std::vector<uint64_t> shop_relic(3);
    std::vector<uint64_t> potion(3);
    for (int i = 0; i < c.seeds; ++i) {
        const int64_t seed = static_cast<int64_t>(i) + 0x80001;
        RunState rs{};
        rs.relic_rng = from_seed(seed);
        const int r = relic_category(return_random_relic_tier(rs));
        exact(c, r >= 0 && r < 3, "reward relic tier outside C/U/R");
        if (r >= 0 && r < 3) ++relic[static_cast<std::size_t>(r)];

        RngStream merchant = from_seed(seed + 0x10000);
        const int sr =
            relic_category(shop_relic_tier_for_roll(random(merchant, 99)));
        exact(c, sr >= 0 && sr < 3, "shop relic tier outside C/U/R");
        if (sr >= 0 && sr < 3) ++shop_relic[static_cast<std::size_t>(sr)];

        RngStream potion_rng = from_seed(seed + 0x20000);
        const PotionId potion_id = return_random_potion(potion_rng);
        const PotionDef* def = potion_def(potion_id);
        exact(c, def != nullptr, "potion tier roll returned an unknown identity");
        if (def != nullptr) {
            const int pr = potion_category(def->rarity);
            exact(c, pr >= 0 && pr < 3, "potion rarity outside C/U/R");
            if (pr >= 0 && pr < 3) ++potion[static_cast<std::size_t>(pr)];
        }
    }
    add_test(c, "tier.relic_reward", relic, {0.50, 0.33, 0.17});
    add_test(c, "tier.relic_shop", shop_relic, {0.48, 0.34, 0.18});
    add_test(c, "tier.potion_65_25_10", potion, {0.65, 0.25, 0.10});
}

int java_round_positive(float value) {
    return static_cast<int>(std::floor(value + 0.5f));
}

void map_quota_checks(Campaign& c) {
    for (int i = 0; i < c.seeds; ++i) {
        const int64_t seed = static_cast<int64_t>(i) + 0x90001;
        const RoomAssignment rooms = generate_map_rooms(seed, 1, 20);
        const int count = rooms.available_room_count;
        RoomQuota expected{};
        expected.shop = java_round_positive(static_cast<float>(count) * 0.05f);
        expected.rest = java_round_positive(static_cast<float>(count) * 0.12f);
        expected.treasure =
            java_round_positive(static_cast<float>(count) * 0.0f);
        expected.elite = java_round_positive(
            static_cast<float>(count) * 0.08f * 1.6f);
        expected.event = java_round_positive(static_cast<float>(count) * 0.22f);
        expected.monster = count - expected.shop - expected.rest -
                           expected.treasure - expected.elite - expected.event;
        exact(c, rooms.quota.shop == expected.shop &&
                     rooms.quota.rest == expected.rest &&
                     rooms.quota.treasure == expected.treasure &&
                     rooms.quota.elite == expected.elite &&
                     rooms.quota.event == expected.event &&
                     rooms.quota.monster == expected.monster,
              "map quota disagreed with Java Math.round table");

        for (int x = 0; x < kGameMapCols; ++x) {
            exact(c, rooms.at(x, kMonsterRow) == RoomType::Monster &&
                         rooms.at(x, kTreasureRow) == RoomType::Treasure &&
                         rooms.at(x, kRestRow) == RoomType::Rest,
                  "map fixed-row assignment disagreed with Java");
        }
    }
}

void print_usage() {
    std::cerr << "usage: dist_check [--seeds N]\n";
}

bool parse_args(int argc, char** argv, Campaign& campaign) {
    bool saw_seeds = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--seeds" && i + 1 < argc) {
            if (saw_seeds) {
                std::cerr << "dist_check: repeated --seeds\n";
                return false;
            }
            saw_seeds = true;
            const std::string_view value(argv[++i]);
            int parsed = 0;
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (error != std::errc{} || end != value.data() + value.size()) {
                std::cerr << "dist_check: invalid --seeds value\n";
                return false;
            }
            campaign.seeds = parsed;
        } else if (arg == "--help") {
            print_usage();
            return false;
        } else {
            print_usage();
            return false;
        }
    }
    if (campaign.seeds < kMinimumSeeds) {
        std::cerr << "dist_check: --seeds must be >= " << kMinimumSeeds
                  << " (tier-4 frozen scope)\n";
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Campaign campaign;
    if (!parse_args(argc, argv, campaign)) return 2;

    encounter_weight_checks(campaign);
    composition_checks(campaign);
    reward_dynamics_checks(campaign);
    chest_check(campaign);
    event_pity_check(campaign);
    tier_roll_checks(campaign);
    map_quota_checks(campaign);

    const auto decisions =
        sts::dist_check::holm_bonferroni(campaign.tests, kFamilyAlpha);
    bool flagged = !campaign.exact_failures.empty();
    std::cout << "dist_check seeds=" << campaign.seeds
              << " stochastic_hypotheses=" << campaign.tests.size()
              << " family_alpha=" << kFamilyAlpha
              << " correction=Holm-Bonferroni\n";
    std::cout << std::scientific << std::setprecision(6);
    for (const auto& decision : decisions) {
        const auto it = std::find_if(
            campaign.tests.begin(), campaign.tests.end(),
            [&](const ChiSquareResult& test) {
                return test.name == decision.name;
            });
        std::cout << (decision.rejected ? "FLAG " : "PASS ")
                  << decision.name << " n=" << it->sample_count
                  << " chi2=" << it->statistic
                  << " df=" << it->degrees_of_freedom
                  << " p=" << decision.p_value
                  << " holm=" << decision.threshold << '\n';
        flagged = flagged || decision.rejected;
    }
    for (const std::string& failure : campaign.exact_failures) {
        std::cout << "FLAG exact: " << failure << '\n';
    }
    if (flagged) {
        std::cout << "RESULT FLAGGED -- stop-the-line divergence\n";
        return 1;
    }
    std::cout << "RESULT PASS\n";
    return 0;
}
