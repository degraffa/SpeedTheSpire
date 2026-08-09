// Act-3 "Beyond" ELITES -- the Giant Head (a countdown plus the Slow tax), the
// Nemesis (the monster-side Intangible and its two cap sites, Burns into the
// discard) and the Reptomancer with its SnakeDaggers (a double constructor
// draw, queue-time spawn planning over a derived slot map, and the post-super
// suicide sweep).
//
// WHAT THIS FILE PINS, and why each part is here rather than assumed:
//
//   * STAT/MOVE TABLES, per monster, at EVERY ascension branch the Java has --
//     not just the A20 column the engine runs. All four classes gate HP or
//     damage on `>= 3`, `>= 8` or `>= 18` while NAMING several of the constants
//     A_2_*, so every boundary is asserted on both sides. The SnakeDagger's row
//     exists to pin a total ABSENCE of branching.
//   * THE GIANT HEAD'S COUNTDOWN, including the fact that the A18 pre-battle
//     decrement lands AFTER the opening rollMove, not before it -- the one fact
//     in the batch that two plausible orderings both make look legal.
//   * THE IT_IS_TIME RAMP AND ITS TWO CLAMPS: the `count > -6` floor in getMove
//     and the `index > 7` cap in takeTurn, driven far enough to reach both.
//   * SLOW as a live ZERO-amount slot: applied at 0, stacked by every card
//     played, multiplying incoming damage by 1 + 0.1*stacks through the float
//     pipeline, and RESET (not removed) at end of round.
//   * THE NEMESIS'S INTANGIBLE CYCLE end to end -- the justApplied skip, the
//     removal two rounds later, the re-arm -- plus BOTH cap sites, which differ:
//     the power caps NORMAL damage and the monster's own damage() override also
//     caps THORNS and HP_LOSS.
//   * getMove DRAW COUNTS. The Nemesis spends one or two ai_rng draws depending
//     on the arm, including the arm whose randomBoolean is spent even though the
//     cooldown then refuses the Scythe.
//   * THE REPTOMANCER'S DOUBLE monster_hp_rng DRAW, visible only as the stream
//     offset that moves both daggers' HP.
//   * THE SPAWN TURN'S EXACT STREAM ORDER AND SLOT MAP: both daggers' HP rolls
//     at QUEUE time, both children's init rolls at resolve, the Reptomancer's
//     own roll behind them, the four-slot cap, slot recycling, and the Minion
//     applications landing at the queue TOP rather than the bottom.
//   * THE SUICIDE SWEEP: post-super, self-excluding, triggerRelics TRUE, reverse
//     list order.
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
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_giant_head.hpp"
#include "sts/engine/monster_nemesis.hpp"
#include "sts/engine/monster_reptomancer.hpp"
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

// --- shared helpers (the city_elites_test shapes) ----------------------------

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

CombatState SoloGroup(int64_t seed, MonsterId id) {
    const MonsterId group[] = {id};
    CombatState s = MakeSeeded(seed, /*monsters=*/0);
    spawn_group(s, group);
    use_pre_battle_actions(s);
    drain(s);
    return s;
}

// The Reptomancer group as MonsterHelper builds it: dagger, Reptomancer, dagger
// (MonsterHelper.java:536-539).
CombatState ReptomancerGroup(int64_t seed) {
    const MonsterId group[] = {MonsterId::SNAKE_DAGGER, MonsterId::REPTOMANCER,
                               MonsterId::SNAKE_DAGGER};
    CombatState s = MakeSeeded(seed, /*monsters=*/0);
    spawn_group(s, group);
    use_pre_battle_actions(s);
    drain(s);
    return s;
}

constexpr uint8_t kRepto = 1;  // the Reptomancer is always constructed second

std::vector<std::string_view> members_of(std::string_view key, int64_t seed) {
    RngStream misc = from_seed(seed);
    ResolvedGroup g{};
    EXPECT_TRUE(resolve_encounter(key, misc, g)) << key;
    return std::vector<std::string_view>(g.members.begin(),
                                         g.members.begin() + g.count);
}

// ============================================================================
// 1. Stat and move tables -- every ascension branch, per monster
// ============================================================================

// GiantHead.java:38-77. TWO tier boundaries and they are on DIFFERENT numbers:
// HP on `>= 8`, startingDeathDmg on `>= 3` (whose constant is named
// A_2_DEATH_DMG). COUNT_DMG and GLARE_WEAK never branch at all.
TEST(BeyondElites, GiantHeadStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kGiantHead;
    // setHp(500,500) / setHp(520,520) -- DEGENERATE ranges at both tiers, spelled
    // {min: N, max: N} because the observable is the DRAW, not the number.
    EXPECT_EQ(d.hp_min(0), 500);
    EXPECT_EQ(d.hp_max(0), 500);
    EXPECT_EQ(d.hp_min(3), 500) << "the HP branch is >= 8; A3 moves the DAMAGE";
    EXPECT_EQ(d.hp_min(7), 500) << "boundary: still the base column at 7";
    EXPECT_EQ(d.hp_min(8), 520);
    EXPECT_EQ(d.hp_max(8), 520);
    EXPECT_EQ(d.hp_min(kA20), 520);

    // startingDeathDmg 30 / 40 at >= 3 (:69), carried as IT_IS_TIME's single
    // authored step -- damage.get(1), i.e. the ramp at count == 0.
    const uint8_t time = r::kGiantHeadMoveItIsTime;
    EXPECT_EQ(step_count(d, time), 1);
    EXPECT_EQ(step_amount(d, time, 0, 0), 30);
    EXPECT_EQ(step_amount(d, time, 0, 2), 30) << "the branch is >= 3, not >= 2";
    EXPECT_EQ(step_amount(d, time, 0, 3), 40);
    EXPECT_EQ(step_amount(d, time, 0, 17), 40);
    EXPECT_EQ(step_amount(d, time, 0, 18), 40) << "A18 moves the COUNT, not this";
    EXPECT_EQ(step_amount(d, time, 0, kA20), 40);

    // COUNT_DMG 13 and GLARE_WEAK 1 are field constants (:44,46) and never
    // branch, so both rows carry a flat literal and no tier column.
    const uint8_t cnt = r::kGiantHeadMoveCount;
    const uint8_t glare = r::kGiantHeadMoveGlare;
    for (int32_t asc : {0, 2, 3, 8, 17, 18, kA20}) {
        EXPECT_EQ(step_amount(d, cnt, 0, asc), 13) << "asc " << asc;
        EXPECT_EQ(step_amount(d, glare, 0, asc), 1) << "asc " << asc;
    }
    EXPECT_EQ(d.move(glare)->intent, r::MonsterIntent::DEBUFF);
    EXPECT_EQ(d.move(cnt)->intent, r::MonsterIntent::ATTACK);
    EXPECT_EQ(d.move(time)->intent, r::MonsterIntent::ATTACK);
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::ELITE);
    EXPECT_EQ(d.roll_count, 0) << "the super argument is the LITERAL 500";
}

// Nemesis.java:45-85. HP on `>= 8`, fireDmg on `>= 3` (A_2_FIRE_DMG), Burn count
// on `>= 18`. SCYTHE_DMG is flat.
TEST(BeyondElites, NemesisStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kNemesis;
    EXPECT_EQ(d.hp_min(0), 185);
    EXPECT_EQ(d.hp_max(0), 185);
    EXPECT_EQ(d.hp_min(3), 185) << "the HP branch is >= 8";
    EXPECT_EQ(d.hp_min(7), 185) << "boundary";
    EXPECT_EQ(d.hp_min(8), 200);
    EXPECT_EQ(d.hp_max(8), 200);
    EXPECT_EQ(d.hp_min(kA20), 200);

    // TRI_ATTACK: THREE separate DamageActions on fireDmg (6, 7 from A3).
    const uint8_t tri = r::kNemesisMoveTriAttack;
    EXPECT_EQ(step_count(d, tri), 3)
        << "a `for (i < 3)` of separate DamageActions (:99-101), not one tripled "
           "hit -- so block and per-hit powers apply per hit";
    for (uint8_t k = 0; k < 3; ++k) {
        EXPECT_EQ(step_amount(d, tri, k, 0), 6);
        EXPECT_EQ(step_amount(d, tri, k, 2), 6) << "the branch is >= 3";
        EXPECT_EQ(step_amount(d, tri, k, 3), 7);
        EXPECT_EQ(step_amount(d, tri, k, kA20), 7);
    }
    // SCYTHE 45, flat.
    const uint8_t scythe = r::kNemesisMoveScythe;
    EXPECT_EQ(step_count(d, scythe), 1);
    for (int32_t asc : {0, 3, 8, 17, 18, kA20}) {
        EXPECT_EQ(step_amount(d, scythe, 0, asc), 45) << "asc " << asc;
    }
    // TRI_BURN: ONE MakeTempCardInDiscardAction whose amount is the count.
    const uint8_t burn = r::kNemesisMoveTriBurn;
    EXPECT_EQ(step_count(d, burn), 1) << "one action WITH a count, not N actions";
    EXPECT_EQ(step_amount(d, burn, 0, 0), 3);
    EXPECT_EQ(step_amount(d, burn, 0, 17), 3);
    EXPECT_EQ(step_amount(d, burn, 0, 18), 5);
    EXPECT_EQ(step_amount(d, burn, 0, kA20), 5);
    EXPECT_EQ(d.move(burn)->intent, r::MonsterIntent::DEBUFF)
        << "Intent.DEBUFF even though the payload is cards";
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::ELITE);
    EXPECT_EQ(d.roll_count, 0) << "the super argument is the LITERAL 185";
    // There is NO move 1 in this class -- the ids are 2, 3, 4.
    EXPECT_EQ(d.move(1), nullptr);
    EXPECT_EQ(d.move_count, 3);
}

// Reptomancer.java:44-79. HP on `>= 8`, BOTH damage numbers on `>= 3` in one
// block, daggersPerSpawn on `>= 18`.
TEST(BeyondElites, ReptomancerStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kReptomancer;
    EXPECT_EQ(d.hp_min(0), 180);
    EXPECT_EQ(d.hp_max(0), 190);
    EXPECT_EQ(d.hp_min(7), 180) << "boundary: the branch is >= 8";
    EXPECT_EQ(d.hp_min(8), 190);
    EXPECT_EQ(d.hp_max(8), 200);
    EXPECT_EQ(d.hp_min(kA20), 190);

    // The SUPER-ARGUMENT roll: an unbranched literal (180, 190) at EVERY
    // ascension, unlike the setHp above it.
    ASSERT_EQ(d.roll_count, 1);
    const r::MonsterRollDef* sa = d.roll(r::kReptomancerRollSuperArgHp);
    ASSERT_NE(sa, nullptr);
    EXPECT_EQ(sa->stream, r::MonsterRollStream::MONSTER_HP);
    EXPECT_EQ(sa->timing, r::MonsterRollTiming::CONSTRUCTOR_BEFORE_HP);
    for (int32_t asc : {0, 3, 7, 8, 18, kA20}) {
        EXPECT_EQ(sa->min(asc), 180) << "asc " << asc;
        EXPECT_EQ(sa->max(asc), 190) << "asc " << asc;
    }

    // SNAKE_STRIKE: two damage steps THEN the Weak, in that queue order.
    const uint8_t strike = r::kReptomancerMoveSnakeStrike;
    EXPECT_EQ(step_count(d, strike), 3);
    for (uint8_t k = 0; k < 2; ++k) {
        EXPECT_EQ(step_amount(d, strike, k, 0), 13);
        EXPECT_EQ(step_amount(d, strike, k, 2), 13) << "the branch is >= 3";
        EXPECT_EQ(step_amount(d, strike, k, 3), 16);
        EXPECT_EQ(step_amount(d, strike, k, kA20), 16);
    }
    EXPECT_EQ(d.move(strike)->effects[2].op, r::Opcode::APPLY_POWER);
    for (int32_t asc : {0, 3, 18, kA20}) {
        EXPECT_EQ(step_amount(d, strike, 2, asc), 1) << "WeakPower(player, 1)";
    }
    EXPECT_EQ(d.move(strike)->intent, r::MonsterIntent::ATTACK_DEBUFF);

    // BIG_BITE 30 / 34 from A3.
    const uint8_t bite = r::kReptomancerMoveBigBite;
    EXPECT_EQ(step_amount(d, bite, 0, 0), 30);
    EXPECT_EQ(step_amount(d, bite, 0, 2), 30);
    EXPECT_EQ(step_amount(d, bite, 0, 3), 34);
    EXPECT_EQ(step_amount(d, bite, 0, kA20), 34);

    // SPAWN_DAGGER is a NOP placeholder telegraphed as UNKNOWN.
    const uint8_t spawn = r::kReptomancerMoveSpawnDagger;
    EXPECT_EQ(step_count(d, spawn), 1);
    EXPECT_EQ(d.move(spawn)->effects[0].op, r::Opcode::NOP);
    EXPECT_EQ(d.move(spawn)->intent, r::MonsterIntent::UNKNOWN);
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::ELITE);
}

// SnakeDagger.java:37-50. The row that pins an ABSENCE: no ascension branch of
// any kind, and NO `rolls` entry, because its super-argument draw IS the HP.
TEST(BeyondElites, SnakeDaggerStatTableHasNoAscensionBranchAtAll) {
    const auto& d = r::kSnakeDagger;
    for (int32_t asc : {0, 2, 3, 7, 8, 17, 18, 19, kA20}) {
        EXPECT_EQ(d.hp_min(asc), 20) << "asc " << asc;
        EXPECT_EQ(d.hp_max(asc), 25) << "asc " << asc;
    }
    EXPECT_EQ(d.roll_count, 0)
        << "the super argument monsterHpRng.random(20,25) is the ONLY draw and "
           "there is no setHp under it -- the `hp` column IS that draw, unlike "
           "the Taskmaster's and the Reptomancer's overwritten ones";
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::NORMAL)
        << "SnakeDagger assigns no this.type even inside an elite group";

    const uint8_t wound = r::kSnakeDaggerMoveWound;
    EXPECT_EQ(step_count(d, wound), 2);
    EXPECT_EQ(step_amount(d, wound, 0, kA20), 9);
    EXPECT_EQ(d.move(wound)->effects[1].op, r::Opcode::MAKE_CARD);
    EXPECT_EQ(step_amount(d, wound, 1, kA20), 1);
    EXPECT_EQ(d.move(wound)->intent, r::MonsterIntent::ATTACK_DEBUFF);

    const uint8_t explode = r::kSnakeDaggerMoveExplode;
    EXPECT_EQ(step_count(d, explode), 2);
    EXPECT_EQ(step_amount(d, explode, 0, kA20), 25);
    EXPECT_EQ(d.move(explode)->effects[1].op, r::Opcode::LOSE_HP)
        << "the self-kill is an HP_LOSS through the damage path, not a "
           "SuicideAction";
    EXPECT_EQ(step_amount(d, explode, 1, kA20), 0)
        << "a per-instance placeholder: the module substitutes the live HP";
    EXPECT_EQ(d.move(explode)->intent, r::MonsterIntent::ATTACK);
}

TEST(BeyondElites, EveryRowIsRegisteredAndOnlyTheDaggerIsSpawnable) {
    for (MonsterId id : {MonsterId::GIANT_HEAD, MonsterId::NEMESIS,
                         MonsterId::REPTOMANCER, MonsterId::SNAKE_DAGGER}) {
        EXPECT_NE(r::monster_def(id), nullptr);
        EXPECT_TRUE(monster_init_fn(id) != nullptr);
        EXPECT_TRUE(monster_turn_fn(id) != &default_monster_turn);
        EXPECT_TRUE(monster_roll_move_fn(id) != nullptr)
            << "all four end takeTurn in a RollMoveAction outside the switch";
    }
    EXPECT_TRUE(monster_spawn_at_hp_fn(MonsterId::SNAKE_DAGGER) != nullptr);
    for (MonsterId id : {MonsterId::GIANT_HEAD, MonsterId::NEMESIS,
                         MonsterId::REPTOMANCER}) {
        EXPECT_TRUE(monster_spawn_at_hp_fn(id) == nullptr)
            << "an elite is never summoned";
    }
    // The die() split: only the Reptomancer has content, and it is POST-super.
    EXPECT_EQ(monster_die_fn(MonsterId::REPTOMANCER), nullptr);
    EXPECT_NE(monster_die_after_fn(MonsterId::REPTOMANCER), nullptr);
    for (MonsterId id : {MonsterId::GIANT_HEAD, MonsterId::NEMESIS,
                         MonsterId::SNAKE_DAGGER}) {
        EXPECT_EQ(monster_die_fn(id), nullptr);
        EXPECT_EQ(monster_die_after_fn(id), nullptr);
    }
    // The pre-battle split.
    EXPECT_NE(monster_pre_battle_fn(MonsterId::GIANT_HEAD), nullptr);
    EXPECT_NE(monster_pre_battle_fn(MonsterId::REPTOMANCER), nullptr);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::NEMESIS), nullptr);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::SNAKE_DAGGER), nullptr);
}

// ============================================================================
// 2. The Giant Head: the countdown, the pre-battle ordering, the ramp
// ============================================================================

TEST(BeyondElites, GiantHeadPreBattleDecrementLandsAfterTheOpeningRoll) {
    // THE ORDERING FACT. count = 5 at construction; init()'s rollMove takes the
    // ordinary arm and decrements to 4; usePreBattleAction then decrements again
    // (A18+) to 3. Both orders produce a legal-looking telegraph, which is why
    // this is asserted at each step rather than only at the end.
    CombatState s = MakeSeeded(11, /*monsters=*/0);
    const MonsterId group[] = {MonsterId::GIANT_HEAD};
    spawn_group(s, group);
    EXPECT_EQ(giant_head_count(s.monsters[0]), 4)
        << "after ctor+init only: 5 - 1 for the opening getMove";
    use_pre_battle_actions(s);
    EXPECT_EQ(giant_head_count(s.monsters[0]), 3)
        << "GiantHead.java:83-85 -- the A18 arm spends a countdown turn, and it "
           "spends it AFTER the first roll";
    // ...and the Slow it queued is a live slot at amount ZERO.
    drain(s);
    EXPECT_TRUE(monster_has(s, 0, PowerId::SLOW));
    EXPECT_EQ(monster_power(s, 0, PowerId::SLOW), 0)
        << "new SlowPower(this, 0) -- the zero is the Java's, not a default";
}

TEST(BeyondElites, GiantHeadCountdownReachesItIsTimeAndThenRampsToTheCap) {
    CombatState s = SoloGroup(11, MonsterId::GIANT_HEAD);
    ASSERT_EQ(giant_head_count(s.monsters[0]), 3);
    // Three more decisions take count 3 -> 2 -> 1, then the `<= 1` arm engages.
    // Drive the decisions directly so the walk is independent of the seed.
    giant_head_decide_move(s, 0, 10, kA20);
    EXPECT_EQ(giant_head_count(s.monsters[0]), 2);
    giant_head_decide_move(s, 0, 90, kA20);
    EXPECT_EQ(giant_head_count(s.monsters[0]), 1);
    // count == 1 -> the `<= 1` arm: decrement to 0 and force IT_IS_TIME.
    giant_head_decide_move(s, 0, 0, kA20);
    EXPECT_EQ(giant_head_count(s.monsters[0]), 0);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kGiantHeadMoveItIsTime);
    EXPECT_EQ(giant_head_it_is_time_damage(0, kA20), 40)
        << "the first It Is Time is startingDeathDmg exactly";

    // The ramp: 40, 45, 50 ... 70, and then it STOPS -- both because count
    // floors at -6 and because the damage index caps at 7.
    const int32_t expected[] = {45, 50, 55, 60, 65, 70};
    for (int32_t want : expected) {
        giant_head_decide_move(s, 0, 50, kA20);
        EXPECT_EQ(giant_head_it_is_time_damage(
                      giant_head_count(s.monsters[0]), kA20),
                  want);
    }
    EXPECT_EQ(giant_head_count(s.monsters[0]), -6);
    // Five more decisions: the floor holds and so does the damage.
    for (int i = 0; i < 5; ++i) {
        giant_head_decide_move(s, 0, 99, kA20);
        EXPECT_EQ(giant_head_count(s.monsters[0]), -6)
            << "`if (count > -6) --count;` (GiantHead.java:156-158)";
        EXPECT_EQ(giant_head_it_is_time_damage(
                      giant_head_count(s.monsters[0]), kA20),
                  70)
            << "damage.get(7) == startingDeathDmg + 30, the index clamp";
    }
}

TEST(BeyondElites, GiantHeadItIsTimeQueuesTheRampedAmountAndStillRolls) {
    CombatState s = SoloGroup(11, MonsterId::GIANT_HEAD);
    giant_head_set_count(s.monsters[0], -2);
    telegraph(s, 0, r::kGiantHeadMoveItIsTime, MonsterIntent::ATTACK);
    giant_head_take_turn(s, 0);
    ASSERT_EQ(s.action_count, 2);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::DAMAGE));
    EXPECT_EQ(queued(s, 0).amount, 50) << "40 - (-2)*5";
    EXPECT_EQ(queued(s, 1).opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE))
        << "RollMoveAction sits OUTSIDE the switch (GiantHead.java:113), so "
           "IT_IS_TIME keeps the ramp climbing";
}

TEST(BeyondElites, GiantHeadOrdinaryArmObeysTheLastTwoMovesGuards) {
    CombatState s = SoloGroup(11, MonsterId::GIANT_HEAD);
    // Two Glares in a row, then num < 50 again: the guard flips it to Count.
    giant_head_set_count(s.monsters[0], 5);
    set_monster_move(s.monsters[0], r::kGiantHeadMoveGlare, MonsterIntent::DEBUFF);
    set_monster_move(s.monsters[0], r::kGiantHeadMoveGlare, MonsterIntent::DEBUFF);
    giant_head_decide_move(s, 0, 49, kA20);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kGiantHeadMoveCount);
    // The mirror: two Counts and num >= 50 gives Glare.
    giant_head_set_count(s.monsters[0], 5);
    set_monster_move(s.monsters[0], r::kGiantHeadMoveCount, MonsterIntent::ATTACK);
    set_monster_move(s.monsters[0], r::kGiantHeadMoveCount, MonsterIntent::ATTACK);
    giant_head_decide_move(s, 0, 50, kA20);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kGiantHeadMoveGlare);
    // A single Glare is not two: num < 50 still gives Glare.
    giant_head_set_count(s.monsters[0], 5);
    s.monsters[0].move_history[0] = r::kGiantHeadMoveGlare;
    s.monsters[0].move_history[1] = r::kGiantHeadMoveCount;
    giant_head_decide_move(s, 0, 0, kA20);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kGiantHeadMoveGlare);
}

// ============================================================================
// 3. Slow (powers.yaml 106)
// ============================================================================

TEST(BeyondElites, SlowStacksOnEveryCardPlayAndResetsRatherThanExpiring) {
    CombatState s = SoloGroup(11, MonsterId::GIANT_HEAD);
    ASSERT_EQ(monster_power(s, 0, PowerId::SLOW), 0);

    for (int i = 1; i <= 3; ++i) {
        dispatch_on_after_use_card(s, 0, static_cast<uint16_t>(CardId::STRIKE));
        drain(s);
        EXPECT_EQ(monster_power(s, 0, PowerId::SLOW), i)
            << "one stack per card, no card-type test (SlowPower.java:51-54)";
    }
    // atEndOfRound: `amount = 0`, and the SLOT SURVIVES.
    dispatch_at_end_of_round(s);
    drain(s);
    EXPECT_TRUE(monster_has(s, 0, PowerId::SLOW))
        << "a RESET, not a removal -- REDUCE_POWER to zero would delete the slot";
    EXPECT_EQ(monster_power(s, 0, PowerId::SLOW), 0);
    // ...and the counter restarts from zero next turn.
    dispatch_on_after_use_card(s, 0, static_cast<uint16_t>(CardId::DEFEND));
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::SLOW), 1);
}

TEST(BeyondElites, SlowMultipliesIncomingNormalDamageThroughTheFloatPipeline) {
    CombatState s = SoloGroup(11, MonsterId::GIANT_HEAD);
    const int16_t full = s.monsters[0].hp;
    // Amount 0 is the identity.
    player_attacks(s, 0, 13);
    EXPECT_EQ(s.monsters[0].hp, full - 13) << "1 + 0*0.1 == 1.0f";

    // Three stacks: floor(13 * 1.3f) == 16, NOT 13 + 3.
    for (int i = 0; i < 3; ++i) {
        dispatch_on_after_use_card(s, 0, static_cast<uint16_t>(CardId::STRIKE));
        drain(s);
    }
    ASSERT_EQ(monster_power(s, 0, PowerId::SLOW), 3);
    const int16_t before = s.monsters[0].hp;
    player_attacks(s, 0, 13);
    EXPECT_EQ(s.monsters[0].hp, before - 16)
        << "the single mathutils_floor at the end of compute_damage truncates "
           "once: floor(13 * 1.3f)";

    // THORNS skips DamageInfo.applyPowers entirely, so Slow does not scale it.
    const int16_t before_thorns = s.monsters[0].hp;
    player_attacks(s, 0, 13, DamageType::THORNS);
    EXPECT_EQ(s.monsters[0].hp, before_thorns - 13)
        << "the NORMAL guard (SlowPower.java:58) is satisfied at the call site";
}

TEST(BeyondElites, SlowIsADebuffAndCarriesTheDefaultPriority) {
    const r::PowerDef* d = r::power_def(PowerId::SLOW);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, r::PowerType::DEBUFF) << "SlowPower.java:34, explicit";
    EXPECT_EQ(d->priority, r::kDefaultPowerPriority)
        << "the ctor assigns no priority";
    EXPECT_EQ(r::power_game_id(PowerId::SLOW), "Slow");
}

// ============================================================================
// 4. The Nemesis: Intangible, the two cap sites, the Burns, the draw counts
// ============================================================================

TEST(BeyondElites, NemesisIntangibleCycleArmsSkipsDecaysAndRearms) {
    CombatState s = SoloGroup(7, MonsterId::NEMESIS);
    ASSERT_FALSE(monster_has(s, 0, PowerId::INTANGIBLE_MONSTER));

    // Turn N: any move re-arms it, because the guard is outside the switch.
    telegraph(s, 0, r::kNemesisMoveScythe, MonsterIntent::ATTACK);
    nemesis_take_turn(s, 0);
    drain(s);
    ASSERT_TRUE(monster_has(s, 0, PowerId::INTANGIBLE_MONSTER));
    EXPECT_EQ(monster_power(s, 0, PowerId::INTANGIBLE_MONSTER), 1);
    EXPECT_EQ(s.monsters[0].powers[0].counter, 1)
        << "justApplied, latched on op_apply_power's NEW-SLOT path";

    // End of round N: the latch is spent, the amount is untouched.
    dispatch_at_end_of_round(s);
    drain(s);
    EXPECT_TRUE(monster_has(s, 0, PowerId::INTANGIBLE_MONSTER));
    EXPECT_EQ(monster_power(s, 0, PowerId::INTANGIBLE_MONSTER), 1);
    EXPECT_EQ(s.monsters[0].powers[0].counter, 0);

    // Turn N+1: the monster still has it, so NOTHING is queued for it.
    telegraph(s, 0, r::kNemesisMoveTriAttack, MonsterIntent::ATTACK);
    nemesis_take_turn(s, 0);
    EXPECT_EQ(count_opcodes(s, Opcode::APPLY_POWER), 0)
        << "`if (!hasPower(\"Intangible\"))` (Nemesis.java:114) is a live read";
    drain(s);

    // End of round N+1: reduce 1 -> zero -> the slot is removed.
    dispatch_at_end_of_round(s);
    drain(s);
    EXPECT_FALSE(monster_has(s, 0, PowerId::INTANGIBLE_MONSTER))
        << "op_reduce_power removes at zero";

    // Turn N+2: re-armed.
    telegraph(s, 0, r::kNemesisMoveTriBurn, MonsterIntent::DEBUFF);
    nemesis_take_turn(s, 0);
    drain(s);
    EXPECT_TRUE(monster_has(s, 0, PowerId::INTANGIBLE_MONSTER));
}

TEST(BeyondElites, NemesisIntangibleCapsNormalThornsAndHpLossAlike) {
    CombatState s = SoloGroup(7, MonsterId::NEMESIS);
    telegraph(s, 0, r::kNemesisMoveScythe, MonsterIntent::ATTACK);
    nemesis_take_turn(s, 0);
    drain(s);
    ASSERT_TRUE(monster_has(s, 0, PowerId::INTANGIBLE_MONSTER));

    int16_t hp = s.monsters[0].hp;
    player_attacks(s, 0, 40);
    EXPECT_EQ(s.monsters[0].hp, hp - 1)
        << "atDamageFinalReceive caps NORMAL at 1 (IntangiblePower.java:43-46)";

    // THORNS and HP_LOSS skip DamageInfo.applyPowers, so the POWER cannot see
    // them -- the Nemesis's own damage() override is what caps these.
    hp = s.monsters[0].hp;
    player_attacks(s, 0, 40, DamageType::THORNS);
    EXPECT_EQ(s.monsters[0].hp, hp - 1)
        << "Nemesis.damage:122-124 has NO DamageType test";
    hp = s.monsters[0].hp;
    ActionQueueItem lose{};
    lose.opcode = static_cast<uint16_t>(Opcode::LOSE_HP);
    lose.src = kActorPlayer;
    lose.tgt = 0;
    lose.amount = 40;
    execute_opcode(s, lose);
    EXPECT_EQ(s.monsters[0].hp, hp - 1) << "HP_LOSS is capped by the same guard";
}

TEST(BeyondElites, TheMonsterIntangibleRowIsNotThePlayerOne) {
    // Two classes, two POWER_ID literals, two rows -- and the ids must not be
    // conflated, because id 29 decays at end of ROUND and this one at end of
    // TURN.
    EXPECT_NE(PowerId::INTANGIBLE, PowerId::INTANGIBLE_MONSTER);
    EXPECT_EQ(r::power_game_id(PowerId::INTANGIBLE), "IntangiblePlayer");
    EXPECT_EQ(r::power_game_id(PowerId::INTANGIBLE_MONSTER), "Intangible");
    EXPECT_EQ(r::power_def(PowerId::INTANGIBLE_MONSTER)->priority, 75);
    EXPECT_EQ(r::power_def(PowerId::INTANGIBLE_MONSTER)->type, r::PowerType::BUFF);

    // The player-side power is untouched by this batch: it has no justApplied
    // latch, so a fresh application starts at counter 0.
    CombatState s = MakeState(1);
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    it.src = kActorPlayer;
    it.tgt = kActorPlayer;
    it.amount = 1;
    it.flags = make_apply_power_flags(PowerId::INTANGIBLE);
    execute_opcode(s, it);
    ASSERT_EQ(s.player_power_count, 1);
    EXPECT_EQ(s.player_powers[0].counter, 0);
}

TEST(BeyondElites, NemesisTriBurnMakesFiveBurnsIntoTheDiscardAtA20) {
    CombatState s = SoloGroup(7, MonsterId::NEMESIS);
    const uint8_t before_draw = s.draw_count;
    telegraph(s, 0, r::kNemesisMoveTriBurn, MonsterIntent::DEBUFF);
    nemesis_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(discard_count_of(s, CardId::BURN), 5)
        << "MakeTempCardInDiscardAction(new Burn(), 5) at A18+ "
           "(Nemesis.java:108)";
    EXPECT_EQ(s.draw_count, before_draw)
        << "DISCARD, not draw -- there is no draw-pile variant in the class";
}

TEST(BeyondElites, NemesisScytheCooldownAgesOnEveryDecisionIncludingTheFirst) {
    CombatState s = SoloGroup(7, MonsterId::NEMESIS);
    // The opening getMove already ran (inside init) and already decremented.
    EXPECT_EQ(nemesis_scythe_cooldown(s.monsters[0]), 0);
    EXPECT_EQ(s.monsters[0].flags & kMonsterFlagNemesisFirstMove, 0u)
        << "firstMove is spent by init()'s rollMove";

    // Force the Scythe through the num < 30 arm and watch the cooldown load.
    s.monsters[0].move_history[0] = r::kNemesisMoveTriBurn;
    nemesis_decide_move(s, 0, 0);
    ASSERT_EQ(s.monsters[0].move_history[0], r::kNemesisMoveScythe);
    EXPECT_EQ(nemesis_scythe_cooldown(s.monsters[0]), 2);
    // Next decision: --cooldown to 1 and lastMove IS Scythe, so the first test
    // fails twice over and the arm falls through to the randomBoolean.
    nemesis_decide_move(s, 0, 0);
    EXPECT_NE(s.monsters[0].move_history[0], r::kNemesisMoveScythe);
    EXPECT_EQ(nemesis_scythe_cooldown(s.monsters[0]), 1);
}

TEST(BeyondElites, NemesisSpendsAnExtraAiDrawEvenWhenTheCooldownRefuses) {
    // The `>= 65` arm is `if (aiRng.randomBoolean() && scytheCooldown <= 0)`
    // (Nemesis.java:187). Java evaluates the LEFT operand first, so the draw is
    // spent whatever the cooldown says. Swap the operands and the shared stream
    // desynchronises silently.
    CombatState s = SoloGroup(7, MonsterId::NEMESIS);
    s.monsters[0].move_history[0] = r::kNemesisMoveTriBurn;
    nemesis_set_scythe_cooldown(s.monsters[0], 9);  // will still be > 0 after --
    const auto before = s.ai_rng.counter;
    nemesis_decide_move(s, 0, 80);
    EXPECT_EQ(s.ai_rng.counter, before + 1)
        << "one randomBoolean, spent despite the cooldown refusing the Scythe";
    EXPECT_EQ(s.monsters[0].move_history[0], r::kNemesisMoveTriAttack);

    // ...and an arm that never reaches a randomBoolean spends nothing extra.
    s.monsters[0].move_history[0] = r::kNemesisMoveScythe;
    s.monsters[0].move_history[1] = r::kNemesisMoveScythe;
    const auto before2 = s.ai_rng.counter;
    nemesis_decide_move(s, 0, 90);
    EXPECT_EQ(s.ai_rng.counter, before2)
        << "`!lastMove(TRI_BURN)` short-circuits before any draw";
    EXPECT_EQ(s.monsters[0].move_history[0], r::kNemesisMoveTriBurn);
}

TEST(BeyondElites, NemesisFirstMoveIsABareFiftyFiftyOnNum) {
    CombatState s = MakeSeeded(3, /*monsters=*/1);
    // Build the record without running init, so firstMove is still pending.
    MonsterState& m = s.monsters[0];
    m = MonsterState{};
    m.monster_id = static_cast<uint16_t>(MonsterId::NEMESIS);
    m.hp = 200;
    m.max_hp = 200;
    m.flags |= kMonsterFlagNemesisFirstMove;
    const auto before = s.ai_rng.counter;
    nemesis_decide_move(s, 0, 49);
    EXPECT_EQ(m.move_history[0], r::kNemesisMoveTriAttack);
    EXPECT_EQ(s.ai_rng.counter, before) << "the firstMove arm takes no extra draw";

    m.flags |= kMonsterFlagNemesisFirstMove;
    m.move_history[0] = 0;
    nemesis_decide_move(s, 0, 50);
    EXPECT_EQ(m.move_history[0], r::kNemesisMoveTriBurn);
}

// ============================================================================
// 5. The Reptomancer and its daggers
// ============================================================================

TEST(BeyondElites, ReptomancerExtraCtorDrawShiftsTheSecondDaggersHpRoll) {
    // The Taskmaster's fact, in a new group. The Reptomancer sits at index 1, so
    // its DISCARDED super-argument draw moves the roll the SECOND dagger takes.
    // Re-derived by hand off the same seed rather than asserted as a constant.
    constexpr int64_t kSeed = 4242;
    RngStream hp = from_seed(kSeed);
    const int32_t d0 = random(hp, 20, 25);                 // dagger at index 0
    (void)random(hp, 180, 190);                            // the super argument
    const int32_t repto = random(hp, 190, 200);            // the setHp under it
    const int32_t d2 = random(hp, 20, 25);                 // dagger at index 2

    CombatState s = ReptomancerGroup(kSeed);
    ASSERT_EQ(s.monster_count, 3);
    EXPECT_EQ(s.monsters[0].max_hp, d0);
    EXPECT_EQ(s.monsters[kRepto].max_hp, repto);
    EXPECT_EQ(s.monsters[2].max_hp, d2);

    // The negative control: WITHOUT the discarded draw the third roll differs,
    // which is the only way the extra draw is observable at all.
    RngStream naive = from_seed(kSeed);
    (void)random(naive, 20, 25);
    (void)random(naive, 190, 200);
    EXPECT_NE(random(naive, 20, 25), d2)
        << "if this ever ties, pick another seed -- the test is meaningless "
           "without the difference";
}

TEST(BeyondElites, ReptomancerPreBattleMinionizesEveryoneAndPlacesTheDaggers) {
    CombatState s = ReptomancerGroup(4242);
    EXPECT_TRUE(monster_has(s, 0, PowerId::MINION));
    EXPECT_TRUE(monster_has(s, 2, PowerId::MINION));
    EXPECT_FALSE(monster_has(s, kRepto, PowerId::MINION))
        << "`!m.id.equals(this.id)` spares the Reptomancer itself";
    EXPECT_EQ(monster_power(s, 0, PowerId::MINION), kMinionAppliedAmount);

    // daggers[1] is the record BEFORE the Reptomancer (POSX[1] = -220) and
    // daggers[0] the one AFTER it (POSX[0] = 210) -- the index comparison at
    // Reptomancer.java:96-100, which is also MonsterHelper's construction order.
    EXPECT_EQ(s.monsters[0].draw_x, kReptomancerDaggerX[1]);
    EXPECT_EQ(s.monsters[kRepto].draw_x, kReptomancerDrawX);
    EXPECT_EQ(s.monsters[2].draw_x, kReptomancerDaggerX[0]);
}

TEST(BeyondElites, ReptomancerSpawnTurnPlansBothDaggersAtQueueTime) {
    // The batch's most fragile ordering. At A20 daggersPerSpawn is 2, and the
    // two initial daggers hold POSX[1] and POSX[0], so the free slots are 2
    // (180) and 3 (-250) -- IN INDEX ORDER, not left-to-right.
    constexpr int64_t kSeed = 909;
    CombatState s = ReptomancerGroup(kSeed);
    const auto hp_before = s.monster_hp_rng.counter;
    const auto ai_before = s.ai_rng.counter;

    telegraph(s, kRepto, r::kReptomancerMoveSpawnDagger, MonsterIntent::UNKNOWN);
    reptomancer_take_turn(s, kRepto);

    EXPECT_EQ(s.monster_hp_rng.counter, hp_before + 2)
        << "BOTH SnakeDagger constructors run at QUEUE time "
           "(Reptomancer.java:123)";
    EXPECT_EQ(s.ai_rng.counter, ai_before)
        << "the children's init() rolls are RESOLVE-time work";
    ASSERT_EQ(s.action_count, 3);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::SPAWN_MONSTER));
    EXPECT_EQ(queued(s, 1).opcode, static_cast<uint16_t>(Opcode::SPAWN_MONSTER));
    EXPECT_EQ(queued(s, 2).opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE));
    EXPECT_EQ(spawn_draw_x_from_flags(queued(s, 0).flags),
              kReptomancerDaggerX[2]);
    EXPECT_EQ(spawn_draw_x_from_flags(queued(s, 1).flags),
              kReptomancerDaggerX[3]);
    EXPECT_NE(queued(s, 0).flags & kSpawnApplyMinion, 0u);
    EXPECT_NE(queued(s, 0).flags & kSpawnMinionAtTop, 0u)
        << "SpawnMonsterAction.java:68 is addToTop, unlike "
           "SummonGremlinAction.java:114";
    EXPECT_EQ(queued(s, 0).flags & kSpawnRunPreBattle, 0u)
        << "SpawnMonsterAction does not call usePreBattleAction";

    // Positions: list x's are [-220, -20, 210]. A newcomer at 180 counts the two
    // records it is strictly right of -> slot 2. Then the list is
    // [-220, -20, 180, 210] and a newcomer at -250 counts none -> slot 0.
    EXPECT_EQ(queued(s, 0).tgt, 2);
    EXPECT_EQ(queued(s, 1).tgt, 0);
    // ...which pushes the Reptomancer from index 1 to index 2.
    EXPECT_EQ(queued(s, 2).tgt, 2)
        << "the trailing RollMoveAction must name the POST-insertion index";

    // Resolve: the children roll on ai_rng, in queue order.
    drain(s);
    EXPECT_EQ(s.monster_count, 5);
    EXPECT_EQ(s.ai_rng.counter, ai_before + 3)
        << "dagger, dagger, then the Reptomancer's own roll";
    EXPECT_EQ(s.monsters[2].monster_id,
              static_cast<uint16_t>(MonsterId::REPTOMANCER));
    EXPECT_EQ(s.monsters[0].draw_x, kReptomancerDaggerX[3]);
    EXPECT_EQ(s.monsters[3].draw_x, kReptomancerDaggerX[2]);
    EXPECT_TRUE(monster_has(s, 0, PowerId::MINION));
    EXPECT_TRUE(monster_has(s, 3, PowerId::MINION));
    // Both spawned daggers open on WOUND -- firstMove, per record.
    EXPECT_EQ(s.monsters[0].move_history[0], r::kSnakeDaggerMoveWound);
    EXPECT_EQ(s.monsters[3].move_history[0], r::kSnakeDaggerMoveWound);
}

TEST(BeyondElites, ReptomancerSlotsAreDerivedFromLivePositionsAndAreRecycled) {
    // With both encounter daggers dead, slots 0 and 1 are free again and the
    // two spawns take them IN INDEX ORDER -- POSX[0] = 210 first.
    CombatState s = ReptomancerGroup(909);
    s.monsters[0].hp = 0;
    s.monsters[2].hp = 0;
    telegraph(s, kRepto, r::kReptomancerMoveSpawnDagger, MonsterIntent::UNKNOWN);
    reptomancer_take_turn(s, kRepto);
    EXPECT_EQ(spawn_draw_x_from_flags(queued(s, 0).flags),
              kReptomancerDaggerX[0]);
    EXPECT_EQ(spawn_draw_x_from_flags(queued(s, 1).flags),
              kReptomancerDaggerX[1]);
    drain(s);
    // The DEAD records keep their draw_x and stay in the list, so the group now
    // holds two records at each of POSX[0] and POSX[1] -- one dead, one live --
    // and the derivation still says both slots are occupied.
    telegraph(s, kRepto + 1, r::kReptomancerMoveSpawnDagger,
              MonsterIntent::UNKNOWN);
    ASSERT_EQ(s.monsters[kRepto + 1].monster_id,
              static_cast<uint16_t>(MonsterId::REPTOMANCER));
    reptomancer_take_turn(s, static_cast<uint8_t>(kRepto + 1));
    EXPECT_EQ(spawn_draw_x_from_flags(queued(s, 0).flags),
              kReptomancerDaggerX[2]);
    EXPECT_EQ(spawn_draw_x_from_flags(queued(s, 1).flags),
              kReptomancerDaggerX[3]);
}

TEST(BeyondElites, ReptomancerSpawnStopsAtFourPositionsEvenWhenAskedForMore) {
    // The POSITION cap is independent of canSpawn's group cap: takeTurn spawns
    // only into free POSX slots, and with all four held it spawns nothing at all
    // -- but the trailing RollMoveAction still happens.
    CombatState s = ReptomancerGroup(909);
    telegraph(s, kRepto, r::kReptomancerMoveSpawnDagger, MonsterIntent::UNKNOWN);
    reptomancer_take_turn(s, kRepto);
    drain(s);  // slots 2 and 3 now filled; all four held
    const uint8_t repto_now = 2;
    ASSERT_EQ(s.monsters[repto_now].monster_id,
              static_cast<uint16_t>(MonsterId::REPTOMANCER));
    const auto hp_before = s.monster_hp_rng.counter;
    telegraph(s, repto_now, r::kReptomancerMoveSpawnDagger,
              MonsterIntent::UNKNOWN);
    reptomancer_take_turn(s, repto_now);
    EXPECT_EQ(count_opcodes(s, Opcode::SPAWN_MONSTER), 0);
    EXPECT_EQ(s.monster_hp_rng.counter, hp_before)
        << "no construction, so no draw";
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE));
    EXPECT_EQ(queued(s, 0).tgt, repto_now) << "nothing inserted, index unmoved";
}

TEST(BeyondElites, ReptomancerCanSpawnCountsEveryLiveRecordNotJustDaggers) {
    CombatState s = ReptomancerGroup(909);
    EXPECT_EQ(reptomancer_alive_count(s, kRepto), 2);
    s.monsters[0].hp = 0;
    EXPECT_EQ(reptomancer_alive_count(s, kRepto), 1)
        << "`if (m == this || m.isDying) continue;` (Reptomancer.java:142)";
    // The gate is on the DECISION only. Fill the group past the threshold and
    // the num-in-[33,66) arm turns into Snake Strike instead of a summon.
    s.monsters[0].hp = 10;
    telegraph(s, kRepto, r::kReptomancerMoveSpawnDagger, MonsterIntent::UNKNOWN);
    reptomancer_take_turn(s, kRepto);
    drain(s);  // now four other records are alive
    const uint8_t repto_now = 2;
    ASSERT_GT(reptomancer_alive_count(s, repto_now), kReptomancerMaxOtherAlive);
    s.monsters[repto_now].move_history[0] = r::kReptomancerMoveBigBite;
    s.monsters[repto_now].move_history[1] = r::kReptomancerMoveBigBite;
    reptomancer_decide_move(s, repto_now, 40);
    EXPECT_EQ(s.monsters[repto_now].move_history[0],
              r::kReptomancerMoveSnakeStrike)
        << "canSpawn() false -> the else arm (Reptomancer.java:185)";
}

TEST(BeyondElites, ReptomancerGetMoveRecursesWithFreshDraws) {
    CombatState s = ReptomancerGroup(909);
    MonsterState& m = s.monsters[kRepto];
    // Arm 1: num < 33 with lastMove == SNAKE_STRIKE re-enters with a fresh
    // aiRng.random(33, 99), which can never come back below 33.
    m.move_history[0] = r::kReptomancerMoveSnakeStrike;
    m.move_history[1] = r::kReptomancerMoveBigBite;
    auto before = s.ai_rng.counter;
    reptomancer_decide_move(s, kRepto, 0);
    EXPECT_GE(s.ai_rng.counter, before + 1) << "at least one re-draw";
    EXPECT_NE(m.move_history[0], r::kReptomancerMoveSnakeStrike);

    // Arm 2: num >= 66 with lastMove == BIG_BITE re-enters with random(65),
    // which WIDENS the range back down -- so it can land on any arm.
    m.move_history[0] = r::kReptomancerMoveBigBite;
    m.move_history[1] = r::kReptomancerMoveSnakeStrike;
    before = s.ai_rng.counter;
    reptomancer_decide_move(s, kRepto, 99);
    EXPECT_GE(s.ai_rng.counter, before + 1);
    EXPECT_NE(m.move_history[0], r::kReptomancerMoveBigBite);
}

TEST(BeyondElites, ReptomancerFirstMoveForcesTheOpeningSummonWithoutReadingNum) {
    CombatState s = ReptomancerGroup(909);
    // init() already spent the latch on the forced SPAWN_DAGGER.
    EXPECT_EQ(s.monsters[kRepto].move_history[0],
              r::kReptomancerMoveSpawnDagger);
    EXPECT_EQ(s.monsters[kRepto].flags & kMonsterFlagReptomancerFirstMove, 0u);
    // ...and the aiRng.random(99) that produced the ignored `num` was still
    // spent: one draw per monster in spawn order.
    RngStream ai = from_seed(909);
    (void)random(ai, 99);
    (void)random(ai, 99);
    (void)random(ai, 99);
    EXPECT_EQ(s.ai_rng.counter, ai.counter);
}

TEST(BeyondElites, ReptomancerDeathSuicidesEverySurvivorInReverseListOrder) {
    CombatState s = ReptomancerGroup(909);
    ASSERT_EQ(s.monster_count, 3);
    // The sweep runs on the POST-super side, so the Reptomancer's own record is
    // already at 0 HP when it walks -- which is the only thing excluding it.
    s.monsters[kRepto].hp = 0;
    reptomancer_die_after(s, kRepto);
    ASSERT_EQ(s.action_count, 2);
    // Both pushes are addToTop, so the LAST survivor's suicide is on top.
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::SUICIDE));
    EXPECT_EQ(queued(s, 0).tgt, 2);
    EXPECT_EQ(queued(s, 1).tgt, 0);
    EXPECT_EQ(queued(s, 0).flags & 1u, 1u)
        << "the 1-arg SuicideAction defaults triggerRelics to TRUE";
    drain(s);
    EXPECT_EQ(s.monsters[0].hp, 0);
    EXPECT_EQ(s.monsters[2].hp, 0);
}

TEST(BeyondElites, ReptomancerDeathSweepSkipsRecordsThatAreAlreadyDown) {
    CombatState s = ReptomancerGroup(909);
    s.monsters[0].hp = 0;   // already dead before the boss falls
    s.monsters[kRepto].hp = 0;
    reptomancer_die_after(s, kRepto);
    EXPECT_EQ(count_opcodes(s, Opcode::SUICIDE), 1)
        << "`if (m.isDead || m.isDying) continue;`";
    EXPECT_EQ(queued(s, 0).tgt, 2);
}

TEST(BeyondElites, SnakeDaggerOpensOnWoundAndThenExplodesForever) {
    CombatState s = ReptomancerGroup(909);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kSnakeDaggerMoveWound);
    EXPECT_EQ(s.monsters[2].move_history[0], r::kSnakeDaggerMoveWound);
    // Every later decision is EXPLODE, and each still spends its aiRng draw.
    for (int i = 0; i < 3; ++i) {
        const auto before = s.ai_rng.counter;
        snake_dagger_roll_move(s, 0);
        EXPECT_EQ(s.ai_rng.counter, before + 1)
            << "getMove ignores num, rollMove still draws";
        EXPECT_EQ(s.monsters[0].move_history[0], r::kSnakeDaggerMoveExplode);
    }
}

TEST(BeyondElites, SnakeDaggerWoundTurnDealsNineAndMakesOneWound) {
    CombatState s = ReptomancerGroup(909);
    const int16_t hp = s.player_hp;
    telegraph(s, 0, r::kSnakeDaggerMoveWound, MonsterIntent::ATTACK_DEBUFF);
    snake_dagger_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(s.player_hp, hp - 9);
    EXPECT_EQ(discard_count_of(s, CardId::WOUND), 1);
}

TEST(BeyondElites, SnakeDaggerExplodeKillsItselfWithItsLiveHpAndStillRolls) {
    CombatState s = ReptomancerGroup(909);
    const int16_t dagger_hp = s.monsters[0].hp;
    const int16_t player = s.player_hp;
    telegraph(s, 0, r::kSnakeDaggerMoveExplode, MonsterIntent::ATTACK);
    snake_dagger_take_turn(s, 0);
    ASSERT_EQ(s.action_count, 3);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::DAMAGE));
    EXPECT_EQ(queued(s, 0).amount, 25);
    EXPECT_EQ(queued(s, 1).opcode, static_cast<uint16_t>(Opcode::LOSE_HP));
    EXPECT_EQ(queued(s, 1).amount, dagger_hp)
        << "`this.currentHealth` is read at QUEUE time (SnakeDagger.java:73)";
    EXPECT_EQ(queued(s, 2).opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE))
        << "RollMoveAction is outside the switch and has no liveness gate";
    drain(s);
    EXPECT_EQ(s.player_hp, player - 25);
    EXPECT_EQ(s.monsters[0].hp, 0);
}

TEST(BeyondElites, SmartPositioningHasTwoFormsAndTheyAreNotTheSameLoop) {
    // SummonGremlinAction BREAKS; SpawnMonsterAction COUNTS. They agree on a
    // sorted list and diverge the moment one is not sorted -- which is the whole
    // reason there are two functions.
    CombatState s = MakeState(3);
    s.monsters[0].draw_x = -220;
    s.monsters[1].draw_x = -20;
    s.monsters[2].draw_x = 210;
    EXPECT_EQ(smart_position_for(s, 180), 2);
    EXPECT_EQ(smart_position_for_spawn_action(s, 180), 2)
        << "sorted list: the two agree, which is why the Reptomancer never "
           "notices";

    // An UNSORTED list -- what MonsterHelper is free to build.
    s.monsters[0].draw_x = 210;
    s.monsters[1].draw_x = -220;
    s.monsters[2].draw_x = -20;
    EXPECT_EQ(smart_position_for(s, 180), 0)
        << "break: stops at the first record it is not strictly right of";
    EXPECT_EQ(smart_position_for_spawn_action(s, 180), 2)
        << "continue: counts BOTH of -220 and -20 across the whole list";
}

// ============================================================================
// 6. Encounter compositions -- spawn-order-exact
// ============================================================================

TEST(BeyondElites, Act3EliteEncountersSpawnInTheJavaListOrder) {
    using V = std::vector<std::string_view>;
    EXPECT_EQ(members_of("Giant Head", 1), (V{"GiantHead"}))
        << "MonsterHelper.java:579-581 -- solo";
    EXPECT_EQ(members_of("Nemesis", 1), (V{"Nemesis"}))
        << "MonsterHelper.java:573-575 -- solo";
    EXPECT_EQ(members_of("Reptomancer", 1),
              (V{"Dagger", "Reptomancer", "Dagger"}))
        << "MonsterHelper.java:536-539 -- dagger at POSX[1], the Reptomancer, "
           "dagger at POSX[0]; SnakeDagger.ID is \"Dagger\"";
    // None of the three draws misc_rng: no POOL step anywhere in the rows.
    for (const char* key : {"Giant Head", "Nemesis", "Reptomancer"}) {
        RngStream misc = from_seed(5);
        ResolvedGroup g{};
        ASSERT_TRUE(resolve_encounter(key, misc, g)) << key;
        EXPECT_EQ(misc.counter, 0) << key << " must be a fixed composition";
    }
}

TEST(BeyondElites, EveryAct3EliteGameIdResolvesToItsRow) {
    EXPECT_EQ(static_cast<MonsterId>(r::monster_from_game_id("GiantHead")),
              MonsterId::GIANT_HEAD);
    EXPECT_EQ(static_cast<MonsterId>(r::monster_from_game_id("Nemesis")),
              MonsterId::NEMESIS);
    EXPECT_EQ(static_cast<MonsterId>(r::monster_from_game_id("Reptomancer")),
              MonsterId::REPTOMANCER);
    EXPECT_EQ(static_cast<MonsterId>(r::monster_from_game_id("Dagger")),
              MonsterId::SNAKE_DAGGER)
        << "the ID string is \"Dagger\", not \"SnakeDagger\"";
}

}  // namespace
}  // namespace sts::engine
