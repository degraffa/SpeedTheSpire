// Louse AI + monster turn + curl-up (B3.13).
//
// For each variant (Normal, Defensive) x 32 seeds x 20 turns, byte-compares the
// engine's louse (include/sts/engine/monster_louse.*) against an INDEPENDENT
// hand-derived fixture (tests/fixtures/louse_{normal,defensive}_fixture.tsv, from
// gen_louse_fixture.py): the HP roll, the per-instance bite roll, the pre-battle
// curl-up roll (all monsterHpRng, in order), the per-turn move sequence, and the
// aiRng/monsterHpRng state after every draw. A separate test drives the Curl Up
// on-first-attack block trigger (the B3.13 acceptance).
//
// DRAW-COUNTING (matches monster_louse.hpp + the generator): init = 2 monsterHpRng
// (HP, bite) + 1 aiRng.random(99) (decision #1, num USED). pre-battle = 1
// monsterHpRng (curl-up). Each take-turn = 1 aiRng.random(99).

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_louse.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

#ifndef STS_FIXTURE_DIR
#error "STS_FIXTURE_DIR must be defined by the build (see tests/CMakeLists.txt)"
#endif

namespace sts::engine {
namespace {

constexpr int kNumTurns = 20;
constexpr uint8_t kBite = 3;
constexpr uint8_t kMove4 = 4;

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
    int bite = 0;
    uint64_t ctor_s0 = 0, ctor_s1 = 0;
    int32_t ctor_counter = 0;
    int curl = 0;
    uint64_t curl_s0 = 0, curl_s1 = 0;
    int32_t curl_counter = 0;
    std::vector<TurnRow> turns;
};

using InitFn = void (*)(CombatState&, uint8_t);
using TurnFn = void (*)(CombatState&, uint8_t);

std::vector<SeedCase> LoadFixture(const std::string& file) {
    std::vector<SeedCase> cases;
    std::ifstream in(std::string(STS_FIXTURE_DIR) + "/" + file);
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
        auto col = [&ss]() {
            std::string v;
            std::getline(ss, v, '\t');
            return v;
        };
        if (kind == "seed") {
            SeedCase c;
            c.label = col();
            c.seed = std::stoll(col());
            cases.push_back(std::move(c));
        } else if (kind == "hp") {
            SeedCase& c = cases.back();
            c.hp = std::stoi(col());
            c.bite = std::stoi(col());
            c.ctor_s0 = std::stoull(col());
            c.ctor_s1 = std::stoull(col());
            c.ctor_counter = std::stoi(col());
        } else if (kind == "curl") {
            SeedCase& c = cases.back();
            c.curl = std::stoi(col());
            c.curl_s0 = std::stoull(col());
            c.curl_s1 = std::stoull(col());
            c.curl_counter = std::stoi(col());
        } else if (kind == "turn") {
            SeedCase& c = cases.back();
            TurnRow r;
            r.turn = std::stoi(col());
            r.move = static_cast<uint8_t>(std::stoi(col()));
            r.ai_s0 = std::stoull(col());
            r.ai_s1 = std::stoull(col());
            r.ai_counter = std::stoi(col());
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

void drain(CombatState& s) {
    while (s.action_count > 0) {
        const ActionQueueItem it = s.action_queue[s.action_head];
        s.action_head =
            static_cast<uint8_t>((s.action_head + 1) % kActionQueueCap);
        --s.action_count;
        execute_opcode(s, it);
    }
}

int16_t monster_power(const CombatState& s, uint8_t mi, PowerId id) {
    const MonsterState& m = s.monsters[mi];
    for (uint8_t i = 0; i < m.power_count; ++i) {
        if (m.powers[i].power_id == static_cast<uint16_t>(id)) {
            return m.powers[i].amount;
        }
    }
    return -1;  // absent
}

// Run the fixture-driven direct-loop check for one louse variant.
void RunVariant(const std::string& file, InitFn init_fn, TurnFn turn_fn,
                int hp_min, int hp_max) {
    const std::vector<SeedCase> cases = LoadFixture(file);
    ASSERT_EQ(cases.size(), 32u) << "fixture missing/malformed: " << file;

    for (const auto& c : cases) {
        ASSERT_EQ(c.turns.size(), static_cast<size_t>(kNumTurns)) << c.label;
        CombatState s = MakeState(c.seed);
        init_fn(s, 0);

        // ctor: HP roll + bite roll (two monsterHpRng draws, in order).
        EXPECT_EQ(s.monsters[0].hp, c.hp) << file << " " << c.label;
        EXPECT_EQ(s.monsters[0].hp, s.monsters[0].max_hp) << file << " " << c.label;
        EXPECT_GE(s.monsters[0].hp, hp_min) << file << " " << c.label;
        EXPECT_LE(s.monsters[0].hp, hp_max) << file << " " << c.label;
        EXPECT_EQ(static_cast<int>(s.monsters[0].pad0), c.bite)  // bite in pad0
            << file << " " << c.label;
        EXPECT_GE(c.bite, 6);
        EXPECT_LE(c.bite, 8);
        EXPECT_EQ(s.monster_hp_rng.s0, c.ctor_s0) << file << " " << c.label;
        EXPECT_EQ(s.monster_hp_rng.s1, c.ctor_s1) << file << " " << c.label;
        EXPECT_EQ(s.monster_hp_rng.counter, c.ctor_counter) << file << " " << c.label;
        EXPECT_EQ(s.ai_rng.counter, 1) << file << " " << c.label;  // decision #1

        // pre-battle: curl-up roll (third monsterHpRng draw) -> CurlUp power.
        louse_use_pre_battle_action(s, 0);
        ASSERT_EQ(s.action_count, 1) << file << " " << c.label
            << " usePreBattleAction must queue ApplyPowerAction";
        drain(s);
        EXPECT_EQ(monster_power(s, 0, PowerId::CURL_UP), c.curl)
            << file << " " << c.label;
        EXPECT_GE(c.curl, 9);
        EXPECT_LE(c.curl, 12);
        EXPECT_EQ(s.monster_hp_rng.s0, c.curl_s0) << file << " " << c.label;
        EXPECT_EQ(s.monster_hp_rng.s1, c.curl_s1) << file << " " << c.label;
        EXPECT_EQ(s.monster_hp_rng.counter, c.curl_counter)
            << file << " " << c.label;

        // per-turn move sequence + aiRng stream.
        for (size_t k = 0; k < c.turns.size(); ++k) {
            const TurnRow& row = c.turns[k];
            const uint8_t executed = s.monsters[0].move_history[0];
            EXPECT_EQ(executed, row.move) << file << " " << c.label
                                          << " turn " << row.turn;
            EXPECT_TRUE(executed == kBite || executed == kMove4)
                << file << " " << c.label << " turn " << row.turn;

            turn_fn(s, 0);

            EXPECT_EQ(s.ai_rng.s0, row.ai_s0) << file << " " << c.label
                                              << " turn " << row.turn;
            EXPECT_EQ(s.ai_rng.s1, row.ai_s1) << file << " " << c.label
                                              << " turn " << row.turn;
            EXPECT_EQ(s.ai_rng.counter, row.ai_counter)
                << file << " " << c.label << " turn " << row.turn;
        }
    }
}

// --- Tests ------------------------------------------------------------------

TEST(LouseFixture, NormalMatchesFixture) {
    RunVariant("louse_normal_fixture.tsv", &louse_normal_init,
               &louse_normal_take_turn, /*hp_min=*/11, /*hp_max=*/16);
}

TEST(LouseFixture, DefensiveMatchesFixture) {
    RunVariant("louse_defensive_fixture.tsv", &louse_defensive_init,
               &louse_defensive_take_turn, /*hp_min=*/12, /*hp_max=*/18);
}

// Normal and Defensive share getMove exactly -> identical move sequence + aiRng
// stream for the same seed (only the monsterHpRng stream diverges via the HP
// range, and only the move-4 EFFECT differs). Pin that equivalence.
TEST(LouseFixture, VariantsShareMoveSequenceAndAiRng) {
    const auto n = LoadFixture("louse_normal_fixture.tsv");
    const auto d = LoadFixture("louse_defensive_fixture.tsv");
    ASSERT_EQ(n.size(), d.size());
    for (size_t i = 0; i < n.size(); ++i) {
        ASSERT_EQ(n[i].turns.size(), d[i].turns.size());
        for (size_t k = 0; k < n[i].turns.size(); ++k) {
            EXPECT_EQ(n[i].turns[k].move, d[i].turns[k].move) << n[i].label;
            EXPECT_EQ(n[i].turns[k].ai_s0, d[i].turns[k].ai_s0) << n[i].label;
            EXPECT_EQ(n[i].turns[k].ai_s1, d[i].turns[k].ai_s1) << n[i].label;
            EXPECT_EQ(n[i].turns[k].ai_counter, d[i].turns[k].ai_counter)
                << n[i].label;
        }
    }
}

TEST(LouseSpawn, ConstructorThenPreBattlePhasesPreserveGroupOrder) {
    constexpr int64_t seed = 424242;
    CombatState s = MakeState(seed);
    const MonsterId group[] = {MonsterId::LOUSE_NORMAL, MonsterId::CULTIST,
                               MonsterId::LOUSE_DEFENSIVE};

    // Hand-drive only the monsterHpRng stream. All constructor rolls happen in
    // spawn order first; pre-battle Curl Up rolls form a later spawn-order phase.
    RngStream expected = from_seed(seed);
    const int red_hp = random(expected, 11, 16);
    const int red_bite = random(expected, 6, 8);
    const int cultist_hp = random(expected, 50, 56);
    const int green_hp = random(expected, 12, 18);
    const int green_bite = random(expected, 6, 8);

    spawn_group(s, group);
    EXPECT_EQ(s.monster_hp_rng.counter, 5);
    EXPECT_EQ(s.monsters[0].hp, red_hp);
    EXPECT_EQ(s.monsters[0].pad0, red_bite);
    EXPECT_EQ(s.monsters[1].hp, cultist_hp);
    EXPECT_EQ(s.monsters[2].hp, green_hp);
    EXPECT_EQ(s.monsters[2].pad0, green_bite);
    EXPECT_EQ(monster_power(s, 0, PowerId::CURL_UP), -1)
        << "Curl Up is not a constructor roll";

    const int red_curl = random(expected, 9, 12);
    const int green_curl = random(expected, 9, 12);
    use_pre_battle_actions(s);
    EXPECT_EQ(s.monster_hp_rng.counter, 7);
    EXPECT_EQ(s.monster_hp_rng.s0, expected.s0);
    EXPECT_EQ(s.monster_hp_rng.s1, expected.s1);
    ASSERT_EQ(s.action_count, 2) << "only the two louses queue pre-battle powers";
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::CURL_UP), red_curl);
    EXPECT_EQ(monster_power(s, 1, PowerId::CURL_UP), -1);
    EXPECT_EQ(monster_power(s, 2, PowerId::CURL_UP), green_curl);
}

TEST(LouseTurn, BiteStrengthenAndWeakenEffectsResolve) {
    CombatState bite = MakeState(/*seed=*/101);
    louse_normal_init(bite, 0);
    bite.player_hp = 50;
    bite.player_max_hp = 50;
    bite.monsters[0].pad0 = 8;  // per-instance bite roll
    set_monster_move(bite.monsters[0], kBite,
                     sts::registry::MonsterIntent::ATTACK);
    louse_normal_take_turn(bite, 0);
    drain(bite);
    EXPECT_EQ(bite.player_hp, 42);

    CombatState strengthen = MakeState(/*seed=*/102);
    louse_normal_init(strengthen, 0);
    set_monster_move(strengthen.monsters[0], kMove4,
                     sts::registry::MonsterIntent::BUFF);
    louse_normal_take_turn(strengthen, 0);
    drain(strengthen);
    EXPECT_EQ(monster_power(strengthen, 0, PowerId::STRENGTH), 4)
        << "A17+ red louse Strengthen";

    CombatState weaken = MakeState(/*seed=*/103);
    louse_defensive_init(weaken, 0);
    set_monster_move(weaken.monsters[0], kMove4,
                     sts::registry::MonsterIntent::DEBUFF);
    louse_defensive_take_turn(weaken, 0);
    drain(weaken);
    bool has_weak = false;
    for (uint8_t i = 0; i < weaken.player_power_count; ++i) {
        if (weaken.player_powers[i].power_id ==
            static_cast<uint16_t>(PowerId::WEAK)) {
            has_weak = weaken.player_powers[i].amount == 2;
        }
    }
    EXPECT_TRUE(has_weak) << "green louse Weaken applies 2 Weak to the player";
}

// B3.13 acceptance: Curl Up gains Block on the FIRST attack the louse takes, once
// (CurlUpPower.onAttacked: non-lethal NORMAL >0 damage -> gain amount Block, then
// self-remove). A second attack grants no further block.
TEST(LouseCurlUp, TriggersOnceOnFirstAttackDamage) {
    CombatState s = MakeState(/*seed=*/999);
    louse_normal_init(s, 0);
    louse_use_pre_battle_action(s, 0);
    drain(s);  // queued ApplyPowerAction(CurlUpPower)
    const int16_t curl = monster_power(s, 0, PowerId::CURL_UP);
    ASSERT_GE(curl, 9);
    // Give the louse plenty of HP so the attack is non-lethal (damage < hp).
    s.monsters[0].hp = 100;
    s.monsters[0].max_hp = 100;
    s.monsters[0].block = 0;

    // Player attacks the louse for 6 (NORMAL, src=player, tgt=louse slot 0).
    ActionQueueItem hit{};
    hit.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    hit.src = kActorPlayer;
    hit.tgt = 0;
    hit.amount = 6;
    add_to_bottom(s, hit);
    drain(s);

    // The hit that triggered Curl Up is NOT itself blocked (block applies to
    // future hits): hp 100 -> 94, then +curl block, Curl Up removed.
    EXPECT_EQ(s.monsters[0].hp, 94);
    EXPECT_EQ(s.monsters[0].block, curl);
    EXPECT_EQ(monster_power(s, 0, PowerId::CURL_UP), -1) << "Curl Up self-removed";

    // A second attack: no more Curl Up, so no additional block gain. The 6 damage
    // is soaked by the standing block (curl >= 9 > 6): block curl -> curl-6, hp 94.
    ActionQueueItem hit2 = hit;
    add_to_bottom(s, hit2);
    drain(s);
    EXPECT_EQ(s.monsters[0].block, static_cast<int16_t>(curl - 6));
    EXPECT_EQ(s.monsters[0].hp, 94) << "block absorbed the second hit";
    EXPECT_EQ(monster_power(s, 0, PowerId::CURL_UP), -1) << "still no Curl Up";
}

// CurlUpPower flips its private `triggered` boolean synchronously, then queues
// GainBlock and removal at the BOTTOM. For a multi-hit card, hit 2 is already in
// the queue and therefore resolves before those responses; it must see
// triggered=true and not grant a second block application.
TEST(LouseCurlUp, QueuedMultiHitUsesSynchronousTriggeredLatch) {
    CombatState s = MakeState(/*seed=*/314159);
    louse_normal_init(s, 0);
    louse_use_pre_battle_action(s, 0);
    drain(s);
    const int16_t curl = monster_power(s, 0, PowerId::CURL_UP);
    ASSERT_GE(curl, 9);
    s.monsters[0].hp = 100;
    s.monsters[0].max_hp = 100;

    ActionQueueItem hit{};
    hit.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    hit.src = kActorPlayer;
    hit.tgt = 0;
    hit.amount = 6;
    add_to_bottom(s, hit);
    add_to_bottom(s, hit);
    drain(s);

    EXPECT_EQ(s.monsters[0].hp, 88) << "both queued hits land before Curl Up block";
    EXPECT_EQ(s.monsters[0].block, curl) << "Curl Up block queued exactly once";
    EXPECT_EQ(monster_power(s, 0, PowerId::CURL_UP), -1);
    EXPECT_EQ(s.monsters[0].flags & kMonsterFlagCurlUpTriggered, 0u)
        << "removing the power destroys its triggered latch";
}

TEST(LouseCurlUp, FullyBlockedAndLethalHitsDoNotTrigger) {
    auto fresh = []() {
        CombatState s = MakeState(/*seed=*/271828);
        louse_defensive_init(s, 0);
        louse_use_pre_battle_action(s, 0);
        drain(s);
        s.monsters[0].hp = 20;
        s.monsters[0].max_hp = 20;
        return s;
    };

    ActionQueueItem hit{};
    hit.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    hit.src = kActorPlayer;
    hit.tgt = 0;

    CombatState blocked = fresh();
    const int16_t curl = monster_power(blocked, 0, PowerId::CURL_UP);
    blocked.monsters[0].block = 6;
    hit.amount = 6;
    execute_opcode(blocked, hit);
    EXPECT_EQ(monster_power(blocked, 0, PowerId::CURL_UP), curl);
    EXPECT_EQ(blocked.action_count, 0) << "post-block damageAmount == 0";

    CombatState lethal = fresh();
    hit.amount = 20;
    execute_opcode(lethal, hit);
    EXPECT_EQ(lethal.monsters[0].hp, 0);
    EXPECT_EQ(monster_power(lethal, 0, PowerId::CURL_UP), curl)
        << "damageAmount < currentHealth guard rejects lethal damage";
    EXPECT_EQ(lethal.action_count, 0);
}

}  // namespace
}  // namespace sts::engine
