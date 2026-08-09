// Act-2 city ELITES -- the Gremlin Leader (with the whole SummonGremlinAction
// machinery), the Taskmaster and the Book of Stabbing, plus the two powers they
// bring (Minion, Painful Stabs), the spawn path's new Minion/draw_x operands,
// the ON_INFLICT_DAMAGE binder and the post-`super.die()` escape fan-out.
//
// WHAT THIS FILE PINS, and why each part is here rather than assumed:
//
//   * STAT/MOVE TABLES, per monster, at EVERY ascension branch the Java has --
//     not just the A20 column the engine runs. All three of these classes gate
//     HP on `>= 8` while NAMING the constants `A_2_HP_*`, and two of them have a
//     second `>= 3` branch named `A_2_*` as well, so every boundary is asserted
//     on both sides. The Gremlin Leader's blockAmt row exists to pin a
//     NON-difference (6 on both sides of the `>= 3` arm).
//   * THE TASKMASTER'S DOUBLE monster_hp_rng DRAW. Nothing else in the roster
//     draws in the `super(...)` argument list AND in setHp, and the second draw
//     hides the first: the only way it is visible is as a stream OFFSET, which
//     is what shifts the Red Slaver's HP in the group it ships in.
//   * THE RALLY TURN'S EXACT STREAM ORDER: both summons' pool picks and HP rolls
//     at QUEUE time, both children's init rolls at resolve, and the LEADER's own
//     roll THIRD on ai_rng -- with the Minion/Angry applications landing behind
//     it. This is the single most fragile ordering in the batch.
//   * SMART POSITIONING AND SLOT RECYCLING over the three POSX values, including
//     the leader's own index shifting under its still-queued ROLL_MOVE, which is
//     the hazard pending action_queue items are not remapped against.
//   * getMove's RECURSION, on both arms, with the fresh draw counted.
//   * THE ENCOURAGE WALK'S ASYMMETRY: the leader gets Strength and NO block, and
//     gets it without an isDying guard.
//   * THE ESCAPE TRIGGER: die() queues one ESCAPE per surviving record, the
//     leader excludes itself only because super.die() already ran, and the
//     escapees never telegraph Intent.ESCAPE -- which is what
//     BLOCK_RANDOM_MONSTER's filter reads.
//   * MINION SUPPRESSING FEED AND HAND OF GREED, the two sites whose comments
//     named this landing.
//   * ENCOUNTER COMPOSITIONS, spawn-order-exact.

#include <cstdint>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_book_of_stabbing.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_gremlin.hpp"
#include "sts/engine/monster_gremlin_leader.hpp"
#include "sts/engine/monster_taskmaster.hpp"
#include "sts/engine/power_hooks.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/monster_table.hpp"
#include "sts/registry/power_table.hpp"

namespace sts::engine {
namespace {

namespace r = sts::registry;
constexpr int32_t kA20 = kMonsterAscension;

// --- shared helpers (the city_normals_ii_test shapes) ------------------------

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

const ActionQueueItem& queued(const CombatState& s, uint8_t i) {
    return s.action_queue[(s.action_head + i) % kActionQueueCap];
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

bool monster_has(const CombatState& s, uint8_t mi, PowerId id) {
    const MonsterState& m = s.monsters[mi];
    for (uint8_t i = 0; i < m.power_count; ++i) {
        if (m.powers[i].power_id == static_cast<uint16_t>(id)) {
            return true;
        }
    }
    return false;
}

void give_monster_power(CombatState& s, uint8_t mi, PowerId id, int16_t amt) {
    MonsterState& m = s.monsters[mi];
    m.powers[m.power_count].power_id = static_cast<uint16_t>(id);
    m.powers[m.power_count].amount = amt;
    m.powers[m.power_count].counter = 0;
    ++m.power_count;
}

void player_attacks(CombatState& s, uint8_t mi, int32_t base,
                    DamageType type = DamageType::NORMAL) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    it.src = kActorPlayer;
    it.tgt = mi;
    it.amount = base;
    it.flags = make_damage_flags(type);
    execute_opcode(s, it);
}

int32_t step_amount(const r::MonsterDef& def, uint8_t move, uint8_t k,
                    int32_t asc) {
    const r::MonsterMove* mv = def.move(move);
    return mv == nullptr ? -1 : mv->effects[k].amount.at(asc);
}

uint8_t step_count(const r::MonsterDef& def, uint8_t move) {
    const r::MonsterMove* mv = def.move(move);
    return mv == nullptr ? 0 : mv->effect_count;
}

void telegraph(CombatState& s, uint8_t mi, uint8_t move, MonsterIntent intent) {
    set_monster_move(s.monsters[mi], move, intent);
}

int discard_count_of(const CombatState& s, CardId id) {
    int n = 0;
    for (uint8_t i = 0; i < s.discard_count; ++i) {
        if (s.card_pool[s.discard[i]].card_id == static_cast<uint16_t>(id)) {
            ++n;
        }
    }
    return n;
}

int count_opcodes(const CombatState& s, Opcode op) {
    int n = 0;
    for (uint8_t i = 0; i < s.action_count; ++i) {
        if (queued(s, i).opcode == static_cast<uint16_t>(op)) {
            ++n;
        }
    }
    return n;
}

// The Gremlin Leader group as MonsterHelper builds it, minus the miscRng pool
// picks: two named gremlins then the leader, in that order
// (MonsterHelper.java:507-509). Spawning by an explicit id list is what lets a
// case choose WHICH gremlins it is reasoning about.
CombatState LeaderGroup(int64_t seed, MonsterId a, MonsterId b) {
    const MonsterId group[] = {a, b, MonsterId::GREMLIN_LEADER};
    CombatState s = MakeSeeded(seed, /*monsters=*/0);
    spawn_group(s, group);
    use_pre_battle_actions(s);
    drain(s);
    return s;
}

constexpr uint8_t kLeader = 2;  // the leader is always constructed third

// ============================================================================
// 1. Stat and move tables -- every ascension branch, per monster
// ============================================================================

// GremlinLeader.java:73-87. TWO tier boundaries, and BOTH constant names lie:
// the HP pair is named A_2_HP_* on a `>= 8` branch, and the middle strAmt arm is
// named A_2_STR_AMT on a `>= 3` branch.
TEST(CityElites, GremlinLeaderStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kGremlinLeader;
    EXPECT_EQ(d.hp_min(0), 140);
    EXPECT_EQ(d.hp_max(0), 148);
    EXPECT_EQ(d.hp_min(2), 140) << "the constants are named A_2_HP_* and the "
                                   "branch is >= 8 -- A2 must not move it";
    EXPECT_EQ(d.hp_min(7), 140) << "boundary: still the base column at 7";
    EXPECT_EQ(d.hp_min(8), 145);
    EXPECT_EQ(d.hp_max(8), 155);
    EXPECT_EQ(d.hp_min(kA20), 145);
    EXPECT_EQ(d.hp_max(kA20), 155);

    // strAmt 3 / 4 at >= 3 / 5 at >= 18 (:78-87).
    const uint8_t enc = r::kGremlinLeaderMoveEncourage;
    EXPECT_EQ(step_amount(d, enc, 0, 0), 3);
    EXPECT_EQ(step_amount(d, enc, 0, 2), 3) << "the branch is >= 3, not >= 2";
    EXPECT_EQ(step_amount(d, enc, 0, 3), 4);
    EXPECT_EQ(step_amount(d, enc, 0, 17), 4);
    EXPECT_EQ(step_amount(d, enc, 0, 18), 5);
    EXPECT_EQ(step_amount(d, enc, 0, kA20), 5);

    // blockAmt 6 / STILL 6 at >= 3 / 10 at >= 18. The a3 column is spelled out
    // in the row precisely to pin this NON-change.
    EXPECT_EQ(step_amount(d, enc, 1, 0), 6);
    EXPECT_EQ(step_amount(d, enc, 1, 3), 6)
        << "blockAmt is UNCHANGED across the >= 3 arm (GremlinLeader.java:82-83)";
    EXPECT_EQ(step_amount(d, enc, 1, 17), 6);
    EXPECT_EQ(step_amount(d, enc, 1, 18), 10);

    // STAB_DMG / STAB_AMT are FIELD INITIALIZERS (:62-63) and never branch, so
    // the row carries three flat 6s and no tier column at all.
    const uint8_t stab = r::kGremlinLeaderMoveStab;
    EXPECT_EQ(step_count(d, stab), 3)
        << "three SEPARATE DamageActions (:128-130), not one tripled hit";
    for (int32_t asc : {0, 2, 3, 8, 17, 18, kA20}) {
        EXPECT_EQ(step_amount(d, stab, 0, asc), 6) << "asc " << asc;
        EXPECT_EQ(step_amount(d, stab, 2, asc), 6) << "asc " << asc;
    }

    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::ELITE);
    EXPECT_FALSE(d.is_boss());
    EXPECT_EQ(d.move(r::kGremlinLeaderMoveRally)->intent,
              r::MonsterIntent::UNKNOWN);
    EXPECT_EQ(d.move(enc)->intent, r::MonsterIntent::DEFEND_BUFF);
    EXPECT_EQ(d.move(stab)->intent, r::MonsterIntent::ATTACK);
    EXPECT_EQ(r::kGremlinLeaderMoveRally, 2);
    EXPECT_EQ(r::kGremlinLeaderMoveEncourage, 3);
    EXPECT_EQ(r::kGremlinLeaderMoveStab, 4);
    EXPECT_EQ(d.roll_count, 0) << "the super(...) HP argument is a LITERAL 148 "
                                  "-- unlike the Taskmaster's";
}

// Taskmaster.java:52-59. HP on `>= 8` (constants named A_2_HP_*), woundCount on
// `>= 3` (constant named A_2_WOUNDS) and `>= 18`.
TEST(CityElites, TaskmasterStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kTaskmaster;
    EXPECT_EQ(d.hp_min(0), 54);
    EXPECT_EQ(d.hp_max(0), 60);
    EXPECT_EQ(d.hp_min(7), 54) << "boundary: the branch is >= 8";
    EXPECT_EQ(d.hp_min(8), 57);
    EXPECT_EQ(d.hp_max(8), 64);
    EXPECT_EQ(d.hp_min(kA20), 57);

    const uint8_t whip = r::kTaskmasterMoveScouringWhip;
    EXPECT_EQ(step_count(d, whip), 2)
        << "damage + the Wounds; the A18 Strength is a PRESENCE branch and is "
           "native, not a third step";
    // damage.get(1) == 7, flat.
    for (int32_t asc : {0, 2, 3, 8, 18, kA20}) {
        EXPECT_EQ(step_amount(d, whip, 0, asc), 7) << "asc " << asc;
    }
    // woundCount 1 / 2 at >= 3 / 3 at >= 18.
    EXPECT_EQ(step_amount(d, whip, 1, 0), 1);
    EXPECT_EQ(step_amount(d, whip, 1, 2), 1) << "the branch is >= 3, not >= 2";
    EXPECT_EQ(step_amount(d, whip, 1, 3), 2);
    EXPECT_EQ(step_amount(d, whip, 1, 17), 2);
    EXPECT_EQ(step_amount(d, whip, 1, 18), 3);
    EXPECT_EQ(step_amount(d, whip, 1, kA20), 3);

    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::ELITE);
    EXPECT_EQ(d.move(whip)->intent, r::MonsterIntent::ATTACK_DEBUFF);
    EXPECT_EQ(r::kTaskmasterMoveScouringWhip, 2);
    EXPECT_EQ(d.move(1), nullptr)
        << "damage.get(0) == 4 (WHIP) is DEAD -- getMove only ever sets 2, so "
           "no move row carries the 4";
    // The oracle join key is the Java ID, not the class name.
    EXPECT_EQ(r::monster_game_id(MonsterId::TASKMASTER), "SlaverBoss");
}

// BookOfStabbing.java:62-73. HP on `>= 8`, both damage numbers on `>= 3`.
TEST(CityElites, BookOfStabbingStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kBookOfStabbing;
    EXPECT_EQ(d.hp_min(0), 160);
    EXPECT_EQ(d.hp_max(0), 164);
    EXPECT_EQ(d.hp_min(7), 160) << "boundary: the branch is >= 8";
    EXPECT_EQ(d.hp_min(8), 168);
    EXPECT_EQ(d.hp_max(8), 172);
    EXPECT_EQ(d.hp_min(kA20), 168);

    const uint8_t stab = r::kBookOfStabbingMoveStab;
    const uint8_t big = r::kBookOfStabbingMoveBigStab;
    EXPECT_EQ(step_count(d, stab), 1)
        << "ONE template step -- the repetition count is per-instance state";
    EXPECT_EQ(step_amount(d, stab, 0, 0), 6);
    EXPECT_EQ(step_amount(d, stab, 0, 2), 6) << "the branch is >= 3";
    EXPECT_EQ(step_amount(d, stab, 0, 3), 7);
    EXPECT_EQ(step_amount(d, stab, 0, kA20), 7);
    EXPECT_EQ(step_amount(d, big, 0, 0), 21);
    EXPECT_EQ(step_amount(d, big, 0, 2), 21);
    EXPECT_EQ(step_amount(d, big, 0, 3), 24);
    EXPECT_EQ(step_amount(d, big, 0, kA20), 24);

    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::ELITE);
    EXPECT_EQ(d.move(stab)->intent, r::MonsterIntent::ATTACK);
    EXPECT_EQ(d.move(big)->intent, r::MonsterIntent::ATTACK);
    EXPECT_EQ(r::kBookOfStabbingMoveStab, 1);
    EXPECT_EQ(r::kBookOfStabbingMoveBigStab, 2);
}

// ============================================================================
// 2. The Taskmaster's TWO constructor draws
// ============================================================================

TEST(CityElites, TaskmasterCtorDrawsMonsterHpRngTwiceInTheJavasOrder) {
    // The registry carries the extra draw as data, with the timing that orders
    // it AHEAD of setHp.
    ASSERT_EQ(r::kTaskmaster.roll_count, 1);
    const r::MonsterRollDef* roll =
        r::kTaskmaster.roll(r::kTaskmasterRollSuperArgHp);
    ASSERT_NE(roll, nullptr);
    EXPECT_EQ(roll->stream, r::MonsterRollStream::MONSTER_HP);
    EXPECT_EQ(roll->timing, r::MonsterRollTiming::CONSTRUCTOR_BEFORE_HP);
    // The super-argument is the FLAT literal random(54, 60) at every ascension --
    // only the setHp under it is branched.
    for (int32_t asc : {0, 3, 8, 18, kA20}) {
        EXPECT_EQ(roll->min(asc), 54) << "asc " << asc;
        EXPECT_EQ(roll->max(asc), 60) << "asc " << asc;
    }

    CombatState s = MakeSeeded(7, /*monsters=*/0);
    const MonsterId solo[] = {MonsterId::TASKMASTER};
    spawn_group(s, solo);
    EXPECT_EQ(s.monster_hp_rng.counter, 2)
        << "the super(...) argument (Taskmaster.java:50) AND setHp (:52-56)";
    EXPECT_EQ(s.ai_rng.counter, 1) << "one discarded random(99) at init";

    // And the ORDER is observable: the surviving HP is the SECOND draw over the
    // a8 column, not the first over (54, 60). Re-derived by hand off the same
    // seed.
    RngStream hp = from_seed(7);
    (void)random(hp, 54, 60);
    const int32_t expected = random(hp, 57, 64);
    EXPECT_EQ(s.monsters[0].hp, expected);
    EXPECT_EQ(s.monsters[0].max_hp, expected);
}

TEST(CityElites, TaskmasterExtraDrawShiftsTheRestOfTheSlaversGroup) {
    // The point of the extra draw: it is invisible in the Taskmaster's own HP
    // and visible in everyone constructed after it. The Red Slaver sits at group
    // index 2, so its roll is the FOURTH monster_hp_rng draw, not the third.
    const MonsterId group[] = {MonsterId::SLAVER_BLUE, MonsterId::TASKMASTER,
                               MonsterId::SLAVER_RED};
    CombatState s = MakeSeeded(11, /*monsters=*/0);
    spawn_group(s, group);
    EXPECT_EQ(s.monster_hp_rng.counter, 4)
        << "Blue setHp, Taskmaster super-arg, Taskmaster setHp, Red setHp";

    RngStream hp = from_seed(11);
    (void)random(hp, r::kSlaverBlue.hp_min(kA20), r::kSlaverBlue.hp_max(kA20));
    (void)random(hp, 54, 60);  // the super-argument
    (void)random(hp, r::kTaskmaster.hp_min(kA20), r::kTaskmaster.hp_max(kA20));
    const int32_t red = random(hp, r::kSlaverRed.hp_min(kA20),
                               r::kSlaverRed.hp_max(kA20));
    EXPECT_EQ(s.monsters[2].hp, red)
        << "drop the super-argument draw and this is a different number";
}

// ============================================================================
// 3. Gremlin Leader -- getMove, all three arms and both recursions
// ============================================================================

TEST(CityElites, GremlinLeaderNumAliveGremlinsCountsEveryNonDyingRecordButSelf) {
    CombatState s = MakeState(4);
    s.monsters[0].hp = 5;
    s.monsters[1].hp = 0;     // dying -- excluded
    s.monsters[2].hp = 9;
    s.monsters[3].hp = 100;   // the leader itself
    EXPECT_EQ(gremlin_leader_num_alive_gremlins(s, 3), 2);

    // It does NOT exclude escaped records -- the Java tests only isDying
    // (GremlinLeader.java:194).
    s.monsters[0].flags |= kMonsterFlagEscaped;
    EXPECT_EQ(gremlin_leader_num_alive_gremlins(s, 3), 2)
        << "numAliveGremlins tests isDying alone, never isEscaping";
    // ...and it is not restricted to the three summon slots either: a record
    // with no gremlin position at all still counts.
    EXPECT_EQ(s.monsters[2].draw_x, 0);
}

TEST(CityElites, GremlinLeaderZeroMinionArmTakesNoExtraDraw) {
    // numAliveGremlins == 0 (:146-157): RALLY below 75 unless it just rallied,
    // STAB at or above unless it just stabbed. NEITHER path recurses.
    CombatState s = MakeSeeded(3, /*monsters=*/1);
    s.monsters[0].hp = 100;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::GREMLIN_LEADER);

    gremlin_leader_decide_move(s, 0, 74);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kGremlinLeaderMoveRally);
    EXPECT_EQ(s.ai_rng.counter, 0) << "no recursion on the zero-minion arm";

    gremlin_leader_decide_move(s, 0, 74);  // lastMove is RALLY now
    EXPECT_EQ(s.monsters[0].move_history[0], r::kGremlinLeaderMoveStab);
    gremlin_leader_decide_move(s, 0, 75);  // lastMove is STAB now
    EXPECT_EQ(s.monsters[0].move_history[0], r::kGremlinLeaderMoveRally);
    gremlin_leader_decide_move(s, 0, 99);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kGremlinLeaderMoveStab);
    EXPECT_EQ(s.ai_rng.counter, 0);
}

TEST(CityElites, GremlinLeaderOneMinionLowRollRecursesWithAFreshDraw) {
    // The `num < 50 && lastMove(RALLY)` arm (:163): getMove(aiRng.random(50,99)).
    // The re-draw lands at or above 50, so the recursion cannot re-enter this
    // arm -- one extra draw, then a terminating decision.
    CombatState s = MakeSeeded(5, /*monsters=*/2);
    s.monsters[0].hp = 10;  // one live minion
    s.monsters[1].hp = 100;
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::GREMLIN_LEADER);
    telegraph(s, 1, r::kGremlinLeaderMoveRally, MonsterIntent::UNKNOWN);

    // Pre-compute what the fresh draw will be, so the branch taken is checkable.
    RngStream probe = from_seed(5);
    const int32_t redraw = random(probe, 50, 99);

    gremlin_leader_decide_move(s, 1, 10);
    EXPECT_EQ(s.ai_rng.counter, 1) << "exactly one recursive re-draw";
    // With lastMove == RALLY, the re-entered arms are `num < 80 ->
    // !lastMove(ENCOURAGE) -> ENCOURAGE` and `else -> !lastMove(STAB) -> STAB`.
    const uint8_t expected = redraw < 80 ? r::kGremlinLeaderMoveEncourage
                                         : r::kGremlinLeaderMoveStab;
    EXPECT_EQ(s.monsters[1].move_history[0], expected);
}

TEST(CityElites, GremlinLeaderOneMinionHighRollRecursesAndCanRecurseAgain) {
    // The `num >= 80 && lastMove(STAB)` arm (:174): getMove(aiRng.random(0,80)).
    // A re-drawn 80 lands in the SAME arm, so this is the one path in the whole
    // roster whose draw count is unbounded. Driven with a stream whose first
    // value is forced by search, so the second recursion is really exercised.
    int64_t seed_with_80 = -1;
    for (int64_t seed = 1; seed <= 4000 && seed_with_80 < 0; ++seed) {
        RngStream probe = from_seed(seed);
        if (random(probe, 0, 80) == 80) {
            seed_with_80 = seed;
        }
    }
    ASSERT_GE(seed_with_80, 0) << "no seed in range re-draws an 80";

    CombatState s = MakeSeeded(seed_with_80, /*monsters=*/2);
    s.monsters[0].hp = 10;
    s.monsters[1].hp = 100;
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::GREMLIN_LEADER);
    telegraph(s, 1, r::kGremlinLeaderMoveStab, MonsterIntent::ATTACK);

    gremlin_leader_decide_move(s, 1, 90);
    EXPECT_GE(s.ai_rng.counter, 2)
        << "the first re-draw was 80, which re-enters the same arm";
    // Whatever it settles on, it terminates and telegraphs a real move.
    EXPECT_NE(s.monsters[1].move_history[0], 0);
}

TEST(CityElites, GremlinLeaderTwoMinionArmIsPurelyHistoryDriven) {
    // numAliveGremlins > 1 (:176-188). No recursion, no draws.
    CombatState s = MakeSeeded(9, /*monsters=*/3);
    s.monsters[0].hp = 10;
    s.monsters[1].hp = 10;
    s.monsters[2].hp = 100;
    s.monsters[2].monster_id = static_cast<uint16_t>(MonsterId::GREMLIN_LEADER);

    gremlin_leader_decide_move(s, kLeader, 65);
    EXPECT_EQ(s.monsters[kLeader].move_history[0],
              r::kGremlinLeaderMoveEncourage);
    gremlin_leader_decide_move(s, kLeader, 65);  // lastMove ENCOURAGE
    EXPECT_EQ(s.monsters[kLeader].move_history[0], r::kGremlinLeaderMoveStab);
    gremlin_leader_decide_move(s, kLeader, 66);  // lastMove STAB
    EXPECT_EQ(s.monsters[kLeader].move_history[0],
              r::kGremlinLeaderMoveEncourage);
    gremlin_leader_decide_move(s, kLeader, 99);  // lastMove ENCOURAGE
    EXPECT_EQ(s.monsters[kLeader].move_history[0], r::kGremlinLeaderMoveStab);
    EXPECT_EQ(s.ai_rng.counter, 0)
        << "RALLY is UNREACHABLE with two live minions, and so is any recursion";
}

// ============================================================================
// 4. Gremlin Leader -- spawn, pre-battle, and the Minion markers
// ============================================================================

TEST(CityElites, GremlinLeaderSpawnDrawsOncePerMemberAndOpensOnTheGroupArm) {
    CombatState s = MakeSeeded(13, /*monsters=*/0);
    const MonsterId group[] = {MonsterId::GREMLIN_FAT, MonsterId::GREMLIN_THIEF,
                               MonsterId::GREMLIN_LEADER};
    spawn_group(s, group);
    EXPECT_EQ(s.monster_count, 3);
    EXPECT_EQ(s.monster_hp_rng.counter, 3) << "one setHp each; no extra draws";
    EXPECT_EQ(s.ai_rng.counter, 3) << "one rollMove each";
    EXPECT_EQ(s.monsters[kLeader].draw_x, kGremlinLeaderDrawX);
    // Both minions are alive at the leader's init, so its opening decision comes
    // from the `> 1` arm -- which can never be RALLY.
    EXPECT_NE(s.monsters[kLeader].move_history[0], r::kGremlinLeaderMoveRally)
        << "a full group never opens on RALLY";
}

TEST(CityElites, GremlinLeaderPreBattleAppliesTwoMinionsAtMinusOneAndSetsPositions) {
    CombatState s = MakeSeeded(17, /*monsters=*/0);
    const MonsterId group[] = {MonsterId::GREMLIN_FAT, MonsterId::GREMLIN_THIEF,
                               MonsterId::GREMLIN_LEADER};
    spawn_group(s, group);
    use_pre_battle_actions(s);

    // Exactly TWO applications reach the queue. The Java's loop runs three
    // times; the third target is `gremlins[2] == null` and ApplyPowerAction
    // no-ops on it (ApplyPowerAction.java:96-99).
    EXPECT_EQ(count_opcodes(s, Opcode::APPLY_POWER), 2)
        << "the null third slot is a no-op, not a third item";
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::MINION), kMinionAppliedAmount);
    EXPECT_EQ(monster_power(s, 1, PowerId::MINION), kMinionAppliedAmount);
    EXPECT_EQ(kMinionAppliedAmount, -1)
        << "MinionPower never assigns amount -> AbstractPower's -1, NOT 1";
    EXPECT_FALSE(monster_has(s, kLeader, PowerId::MINION))
        << "the leader is the OWNER argument, never a recipient";

    // The two encounter minions take GremlinLeader.POSX[0] and POSX[1].
    EXPECT_EQ(s.monsters[0].draw_x, kGremlinLeaderSlotX[0]);
    EXPECT_EQ(s.monsters[1].draw_x, kGremlinLeaderSlotX[1]);
    EXPECT_EQ(kGremlinLeaderSlotX[0], -366);
    EXPECT_EQ(kGremlinLeaderSlotX[1], -170);
    EXPECT_EQ(kGremlinLeaderSlotX[2], -532);
    EXPECT_LT(kGremlinLeaderSlotX[0], kGremlinLeaderDrawX)
        << "every summon position is LEFT of the leader, which is why every "
           "insert shifts the leader's index";
}

// ============================================================================
// 5. RALLY -- the batch's most fragile ordering
// ============================================================================

TEST(CityElites, RallyDrawsBothPoolPicksAndBothHpRollsAtQueueTime) {
    CombatState s = LeaderGroup(23, MonsterId::GREMLIN_FAT,
                                MonsterId::GREMLIN_THIEF);
    // Kill both minions so numAliveGremlins is 0 and RALLY is legal.
    s.monsters[0].hp = 0;
    s.monsters[1].hp = 0;
    telegraph(s, kLeader, r::kGremlinLeaderMoveRally, MonsterIntent::UNKNOWN);

    const int32_t ai_before = s.ai_rng.counter;
    const int32_t hp_before = s.monster_hp_rng.counter;
    gremlin_leader_take_turn(s, kLeader);

    // QUEUE TIME: two aiRng pool picks and two monster_hp_rng HP rolls, and
    // NOTHING else -- the children's init rolls have not happened yet.
    EXPECT_EQ(s.ai_rng.counter - ai_before, 2)
        << "SummonGremlinAction's CONSTRUCTOR draws the pool pick "
           "(SummonGremlinAction.java:89), once per summon";
    EXPECT_EQ(s.monster_hp_rng.counter - hp_before, 2)
        << "the gremlin ctor's setHp, also at queue time";
    EXPECT_EQ(s.monster_count, 3) << "no record exists until resolve";

    // The queue is [Spawn, Spawn, RollMove] -- the two summons AHEAD of the
    // trailing roll, which is what puts the leader's own roll third on ai_rng.
    ASSERT_EQ(s.action_count, 3);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::SPAWN_MONSTER));
    EXPECT_EQ(queued(s, 1).opcode, static_cast<uint16_t>(Opcode::SPAWN_MONSTER));
    EXPECT_EQ(queued(s, 2).opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE));

    // Both spawn items carry the summon pattern: pre-battle AND Minion.
    for (uint8_t i = 0; i < 2; ++i) {
        EXPECT_NE(queued(s, i).flags & kSpawnRunPreBattle, 0u);
        EXPECT_NE(queued(s, i).flags & kSpawnApplyMinion, 0u);
    }
    // ...and the leader's ROLL_MOVE is aimed at its POST-INSERTION index, two
    // to the right, because both summons insert below it and pending queue items
    // are not remapped.
    EXPECT_EQ(queued(s, 2).tgt, kLeader + 2);
}

TEST(CityElites, RallyResolvesChildRollsBeforeTheLeadersAndMinionBeforeAngry) {
    CombatState s = LeaderGroup(29, MonsterId::GREMLIN_FAT,
                                MonsterId::GREMLIN_THIEF);
    s.monsters[0].hp = 0;
    s.monsters[1].hp = 0;
    telegraph(s, kLeader, r::kGremlinLeaderMoveRally, MonsterIntent::UNKNOWN);
    gremlin_leader_take_turn(s, kLeader);

    // Resolve the three items one at a time so the ai_rng order is checkable.
    const int32_t after_queue = s.ai_rng.counter;
    ActionQueueItem it = s.action_queue[s.action_head];
    s.action_head = static_cast<uint8_t>((s.action_head + 1) % kActionQueueCap);
    --s.action_count;
    execute_opcode(s, it);  // spawn #1
    EXPECT_EQ(s.ai_rng.counter - after_queue, 1) << "child #1's init rollMove";
    EXPECT_EQ(s.monster_count, 4);

    it = s.action_queue[s.action_head];
    s.action_head = static_cast<uint8_t>((s.action_head + 1) % kActionQueueCap);
    --s.action_count;
    execute_opcode(s, it);  // spawn #2
    EXPECT_EQ(s.ai_rng.counter - after_queue, 2) << "child #2's init rollMove";
    EXPECT_EQ(s.monster_count, 5);

    // The leader's own roll is THIRD, and it lands on the record the queued
    // index was pre-computed for.
    const uint8_t leader_now = kLeader + 2;
    ASSERT_EQ(s.monsters[leader_now].monster_id,
              static_cast<uint16_t>(MonsterId::GREMLIN_LEADER));
    it = s.action_queue[s.action_head];
    EXPECT_EQ(it.opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE));
    EXPECT_EQ(it.tgt, leader_now);
    s.action_head = static_cast<uint8_t>((s.action_head + 1) % kActionQueueCap);
    --s.action_count;
    execute_opcode(s, it);
    EXPECT_EQ(s.ai_rng.counter - after_queue, 3)
        << "the leader's RollMoveAction resolves AFTER both children's inits";

    // What is left is the two spawns' appended power items, in the Java's order:
    // Minion #1, [Angry #1], Minion #2, [Angry #2].
    ASSERT_GE(s.action_count, 2);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::APPLY_POWER));
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 0).flags), PowerId::MINION);
    EXPECT_EQ(queued(s, 0).amount, kMinionAppliedAmount);
    drain(s);
    // Every summoned record carries the Minion marker.
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        if (s.monsters[i].monster_id ==
            static_cast<uint16_t>(MonsterId::GREMLIN_LEADER)) {
            continue;
        }
        EXPECT_TRUE(monster_has(s, i, PowerId::MINION)) << "slot " << int(i);
    }
}

TEST(CityElites, ASummonedGremlinWarriorGetsAngryAfterItsMinion) {
    // The pre-battle bit is what SpawnMonsterAction does NOT do, and the Warrior
    // is the only member of the summon pool that has a pre-battle action at all.
    CombatState s = MakeSeeded(31, /*monsters=*/1);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::GREMLIN_LEADER);
    s.monsters[0].hp = 100;
    s.monsters[0].max_hp = 100;
    s.monsters[0].draw_x = kGremlinLeaderDrawX;

    ActionQueueItem spawn{};
    spawn.opcode = static_cast<uint16_t>(Opcode::SPAWN_MONSTER);
    spawn.src = 0;
    spawn.tgt = 0;
    spawn.amount = 20;
    spawn.flags = make_spawn_monster_flags(
        static_cast<uint16_t>(MonsterId::GREMLIN_WARRIOR),
        kGremlinLeaderSlotX[0], /*run_pre_battle=*/true, /*apply_minion=*/true);
    execute_opcode(s, spawn);

    ASSERT_EQ(s.monster_count, 2);
    EXPECT_EQ(s.monsters[0].monster_id,
              static_cast<uint16_t>(MonsterId::GREMLIN_WARRIOR));
    EXPECT_EQ(s.monsters[0].hp, 20) << "the HP was drawn by the summoner";
    EXPECT_EQ(s.monsters[0].max_hp, 20);
    EXPECT_EQ(s.monsters[0].draw_x, kGremlinLeaderSlotX[0]);

    // Minion FIRST, Angry SECOND (SummonGremlinAction.java:114 then :120).
    ASSERT_EQ(s.action_count, 2);
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 0).flags), PowerId::MINION);
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 1).flags), PowerId::ANGRY);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::MINION), -1);
    EXPECT_EQ(monster_power(s, 0, PowerId::ANGRY), 2) << "A17+ Angry is 2";
}

TEST(CityElites, SpawnFlagsRoundTripDrawXAndLeaveLandedCallersAtZero) {
    // The encoding has to be identity-preserving for every position in Acts 1-3
    // AND leave a bare-MonsterId flags word decoding to draw_x 0, which is what
    // both large-slime split sites write.
    for (int16_t x : {int16_t(-532), int16_t(-366), int16_t(-250), int16_t(-170),
                      int16_t(0), int16_t(35), int16_t(210), int16_t(254),
                      // The field's extremes. S2.27 NARROWED it from 14 signed
                      // bits to 13 (-4096..4095) to free bit 31 for
                      // kSpawnMinionAtTop; every real position in Acts 1-3 is
                      // an order of magnitude inside that, and the bounds are
                      // pinned here so a future widening is a deliberate act.
                      int16_t(-4096), int16_t(4095)}) {
        const uint32_t f = make_spawn_monster_flags(
            static_cast<uint16_t>(MonsterId::GREMLIN_FAT), x, true, true);
        EXPECT_EQ(spawn_draw_x_from_flags(f), x);
        EXPECT_EQ(f & 0xFFFFu, static_cast<uint32_t>(MonsterId::GREMLIN_FAT));
    }
    const uint32_t bare =
        static_cast<uint32_t>(MonsterId::SPIKE_SLIME_MEDIUM);
    EXPECT_EQ(spawn_draw_x_from_flags(bare), 0)
        << "the slime split writes only the id and must keep draw_x 0";
    EXPECT_EQ(bare & kSpawnRunPreBattle, 0u);
    EXPECT_EQ(bare & kSpawnApplyMinion, 0u);
}

TEST(CityElites, SummonSlotsAreDerivedFromLivePositionsAndAreRecycled) {
    // identifySlot's answer is "the first POSX with no LIVE record on it"
    // (monster_gremlin_leader.hpp note (5)). With both encounter minions dead,
    // slots 0 and 1 are free again and the two summons take them in order --
    // which also fixes the insertion positions, since POSX[2] < POSX[0] <
    // POSX[1] < leader.
    CombatState s = LeaderGroup(37, MonsterId::GREMLIN_FAT,
                                MonsterId::GREMLIN_THIEF);
    s.monsters[0].hp = 0;
    s.monsters[1].hp = 0;
    telegraph(s, kLeader, r::kGremlinLeaderMoveRally, MonsterIntent::UNKNOWN);
    gremlin_leader_take_turn(s, kLeader);

    EXPECT_EQ(spawn_draw_x_from_flags(queued(s, 0).flags),
              kGremlinLeaderSlotX[0]);
    EXPECT_EQ(spawn_draw_x_from_flags(queued(s, 1).flags),
              kGremlinLeaderSlotX[1]);
    // Positions: the list is [-366(dead), -170(dead), 35]. A newcomer at -366
    // stops at the first record it is not strictly right of -- index 0. Then the
    // list is [-366, -366, -170, 35] and a newcomer at -170 stops at index 2.
    EXPECT_EQ(queued(s, 0).tgt, 0);
    EXPECT_EQ(queued(s, 1).tgt, 2);
    drain(s);
    EXPECT_EQ(s.monster_count, 5);
    // Final list order by position key, dead records included.
    const int16_t expect_x[] = {kGremlinLeaderSlotX[0], kGremlinLeaderSlotX[0],
                                kGremlinLeaderSlotX[1], kGremlinLeaderSlotX[1],
                                kGremlinLeaderDrawX};
    for (uint8_t i = 0; i < 5; ++i) {
        EXPECT_EQ(s.monsters[i].draw_x, expect_x[i]) << "slot " << int(i);
    }
}

TEST(CityElites, SummonWithOneLiveMinionTakesTheNextTwoFreeSlots) {
    // numAliveGremlins == 1 is the other state RALLY is reachable from. Slot 0
    // is occupied by the survivor, so the summons take slots 1 and 2 -- and slot
    // 2 is POSX[2] == -532, the LEFTMOST position, which always inserts at 0.
    CombatState s = LeaderGroup(41, MonsterId::GREMLIN_FAT,
                                MonsterId::GREMLIN_THIEF);
    s.monsters[1].hp = 0;  // the POSX[1] minion dies; the POSX[0] one lives
    telegraph(s, kLeader, r::kGremlinLeaderMoveRally, MonsterIntent::UNKNOWN);
    gremlin_leader_take_turn(s, kLeader);

    EXPECT_EQ(spawn_draw_x_from_flags(queued(s, 0).flags),
              kGremlinLeaderSlotX[1]);
    EXPECT_EQ(spawn_draw_x_from_flags(queued(s, 1).flags),
              kGremlinLeaderSlotX[2]);
    // -170 walks past -366 and stops at the second record (also -170) -> 1.
    EXPECT_EQ(queued(s, 0).tgt, 1);
    // -532 is left of everything, so it stops immediately -> 0.
    EXPECT_EQ(queued(s, 1).tgt, 0);
    // The leader started at 2 and both inserts land at or below it: 2 -> 3 -> 4.
    EXPECT_EQ(queued(s, 2).tgt, 4);
}

TEST(CityElites, TheSummonPoolIsTheEightEntryAiRngListAndIsDrawnWithReplacement) {
    // Membership, weighting AND stream. The identical eight-entry list on
    // misc_rng is the ENCOUNTER's (encounters.yaml id 35) and must stay separate.
    int warriors = 0, thieves = 0, fats = 0, tsunderes = 0, wizards = 0;
    int duplicate_pairs = 0;
    for (int64_t seed = 1; seed <= 120; ++seed) {
        CombatState s = LeaderGroup(seed, MonsterId::GREMLIN_FAT,
                                    MonsterId::GREMLIN_THIEF);
        s.monsters[0].hp = 0;
        s.monsters[1].hp = 0;
        const int32_t misc_before = s.card_random_rng.counter;
        telegraph(s, kLeader, r::kGremlinLeaderMoveRally, MonsterIntent::UNKNOWN);
        gremlin_leader_take_turn(s, kLeader);
        EXPECT_EQ(s.card_random_rng.counter, misc_before)
            << "the summon pool is ai_rng; nothing else moves";

        const auto id_of = [&](uint8_t i) {
            return static_cast<MonsterId>(queued(s, i).flags & 0xFFFFu);
        };
        if (id_of(0) == id_of(1)) {
            ++duplicate_pairs;  // drawn WITH replacement
        }
        for (uint8_t i = 0; i < 2; ++i) {
            switch (id_of(i)) {
                case MonsterId::GREMLIN_WARRIOR: ++warriors; break;
                case MonsterId::GREMLIN_THIEF: ++thieves; break;
                case MonsterId::GREMLIN_FAT: ++fats; break;
                case MonsterId::GREMLIN_TSUNDERE: ++tsunderes; break;
                case MonsterId::GREMLIN_WIZARD: ++wizards; break;
                default: ADD_FAILURE() << "a non-gremlin came out of the pool";
            }
        }
    }
    EXPECT_GT(warriors, 0);
    EXPECT_GT(thieves, 0);
    EXPECT_GT(fats, 0);
    EXPECT_GT(tsunderes, 0);
    EXPECT_GT(wizards, 0);
    EXPECT_GT(duplicate_pairs, 0)
        << "the pool is rebuilt per call and never removes -- duplicates are "
           "legal, unlike Gremlin Gang's draw-without-replacement";
    // The weighting: Warrior/Thief/Fat have TWO slots each, Tsundere and Wizard
    // one. Over 240 picks the doubled entries must clearly outweigh the singles.
    EXPECT_GT(warriors, wizards);
    EXPECT_GT(fats, tsunderes);
}

// ============================================================================
// 6. ENCOURAGE -- the asymmetric walk
// ============================================================================

TEST(CityElites, EncourageGivesTheLeaderStrengthButNoBlock) {
    CombatState s = LeaderGroup(43, MonsterId::GREMLIN_FAT,
                                MonsterId::GREMLIN_THIEF);
    telegraph(s, kLeader, r::kGremlinLeaderMoveEncourage,
              MonsterIntent::DEFEND_BUFF);
    const int32_t ai_before = s.ai_rng.counter;
    gremlin_leader_take_turn(s, kLeader);

    // getEncourageQuote's aiRng.random(0, 2) is a SEEDED draw at QUEUE time.
    EXPECT_EQ(s.ai_rng.counter - ai_before, 1)
        << "the ShoutAction argument is evaluated before addToBottom "
           "(GremlinLeader.java:113,141)";

    // Group order: Str(0), Blk(0), Str(1), Blk(1), Str(leader), then ROLL_MOVE.
    // The leader's own Strength is queued LAST and carries NO block.
    ASSERT_EQ(s.action_count, 6);
    const uint8_t str_step = 0;
    (void)str_step;
    EXPECT_EQ(queued(s, 0).tgt, 0);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::APPLY_POWER));
    EXPECT_EQ(queued(s, 1).tgt, 0);
    EXPECT_EQ(queued(s, 1).opcode, static_cast<uint16_t>(Opcode::BLOCK));
    EXPECT_EQ(queued(s, 2).tgt, 1);
    EXPECT_EQ(queued(s, 2).opcode, static_cast<uint16_t>(Opcode::APPLY_POWER));
    EXPECT_EQ(queued(s, 3).tgt, 1);
    EXPECT_EQ(queued(s, 3).opcode, static_cast<uint16_t>(Opcode::BLOCK));
    EXPECT_EQ(queued(s, 4).tgt, kLeader);
    EXPECT_EQ(queued(s, 4).opcode, static_cast<uint16_t>(Opcode::APPLY_POWER))
        << "the `m == this` arm queues Strength and `continue`s -- no block";
    EXPECT_EQ(queued(s, 5).opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE));
    EXPECT_EQ(queued(s, 5).tgt, kLeader) << "no record was inserted this turn";

    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), 5) << "strAmt at A20";
    EXPECT_EQ(monster_power(s, 1, PowerId::STRENGTH), 5);
    EXPECT_EQ(monster_power(s, kLeader, PowerId::STRENGTH), 5);
    EXPECT_EQ(s.monsters[0].block, 10) << "blockAmt at A20";
    EXPECT_EQ(s.monsters[1].block, 10);
    EXPECT_EQ(s.monsters[kLeader].block, 0)
        << "the leader buffs itself and shields itself NOT AT ALL";
}

TEST(CityElites, EncourageSkipsDyingMinionsButNeverSkipsItself) {
    CombatState s = LeaderGroup(47, MonsterId::GREMLIN_FAT,
                                MonsterId::GREMLIN_THIEF);
    s.monsters[0].hp = 0;  // isDying -- skipped entirely
    telegraph(s, kLeader, r::kGremlinLeaderMoveEncourage,
              MonsterIntent::DEFEND_BUFF);
    gremlin_leader_take_turn(s, kLeader);
    ASSERT_EQ(s.action_count, 4);  // Str(1), Blk(1), Str(leader), ROLL_MOVE
    EXPECT_EQ(queued(s, 0).tgt, 1);
    EXPECT_EQ(queued(s, 2).tgt, kLeader);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), -1);
    EXPECT_EQ(monster_power(s, kLeader, PowerId::STRENGTH), 5);

    // And the `m == this` arm is NOT isDying-guarded: a leader on its last legs
    // still buffs itself. (Reached here by driving the body directly, which is
    // exactly what a queued ENCOURAGE resolving after a lethal hit would do.)
    CombatState d = LeaderGroup(53, MonsterId::GREMLIN_FAT,
                                MonsterId::GREMLIN_THIEF);
    d.monsters[0].hp = 0;
    d.monsters[1].hp = 0;
    d.monsters[kLeader].hp = 0;
    telegraph(d, kLeader, r::kGremlinLeaderMoveEncourage,
              MonsterIntent::DEFEND_BUFF);
    gremlin_leader_take_turn(d, kLeader);
    ASSERT_EQ(d.action_count, 2);
    EXPECT_EQ(queued(d, 0).tgt, kLeader);
    EXPECT_EQ(queued(d, 0).opcode, static_cast<uint16_t>(Opcode::APPLY_POWER))
        << "the self arm has no isDying guard (GremlinLeader.java:115-118)";
}

TEST(CityElites, StabQueuesThreeSeparateSixDamageHits) {
    CombatState s = LeaderGroup(59, MonsterId::GREMLIN_FAT,
                                MonsterId::GREMLIN_THIEF);
    telegraph(s, kLeader, r::kGremlinLeaderMoveStab, MonsterIntent::ATTACK);
    const int32_t ai_before = s.ai_rng.counter;
    gremlin_leader_take_turn(s, kLeader);
    EXPECT_EQ(s.ai_rng.counter, ai_before) << "STAB draws nothing at queue time";
    ASSERT_EQ(s.action_count, 4);
    for (uint8_t i = 0; i < 3; ++i) {
        EXPECT_EQ(queued(s, i).opcode, static_cast<uint16_t>(Opcode::DAMAGE));
        EXPECT_EQ(queued(s, i).tgt, kActorPlayer);
        EXPECT_EQ(queued(s, i).amount, 6);
    }
    EXPECT_EQ(queued(s, 3).opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE));
}

// ============================================================================
// 7. The escape trigger -- the stage-b row's discharge, exercised
// ============================================================================

TEST(CityElites, GremlinLeaderDeathQueuesOneEscapePerSurvivorAndNoneForItself) {
    CombatState s = LeaderGroup(61, MonsterId::GREMLIN_FAT,
                                MonsterId::GREMLIN_THIEF);
    // The die() body is the POST-super slot, and the pre-super slot is empty.
    EXPECT_TRUE(monster_die_fn(MonsterId::GREMLIN_LEADER) == nullptr)
        << "everything GremlinLeader.die() does runs after super.die()";
    ASSERT_NE(monster_die_after_fn(MonsterId::GREMLIN_LEADER), nullptr);

    // Kill the leader through the real damage path so the whole death edge runs.
    player_attacks(s, kLeader, s.monsters[kLeader].hp + 50);
    EXPECT_EQ(s.monsters[kLeader].hp, 0);
    EXPECT_EQ(count_opcodes(s, Opcode::ESCAPE), 2)
        << "one per non-dying record; the leader excludes ITSELF only because "
           "super.die() already zeroed it";
    for (uint8_t i = 0; i < s.action_count; ++i) {
        EXPECT_NE(queued(s, i).tgt, kLeader);
    }
    drain(s);
    EXPECT_TRUE(monster_escaped(s.monsters[0]));
    EXPECT_TRUE(monster_escaped(s.monsters[1]));
    EXPECT_GT(s.monsters[0].hp, 0) << "an escapee is NOT dying";
    EXPECT_TRUE(monster_dead_or_escaped(s.monsters[0]));
    EXPECT_TRUE(monster_basically_dead(s.monsters[0]));
}

TEST(CityElites, ADeadMinionGetsNoEscapeAndSummonedOnesDo) {
    CombatState s = LeaderGroup(67, MonsterId::GREMLIN_FAT,
                                MonsterId::GREMLIN_THIEF);
    s.monsters[0].hp = 0;  // already dying
    // Add a summoned record so the fan-out is proved to walk the WHOLE list.
    ActionQueueItem spawn{};
    spawn.opcode = static_cast<uint16_t>(Opcode::SPAWN_MONSTER);
    spawn.src = kLeader;
    spawn.tgt = 0;
    spawn.amount = 15;
    spawn.flags = make_spawn_monster_flags(
        static_cast<uint16_t>(MonsterId::GREMLIN_TSUNDERE),
        kGremlinLeaderSlotX[2], true, true);
    execute_opcode(s, spawn);
    drain(s);
    ASSERT_EQ(s.monster_count, 4);

    player_attacks(s, 3, s.monsters[3].hp + 50);  // the leader shifted to 3
    EXPECT_EQ(count_opcodes(s, Opcode::ESCAPE), 2)
        << "the summoned Tsundere and the surviving encounter minion; the dead "
           "one and the leader are both skipped";
    drain(s);
    EXPECT_TRUE(monster_escaped(s.monsters[0])) << "the summoned Tsundere";
    EXPECT_FALSE(monster_escaped(s.monsters[1])) << "already dead, never escapes";
}

TEST(CityElites, AnEscapedGremlinNeverTelegraphsEscapeIntent) {
    // The whole reason the stage-b row discharges as a FINDING rather than a
    // body: move 99 is never entered, so the escapee's intent is whatever it was
    // already showing. That is what BLOCK_RANDOM_MONSTER's filter reads
    // (GainBlockRandomMonsterAction.java:26-38), NOT the escaped flag -- so the
    // engine must NOT "improve" the filter into monster_dead_or_escaped.
    CombatState s = LeaderGroup(71, MonsterId::GREMLIN_TSUNDERE,
                                MonsterId::GREMLIN_THIEF);
    const uint8_t intent_before = s.monsters[0].intent;
    const uint8_t move_before = s.monsters[0].move_history[0];
    player_attacks(s, kLeader, s.monsters[kLeader].hp + 50);
    drain(s);
    ASSERT_TRUE(monster_escaped(s.monsters[0]));
    EXPECT_EQ(s.monsters[0].intent, intent_before)
        << "no re-telegraph, unlike the Looter's and the Mugger's escapes";
    EXPECT_EQ(s.monsters[0].move_history[0], move_before)
        << "move 99 is never set -- it stays unreachable in every act";
    EXPECT_NE(s.monsters[0].intent,
              static_cast<uint8_t>(r::MonsterIntent::ESCAPE));

    // And therefore an escaped-but-alive ally is still a legal
    // BLOCK_RANDOM_MONSTER recipient, exactly as it is in the Java. Pinned as a
    // reading, not silently relied on.
    CombatState b = MakeSeeded(73, /*monsters=*/2);
    b.monsters[0].hp = 10;
    b.monsters[0].flags |= kMonsterFlagEscaped;
    b.monsters[1].hp = 10;
    ActionQueueItem blk{};
    blk.opcode = static_cast<uint16_t>(Opcode::BLOCK_RANDOM_MONSTER);
    blk.src = 1;
    blk.tgt = 1;
    blk.amount = 7;
    execute_opcode(b, blk);
    EXPECT_EQ(b.monsters[0].block, 7)
        << "the filter tests the TELEGRAPHED intent and isDying, never the "
           "escaped flag -- and the Java does the same";
}

// ============================================================================
// 8. Minion -- the two sites whose comments named this landing
// ============================================================================

TEST(CityElites, MinionSuppressesFeedsMaxHpAndHandOfGreedsGold) {
    CombatState s = MakeState(1);
    s.monsters[0].monster_id =
        static_cast<uint16_t>(MonsterId::GREMLIN_WARRIOR);
    s.monsters[0].hp = 4;
    s.monsters[0].max_hp = 4;
    give_monster_power(s, 0, PowerId::MINION, kMinionAppliedAmount);
    const int16_t before_max = s.player_max_hp;

    ActionQueueItem feed{};
    feed.opcode = static_cast<uint16_t>(Opcode::DAMAGE_FEED);
    feed.src = kActorPlayer;
    feed.tgt = 0;
    feed.amount = 10;
    feed.flags = 3;
    execute_opcode(s, feed);
    EXPECT_EQ(s.monsters[0].hp, 0);
    EXPECT_EQ(s.player_max_hp, before_max)
        << "FeedAction.java:38 -- killing a minion grants no max HP";

    CombatState g = MakeState(1);
    g.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::GREMLIN_FAT);
    g.monsters[0].hp = 4;
    g.monsters[0].max_hp = 4;
    give_monster_power(g, 0, PowerId::MINION, kMinionAppliedAmount);
    ActionQueueItem greed{};
    greed.opcode = static_cast<uint16_t>(Opcode::DAMAGE_GREED);
    greed.src = kActorPlayer;
    greed.tgt = 0;
    greed.amount = 10;
    greed.flags = make_damage_greed_flags(20);
    execute_opcode(g, greed);
    EXPECT_EQ(g.monsters[0].hp, 0);
    EXPECT_EQ(g.combat_gold, 0)
        << "GreedAction.java:37 -- killing a minion pays no gold";
}

TEST(CityElites, WithoutMinionFeedAndGreedStillPay) {
    // The negative control: the same two kills on a record with no marker.
    CombatState s = MakeState(1);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = 4;
    s.monsters[0].max_hp = 4;
    ActionQueueItem feed{};
    feed.opcode = static_cast<uint16_t>(Opcode::DAMAGE_FEED);
    feed.src = kActorPlayer;
    feed.tgt = 0;
    feed.amount = 10;
    feed.flags = 3;
    execute_opcode(s, feed);
    EXPECT_EQ(s.player_max_hp, 403);

    CombatState g = MakeState(1);
    g.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    g.monsters[0].hp = 4;
    g.monsters[0].max_hp = 4;
    ActionQueueItem greed{};
    greed.opcode = static_cast<uint16_t>(Opcode::DAMAGE_GREED);
    greed.src = kActorPlayer;
    greed.tgt = 0;
    greed.amount = 10;
    greed.flags = make_damage_greed_flags(20);
    execute_opcode(g, greed);
    EXPECT_EQ(g.combat_gold, 20);
}

TEST(CityElites, MinionIsAPureMarkerAndPainfulStabsBindsExactlyOneHook) {
    const r::PowerDef* minion = r::power_def(PowerId::MINION);
    ASSERT_NE(minion, nullptr);
    EXPECT_EQ(minion->type, r::PowerType::BUFF);
    EXPECT_FALSE(minion->native)
        << "MinionPower's only member past the ctor is updateDescription";
    EXPECT_EQ(minion->hook_count, 0);
    EXPECT_EQ(r::power_game_id(PowerId::MINION), "Minion");

    const r::PowerDef* stabs = r::power_def(PowerId::PAINFUL_STABS);
    ASSERT_NE(stabs, nullptr);
    EXPECT_EQ(stabs->type, r::PowerType::BUFF)
        << "PainfulStabsPower assigns no type -> AbstractPower's default";
    EXPECT_TRUE(stabs->native);
    EXPECT_EQ(r::power_game_id(PowerId::PAINFUL_STABS), "Painful Stabs")
        << "WITH the space -- it is the oracle join key";
    EXPECT_EQ(static_cast<int>(Hook::ON_INFLICT_DAMAGE), 17);
}

// ============================================================================
// 9. Book of Stabbing -- the growing counter and its per-hit Wounds
// ============================================================================

TEST(CityElites, BookOfStabbingInitLeavesTheCounterAtTwoAtA20) {
    CombatState s = MakeSeeded(79, /*monsters=*/0);
    const MonsterId solo[] = {MonsterId::BOOK_OF_STABBING};
    spawn_group(s, solo);
    EXPECT_EQ(s.monster_hp_rng.counter, 1);
    EXPECT_EQ(s.ai_rng.counter, 1);
    EXPECT_EQ(s.monsters[0].pad0, 2)
        << "stabCount starts at 1 and EVERY A18+ decision increments -- "
           "including the init rollMove, so the first STAB is already 2 hits";
}

TEST(CityElites, BookOfStabbingGetMoveIncrementsOnAllFourPathsAtA18) {
    // The four paths of BookOfStabbing.java:129-150, driven directly at both
    // sides of the A18 gate. `num` and the history decide the move; the
    // ascension decides how many of the paths also grow the counter.
    struct Case { int32_t num; uint8_t last; uint8_t last2; uint8_t move; };
    // path 1: num < 15 && lastMove(BIG_STAB) -> STAB, ALWAYS ++
    CombatState a = MakeSeeded(83, 1);
    a.monsters[0].pad0 = 3;
    telegraph(a, 0, r::kBookOfStabbingMoveBigStab, MonsterIntent::ATTACK);
    book_of_stabbing_decide_move(a, 0, 10, /*ascension=*/1);
    EXPECT_EQ(a.monsters[0].move_history[0], r::kBookOfStabbingMoveStab);
    EXPECT_EQ(a.monsters[0].pad0, 4) << "path 1 increments at EVERY ascension";

    // path 2: num < 15, no BIG_STAB behind -> BIG_STAB, ++ only at A18
    CombatState b = MakeSeeded(83, 1);
    b.monsters[0].pad0 = 3;
    telegraph(b, 0, r::kBookOfStabbingMoveStab, MonsterIntent::ATTACK);
    book_of_stabbing_decide_move(b, 0, 10, /*ascension=*/17);
    EXPECT_EQ(b.monsters[0].move_history[0], r::kBookOfStabbingMoveBigStab);
    EXPECT_EQ(b.monsters[0].pad0, 3) << "below A18 this path does not grow";
    CombatState b18 = MakeSeeded(83, 1);
    b18.monsters[0].pad0 = 3;
    telegraph(b18, 0, r::kBookOfStabbingMoveStab, MonsterIntent::ATTACK);
    book_of_stabbing_decide_move(b18, 0, 10, /*ascension=*/18);
    EXPECT_EQ(b18.monsters[0].pad0, 4);

    // path 3: num >= 15 && lastTwoMoves(STAB) -> BIG_STAB, ++ only at A18
    CombatState c = MakeSeeded(83, 1);
    c.monsters[0].pad0 = 3;
    telegraph(c, 0, r::kBookOfStabbingMoveStab, MonsterIntent::ATTACK);
    telegraph(c, 0, r::kBookOfStabbingMoveStab, MonsterIntent::ATTACK);
    book_of_stabbing_decide_move(c, 0, 15, /*ascension=*/17);
    EXPECT_EQ(c.monsters[0].move_history[0], r::kBookOfStabbingMoveBigStab);
    EXPECT_EQ(c.monsters[0].pad0, 3);
    CombatState c18 = MakeSeeded(83, 1);
    c18.monsters[0].pad0 = 3;
    telegraph(c18, 0, r::kBookOfStabbingMoveStab, MonsterIntent::ATTACK);
    telegraph(c18, 0, r::kBookOfStabbingMoveStab, MonsterIntent::ATTACK);
    book_of_stabbing_decide_move(c18, 0, 15, /*ascension=*/18);
    EXPECT_EQ(c18.monsters[0].pad0, 4);

    // path 4: the fallthrough -> STAB, ALWAYS ++
    CombatState d = MakeSeeded(83, 1);
    d.monsters[0].pad0 = 3;
    book_of_stabbing_decide_move(d, 0, 50, /*ascension=*/1);
    EXPECT_EQ(d.monsters[0].move_history[0], r::kBookOfStabbingMoveStab);
    EXPECT_EQ(d.monsters[0].pad0, 4);
}

TEST(CityElites, BookOfStabbingStabCountSaturatesRatherThanWrapping) {
    CombatState s = MakeSeeded(89, 1);
    s.monsters[0].pad0 = 255;
    book_of_stabbing_decide_move(s, 0, 50, kA20);
    EXPECT_EQ(s.monsters[0].pad0, 255)
        << "a wrap would be a 255-hit turn followed by a 0-hit one";
}

TEST(CityElites, BookOfStabbingStabQueuesOneDamagePerCountedStab) {
    CombatState s = MakeSeeded(97, /*monsters=*/0);
    const MonsterId solo[] = {MonsterId::BOOK_OF_STABBING};
    spawn_group(s, solo);
    s.monsters[0].pad0 = 4;
    telegraph(s, 0, r::kBookOfStabbingMoveStab, MonsterIntent::ATTACK);
    gremlin_leader_num_alive_gremlins(s, 0);  // no-op read; keeps the include used
    book_of_stabbing_take_turn(s, 0);
    ASSERT_EQ(s.action_count, 5);
    for (uint8_t i = 0; i < 4; ++i) {
        EXPECT_EQ(queued(s, i).opcode, static_cast<uint16_t>(Opcode::DAMAGE));
        EXPECT_EQ(queued(s, i).amount, 7) << "stabDmg at A20";
        EXPECT_EQ(queued(s, i).tgt, kActorPlayer);
    }
    EXPECT_EQ(queued(s, 4).opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE))
        << "the trailing RollMoveAction is BEHIND every damage, so the count "
           "the turn used is the count getMove committed";

    CombatState big = MakeSeeded(97, /*monsters=*/0);
    spawn_group(big, solo);
    big.monsters[0].pad0 = 4;
    telegraph(big, 0, r::kBookOfStabbingMoveBigStab, MonsterIntent::ATTACK);
    book_of_stabbing_take_turn(big, 0);
    ASSERT_EQ(big.action_count, 2);
    EXPECT_EQ(queued(big, 0).amount, 24) << "bigStabDmg at A20 -- ONE hit";
}

TEST(CityElites, PainfulStabsMakesOneWoundPerNonThornsHitOnThePlayer) {
    CombatState s = MakeSeeded(101, /*monsters=*/0);
    const MonsterId solo[] = {MonsterId::BOOK_OF_STABBING};
    spawn_group(s, solo);
    use_pre_battle_actions(s);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::PAINFUL_STABS), -1)
        << "PainfulStabsPower assigns amount = -1 explicitly";

    // A three-hit STAB puts THREE Wounds in the discard: the hook is per HIT.
    s.monsters[0].pad0 = 3;
    telegraph(s, 0, r::kBookOfStabbingMoveStab, MonsterIntent::ATTACK);
    book_of_stabbing_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(discard_count_of(s, CardId::WOUND), 3);

    // THORNS damage from the same owner makes none (PainfulStabsPower.java:41).
    const int before = s.discard_count;
    ActionQueueItem thorns{};
    thorns.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    thorns.src = 0;
    thorns.tgt = kActorPlayer;
    thorns.amount = 5;
    thorns.flags = make_damage_flags(DamageType::THORNS);
    execute_opcode(s, thorns);
    drain(s);
    EXPECT_EQ(s.discard_count, before) << "info.type != THORNS is the guard";

    // A fully-blocked hit makes none either -- that half of the guard is the
    // dispatcher's (`damageAmount > 0`).
    s.player_block = 100;
    ActionQueueItem blocked{};
    blocked.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    blocked.src = 0;
    blocked.tgt = kActorPlayer;
    blocked.amount = 5;
    blocked.flags = make_damage_flags(DamageType::NORMAL);
    execute_opcode(s, blocked);
    drain(s);
    EXPECT_EQ(s.discard_count, before);
}

TEST(CityElites, PainfulStabsOnlyFiresForItsOwnOwnersHits) {
    // ON_INFLICT_DAMAGE walks the ATTACKER's power list, so a second monster's
    // identical hit makes no Wound.
    CombatState s = MakeSeeded(103, /*monsters=*/2);
    s.monsters[0].monster_id =
        static_cast<uint16_t>(MonsterId::BOOK_OF_STABBING);
    s.monsters[0].hp = 100;
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[1].hp = 40;
    give_monster_power(s, 0, PowerId::PAINFUL_STABS, -1);

    ActionQueueItem hit{};
    hit.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    hit.tgt = kActorPlayer;
    hit.amount = 5;
    hit.flags = make_damage_flags(DamageType::NORMAL);
    hit.src = 1;  // the Jaw Worm swings
    execute_opcode(s, hit);
    drain(s);
    EXPECT_EQ(discard_count_of(s, CardId::WOUND), 0);
    hit.src = 0;  // the Book swings
    execute_opcode(s, hit);
    drain(s);
    EXPECT_EQ(discard_count_of(s, CardId::WOUND), 1);
}

// ============================================================================
// 10. Taskmaster's turn
// ============================================================================

TEST(CityElites, TaskmasterTurnIsDamageThenWoundsThenTheA18Strength) {
    CombatState s = MakeSeeded(107, /*monsters=*/0);
    const MonsterId solo[] = {MonsterId::TASKMASTER};
    spawn_group(s, solo);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kTaskmasterMoveScouringWhip);

    const int32_t ai_before = s.ai_rng.counter;
    taskmaster_take_turn(s, 0);
    EXPECT_EQ(s.ai_rng.counter, ai_before)
        << "playSfx' MathUtils.random(1) is UNSEEDED";
    ASSERT_EQ(s.action_count, 4);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::DAMAGE));
    EXPECT_EQ(queued(s, 0).amount, 7);
    EXPECT_EQ(queued(s, 1).opcode, static_cast<uint16_t>(Opcode::MAKE_CARD));
    EXPECT_EQ(queued(s, 1).amount, 3) << "woundCount at A20";
    EXPECT_EQ(queued(s, 1).src, static_cast<uint8_t>(CardPile::DISCARD));
    EXPECT_EQ(queued(s, 2).opcode, static_cast<uint16_t>(Opcode::APPLY_POWER));
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 2).flags), PowerId::STRENGTH);
    EXPECT_EQ(queued(s, 2).amount, 1);
    EXPECT_EQ(queued(s, 2).tgt, 0) << "ApplyPowerAction(this, this, ...)";
    EXPECT_EQ(queued(s, 3).opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE));

    drain(s);
    EXPECT_EQ(discard_count_of(s, CardId::WOUND), 3);
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), 1);
    // The trailing roll spends a draw and re-telegraphs the same forced move.
    EXPECT_EQ(s.ai_rng.counter - ai_before, 1);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kTaskmasterMoveScouringWhip);
}

// ============================================================================
// 11. Encounter compositions -- spawn-order-exact
// ============================================================================

std::vector<std::string_view> members_of(std::string_view key, int64_t seed) {
    RngStream misc = from_seed(seed);
    ResolvedGroup g{};
    EXPECT_TRUE(resolve_encounter(key, misc, g)) << key;
    return std::vector<std::string_view>(g.members.begin(),
                                         g.members.begin() + g.count);
}

TEST(CityElites, SlaversSpawnsBlueThenTaskmasterThenRed) {
    using V = std::vector<std::string_view>;
    EXPECT_EQ(members_of("Slavers", 1),
              (V{"SlaverBlue", "SlaverBoss", "SlaverRed"}))
        << "MonsterHelper.java:510-512, and Taskmaster.ID is \"SlaverBoss\"";
    EXPECT_EQ(members_of("Book of Stabbing", 1), (V{"BookOfStabbing"}))
        << "MonsterHelper.java:504-506 -- solo";
    EXPECT_EQ(members_of("Colosseum Nobs", 1), (V{"SlaverBoss", "GremlinNob"}))
        << "MonsterHelper.java:516-518 -- Taskmaster FIRST; S2.32's Colosseum "
           "becomes spawnable with this batch's row";
}

TEST(CityElites, GremlinLeaderEncounterDrawsTwoMiscRngGremlinsThenTheLeader) {
    // The ENCOUNTER pool is misc_rng and is drawn WITH replacement, two 1-count
    // POOL steps (encounters.yaml id 35 / MonsterHelper.java:767-778). The
    // leader is always third, which is what makes its usePreBattleAction's
    // hard-coded indices 0 and 1 correct.
    for (int64_t seed = 1; seed <= 40; ++seed) {
        RngStream misc = from_seed(seed);
        ResolvedGroup g{};
        ASSERT_TRUE(resolve_encounter("Gremlin Leader", misc, g));
        ASSERT_EQ(g.count, 3);
        EXPECT_EQ(g.members[2], "GremlinLeader") << "seed " << seed;
        EXPECT_EQ(misc.counter, 2) << "one draw per minion, none for the leader";
    }
}

TEST(CityElites, EveryEliteRowIsRegisteredAndSpawnable) {
    // The three rows are what un-park the encounters; nothing in run_advance
    // changed. Also the negative: none of the three is mid-combat spawnable.
    for (MonsterId id : {MonsterId::GREMLIN_LEADER, MonsterId::TASKMASTER,
                         MonsterId::BOOK_OF_STABBING}) {
        EXPECT_NE(r::monster_def(id), nullptr);
        EXPECT_TRUE(monster_init_fn(id) != nullptr);
        EXPECT_TRUE(monster_turn_fn(id) != &default_monster_turn);
        EXPECT_TRUE(monster_roll_move_fn(id) != nullptr);
        EXPECT_TRUE(monster_spawn_at_hp_fn(id) == nullptr)
            << "an elite is never summoned";
    }
    // ...while all five gremlins now ARE.
    for (MonsterId id : {MonsterId::GREMLIN_WARRIOR, MonsterId::GREMLIN_THIEF,
                         MonsterId::GREMLIN_FAT, MonsterId::GREMLIN_TSUNDERE,
                         MonsterId::GREMLIN_WIZARD}) {
        EXPECT_TRUE(monster_spawn_at_hp_fn(id) != nullptr)
            << "every member of the summon pool needs one";
    }
    EXPECT_EQ(static_cast<int>(MonsterId::GREMLIN_LEADER), 37);
    EXPECT_EQ(static_cast<int>(MonsterId::TASKMASTER), 38);
    EXPECT_EQ(static_cast<int>(MonsterId::BOOK_OF_STABBING), 39);
    EXPECT_EQ(static_cast<int>(PowerId::MINION), 96);
    EXPECT_EQ(static_cast<int>(PowerId::PAINFUL_STABS), 97);
}

TEST(CityElites, ASummonedGremlinKeepsTheGremlinGangBehaviour) {
    // A summoned gremlin is the SAME body the Act-1 encounter runs: same opening
    // move, same discarded init draw, same charge counter.
    struct Row { MonsterId id; uint8_t move; };
    const Row rows[] = {
        {MonsterId::GREMLIN_WARRIOR, r::kGremlinWarriorMoveScratch},
        {MonsterId::GREMLIN_THIEF, r::kGremlinThiefMovePuncture},
        {MonsterId::GREMLIN_FAT, r::kGremlinFatMoveBlunt},
        {MonsterId::GREMLIN_TSUNDERE, r::kGremlinTsundereMoveProtect},
        {MonsterId::GREMLIN_WIZARD, r::kGremlinWizardMoveCharge},
    };
    for (const Row& row : rows) {
        CombatState s = MakeSeeded(109, /*monsters=*/0);
        s.monster_count = 0;
        ActionQueueItem spawn{};
        spawn.opcode = static_cast<uint16_t>(Opcode::SPAWN_MONSTER);
        spawn.tgt = 0;
        spawn.amount = 25;
        spawn.flags = make_spawn_monster_flags(static_cast<uint16_t>(row.id),
                                               kGremlinLeaderSlotX[1]);
        execute_opcode(s, spawn);
        ASSERT_EQ(s.monster_count, 1);
        EXPECT_EQ(s.monsters[0].hp, 25);
        EXPECT_EQ(s.monsters[0].max_hp, 25)
            << "the summoner's draw, not a second setHp";
        EXPECT_EQ(s.monster_hp_rng.counter, 0)
            << "a spawn-at-hp init NEVER draws monster_hp_rng";
        EXPECT_EQ(s.ai_rng.counter, 1) << "one discarded init rollMove";
        EXPECT_EQ(s.monsters[0].move_history[0], row.move);
        EXPECT_EQ(s.monsters[0].draw_x, kGremlinLeaderSlotX[1]);
    }
    // The Wizard's charge counter starts at 1, exactly as its encounter init.
    CombatState w = MakeSeeded(109, /*monsters=*/0);
    w.monster_count = 0;
    ActionQueueItem spawn{};
    spawn.opcode = static_cast<uint16_t>(Opcode::SPAWN_MONSTER);
    spawn.tgt = 0;
    spawn.amount = 25;
    spawn.flags = make_spawn_monster_flags(
        static_cast<uint16_t>(MonsterId::GREMLIN_WIZARD));
    execute_opcode(w, spawn);
    EXPECT_EQ(w.monsters[0].pad0, 1);
}

}  // namespace
}  // namespace sts::engine
