// Jaw Worm AI + monster turn (design doc §9).
//
// For each of 32 seeds x 20 turns, byte-compares the engine's Jaw Worm
// (include/sts/engine/monster_jaw_worm.*) against an INDEPENDENT hand-derived
// fixture (tests/fixtures/jaw_worm_fixture.tsv, produced by the Python oracle
// gen_jaw_worm_fixture.py): the HP roll, the per-turn move sequence, and the
// aiRng/monsterHpRng (s0, s1, counter) state after every relevant draw.
//
// The fixture is a SECOND implementation of the same frozen spec in a different
// language, so a translation bug in the C++ has a real chance of diverging here
// rather than agreeing with a self-derived copy.
//
// Two drivers exercise the same fixture:
//   * a direct loop calling jaw_worm_take_turn (the unit-test path), for all 32
//     seeds -- the primary acceptance check;
//   * a pump()-driven path using real END_TURN sentinels through the pump for
//     one seed, confirming the MonsterTurnFn wiring produces the identical
//     sequence and counters.
//
// STS_FIXTURE_DIR is injected by tests/CMakeLists.txt as an absolute path to
// tests/fixtures so this binary resolves the fixture regardless of ctest's CWD.
//
// DRAW-COUNTING CONVENTION (matches monster_jaw_worm.hpp and the Python
// generator): jaw_worm_init = decision #1 (forced Chomp, one aiRng.random(99)
// draw whose value is ignored). Each jaw_worm_take_turn executes turn K then
// rolls decision #(K+1): one aiRng.random(99) plus, on tiebreak branches, one
// aiRng.randomBoolean -- so the aiRng counter advances 1 or 2 per turn.
// monsterHpRng is drawn once, at init (counter == 1).

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/monster_jaw_worm.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

#ifndef STS_FIXTURE_DIR
#error "STS_FIXTURE_DIR must be defined by the build (see tests/CMakeLists.txt)"
#endif

namespace sts::engine {
namespace {

constexpr int kNumTurns = 20;

// One turn's expected record from the fixture.
struct TurnRow {
    int turn = 0;
    uint8_t move = 0;
    uint64_t ai_s0 = 0;
    uint64_t ai_s1 = 0;
    int32_t ai_counter = 0;
};

// One seed block: the HP roll + monsterHpRng end state, and 20 turn rows.
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
    return std::string(STS_FIXTURE_DIR) + "/jaw_worm_fixture.tsv";
}

// Parse jaw_worm_fixture.tsv: '#' comments skipped; 'seed' rows open a block,
// followed by one 'hp' row and 20 'turn' rows.
std::vector<SeedCase> LoadFixture() {
    std::vector<SeedCase> cases;
    std::ifstream in(FixturePath());
    if (!in.is_open()) {
        return cases;  // empty -> the fixture-presence test fails loudly.
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

// A combat state seeded so monster_hp_rng and ai_rng both derive from `seed`
// (the game seeds every floor-scoped stream from the same run_seed + floor;
// here we exercise the streams in isolation with a shared seed, matching the
// fixture generator). Player kept alive so the pump never short-circuits.
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

// --- Tests ------------------------------------------------------------------

TEST(JawWormFixture, LoadsThirtyTwoSeeds) {
    const auto& cases = Fixture();
    ASSERT_EQ(cases.size(), 32u)
        << "fixture missing or malformed at " << FixturePath();
    for (const auto& c : cases) {
        EXPECT_EQ(c.turns.size(), static_cast<size_t>(kNumTurns)) << c.label;
    }
}

// jaw_worm_init post-conditions independent of the fixture (sanity net).
TEST(JawWormInit, ForcedFirstMoveAndOneDrawEachStream) {
    for (const auto& c : Fixture()) {
        CombatState s = MakeState(c.seed);
        jaw_worm_init(s, 0);
        const MonsterState& m = s.monsters[0];
        EXPECT_EQ(m.monster_id, static_cast<uint16_t>(MonsterId::JAW_WORM));
        EXPECT_EQ(m.hp, m.max_hp);
        EXPECT_GE(m.hp, kJawWormHpMin);
        EXPECT_LE(m.hp, kJawWormHpMax);
        EXPECT_EQ(m.move_history[0], kMoveChomp) << c.label;  // forced first move
        EXPECT_EQ(m.intent, static_cast<uint8_t>(MonsterIntent::ATTACK));
        EXPECT_EQ(s.monster_hp_rng.counter, 1) << c.label;    // one HP roll
        EXPECT_EQ(s.ai_rng.counter, 1) << c.label;            // decision #1 draw
    }
}

// Primary acceptance: direct-loop driver over all 32 seeds x 20 turns.
TEST(JawWormFixture, DirectLoopMatchesFixture) {
    for (const auto& c : Fixture()) {
        CombatState s = MakeState(c.seed);
        jaw_worm_init(s, 0);

        // HP roll + monsterHpRng end state.
        EXPECT_EQ(s.monsters[0].hp, c.hp) << c.label;
        EXPECT_EQ(s.monster_hp_rng.s0, c.hp_s0) << c.label;
        EXPECT_EQ(s.monster_hp_rng.s1, c.hp_s1) << c.label;
        EXPECT_EQ(s.monster_hp_rng.counter, c.hp_counter) << c.label;

        for (int k = 0; k < kNumTurns; ++k) {
            const TurnRow& row = c.turns[k];
            // The move about to be executed on turn k+1 is the current head.
            const uint8_t executed = s.monsters[0].move_history[0];
            EXPECT_EQ(executed, row.move)
                << c.label << " turn " << row.turn;

            jaw_worm_take_turn(s, 0);  // execute turn k+1, roll decision #(k+2)

            EXPECT_EQ(s.ai_rng.s0, row.ai_s0)
                << c.label << " turn " << row.turn;
            EXPECT_EQ(s.ai_rng.s1, row.ai_s1)
                << c.label << " turn " << row.turn;
            EXPECT_EQ(s.ai_rng.counter, row.ai_counter)
                << c.label << " turn " << row.turn;
        }
    }
}

// Integration: drive the SAME sequence through the real pump using END_TURN
// sentinels, with jaw_worm_take_turn wired in as pump's MonsterTurnFn. Each
// end-turn sentinel drives exactly one monster turn (design doc §5.2), so this
// must reproduce the fixture's move sequence and aiRng counters bit-for-bit.
TEST(JawWormPump, EndTurnDrivenMatchesFixture) {
    const auto& cases = Fixture();
    ASSERT_FALSE(cases.empty());
    const SeedCase& c = cases.front();  // one representative seed (r0)

    CombatState s = MakeState(c.seed);
    jaw_worm_init(s, 0);
    // The production jaw_worm_take_turn enqueues the move's real damage, so an
    // 80-HP player would die partway through these 20 monster turns and the pump
    // would halt at COMBAT_OVER before the sequence finishes. This test only
    // cares about the MOVE sequence + ai_rng counters through the pump wiring, so
    // give the player enough HP to survive all 20 turns and keep landing on
    // WAITING_ON_USER.
    s.player_hp = 30000;
    s.player_max_hp = 30000;
    // Player-turn invariant the pump expects before the first end-turn
    // (monster_attacks_queued stays true through the player's turn).
    s.monster_attacks_queued = 1;
    s.turn_has_ended = 0;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);

    EXPECT_EQ(s.monsters[0].hp, c.hp) << c.label;

    for (int k = 0; k < kNumTurns; ++k) {
        const TurnRow& row = c.turns[k];
        const uint8_t executed = s.monsters[0].move_history[0];
        EXPECT_EQ(executed, row.move) << c.label << " turn " << row.turn;

        // Player ends the turn: the end-turn sentinel drives step 3 -> 4 -> 5
        // (one jaw_worm_take_turn via the MonsterTurnFn seam) -> 6 -> back to
        // WAITING_ON_USER.
        add_card_to_queue_bottom(s, make_end_turn_sentinel());
        pump(s, jaw_worm_take_turn);

        EXPECT_EQ(s.ai_rng.s0, row.ai_s0) << c.label << " turn " << row.turn;
        EXPECT_EQ(s.ai_rng.s1, row.ai_s1) << c.label << " turn " << row.turn;
        EXPECT_EQ(s.ai_rng.counter, row.ai_counter)
            << c.label << " turn " << row.turn;
        EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER))
            << c.label << " turn " << row.turn;
    }
}

}  // namespace
}  // namespace sts::engine
