// Tier-2 small/medium slime coverage (B3.14).
//
// Four independent 32-seed x 20-turn fixtures pin HP, move history, full aiRng
// state, and logical draw counters. Focused effect tests pin Damage -> Slimed
// discard ordering, Frail/Weak lick application, and encounter-to-dispatch wiring.

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_slime.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/monster_table.hpp"

#ifndef STS_FIXTURE_DIR
#error "STS_FIXTURE_DIR must be defined by tests/CMakeLists.txt"
#endif

namespace sts::engine {
namespace {

constexpr int kTurns = 20;

struct TurnRow {
    uint8_t move = 0;
    uint64_t ai_s0 = 0;
    uint64_t ai_s1 = 0;
    int32_t ai_counter = 0;
};

struct SeedCase {
    std::string label;
    int64_t seed = 0;
    int hp = 0;
    uint64_t hp_s0 = 0;
    uint64_t hp_s1 = 0;
    int32_t hp_counter = 0;
    std::vector<TurnRow> turns;
};

using InitFn = void (*)(CombatState&, uint8_t);
using TurnFn = void (*)(CombatState&, uint8_t);

std::vector<SeedCase> load_fixture(const std::string& name) {
    std::ifstream in(std::string(STS_FIXTURE_DIR) + "/" + name);
    std::vector<SeedCase> out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string kind;
        std::getline(ss, kind, '\t');
        auto col = [&]() {
            std::string value;
            std::getline(ss, value, '\t');
            return value;
        };
        if (kind == "seed") {
            SeedCase c;
            c.label = col();
            c.seed = std::stoll(col());
            c.hp = std::stoi(col());
            c.hp_s0 = std::stoull(col());
            c.hp_s1 = std::stoull(col());
            c.hp_counter = std::stoi(col());
            out.push_back(std::move(c));
        } else if (kind == "turn") {
            (void)col();  // one-based turn number
            TurnRow row;
            row.move = static_cast<uint8_t>(std::stoi(col()));
            row.ai_s0 = std::stoull(col());
            row.ai_s1 = std::stoull(col());
            row.ai_counter = std::stoi(col());
            out.back().turns.push_back(row);
        }
    }
    return out;
}

CombatState make_state(int64_t seed) {
    CombatState s{};
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.monster_count = 1;
    s.monster_hp_rng = from_seed(seed);
    s.ai_rng = from_seed(seed);
    return s;
}

void drain(CombatState& s) {
    while (s.action_count > 0) {
        const ActionQueueItem item = s.action_queue[s.action_head];
        s.action_head = static_cast<uint8_t>((s.action_head + 1) % kActionQueueCap);
        --s.action_count;
        execute_opcode(s, item);
    }
}

const PowerSlot* player_power(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.player_powers[i];
        }
    }
    return nullptr;
}

void run_fixture(const std::string& file, MonsterId id, InitFn init,
                 TurnFn take_turn, int hp_min, int hp_max) {
    const std::vector<SeedCase> cases = load_fixture(file);
    ASSERT_EQ(cases.size(), 32u) << file;
    bool saw_extra_ai_draw = false;
    for (const SeedCase& c : cases) {
        ASSERT_EQ(c.turns.size(), static_cast<size_t>(kTurns)) << c.label;
        CombatState s = make_state(c.seed);
        init(s, 0);
        EXPECT_EQ(s.monsters[0].monster_id, static_cast<uint16_t>(id));
        EXPECT_EQ(s.monsters[0].hp, c.hp) << c.label;
        EXPECT_EQ(s.monsters[0].max_hp, c.hp) << c.label;
        EXPECT_GE(c.hp, hp_min);
        EXPECT_LE(c.hp, hp_max);
        EXPECT_EQ(s.monster_hp_rng.s0, c.hp_s0) << c.label;
        EXPECT_EQ(s.monster_hp_rng.s1, c.hp_s1) << c.label;
        EXPECT_EQ(s.monster_hp_rng.counter, c.hp_counter) << c.label;
        EXPECT_EQ(s.ai_rng.counter, 1) << c.label;

        int32_t prior_counter = s.ai_rng.counter;
        for (size_t k = 0; k < c.turns.size(); ++k) {
            const TurnRow& row = c.turns[k];
            EXPECT_EQ(s.monsters[0].move_history[0], row.move)
                << c.label << " turn " << (k + 1);
            take_turn(s, 0);
            EXPECT_EQ(s.ai_rng.s0, row.ai_s0) << c.label << " turn " << (k + 1);
            EXPECT_EQ(s.ai_rng.s1, row.ai_s1) << c.label << " turn " << (k + 1);
            EXPECT_EQ(s.ai_rng.counter, row.ai_counter)
                << c.label << " turn " << (k + 1);
            if (row.ai_counter - prior_counter > 1) saw_extra_ai_draw = true;
            prior_counter = row.ai_counter;
            drain(s);  // keep queue capacity bounded; fixture validates AI only
        }
    }
    if (id == MonsterId::ACID_SLIME_MEDIUM) {
        EXPECT_TRUE(saw_extra_ai_draw)
            << "fixture battery must exercise a conditional boolean tie-break";
    }
}

TEST(SlimeFixture, SpikeSmallMatchesIndependentOracle) {
    run_fixture("slime_spike_small_fixture.tsv", MonsterId::SPIKE_SLIME_SMALL,
                &spike_slime_small_init, &spike_slime_small_take_turn, 11, 15);
}

TEST(SlimeFixture, SpikeMediumMatchesIndependentOracle) {
    run_fixture("slime_spike_medium_fixture.tsv", MonsterId::SPIKE_SLIME_MEDIUM,
                &spike_slime_medium_init, &spike_slime_medium_take_turn, 29, 34);
}

TEST(SlimeFixture, AcidSmallMatchesIndependentOracleAndStopsDrawingAi) {
    run_fixture("slime_acid_small_fixture.tsv", MonsterId::ACID_SLIME_SMALL,
                &acid_slime_small_init, &acid_slime_small_take_turn, 9, 13);
}

TEST(SlimeFixture, AcidMediumMatchesIndependentOracle) {
    run_fixture("slime_acid_medium_fixture.tsv", MonsterId::ACID_SLIME_MEDIUM,
                &acid_slime_medium_init, &acid_slime_medium_take_turn, 29, 34);
}

TEST(SlimeEffects, MediumTacklesDamageThenPutSlimedInDiscard) {
    const auto check = [](InitFn init, TurnFn turn, int damage) {
        CombatState s = make_state(1001 + damage);
        init(s, 0);
        set_monster_move(s.monsters[0], 1, MonsterIntent::ATTACK_DEBUFF);
        turn(s, 0);
        ASSERT_EQ(s.action_count, 2);
        const uint8_t first = s.action_head;
        const uint8_t second = static_cast<uint8_t>((first + 1) % kActionQueueCap);
        EXPECT_EQ(s.action_queue[first].opcode,
                  static_cast<uint16_t>(Opcode::DAMAGE));
        EXPECT_EQ(s.action_queue[second].opcode,
                  static_cast<uint16_t>(Opcode::MAKE_CARD));
        EXPECT_EQ(s.action_queue[second].src,
                  static_cast<uint8_t>(CardPile::DISCARD));
        EXPECT_EQ(make_card_id_from_flags(s.action_queue[second].flags),
                  static_cast<uint16_t>(CardId::SLIMED));
        drain(s);
        EXPECT_EQ(s.player_hp, 80 - damage);
        ASSERT_EQ(s.discard_count, 1);
        EXPECT_EQ(s.card_pool[s.discard[0]].card_id,
                  static_cast<uint16_t>(CardId::SLIMED));
        EXPECT_EQ(s.hand_count, 0);
        EXPECT_EQ(s.draw_count, 0);
    };
    check(&spike_slime_medium_init, &spike_slime_medium_take_turn, 10);
    check(&acid_slime_medium_init, &acid_slime_medium_take_turn, 8);
}

TEST(SlimeEffects, LicksApplyFrailAndWeakToPlayer) {
    CombatState spike = make_state(2001);
    spike_slime_medium_init(spike, 0);
    set_monster_move(spike.monsters[0], 4, MonsterIntent::DEBUFF);
    spike_slime_medium_take_turn(spike, 0);
    drain(spike);
    ASSERT_NE(player_power(spike, PowerId::FRAIL), nullptr);
    EXPECT_EQ(player_power(spike, PowerId::FRAIL)->amount, 1);
    EXPECT_NE(spike.flags & kCombatFlagFrailJustApplied, 0u)
        << "monster-sourced new Frail preserves B3.9 justApplied semantics";

    CombatState acid_s = make_state(2002);
    acid_slime_small_init(acid_s, 0);
    set_monster_move(acid_s.monsters[0], 2, MonsterIntent::DEBUFF);
    acid_slime_small_take_turn(acid_s, 0);
    drain(acid_s);
    ASSERT_NE(player_power(acid_s, PowerId::WEAK), nullptr);
    EXPECT_EQ(player_power(acid_s, PowerId::WEAK)->amount, 1);

    CombatState acid_m = make_state(2003);
    acid_slime_medium_init(acid_m, 0);
    set_monster_move(acid_m.monsters[0], 4, MonsterIntent::DEBUFF);
    acid_slime_medium_take_turn(acid_m, 0);
    drain(acid_m);
    ASSERT_NE(player_power(acid_m, PowerId::WEAK), nullptr);
    EXPECT_EQ(player_power(acid_m, PowerId::WEAK)->amount, 1);
}

TEST(SlimeEncounter, SmallAndManySlimeProgramsDispatchImplementedActors) {
    for (const std::string key : {"Small Slimes", "Lots of Slimes"}) {
        CombatState s = make_state(3001);
        RngStream misc = from_seed(3001);
        ResolvedGroup group{};
        ASSERT_TRUE(resolve_encounter(key, misc, group));
        MonsterId ids[kMonsterCap]{};
        for (uint8_t i = 0; i < group.count; ++i) {
            ids[i] = static_cast<MonsterId>(
                sts::registry::monster_from_game_id(group.members[i]));
            EXPECT_NE(ids[i], MonsterId::NONE) << group.members[i];
            EXPECT_NE(monster_init_fn(ids[i]), nullptr) << group.members[i];
            EXPECT_NE(monster_turn_fn(ids[i]), &default_monster_turn)
                << group.members[i];
        }
        spawn_group(s, std::span<const MonsterId>(ids, group.count));
        EXPECT_EQ(s.monster_count, group.count);
        EXPECT_EQ(s.monster_hp_rng.counter, group.count)
            << "every slime ctor performs exactly one HP draw";
        EXPECT_EQ(s.ai_rng.counter, group.count)
            << "every slime init performs exactly one initial random(99) draw";
        use_pre_battle_actions(s);
        EXPECT_EQ(s.action_count, 0) << "slimes have no pre-battle action";
    }
}

}  // namespace
}  // namespace sts::engine
