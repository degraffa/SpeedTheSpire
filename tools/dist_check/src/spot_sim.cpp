#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/map_rooms.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/fuzz/fuzz_run.hpp"
#include "sts/registry/game_ids.hpp"

namespace {

using namespace sts::engine;

struct Aggregate {
    uint64_t seeds = 0;
    uint64_t clean = 0;
    std::map<std::string, uint64_t> encounters;
    std::map<std::string, uint64_t> reward_rarity;
    std::map<std::string, uint64_t> event_outcomes;
};

struct RunObservation {
    Aggregate* aggregate = nullptr;
    uint8_t previous_phase = 0xFF;
    uint16_t event_floor_seen = 0;
};

std::string reward_rarity_name(CardId id) {
    for (const CardId candidate : sts::registry::kIroncladCommonPool) {
        if (candidate == id) return "COMMON";
    }
    for (const CardId candidate : sts::registry::kIroncladUncommonPool) {
        if (candidate == id) return "UNCOMMON";
    }
    for (const CardId candidate : sts::registry::kIroncladRarePool) {
        if (candidate == id) return "RARE";
    }
    return "UNKNOWN";
}

std::string encounter_signature(const RunController& rc) {
    std::vector<std::string> ids;
    ids.reserve(rc.combat.monster_count);
    for (int i = 0; i < rc.combat.monster_count; ++i) {
        const auto id = static_cast<sts::registry::MonsterId>(
            rc.combat.monsters[i].monster_id);
        ids.emplace_back(sts::registry::monster_game_id(id));
    }
    std::sort(ids.begin(), ids.end());
    std::string signature;
    for (const std::string& id : ids) {
        if (!signature.empty()) signature.push_back('+');
        signature += id;
    }
    return signature.empty() ? "EMPTY" : signature;
}

void observe(const RunController& rc, void* opaque) noexcept {
    auto& observation = *static_cast<RunObservation*>(opaque);
    Aggregate& aggregate = *observation.aggregate;
    const auto phase = static_cast<RunPhase>(rc.phase);
    const bool phase_entry = rc.phase != observation.previous_phase;

    if (phase_entry && phase == RunPhase::COMBAT) {
        ++aggregate.encounters[encounter_signature(rc)];
    }

    if (phase_entry && phase == RunPhase::COMBAT_REWARD) {
        for (int i = 0; i < rc.rewards.count; ++i) {
            const RunRewardItem& item = rc.rewards.items[i];
            if (static_cast<RewardItemKind>(item.kind) != RewardItemKind::CARDS) {
                continue;
            }
            for (int j = 0; j < item.card_count; ++j) {
                const CardId id = static_cast<CardId>(item.card_ids[j]);
                ++aggregate.reward_rarity[reward_rarity_name(id)];
            }
        }
    }

    RoomType map_room = RoomType::None;
    if (rc.run.floor > 0 && rc.run.floor <= kMapRows &&
        rc.cur_x < kMapCols) {
        const int y = static_cast<int>(rc.run.floor) - 1;
        map_room = static_cast<RoomType>(
            rc.run.map[run_state_map_index(rc.cur_x, y)].room_type);
    }
    if (map_room == RoomType::Event &&
        observation.event_floor_seen != rc.run.floor) {
        std::string_view outcome;
        switch (phase) {
            case RunPhase::COMBAT: outcome = "MONSTER"; break;
            case RunPhase::SHOP: outcome = "SHOP"; break;
            case RunPhase::TREASURE_ROOM: outcome = "TREASURE"; break;
            case RunPhase::EVENT_DIALOG:
            case RunPhase::ROOM_UNIMPLEMENTED: outcome = "EVENT"; break;
            default: break;
        }
        if (!outcome.empty()) {
            ++aggregate.event_outcomes[std::string(outcome)];
            observation.event_floor_seen = rc.run.floor;
        }
    }

    observation.previous_phase = rc.phase;
}

void print_json_string(std::string_view value) {
    std::cout << '"';
    for (const char ch : value) {
        if (ch == '"' || ch == '\\') std::cout << '\\';
        std::cout << ch;
    }
    std::cout << '"';
}

void print_counts(const std::map<std::string, uint64_t>& counts) {
    std::cout << '{';
    bool first = true;
    for (const auto& [name, count] : counts) {
        if (!first) std::cout << ',';
        first = false;
        print_json_string(name);
        std::cout << ':' << count;
    }
    std::cout << '}';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 || std::string_view(argv[1]) != "--seed-file") {
        std::cerr << "usage: dist_check_spot_sim --seed-file PATH\n";
        return 2;
    }

    std::ifstream input(argv[2]);
    if (!input) {
        std::cerr << "cannot open seed file: " << argv[2] << '\n';
        return 2;
    }

    Aggregate aggregate;
    int64_t seed = 0;
    while (input >> seed) {
        ++aggregate.seeds;
        sts::fuzz::CaseId id;
        id.run_seed = seed;
        id.ascension = 20;
        id.policy = sts::fuzz::PolicyKind::RANDOM;
        id.policy_seed = static_cast<uint64_t>(seed) ^
                         UINT64_C(0xB53A20D1571234);

        sts::fuzz::RunLimits limits;
        limits.max_actions = 3000;
        limits.revisit_limit = 512;
        limits.fail_on_livelock = false;
        sts::fuzz::CaseResult result;
        RunObservation observation{&aggregate};
        const sts::fuzz::StepObserver observer{observe, &observation};
        if (!sts::fuzz::run_case(id, limits, nullptr, result,
                                 false, sts::fuzz::Inject{}, observer)) {
            std::cerr << "sim run failed seed=" << seed
                      << " action=" << result.failure.step
                      << " kind="
                      << sts::fuzz::fail_kind_name(result.failure.kind)
                      << " phase="
                      << static_cast<unsigned>(result.failure.phase) << '\n';
            return 1;
        }
        if (result.end_reason != sts::fuzz::EndReason::RUN_OVER) {
            std::cerr << "sim run was not a full Act-1 terminal seed=" << seed
                      << " end_reason="
                      << sts::fuzz::end_reason_name(result.end_reason) << '\n';
            return 1;
        }
        ++aggregate.clean;
    }

    std::cout << "{\"seeds\":" << aggregate.seeds
              << ",\"clean\":" << aggregate.clean << ",\"encounters\":";
    print_counts(aggregate.encounters);
    std::cout << ",\"reward_rarity\":";
    print_counts(aggregate.reward_rarity);
    std::cout << ",\"event_outcomes\":";
    print_counts(aggregate.event_outcomes);
    std::cout << "}\n";
    return aggregate.seeds >= 200 && aggregate.clean == aggregate.seeds ? 0 : 1;
}
