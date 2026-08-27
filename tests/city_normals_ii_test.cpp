// Act-2 city normals II -- the Mugger, the Snake Plant, the Snecko, the
// Centurion and the Healer, plus the one power they bring (Malleable), the
// BLOCK_RANDOM_MONSTER opcode, op_heal's new monster branch, the MonsterDieFn
// death-edge seam, and the faithful per-steal stolen-gold settlement.
//
// WHAT THIS FILE PINS, and why each part is here rather than assumed:
//
//   * STAT/MOVE TABLES, per monster, at EVERY ascension branch the Java has --
//     not just the A20 column the engine runs. Two of these rows exist to pin
//     NON-differences (the Centurion's furyHits, assigned 3 on both sides of its
//     A2 branch; the Healer's healAmt at A2 and magicDmg at A17), because a
//     "surely it scales" edit is exactly the change that would slip through.
//   * RNG DRAW COUNTS, per monster, per phase. The Mugger is the whole reason
//     this matters here: its playSfx and playDeathSfx roll the SEEDED aiRng
//     where the Looter's identically-shaped methods roll unseeded MathUtils, so
//     a wrong reading desynchronises every later monster in the group.
//   * THE CONDITIONAL DRAW inside BLOCK_RANDOM_MONSTER: a Centurion with no
//     valid ally spends NOTHING, one with an ally spends exactly one. That is
//     the difference between two seeds' whole combats.
//   * MALLEABLE'S THREE EDGES: the escalate-then-reset cycle, the STRICT
//     survival test (an exactly-lethal hit triggers nothing), and the damage
//     types it ignores.
//   * THE PER-STEAL GOLD CLAMP, RED-first: two thieves against a purse smaller
//     than their combined take, with exactly one of them killed. The previous
//     sum-then-clamp returns a different number here, which is the whole reason
//     the deviation was re-scoped to this task.
//   * ENCOUNTER COMPOSITIONS, spawn-order-exact, because spawn order fixes each
//     monster's index -- and the Centurion's PROTECT reads the group.

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_centurion.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_healer.hpp"
#include "sts/engine/monster_looter.hpp"
#include "sts/engine/monster_mugger.hpp"
#include "sts/engine/monster_snake_plant.hpp"
#include "sts/engine/monster_snecko.hpp"
#include "sts/engine/power_hooks.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

namespace r = sts::registry;
constexpr int32_t kA20 = kMonsterAscension;

// --- shared helpers ---------------------------------------------------------

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

int16_t monster_power_counter(const CombatState& s, uint8_t mi, PowerId id) {
    const MonsterState& m = s.monsters[mi];
    for (uint8_t i = 0; i < m.power_count; ++i) {
        if (m.powers[i].power_id == static_cast<uint16_t>(id)) {
            return m.powers[i].counter;
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

bool player_has(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return true;
        }
    }
    return false;
}

void give_monster_power(CombatState& s, uint8_t mi, PowerId id, int16_t amt,
                        int16_t counter = 0) {
    MonsterState& m = s.monsters[mi];
    m.powers[m.power_count].power_id = static_cast<uint16_t>(id);
    m.powers[m.power_count].amount = amt;
    m.powers[m.power_count].counter = counter;
    ++m.power_count;
}

// APPLY_POWER through the real opcode dispatch (the internal op_apply_power is
// not on the test include path, and going through execute_opcode also pins the
// dispatch entry rather than only the body).
void apply_power(CombatState& s, uint8_t src, uint8_t tgt, PowerId id,
                 int32_t amount) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    it.src = src;
    it.tgt = tgt;
    it.amount = amount;
    it.flags = make_apply_power_flags(id);
    execute_opcode(s, it);
}

// BLOCK_RANDOM_MONSTER through the real opcode dispatch. `tgt` is deliberately
// set to `src` here, exactly as the authored step does, to prove the op ignores
// it and resolves its own recipient.
void block_random_monster(CombatState& s, uint8_t src, int32_t amount) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::BLOCK_RANDOM_MONSTER);
    it.src = src;
    it.tgt = src;
    it.amount = amount;
    execute_opcode(s, it);
}

// A plain NORMAL attack from the player onto monster `mi`, through the real
// DAMAGE opcode so every hook fires exactly as it does in combat.
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

// Telegraph `move` on `mi` without touching any stream, so a turn body can be
// driven directly. set_monster_move also pushes the ring, which is what the
// history predicates read.
void telegraph(CombatState& s, uint8_t mi, uint8_t move, MonsterIntent intent) {
    set_monster_move(s.monsters[mi], move, intent);
}

// ============================================================================
// 1. Stat and move tables -- every ascension branch, per monster
// ============================================================================

// Mugger.java:61-75. THREE tier boundaries in one ctor -- goldAmt at >= 17, HP
// at >= 7, the damage block at >= 2 -- plus escapeDef's +6 at >= 17 which lives
// in takeTurn (:119-123), not the ctor.
TEST(CityNormalsII, MuggerStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kMugger;
    EXPECT_EQ(d.hp_min(0), 48);
    EXPECT_EQ(d.hp_max(0), 52);
    EXPECT_EQ(d.hp_min(6), 48) << "the HP branch is >= 7";
    EXPECT_EQ(d.hp_min(7), 50);
    EXPECT_EQ(d.hp_max(7), 54);
    EXPECT_EQ(d.hp_min(kA20), 50);
    EXPECT_EQ(d.hp_max(kA20), 54);

    // swipeDmg 10 / 11 at >= 2; bigSwipeDmg 16 / 18 at >= 2.
    EXPECT_EQ(step_amount(d, r::kMuggerMoveMug, 0, 1), 10);
    EXPECT_EQ(step_amount(d, r::kMuggerMoveMug, 0, 2), 11);
    EXPECT_EQ(step_amount(d, r::kMuggerMoveMug, 0, kA20), 11);
    EXPECT_EQ(step_amount(d, r::kMuggerMoveBigswipe, 0, 1), 16);
    EXPECT_EQ(step_amount(d, r::kMuggerMoveBigswipe, 0, 2), 18);

    // escapeDef 11, +6 at >= 17 -- boundary asserted at 16/17. NOT the Looter's
    // flat 6: the two thieves do not share this number.
    EXPECT_EQ(step_amount(d, r::kMuggerMoveSmokeBomb, 0, 0), 11);
    EXPECT_EQ(step_amount(d, r::kMuggerMoveSmokeBomb, 0, 16), 11);
    EXPECT_EQ(step_amount(d, r::kMuggerMoveSmokeBomb, 0, 17), 17);
    EXPECT_EQ(step_amount(d, r::kMuggerMoveSmokeBomb, 0, kA20), 17);
    EXPECT_EQ(step_amount(r::kLooter, r::kLooterMoveSmokeBomb, 0, kA20), 6)
        << "the Looter's block is flat 6 -- the rows must not be unified";

    EXPECT_EQ(d.move(r::kMuggerMoveEscape)->intent, r::MonsterIntent::ESCAPE);
    EXPECT_EQ(d.move(r::kMuggerMoveSmokeBomb)->intent, r::MonsterIntent::DEFEND);
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::NORMAL);
    EXPECT_TRUE(d.ai_native);
    EXPECT_EQ(d.roll_count, 0);
    // goldAmt: 15 below A17, 20 from A17 (Mugger.java:61). The engine's constant
    // is the A20 column; asserted against the Looter's, which is numerically
    // equal and structurally separate.
    EXPECT_EQ(kMuggerGoldAmt, 20);
    EXPECT_EQ(thief_gold_amount(MonsterId::MUGGER), 20);
    EXPECT_EQ(thief_gold_amount(MonsterId::LOOTER), kLooterGoldAmt);
}

// The named A2 pin from the test plan, kept as its own case because it is the
// number a reader is most likely to copy from the Looter row (12/14) by mistake.
TEST(CityNormalsII, MuggerBigSwipeIsEighteenAtA2) {
    EXPECT_EQ(step_amount(r::kMugger, r::kMuggerMoveBigswipe, 0, 2), 18);
    EXPECT_EQ(step_amount(r::kMugger, r::kMuggerMoveBigswipe, 0, kA20), 18);
    EXPECT_EQ(step_amount(r::kLooter, r::kLooterMoveLunge, 0, 2), 14)
        << "the Looter's equivalent move is 14, not 18";
}

// The other named pin: the A17 block arm, which is written as `escapeDef + 6`
// in takeTurn rather than as a named constant.
TEST(CityNormalsII, MuggerSmokeBombBlockIsSeventeenAtA17) {
    EXPECT_EQ(step_amount(r::kMugger, r::kMuggerMoveSmokeBomb, 0, 16), 11);
    EXPECT_EQ(step_amount(r::kMugger, r::kMuggerMoveSmokeBomb, 0, 17), 17);

    // ...and it actually reaches the record at A20.
    CombatState s = MakeSeeded(3);
    mugger_init(s, 0);
    telegraph(s, 0, r::kMuggerMoveSmokeBomb, MonsterIntent::DEFEND);
    mugger_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(s.monsters[0].block, 17);
}

// SnakePlant.java:59-64. HP at >= 7, rainBlowsDmg at >= 2, and a hit COUNT that
// does not move at all.
TEST(CityNormalsII, SnakePlantStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kSnakePlant;
    EXPECT_EQ(d.hp_min(0), 75);
    EXPECT_EQ(d.hp_max(0), 79);
    EXPECT_EQ(d.hp_min(6), 75);
    EXPECT_EQ(d.hp_min(7), 78);
    EXPECT_EQ(d.hp_max(7), 82);
    EXPECT_EQ(d.hp_min(kA20), 78);
    EXPECT_EQ(d.hp_max(kA20), 82);

    EXPECT_EQ(step_amount(d, r::kSnakePlantMoveChompyChomps, 0, 1), 7);
    EXPECT_EQ(step_amount(d, r::kSnakePlantMoveChompyChomps, 0, 2), 8);
    EXPECT_EQ(step_amount(d, r::kSnakePlantMoveChompyChomps, 2, kA20), 8);

    // SPORES: Frail FIRST, then Weak, both flat 2.
    const r::MonsterMove* sp = d.move(r::kSnakePlantMoveSpores);
    ASSERT_NE(sp, nullptr);
    ASSERT_EQ(sp->effect_count, 2);
    EXPECT_EQ(sp->effects[0].extra, make_apply_power_flags(PowerId::FRAIL));
    EXPECT_EQ(sp->effects[1].extra, make_apply_power_flags(PowerId::WEAK));
    EXPECT_EQ(sp->effects[0].amount.at(0), 2);
    EXPECT_EQ(sp->effects[1].amount.at(kA20), 2);
    EXPECT_EQ(sp->intent, r::MonsterIntent::STRONG_DEBUFF);

    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::NORMAL);
    EXPECT_EQ(d.roll_count, 0);
    EXPECT_EQ(kSnakePlantMalleableAmount, 3);
}

// Snecko.java:73-84. The one monster in the batch whose getMove has NO ascension
// branch -- but whose TAIL move gains a whole STEP at A17.
TEST(CityNormalsII, SneckoStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kSnecko;
    EXPECT_EQ(d.hp_min(0), 114);
    EXPECT_EQ(d.hp_max(0), 120);
    EXPECT_EQ(d.hp_min(6), 114);
    EXPECT_EQ(d.hp_min(7), 120);
    EXPECT_EQ(d.hp_max(7), 125);
    EXPECT_EQ(d.hp_min(kA20), 120);

    EXPECT_EQ(step_amount(d, r::kSneckoMoveBite, 0, 1), 15);
    EXPECT_EQ(step_amount(d, r::kSneckoMoveBite, 0, 2), 18);
    EXPECT_EQ(step_amount(d, r::kSneckoMoveTail, 0, 1), 8);
    EXPECT_EQ(step_amount(d, r::kSneckoMoveTail, 0, 2), 10);

    // TAIL's three authored steps, in order: damage, Weak, Vulnerable.
    const r::MonsterMove* tail = d.move(r::kSneckoMoveTail);
    ASSERT_NE(tail, nullptr);
    ASSERT_EQ(tail->effect_count, 3);
    EXPECT_EQ(tail->effects[1].extra, make_apply_power_flags(PowerId::WEAK));
    EXPECT_EQ(tail->effects[2].extra,
              make_apply_power_flags(PowerId::VULNERABLE));
    EXPECT_EQ(tail->effects[1].amount.at(kA20), 2);
    EXPECT_EQ(tail->effects[2].amount.at(0), 2);
    EXPECT_EQ(tail->intent, r::MonsterIntent::ATTACK_DEBUFF);

    // The step's PRESENCE gate -- the thing the table itself cannot say.
    EXPECT_FALSE(snecko_tail_applies_weak(16));
    EXPECT_TRUE(snecko_tail_applies_weak(17));
    EXPECT_TRUE(snecko_tail_applies_weak(kA20));

    // GLARE's Confusion carries AbstractPower's -1, not 1 (see the oracle pin
    // below).
    EXPECT_EQ(step_amount(d, r::kSneckoMoveGlare, 0, kA20),
              kConfusionAppliedAmount);
    EXPECT_EQ(kConfusionAppliedAmount, -1);
    EXPECT_EQ(d.move(r::kSneckoMoveGlare)->intent,
              r::MonsterIntent::STRONG_DEBUFF);
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::NORMAL);
    EXPECT_EQ(d.roll_count, 0);
}

// Centurion.java:57-71. HP at >= 7, blockAmount at >= 17, damage at >= 2 -- and
// furyHits assigned 3 on BOTH sides of that last branch.
TEST(CityNormalsII, CenturionStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kCenturion;
    EXPECT_EQ(d.hp_min(0), 76);
    EXPECT_EQ(d.hp_max(0), 80);
    EXPECT_EQ(d.hp_min(6), 76);
    EXPECT_EQ(d.hp_min(7), 78);
    EXPECT_EQ(d.hp_max(7), 83);

    EXPECT_EQ(step_amount(d, r::kCenturionMoveSlash, 0, 1), 12);
    EXPECT_EQ(step_amount(d, r::kCenturionMoveSlash, 0, 2), 14);
    EXPECT_EQ(step_amount(d, r::kCenturionMoveFury, 0, 1), 6);
    EXPECT_EQ(step_amount(d, r::kCenturionMoveFury, 0, 2), 7);

    // blockAmount 15 / 20 at >= 17 -- boundary asserted at 16/17.
    EXPECT_EQ(step_amount(d, r::kCenturionMoveProtect, 0, 0), 15);
    EXPECT_EQ(step_amount(d, r::kCenturionMoveProtect, 0, 16), 15);
    EXPECT_EQ(step_amount(d, r::kCenturionMoveProtect, 0, 17), 20);
    EXPECT_EQ(step_amount(d, r::kCenturionMoveProtect, 0, kA20), 20);

    // PROTECT is the BLOCK_RANDOM_MONSTER opcode, not a plain BLOCK.
    const r::MonsterMove* prot = d.move(r::kCenturionMoveProtect);
    ASSERT_NE(prot, nullptr);
    ASSERT_EQ(prot->effect_count, 1);
    EXPECT_EQ(static_cast<uint16_t>(prot->effects[0].op),
              static_cast<uint16_t>(Opcode::BLOCK_RANDOM_MONSTER));
    EXPECT_EQ(prot->effects[0].target, r::MonsterMoveTarget::SELF);
    EXPECT_EQ(prot->intent, r::MonsterIntent::DEFEND);

    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::NORMAL);
    EXPECT_EQ(d.roll_count, 0);
}

// Healer.java:59-76. The A17 arm is a full RESTATEMENT, not an increment on top
// of A2: healAmt does NOT move at A2 and magicDmg does NOT move at A17.
TEST(CityNormalsII, HealerA17StatsDoNotInheritA2) {
    const auto& d = r::kHealer;
    EXPECT_EQ(d.hp_min(0), 48);
    EXPECT_EQ(d.hp_max(0), 56);
    EXPECT_EQ(d.hp_min(6), 48);
    EXPECT_EQ(d.hp_min(7), 50);
    EXPECT_EQ(d.hp_max(7), 58);

    // magicDmg 8 -> 9 at A2 and STILL 9 at A17.
    EXPECT_EQ(step_amount(d, r::kHealerMoveAttack, 0, 0), 8);
    EXPECT_EQ(step_amount(d, r::kHealerMoveAttack, 0, 1), 8);
    EXPECT_EQ(step_amount(d, r::kHealerMoveAttack, 0, 2), 9);
    EXPECT_EQ(step_amount(d, r::kHealerMoveAttack, 0, 16), 9);
    EXPECT_EQ(step_amount(d, r::kHealerMoveAttack, 0, 17), 9)
        << "magicDmg is 9 on BOTH the A2 and A17 arms (Healer.java:66,71)";
    EXPECT_EQ(step_amount(d, r::kHealerMoveAttack, 0, kA20), 9);

    // healAmt 16, UNCHANGED at A2, 20 from A17.
    EXPECT_EQ(step_amount(d, r::kHealerMoveHeal, 0, 0), 16);
    EXPECT_EQ(step_amount(d, r::kHealerMoveHeal, 0, 2), 16)
        << "healAmt does NOT move at A2 (Healer.java:73)";
    EXPECT_EQ(step_amount(d, r::kHealerMoveHeal, 0, 16), 16);
    EXPECT_EQ(step_amount(d, r::kHealerMoveHeal, 0, 17), 20);
    EXPECT_EQ(step_amount(d, r::kHealerMoveHeal, 0, kA20), 20);

    // strAmt is the one that moves at BOTH boundaries: 2 / 3 / 4.
    EXPECT_EQ(step_amount(d, r::kHealerMoveBuff, 0, 0), 2);
    EXPECT_EQ(step_amount(d, r::kHealerMoveBuff, 0, 2), 3);
    EXPECT_EQ(step_amount(d, r::kHealerMoveBuff, 0, 16), 3);
    EXPECT_EQ(step_amount(d, r::kHealerMoveBuff, 0, 17), 4);

    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::NORMAL);
    EXPECT_EQ(d.roll_count, 0);
}

// Moves 2 and 3 BOTH telegraph Intent.BUFF (Healer.java:161,165,179) -- the
// first intent collision in the roster, so intent alone cannot identify a move.
TEST(CityNormalsII, HealerHealMoveTelegraphsBuffIntentNotDefend) {
    const auto& d = r::kHealer;
    EXPECT_EQ(d.move(r::kHealerMoveHeal)->intent, r::MonsterIntent::BUFF);
    EXPECT_EQ(d.move(r::kHealerMoveBuff)->intent, r::MonsterIntent::BUFF);
    EXPECT_NE(d.move(r::kHealerMoveHeal)->intent, r::MonsterIntent::DEFEND);
    EXPECT_EQ(d.move(r::kHealerMoveAttack)->intent,
              r::MonsterIntent::ATTACK_DEBUFF);
}

// ============================================================================
// 2. The Mugger's machine and its SEEDED draws
// ============================================================================

// The Mugger's talk gate is `slashCount == 1` (Mugger.java:91) where the
// Looter's is `slashCount == 0` (Looter.java:92). Java's && short-circuits, so
// the 0.6 coin is DRAWN on exactly one Mug -- the second here, the first there.
// Draw ACCOUNTING is the assertion: the TalkAction itself is presentation.
TEST(CityNormalsII, MuggerTalkGateFiresOnTheSecondMugNotTheFirst) {
    CombatState s = MakeSeeded(11);
    mugger_init(s, 0);
    const int32_t after_init = s.ai_rng.counter;
    ASSERT_EQ(s.monsters[0].move_history[0], r::kMuggerMoveMug);

    // FIRST Mug: playSfx' random(2) ONLY -- the gate reads slashCount == 1 and
    // this is the count-0 turn, so no coin is drawn.
    mugger_take_turn(s, 0);
    EXPECT_EQ(s.ai_rng.counter, after_init + 1)
        << "first Mug: playSfx only, no talk coin";
    EXPECT_EQ(mugger_steal_count(s.monsters[0]), 1);
    drain(s);
    // The queued SetMoveAction re-telegraphed Mug.
    ASSERT_EQ(s.monsters[0].move_history[0], r::kMuggerMoveMug);

    // SECOND Mug: playSfx + the 0.6 talk coin + the 0.5 Smoke-Bomb coin (the
    // count reaches 2 on this turn) == THREE draws.
    const int32_t before_second = s.ai_rng.counter;
    mugger_take_turn(s, 0);
    EXPECT_EQ(s.ai_rng.counter, before_second + 3)
        << "second Mug: playSfx + talk gate + the slashCount==2 coin";
    EXPECT_EQ(mugger_steal_count(s.monsters[0]), 2);

    // The LOOTER's gate is the mirror image: its first Mug draws the coin and
    // playSfx draws nothing at all (unseeded MathUtils).
    CombatState l = MakeSeeded(11);
    looter_init(l, 0);
    const int32_t l_after_init = l.ai_rng.counter;
    looter_take_turn(l, 0);
    EXPECT_EQ(l.ai_rng.counter, l_after_init + 1)
        << "the Looter's FIRST Mug draws the talk coin and nothing else";
}

// playSfx is the Mugger's sharpest divergence from the Looter: aiRng.random(2)
// (Mugger.java:139) against MathUtils (Looter.java:137-143). Pinned per attack
// move, both of them.
TEST(CityNormalsII, MuggerPlaySfxDrawsAiRngUnlikeTheLooter) {
    // BIGSWIPE: playSfx and nothing else -- no randomBoolean anywhere in the case.
    CombatState s = MakeSeeded(5);
    mugger_init(s, 0);
    telegraph(s, 0, r::kMuggerMoveBigswipe, MonsterIntent::ATTACK);
    const int32_t before = s.ai_rng.counter;
    mugger_take_turn(s, 0);
    EXPECT_EQ(s.ai_rng.counter, before + 1) << "Big Swipe: exactly one sfx draw";
    EXPECT_EQ(s.monsters[0].move_history[0], r::kMuggerMoveSmokeBomb)
        << "Big Swipe telegraphs Smoke Bomb SYNCHRONOUSLY";

    // SMOKE_BOMB and ESCAPE draw NOTHING: neither has a playSfx call.
    const int32_t before_bomb = s.ai_rng.counter;
    drain(s);
    mugger_take_turn(s, 0);
    EXPECT_EQ(s.ai_rng.counter, before_bomb) << "Smoke Bomb spends no draw";
    drain(s);
    ASSERT_EQ(s.monsters[0].move_history[0], r::kMuggerMoveEscape);
    mugger_take_turn(s, 0);
    EXPECT_EQ(s.ai_rng.counter, before_bomb) << "Escape spends no draw";
    EXPECT_NE(s.flags & kCombatFlagMugged, 0u)
        << "room.mugged is set SYNCHRONOUSLY (Mugger.java:129)";
    drain(s);
    EXPECT_TRUE(monster_escaped(s.monsters[0]));

    // The equivalent Looter moves spend nothing either -- but for a DIFFERENT
    // reason (its sounds are unseeded), which is why the Mug comparison above
    // is the one that separates them.
    CombatState l = MakeSeeded(5);
    looter_init(l, 0);
    telegraph(l, 0, r::kLooterMoveLunge, MonsterIntent::ATTACK);
    const int32_t l_before = l.ai_rng.counter;
    looter_take_turn(l, 0);
    EXPECT_EQ(l.ai_rng.counter, l_before)
        << "the Looter's Lunge draws NOTHING -- its playSfx is MathUtils";
}

// die() is the other seeded surface, and the reason the batch needed a
// MonsterDieFn slot at all. Driven through the REAL death edge (a lethal
// op_damage), not by calling the body, so the dispatch site is what is tested.
TEST(CityNormalsII, MuggerDeathConsumesOneAiRngDraw) {
    CombatState s = MakeSeeded(21);
    mugger_init(s, 0);
    s.monsters[0].hp = 5;
    const int32_t before = s.ai_rng.counter;
    player_attacks(s, 0, 5);
    ASSERT_EQ(s.monsters[0].hp, 0);
    EXPECT_EQ(s.ai_rng.counter, before + 1)
        << "playDeathSfx' aiRng.random(2) (Mugger.java:148)";

    // A SECOND hit on the already-dead record must not re-fire it: the death
    // edge is `old_hp > 0 && new_hp == 0`.
    const int32_t after = s.ai_rng.counter;
    player_attacks(s, 0, 5);
    EXPECT_EQ(s.ai_rng.counter, after) << "the death edge fires once";
}

TEST(CityNormalsII, LooterDeathConsumesNone) {
    CombatState s = MakeSeeded(21);
    looter_init(s, 0);
    s.monsters[0].hp = 5;
    const int32_t before = s.ai_rng.counter;
    player_attacks(s, 0, 5);
    ASSERT_EQ(s.monsters[0].hp, 0);
    EXPECT_EQ(s.ai_rng.counter, before)
        << "Looter.playDeathSfx rolls MathUtils, not aiRng (Looter.java:151)";
    EXPECT_EQ(monster_die_fn(MonsterId::LOOTER), nullptr);
    EXPECT_NE(monster_die_fn(MonsterId::MUGGER), nullptr);
}

// The whole machine, end to end: Mug, Mug, [Smoke Bomb | Big Swipe -> Smoke
// Bomb], Escape. Both branches of the 0.5 coin are exercised by sweeping seeds.
TEST(CityNormalsII, MuggerReachesEscapeOnEveryCoinBranch) {
    bool saw_bomb_branch = false;
    bool saw_swipe_branch = false;
    for (int64_t seed = 1; seed <= 24; ++seed) {
        CombatState s = MakeSeeded(seed);
        mugger_init(s, 0);
        std::vector<uint8_t> moves;
        for (int turn = 0; turn < 6 && !monster_escaped(s.monsters[0]); ++turn) {
            moves.push_back(s.monsters[0].move_history[0]);
            mugger_take_turn(s, 0);
            drain(s);
        }
        ASSERT_TRUE(monster_escaped(s.monsters[0])) << seed;
        ASSERT_GE(moves.size(), 4u);
        EXPECT_EQ(moves[0], r::kMuggerMoveMug);
        EXPECT_EQ(moves[1], r::kMuggerMoveMug);
        if (moves[2] == r::kMuggerMoveSmokeBomb) {
            saw_bomb_branch = true;
            EXPECT_EQ(moves[3], r::kMuggerMoveEscape);
            EXPECT_EQ(mugger_steal_count(s.monsters[0]), 2);
        } else {
            saw_swipe_branch = true;
            EXPECT_EQ(moves[2], r::kMuggerMoveBigswipe);
            EXPECT_EQ(moves[3], r::kMuggerMoveSmokeBomb);
            EXPECT_EQ(mugger_steal_count(s.monsters[0]), 3);
        }
    }
    EXPECT_TRUE(saw_bomb_branch);
    EXPECT_TRUE(saw_swipe_branch);
}

// ============================================================================
// 3. The stolen-gold settlement -- FAITHFUL PER-STEAL CLAMP (RED-first case)
// ============================================================================

RunController MakeThiefRun(int32_t gold, uint8_t looter_steals,
                           uint8_t mugger_steals) {
    RunController rc{};
    rc.run.gold = gold;
    rc.combat.monster_count = 2;
    rc.combat.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::LOOTER);
    rc.combat.monsters[0].hp = 40;
    rc.combat.monsters[0].max_hp = 40;
    rc.combat.monsters[0].pad0 = looter_steals;
    rc.combat.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::MUGGER);
    rc.combat.monsters[1].hp = 50;
    rc.combat.monsters[1].max_hp = 50;
    rc.combat.monsters[1].pad0 = mugger_steals;
    return rc;
}

// THE CASE THE OLD SUM-THEN-CLAMP GOT WRONG. Purse 30, two thieves at 20 each,
// one steal apiece. The game's per-steal clamp gives the Looter (slot 0, and so
// first in the turn queue) 20 and the Mugger 10. Which of them dies therefore
// decides what comes back -- 20 or 10 -- where sum-then-clamp
// (min(40, 30) = 30, then min(dead_unclamped, 30)) returned 20 either way.
//
// RED-FIRST EVIDENCE: with the previous body restored, the second EXPECT below
// fails with `rc.run.gold == 0, returned 20` instead of 10.
TEST(CityNormalsII, TwoThievesSettlesBothThievesGoldAgainstOnePurse) {
    // (a) The FIRST thief dies: it got the full 20.
    {
        RunController rc = MakeThiefRun(30, 1, 1);
        rc.combat.monsters[0].hp = 0;  // the Looter is killed
        const int32_t back = settle_stolen_gold(rc);
        EXPECT_EQ(rc.run.gold, 0) << "30 taken in total, purse emptied";
        EXPECT_EQ(back, 20) << "the Looter stole first, so it holds 20";
    }
    // (b) The SECOND thief dies: it only ever got the REMAINDER.
    {
        RunController rc = MakeThiefRun(30, 1, 1);
        rc.combat.monsters[1].hp = 0;  // the Mugger is killed
        const int32_t back = settle_stolen_gold(rc);
        EXPECT_EQ(rc.run.gold, 0) << "the total deducted is unchanged";
        EXPECT_EQ(back, 10)
            << "the Mugger stole SECOND against a 10-gold remainder; "
               "sum-then-clamp returned 20 here";
    }
    // (c) Both die: everything comes back.
    {
        RunController rc = MakeThiefRun(30, 1, 1);
        rc.combat.monsters[0].hp = 0;
        rc.combat.monsters[1].hp = 0;
        EXPECT_EQ(settle_stolen_gold(rc), 30);
        EXPECT_EQ(rc.run.gold, 0);
    }
    // (d) Neither dies (both escaped): the gold is gone and nothing returns.
    {
        RunController rc = MakeThiefRun(30, 1, 1);
        EXPECT_EQ(settle_stolen_gold(rc), 0);
        EXPECT_EQ(rc.run.gold, 0);
    }
}

// The steals INTERLEAVE by round, not by thief: round 1 is Looter-then-Mugger,
// round 2 the same. With a purse of 50 and two steals each, the Looter takes
// 20 + 10 and the Mugger 20 + 0.
TEST(CityNormalsII, TwoThievesStealsInterleaveByRound) {
    RunController rc = MakeThiefRun(50, 2, 2);
    rc.combat.monsters[0].hp = 0;  // only the Looter dies
    EXPECT_EQ(settle_stolen_gold(rc), 30) << "20 in round 1, 10 in round 2";
    EXPECT_EQ(rc.run.gold, 0);

    RunController rc2 = MakeThiefRun(50, 2, 2);
    rc2.combat.monsters[1].hp = 0;  // only the Mugger dies
    EXPECT_EQ(settle_stolen_gold(rc2), 20) << "20 in round 1, nothing left after";
    EXPECT_EQ(rc2.run.gold, 0);
}

// A purse that COVERS both thieves is the uninteresting case, and it must stay
// uninteresting: every steal is paid in full and the two models agree.
TEST(CityNormalsII, TwoThievesWithAmplePurseAreUnaffectedByTheClamp) {
    RunController rc = MakeThiefRun(500, 3, 2);
    rc.combat.monsters[1].hp = 0;
    EXPECT_EQ(settle_stolen_gold(rc), 40) << "the Mugger's two full steals";
    EXPECT_EQ(rc.run.gold, 500 - 100);
}

// The SOLO-thief behaviour every Act-1 test depends on is unchanged: one thief
// against a short purse still yields min(count * goldAmt, gold).
TEST(CityNormalsII, SoloThiefSettlementIsUnchanged) {
    RunController rc{};
    rc.run.gold = 25;
    rc.combat.monster_count = 1;
    rc.combat.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::LOOTER);
    rc.combat.monsters[0].hp = 0;
    rc.combat.monsters[0].max_hp = 40;
    rc.combat.monsters[0].pad0 = 3;  // 60 requested against 25
    EXPECT_EQ(settle_stolen_gold(rc), 25);
    EXPECT_EQ(rc.run.gold, 0);
}

// A group with no thief at all must not touch the purse.
TEST(CityNormalsII, SettlementIgnoresNonThieves) {
    RunController rc{};
    rc.run.gold = 99;
    rc.combat.monster_count = 2;
    rc.combat.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::HEALER);
    rc.combat.monsters[0].pad0 = 3;  // scratch that means something else entirely
    rc.combat.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::CENTURION);
    rc.combat.monsters[1].pad0 = 2;
    EXPECT_EQ(settle_stolen_gold(rc), 0);
    EXPECT_EQ(rc.run.gold, 99) << "pad0 is TYPE-SCOPED -- only thieves' pad0 is a "
                                  "steal count";
    EXPECT_FALSE(is_thief(rc.combat.monsters[0]));
    EXPECT_EQ(thief_stolen_gold(rc.combat.monsters[0]), 0);
}

// ============================================================================
// 4. The Snake Plant -- lastMoveBefore, three hits, and Malleable
// ============================================================================

// The A17 arm widens the num >= 65 guard from lastMove(SPORES) to
// lastMove(SPORES) || lastMoveBefore(SPORES) (SnakePlant.java:131 vs :141).
// The history "SPORES then CHOMPY" separates them: at A17 Spores is still too
// recent, below A17 it is not.
TEST(CityNormalsII, SnakePlantA17AlsoRefusesSporesFromTwoMovesBack) {
    auto decide = [](int32_t asc) {
        MonsterState m{};
        m.monster_id = static_cast<uint16_t>(MonsterId::SNAKE_PLANT);
        // Ring: [0] == CHOMPY (most recent), [1] == SPORES.
        set_monster_move(m, r::kSnakePlantMoveSpores, MonsterIntent::STRONG_DEBUFF);
        set_monster_move(m, r::kSnakePlantMoveChompyChomps, MonsterIntent::ATTACK);
        snake_plant_decide_move(m, 80, asc);  // num >= 65
        return m.move_history[0];
    };
    EXPECT_EQ(decide(17), r::kSnakePlantMoveChompyChomps)
        << "A17 reads lastMoveBefore too, so Spores is refused";
    EXPECT_EQ(decide(kA20), r::kSnakePlantMoveChompyChomps);
    // THE NEGATIVE CONTROL: one ascension lower, the same history takes Spores.
    EXPECT_EQ(decide(16), r::kSnakePlantMoveSpores)
        << "below A17 only lastMove is consulted";
    EXPECT_EQ(decide(0), r::kSnakePlantMoveSpores);
}

// The num < 65 arm is shared VERBATIM by both ascension arms, and the num >= 65
// arm agrees whenever Spores was the IMMEDIATELY preceding move.
TEST(CityNormalsII, SnakePlantSharedArmsAgreeAcrossAscension) {
    for (int32_t asc : {0, 2, 7, 16, 17, kA20}) {
        MonsterState m{};
        // Two Chompies in a row -> Spores, on the num < 65 arm, at every asc.
        set_monster_move(m, r::kSnakePlantMoveChompyChomps, MonsterIntent::ATTACK);
        set_monster_move(m, r::kSnakePlantMoveChompyChomps, MonsterIntent::ATTACK);
        snake_plant_decide_move(m, 64, asc);
        EXPECT_EQ(m.move_history[0], r::kSnakePlantMoveSpores) << asc;

        MonsterState n{};
        // Spores as the MOST RECENT move -> Chompy on the num >= 65 arm, at
        // every asc (this is the disjunct the two arms share).
        set_monster_move(n, r::kSnakePlantMoveSpores, MonsterIntent::STRONG_DEBUFF);
        snake_plant_decide_move(n, 65, asc);
        EXPECT_EQ(n.move_history[0], r::kSnakePlantMoveChompyChomps) << asc;

        MonsterState o{};
        // An EMPTY history on the num >= 65 arm takes Spores at every asc.
        snake_plant_decide_move(o, 99, asc);
        EXPECT_EQ(o.move_history[0], r::kSnakePlantMoveSpores) << asc;
    }
}

// numBlows == 3 separate DamageActions (SnakePlant.java:99-104), so block, a
// halving power and a lethal clamp all apply per hit -- and Malleable escalates
// per hit when the plant is on the receiving end of the same shape.
TEST(CityNormalsII, SnakePlantChompyIsThreeSeparateHits) {
    EXPECT_EQ(step_count(r::kSnakePlant, r::kSnakePlantMoveChompyChomps), 3);

    CombatState s = MakeSeeded(9);
    snake_plant_init(s, 0);
    telegraph(s, 0, r::kSnakePlantMoveChompyChomps, MonsterIntent::ATTACK);
    snake_plant_take_turn(s, 0);
    // Three DAMAGE items, then the trailing ROLL_MOVE.
    ASSERT_EQ(s.action_count, 4);
    for (uint8_t i = 0; i < 3; ++i) {
        EXPECT_EQ(queued(s, i).opcode, static_cast<uint16_t>(Opcode::DAMAGE))
            << int(i);
        EXPECT_EQ(queued(s, i).amount, 8) << "rainBlowsDmg at A20";
        EXPECT_EQ(queued(s, i).tgt, kActorPlayer);
    }
    EXPECT_EQ(queued(s, 3).opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE));

    // Three hits against 8 block: the FIRST is absorbed, the other two are not
    // -- which one lumped 24-damage hit could never reproduce.
    s.player_block = 8;
    const int16_t hp_before = s.player_hp;
    drain(s);
    EXPECT_EQ(s.player_hp, hp_before - 16);
    EXPECT_EQ(s.player_block, 0);
}

// SPORES applies Frail BEFORE Weak (SnakePlant.java:107-110), both at 2 and both
// isSourceMonster (so neither decrements on the turn it lands).
TEST(CityNormalsII, SnakePlantSporesAppliesFrailBeforeWeak) {
    CombatState s = MakeSeeded(9);
    snake_plant_init(s, 0);
    telegraph(s, 0, r::kSnakePlantMoveSpores, MonsterIntent::STRONG_DEBUFF);
    snake_plant_take_turn(s, 0);
    ASSERT_GE(s.action_count, 2);
    EXPECT_EQ(queued(s, 0).flags, make_apply_power_flags(PowerId::FRAIL));
    EXPECT_EQ(queued(s, 1).flags, make_apply_power_flags(PowerId::WEAK));
    drain(s);
    EXPECT_EQ(player_power(s, PowerId::FRAIL), 2);
    EXPECT_EQ(player_power(s, PowerId::WEAK), 2);
}

// The Malleable opener: amount 3 AND basePower 3 (PowerSlot.counter), from the
// 1-arg ctor.
TEST(CityNormalsII, SnakePlantOpensWithMalleableThree) {
    CombatState s = MakeSeeded(9);
    snake_plant_init(s, 0);
    const int32_t hp_draws = s.monster_hp_rng.counter;
    const int32_t ai_draws = s.ai_rng.counter;
    use_pre_battle_actions(s);
    EXPECT_EQ(s.monster_hp_rng.counter, hp_draws) << "no pre-battle roll";
    EXPECT_EQ(s.ai_rng.counter, ai_draws);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::MALLEABLE), 3);
    EXPECT_EQ(monster_power_counter(s, 0, PowerId::MALLEABLE), 3)
        << "basePower rides in PowerSlot.counter";
}

// ============================================================================
// 5. Malleable -- the escalate/reset cycle and its three refusals
// ============================================================================

TEST(CityNormalsII, SnakePlantMalleableGainsBlockAndEscalatesThenResetsAtEndOfTurn) {
    CombatState s = MakeState();
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::SNAKE_PLANT);
    s.monsters[0].hp = 80;
    s.monsters[0].max_hp = 80;
    give_monster_power(s, 0, PowerId::MALLEABLE, 3, /*counter=*/3);

    // Hit 1: block 3 queued, amount escalates to 4 SYNCHRONOUSLY.
    player_attacks(s, 0, 5);
    EXPECT_EQ(monster_power(s, 0, PowerId::MALLEABLE), 4)
        << "++amount is synchronous (MalleablePower.java:72)";
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::BLOCK));
    EXPECT_EQ(queued(s, 0).amount, 3) << "the block queued is the PRE-escalation "
                                         "amount";
    // Hit 2 BEFORE the queue drains -- exactly what a multi-hit attack does.
    player_attacks(s, 0, 5);
    EXPECT_EQ(monster_power(s, 0, PowerId::MALLEABLE), 5);
    ASSERT_EQ(s.action_count, 2);
    EXPECT_EQ(queued(s, 1).amount, 4) << "3 then 4, not 3 then 3";
    drain(s);
    EXPECT_EQ(s.monsters[0].block, 7);

    // End of TURN resets the amount to basePower (the counter), not to zero.
    dispatch_at_end_of_round(s);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::MALLEABLE), 3)
        << "atEndOfTurn: amount = basePower (MalleablePower.java:44-51)";
    EXPECT_EQ(monster_power_counter(s, 0, PowerId::MALLEABLE), 3)
        << "basePower itself never moves without a re-application";
}

// `damageAmount < owner.currentHealth` is STRICT (MalleablePower.java:64): an
// EXACTLY-lethal hit and an overkill both trigger nothing at all.
TEST(CityNormalsII, MalleableDoesNotTriggerOnLethalDamage) {
    auto hit_for = [](int32_t hp, int32_t dmg) {
        CombatState s = MakeState();
        s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::SNAKE_PLANT);
        s.monsters[0].hp = static_cast<int16_t>(hp);
        s.monsters[0].max_hp = static_cast<int16_t>(hp);
        give_monster_power(s, 0, PowerId::MALLEABLE, 3, 3);
        player_attacks(s, 0, dmg);
        return s;
    };
    // Survivable: fires.
    CombatState live = hit_for(10, 9);
    EXPECT_EQ(monster_power(live, 0, PowerId::MALLEABLE), 4);
    EXPECT_EQ(live.action_count, 1);
    // EXACTLY lethal: does NOT fire (this is the `<` rather than `<=`).
    CombatState exact = hit_for(10, 10);
    EXPECT_EQ(monster_power(exact, 0, PowerId::MALLEABLE), 3)
        << "damageAmount == currentHealth fails the STRICT test";
    EXPECT_EQ(exact.action_count, 0);
    // Overkill: also does not fire.
    CombatState over = hit_for(10, 40);
    EXPECT_EQ(monster_power(over, 0, PowerId::MALLEABLE), 3);
    EXPECT_EQ(over.action_count, 0);
}

// The type guard (`info.type == NORMAL`) and the `damageAmount > 0` guard: a
// THORNS reflect, an HP_LOSS and a fully-blocked hit all leave it alone.
TEST(CityNormalsII, MalleableIgnoresThornsAndHpLoss) {
    auto fresh = []() {
        CombatState s = MakeState();
        s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::SNAKE_PLANT);
        s.monsters[0].hp = 80;
        s.monsters[0].max_hp = 80;
        give_monster_power(s, 0, PowerId::MALLEABLE, 3, 3);
        return s;
    };
    CombatState thorns = fresh();
    player_attacks(thorns, 0, 5, DamageType::THORNS);
    EXPECT_EQ(monster_power(thorns, 0, PowerId::MALLEABLE), 3);
    EXPECT_EQ(thorns.action_count, 0);

    CombatState loss = fresh();
    {
        ActionQueueItem it{};
        it.opcode = static_cast<uint16_t>(Opcode::LOSE_HP);
        it.src = 0;
        it.tgt = 0;
        it.amount = 5;
        execute_opcode(loss, it);
    }
    EXPECT_EQ(loss.monsters[0].hp, 75);
    EXPECT_EQ(monster_power(loss, 0, PowerId::MALLEABLE), 3)
        << "LoseHPAction is HP_LOSS, which the type guard excludes";
    EXPECT_EQ(loss.action_count, 0);

    CombatState blocked = fresh();
    blocked.monsters[0].block = 20;
    player_attacks(blocked, 0, 5);
    EXPECT_EQ(blocked.monsters[0].hp, 80);
    EXPECT_EQ(monster_power(blocked, 0, PowerId::MALLEABLE), 3)
        << "a fully-blocked hit fails `damageAmount > 0`";
    EXPECT_EQ(blocked.action_count, 0);
}

// stackPower moves BOTH numbers (MalleablePower.java:79-82), unlike Flight's
// frozen storedAmount. Unreachable in S1/S2 and pinned anyway.
TEST(CityNormalsII, MalleableStackingRaisesBasePowerToo) {
    CombatState s = MakeState();
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::SNAKE_PLANT);
    s.monsters[0].hp = 80;
    s.monsters[0].max_hp = 80;
    apply_power(s, 0, 0, PowerId::MALLEABLE, 3);
    EXPECT_EQ(monster_power(s, 0, PowerId::MALLEABLE), 3);
    EXPECT_EQ(monster_power_counter(s, 0, PowerId::MALLEABLE), 3);
    apply_power(s, 0, 0, PowerId::MALLEABLE, 2);
    EXPECT_EQ(monster_power(s, 0, PowerId::MALLEABLE), 5);
    EXPECT_EQ(monster_power_counter(s, 0, PowerId::MALLEABLE), 5)
        << "basePower += stackAmount -- the reset target moves too";
}

// ============================================================================
// 6. The Snecko
// ============================================================================

// firstTurn forces GLARE and returns WITHOUT reading num (Snecko.java:142-146),
// so the opener is GLARE at every seed -- and the discarded init draw still
// advances the shared stream.
TEST(CityNormalsII, SneckoOpensWithGlareRegardlessOfRoll) {
    for (int64_t seed = 1; seed <= 40; ++seed) {
        CombatState s = MakeSeeded(seed);
        snecko_init(s, 0);
        EXPECT_EQ(s.monsters[0].move_history[0], r::kSneckoMoveGlare) << seed;
        EXPECT_EQ(s.monsters[0].intent,
                  static_cast<uint8_t>(MonsterIntent::STRONG_DEBUFF));
        EXPECT_EQ(s.ai_rng.counter, 1) << "the draw happens and is discarded";
        EXPECT_EQ(s.monster_hp_rng.counter, 1);
    }
}

// The post-opener tree has NO ascension branch: num < 40 -> TAIL, else
// lastTwoMoves(BITE) ? TAIL : BITE (Snecko.java:147-157).
TEST(CityNormalsII, SneckoRollTreeIsHistoryGuardedAndAscensionFree) {
    CombatState s = MakeSeeded(2);
    snecko_init(s, 0);
    // Two Bites in the ring, then a high roll -> TAIL.
    telegraph(s, 0, r::kSneckoMoveBite, MonsterIntent::ATTACK);
    telegraph(s, 0, r::kSneckoMoveBite, MonsterIntent::ATTACK);
    // Find seeds giving num >= 40 and num < 40 respectively.
    bool tested_high = false;
    bool tested_low = false;
    for (int64_t seed = 1; seed <= 60 && !(tested_high && tested_low); ++seed) {
        RngStream probe = from_seed(seed);
        const int32_t num = random(probe, 99);
        CombatState t = MakeSeeded(seed);
        t.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::SNECKO);
        t.monsters[0].hp = 120;
        t.monsters[0].max_hp = 120;
        telegraph(t, 0, r::kSneckoMoveBite, MonsterIntent::ATTACK);
        telegraph(t, 0, r::kSneckoMoveBite, MonsterIntent::ATTACK);
        snecko_roll_move(t, 0);
        EXPECT_EQ(t.monsters[0].move_history[0], r::kSneckoMoveTail)
            << "two Bites force Tail on both arms, seed " << seed;
        if (num < 40) {
            tested_low = true;
        } else {
            tested_high = true;
        }
    }
    EXPECT_TRUE(tested_low);
    EXPECT_TRUE(tested_high);

    // With NO Bite history, a high roll takes BITE.
    for (int64_t seed = 1; seed <= 60; ++seed) {
        RngStream probe = from_seed(seed);
        if (random(probe, 99) < 40) {
            continue;
        }
        CombatState t = MakeSeeded(seed);
        t.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::SNECKO);
        snecko_roll_move(t, 0);
        EXPECT_EQ(t.monsters[0].move_history[0], r::kSneckoMoveBite);
        break;
    }
}

// TAIL's step ORDER is damage -> Weak -> Vulnerable, with the Weak present only
// at A17+ (Snecko.java:108-118). The A20 engine queues all three, in order.
TEST(CityNormalsII, SneckoTailAppliesWeakBeforeVulnerableAtA17) {
    ASSERT_TRUE(snecko_tail_applies_weak(kA20));
    CombatState s = MakeSeeded(6);
    snecko_init(s, 0);
    telegraph(s, 0, r::kSneckoMoveTail, MonsterIntent::ATTACK_DEBUFF);
    snecko_take_turn(s, 0);
    ASSERT_EQ(s.action_count, 4) << "damage, Weak, Vulnerable, then the roll";
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::DAMAGE));
    EXPECT_EQ(queued(s, 0).amount, 10) << "tailDmg at A2+";
    EXPECT_EQ(queued(s, 1).flags, make_apply_power_flags(PowerId::WEAK));
    EXPECT_EQ(queued(s, 1).amount, 2);
    EXPECT_EQ(queued(s, 2).flags, make_apply_power_flags(PowerId::VULNERABLE));
    EXPECT_EQ(queued(s, 2).amount, 2);
    EXPECT_EQ(queued(s, 3).opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE));

    // BITE and GLARE go through the whole-program helper unchanged: one step
    // plus the roll.
    CombatState b = MakeSeeded(6);
    snecko_init(b, 0);
    telegraph(b, 0, r::kSneckoMoveBite, MonsterIntent::ATTACK);
    snecko_take_turn(b, 0);
    ASSERT_EQ(b.action_count, 2);
    EXPECT_EQ(queued(b, 0).amount, 18) << "biteDmg at A2+";
}

// GLARE applies Confusion with AbstractPower's -1, and the S1 native onCardDraw
// body is unchanged by it: one card_random_rng.random(3) per drawn card whose
// cost >= 0, none for a cost < 0, and the draw happens even when the roll
// changes nothing.
TEST(CityNormalsII, SneckoConfusionRandomisesDrawnCardCosts) {
    CombatState s = MakeSeeded(6);
    snecko_init(s, 0);
    ASSERT_EQ(s.monsters[0].move_history[0], r::kSneckoMoveGlare);
    snecko_take_turn(s, 0);
    drain(s);
    ASSERT_TRUE(player_has(s, PowerId::CONFUSION));
    EXPECT_EQ(player_power(s, PowerId::CONFUSION), -1)
        << "ConfusionPower carries AbstractPower's field initializer (-1); the "
           "oracle corpus act1_a20_50 reports exactly that";

    // Build a draw pile: STRIKE (cost 1) and WOUND (unplayable, cost < 0).
    auto fresh_card = [&](CardId id) {
        CardPoolIndex pi = 0;
        while (pi < kCardPoolCap &&
               s.card_pool[pi].card_id != static_cast<uint16_t>(CardId::NONE)) {
            ++pi;
        }
        const CardDef* def = card_def(id);
        s.card_pool[pi].card_id = static_cast<uint16_t>(id);
        s.card_pool[pi].cost_now = card_cost(*def, 0);
        s.card_pool[pi].flags = card_flags(*def, 0);
        s.draw[s.draw_count++] = pi;
        return pi;
    };
    const CardPoolIndex wound = fresh_card(CardId::WOUND);
    fresh_card(CardId::STRIKE);  // drawn first (draw[] tail is the top)

    s.card_random_rng = from_seed(7);
    RngStream ref = from_seed(7);
    const int32_t expect_first = random(ref, 3);

    ActionQueueItem d{};
    d.opcode = static_cast<uint16_t>(Opcode::DRAW);
    d.src = kActorPlayer;
    d.tgt = kActorPlayer;
    d.amount = 2;
    execute_opcode(s, d);

    ASSERT_EQ(s.hand_count, 2);
    EXPECT_EQ(s.card_pool[s.hand[0]].cost_now, expect_first);
    EXPECT_EQ(s.card_pool[wound].cost_now, 0) << "cost < 0 -> untouched";
    EXPECT_EQ(s.card_random_rng.counter, 1)
        << "exactly one roll for the one cost >= 0 card";
}

// A second Confusion application must NOT move the -1
// (AbstractPower.stackPower:152-158) -- reachable when Snecko Eye and a Snecko
// are in the same combat.
TEST(CityNormalsII, ConfusionDoesNotStackOffItsMinusOne) {
    CombatState s = MakeState();
    apply_power(s, kActorPlayer, kActorPlayer, PowerId::CONFUSION,
                kConfusionAppliedAmount);
    ASSERT_EQ(player_power(s, PowerId::CONFUSION), -1);
    apply_power(s, kActorPlayer, kActorPlayer, PowerId::CONFUSION,
                kConfusionAppliedAmount);
    EXPECT_EQ(player_power(s, PowerId::CONFUSION), -1)
        << "stackPower returns immediately on amount == -1";
    EXPECT_EQ(s.player_power_count, 1);
}

// ============================================================================
// 7. The Centurion -- aliveCount, and the conditional ai_rng draw
// ============================================================================

// A Centurion with a living ally can PROTECT; alone it FURIES. The ally leaves
// the count in BOTH ways the Java's predicate allows: dying and escaping.
TEST(CityNormalsII, CenturionProtectsWhileHealerAliveAndFuriesAlone) {
    // Pick a seed whose roll is >= 65 so arm 1 is the one under test.
    int64_t seed = 0;
    for (int64_t c = 1; c < 200; ++c) {
        RngStream probe = from_seed(c);
        if (random(probe, 99) >= 65) {
            seed = c;
            break;
        }
    }
    ASSERT_NE(seed, 0);

    auto roll_with_ally = [&](bool ally_dead, bool ally_escaped) {
        CombatState s = MakeSeeded(seed, /*monsters=*/2);
        s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::CENTURION);
        s.monsters[0].hp = 80;
        s.monsters[0].max_hp = 80;
        s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::HEALER);
        s.monsters[1].hp = ally_dead ? 0 : 55;
        s.monsters[1].max_hp = 55;
        if (ally_escaped) {
            s.monsters[1].flags |= kMonsterFlagEscaped;
        }
        centurion_roll_move(s, 0);
        return s.monsters[0].move_history[0];
    };
    EXPECT_EQ(roll_with_ally(false, false), r::kCenturionMoveProtect)
        << "aliveCount 2 -> PROTECT";
    EXPECT_EQ(roll_with_ally(true, false), r::kCenturionMoveFury)
        << "a DEAD ally leaves aliveCount 1 -> FURY";
    EXPECT_EQ(roll_with_ally(false, true), r::kCenturionMoveFury)
        << "an ESCAPED ally leaves aliveCount 1 too (isEscaping, not just "
           "isDying)";

    // Solo from the start: same answer, and the count includes ITSELF (so a
    // solo Centurion is 1, never 0).
    CombatState solo = MakeSeeded(seed);
    solo.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::CENTURION);
    solo.monsters[0].hp = 80;
    solo.monsters[0].max_hp = 80;
    centurion_roll_move(solo, 0);
    EXPECT_EQ(solo.monsters[0].move_history[0], r::kCenturionMoveFury);
}

// The whole tree, arm by arm (Centurion.java:132-160). Arm 2 (SLASH) is the one
// a low roll or a repeated PROTECT/FURY history falls into.
TEST(CityNormalsII, CenturionMoveTreeArmsAreExact) {
    auto decide = [](int32_t num, uint8_t h0, uint8_t h1, bool with_ally) {
        CombatState s = MakeState(with_ally ? 2 : 1);
        s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::CENTURION);
        s.monsters[0].hp = 80;
        s.monsters[0].max_hp = 80;
        if (with_ally) {
            s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::HEALER);
            s.monsters[1].hp = 55;
            s.monsters[1].max_hp = 55;
        }
        if (h1 != 0) {
            telegraph(s, 0, h1, MonsterIntent::ATTACK);
        }
        if (h0 != 0) {
            telegraph(s, 0, h0, MonsterIntent::ATTACK);
        }
        // Drive the pure decision through the roll fn on a stream whose first
        // draw is `num` would need a seed search; instead assert the shape the
        // roll fn produces for the two reachable arms via history alone.
        s.ai_rng = from_seed(1);
        (void)num;
        centurion_roll_move(s, 0);
        return s.monsters[0].move_history[0];
    };
    // Two PROTECTs in a row block arm 1, and arm 2 then takes SLASH (its own
    // guard, !lastTwoMoves(SLASH), is satisfied).
    EXPECT_EQ(decide(99, r::kCenturionMoveProtect, r::kCenturionMoveProtect,
                     /*with_ally=*/true),
              r::kCenturionMoveSlash);
    // Two FURYs in a row: same.
    EXPECT_EQ(decide(99, r::kCenturionMoveFury, r::kCenturionMoveFury, true),
              r::kCenturionMoveSlash);
    // Two SLASHes in a row: arm 2 is blocked, so arm 3 decides -- with an ally,
    // PROTECT.
    EXPECT_EQ(decide(99, r::kCenturionMoveSlash, r::kCenturionMoveSlash, true),
              r::kCenturionMoveProtect);
    // ...and alone, FURY.
    EXPECT_EQ(decide(99, r::kCenturionMoveSlash, r::kCenturionMoveSlash, false),
              r::kCenturionMoveFury);
}

// GainBlockRandomMonsterAction spends its ai_rng draw ONLY when the valid list
// is non-empty (GainBlockRandomMonsterAction.java:36) -- the seed-level
// difference between a solo Centurion and a paired one.
TEST(CityNormalsII, CenturionProtectConsumesNoAiRngWhenNoValidAlly) {
    CombatState s = MakeSeeded(13);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::CENTURION);
    s.monsters[0].hp = 80;
    s.monsters[0].max_hp = 80;
    const int32_t before = s.ai_rng.counter;
    block_random_monster(s, 0, 20);
    EXPECT_EQ(s.ai_rng.counter, before) << "no valid ally -> ZERO draws";
    EXPECT_EQ(s.monsters[0].block, 20) << "the source blocks itself instead";
}

TEST(CityNormalsII, CenturionProtectDrawsOnceWithOneAlly) {
    CombatState s = MakeSeeded(13, /*monsters=*/2);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::CENTURION);
    s.monsters[0].hp = 80;
    s.monsters[0].max_hp = 80;
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::HEALER);
    s.monsters[1].hp = 55;
    s.monsters[1].max_hp = 55;
    const int32_t before = s.ai_rng.counter;
    block_random_monster(s, 0, 20);
    EXPECT_EQ(s.ai_rng.counter, before + 1) << "one valid ally still costs a draw";
    EXPECT_EQ(s.monsters[0].block, 0) << "the ONLY valid target is the ally";
    EXPECT_EQ(s.monsters[1].block, 20);

    // A DEAD ally is excluded, so the draw disappears again.
    CombatState d = MakeSeeded(13, /*monsters=*/2);
    d.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::CENTURION);
    d.monsters[0].hp = 80;
    d.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::HEALER);
    d.monsters[1].hp = 0;
    const int32_t d_before = d.ai_rng.counter;
    block_random_monster(d, 0, 20);
    EXPECT_EQ(d.ai_rng.counter, d_before);
    EXPECT_EQ(d.monsters[0].block, 20);
}

// The escape filter reads the TELEGRAPHED INTENT, not the escaped flag: an ally
// that has merely ANNOUNCED its exit is already skipped while still alive and
// present.
TEST(CityNormalsII, CenturionProtectSkipsAnAllyTelegraphingEscape) {
    CombatState s = MakeSeeded(13, /*monsters=*/2);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::CENTURION);
    s.monsters[0].hp = 80;
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::MUGGER);
    s.monsters[1].hp = 50;
    s.monsters[1].max_hp = 50;
    // Alive, unescaped, positive HP -- but telegraphing ESCAPE.
    telegraph(s, 1, r::kMuggerMoveEscape, MonsterIntent::ESCAPE);
    ASSERT_FALSE(monster_dead_or_escaped(s.monsters[1]));

    const int32_t before = s.ai_rng.counter;
    block_random_monster(s, 0, 20);
    EXPECT_EQ(s.ai_rng.counter, before)
        << "the announced escape empties the valid list, so no draw";
    EXPECT_EQ(s.monsters[0].block, 20);
    EXPECT_EQ(s.monsters[1].block, 0);

    // Re-telegraph an ordinary intent and the same ally becomes valid again.
    telegraph(s, 1, r::kMuggerMoveMug, MonsterIntent::ATTACK);
    s.monsters[0].block = 0;
    const int32_t before2 = s.ai_rng.counter;
    block_random_monster(s, 0, 20);
    EXPECT_EQ(s.ai_rng.counter, before2 + 1);
    EXPECT_EQ(s.monsters[1].block, 20);
}

// FURY is three separate DamageActions and stays three at A2 (furyHits is
// assigned 3 on BOTH sides of the branch, Centurion.java:66,70).
TEST(CityNormalsII, CenturionFuryHitsAreThreeSeparateDamageActions) {
    EXPECT_EQ(step_count(r::kCenturion, r::kCenturionMoveFury), 3);
    CombatState s = MakeSeeded(8);
    centurion_init(s, 0);
    telegraph(s, 0, r::kCenturionMoveFury, MonsterIntent::ATTACK);
    const int32_t before = s.ai_rng.counter;
    centurion_take_turn(s, 0);
    EXPECT_EQ(s.ai_rng.counter, before)
        << "playSfx is UNSEEDED MathUtils -- three calls, zero draws";
    ASSERT_EQ(s.action_count, 4);
    for (uint8_t i = 0; i < 3; ++i) {
        EXPECT_EQ(queued(s, i).opcode, static_cast<uint16_t>(Opcode::DAMAGE));
        EXPECT_EQ(queued(s, i).amount, 7) << "furyDmg at A2+";
    }
    EXPECT_EQ(queued(s, 3).opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE));
}

TEST(CityNormalsII, CenturionFuryHitCountIsUnchangedAtA2) {
    // The step COUNT is not a tiered value at all -- it is the row's shape --
    // so the assertion is that every tier resolves the SAME three amounts, and
    // that the Java's two assignments agree. A future "surely A2 adds a hit"
    // edit fails here.
    for (int32_t asc : {0, 1, 2, 7, 16, 17, kA20}) {
        EXPECT_EQ(step_count(r::kCenturion, r::kCenturionMoveFury), 3) << asc;
        const int32_t expect = asc >= 2 ? 7 : 6;
        for (uint8_t k = 0; k < 3; ++k) {
            EXPECT_EQ(step_amount(r::kCenturion, r::kCenturionMoveFury, k, asc),
                      expect)
                << "asc " << asc << " step " << int(k);
        }
    }
}

// ============================================================================
// 8. The Healer -- both ascension-switched gates, and the group fan-out
// ============================================================================

// Gate 1's threshold RISES at A17: > 20 rather than > 15 (Healer.java:161,165).
// A group missing exactly 18 HP separates them.
TEST(CityNormalsII, HealerHealThresholdIsTwentyAtA17AndFifteenBelow) {
    auto decide = [](int32_t missing, int32_t asc) {
        CombatState s = MakeState();
        s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::HEALER);
        s.monsters[0].max_hp = 56;
        s.monsters[0].hp = static_cast<int16_t>(56 - missing);
        healer_decide_move(s, 0, /*num=*/99, asc);
        return s.monsters[0].move_history[0];
    };
    // 18 missing: below A17 that is over the bar, at A17 it is not.
    EXPECT_EQ(decide(18, 16), r::kHealerMoveHeal);
    EXPECT_NE(decide(18, 17), r::kHealerMoveHeal)
        << "the A17 threshold is > 20, so 18 is not enough";
    EXPECT_NE(decide(18, kA20), r::kHealerMoveHeal);
    // 21 missing clears both bars.
    EXPECT_EQ(decide(21, 16), r::kHealerMoveHeal);
    EXPECT_EQ(decide(21, kA20), r::kHealerMoveHeal);
    // The bars are STRICT `>`: exactly 15 / exactly 20 do NOT heal.
    EXPECT_NE(decide(15, 0), r::kHealerMoveHeal);
    EXPECT_EQ(decide(16, 0), r::kHealerMoveHeal);
    EXPECT_NE(decide(20, kA20), r::kHealerMoveHeal);
    EXPECT_EQ(decide(21, kA20), r::kHealerMoveHeal);
    // ...and it is the GROUP's missing HP, not the Healer's own.
    {
        CombatState s = MakeState(2);
        s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::HEALER);
        s.monsters[0].hp = 56;
        s.monsters[0].max_hp = 56;
        s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::CENTURION);
        s.monsters[1].hp = 50;
        s.monsters[1].max_hp = 80;  // 30 missing, all of it on the ALLY
        healer_decide_move(s, 0, 99, kA20);
        EXPECT_EQ(s.monsters[0].move_history[0], r::kHealerMoveHeal);
        // A DEAD ally does not count toward it.
        CombatState d = s;
        d.monsters[0].move_history[0] = 0;
        d.monsters[0].move_history[1] = 0;
        d.monsters[1].hp = 0;
        healer_decide_move(d, 0, 99, kA20);
        EXPECT_NE(d.monsters[0].move_history[0], r::kHealerMoveHeal);
    }
}

// Gate 2 TIGHTENS at A17: !lastMove(ATTACK) rather than !lastTwoMoves(ATTACK)
// (Healer.java:172,176). A history of exactly ONE preceding ATTACK separates
// them, and it separates them the OTHER WAY round from gate 1.
TEST(CityNormalsII, HealerAttackGuardIsLastMoveAtA17AndLastTwoMovesBelow) {
    auto decide = [](int32_t asc) {
        CombatState s = MakeState();
        s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::HEALER);
        s.monsters[0].hp = 56;
        s.monsters[0].max_hp = 56;  // needToHeal 0 -- gate 1 cannot fire
        telegraph(s, 0, r::kHealerMoveBuff, MonsterIntent::BUFF);
        telegraph(s, 0, r::kHealerMoveAttack, MonsterIntent::ATTACK_DEBUFF);
        healer_decide_move(s, 0, /*num=*/99, asc);
        return s.monsters[0].move_history[0];
    };
    EXPECT_EQ(decide(16), r::kHealerMoveAttack)
        << "below A17 one preceding ATTACK does not block arm 2";
    EXPECT_NE(decide(17), r::kHealerMoveAttack)
        << "at A17 a single preceding ATTACK blocks it";
    EXPECT_EQ(decide(17), r::kHealerMoveBuff) << "it falls through to arm 3";
    EXPECT_EQ(decide(kA20), r::kHealerMoveBuff);

    // num < 40 blocks arm 2 at EVERY ascension.
    CombatState s = MakeState();
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::HEALER);
    s.monsters[0].hp = 56;
    s.monsters[0].max_hp = 56;
    healer_decide_move(s, 0, /*num=*/39, kA20);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kHealerMoveBuff);

    // Arm 4: two BUFFs in the ring block arm 3, so ATTACK is the fallback even
    // though arm 2 rejected it.
    CombatState f = MakeState();
    f.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::HEALER);
    f.monsters[0].hp = 56;
    f.monsters[0].max_hp = 56;
    telegraph(f, 0, r::kHealerMoveBuff, MonsterIntent::BUFF);
    telegraph(f, 0, r::kHealerMoveBuff, MonsterIntent::BUFF);
    healer_decide_move(f, 0, /*num=*/10, kA20);
    EXPECT_EQ(f.monsters[0].move_history[0], r::kHealerMoveAttack);
}

// HEAL and BUFF fan out over EVERY live member INCLUDING the Healer itself, in
// slot order, with liveness read at QUEUE time.
TEST(CityNormalsII, HealerHealsAndBuffsEveryLiveMonsterIncludingItself) {
    CombatState s = MakeState(2);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::CENTURION);
    s.monsters[0].hp = 40;
    s.monsters[0].max_hp = 80;
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::HEALER);
    s.monsters[1].hp = 30;
    s.monsters[1].max_hp = 56;

    telegraph(s, 1, r::kHealerMoveHeal, MonsterIntent::BUFF);
    healer_take_turn(s, 1);
    ASSERT_EQ(s.action_count, 3) << "two heals in SLOT order, then the roll";
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::HEAL));
    EXPECT_EQ(queued(s, 0).tgt, 0) << "the Centurion first -- group order";
    EXPECT_EQ(queued(s, 0).amount, 20) << "healAmt at A17+";
    EXPECT_EQ(queued(s, 1).tgt, 1) << "then the Healer itself";
    drain(s);
    EXPECT_EQ(s.monsters[0].hp, 60);
    EXPECT_EQ(s.monsters[1].hp, 50);

    // BUFF: the same fan-out with Strength.
    telegraph(s, 1, r::kHealerMoveBuff, MonsterIntent::BUFF);
    healer_take_turn(s, 1);
    ASSERT_EQ(s.action_count, 3);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::APPLY_POWER));
    EXPECT_EQ(queued(s, 0).flags, make_apply_power_flags(PowerId::STRENGTH));
    EXPECT_EQ(queued(s, 0).amount, 4) << "strAmt at A17+";
    EXPECT_EQ(queued(s, 0).src, 1) << "the SOURCE is the Healer";
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), 4);
    EXPECT_EQ(monster_power(s, 1, PowerId::STRENGTH), 4);
}

TEST(CityNormalsII, HealerSkipsDeadAndEscapingAllies) {
    CombatState s = MakeState(3);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::CENTURION);
    s.monsters[0].hp = 0;  // dead
    s.monsters[0].max_hp = 80;
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::MUGGER);
    s.monsters[1].hp = 30;
    s.monsters[1].max_hp = 50;
    s.monsters[1].flags |= kMonsterFlagEscaped;  // alive but gone
    s.monsters[2].monster_id = static_cast<uint16_t>(MonsterId::HEALER);
    s.monsters[2].hp = 30;
    s.monsters[2].max_hp = 56;

    telegraph(s, 2, r::kHealerMoveHeal, MonsterIntent::BUFF);
    healer_take_turn(s, 2);
    ASSERT_EQ(s.action_count, 2) << "one heal (itself) plus the roll";
    EXPECT_EQ(queued(s, 0).tgt, 2);
    drain(s);
    EXPECT_EQ(s.monsters[0].hp, 0);
    EXPECT_EQ(s.monsters[1].hp, 30) << "the escapee is not healed";
    EXPECT_EQ(s.monsters[2].hp, 50);
}

// op_heal's monster branch: clamp to maxHealth, and nothing at all while
// isDying (AbstractMonster.java:385-394).
TEST(CityNormalsII, HealClampsToMaxHp) {
    CombatState s = MakeState(2);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::CENTURION);
    s.monsters[0].hp = 75;
    s.monsters[0].max_hp = 80;
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::HEALER);
    s.monsters[1].hp = 0;  // isDying
    s.monsters[1].max_hp = 56;

    auto heal = [&](uint8_t tgt, int32_t amount) {
        ActionQueueItem it{};
        it.opcode = static_cast<uint16_t>(Opcode::HEAL);
        it.src = 1;
        it.tgt = tgt;
        it.amount = amount;
        execute_opcode(s, it);
    };
    heal(0, 20);
    EXPECT_EQ(s.monsters[0].hp, 80) << "clamped to maxHealth";
    heal(1, 20);
    EXPECT_EQ(s.monsters[1].hp, 0)
        << "a dying monster is not healed (the isDying early return)";
    // A non-positive amount is a no-op, and the PLAYER branch still routes
    // through the relic pass.
    s.player_hp = 10;
    heal(kActorPlayer, 5);
    EXPECT_EQ(s.player_hp, 15);
    heal(kActorPlayer, 0);
    EXPECT_EQ(s.player_hp, 15);
}

// ============================================================================
// 9. Encounter compositions -- spawn-order-exact
// ============================================================================

std::vector<std::string_view> members_of(std::string_view key, int64_t seed,
                                         int32_t* draws = nullptr) {
    RngStream misc = from_seed(seed);
    ResolvedGroup g{};
    EXPECT_TRUE(resolve_encounter(key, misc, g)) << key;
    if (draws != nullptr) {
        *draws = misc.counter;
    }
    return std::vector<std::string_view>(g.members.begin(),
                                         g.members.begin() + g.count);
}

TEST(CityNormalsII, TwoThievesSpawnsLooterThenMugger) {
    using V = std::vector<std::string_view>;
    EXPECT_EQ(members_of("2 Thieves", 1), (V{"Looter", "Mugger"}))
        << "MonsterHelper.java:462-464 builds {Looter(-200,15), Mugger(80,0)}";

    // The order is what the HP stream sees, and what fixes the settlement's
    // steal interleaving.
    const MonsterId group[] = {MonsterId::LOOTER, MonsterId::MUGGER};
    CombatState s = MakeSeeded(31, /*monsters=*/0);
    spawn_group(s, group);
    ASSERT_EQ(s.monster_count, 2);
    EXPECT_EQ(s.monsters[0].monster_id,
              static_cast<uint16_t>(MonsterId::LOOTER));
    EXPECT_EQ(s.monsters[1].monster_id,
              static_cast<uint16_t>(MonsterId::MUGGER));
    EXPECT_EQ(s.monster_hp_rng.counter, 2);
    EXPECT_EQ(s.ai_rng.counter, 2) << "one discarded random(99) each";
    EXPECT_EQ(s.monsters[0].move_history[0], r::kLooterMoveMug);
    EXPECT_EQ(s.monsters[1].move_history[0], r::kMuggerMoveMug);

    // Both apply Thievery, in spawn order.
    use_pre_battle_actions(s);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::THIEVERY), kLooterGoldAmt);
    EXPECT_EQ(monster_power(s, 1, PowerId::THIEVERY), kMuggerGoldAmt);
}

TEST(CityNormalsII, CenturionAndHealerSpawnsCenturionThenHealer) {
    using V = std::vector<std::string_view>;
    EXPECT_EQ(members_of("Centurion and Healer", 1), (V{"Centurion", "Healer"}))
        << "MonsterHelper.java:498-500";

    const MonsterId group[] = {MonsterId::CENTURION, MonsterId::HEALER};
    CombatState s = MakeSeeded(29, /*monsters=*/0);
    spawn_group(s, group);
    ASSERT_EQ(s.monster_count, 2);
    EXPECT_EQ(s.monsters[0].monster_id,
              static_cast<uint16_t>(MonsterId::CENTURION));
    EXPECT_EQ(s.monsters[1].monster_id,
              static_cast<uint16_t>(MonsterId::HEALER));
    EXPECT_EQ(s.monster_hp_rng.counter, 2);
    EXPECT_EQ(s.ai_rng.counter, 2) << "one random(99) each, both READ";
    // Both are at full HP after spawn, which is what makes the Healer's
    // needToHeal 0 on turn 1.
    EXPECT_EQ(s.monsters[0].hp, s.monsters[0].max_hp);
    EXPECT_EQ(s.monsters[1].hp, s.monsters[1].max_hp);
    EXPECT_NE(s.monsters[1].move_history[0], r::kHealerMoveHeal)
        << "a full-HP group never opens on HEAL";
}

// THE CONSTRUCT-ALL-THEN-INIT-ALL PROPERTY, stated as its own pin. The game
// builds every member before MonsterGroup.init() runs any of them
// (MonsterGroup.java:31-33,62-66), so the FIRST member's getMove already sees
// the whole group. The Centurion at slot 0 is the reader that proves it: with a
// living ally it must be ABLE to open on PROTECT.
TEST(CityNormalsII, CenturionAtSlotZeroCanOpenOnProtect) {
    const MonsterId group[] = {MonsterId::CENTURION, MonsterId::HEALER};
    bool saw_protect = false;
    for (int64_t seed = 1; seed <= 60 && !saw_protect; ++seed) {
        CombatState s = MakeSeeded(seed, /*monsters=*/0);
        spawn_group(s, group);
        if (s.monsters[0].move_history[0] == r::kCenturionMoveProtect) {
            saw_protect = true;
        }
    }
    EXPECT_TRUE(saw_protect)
        << "a Centurion spawned ahead of a living Healer must be able to open "
           "on PROTECT -- if this fails, the spawn pre-pass regressed and the "
           "opening telegraph is being decided against an empty slot";
}

// THE SAME PROPERTY THROUGH THE ENTRY POINT THE RUN LAYER ACTUALLY USES, which
// is the whole finding. The test above spawns with spawn_group; every combat in
// a real run is spawned by spawn_group_trace (run_advance.cpp's combat-begin
// step 6), and that one used to publish monster_count slot by slot. aliveCount
// bounds its walk by monster_count, so the Centurion at slot 0 counted only
// ITSELF, took Centurion.java:143-144's alone-arm and telegraphed FURY where the
// game telegraphed PROTECT -- 3 x 7 damage instead of a 20-block
// GainBlockRandomMonsterAction, and one ai_rng draw short from there on.
//
// LIVE WITNESS: S2.43 depth capture STS108173 (A20 Ironclad, floor 22, the
// "Centurion and Healer" strong encounter). The whole-run differ read a constant
// 21-hp player deficit from the first record after that monster turn; the run
// replays zero-diff to its terminal with the pre-pass fixed.
//
// The pair-vs-solo comparison is what makes this non-tautological: with the
// count grown incrementally the two columns are IDENTICAL for every seed,
// because a slot-0 Centurion cannot tell a group of two from a group of one.
TEST(CityNormalsII, CenturionAtSlotZeroOpensOnProtectThroughTheSpawnTrace) {
    const MonsterId pair[] = {MonsterId::CENTURION, MonsterId::HEALER};
    const MonsterId solo[] = {MonsterId::CENTURION};
    int group_aware = 0;
    for (int64_t seed = 1; seed <= 200; ++seed) {
        CombatState a = MakeSeeded(seed, /*monsters=*/0);
        spawn_group_trace(a, std::span<const MonsterId>(pair), 0b11u);
        CombatState b = MakeSeeded(seed, /*monsters=*/0);
        spawn_group_trace(b, std::span<const MonsterId>(solo), 0b1u);
        ASSERT_EQ(a.monster_count, 2);
        ASSERT_EQ(b.monster_count, 1);
        const uint8_t with_ally = a.monsters[0].move_history[0];
        const uint8_t alone = b.monsters[0].move_history[0];
        if (with_ally == alone) {
            continue;  // the SLASH arm, which reads no group at all
        }
        ++group_aware;
        EXPECT_EQ(with_ally, r::kCenturionMoveProtect);
        EXPECT_EQ(a.monsters[0].intent,
                  static_cast<uint8_t>(r::MonsterIntent::DEFEND));
        EXPECT_EQ(alone, r::kCenturionMoveFury);
        EXPECT_EQ(b.monsters[0].intent,
                  static_cast<uint8_t>(r::MonsterIntent::ATTACK));
    }
    EXPECT_GT(group_aware, 0)
        << "no seed in 200 made a slot-0 Centurion decide differently with an "
           "ally than alone -- spawn_group_trace is publishing monster_count "
           "after the init that reads it, which is the STS108173 defect";
}

TEST(CityNormalsII, SnakePlantAndSneckoAreSoloGroups) {
    using V = std::vector<std::string_view>;
    EXPECT_EQ(members_of("Snake Plant", 1), V{"SnakePlant"});
    EXPECT_EQ(members_of("Snecko", 1), V{"Snecko"});
}

TEST(CityNormalsII, ThreeCultistsRollsThreeIndependentHpValuesInOrder) {
    using V = std::vector<std::string_view>;
    EXPECT_EQ(members_of("3 Cultists", 1),
              (V{"Cultist", "Cultist", "Cultist"}));

    const MonsterId group[] = {MonsterId::CULTIST, MonsterId::CULTIST,
                               MonsterId::CULTIST};
    CombatState s = MakeSeeded(41, /*monsters=*/0);
    // The expected HP values, re-derived from an INDEPENDENT stream in spawn
    // order -- three draws, not one shared value.
    RngStream ref = from_seed(41);
    int16_t expect[3];
    for (int i = 0; i < 3; ++i) {
        expect[i] = static_cast<int16_t>(
            random(ref, r::kCultist.hp_min(kA20), r::kCultist.hp_max(kA20)));
    }
    spawn_group(s, group);
    ASSERT_EQ(s.monster_count, 3);
    EXPECT_EQ(s.monster_hp_rng.counter, 3);
    for (uint8_t i = 0; i < 3; ++i) {
        EXPECT_EQ(s.monsters[i].hp, expect[i]) << int(i);
        EXPECT_EQ(s.monsters[i].max_hp, expect[i]);
    }
    // The `talk` boolean the third Cultist differs by consumes NO seeded RNG:
    // INCANTATION's dialogue roll is unseeded MathUtils.
    EXPECT_EQ(s.ai_rng.counter, 3) << "one rollMove each, nothing else";
}

// Every fixed Act-2 list this batch touches costs ZERO miscRng draws.
TEST(CityNormalsII, FixedActTwoGroupsConsumeZeroMiscRng) {
    for (std::string_view key : {"2 Thieves", "Snake Plant", "Snecko",
                                 "Centurion and Healer", "Cultist and Chosen",
                                 "3 Cultists"}) {
        int32_t draws = -1;
        members_of(key, 7, &draws);
        EXPECT_EQ(draws, 0) << key;
    }
}

// ============================================================================
// 10. Dispatch-table shape -- the seams this batch added or joined
// ============================================================================

TEST(CityNormalsII, EveryNewMonsterIsRegisteredInEveryDispatchSwitch) {
    for (MonsterId id : {MonsterId::MUGGER, MonsterId::SNAKE_PLANT,
                         MonsterId::SNECKO, MonsterId::CENTURION,
                         MonsterId::HEALER}) {
        EXPECT_NE(monster_init_fn(id), nullptr) << static_cast<int>(id);
        EXPECT_NE(monster_turn_fn(id), &default_monster_turn)
            << static_cast<int>(id);
        EXPECT_EQ(monster_spawn_at_hp_fn(id), nullptr)
            << "none of the five is mid-combat spawnable";
    }
    // Four of five queue a trailing roll; the Mugger queues none.
    EXPECT_NE(monster_roll_move_fn(MonsterId::SNAKE_PLANT), nullptr);
    EXPECT_NE(monster_roll_move_fn(MonsterId::SNECKO), nullptr);
    EXPECT_NE(monster_roll_move_fn(MonsterId::CENTURION), nullptr);
    EXPECT_NE(monster_roll_move_fn(MonsterId::HEALER), nullptr);
    EXPECT_EQ(monster_roll_move_fn(MonsterId::MUGGER), nullptr)
        << "Mugger.takeTurn has no trailing RollMoveAction";
    // Two of five have a pre-battle action.
    EXPECT_NE(monster_pre_battle_fn(MonsterId::MUGGER), nullptr);
    EXPECT_NE(monster_pre_battle_fn(MonsterId::SNAKE_PLANT), nullptr);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::SNECKO), nullptr);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::CENTURION), nullptr);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::HEALER), nullptr);
    // Exactly one has a die() body with combat content.
    EXPECT_NE(monster_die_fn(MonsterId::MUGGER), nullptr);
    for (MonsterId id : {MonsterId::SNAKE_PLANT, MonsterId::SNECKO,
                         MonsterId::CENTURION, MonsterId::HEALER,
                         MonsterId::LOOTER}) {
        EXPECT_EQ(monster_die_fn(id), nullptr) << static_cast<int>(id);
    }
}

// lastMoveBefore is now shared (rule of two). Pinned here because promoting it
// out of monster_gremlin_nob.cpp is a behaviour-preserving move that nothing
// else would catch if it drifted.
TEST(CityNormalsII, LastMoveBeforeReadsTheSecondRingSlot) {
    MonsterState m{};
    EXPECT_FALSE(last_move_before_is(m, 1)) << "empty history is false";
    set_monster_move(m, 1, MonsterIntent::ATTACK);
    EXPECT_FALSE(last_move_before_is(m, 1)) << "one move: slot [1] is still 0";
    set_monster_move(m, 2, MonsterIntent::ATTACK);
    EXPECT_TRUE(last_move_before_is(m, 1));
    EXPECT_FALSE(last_move_before_is(m, 2));
    EXPECT_TRUE(last_move_is(m, 2));
    // It is NOT lastTwoMoves: that needs BOTH slots to match.
    EXPECT_FALSE(last_two_moves_are(m, 1));
}

}  // namespace
}  // namespace sts::engine
