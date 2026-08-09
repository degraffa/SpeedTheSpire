// Act-3 beyond normals I -- the Darkling, the Orb Walker, and the three
// "ancient shapes" (Repulsor, Exploder, Spiker), plus the three powers they
// bring (Regrow / "Life Link", Explosive, Generic Strength Up), the first
// producer of the framework's halfDead bit, and the first user of its die()
// veto.
//
// WHAT THIS FILE PINS, and why each part is here rather than assumed:
//
//   * STAT/MOVE TABLES, per monster, at EVERY ascension branch the Java has --
//     not just the A20 column the engine runs. Two boundaries here are the kind
//     that read like typos and are not: the Darkling gates HP on >= 7 while
//     gating its damage block on >= 2 (the class constants are named A_2_HP_*
//     anyway), and the Spiker's A17 Thorns arm COMPOSES with the A2 value
//     (`startingThorns + 3` = 7 at A20) instead of restating a literal.
//   * RNG DRAW COUNTS, per monster, per phase. The Orb Walker is the whole
//     reason this matters: its super-argument HP roll is drawn BEFORE the setHp
//     roll and immediately thrown away, so it costs two draws where every other
//     monster in the batch costs one -- and the Exploder's sub-A7 setHp(30,30)
//     still costs one despite a degenerate range.
//   * THE DARKLING'S HALF-DEATH, end to end: the vetoed die(), the by-hand
//     fan-outs that replace it, the DOUBLE push of move 4 onto the history, the
//     revival heal that only lands because isDying is not `hp <= 0`, and the
//     synchronous group kill that ends the fight.
//   * THE TWO LIVENESS SENSES DISAGREEING, which is what halfDead is for: a
//     half-dead Darkling is OUT for targeting (RANDOM_ENEMY, Feed) and IN for
//     the fight (the turn queue, combat-over).
//   * THE EXPLODER'S CADENCE including the death-edge ordering: Suicide resolves
//     BEFORE the 30 THORNS, so the player is hit by an already-dead monster.
//   * ENCOUNTER COMPOSITIONS, spawn-order-exact, because spawn order fixes each
//     monster's index -- and the Darkling's CHOMP arm reads its own slot PARITY,
//     so slot 1 of a group of three is a structurally different monster.
//   * THE UN-PARKING of all six groups these five monsters appear in,
//     "Sphere and 2 Shapes" included: S2.21 landed its SphericGuardian and
//     parked the row on the shapes, which arrive here.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_darkling.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_exploder.hpp"
#include "sts/engine/monster_orb_walker.hpp"
#include "sts/engine/monster_repulsor.hpp"
#include "sts/engine/monster_spiker.hpp"
#include "sts/engine/power_hooks.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/rng_stream.hpp"
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
    s.shuffle_rng = from_seed(seed);
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

// Reset the ring COMPLETELY -- head, tail AND count. Setting only head/count
// leaves action_tail where it was, so the next add_to_bottom writes past the
// live window and the "queue" a test then reads is stale bytes.
void clear_queue(CombatState& s) {
    s.action_head = 0;
    s.action_tail = 0;
    s.action_count = 0;
}

const ActionQueueItem& queued(const CombatState& s, uint8_t i) {
    return s.action_queue[(s.action_head + i) % kActionQueueCap];
}

int count_queued(const CombatState& s, Opcode op) {
    int n = 0;
    for (uint8_t i = 0; i < s.action_count; ++i) {
        if (queued(s, i).opcode == static_cast<uint16_t>(op)) {
            ++n;
        }
    }
    return n;
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

void telegraph(CombatState& s, uint8_t mi, uint8_t move, MonsterIntent intent) {
    set_monster_move(s.monsters[mi], move, intent);
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

// ============================================================================
// 1. Stat and move tables -- every ascension branch, per monster
// ============================================================================

// Darkling.java:77-91. TWO tier boundaries in one ctor and THEY DIFFER: HP is
// gated on >= 7 (:77) while chompDmg/nipDmg are gated on >= 2 (:82), even though
// the HP constants are literally named A_2_HP_MIN/A_2_HP_MAX (:52-53). The
// branch is what the row transcribes, not the constant name, and the boundary
// assertions below are what would catch a "surely 2" edit.
TEST(BeyondNormalsI, DarklingStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kDarkling;
    EXPECT_EQ(d.hp_min(0), 48);
    EXPECT_EQ(d.hp_max(0), 56);
    EXPECT_EQ(d.hp_min(2), 48) << "the HP branch is >= 7, NOT >= 2";
    EXPECT_EQ(d.hp_min(6), 48);
    EXPECT_EQ(d.hp_min(7), 50);
    EXPECT_EQ(d.hp_max(7), 59);
    EXPECT_EQ(d.hp_min(kA20), 50);
    EXPECT_EQ(d.hp_max(kA20), 59);

    // chompDmg 8, 9 from A2 -- and BOTH of CHOMP's two steps carry it.
    ASSERT_EQ(step_count(d, r::kDarklingMoveChomp), 2u)
        << "CHOMP is TWO separate DamageActions (Darkling.java:106-107)";
    for (uint8_t k = 0; k < 2; ++k) {
        EXPECT_EQ(step_amount(d, r::kDarklingMoveChomp, k, 0), 8);
        EXPECT_EQ(step_amount(d, r::kDarklingMoveChomp, k, 1), 8);
        EXPECT_EQ(step_amount(d, r::kDarklingMoveChomp, k, 2), 9);
        EXPECT_EQ(step_amount(d, r::kDarklingMoveChomp, k, kA20), 9);
    }

    // nipDmg is a ROLL, not a column: base 7..11, A2 9..13.
    const r::MonsterRollDef* nip = d.roll(r::kDarklingRollNipDamage);
    ASSERT_NE(nip, nullptr);
    EXPECT_EQ(nip->stream, r::MonsterRollStream::MONSTER_HP);
    EXPECT_EQ(nip->timing, r::MonsterRollTiming::CONSTRUCTOR_AFTER_HP);
    EXPECT_EQ(nip->min(0), 7);
    EXPECT_EQ(nip->max(0), 11);
    EXPECT_EQ(nip->min(1), 7);
    EXPECT_EQ(nip->min(2), 9);
    EXPECT_EQ(nip->max(2), 13);
    EXPECT_EQ(nip->min(kA20), 9);
    EXPECT_EQ(nip->max(kA20), 13);
    // ... and the NIP move's authored amount is the 0 placeholder the module
    // substitutes for, exactly as the Louse BITE row does.
    EXPECT_EQ(step_amount(d, r::kDarklingMoveNip, 0, kA20), 0);

    // BLOCK_AMT 12 and the A17-only Strength 2 (Darkling.java:62, :111-113).
    ASSERT_EQ(step_count(d, r::kDarklingMoveHarden), 2u);
    EXPECT_EQ(step_amount(d, r::kDarklingMoveHarden, 0, 0), 12);
    EXPECT_EQ(step_amount(d, r::kDarklingMoveHarden, 0, kA20), 12)
        << "BLOCK_AMT is flat at every ascension";
    EXPECT_EQ(step_amount(d, r::kDarklingMoveHarden, 1, kA20), 2);

    // COUNT is a combat NO-OP -- one NOP step, nothing else.
    ASSERT_EQ(step_count(d, r::kDarklingMoveCount), 1u);
    EXPECT_EQ(d.move(r::kDarklingMoveCount)->effects[0].op, r::Opcode::NOP);
    EXPECT_EQ(d.move(r::kDarklingMoveCount)->intent, r::MonsterIntent::UNKNOWN);

    // REINCARNATE: a placeholder HEAL (maxHealth/2 is per-instance) then Regrow 1.
    ASSERT_EQ(step_count(d, r::kDarklingMoveReincarnate), 2u);
    EXPECT_EQ(d.move(r::kDarklingMoveReincarnate)->effects[0].op, r::Opcode::HEAL);
    EXPECT_EQ(step_amount(d, r::kDarklingMoveReincarnate, 0, kA20), 0);
    EXPECT_EQ(step_amount(d, r::kDarklingMoveReincarnate, 1, kA20), 1);
    EXPECT_EQ(d.move(r::kDarklingMoveReincarnate)->intent, r::MonsterIntent::BUFF);

    EXPECT_TRUE(d.ai_native);
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::NORMAL);
}

// OrbWalker.java:53-67. The super-argument roll is TIER-INDEPENDENT while the
// setHp roll is tiered -- the two ranges genuinely differ from A7 up, which is
// the whole reason the discarded draw is visible in the stream.
TEST(BeyondNormalsI, OrbWalkerStatTableAndTheDiscardedSuperArgumentRoll) {
    const auto& d = r::kOrbWalker;
    EXPECT_EQ(d.hp_min(0), 90);
    EXPECT_EQ(d.hp_max(0), 96);
    EXPECT_EQ(d.hp_min(6), 90);
    EXPECT_EQ(d.hp_min(7), 92);
    EXPECT_EQ(d.hp_max(7), 102);
    EXPECT_EQ(d.hp_min(kA20), 92);
    EXPECT_EQ(d.hp_max(kA20), 102);

    const r::MonsterRollDef* super_arg = d.roll(r::kOrbWalkerRollSuperArgHp);
    ASSERT_NE(super_arg, nullptr);
    EXPECT_EQ(super_arg->timing, r::MonsterRollTiming::CONSTRUCTOR_BEFORE_HP)
        << "the super(...) argument is evaluated BEFORE the ctor body";
    EXPECT_EQ(super_arg->stream, r::MonsterRollStream::MONSTER_HP);
    for (int32_t asc : {0, 1, 2, 7, 17, kA20}) {
        EXPECT_EQ(super_arg->min(asc), 90) << "asc=" << asc;
        EXPECT_EQ(super_arg->max(asc), 96) << "asc=" << asc;
    }
    EXPECT_NE(super_arg->max(kA20), d.hp_max(kA20))
        << "at A20 the discarded roll and the surviving one span DIFFERENT "
           "ranges -- which is why the first draw cannot be folded away";

    // laserDmg 10/11 is index 0; clawDmg 15/16 is index 1.
    EXPECT_EQ(step_amount(d, r::kOrbWalkerMoveLaser, 0, 1), 10);
    EXPECT_EQ(step_amount(d, r::kOrbWalkerMoveLaser, 0, 2), 11);
    EXPECT_EQ(step_amount(d, r::kOrbWalkerMoveClaw, 0, 1), 15);
    EXPECT_EQ(step_amount(d, r::kOrbWalkerMoveClaw, 0, 2), 16);
    EXPECT_EQ(d.move(r::kOrbWalkerMoveLaser)->intent,
              r::MonsterIntent::ATTACK_DEBUFF);
    EXPECT_EQ(d.move(r::kOrbWalkerMoveClaw)->intent, r::MonsterIntent::ATTACK);

    // LASER = damage, then a Burn to a RANDOM DRAW-PILE SPOT, then a Burn to the
    // DISCARD -- two independent copies in that order.
    ASSERT_EQ(step_count(d, r::kOrbWalkerMoveLaser), 3u);
    const r::MonsterMove* laser = d.move(r::kOrbWalkerMoveLaser);
    EXPECT_EQ(laser->effects[1].op, r::Opcode::MAKE_CARD);
    EXPECT_EQ(laser->effects[2].op, r::Opcode::MAKE_CARD);
    EXPECT_EQ(laser->effects[1].amount.at(kA20), 1);
    EXPECT_EQ(laser->effects[2].amount.at(kA20), 1);
}

// Repulsor.java:49-55. ONE HP branch, one flat damage branch, dazeAmt flat 2.
TEST(BeyondNormalsI, RepulsorStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kRepulsor;
    EXPECT_EQ(d.hp_min(0), 29);
    EXPECT_EQ(d.hp_max(0), 35);
    EXPECT_EQ(d.hp_min(6), 29);
    EXPECT_EQ(d.hp_min(7), 31);
    EXPECT_EQ(d.hp_max(7), 38);
    EXPECT_EQ(d.hp_min(kA20), 31);
    EXPECT_EQ(d.roll_count, 0u) << "no per-instance ctor roll";

    EXPECT_EQ(step_amount(d, r::kRepulsorMoveAttack, 0, 1), 11);
    EXPECT_EQ(step_amount(d, r::kRepulsorMoveAttack, 0, 2), 13);
    EXPECT_EQ(step_amount(d, r::kRepulsorMoveAttack, 0, kA20), 13);

    // dazeAmt 2, flat, and it is ONE step of amount 2 (op_make_card draws per
    // copy) rather than two steps of amount 1.
    ASSERT_EQ(step_count(d, r::kRepulsorMoveDaze), 1u);
    EXPECT_EQ(step_amount(d, r::kRepulsorMoveDaze, 0, 0), 2);
    EXPECT_EQ(step_amount(d, r::kRepulsorMoveDaze, 0, kA20), 2);
    EXPECT_EQ(d.move(r::kRepulsorMoveDaze)->intent, r::MonsterIntent::DEBUFF);
}

// Exploder.java:56-61. THE DEGENERATE COLUMN IS REAL: setHp(30, 30) below A7.
TEST(BeyondNormalsI, ExploderStatTableIncludesTheDegenerateSubA7Column) {
    const auto& d = r::kExploder;
    EXPECT_EQ(d.hp_min(0), 30);
    EXPECT_EQ(d.hp_max(0), 30) << "setHp(30, 30) -- a REAL call, and it draws";
    EXPECT_EQ(d.hp_min(6), 30);
    EXPECT_EQ(d.hp_max(6), 30);
    EXPECT_EQ(d.hp_min(7), 30);
    EXPECT_EQ(d.hp_max(7), 35);
    EXPECT_EQ(d.hp_max(kA20), 35);
    // Contrast the SphericGuardian, whose flat column exists because setHp is
    // never CALLED -- it draws nothing at all. The two must not be conflated.
    EXPECT_EQ(r::kSphericGuardian.hp_min(kA20), 20);
    EXPECT_EQ(r::kSphericGuardian.hp_max(kA20), 20);

    EXPECT_EQ(step_amount(d, r::kExploderMoveAttack, 0, 1), 9);
    EXPECT_EQ(step_amount(d, r::kExploderMoveAttack, 0, 2), 11);
    // Move 2 is named BLOCK in the Java and gains no block: a NOP.
    ASSERT_EQ(step_count(d, r::kExploderMoveBlock), 1u);
    EXPECT_EQ(d.move(r::kExploderMoveBlock)->effects[0].op, r::Opcode::NOP);
    EXPECT_EQ(d.move(r::kExploderMoveBlock)->intent, r::MonsterIntent::UNKNOWN);
}

// Spiker.java:57-68, :72-79. THE A17 THORNS ARM COMPOSES with the A2 value.
TEST(BeyondNormalsI, SpikerStatTableAndTheComposingA17ThornsArm) {
    const auto& d = r::kSpiker;
    EXPECT_EQ(d.hp_min(0), 42);
    EXPECT_EQ(d.hp_max(0), 56);
    EXPECT_EQ(d.hp_min(6), 42);
    EXPECT_EQ(d.hp_min(7), 44);
    EXPECT_EQ(d.hp_max(7), 60);

    EXPECT_EQ(step_amount(d, r::kSpikerMoveAttack, 0, 1), 7);
    EXPECT_EQ(step_amount(d, r::kSpikerMoveAttack, 0, 2), 9);
    // BUFF_AMT 2, flat.
    EXPECT_EQ(step_amount(d, r::kSpikerMoveBuffThorns, 0, 0), 2);
    EXPECT_EQ(step_amount(d, r::kSpikerMoveBuffThorns, 0, kA20), 2);
    EXPECT_EQ(d.move(r::kSpikerMoveBuffThorns)->effects[0].op,
              r::Opcode::APPLY_POWER);

    // The opener: 3, then 4 from A2, then 4 + 3 = SEVEN from A17. NOT 6.
    EXPECT_EQ(spiker_starting_thorns(0), 3);
    EXPECT_EQ(spiker_starting_thorns(1), 3);
    EXPECT_EQ(spiker_starting_thorns(2), 4);
    EXPECT_EQ(spiker_starting_thorns(16), 4);
    EXPECT_EQ(spiker_starting_thorns(17), 7)
        << "the A17 arm is `startingThorns + 3`, reading the ALREADY-TIERED A2 "
           "value -- 4 + 3, not a restated 6 and not 3 + 3";
    EXPECT_EQ(spiker_starting_thorns(kA20), 7);
}

// ============================================================================
// 2. Registry identity, dispatch registration, and the three power rows
// ============================================================================

TEST(BeyondNormalsI, IdsAndDispatch) {
    EXPECT_EQ(static_cast<uint16_t>(MonsterId::DARKLING), 49);
    EXPECT_EQ(static_cast<uint16_t>(MonsterId::ORB_WALKER), 50);
    EXPECT_EQ(static_cast<uint16_t>(MonsterId::REPULSOR), 51);
    EXPECT_EQ(static_cast<uint16_t>(MonsterId::EXPLODER), 52);
    EXPECT_EQ(static_cast<uint16_t>(MonsterId::SPIKER), 53);
    EXPECT_EQ(static_cast<uint16_t>(PowerId::REGROW), 99);
    EXPECT_EQ(static_cast<uint16_t>(PowerId::EXPLOSIVE), 100);
    EXPECT_EQ(static_cast<uint16_t>(PowerId::GENERIC_STRENGTH_UP), 101);

    EXPECT_EQ(sts::registry::monster_game_id(MonsterId::DARKLING), "Darkling");
    EXPECT_EQ(sts::registry::monster_game_id(MonsterId::ORB_WALKER), "Orb Walker")
        << "with a SPACE -- OrbWalker.java:32";
    EXPECT_EQ(sts::registry::monster_game_id(MonsterId::REPULSOR), "Repulsor");
    EXPECT_EQ(sts::registry::monster_game_id(MonsterId::EXPLODER), "Exploder");
    EXPECT_EQ(sts::registry::monster_game_id(MonsterId::SPIKER), "Spiker");

    EXPECT_EQ(monster_init_fn(MonsterId::DARKLING), &darkling_init);
    EXPECT_EQ(monster_turn_fn(MonsterId::DARKLING), &darkling_take_turn);
    EXPECT_EQ(monster_roll_move_fn(MonsterId::DARKLING), &darkling_roll_move);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::DARKLING),
              &darkling_use_pre_battle_action);
    EXPECT_EQ(monster_die_fn(MonsterId::DARKLING), &darkling_die);
    EXPECT_EQ(monster_die_after_fn(MonsterId::DARKLING), nullptr)
        << "Darkling.die has no content AFTER super.die()";

    EXPECT_EQ(monster_init_fn(MonsterId::ORB_WALKER), &orb_walker_init);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::ORB_WALKER),
              &orb_walker_use_pre_battle_action);
    EXPECT_EQ(monster_init_fn(MonsterId::REPULSOR), &repulsor_init);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::REPULSOR), nullptr)
        << "Repulsor.java declares no usePreBattleAction at all";
    EXPECT_EQ(monster_init_fn(MonsterId::EXPLODER), &exploder_init);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::EXPLODER),
              &exploder_use_pre_battle_action);
    EXPECT_EQ(monster_init_fn(MonsterId::SPIKER), &spiker_init);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::SPIKER),
              &spiker_use_pre_battle_action);

    // All five queue a trailing ROLL_MOVE; none is mid-combat spawnable; only
    // the Darkling has a damage() override with content.
    for (MonsterId id : {MonsterId::DARKLING, MonsterId::ORB_WALKER,
                         MonsterId::REPULSOR, MonsterId::EXPLODER,
                         MonsterId::SPIKER}) {
        EXPECT_NE(monster_roll_move_fn(id), nullptr)
            << static_cast<int>(id) << " ends takeTurn in a RollMoveAction";
        EXPECT_EQ(monster_spawn_at_hp_fn(id), nullptr)
            << static_cast<int>(id) << " is never mid-combat spawned";
    }
    for (MonsterId id : {MonsterId::ORB_WALKER, MonsterId::REPULSOR,
                         MonsterId::EXPLODER, MonsterId::SPIKER}) {
        CombatState s = MakeSeeded(11);
        monster_init_fn(id)(s, 0);
        const int16_t hp = s.monsters[0].hp;
        const uint8_t queued_before = s.action_count;
        on_monster_damaged(s, 0, 3);
        EXPECT_EQ(s.monsters[0].hp, hp);
        EXPECT_EQ(s.action_count, queued_before)
            << static_cast<int>(id) << " has no damage() content";
    }
}

// Regrow: the row's game_id is the trap. RegrowPower.POWER_ID is "Life Link"
// (RegrowPower.java:16), not "Regrow" -- and ResurrectPower declares the SAME
// string while having zero construction sites anywhere in the tree, so a second
// "Life Link" row must never be added.
TEST(BeyondNormalsI, RegrowIsAPureMarkerPowerCalledLifeLink) {
    const PowerDef* def = power_def(PowerId::REGROW);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(sts::registry::power_game_id(PowerId::REGROW), "Life Link");
    EXPECT_NE(sts::registry::power_game_id(PowerId::REGROW), "Regrow");
    EXPECT_EQ(def->type, PowerType::BUFF);
    EXPECT_FALSE(def->native) << "RegrowPower has no hooks at all";
    for (int h = 0; h < kHookCount; ++h) {
        EXPECT_EQ(def->hook_binding(static_cast<r::Hook>(h)), nullptr)
            << "Regrow binds hook " << static_cast<int>(h);
    }
}

TEST(BeyondNormalsI, ExplosiveIsNativeOnDuringTurnOnly) {
    const PowerDef* def = power_def(PowerId::EXPLOSIVE);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(sts::registry::power_game_id(PowerId::EXPLOSIVE), "Explosive");
    EXPECT_EQ(def->type, PowerType::BUFF)
        << "no type assignment in the ctor -> AbstractPower's BUFF default, "
           "even though the power kills its owner";
    EXPECT_TRUE(def->native);
    EXPECT_NE(def->hook_binding(r::Hook::DURING_TURN), nullptr);
    EXPECT_EQ(def->hook_binding(r::Hook::AT_END_OF_TURN), nullptr);
    EXPECT_EQ(def->hook_binding(r::Hook::AT_END_OF_ROUND), nullptr);
    EXPECT_EQ(static_cast<int>(Hook::DURING_TURN), 15);
}

// Generic Strength Up is a DATA row, not native: one unconditional queued
// APPLY_POWER of Strength at the power's own stack amount, every round.
TEST(BeyondNormalsI, GenericStrengthUpIsADataHookAtEndOfRound) {
    const PowerDef* def = power_def(PowerId::GENERIC_STRENGTH_UP);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(sts::registry::power_game_id(PowerId::GENERIC_STRENGTH_UP),
              "Generic Strength Up Power");
    EXPECT_EQ(def->type, PowerType::BUFF);
    EXPECT_FALSE(def->native);
    const r::PowerHookBinding* prog = def->hook_binding(r::Hook::AT_END_OF_ROUND);
    ASSERT_NE(prog, nullptr);
    ASSERT_EQ(prog->step_count, 1u);
    EXPECT_EQ(prog->steps[0].op, r::Opcode::APPLY_POWER);
    EXPECT_EQ(prog->steps[0].amount, 0)
        << "amount 0 == the schema's 'use the power's own stack amount', which "
           "is exactly the Java's `this.amount` on both the constructed "
           "StrengthPower and the stack count";
}

// The end-to-end consequence: an Orb Walker opens with Generic Strength Up 5 at
// A20, and every end-of-round turns that into +5 Strength, uncapped.
TEST(BeyondNormalsI, OrbWalkerRampsFiveStrengthEveryRound) {
    CombatState s = MakeSeeded(3);
    orb_walker_init(s, 0);
    orb_walker_use_pre_battle_action(s, 0);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::GENERIC_STRENGTH_UP), 5);
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), -1);

    for (int round = 1; round <= 3; ++round) {
        dispatch_at_end_of_round(s);
        drain(s);
        EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), 5 * round)
            << "round " << round;
        EXPECT_EQ(monster_power(s, 0, PowerId::GENERIC_STRENGTH_UP), 5)
            << "the power itself never decays";
    }
}

// ============================================================================
// 3. Construction draw counts -- the bit-exactness surface
// ============================================================================

TEST(BeyondNormalsI, ConstructionDrawCountsPerMonster) {
    struct Row { MonsterId id; int32_t hp_draws; const char* why; };
    const Row kRows[] = {
        {MonsterId::DARKLING, 2,
         "setHp (Darkling.java:77-81) then the nipDmg roll (:82-88)"},
        {MonsterId::ORB_WALKER, 2,
         "the DISCARDED super-argument roll (OrbWalker.java:53) then setHp "
         "(:54-58)"},
        {MonsterId::REPULSOR, 1, "setHp only (Repulsor.java:50-54)"},
        {MonsterId::EXPLODER, 1,
         "setHp only (Exploder.java:56-60) -- and it draws even on the "
         "degenerate sub-A7 range"},
        {MonsterId::SPIKER, 1, "setHp only (Spiker.java:57-61)"},
    };
    for (const Row& row : kRows) {
        CombatState s = MakeSeeded(99);
        const int32_t hp0 = s.monster_hp_rng.counter;
        const int32_t ai0 = s.ai_rng.counter;
        monster_init_fn(row.id)(s, 0);
        EXPECT_EQ(s.monster_hp_rng.counter - hp0, row.hp_draws) << row.why;
        EXPECT_EQ(s.ai_rng.counter - ai0, 1)
            << "init's rollMove is exactly one aiRng.random(99) -- even for the "
               "Exploder, whose getMove never reads it";
        EXPECT_GT(s.monsters[0].hp, 0);
        EXPECT_EQ(s.monsters[0].hp, s.monsters[0].max_hp);
        EXPECT_NE(s.monsters[0].move_history[0], 0)
            << "init telegraphs a move";
    }
}

// The Orb Walker's discarded roll, proved at the STREAM level: the surviving HP
// is the SECOND value, not the first, and the group cost is 2 per member.
TEST(BeyondNormalsI, OrbWalkerBurnsTheFirstHpValueAndKeepsTheSecond) {
    for (int64_t seed = 1; seed < 40; ++seed) {
        RngStream ref = from_seed(seed);
        const int32_t discarded = random(ref, 90, 96);
        const int32_t kept = random(ref, 92, 102);

        CombatState s = MakeSeeded(seed);
        orb_walker_init(s, 0);
        EXPECT_EQ(s.monsters[0].hp, kept) << "seed=" << seed;
        EXPECT_EQ(s.monster_hp_rng.counter, ref.counter) << "seed=" << seed;
        if (discarded != kept) {
            EXPECT_NE(s.monsters[0].hp, discarded) << "seed=" << seed;
        }
    }

    // "2 Orb Walkers" (the Mysterious Sphere event group) therefore costs FOUR
    // monster_hp_rng draws, not two.
    CombatState s = MakeSeeded(7, 2);
    const MonsterId group[] = {MonsterId::ORB_WALKER, MonsterId::ORB_WALKER};
    spawn_group(s, group);
    EXPECT_EQ(s.monster_hp_rng.counter, 4);
    EXPECT_EQ(s.ai_rng.counter, 2);
}

// The discarded-PICK burn path has to reproduce the SAME order: BEFORE first,
// then the setHp draw, then the AFTER rolls. burn_unspawned_ctor_rolls reads the
// same registry rows the init does, so the two cannot drift.
TEST(BeyondNormalsI, UnspawnedBurnMatchesTheInitStreamExactly) {
    struct Row { MonsterId id; int32_t draws; };
    for (const Row& row : {Row{MonsterId::DARKLING, 2},
                           Row{MonsterId::ORB_WALKER, 2},
                           Row{MonsterId::REPULSOR, 1},
                           Row{MonsterId::EXPLODER, 1},
                           Row{MonsterId::SPIKER, 1}}) {
        CombatState burned = MakeSeeded(21);
        burn_unspawned_ctor_rolls(burned, row.id);
        EXPECT_EQ(burned.monster_hp_rng.counter, row.draws)
            << "monster " << static_cast<int>(row.id);
        EXPECT_EQ(burned.ai_rng.counter, 0)
            << "a discarded candidate is never init()ed, so it draws NO ai_rng";

        // ... and the resulting HP stream position is identical to a real spawn.
        CombatState spawned = MakeSeeded(21);
        monster_init_fn(row.id)(spawned, 0);
        EXPECT_EQ(spawned.monster_hp_rng.counter,
                  burned.monster_hp_rng.counter)
            << "monster " << static_cast<int>(row.id);
    }
}

// ============================================================================
// 4. Move selection -- the Darkling's recursion and slot parity
// ============================================================================

// Darkling.java:163. `monsters.lastIndexOf(this) % 2 == 0` -- SLOT 1 CAN NEVER
// CHOMP. In a group of three that makes the middle Darkling a structurally
// different monster from its neighbours, and it is a group-position read, not a
// per-monster one.
TEST(BeyondNormalsI, DarklingSlotParityDecidesWhoCanChomp) {
    bool chomped[3] = {false, false, false};
    for (int64_t seed = 0; seed < 400; ++seed) {
        CombatState s = MakeSeeded(seed, 3);
        const MonsterId group[] = {MonsterId::DARKLING, MonsterId::DARKLING,
                                   MonsterId::DARKLING};
        spawn_group(s, group);
        for (uint8_t mi = 0; mi < 3; ++mi) {
            // Ten decisions each, from the real roll path.
            for (int t = 0; t < 10; ++t) {
                darkling_roll_move(s, mi);
                if (s.monsters[mi].move_history[0] ==
                    r::kDarklingMoveChomp) {
                    chomped[mi] = true;
                }
            }
        }
    }
    EXPECT_TRUE(chomped[0]) << "slot 0 has even parity and can CHOMP";
    EXPECT_FALSE(chomped[1])
        << "slot 1 has ODD parity: the `% 2 == 0` conjunct can never hold, so "
           "the middle Darkling of a group of three NEVER chomps";
    EXPECT_TRUE(chomped[2]) << "slot 2 has even parity and can CHOMP";
}

// Darkling.java:166 and :181 -- the recursion draws again, with DIFFERENT
// bounds. Driving the tree directly proves the draw COUNT is input-dependent
// rather than fixed at one per decision.
TEST(BeyondNormalsI, DarklingGetMoveRecursionSpendsExtraAiRngDraws) {
    // (a) The CHOMP arm is blocked by parity (slot 1), so a num < 40 decision
    //     ALWAYS recurses on random(40, 99) -- exactly one extra draw, and the
    //     re-entry cannot reach the num < 40 arm again.
    bool saw_extra = false;
    for (int64_t seed = 0; seed < 200 && !saw_extra; ++seed) {
        CombatState s = MakeSeeded(seed, 2);
        const MonsterId group[] = {MonsterId::DARKLING, MonsterId::DARKLING};
        spawn_group(s, group);
        // Peek at what slot 1's next roll will draw without disturbing it.
        RngStream peek = s.ai_rng;
        const int32_t num = random(peek, 99);
        if (num >= 40) {
            continue;
        }
        const int32_t before = s.ai_rng.counter;
        darkling_roll_move(s, 1);
        EXPECT_GE(s.ai_rng.counter - before, 2)
            << "slot 1 cannot CHOMP, so a num < 40 decision must re-enter "
               "getMove on a FRESH random(40, 99)";
        EXPECT_NE(s.monsters[1].move_history[0], r::kDarklingMoveChomp);
        saw_extra = true;
    }
    EXPECT_TRUE(saw_extra) << "no seed produced a num < 40 opening roll";

    // (b) The two recursion bounds are not interchangeable. A 40..99 re-entry
    //     can only land in the num < 70 or the >= 70 arm; a 0..99 re-entry can
    //     land back in the CHOMP arm. Pinned as a reachability claim over the
    //     tree: with slot 0 (even parity) and a history of two NIPs, the >= 70
    //     arm recurses on 0..99 and CHOMP becomes reachable again -- which a
    //     random(40, 99) re-entry could never produce.
    bool chomp_after_double_nip = false;
    for (int64_t seed = 0; seed < 600 && !chomp_after_double_nip; ++seed) {
        CombatState s = MakeSeeded(seed, 1);
        const MonsterId one[] = {MonsterId::DARKLING};
        spawn_group(s, one);
        telegraph(s, 0, r::kDarklingMoveNip, MonsterIntent::ATTACK);
        telegraph(s, 0, r::kDarklingMoveNip, MonsterIntent::ATTACK);
        RngStream peek = s.ai_rng;
        if (random(peek, 99) < 70) {
            continue;
        }
        darkling_roll_move(s, 0);
        if (s.monsters[0].move_history[0] == r::kDarklingMoveChomp) {
            chomp_after_double_nip = true;
        }
    }
    EXPECT_TRUE(chomp_after_double_nip)
        << "the >= 70 / lastTwoMoves(NIP) arm recurses on random(0, 99), which "
           "CAN re-enter the CHOMP arm -- a random(40, 99) re-entry never could";
}

// Darkling.java:149-161. firstMove is consumed on init's rollMove and is NEVER
// re-set, so a revived Darkling does not get a second opening move.
TEST(BeyondNormalsI, DarklingFirstMoveIsHardenOrNipAndNeverRepeats) {
    int harden = 0;
    int nip = 0;
    for (int64_t seed = 0; seed < 120; ++seed) {
        CombatState s = MakeSeeded(seed);
        RngStream peek = from_seed(seed);
        // The HP roll comes first on monster_hp_rng, which is a different
        // stream, so ai_rng's first value is the opening decision's num.
        const int32_t num = random(peek, 99);
        darkling_init(s, 0);
        const uint8_t opened = s.monsters[0].move_history[0];
        if (num < 50) {
            EXPECT_EQ(opened, r::kDarklingMoveHarden) << "seed=" << seed;
            EXPECT_EQ(s.monsters[0].intent,
                      static_cast<uint8_t>(MonsterIntent::DEFEND_BUFF))
                << "A20 >= 17, so HARDEN telegraphs DEFEND_BUFF";
            ++harden;
        } else {
            EXPECT_EQ(opened, r::kDarklingMoveNip) << "seed=" << seed;
            ++nip;
        }
        EXPECT_EQ(s.ai_rng.counter, 1u)
            << "the firstMove branch never recurses";
    }
    EXPECT_GT(harden, 0);
    EXPECT_GT(nip, 0);
}

// Repulsor.java:75-82. DAZE is the default arm, so ATTACK needs BOTH a num < 20
// and a last move that was not ATTACK.
TEST(BeyondNormalsI, RepulsorAttacksOnlyOnALowRollAndNeverTwiceRunning) {
    int attacks = 0;
    for (int64_t seed = 0; seed < 300; ++seed) {
        CombatState s = MakeSeeded(seed);
        repulsor_init(s, 0);
        uint8_t prev = s.monsters[0].move_history[0];
        for (int t = 0; t < 8; ++t) {
            const int32_t before = s.ai_rng.counter;
            repulsor_roll_move(s, 0);
            EXPECT_EQ(s.ai_rng.counter - before, 1)
                << "no recursion anywhere in Repulsor.getMove";
            const uint8_t now = s.monsters[0].move_history[0];
            if (now == r::kRepulsorMoveAttack) {
                ++attacks;
                EXPECT_NE(prev, r::kRepulsorMoveAttack)
                    << "!lastMove(ATTACK) forbids back-to-back attacks";
            }
            prev = now;
        }
    }
    EXPECT_GT(attacks, 0);
}

// Exploder.java:86-92, and the derivation argued in monster_exploder.hpp. The
// cadence must hold FOR ALL TIME, not just for the first three decisions --
// which is where the obvious `last_two_moves_are(ATTACK)` form fails.
TEST(BeyondNormalsI, ExploderTelegraphsAttackAttackThenMoveTwoForever) {
    CombatState s = MakeSeeded(5);
    exploder_init(s, 0);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kExploderMoveAttack)
        << "turnCount 0 < 2";

    // Turn 1: takeTurn, then the trailing roll. turnCount 1 -> still ATTACK.
    exploder_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kExploderMoveAttack);

    // Turn 2: turnCount 2 -> move 2.
    exploder_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kExploderMoveBlock);

    // Turns 3..8: it must STAY on move 2. This is the assertion the naive
    // history predicate fails at turn 3, where the ring no longer holds two
    // consecutive ATTACKs.
    for (int t = 3; t <= 8; ++t) {
        exploder_take_turn(s, 0);
        drain(s);
        EXPECT_EQ(s.monsters[0].move_history[0], r::kExploderMoveBlock)
            << "turn " << t << ": move 2 is ABSORBING";
    }
}

// Exploder.java:86-92: `num` is never read, but the draw still happens, so the
// stream advances identically no matter what value comes out.
TEST(BeyondNormalsI, ExploderRollDrawsButIgnoresTheValue) {
    for (int64_t seed = 0; seed < 60; ++seed) {
        CombatState s = MakeSeeded(seed);
        exploder_init(s, 0);
        const int32_t before = s.ai_rng.counter;
        exploder_roll_move(s, 0);
        EXPECT_EQ(s.ai_rng.counter - before, 1) << "seed=" << seed;
    }
}

// Spiker.java:97-110. The counter is the whole point: after SIX buffs it latches
// into ATTACK permanently, and the counter is not recoverable from the history.
TEST(BeyondNormalsI, SpikerLatchesIntoAttackAfterSixBuffs) {
    CombatState s = MakeSeeded(4);
    spiker_init(s, 0);
    spiker_use_pre_battle_action(s, 0);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::THORNS), 7)
        << "the A17 arm composes with the A2 value (Spiker.java:75)";
    EXPECT_EQ(spiker_thorns_count(s, 0), 0);

    int buffs = 0;
    for (int t = 0; t < 200 && buffs < 6; ++t) {
        if (s.monsters[0].move_history[0] == r::kSpikerMoveBuffThorns) {
            ++buffs;
        }
        spiker_take_turn(s, 0);
        drain(s);
    }
    ASSERT_EQ(buffs, 6) << "six BUFF turns should arrive well inside 200";
    EXPECT_EQ(spiker_thorns_count(s, 0), 6);
    EXPECT_EQ(monster_power(s, 0, PowerId::THORNS), 7 + 6 * 2)
        << "7 opening + 2 per BUFF = 19 at A20";

    // From here on it is ATTACK forever, whatever the roll says.
    for (int t = 0; t < 40; ++t) {
        spiker_roll_move(s, 0);
        EXPECT_EQ(s.monsters[0].move_history[0], r::kSpikerMoveAttack)
            << "thornsCount > 5 is checked FIRST and never cleared";
    }
    EXPECT_EQ(spiker_thorns_count(s, 0), 6) << "the counter saturates";
}

// The interaction that makes a ramped Spiker dangerous: ThornsPower reflects PER
// HIT, so a multi-hit attack pays the toll once per hit.
TEST(BeyondNormalsI, SpikerThornsReflectsOncePerHitOfAMultiHitAttack) {
    CombatState s = MakeSeeded(8);
    spiker_init(s, 0);
    spiker_use_pre_battle_action(s, 0);
    drain(s);
    s.monsters[0].hp = 200;
    s.monsters[0].max_hp = 200;
    const int16_t before = s.player_hp;
    for (int hit = 0; hit < 4; ++hit) {
        player_attacks(s, 0, 1);
        drain(s);
    }
    EXPECT_EQ(before - s.player_hp, 4 * 7)
        << "four hits, seven Thorns each -- the reflect is per HIT, not per "
           "attack";
}

// ============================================================================
// 5. Turn bodies -- what each move actually queues
// ============================================================================

// The A17-only step of HARDEN: an effect list carries per-tier AMOUNTS but not
// per-tier PRESENCE, so the module owns the gate. At A20 both steps are queued.
TEST(BeyondNormalsI, DarklingHardenQueuesBlockThenTheA17Strength) {
    CombatState s = MakeSeeded(6);
    darkling_init(s, 0);
    clear_queue(s);
    telegraph(s, 0, r::kDarklingMoveHarden, MonsterIntent::DEFEND_BUFF);
    darkling_take_turn(s, 0);
    ASSERT_EQ(s.action_count, 3u) << "BLOCK, APPLY_POWER, ROLL_MOVE";
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::BLOCK));
    EXPECT_EQ(queued(s, 0).amount, 12);
    EXPECT_EQ(queued(s, 1).opcode, static_cast<uint16_t>(Opcode::APPLY_POWER));
    EXPECT_EQ(queued(s, 1).amount, 2);
    EXPECT_EQ(queued(s, 2).opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE));
    drain(s);
    EXPECT_EQ(s.monsters[0].block, 12);
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), 2);
}

// NIP uses the per-instance rolled nipDmg (pad0), not a table column.
TEST(BeyondNormalsI, DarklingNipUsesTheRolledPerInstanceDamage) {
    for (int64_t seed = 0; seed < 40; ++seed) {
        CombatState s = MakeSeeded(seed);
        darkling_init(s, 0);
        const uint8_t nip = s.monsters[0].pad0;
        EXPECT_GE(nip, 9) << "seed=" << seed;
        EXPECT_LE(nip, 13) << "seed=" << seed;
        clear_queue(s);
        telegraph(s, 0, r::kDarklingMoveNip, MonsterIntent::ATTACK);
        darkling_take_turn(s, 0);
        ASSERT_GE(s.action_count, 1u);
        EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::DAMAGE));
        EXPECT_EQ(queued(s, 0).amount, static_cast<int32_t>(nip))
            << "seed=" << seed;
    }
}

// LASER: damage, then ONE Burn into a random draw-pile spot (exactly one
// card_random_rng draw), then ONE Burn to the discard (no draw), in that order.
TEST(BeyondNormalsI, OrbWalkerLaserSplitsOneBurnIntoTwoPiles) {
    CombatState s = MakeSeeded(12);
    orb_walker_init(s, 0);
    // A non-empty draw pile, so addToRandomSpot has something to index into.
    for (int i = 0; i < 5; ++i) {
        s.draw[i] = static_cast<CardPoolIndex>(i);
    }
    s.draw_count = 5;
    for (int i = 0; i < 5; ++i) {
        s.card_pool[i].card_id = static_cast<uint16_t>(CardId::STRIKE);
    }
    clear_queue(s);
    telegraph(s, 0, r::kOrbWalkerMoveLaser, MonsterIntent::ATTACK_DEBUFF);
    orb_walker_take_turn(s, 0);
    ASSERT_EQ(s.action_count, 4u) << "DAMAGE, MAKE_CARD, MAKE_CARD, ROLL_MOVE";
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::DAMAGE));
    EXPECT_EQ(queued(s, 1).opcode, static_cast<uint16_t>(Opcode::MAKE_CARD));
    EXPECT_EQ(queued(s, 1).src, static_cast<uint8_t>(CardPile::DRAW_RANDOM))
        << "the DRAW-pile copy is made FIRST "
           "(MakeTempCardInDiscardAndDeckAction.java:30-33)";
    EXPECT_EQ(queued(s, 2).opcode, static_cast<uint16_t>(Opcode::MAKE_CARD));
    EXPECT_EQ(queued(s, 2).src, static_cast<uint8_t>(CardPile::DISCARD));

    const int32_t before = s.card_random_rng.counter;
    const uint8_t draw_before = s.draw_count;
    const uint8_t discard_before = s.discard_count;
    drain(s);
    EXPECT_EQ(s.card_random_rng.counter - before, 1)
        << "only the draw-pile copy draws; the discard copy is appended";
    EXPECT_EQ(s.draw_count, static_cast<uint8_t>(draw_before + 1));
    EXPECT_EQ(s.discard_count, static_cast<uint8_t>(discard_before + 1));
}

// DAZE: two Dazed, two SEQUENTIAL card_random_rng draws -- one per copy -- and
// ZERO on an empty draw pile (CardGroup.addToRandomSpot's empty-group branch).
TEST(BeyondNormalsI, RepulsorDazeDrawsOncePerCopyAndNotAtAllWhenEmpty) {
    {
        CombatState s = MakeSeeded(13);
        repulsor_init(s, 0);
        for (int i = 0; i < 4; ++i) {
            s.draw[i] = static_cast<CardPoolIndex>(i);
            s.card_pool[i].card_id = static_cast<uint16_t>(CardId::STRIKE);
        }
        s.draw_count = 4;
        clear_queue(s);
        telegraph(s, 0, r::kRepulsorMoveDaze, MonsterIntent::DEBUFF);
        repulsor_take_turn(s, 0);
        const int32_t before = s.card_random_rng.counter;
        drain(s);
        EXPECT_EQ(s.card_random_rng.counter - before, 2)
            << "MakeTempCardInDrawPileAction loops per copy and each one hits "
               "addToRandomSpot";
        EXPECT_EQ(s.draw_count, 6);
    }
    {
        CombatState s = MakeSeeded(13);
        repulsor_init(s, 0);
        s.draw_count = 0;
        clear_queue(s);
        telegraph(s, 0, r::kRepulsorMoveDaze, MonsterIntent::DEBUFF);
        repulsor_take_turn(s, 0);
        const int32_t before = s.card_random_rng.counter;
        drain(s);
        // CardGroup.addToRandomSpot's EMPTY-group branch appends WITHOUT a
        // draw -- so the first copy is free. It also makes the pile non-empty,
        // so the SECOND copy does draw. One, not two and not zero: the per-copy
        // loop is what makes that distinction expressible at all.
        EXPECT_EQ(s.card_random_rng.counter - before, 1)
            << "the first copy lands in an empty pile and draws nothing; the "
               "second copy sees a pile of one and draws";
        EXPECT_EQ(s.draw_count, 2);
    }
}

// ============================================================================
// 6. The Darkling's half-death, revival, and group kill
// ============================================================================

// Spawn a real 3-Darkling group with its pre-battle actions run.
CombatState MakeDarklingGroup(int64_t seed) {
    CombatState s = MakeSeeded(seed, 3);
    const MonsterId group[] = {MonsterId::DARKLING, MonsterId::DARKLING,
                               MonsterId::DARKLING};
    spawn_group(s, group);
    use_pre_battle_actions(s);
    drain(s);
    return s;
}

TEST(BeyondNormalsI, DarklingPreBattleLatchesCannotLoseAndGrantsRegrow) {
    CombatState s = MakeDarklingGroup(31);
    EXPECT_NE(s.flags & kCombatFlagCannotLose, 0u)
        << "a BARE field assignment (Darkling.java:96), not a CannotLoseAction";
    for (uint8_t mi = 0; mi < 3; ++mi) {
        EXPECT_EQ(monster_power(s, mi, PowerId::REGROW), 1) << "slot " << mi;
    }
}

// The whole half-death block, in one place: die() vetoed, halfDead set, powers
// cleared, move 4 pushed TWICE, and the record still IN the fight.
TEST(BeyondNormalsI, DarklingHalfDeathVetoesDieAndTelegraphsCountTwice) {
    CombatState s = MakeDarklingGroup(31);
    // A power on the victim, so the by-hand onDeath walk and the powers.clear()
    // are both observable.
    give_monster_power(s, 0, PowerId::STRENGTH, 3);
    const uint8_t powers_before = s.monsters[0].power_count;
    ASSERT_GE(powers_before, 2u) << "Regrow plus the Strength just added";

    telegraph(s, 0, r::kDarklingMoveNip, MonsterIntent::ATTACK);
    player_attacks(s, 0, 500);

    const MonsterState& m = s.monsters[0];
    EXPECT_EQ(m.hp, 0);
    EXPECT_TRUE(monster_half_dead(m));
    EXPECT_EQ(m.power_count, 0)
        << "`this.powers.clear()` (Darkling.java:211) -- something the base "
           "die() NEVER does";

    // THE TWO LIVENESS SENSES DISAGREE, which is the point of the bit.
    EXPECT_TRUE(monster_dead_or_escaped(m)) << "OUT for targeting";
    EXPECT_FALSE(monster_basically_dead(m)) << "IN the fight";

    // The telegraph: setMove ran SYNCHRONOUSLY and a SET_MOVE was queued.
    EXPECT_EQ(m.move_history[0], r::kDarklingMoveCount);
    EXPECT_EQ(count_queued(s, Opcode::SET_MOVE), 1);
    drain(s);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kDarklingMoveCount);
    EXPECT_EQ(s.monsters[0].move_history[1], r::kDarklingMoveCount)
        << "move 4 is pushed TWICE -- once at :221, once when the queued "
           "SetMoveAction at :223 resolves (AbstractMonster.setMove appends on "
           "EVERY call)";
    EXPECT_EQ(s.monsters[0].move_history[2], r::kDarklingMoveNip)
        << "and the pre-death move is what got pushed down";

    // Re-hitting a half-dead Darkling does nothing at all: a damage sponge.
    const uint8_t queue_before = s.action_count;
    player_attacks(s, 0, 50);
    EXPECT_EQ(s.monsters[0].hp, 0);
    EXPECT_TRUE(monster_half_dead(s.monsters[0]));
    EXPECT_EQ(s.action_count, queue_before)
        << "the all-dead test is not re-run for an already-half-dead record";

    // The other two are untouched and the fight is not over.
    EXPECT_GT(s.monsters[1].hp, 0);
    EXPECT_GT(s.monsters[2].hp, 0);
    EXPECT_NE(s.flags & kCombatFlagCannotLose, 0u);
}

// COUNT -> REINCARNATE, and the heal that ONLY lands because isDying is
// `hp <= 0 && !halfDead` rather than a bare `hp <= 0`.
TEST(BeyondNormalsI, DarklingReincarnatesToHalfMaxHpAndClearsHalfDead) {
    CombatState s = MakeDarklingGroup(31);
    const int16_t max_hp = s.monsters[0].max_hp;
    telegraph(s, 0, r::kDarklingMoveNip, MonsterIntent::ATTACK);
    player_attacks(s, 0, 500);
    drain(s);
    ASSERT_TRUE(monster_half_dead(s.monsters[0]));
    ASSERT_EQ(s.monsters[0].move_history[0], r::kDarklingMoveCount);

    // The COUNT turn: a combat no-op plus the trailing roll, and the roll sees
    // halfDead -> REINCARNATE.
    darkling_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kDarklingMoveReincarnate);
    EXPECT_TRUE(monster_half_dead(s.monsters[0]))
        << "still half dead going INTO the revival turn";

    // The REINCARNATE turn.
    darkling_take_turn(s, 0);
    ASSERT_GE(s.action_count, 3u);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::HEAL));
    EXPECT_EQ(queued(s, 0).amount, max_hp / 2)
        << "maxHealth / 2, INTEGER division (Darkling.java:131)";
    EXPECT_EQ(queued(s, 1).opcode, static_cast<uint16_t>(Opcode::APPLY_POWER));
    drain(s);

    EXPECT_EQ(s.monsters[0].hp, max_hp / 2)
        << "the heal LANDED -- it would silently no-op if isDying were modelled "
           "as a bare hp <= 0";
    EXPECT_FALSE(monster_half_dead(s.monsters[0]))
        << "ChangeState(REVIVE) cleared the bit";
    EXPECT_FALSE(monster_basically_dead(s.monsters[0]));
    EXPECT_FALSE(monster_dead_or_escaped(s.monsters[0]));
    EXPECT_EQ(monster_power(s, 0, PowerId::REGROW), 1)
        << "the pre-battle stack was cleared at half-death; the revival "
           "re-grants exactly one";
    EXPECT_NE(s.monsters[0].move_history[0], r::kDarklingMoveReincarnate)
        << "the post-revival roll takes the NORMAL tree, because the REVIVE "
           "clear resolves BEFORE it";

    // And the polluted history is what that first normal decision reads.
    EXPECT_EQ(s.monsters[0].move_history[1], r::kDarklingMoveReincarnate);
}

// The last Darkling down: the latch drops and every member dies synchronously,
// inside the one op_damage.
TEST(BeyondNormalsI, DarklingAllDeadDropsTheLatchAndKillsTheWholeGroup) {
    CombatState s = MakeDarklingGroup(31);
    for (uint8_t mi = 0; mi < 2; ++mi) {
        telegraph(s, mi, r::kDarklingMoveNip, MonsterIntent::ATTACK);
        player_attacks(s, mi, 500);
        drain(s);
        EXPECT_TRUE(monster_half_dead(s.monsters[mi]));
        EXPECT_NE(s.flags & kCombatFlagCannotLose, 0u)
            << "the latch stays up while any Darkling is still standing";
    }
    ASSERT_FALSE(monster_basically_dead(s.monsters[0]));

    player_attacks(s, 2, 500);

    EXPECT_EQ(s.flags & kCombatFlagCannotLose, 0u)
        << "cannotLose = false (Darkling.java:226)";
    for (uint8_t mi = 0; mi < 3; ++mi) {
        EXPECT_EQ(s.monsters[mi].hp, 0) << "slot " << mi;
        EXPECT_FALSE(monster_half_dead(s.monsters[mi])) << "slot " << mi;
        EXPECT_TRUE(monster_basically_dead(s.monsters[mi]))
            << "slot " << mi << " -- the fight must actually be able to end";
    }
    EXPECT_EQ(count_queued(s, Opcode::SET_MOVE), 0)
        << "the all-dead arm telegraphs nothing";
}

// The double-fire, spelled out. Relic onMonsterDeath fires TWICE per Darkling
// (half-death + the final sweep); power onDeath fires ONCE, because
// powers.clear() emptied the list in between. FLAGGED FOR ORACLE CONFIRMATION in
// the S2.25 Log -- this is a faithful transcription, not an independently
// verified game behaviour.
TEST(BeyondNormalsI, RelicOnMonsterDeathFiresTwicePerDarkling) {
    CombatState s = MakeDarklingGroup(31);
    s.relics[0].relic_id = static_cast<uint16_t>(RelicId::GREMLIN_HORN);
    s.relic_count = 1;

    int energy_grants = 0;
    for (uint8_t mi = 0; mi < 3; ++mi) {
        telegraph(s, mi, r::kDarklingMoveNip, MonsterIntent::ATTACK);
        player_attacks(s, mi, 500);
        energy_grants += count_queued(s, Opcode::GAIN_ENERGY);
        clear_queue(s);
    }
    // Half-deaths 1 and 2 each see a sibling still IN the fight and fire. The
    // third half-death fires too -- its two siblings are half-dead, which
    // areMonstersBasicallyDead counts as ALIVE -- and then the sweep fires once
    // more for each of the first two members (the last one to be swept has
    // nobody left in the fight, so Gremlin Horn's own guard stops it).
    EXPECT_EQ(energy_grants, 5)
        << "three half-death triggers plus two sweep triggers. The relic walk "
           "is the ONLY one that doubles: powers.clear() (Darkling.java:211) "
           "empties the list before the sweep can re-run the power walk";
}

// Feed on a hit that leaves a Darkling HALF-dead grants NO max HP: nothing died.
TEST(BeyondNormalsI, FeedOnAHalfKilledDarklingGrantsNoMaxHp) {
    CombatState s = MakeDarklingGroup(31);
    const int16_t max_before = s.player_max_hp;
    ActionQueueItem feed{};
    feed.opcode = static_cast<uint16_t>(Opcode::DAMAGE_FEED);
    feed.src = kActorPlayer;
    feed.tgt = 0;
    feed.amount = 500;
    feed.flags = 3;  // the second-operand max-HP gain
    execute_opcode(s, feed);
    EXPECT_EQ(s.monsters[0].hp, 0);
    EXPECT_TRUE(monster_half_dead(s.monsters[0]));
    EXPECT_EQ(s.player_max_hp, max_before)
        << "FeedAction.java:38's `|| halfDead` term is now LIVE";
}

// The targeting sense: RANDOM_ENEMY must never pick a half-dead Darkling, even
// though that Darkling is still taking turns.
TEST(BeyondNormalsI, RandomEnemySkipsAHalfDeadDarkling) {
    CombatState s = MakeDarklingGroup(31);
    telegraph(s, 0, r::kDarklingMoveNip, MonsterIntent::ATTACK);
    player_attacks(s, 0, 500);
    drain(s);
    ASSERT_TRUE(monster_half_dead(s.monsters[0]));
    const int16_t hp1 = s.monsters[1].hp;
    const int16_t hp2 = s.monsters[2].hp;

    for (int i = 0; i < 60; ++i) {
        ActionQueueItem it{};
        it.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
        it.src = kActorPlayer;
        it.tgt = kActorRandomEnemy;
        it.amount = 1;
        it.flags = make_damage_flags(DamageType::NORMAL);
        execute_opcode(s, it);
        drain(s);
    }
    EXPECT_EQ(s.monsters[0].hp, 0);
    EXPECT_LT(s.monsters[1].hp + s.monsters[2].hp, hp1 + hp2)
        << "the damage went somewhere";
    EXPECT_TRUE(monster_half_dead(s.monsters[0]));
}

// ============================================================================
// 7. The Exploder's fuse
// ============================================================================

TEST(BeyondNormalsI, ExplosiveTicksDownAndThenSuicidesBeforeTheBlast) {
    CombatState s = MakeSeeded(17);
    exploder_init(s, 0);
    exploder_use_pre_battle_action(s, 0);
    drain(s);
    ASSERT_EQ(monster_power(s, 0, PowerId::EXPLOSIVE), 3);

    // Turns 1 and 2: attack, then the fuse ticks.
    for (int turn = 1; turn <= 2; ++turn) {
        const int16_t player_before = s.player_hp;
        exploder_take_turn(s, 0);
        dispatch_during_turn(s, 0);
        drain(s);
        EXPECT_LT(s.player_hp, player_before) << "turn " << turn;
        EXPECT_EQ(monster_power(s, 0, PowerId::EXPLOSIVE), 3 - turn)
            << "turn " << turn;
        EXPECT_GT(s.monsters[0].hp, 0);
    }

    // Turn 3: move 2 does nothing, then the fuse is 1 -> Suicide, then 30.
    ASSERT_EQ(s.monsters[0].move_history[0], r::kExploderMoveBlock);
    const int16_t player_before = s.player_hp;
    exploder_take_turn(s, 0);
    dispatch_during_turn(s, 0);

    // Ordering, read straight off the queue: the ROLL_MOVE the turn queued, then
    // duringTurn's SUICIDE, then the DAMAGE.
    int suicide_at = -1;
    int damage_at = -1;
    for (uint8_t i = 0; i < s.action_count; ++i) {
        if (queued(s, i).opcode == static_cast<uint16_t>(Opcode::SUICIDE)) {
            suicide_at = i;
            EXPECT_EQ(queued(s, i).flags & 1u, 1u)
                << "the 1-arg SuicideAction defaults triggerRelics to TRUE "
                   "(SuicideAction.java:17-19) -- unlike the slime split's";
        }
        if (queued(s, i).opcode == static_cast<uint16_t>(Opcode::DAMAGE)) {
            damage_at = i;
        }
    }
    ASSERT_GE(suicide_at, 0);
    ASSERT_GE(damage_at, 0);
    EXPECT_LT(suicide_at, damage_at)
        << "the player takes the blast from an ALREADY-DEAD Exploder";
    EXPECT_EQ(queued(s, static_cast<uint8_t>(damage_at)).amount, 30);

    drain(s);
    EXPECT_EQ(s.monsters[0].hp, 0);
    EXPECT_EQ(player_before - s.player_hp, 30);
}

// The blast is THORNS-typed and pure: neither the owner's Strength nor the
// player's Vulnerable moves the 30. Block still absorbs it.
TEST(BeyondNormalsI, ExplosiveBlastIsThornsTypedAndUnscaled) {
    CombatState s = MakeSeeded(19);
    exploder_init(s, 0);
    give_monster_power(s, 0, PowerId::EXPLOSIVE, 1);
    give_monster_power(s, 0, PowerId::STRENGTH, 20);
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = kActorPlayer;
    apply.tgt = kActorPlayer;
    apply.amount = 5;
    apply.flags = make_apply_power_flags(PowerId::VULNERABLE);
    execute_opcode(s, apply);

    const int16_t before = s.player_hp;
    dispatch_during_turn(s, 0);
    drain(s);
    EXPECT_EQ(before - s.player_hp, 30)
        << "THORNS skips every NORMAL-only modifier on both sides";
}

// The isDying guard: an Exploder already killed this turn does NOT explode.
TEST(BeyondNormalsI, ExplosiveDoesNotDetonateForAnAlreadyDyingOwner) {
    CombatState s = MakeSeeded(23);
    exploder_init(s, 0);
    give_monster_power(s, 0, PowerId::EXPLOSIVE, 1);
    s.monsters[0].hp = 0;  // isDying
    const int16_t before = s.player_hp;
    dispatch_during_turn(s, 0);
    EXPECT_EQ(count_queued(s, Opcode::SUICIDE), 0);
    EXPECT_EQ(count_queued(s, Opcode::DAMAGE), 0);
    EXPECT_EQ(count_queued(s, Opcode::REDUCE_POWER), 1)
        << "the else arm still runs -- the guard is on the detonation only";
    drain(s);
    EXPECT_EQ(s.player_hp, before);
}

// ============================================================================
// 8. Encounters -- compositions, and the un-parking
// ============================================================================

std::vector<std::string> members(const ResolvedGroup& g) {
    std::vector<std::string> out;
    for (uint8_t i = 0; i < g.count; ++i) {
        out.emplace_back(g.members[i]);
    }
    return out;
}

// The un-park gate is `monster_init_fn(id) == nullptr`, asked of the dispatch
// switch directly -- the Looter's precedent. Registering this batch's five inits
// un-parks all six groups with no run-layer edit, INCLUDING "Sphere and 2
// Shapes", which S2.21 landed the SphericGuardian for and parked on the shapes.
TEST(BeyondNormalsI, AllSixBeyondGroupsGoLive) {
    for (const char* key : {"3 Darklings", "Orb Walker", "3 Shapes", "4 Shapes",
                            "Sphere and 2 Shapes", "2 Orb Walkers"}) {
        for (int64_t seed = 0; seed < 12; ++seed) {
            RngStream misc = from_seed(seed);
            ResolvedGroup group{};
            ASSERT_TRUE(resolve_encounter(key, misc, group)) << key;
            ASSERT_GT(group.count, 0) << key;
            for (uint8_t i = 0; i < group.count; ++i) {
                const MonsterId id = static_cast<MonsterId>(
                    r::monster_from_game_id(group.members[i]));
                EXPECT_NE(id, MonsterId::NONE) << key << " -> "
                                               << group.members[i];
                EXPECT_NE(monster_init_fn(id), nullptr)
                    << key << " -> " << group.members[i] << " is still parked";
            }
        }
    }
}

// "3 Darklings" is the s2-design section 5 trap-8 row: the SAME key in both
// pools, two encounter rows, one group. Spawn-order-exact, and 6 HP draws.
TEST(BeyondNormalsI, ThreeDarklingsIsThreeDarklingsAndSixHpDraws) {
    RngStream misc = from_seed(77);
    ResolvedGroup group{};
    ASSERT_TRUE(resolve_encounter("3 Darklings", misc, group));
    EXPECT_EQ(members(group),
              (std::vector<std::string>{"Darkling", "Darkling", "Darkling"}));

    CombatState s = MakeSeeded(77, 3);
    const MonsterId ids[] = {MonsterId::DARKLING, MonsterId::DARKLING,
                             MonsterId::DARKLING};
    spawn_group(s, ids);
    EXPECT_EQ(s.monster_hp_rng.counter, 6)
        << "hp, nip / hp, nip / hp, nip -- in slot order";
    EXPECT_EQ(s.ai_rng.counter, 3);
}

// "Sphere and 2 Shapes" (encounters.yaml id 51): two getAncientShape picks and
// then a SphericGuardian, spawn-order-exact -- and now fully spawnable, which is
// what S2.21 parked. The SphericGuardian draws NO HP value, so a group of three
// costs exactly two monster_hp_rng draws.
TEST(BeyondNormalsI, SphereAndTwoShapesSpawnsAndCostsOnlyTheShapesHpDraws) {
    const std::string kByIndex[3] = {"Spiker", "Repulsor", "Exploder"};
    bool seen[3] = {false, false, false};
    for (int64_t seed = 0; seed < 40; ++seed) {
        RngStream ref = from_seed(seed);
        std::vector<std::string> expected;
        for (int i = 0; i < 2; ++i) {
            const int32_t r0 = random(ref, 2);
            seen[r0] = true;
            expected.push_back(kByIndex[r0]);
        }
        expected.emplace_back("SphericGuardian");

        RngStream misc = from_seed(seed);
        ResolvedGroup group{};
        ASSERT_TRUE(resolve_encounter("Sphere and 2 Shapes", misc, group));
        ASSERT_EQ(members(group), expected) << "seed=" << seed;

        CombatState s = MakeSeeded(seed, group.count);
        MonsterId ids[kMonsterCap]{};
        for (uint8_t i = 0; i < group.count; ++i) {
            ids[i] = static_cast<MonsterId>(
                r::monster_from_game_id(group.members[i]));
        }
        spawn_group(s, std::span<const MonsterId>(ids, group.count));
        use_pre_battle_actions(s);
        drain(s);

        EXPECT_EQ(s.monster_hp_rng.counter, 2)
            << "seed=" << seed
            << " -- each ancient shape draws ONE, and the SphericGuardian "
               "draws NONE (its ctor never calls setHp)";
        EXPECT_EQ(s.ai_rng.counter, 3) << "one init rollMove per member";
        for (uint8_t i = 0; i < group.count; ++i) {
            EXPECT_GT(s.monsters[i].hp, 0) << "seed=" << seed << " slot " << i;
        }
        // The sphere's pre-battle Barricade/Artifact/40-block still lands, and
        // a Spiker member still opens on 7 Thorns.
        for (uint8_t i = 0; i < group.count; ++i) {
            if (s.monsters[i].monster_id ==
                static_cast<uint16_t>(MonsterId::SPIKER)) {
                EXPECT_EQ(monster_power(s, i, PowerId::THORNS), 7);
            }
            if (s.monsters[i].monster_id ==
                static_cast<uint16_t>(MonsterId::EXPLODER)) {
                EXPECT_EQ(monster_power(s, i, PowerId::EXPLOSIVE), 3);
            }
        }
    }
    EXPECT_TRUE(seen[0] && seen[1] && seen[2])
        << "all three ancient shapes must be reachable";
}

// "3 Shapes" / "4 Shapes" are draw-WITHOUT-replacement over a 6-slot pool, so a
// 4-Shapes group can hold two of the same shape but never three.
TEST(BeyondNormalsI, ShapeGroupsSpawnAndNeverHoldMoreThanTwoOfAKind) {
    for (int64_t seed = 0; seed < 40; ++seed) {
        for (const char* key : {"3 Shapes", "4 Shapes"}) {
            RngStream misc = from_seed(seed);
            ResolvedGroup group{};
            ASSERT_TRUE(resolve_encounter(key, misc, group));
            int counts[3] = {0, 0, 0};
            MonsterId ids[kMonsterCap]{};
            for (uint8_t i = 0; i < group.count; ++i) {
                const std::string name(group.members[i]);
                if (name == "Repulsor") {
                    ++counts[0];
                } else if (name == "Exploder") {
                    ++counts[1];
                } else if (name == "Spiker") {
                    ++counts[2];
                } else {
                    ADD_FAILURE() << key << " -> " << name;
                }
                ids[i] = static_cast<MonsterId>(
                    r::monster_from_game_id(group.members[i]));
            }
            for (int c : counts) {
                EXPECT_LE(c, 2) << key << " seed=" << seed
                                << " -- the pool holds two of each";
            }
            CombatState s = MakeSeeded(seed, group.count);
            spawn_group(s, std::span<const MonsterId>(ids, group.count));
            use_pre_battle_actions(s);
            drain(s);
            EXPECT_EQ(s.monster_hp_rng.counter, static_cast<int32_t>(group.count))
                << key << " -- one setHp draw per shape";
        }
    }
}

}  // namespace
}  // namespace sts::engine
