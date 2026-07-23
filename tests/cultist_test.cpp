// Cultist AI + monster turn + Ritual Strength ramp (B3.13).
//
// For each of 32 seeds x 20 turns, byte-compares the engine's Cultist
// (include/sts/engine/monster_cultist.*) against an INDEPENDENT hand-derived
// fixture (tests/fixtures/cultist_fixture.tsv, from gen_cultist_fixture.py): the
// HP roll, the per-turn move sequence, and the aiRng/monsterHpRng state after
// every draw. A separate pump-driven test exercises the monster Ritual variant
// (RitualPower onPlayer==false: atEndOfRound Strength with skipFirst) end-to-end.
//
// DRAW-COUNTING (matches monster_cultist.hpp + the generator): cultist_init =
// decision #1 (forced Incantation, 1 aiRng.random(99) draw IGNORED, 1 monsterHpRng
// HP draw). Each cultist_take_turn executes turn K then rolls decision #(K+1): 1
// aiRng.random(99) draw (deterministic tree, no tiebreak booleans).

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/monster_cultist.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

#ifndef STS_FIXTURE_DIR
#error "STS_FIXTURE_DIR must be defined by the build (see tests/CMakeLists.txt)"
#endif

namespace sts::engine {
namespace {

constexpr int kNumTurns = 20;
constexpr uint8_t kMoveDarkStrike = sts::registry::kCultistMoveDarkStrike;    // 1
constexpr uint8_t kMoveIncantation = sts::registry::kCultistMoveIncantation;  // 3

struct TurnRow {
    int turn = 0;
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

std::string FixturePath() {
    return std::string(STS_FIXTURE_DIR) + "/cultist_fixture.tsv";
}

std::vector<SeedCase> LoadFixture() {
    std::vector<SeedCase> cases;
    std::ifstream in(FixturePath());
    if (!in.is_open()) {
        return cases;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream ss(line);
        std::string kind;
        std::getline(ss, kind, '\t');
        if (kind == "seed") {
            SeedCase c;
            std::string value;
            std::getline(ss, c.label, '\t');
            std::getline(ss, value, '\t');
            c.seed = std::stoll(value);
            cases.push_back(std::move(c));
        } else if (kind == "hp") {
            SeedCase& c = cases.back();
            std::string hp, s0, s1, ctr;
            std::getline(ss, hp, '\t');
            std::getline(ss, s0, '\t');
            std::getline(ss, s1, '\t');
            std::getline(ss, ctr, '\t');
            c.hp = std::stoi(hp);
            c.hp_s0 = std::stoull(s0);
            c.hp_s1 = std::stoull(s1);
            c.hp_counter = std::stoi(ctr);
        } else if (kind == "turn") {
            SeedCase& c = cases.back();
            TurnRow r;
            std::string k, mv, s0, s1, ctr;
            std::getline(ss, k, '\t');
            std::getline(ss, mv, '\t');
            std::getline(ss, s0, '\t');
            std::getline(ss, s1, '\t');
            std::getline(ss, ctr, '\t');
            r.turn = std::stoi(k);
            r.move = static_cast<uint8_t>(std::stoi(mv));
            r.ai_s0 = std::stoull(s0);
            r.ai_s1 = std::stoull(s1);
            r.ai_counter = std::stoi(ctr);
            c.turns.push_back(r);
        }
    }
    return cases;
}

CombatState MakeState(int64_t seed) {
    CombatState s{};
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.monster_count = 1;
    s.monster_hp_rng = from_seed(seed);
    s.ai_rng = from_seed(seed);
    return s;
}

const std::vector<SeedCase>& Fixture() {
    static const std::vector<SeedCase> cases = LoadFixture();
    return cases;
}

int16_t monster_power(const CombatState& s, uint8_t mi, PowerId id) {
    const MonsterState& m = s.monsters[mi];
    for (uint8_t i = 0; i < m.power_count; ++i) {
        if (m.powers[i].power_id == static_cast<uint16_t>(id)) {
            return m.powers[i].amount;
        }
    }
    return 0;
}

// --- Tests ------------------------------------------------------------------

TEST(CultistFixture, LoadsThirtyTwoSeeds) {
    const auto& cases = Fixture();
    ASSERT_EQ(cases.size(), 32u)
        << "fixture missing or malformed at " << FixturePath();
    for (const auto& c : cases) {
        EXPECT_EQ(c.turns.size(), static_cast<size_t>(kNumTurns)) << c.label;
    }
}

TEST(CultistInit, ForcedIncantationAndOneDrawEachStream) {
    for (const auto& c : Fixture()) {
        CombatState s = MakeState(c.seed);
        cultist_init(s, 0);
        const MonsterState& m = s.monsters[0];
        EXPECT_EQ(m.monster_id, static_cast<uint16_t>(MonsterId::CULTIST));
        EXPECT_EQ(m.hp, m.max_hp);
        EXPECT_GE(m.hp, 50);       // A7 setHp(50,56) at A20
        EXPECT_LE(m.hp, 56);
        EXPECT_EQ(m.move_history[0], kMoveIncantation) << c.label;  // forced first
        EXPECT_EQ(m.intent, static_cast<uint8_t>(sts::registry::MonsterIntent::BUFF));
        EXPECT_EQ(m.flags, 0u) << c.label;  // ritual-skip not set until Incantation executes
        EXPECT_EQ(s.monster_hp_rng.counter, 1) << c.label;
        EXPECT_EQ(s.ai_rng.counter, 1) << c.label;
    }
}

TEST(CultistFixture, DirectLoopMatchesFixture) {
    for (const auto& c : Fixture()) {
        CombatState s = MakeState(c.seed);
        cultist_init(s, 0);

        EXPECT_EQ(s.monsters[0].hp, c.hp) << c.label;
        EXPECT_EQ(s.monster_hp_rng.s0, c.hp_s0) << c.label;
        EXPECT_EQ(s.monster_hp_rng.s1, c.hp_s1) << c.label;
        EXPECT_EQ(s.monster_hp_rng.counter, c.hp_counter) << c.label;

        for (size_t k = 0; k < c.turns.size(); ++k) {
            const TurnRow& row = c.turns[k];
            const uint8_t executed = s.monsters[0].move_history[0];
            EXPECT_EQ(executed, row.move) << c.label << " turn " << row.turn;
            // Turn 1 is Incantation, all later turns are Dark Strike.
            EXPECT_EQ(executed, (row.turn == 1) ? kMoveIncantation : kMoveDarkStrike)
                << c.label << " turn " << row.turn;

            cultist_take_turn(s, 0);

            EXPECT_EQ(s.ai_rng.s0, row.ai_s0) << c.label << " turn " << row.turn;
            EXPECT_EQ(s.ai_rng.s1, row.ai_s1) << c.label << " turn " << row.turn;
            EXPECT_EQ(s.ai_rng.counter, row.ai_counter)
                << c.label << " turn " << row.turn;
        }
    }
}

// The monster Ritual variant (RitualPower onPlayer==false): applied on turn 1
// (Incantation), it grants +ritualAmount Strength at the END of each ROUND AFTER
// the first (skipFirst). Drive real rounds through the pump and check the ramp.
TEST(CultistPump, RitualStrengthRampsWithSkipFirst) {
    CombatState s = MakeState(/*seed=*/12345);
    cultist_init(s, 0);
    s.player_hp = 30000;   // survive the ramping Dark Strikes
    s.player_max_hp = 30000;
    s.monster_attacks_queued = 1;
    s.turn_has_ended = 0;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);

    // A20 ritualAmount = 5 (base 4 at A2, +1 at A17).
    constexpr int kRitual = 5;

    // Round 1: Cultist casts Incantation (applies Ritual 5). End of round 1 is
    // skipped (skipFirst), so no Strength yet.
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    pump(s, dispatch_monster_turn);
    EXPECT_EQ(monster_power(s, 0, PowerId::RITUAL), kRitual);
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), 0)
        << "skipFirst: no Strength at the end of the round Ritual was applied";
    EXPECT_EQ(s.monsters[0].flags & kMonsterFlagRitualSkip, 0u)
        << "skipFirst flag consumed at end of round 1";

    // Rounds 2..6: +5 Strength each round-end.
    for (int round = 2; round <= 6; ++round) {
        add_card_to_queue_bottom(s, make_end_turn_sentinel());
        pump(s, dispatch_monster_turn);
        EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), kRitual * (round - 1))
            << "round " << round;
        EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER))
            << "round " << round;
    }
}

}  // namespace
}  // namespace sts::engine
