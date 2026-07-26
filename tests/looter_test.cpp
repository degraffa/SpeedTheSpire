// The Looter: native state-machine AI, the gold-steal accounting, the first
// reachable Act-1 escape, and the liveness predicate it landed with -- an
// ESCAPED monster is alive and OUT of the fight, terminal-state-distinct from
// a kill for the player, the pump, targeting, AoE and the power walks.
//
// The fixture battery is independent of the engine (written from the decompiled
// Java by tests/fixtures/gen_looter_fixture.py, 32 seed-battery seeds): unlike
// the slaver batteries it is NOT 20 turns per seed -- the Looter's takeTurn
// machine ENDS at its Escape on turn 4 or 5 (coin-driven), and a solo combat is
// over the moment the EscapeAction resolves, so each seed's rows run exactly to
// its Escape turn and the ROW COUNT itself pins the 50/50.
//
// DRAW ACCOUNTING at A20 (monster_looter.hpp): one monsterHpRng draw per ctor;
// one aiRng.random(99) at init(), DISCARDED by getMove (Looter.java:176-179);
// then exactly two aiRng randomBoolean draws across the whole combat -- the 0.6
// first-Mug talk gate (:92) and the 0.5 Smoke-Bomb-or-Lunge coin (:101). No
// takeTurn body queues a RollMoveAction.

#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_looter.hpp"
#include "sts/engine/potions.hpp"
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

constexpr int32_t kA20 = kMonsterAscension;
constexpr uint8_t kMug = sts::registry::kLooterMoveMug;
constexpr uint8_t kSmokeBomb = sts::registry::kLooterMoveSmokeBomb;
constexpr uint8_t kEscape = sts::registry::kLooterMoveEscape;
constexpr uint8_t kLunge = sts::registry::kLooterMoveLunge;

struct TurnRow {
    int turn = 0;
    int move = 0;
    int slash_after = 0;
    int stolen_after = 0;
    int escaped_after = 0;
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
    std::ifstream in(std::string(STS_FIXTURE_DIR) + "/looter_fixture.tsv");
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
            r.slash_after = std::stoi(col());
            r.stolen_after = std::stoi(col());
            r.escaped_after = std::stoi(col());
            r.ai_s0 = std::stoull(col());
            r.ai_s1 = std::stoull(col());
            r.ai_counter = std::stoi(col());
            c.turns.push_back(r);
        }
    }
    return cases;
}

CombatState MakeSeeded(int64_t seed) {
    CombatState s{};
    s.player_hp = 400;  // deep enough that every mug/lunge line lands harmlessly
    s.player_max_hp = 400;
    s.monster_count = 1;
    s.monster_hp_rng = from_seed(seed);
    s.ai_rng = from_seed(seed);
    s.card_random_rng = from_seed(seed);
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
    drain(s);
}

// A pump-driven solo Looter combat, exactly the run layer's construction shape:
// spawn_group -> use_pre_battle_actions -> begin_first_turn.
CombatState MakePumpedSolo(int64_t seed) {
    CombatState s = MakeSeeded(seed);
    s.monster_count = 0;  // spawn_group sets it
    const MonsterId ids[1] = {MonsterId::LOOTER};
    spawn_group(s, std::span<const MonsterId>(ids, 1));
    use_pre_battle_actions(s);
    begin_first_turn(s, dispatch_monster_turn);
    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    return s;
}

void end_turn(CombatState& s) {
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    pump(s, dispatch_monster_turn);
}

// --- Fixture battery ---------------------------------------------------------

TEST(LooterFixture, MatchesFixture) {
    const std::vector<SeedCase> cases = LoadFixture();
    ASSERT_EQ(cases.size(), 32u) << "fixture missing/malformed";

    for (const auto& c : cases) {
        ASSERT_GE(c.turns.size(), 4u) << c.label;
        ASSERT_LE(c.turns.size(), 5u) << c.label;
        CombatState s = MakeSeeded(c.seed);
        looter_init(s, 0);

        // ctor: setHp(46, 50) at A20 -- exactly one monsterHpRng draw.
        EXPECT_EQ(s.monsters[0].hp, c.hp) << c.label;
        EXPECT_EQ(s.monsters[0].hp, s.monsters[0].max_hp) << c.label;
        EXPECT_GE(s.monsters[0].hp, 46) << c.label;
        EXPECT_LE(s.monsters[0].hp, 50) << c.label;
        EXPECT_EQ(s.monster_hp_rng.s0, c.hp_s0) << c.label;
        EXPECT_EQ(s.monster_hp_rng.s1, c.hp_s1) << c.label;
        EXPECT_EQ(s.monster_hp_rng.counter, c.hp_counter) << c.label;
        // init() -> rollMove: one aiRng draw, discarded; the opener is Mug.
        EXPECT_EQ(s.ai_rng.counter, 1) << c.label;
        EXPECT_EQ(s.monsters[0].move_history[0], kMug) << c.label;

        for (const TurnRow& row : c.turns) {
            EXPECT_EQ(static_cast<int>(s.monsters[0].move_history[0]), row.move)
                << c.label << " turn " << row.turn;
            looter_take_turn(s, 0);
            drain(s);
            EXPECT_EQ(static_cast<int>(looter_steal_count(s.monsters[0])),
                      row.slash_after)
                << c.label << " turn " << row.turn;
            EXPECT_EQ(looter_stolen_gold(s.monsters[0]), row.stolen_after)
                << c.label << " turn " << row.turn;
            EXPECT_EQ(monster_escaped(s.monsters[0]), row.escaped_after != 0)
                << c.label << " turn " << row.turn;
            EXPECT_EQ(s.ai_rng.s0, row.ai_s0)
                << c.label << " turn " << row.turn;
            EXPECT_EQ(s.ai_rng.s1, row.ai_s1)
                << c.label << " turn " << row.turn;
            EXPECT_EQ(s.ai_rng.counter, row.ai_counter)
                << c.label << " turn " << row.turn;
        }
        // The battery ends AT the escape; the record is escaped with its HP
        // intact -- alive and out of the fight.
        EXPECT_TRUE(monster_escaped(s.monsters[0])) << c.label;
        EXPECT_EQ(s.monsters[0].hp, c.hp) << c.label;
    }
}

// The row count pins the coin (4 rows == Smoke Bomb path, 5 == Lunge path); the
// battery must cover both or the one aiRng-driven branch is untested. The move
// sequences themselves must be exactly the two shapes the machine can produce.
TEST(LooterFixture, BatteryCoversBothCoinPaths) {
    const auto cases = LoadFixture();
    ASSERT_EQ(cases.size(), 32u);
    int smoke_paths = 0, lunge_paths = 0;
    for (const auto& c : cases) {
        std::vector<int> moves;
        for (const auto& row : c.turns) {
            moves.push_back(row.move);
        }
        const std::vector<int> smoke = {kMug, kMug, kSmokeBomb, kEscape};
        const std::vector<int> lunge = {kMug, kMug, kLunge, kSmokeBomb, kEscape};
        if (moves == smoke) {
            ++smoke_paths;
        } else {
            EXPECT_EQ(moves, lunge) << c.label;
            ++lunge_paths;
        }
        // The two aiRng draws: talk gate on turn 1, coin on turn 2, nothing
        // after (counter 1 is init's discarded roll).
        ASSERT_GE(c.turns.size(), 2u);
        EXPECT_EQ(c.turns[0].ai_counter, 2) << c.label;
        EXPECT_EQ(c.turns[1].ai_counter, 3) << c.label;
        EXPECT_EQ(c.turns.back().ai_counter, 3) << c.label;
    }
    EXPECT_GT(smoke_paths, 0);
    EXPECT_GT(lunge_paths, 0);
    EXPECT_EQ(smoke_paths + lunge_paths, 32);
}

// --- Move bodies at A20 ------------------------------------------------------

// usePreBattleAction (Looter.java:84-86): ThieveryPower(this, goldAmt) on
// ITSELF, goldAmt == 20 at A20 (:63). No RNG.
TEST(Looter, PreBattleAppliesThieveryTwentyWithoutDrawing) {
    ASSERT_GE(kA20, 17);
    CombatState s = MakeSeeded(777);
    looter_init(s, 0);
    const int32_t hp_counter = s.monster_hp_rng.counter;
    const int32_t ai_counter = s.ai_rng.counter;

    looter_use_pre_battle_action(s, 0);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::THIEVERY), kLooterGoldAmt);
    EXPECT_EQ(kLooterGoldAmt, 20);
    EXPECT_EQ(s.monster_hp_rng.counter, hp_counter);
    EXPECT_EQ(s.ai_rng.counter, ai_counter);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::LOOTER),
              &looter_use_pre_battle_action);
}

// Mug at A20: damage.get(0) == swipeDmg == 11 (the A2 branch, Looter.java:70),
// and one steal accrued (the engine's count; the game's accrual is
// min(goldAmt, player.gold) -- settlement clamps, monster_looter.hpp note (2)).
TEST(Looter, MugNumbersAndStealAtA20) {
    CombatState s = MakeSeeded(12345);
    looter_init(s, 0);
    ASSERT_EQ(s.monsters[0].move_history[0], kMug);
    const int16_t hp_before = s.player_hp;
    looter_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(hp_before - s.player_hp, 11);
    EXPECT_EQ(looter_steal_count(s.monsters[0]), 1);
    EXPECT_EQ(looter_stolen_gold(s.monsters[0]), 20);
    // First Mug telegraphs Mug again (:108).
    EXPECT_EQ(s.monsters[0].move_history[0], kMug);
}

// Lunge at A20: damage.get(1) == lungeDmg == 14 (the A2 branch, :71), a steal,
// and the unconditional synchronous setMove(SMOKE_BOMB, DEFEND) (:117).
TEST(Looter, LungeNumbersAndTelegraphAtA20) {
    CombatState s = MakeSeeded(9876);
    looter_init(s, 0);
    set_monster_move(s.monsters[0], kLunge, MonsterIntent::ATTACK);
    const int16_t hp_before = s.player_hp;
    const int32_t ai_counter = s.ai_rng.counter;
    looter_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(hp_before - s.player_hp, 14);
    EXPECT_EQ(looter_steal_count(s.monsters[0]), 1);
    EXPECT_EQ(s.monsters[0].move_history[0], kSmokeBomb);
    EXPECT_EQ(s.monsters[0].intent,
              static_cast<uint8_t>(MonsterIntent::DEFEND));
    EXPECT_EQ(s.ai_rng.counter, ai_counter) << "the Lunge turn rolls nothing";
}

// Smoke Bomb: GainBlock(6) -- escapeDef, no ascension branch (:46,122) -- and
// the queued SetMoveAction(3, Intent.ESCAPE) (:123). No steal, no aiRng draw.
TEST(Looter, SmokeBombGainsSixBlockAndTelegraphsEscape) {
    CombatState s = MakeSeeded(4242);
    looter_init(s, 0);
    set_monster_move(s.monsters[0], kSmokeBomb, MonsterIntent::DEFEND);
    const int32_t ai_counter = s.ai_rng.counter;
    looter_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(s.monsters[0].block, 6);
    EXPECT_EQ(looter_steal_count(s.monsters[0]), 0);
    EXPECT_EQ(s.monsters[0].move_history[0], kEscape);
    EXPECT_EQ(s.monsters[0].intent,
              static_cast<uint8_t>(MonsterIntent::ESCAPE));
    EXPECT_EQ(s.ai_rng.counter, ai_counter);
}

// Escape: room.mugged latches SYNCHRONOUSLY inside takeTurn (:128), before the
// queued EscapeAction sets the monster's escaped flag at RESOLVE time (:130 ->
// the ESCAPE opcode).
TEST(Looter, EscapeLatchesMuggedBeforeTheQueuedEscapeResolves) {
    CombatState s = MakeSeeded(31337);
    looter_init(s, 0);
    set_monster_move(s.monsters[0], kEscape, MonsterIntent::ESCAPE);
    looter_take_turn(s, 0);
    EXPECT_NE(s.flags & kCombatFlagMugged, 0u) << "mugged is synchronous";
    EXPECT_FALSE(monster_escaped(s.monsters[0])) << "escape is queued";
    drain(s);
    EXPECT_TRUE(monster_escaped(s.monsters[0]));
    EXPECT_GT(s.monsters[0].hp, 0) << "an escaped monster is NOT dying";
}

// --- The escape terminal state, distinct from a kill -------------------------

// A full pump-driven solo combat: the machine escapes on turn 4 or 5 and the
// pump ends the battle from the liveness predicate -- with the record alive,
// escaped, and the room mugged. Reward implications are B4.5's; the STATE is
// what this task pins.
TEST(LooterEscape, SoloEscapeEndsCombatDistinctFromKill) {
    CombatState s = MakePumpedSolo(2024);
    int turns = 0;
    while (s.phase == static_cast<uint8_t>(CombatPhase::WAITING_ON_USER) &&
           turns < 8) {
        end_turn(s);
        ++turns;
    }
    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::COMBAT_OVER));
    EXPECT_LE(turns, 5);
    EXPECT_GE(turns, 4);
    // The terminal state: player alive, monster ALIVE and escaped, room mugged
    // -- everything a kill terminal does NOT have.
    EXPECT_GT(s.player_hp, 0);
    EXPECT_GT(s.monsters[0].hp, 0);
    EXPECT_TRUE(monster_escaped(s.monsters[0]));
    EXPECT_NE(s.flags & kCombatFlagMugged, 0u);
    EXPECT_EQ(s.flags & kCombatFlagPlayerEscaped, 0u);
    // Mug + Mug always land before any escape path, so the record carries at
    // least two steals for the settlement layer (3 exactly on the Lunge path).
    EXPECT_GE(looter_steal_count(s.monsters[0]), 2);
    EXPECT_LE(looter_steal_count(s.monsters[0]), 3);
    EXPECT_EQ(looter_stolen_gold(s.monsters[0]),
              looter_steal_count(s.monsters[0]) * kLooterGoldAmt);
}

// The kill terminal: hp 0, NOT escaped, room NOT mugged -- and the stolen-gold
// count survives on the dead record, which is what die()'s
// addStolenGoldToRewards (:170-172) becomes for the B4.5 reward layer to read.
TEST(LooterEscape, KilledLooterKeepsItsStolenGoldOnTheRecord) {
    CombatState s = MakePumpedSolo(555);
    end_turn(s);  // Mug
    ASSERT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    end_turn(s);  // Mug (the coin turn)
    ASSERT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    ASSERT_EQ(looter_steal_count(s.monsters[0]), 2);

    player_attacks(s, 0, 200);
    ASSERT_EQ(s.monsters[0].hp, 0);
    pump(s, dispatch_monster_turn);
    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::COMBAT_OVER));
    EXPECT_FALSE(monster_escaped(s.monsters[0]));
    EXPECT_EQ(s.flags & kCombatFlagMugged, 0u);
    EXPECT_EQ(looter_stolen_gold(s.monsters[0]), 40)
        << "the reward layer reads the dead record's accrual";
}

// --- An escaped monster is out of the fight while combat continues -----------

CombatState MakeEscapedPlusLive() {
    CombatState s = MakeSeeded(11);
    s.monster_count = 2;
    looter_init(s, 0);
    s.monsters[0].flags |= kMonsterFlagEscaped;  // already gone
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[1].hp = 40;
    s.monsters[1].max_hp = 40;
    s.monster_attacks_queued = 1;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    return s;
}

TEST(LooterEscape, EscapedMonsterDoesNotEndAMultiMonsterCombat) {
    CombatState s = MakeEscapedPlusLive();
    const PumpStepResult r = pump_step(s, default_monster_turn);
    EXPECT_NE(r.outcome, PumpOutcome::COMBAT_OVER)
        << "one live monster keeps the battle open";
    // ...but with the live one dead too, escape ends it exactly like a kill.
    s.monsters[1].hp = 0;
    const PumpStepResult r2 = pump_step(s, default_monster_turn);
    EXPECT_EQ(r2.outcome, PumpOutcome::COMBAT_OVER);
}

TEST(LooterEscape, EscapedMonsterForfeitsItsQueuedTurn) {
    CombatState s = MakeEscapedPlusLive();
    // queueMonsters (MonsterGroup.java:117-122) skips isDeadOrEscaped: only the
    // live slot 1 is enqueued.
    s.monster_attacks_queued = 0;
    PumpStepResult r = pump_step(s, default_monster_turn);
    ASSERT_EQ(r.outcome, PumpOutcome::QUEUED_MONSTERS);
    ASSERT_EQ(s.monster_queue_count, 1);
    EXPECT_EQ(s.monster_queue[0].monster_index, 1);
    // And a stale queue entry pointing at an escaped monster forfeits at step 5
    // (GameActionManager.java:310).
    s.monster_queue[0].monster_index = 0;
    r = pump_step(s, default_monster_turn);
    EXPECT_EQ(r.outcome, PumpOutcome::RAN_MONSTER);
    EXPECT_EQ(s.monsters[0].move_history[0], kMug)
        << "the escaped Looter must not have taken a turn";
}

TEST(LooterEscape, EscapedMonsterIsNotTargetableAndAoeSkipsIt) {
    CombatState s = MakeEscapedPlusLive();
    // Card targeting: a STRIKE's target row offers only the live slot.
    const CardDef* strike = card_def(CardId::STRIKE);
    ASSERT_NE(strike, nullptr);
    s.card_pool[0].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.card_pool[0].cost_now = card_cost(*strike, 0);
    s.card_pool[0].flags = card_flags(*strike, 0);
    s.hand[0] = 0;
    s.hand_count = 1;
    s.player_energy = 3;
    ActionMask mask{};
    legal_actions(s, mask);
    ASSERT_TRUE(mask.can_play[0]);
    EXPECT_FALSE(mask.can_play_target[0][0]) << "escaped: not a legal target";
    EXPECT_TRUE(mask.can_play_target[0][1]);

    // AoE fan-out (DamageAllEnemiesAction skips isDeadOrEscaped): the escaped
    // record's HP must not move.
    const int16_t looter_hp = s.monsters[0].hp;
    ActionQueueItem aoe{};
    aoe.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    aoe.src = kActorPlayer;
    aoe.tgt = kActorAllEnemies;
    aoe.amount = 5;
    aoe.flags = make_damage_flags(DamageType::NORMAL);
    execute_opcode(s, aoe);
    drain(s);
    EXPECT_EQ(s.monsters[0].hp, looter_hp);
    EXPECT_EQ(s.monsters[1].hp, 35);

    // Random targeting (getRandomMonster aliveOnly, MonsterGroup.java:156-171):
    // the escaped slot is never a candidate.
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(roll_random_target(s), 1);
    }
}

// --- Smoke Bomb: the combat-layer escape body --------------------------------

// SmokeBomb.use (SmokeBomb.java:37-48) sets the PLAYER escaping and never
// touches the monsters; the pump then ends the battle from the flag -- with the
// monsters still alive, which no opcode-written COMBAT_OVER could survive
// before the predicate learned about escape.
TEST(SmokeBombPotion, CombatBodyEndsTheBattleWithMonstersAlive) {
    CombatState s = MakeSeeded(606);
    looter_init(s, 0);
    s.monster_attacks_queued = 1;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    const int16_t monster_hp = s.monsters[0].hp;

    ASSERT_TRUE(potion_use_implemented(PotionId::SMOKE_BOMB));
    ASSERT_TRUE(use_potion(s, PotionId::SMOKE_BOMB, 0));
    EXPECT_NE(s.flags & kCombatFlagPlayerEscaped, 0u)
        << "isEscaping is synchronous (SmokeBomb.java:43)";
    pump(s, dispatch_monster_turn);
    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::COMBAT_OVER));
    // Distinct from every other terminal: player alive, monsters untouched and
    // NOT escaped, room not mugged.
    EXPECT_GT(s.player_hp, 0);
    EXPECT_EQ(s.monsters[0].hp, monster_hp);
    EXPECT_FALSE(monster_escaped(s.monsters[0]));
    EXPECT_EQ(s.flags & kCombatFlagMugged, 0u);
}

// --- Registry identity + the un-parked encounters ----------------------------

TEST(LooterRegistry, IdsAndDispatch) {
    namespace r = sts::registry;
    EXPECT_EQ(r::monster_game_id(r::MonsterId::LOOTER), "Looter");
    EXPECT_EQ(monster_init_fn(MonsterId::LOOTER), &looter_init);
    EXPECT_EQ(monster_turn_fn(MonsterId::LOOTER), &looter_take_turn);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::LOOTER),
              &looter_use_pre_battle_action);
    // No takeTurn body queues a ROLL_MOVE; nothing spawns it mid-combat; no
    // damage() override (Looter.java declares none).
    EXPECT_EQ(monster_roll_move_fn(MonsterId::LOOTER), nullptr);
    EXPECT_EQ(monster_spawn_at_hp_fn(MonsterId::LOOTER), nullptr);
    CombatState s = MakeSeeded(7);
    looter_init(s, 0);
    const int16_t hp = s.monsters[0].hp;
    on_monster_damaged(s, 0, 3);
    EXPECT_EQ(s.monsters[0].hp, hp);
    EXPECT_EQ(s.action_count, 0);

    // The allocated vocabulary values, pinned.
    EXPECT_EQ(static_cast<uint8_t>(MonsterIntent::ESCAPE), 13);
    EXPECT_EQ(static_cast<uint16_t>(Opcode::ESCAPE), 40);
    EXPECT_EQ(static_cast<uint16_t>(r::MonsterId::LOOTER), 26);
    EXPECT_EQ(static_cast<uint16_t>(PowerId::THIEVERY), 75);
}

// Thievery the row: a pure marker BUFF -- no hooks, not native; the steal rides
// DamageAction's stealGold, not this power (ThieveryPower.java:11-31).
TEST(LooterRegistry, ThieveryIsAPureMarkerPower) {
    const PowerDef* def = power_def(PowerId::THIEVERY);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->type, PowerType::BUFF);
    EXPECT_FALSE(def->native);
    EXPECT_EQ(sts::registry::power_game_id(PowerId::THIEVERY), "Thievery");
}

// The un-park gate is `monster_init_fn(id) == nullptr`, asked of the dispatch
// switch directly -- registering looter_init un-parks BOTH remaining Act-1
// encounters with no run-layer edit. This is the closing half of the check
// slaver_test pinned for the four groups the partial landing freed.
TEST(LooterRegistry, TheLastTwoParkedEncountersGoLive) {
    for (const char* key : {"Looter", "Exordium Thugs"}) {
        RngStream misc = from_seed(4711);
        ResolvedGroup group{};
        ASSERT_TRUE(resolve_encounter(key, misc, group)) << key;
        ASSERT_GT(group.count, 0) << key;
        for (uint8_t i = 0; i < group.count; ++i) {
            const MonsterId id = static_cast<MonsterId>(
                sts::registry::monster_from_game_id(group.members[i]));
            EXPECT_NE(id, MonsterId::NONE) << key << " -> " << group.members[i];
            EXPECT_NE(monster_init_fn(id), nullptr)
                << key << " -> " << group.members[i] << " is still parked";
        }
    }
}

}  // namespace
}  // namespace sts::engine
