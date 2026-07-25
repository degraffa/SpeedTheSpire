// The Fungi Beast: native AI, turn body, pre-battle Spore Cloud, and the
// on-death trigger ordering Spore Cloud turns on.
//
// The fixture battery is independent of the engine (written from the decompiled
// Java by tests/fixtures/gen_fungi_beast_fixture.py, 32 seed-battery seeds x 20
// turns): fungi_beast_fixture.tsv carries the HP roll (monsterHpRng), the
// executed move and the exact aiRng draw sequence.
//
// DRAW ACCOUNTING at A20 (monster_fungi_beast.hpp): one monsterHpRng draw per
// ctor, one aiRng.random(99) at init(), and one more per turn through the queued
// RollMoveAction that sits AFTER takeTurn's switch (FungiBeast.java:97). getMove
// READS the rolled num (its d60 threshold), so the fixture's move column checks
// the decision tree, not just a draw count. usePreBattleAction draws NO RNG, so
// the Spore Cloud is asserted separately below.

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
#include "sts/engine/monster_fungi_beast.hpp"
#include "sts/engine/power_hooks.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/monster_table.hpp"

#ifndef STS_FIXTURE_DIR
#error "STS_FIXTURE_DIR must be defined by the build (see tests/CMakeLists.txt)"
#endif

namespace sts::engine {
namespace {

constexpr int kNumTurns = 20;
constexpr int32_t kA20 = kMonsterAscension;
constexpr uint8_t kBite = sts::registry::kFungiBeastMoveBite;
constexpr uint8_t kGrow = sts::registry::kFungiBeastMoveGrow;

struct TurnRow {
    int turn = 0;
    int move = 0;
    uint64_t ai_s0 = 0;
    uint64_t ai_s1 = 0;
    int32_t ai_counter = 0;
};

struct SeedCase {
    std::string label;
    int64_t seed = 0;
    int hp = 0;
    uint64_t hp_s0 = 0, hp_s1 = 0;
    int32_t hp_counter = 0;
    std::vector<TurnRow> turns;
};

std::vector<SeedCase> LoadFixture() {
    std::vector<SeedCase> cases;
    std::ifstream in(std::string(STS_FIXTURE_DIR) + "/fungi_beast_fixture.tsv");
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
            c.hp = std::stoi(col());
            c.hp_s0 = std::stoull(col());
            c.hp_s1 = std::stoull(col());
            c.hp_counter = std::stoi(col());
            cases.push_back(std::move(c));
        } else if (kind == "turn") {
            SeedCase& c = cases.back();
            TurnRow r;
            r.turn = std::stoi(col());
            r.move = std::stoi(col());
            r.ai_s0 = std::stoull(col());
            r.ai_s1 = std::stoull(col());
            r.ai_counter = std::stoi(col());
            c.turns.push_back(r);
        }
    }
    return cases;
}

CombatState MakeState(uint8_t monsters = 1) {
    CombatState s{};
    s.player_hp = 400;
    s.player_max_hp = 400;
    s.monster_count = monsters;
    return s;
}

CombatState MakeSeeded(int64_t seed, uint8_t monsters = 1) {
    CombatState s = MakeState(monsters);
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
    return -1;
}

int16_t player_power(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return s.player_powers[i].amount;
        }
    }
    return -1;
}

// A plain NORMAL attack from the player onto monster `mi`, through the real
// DAMAGE opcode so the death-edge dispatch fires exactly as in combat.
void player_attacks(CombatState& s, uint8_t mi, int32_t base) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    it.src = kActorPlayer;
    it.tgt = mi;
    it.amount = base;
    it.flags = make_damage_flags(DamageType::NORMAL);
    execute_opcode(s, it);
    drain(s);  // Spore Cloud's addToTop ApplyPowerAction
}

// --- Fixture battery ---------------------------------------------------------

TEST(FungiBeastFixture, MatchesFixture) {
    const std::vector<SeedCase> cases = LoadFixture();
    ASSERT_EQ(cases.size(), 32u) << "fixture missing/malformed";

    for (const auto& c : cases) {
        ASSERT_EQ(c.turns.size(), static_cast<size_t>(kNumTurns)) << c.label;
        CombatState s = MakeSeeded(c.seed);
        fungi_beast_init(s, 0);

        EXPECT_EQ(s.monsters[0].hp, c.hp) << c.label;
        EXPECT_EQ(s.monsters[0].hp, s.monsters[0].max_hp) << c.label;
        EXPECT_GE(s.monsters[0].hp, 24) << c.label;   // FungiBeast.java:57
        EXPECT_LE(s.monsters[0].hp, 28) << c.label;
        EXPECT_EQ(s.monster_hp_rng.s0, c.hp_s0) << c.label;
        EXPECT_EQ(s.monster_hp_rng.s1, c.hp_s1) << c.label;
        EXPECT_EQ(s.monster_hp_rng.counter, c.hp_counter) << c.label;
        EXPECT_EQ(s.ai_rng.counter, 1) << c.label;

        for (const TurnRow& row : c.turns) {
            EXPECT_EQ(static_cast<int>(s.monsters[0].move_history[0]), row.move)
                << c.label << " turn " << row.turn;
            fungi_beast_take_turn(s, 0);
            drain(s);
            EXPECT_EQ(s.ai_rng.s0, row.ai_s0) << c.label << " turn " << row.turn;
            EXPECT_EQ(s.ai_rng.s1, row.ai_s1) << c.label << " turn " << row.turn;
            EXPECT_EQ(s.ai_rng.counter, row.ai_counter)
                << c.label << " turn " << row.turn;
            EXPECT_EQ(row.ai_counter, row.turn + 1)
                << c.label << ": one queued RollMoveAction per turn (:97)";
        }
    }
}

// A roll-driven getMove needs both branches actually reached, or "matches the
// fixture" proves nothing about the decision tree.
TEST(FungiBeastFixture, BatteryCoversBothMoves) {
    const auto cases = LoadFixture();
    ASSERT_EQ(cases.size(), 32u);
    int bites = 0, grows = 0;
    for (const auto& c : cases) {
        for (const auto& row : c.turns) {
            bites += row.move == kBite ? 1 : 0;
            grows += row.move == kGrow ? 1 : 0;
        }
    }
    EXPECT_GT(bites, 0);
    EXPECT_GT(grows, 0);
    EXPECT_EQ(bites + grows, 32 * kNumTurns);
}

// getMove never lets three Bites in a row happen: at num < 60 lastTwoMoves(BITE)
// forces Grow, and at num >= 60 Bite is only chosen after a Grow
// (FungiBeast.java:100-113). Read off the independent fixture.
TEST(FungiBeastFixture, NeverThreeBitesInARow) {
    const auto cases = LoadFixture();
    ASSERT_EQ(cases.size(), 32u);
    for (const auto& c : cases) {
        int run = 0;
        for (const auto& row : c.turns) {
            run = row.move == kBite ? run + 1 : 0;
            EXPECT_LE(run, 2) << c.label << " turn " << row.turn;
        }
    }
}

// --- Move effects ------------------------------------------------------------

// takeTurn case BITE (FungiBeast.java:83-87): damage.get(0) == 6, the same in
// both ascension branches (:63,66).
TEST(FungiBeast, BiteIsSixAtEveryAscension) {
    CombatState s = MakeSeeded(555);
    fungi_beast_init(s, 0);
    set_monster_move(s.monsters[0], kBite, MonsterIntent::ATTACK);
    const int16_t before = s.player_hp;
    fungi_beast_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(before - s.player_hp, 6);
}

// takeTurn case GROW (:89-95) at the engine's fixed A20: StrengthPower(this,
// strAmt + 1). strAmt is 4 from A2 (:62), so the A17 branch applies 5 (:91).
TEST(FungiBeast, GrowIsFiveStrengthAtA20) {
    ASSERT_GE(kA20, 17);
    CombatState s = MakeSeeded(555);
    fungi_beast_init(s, 0);
    set_monster_move(s.monsters[0], kGrow, MonsterIntent::BUFF);
    fungi_beast_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), 5);
    EXPECT_EQ(s.player_hp, s.player_max_hp) << "Grow deals no damage";
}

// usePreBattleAction (:75-78): SporeCloudPower(this, 2) on ITSELF, no RNG.
TEST(FungiBeast, PreBattleAppliesSporeCloudTwoWithoutDrawing) {
    CombatState s = MakeSeeded(777);
    fungi_beast_init(s, 0);
    const int32_t hp_counter = s.monster_hp_rng.counter;
    const int32_t ai_counter = s.ai_rng.counter;

    fungi_beast_use_pre_battle_action(s, 0);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::SPORE_CLOUD), 2);
    EXPECT_EQ(s.monster_hp_rng.counter, hp_counter);
    EXPECT_EQ(s.ai_rng.counter, ai_counter);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::FUNGI_BEAST),
              &fungi_beast_use_pre_battle_action);
}

// --- Spore Cloud: the on-death trigger ordering ------------------------------
//
// SporeCloudPower.onDeath (SporeCloudPower.java:35-42) releases `amount`
// Vulnerable onto the player UNLESS AbstractRoom.isBattleEnding()
// (AbstractRoom.java:628-635 -> MonsterGroup.areMonstersBasicallyDead:90-95).
// AbstractMonster.die sets isDying BEFORE walking the power list
// (AbstractMonster.java:741-746), so the dying beast counts itself: with two
// beasts the FIRST death releases and the SECOND does not.

CombatState MakeTwoBeasts() {
    CombatState s = MakeSeeded(31337, /*monsters=*/2);
    fungi_beast_init(s, 0);
    fungi_beast_init(s, 1);
    use_pre_battle_actions(s);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::SPORE_CLOUD), 2);
    EXPECT_EQ(monster_power(s, 1, PowerId::SPORE_CLOUD), 2);
    return s;
}

TEST(SporeCloud, FirstDeathReleasesVulnerableAndTheLastDoesNot) {
    CombatState s = MakeTwoBeasts();
    ASSERT_EQ(s.monster_count, 2);

    // Kill slot 0 while slot 1 is still up: not battle-ending, so 2 Vulnerable.
    player_attacks(s, 0, 200);
    ASSERT_EQ(s.monsters[0].hp, 0);
    ASSERT_GT(s.monsters[1].hp, 0);
    EXPECT_EQ(player_power(s, PowerId::VULNERABLE), 2);

    // Kill the LAST one: isBattleEnding is true at the moment its powers are
    // walked, so it releases nothing and the stack stays at 2.
    player_attacks(s, 1, 200);
    ASSERT_EQ(s.monsters[1].hp, 0);
    EXPECT_EQ(player_power(s, PowerId::VULNERABLE), 2)
        << "the last Spore Cloud must be silent (SporeCloudPower.java:36-38)";
}

// A solo Fungi Beast is always the last monster, so its Spore Cloud never fires.
// This is not a degenerate case -- it is the Exordium Wildlife group's shape once
// its other member is already dead, and it is the branch a naive implementation
// gets wrong.
TEST(SporeCloud, SoloBeastReleasesNothing) {
    CombatState s = MakeSeeded(2024);
    fungi_beast_init(s, 0);
    fungi_beast_use_pre_battle_action(s, 0);
    drain(s);
    ASSERT_EQ(monster_power(s, 0, PowerId::SPORE_CLOUD), 2);

    player_attacks(s, 0, 200);
    ASSERT_EQ(s.monsters[0].hp, 0);
    EXPECT_EQ(player_power(s, PowerId::VULNERABLE), -1);
}

// The powers' onDeath loop runs BEFORE the relics' onMonsterDeath loop
// (AbstractMonster.die:741-750). With no relic in the mirror the observable
// half is that the release happens at the death edge at all -- and that a hit
// which does NOT kill leaves the power silent.
TEST(SporeCloud, OnlyFiresOnTheHitThatKills) {
    CombatState s = MakeTwoBeasts();
    player_attacks(s, 0, 1);                      // scratch, not lethal
    ASSERT_GT(s.monsters[0].hp, 0);
    EXPECT_EQ(player_power(s, PowerId::VULNERABLE), -1);

    player_attacks(s, 0, 200);
    EXPECT_EQ(player_power(s, PowerId::VULNERABLE), 2);

    // A second hit on the already-dead record must not re-fire it (the death
    // edge is `old_hp > 0 && new_hp == 0`).
    player_attacks(s, 0, 200);
    EXPECT_EQ(player_power(s, PowerId::VULNERABLE), 2);
}

// LOSE_HP kills too, and routes through creature.damage() (LoseHPAction.java:41),
// so the same death edge fires.
TEST(SporeCloud, FiresOnAnHpLossKillToo) {
    CombatState s = MakeTwoBeasts();
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::LOSE_HP);
    it.src = kActorPlayer;
    it.tgt = 0;
    it.amount = 200;
    execute_opcode(s, it);
    drain(s);
    ASSERT_EQ(s.monsters[0].hp, 0);
    EXPECT_EQ(player_power(s, PowerId::VULNERABLE), 2);
}

// The registry row: a BUFF (so Artifact does not touch it) whose amount is the
// Vulnerable stack it will release.
TEST(SporeCloud, IsANativeBuffBoundToOnDeath) {
    const PowerDef* def = power_def(PowerId::SPORE_CLOUD);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->type, PowerType::BUFF);
    EXPECT_TRUE(def->native);
    EXPECT_NE(def->hook_binding(sts::registry::Hook::ON_DEATH), nullptr);
    EXPECT_EQ(sts::registry::power_game_id(PowerId::SPORE_CLOUD), "Spore Cloud");
}

// --- Registry identity -------------------------------------------------------

TEST(FungiBeastRegistry, IdsAndDispatch) {
    namespace r = sts::registry;
    EXPECT_EQ(r::monster_game_id(r::MonsterId::FUNGI_BEAST), "FungiBeast");
    EXPECT_EQ(monster_init_fn(MonsterId::FUNGI_BEAST), &fungi_beast_init);
    EXPECT_EQ(monster_turn_fn(MonsterId::FUNGI_BEAST), &fungi_beast_take_turn);
    EXPECT_EQ(monster_roll_move_fn(MonsterId::FUNGI_BEAST), &fungi_beast_roll_move);
    // FungiBeast.damage (:126-133) is an animation-only override -- registered as
    // an explicitly empty case, so this call must be a harmless no-op.
    CombatState s = MakeSeeded(11);
    fungi_beast_init(s, 0);
    const int16_t hp = s.monsters[0].hp;
    on_monster_damaged(s, 0, 3);
    EXPECT_EQ(s.monsters[0].hp, hp);
    EXPECT_EQ(s.action_count, 0);
}

}  // namespace
}  // namespace sts::engine
