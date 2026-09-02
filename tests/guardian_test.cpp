// Tier-2 The Guardian coverage (B3.21).
//
// Named cases pin the ascension columns (incl. the A19 threshold that is the
// live one at the engine's fixed A20), the forced opening Charge Up and its
// single ignored aiRng draw, both move cycles, the mode flip at the EXACT
// cumulative-damage threshold INCLUDING its per-cycle growth (40 -> 50 -> 60 at
// A20), Sharp Hide firing only on attack plays, and the Spot Weakness
// ATTACK_BUFF classification that the Twin Slam telegraph uncovered.
//
// Provenance: TheGuardian.java:92-120,122-132,134-169,171-223,226-232,234-273,
// 275-292; ModeShiftPower.java:11-31; SharpHidePower.java:22-50;
// AbstractMonster.java:99,431-437,451-463,465-467; SpotWeaknessAction.java:32-40.

#include <cstdint>
#include <span>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_guardian.hpp"
#include "sts/engine/power_hooks.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

// TheGuardian.java:78-84.
constexpr uint8_t kCloseUp = 1;
constexpr uint8_t kFierceBash = 2;
constexpr uint8_t kRollAttack = 3;
constexpr uint8_t kTwinSlam = 4;
constexpr uint8_t kWhirlwind = 5;
constexpr uint8_t kChargeUp = 6;
constexpr uint8_t kVentSteam = 7;

// The A20 sheet the engine actually plays (kMonsterAscension == 20).
constexpr int32_t kA20Hp = 250;
constexpr int32_t kA20Threshold = 40;

CombatState make_guardian_state(int64_t seed, int16_t player_hp = 500) {
    CombatState s{};
    s.player_hp = player_hp;
    s.player_max_hp = player_hp;
    s.monster_count = 1;
    s.monster_hp_rng = from_seed(seed);
    s.ai_rng = from_seed(seed);
    guardian_init(s, 0);
    guardian_use_pre_battle_action(s, 0);
    return s;
}

void drain_actions(CombatState& s) {
    while (s.action_count > 0) {
        ActionQueueItem item{};
        ASSERT_TRUE(pop_action_front(s, item));
        execute_opcode(s, item);
    }
}

ActionQueueItem damage_item(uint8_t target, int32_t amount) {
    ActionQueueItem item{};
    item.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    item.src = kActorPlayer;
    item.tgt = target;
    item.amount = amount;
    item.flags = make_damage_flags(DamageType::NORMAL);
    return item;
}

// Land `amount` of unblocked player damage on the Guardian and settle whatever
// the damage() override queued.
void hit_guardian(CombatState& s, int32_t amount) {
    execute_opcode(s, damage_item(0, amount));
    drain_actions(s);
}

const PowerSlot* find_monster_power(const CombatState& s, uint8_t mi,
                                    PowerId id) {
    for (uint8_t i = 0; i < s.monsters[mi].power_count; ++i) {
        if (s.monsters[mi].powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.monsters[mi].powers[i];
        }
    }
    return nullptr;
}

const PowerSlot* find_player_power(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.player_powers[i];
        }
    }
    return nullptr;
}

// Run the Guardian's queued turn through the real pump.
void take_one_turn(CombatState& s) {
    s.monster_attacks_queued = 1;
    s.monster_queue[0] = MonsterQueueItem{0, 0};
    s.monster_queue_count = 1;
    s.turn_has_ended = 0;
    for (;;) {
        const PumpStepResult r = pump_step(s, dispatch_monster_turn);
        if (r.outcome == PumpOutcome::COMBAT_OVER ||
            r.outcome == PumpOutcome::WAITING_ON_USER) {
            return;
        }
    }
}

// ===========================================================================
// Registry
// ===========================================================================

TEST(GuardianRegistry, NativeEntryIntentsAndAscensionColumnsMatchJava) {
    namespace r = sts::registry;

    EXPECT_EQ(static_cast<int>(r::MonsterId::THE_GUARDIAN), 21)
        << "allocated from a reserved block; ids 12-20 are a legal gap";
    EXPECT_EQ(static_cast<int>(r::PowerId::MODE_SHIFT), 45);
    EXPECT_EQ(static_cast<int>(r::PowerId::SHARP_HIDE), 46);
    // Appended above Lagavulin's SLEEP=9 / STUN=10 rather than into them; ids
    // are unique and append-only, never required to be contiguous.
    EXPECT_EQ(static_cast<uint8_t>(r::MonsterIntent::DEFEND), 11);
    EXPECT_EQ(static_cast<uint8_t>(r::MonsterIntent::ATTACK_BUFF), 12);

    EXPECT_EQ(r::monster_game_id(r::MonsterId::THE_GUARDIAN), "TheGuardian");
    EXPECT_EQ(r::monster_from_game_id("TheGuardian"),
              r::MonsterId::THE_GUARDIAN);
    EXPECT_EQ(r::power_game_id(r::PowerId::MODE_SHIFT), "Mode Shift");
    EXPECT_EQ(r::power_game_id(r::PowerId::SHARP_HIDE), "Sharp Hide");

    const r::MonsterDef& g = r::kTheGuardian;
    EXPECT_TRUE(g.ai_native);
    EXPECT_EQ(g.roll_count, 0) << "setHp(int) everywhere: no monsterHpRng draw";
    EXPECT_EQ(g.enemy_type, r::MonsterEnemyType::BOSS);  // TheGuardian.java:94
    EXPECT_TRUE(g.is_boss());

    // The ctor's DESCENDING chain (TheGuardian.java:97-106): A19+ and A9+ share
    // 250 HP and differ only in the mode-shift threshold, so the a19 HP column
    // deliberately repeats 250.
    EXPECT_EQ(g.hp_min(8), 240);
    EXPECT_EQ(g.hp_max(8), 240);
    EXPECT_EQ(g.hp_min(9), 250);
    EXPECT_EQ(g.hp_min(19), 250);
    EXPECT_EQ(g.hp_max(20), kA20Hp);

    struct MoveRow {
        uint8_t id;
        r::MonsterIntent intent;
        uint8_t effects;
    };
    const MoveRow rows[] = {
        {kCloseUp, r::MonsterIntent::BUFF, 1},
        {kFierceBash, r::MonsterIntent::ATTACK, 1},
        {kRollAttack, r::MonsterIntent::ATTACK, 1},
        {kTwinSlam, r::MonsterIntent::ATTACK_BUFF, 3},
        {kWhirlwind, r::MonsterIntent::ATTACK, 4},
        {kChargeUp, r::MonsterIntent::DEFEND, 1},
        {kVentSteam, r::MonsterIntent::STRONG_DEBUFF, 2},
    };
    for (const MoveRow& row : rows) {
        const r::MonsterMove* mv = g.move(row.id);
        ASSERT_NE(mv, nullptr) << "move id " << static_cast<int>(row.id);
        EXPECT_EQ(mv->intent, row.intent) << "move id " << static_cast<int>(row.id);
        EXPECT_EQ(mv->effect_count, row.effects)
            << "move id " << static_cast<int>(row.id);
    }

    // Close Up: Sharp Hide 3, thornsDamage + 1 == 4 at A19+ (:74,185-189).
    const r::MonsterMove* close_up = g.move(kCloseUp);
    EXPECT_EQ(close_up->effects[0].op, r::Opcode::APPLY_POWER);
    EXPECT_EQ(close_up->effects[0].target, r::MonsterMoveTarget::SELF);
    EXPECT_EQ(close_up->effects[0].extra & 0xFFFFu,
              static_cast<uint32_t>(r::PowerId::SHARP_HIDE));
    EXPECT_EQ(close_up->effects[0].amount.at(18), 3);
    EXPECT_EQ(close_up->effects[0].amount.at(19), 4);

    // Fierce Bash 32 / A4 36 and Roll 9 / A4 10 -- the branch is `>= 4`
    // (TheGuardian.java:107), NOT the `A_2_` the field names suggest.
    EXPECT_EQ(g.move(kFierceBash)->effects[0].amount.at(3), 32);
    EXPECT_EQ(g.move(kFierceBash)->effects[0].amount.at(4), 36);
    EXPECT_EQ(g.move(kRollAttack)->effects[0].amount.at(3), 9);
    EXPECT_EQ(g.move(kRollAttack)->effects[0].amount.at(4), 10);

    // Whirlwind is FOUR separate 5-damage hits, not one 20 (:207-214).
    const r::MonsterMove* whirl = g.move(kWhirlwind);
    for (uint8_t i = 0; i < 4; ++i) {
        EXPECT_EQ(whirl->effects[i].op, r::Opcode::DAMAGE);
        EXPECT_EQ(whirl->effects[i].amount.at(20), 5);
    }

    // Twin Slam: two 8s then the Sharp Hide removal (:193-199).
    const r::MonsterMove* twin = g.move(kTwinSlam);
    EXPECT_EQ(twin->effects[0].amount.at(20), 8);
    EXPECT_EQ(twin->effects[1].amount.at(20), 8);
    EXPECT_EQ(twin->effects[2].op, r::Opcode::REMOVE_POWER);
    EXPECT_EQ(twin->effects[2].extra & 0xFFFFu,
              static_cast<uint32_t>(r::PowerId::SHARP_HIDE));

    // Vent Steam applies Weak BEFORE Vulnerable (:177-180).
    const r::MonsterMove* vent = g.move(kVentSteam);
    EXPECT_EQ(vent->effects[0].extra & 0xFFFFu,
              static_cast<uint32_t>(r::PowerId::WEAK));
    EXPECT_EQ(vent->effects[1].extra & 0xFFFFu,
              static_cast<uint32_t>(r::PowerId::VULNERABLE));
    EXPECT_EQ(vent->effects[0].amount.at(20), 2);
    EXPECT_EQ(vent->effects[1].amount.at(20), 2);

    // Charge Up: a flat 9 block, no ascension branch (:73,219).
    EXPECT_EQ(g.move(kChargeUp)->effects[0].op, r::Opcode::BLOCK);
    EXPECT_EQ(g.move(kChargeUp)->effects[0].amount.at(0), 9);
    EXPECT_EQ(g.move(kChargeUp)->effects[0].amount.at(20), 9);

    EXPECT_EQ(monster_init_fn(MonsterId::THE_GUARDIAN), &guardian_init);
    EXPECT_EQ(monster_turn_fn(MonsterId::THE_GUARDIAN), &guardian_take_turn);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::THE_GUARDIAN),
              &guardian_use_pre_battle_action);

    RngStream misc = from_seed(21001);
    ResolvedGroup group{};
    ASSERT_TRUE(resolve_encounter("The Guardian", misc, group));
    ASSERT_EQ(group.count, 1);
    EXPECT_EQ(group.members[0], "TheGuardian");
}

// The A19 column is the acceptance-named one, and it is also the live column at
// the engine's fixed A20 -- so every other number in this file depends on it.
TEST(GuardianRegistry, ModeShiftThresholdColumnsIncludingA19) {
    EXPECT_EQ(guardian_base_mode_shift_threshold(0), 30);   // :103-106
    EXPECT_EQ(guardian_base_mode_shift_threshold(8), 30);
    EXPECT_EQ(guardian_base_mode_shift_threshold(9), 35);   // :100-102
    EXPECT_EQ(guardian_base_mode_shift_threshold(18), 35);
    EXPECT_EQ(guardian_base_mode_shift_threshold(19), 40);  // :97-99
    EXPECT_EQ(guardian_base_mode_shift_threshold(20), kA20Threshold);
    EXPECT_EQ(guardian_base_mode_shift_threshold(kMonsterAscension),
              kA20Threshold)
        << "the engine plays A20, so 40 is the live starting threshold";
}

// ===========================================================================
// Init + pre-battle
// ===========================================================================

TEST(GuardianInit, DegenerateHpDrawOneIgnoredAiRollAndModeShiftAtPreBattle) {
    CombatState s{};
    s.monster_count = 1;
    s.monster_hp_rng = from_seed(21002);
    s.ai_rng = from_seed(21002);
    // setHp(250) (TheGuardian.java:97-106) is setHp(hp, hp)
    // (AbstractMonster.java:777-779) -> monsterHpRng.random(min,max) (:765-767),
    // which increments and advances the stream even on a degenerate range
    // (Random.java:58-61). Fixed sheet, real draw.
    RngStream hp_expected = s.monster_hp_rng;
    ASSERT_EQ(random(hp_expected, kA20Hp, kA20Hp), kA20Hp)
        << "a degenerate range can only return its endpoint";

    guardian_init(s, 0);

    const MonsterState& g = s.monsters[0];
    EXPECT_EQ(g.monster_id, static_cast<uint16_t>(MonsterId::THE_GUARDIAN));
    EXPECT_EQ(g.hp, kA20Hp);
    EXPECT_EQ(g.max_hp, kA20Hp);
    EXPECT_EQ(s.monster_hp_rng.counter, hp_expected.counter)
        << "setHp(int) still draws once: fixed sheet, consumed stream";
    EXPECT_EQ(s.monster_hp_rng.s0, hp_expected.s0);
    EXPECT_EQ(s.monster_hp_rng.s1, hp_expected.s1);
    EXPECT_EQ(s.ai_rng.counter, 1)
        << "init rollMove consumes random(99); getMove then ignores num";
    EXPECT_EQ(g.move_history[0], kChargeUp)
        << "isOpen is true at construction, so getMove forces Charge Up (:227-228)";
    EXPECT_EQ(g.intent, static_cast<uint8_t>(MonsterIntent::DEFEND));

    // Mode Shift belongs to usePreBattleAction (:129), not the ctor.
    EXPECT_EQ(g.power_count, 0);
    guardian_use_pre_battle_action(s, 0);
    ASSERT_EQ(g.power_count, 1);
    const PowerSlot* ms = find_monster_power(s, 0, PowerId::MODE_SHIFT);
    ASSERT_NE(ms, nullptr);
    EXPECT_EQ(ms->amount, kA20Threshold)
        << "ModeShiftPower(this, dmgThreshold) (:129, ModeShiftPower.java:18-25)";
    EXPECT_EQ(guardian_mode_shift_threshold(g), kA20Threshold);
}

// ===========================================================================
// THE acceptance case: the flip point, and the fact that it GROWS
// ===========================================================================

TEST(GuardianModeShift, FlipsAtExactCumulativeThresholdWithPerCycleGrowth) {
    CombatState s = make_guardian_state(21003);
    MonsterState& g = s.monsters[0];

    // --- cycle 1: threshold 40 ---
    hit_guardian(s, 39);
    EXPECT_EQ(g.hp, kA20Hp - 39);
    EXPECT_EQ(g.move_history[0], kChargeUp) << "39 < 40: no flip yet";
    ASSERT_NE(find_monster_power(s, 0, PowerId::MODE_SHIFT), nullptr);
    EXPECT_EQ(find_monster_power(s, 0, PowerId::MODE_SHIFT)->amount, 1)
        << "Mode Shift shows the REMAINING damage (ModeShiftPower.java:29)";

    hit_guardian(s, 1);  // cumulative 40 == threshold
    EXPECT_EQ(g.move_history[0], kCloseUp);
    EXPECT_EQ(g.intent, static_cast<uint8_t>(MonsterIntent::BUFF));
    EXPECT_EQ(find_monster_power(s, 0, PowerId::MODE_SHIFT), nullptr)
        << "RemoveSpecificPowerAction(\"Mode Shift\") (:238)";
    EXPECT_EQ(g.block, 20) << "DEFENSIVE_BLOCK (:72,240)";
    EXPECT_EQ(guardian_mode_shift_threshold(g), kA20Threshold + 10)
        << "dmgThreshold += dmgThresholdIncrease on the flip (:61,245)";

    // Damage taken while CLOSED must not count toward the next flip: the guard
    // is `isOpen && ...` (:279), and isOpen is false until Twin Slam.
    hit_guardian(s, 60);
    EXPECT_EQ(g.move_history[0], kCloseUp) << "closed: no second flip";

    // Walk the defensive cycle back to Offensive Mode.
    take_one_turn(s);  // CLOSE_UP  -> Sharp Hide, telegraph Roll Attack
    EXPECT_EQ(g.move_history[0], kRollAttack);
    take_one_turn(s);  // ROLL      -> telegraph Twin Slam
    EXPECT_EQ(g.move_history[0], kTwinSlam);
    take_one_turn(s);  // TWIN_SLAM -> back to Offensive Mode
    EXPECT_EQ(g.move_history[0], kWhirlwind);

    // --- cycle 2: threshold 50, and the accumulator restarted from zero ---
    const PowerSlot* ms2 = find_monster_power(s, 0, PowerId::MODE_SHIFT);
    ASSERT_NE(ms2, nullptr) << "ApplyPowerAction(ModeShiftPower) on re-opening (:254)";
    EXPECT_EQ(ms2->amount, kA20Threshold + 10)
        << "re-applied at the GROWN threshold, not the original 40";

    hit_guardian(s, 49);
    EXPECT_EQ(g.move_history[0], kWhirlwind) << "49 < 50: no flip";
    EXPECT_EQ(find_monster_power(s, 0, PowerId::MODE_SHIFT)->amount, 1);

    hit_guardian(s, 1);  // cumulative 50 == the grown threshold
    EXPECT_EQ(g.move_history[0], kCloseUp);
    EXPECT_EQ(guardian_mode_shift_threshold(g), kA20Threshold + 20)
        << "and it grows again: 40 -> 50 -> 60";
}

// Walk the defensive cycle (Close Up -> Roll Attack -> Twin Slam) back to
// Offensive Mode. Twin Slam is what re-opens the Guardian and re-applies Mode
// Shift at the GROWN threshold (TheGuardian.java:194,253-266).
void walk_defensive_cycle_back_to_offensive(CombatState& s) {
    ASSERT_EQ(s.monsters[0].move_history[0], kCloseUp);
    take_one_turn(s);  // CLOSE_UP  -> telegraph Roll Attack
    take_one_turn(s);  // ROLL      -> telegraph Twin Slam
    take_one_turn(s);  // TWIN_SLAM -> back to Offensive Mode
    ASSERT_EQ(s.monsters[0].move_history[0], kWhirlwind);
}

// The acceptance names the thresholds "40, 50, 60 ...", so DRIVE all three
// flips rather than only asserting that the third threshold computes to 60:
// dmgThresholdIncrease is applied on EVERY Defensive-Mode entry
// (TheGuardian.java:61,245), so the growth has to compound, not fire once.
// Each cycle also proves the accumulator restarts: N-1 damage must not flip.
TEST(GuardianModeShift, ThreeFlipsRequireFortyThenFiftyThenSixty) {
    CombatState s = make_guardian_state(21015);
    MonsterState& g = s.monsters[0];

    const int32_t thresholds[] = {kA20Threshold, kA20Threshold + 10,
                                  kA20Threshold + 20};  // 40, 50, 60
    int32_t cumulative = 0;
    for (int cycle = 0; cycle < 3; ++cycle) {
        const int32_t need = thresholds[cycle];
        SCOPED_TRACE(testing::Message() << "cycle " << cycle << ", threshold " << need);

        // The opener is Charge Up; every later cycle re-enters at Whirlwind,
        // where Twin Slam rejoins the offensive cycle (:198).
        EXPECT_EQ(g.move_history[0], cycle == 0 ? kChargeUp : kWhirlwind);
        EXPECT_EQ(guardian_mode_shift_threshold(g), need);
        const PowerSlot* ms = find_monster_power(s, 0, PowerId::MODE_SHIFT);
        ASSERT_NE(ms, nullptr);
        EXPECT_EQ(ms->amount, need) << "Mode Shift is applied AT the threshold";

        hit_guardian(s, need - 1);
        EXPECT_NE(g.move_history[0], kCloseUp) << "one short of the threshold";
        EXPECT_EQ(find_monster_power(s, 0, PowerId::MODE_SHIFT)->amount, 1);

        hit_guardian(s, 1);  // the exact cumulative threshold
        EXPECT_EQ(g.move_history[0], kCloseUp);
        EXPECT_EQ(g.block, 20) << "DEFENSIVE_BLOCK (:72,240)";
        EXPECT_EQ(find_monster_power(s, 0, PowerId::MODE_SHIFT), nullptr);

        cumulative += need;
        EXPECT_EQ(g.hp, kA20Hp - cumulative);
        EXPECT_EQ(guardian_mode_shift_threshold(g), need + 10)
            << "dmgThresholdIncrease compounds on every flip (:61,245)";

        walk_defensive_cycle_back_to_offensive(s);
    }
    EXPECT_EQ(cumulative, 150) << "40 + 50 + 60";
    EXPECT_EQ(g.hp, kA20Hp - 150);
    EXPECT_EQ(guardian_mode_shift_threshold(g), kA20Threshold + 30)
        << "the fourth window would need 70";
}

// The one window the accumulator is deliberately NOT derived from
// ModeShiftPower.amount. changeState "Offensive Mode" sets isOpen SYNCHRONOUSLY
// (:262) but only QUEUES the new ModeShiftPower and the "Reset Threshold" that
// zeroes dmgTaken (:254-255). A player with Thorns reflects into that gap --
// Java counts it into dmgTaken and then DISCARDS it when Reset Threshold
// resolves, so the next window must still need the FULL grown threshold.
TEST(GuardianModeShift, ThornsReflectedIntoTheReopenGapIsDiscarded) {
    CombatState s = make_guardian_state(21016);
    MonsterState& g = s.monsters[0];

    hit_guardian(s, kA20Threshold);  // flip 1: threshold grows to 50
    ASSERT_EQ(g.move_history[0], kCloseUp);
    take_one_turn(s);  // CLOSE_UP -> telegraph Roll Attack
    take_one_turn(s);  // ROLL     -> telegraph Twin Slam
    ASSERT_EQ(g.move_history[0], kTwinSlam);

    // Thorns 5: each of Twin Slam's two hits reflects 5 back at the Guardian,
    // and ThornsPower.onAttacked adds to the TOP (power_thorns.cpp), so both
    // land inside the gap -- after isOpen is set, before Mode Shift is applied.
    s.player_powers[s.player_power_count].power_id =
        static_cast<uint16_t>(PowerId::THORNS);
    s.player_powers[s.player_power_count].amount = 5;
    ++s.player_power_count;

    // In ordinary play the DEFENSIVE_BLOCK of 20 is still up when Twin Slam
    // swings (it is dropped by the LoseBlockAction queued AFTER the slams,
    // :256-258), so it absorbs both reflects and no HP is lost -- in which case
    // Java's `tmpHealth > currentHealth` guard means nothing accumulates
    // either, and the gate under test never has to do anything. Clear it so the
    // reflect actually reaches HP, which is the case the gate exists for.
    g.block = 0;

    const int16_t hp_before_twin_slam = g.hp;
    take_one_turn(s);  // TWIN_SLAM

    EXPECT_EQ(hp_before_twin_slam - g.hp, 10) << "two 5-point reflects landed";
    const PowerSlot* ms = find_monster_power(s, 0, PowerId::MODE_SHIFT);
    ASSERT_NE(ms, nullptr);
    EXPECT_EQ(ms->amount, kA20Threshold + 10)
        << "the reflected 10 is DISCARDED by Reset Threshold, not counted";

    // And the discard is real, not just cosmetic on the power: the next window
    // still takes the full 50.
    s.player_power_count = 0;  // drop Thorns so later hits are plain damage
    hit_guardian(s, 49);
    EXPECT_EQ(g.move_history[0], kWhirlwind) << "49 < 50 even after the reflect";
    hit_guardian(s, 1);
    EXPECT_EQ(g.move_history[0], kCloseUp);
}

// One hit larger than the threshold must flip exactly once, and a multi-hit
// attack must not flip twice before the queued change of state resolves --
// that is what closeUpTriggered is for (:77,289).
TEST(GuardianModeShift, OvershootAndMultiHitFlipExactlyOnce) {
    CombatState s = make_guardian_state(21004);
    MonsterState& g = s.monsters[0];

    execute_opcode(s, damage_item(0, 100));  // 100 >> 40
    EXPECT_EQ(g.hp, kA20Hp - 100);
    // Three more hits land while the change of state is still queued.
    execute_opcode(s, damage_item(0, 5));
    execute_opcode(s, damage_item(0, 5));
    execute_opcode(s, damage_item(0, 5));
    drain_actions(s);

    EXPECT_EQ(g.move_history[0], kCloseUp);
    EXPECT_EQ(g.move_history[1], kChargeUp)
        << "exactly ONE queued SetMoveAction was pushed";
    EXPECT_EQ(g.block, 20) << "one GainBlockAction(20), not four";
    EXPECT_EQ(guardian_mode_shift_threshold(g), kA20Threshold + 10)
        << "one threshold growth, not four";
}

// A fully-blocked hit is not damage taken: `tmpHealth > currentHealth` (:279).
TEST(GuardianModeShift, FullyBlockedHitDoesNotAccumulate) {
    CombatState s = make_guardian_state(21005);
    MonsterState& g = s.monsters[0];
    g.block = 100;

    hit_guardian(s, 39);
    EXPECT_EQ(g.hp, kA20Hp);
    EXPECT_EQ(g.block, 61);
    EXPECT_EQ(find_monster_power(s, 0, PowerId::MODE_SHIFT)->amount,
              kA20Threshold)
        << "no HP lost -> Mode Shift untouched";
    EXPECT_EQ(g.move_history[0], kChargeUp);
}

// A lethal hit must not telegraph a flip: the guard also requires !isDying (:279).
TEST(GuardianModeShift, LethalHitDoesNotFlip) {
    CombatState s = make_guardian_state(21006);
    s.monsters[0].hp = 30;
    execute_opcode(s, damage_item(0, 99));
    EXPECT_EQ(s.monsters[0].hp, 0);
    EXPECT_EQ(s.action_count, 0);
    EXPECT_EQ(s.monsters[0].move_history[0], kChargeUp);
}

// ===========================================================================
// Move cycles
// ===========================================================================

TEST(GuardianCycles, OffensiveCycleOrderAndA20Effects) {
    CombatState s = make_guardian_state(21007);
    MonsterState& g = s.monsters[0];
    const int32_t ai_after_init = s.ai_rng.counter;

    take_one_turn(s);  // CHARGE_UP: block 9
    EXPECT_EQ(g.block, 9);
    EXPECT_EQ(g.move_history[0], kFierceBash);
    EXPECT_EQ(g.intent, static_cast<uint8_t>(MonsterIntent::ATTACK));

    take_one_turn(s);  // FIERCE_BASH: A4+ 36
    EXPECT_EQ(s.player_hp, 500 - 36);
    EXPECT_EQ(g.move_history[0], kVentSteam);
    EXPECT_EQ(g.intent, static_cast<uint8_t>(MonsterIntent::STRONG_DEBUFF));

    take_one_turn(s);  // VENT_STEAM: Weak 2 + Vulnerable 2
    ASSERT_NE(find_player_power(s, PowerId::WEAK), nullptr);
    ASSERT_NE(find_player_power(s, PowerId::VULNERABLE), nullptr);
    EXPECT_EQ(find_player_power(s, PowerId::WEAK)->amount, 2);
    EXPECT_EQ(find_player_power(s, PowerId::VULNERABLE)->amount, 2);
    EXPECT_EQ(g.move_history[0], kWhirlwind);

    // WHIRLWIND: four SEPARATE 5s. The player is Vulnerable, so each hit is
    // floor(5 * 1.5) == 7 -- 28 total, where one 20-damage hit would have been
    // 30. That difference is why the four hits are four registry steps.
    const int16_t hp_before = s.player_hp;
    take_one_turn(s);
    EXPECT_EQ(hp_before - s.player_hp, 28);
    EXPECT_EQ(g.move_history[0], kChargeUp)
        << "and the offensive cycle closes back onto Charge Up (:215)";
    EXPECT_EQ(g.intent, static_cast<uint8_t>(MonsterIntent::DEFEND));

    EXPECT_EQ(s.ai_rng.counter, ai_after_init)
        << "every transition after the opener is a direct setMove: no aiRng draws";
}

TEST(GuardianCycles, DefensiveCycleAppliesAndRemovesSharpHide) {
    CombatState s = make_guardian_state(21008);
    MonsterState& g = s.monsters[0];
    hit_guardian(s, kA20Threshold);  // flip into Defensive Mode
    ASSERT_EQ(g.move_history[0], kCloseUp);
    const int16_t block_after_flip = g.block;
    ASSERT_EQ(block_after_flip, 20);

    take_one_turn(s);  // CLOSE_UP: Sharp Hide 4 at A19+ (:185-189)
    const PowerSlot* hide = find_monster_power(s, 0, PowerId::SHARP_HIDE);
    ASSERT_NE(hide, nullptr);
    EXPECT_EQ(hide->amount, 4);
    EXPECT_EQ(g.move_history[0], kRollAttack);

    take_one_turn(s);  // ROLL_ATTACK: A4+ 10
    EXPECT_EQ(s.player_hp, 500 - 10);
    EXPECT_EQ(g.move_history[0], kTwinSlam);
    EXPECT_EQ(g.intent, static_cast<uint8_t>(MonsterIntent::ATTACK_BUFF));

    take_one_turn(s);  // TWIN_SLAM: 2 x 8, drop Sharp Hide, re-open
    EXPECT_EQ(s.player_hp, 500 - 10 - 16);
    EXPECT_EQ(find_monster_power(s, 0, PowerId::SHARP_HIDE), nullptr)
        << "RemoveSpecificPowerAction(\"Sharp Hide\") (:197)";
    EXPECT_EQ(g.block, 0) << "LoseBlockAction(currentBlock) on re-opening (:256-258)";
    ASSERT_NE(find_monster_power(s, 0, PowerId::MODE_SHIFT), nullptr);
    EXPECT_EQ(g.move_history[0], kWhirlwind)
        << "the defensive cycle rejoins the offensive one at Whirlwind (:198)";
}

// ===========================================================================
// Sharp Hide
// ===========================================================================

TEST(GuardianSharpHide, TriggersOnAttackPlaysOnly) {
    CombatState s = make_guardian_state(21009);
    MonsterState& g = s.monsters[0];
    g.powers[g.power_count].power_id = static_cast<uint16_t>(PowerId::SHARP_HIDE);
    g.powers[g.power_count].amount = 4;
    ++g.power_count;

    // A SKILL play does nothing (SharpHidePower.java:45).
    ASSERT_EQ(card_def(CardId::DEFEND)->type, CardType::SKILL);
    dispatch_on_use_card(s, 0, static_cast<uint16_t>(CardId::DEFEND));
    EXPECT_EQ(s.action_count, 0u);
    drain_actions(s);
    EXPECT_EQ(s.player_hp, 500);

    // An ATTACK play reflects `amount` at the player.
    ASSERT_EQ(card_def(CardId::STRIKE)->type, CardType::ATTACK);
    dispatch_on_use_card(s, 0, static_cast<uint16_t>(CardId::STRIKE));
    ASSERT_EQ(s.action_count, 1u);
    drain_actions(s);
    EXPECT_EQ(s.player_hp, 496);
}

// The reflected damage is THORNS-typed, so the NORMAL-only pipeline hooks are
// skipped: a Vulnerable player takes 4, not floor(4 * 1.5) == 6.
TEST(GuardianSharpHide, ReflectedDamageIsThornsTypedAndUnamplified) {
    CombatState s = make_guardian_state(21010);
    MonsterState& g = s.monsters[0];
    g.powers[g.power_count].power_id = static_cast<uint16_t>(PowerId::SHARP_HIDE);
    g.powers[g.power_count].amount = 4;
    ++g.power_count;
    s.player_powers[s.player_power_count].power_id =
        static_cast<uint16_t>(PowerId::VULNERABLE);
    s.player_powers[s.player_power_count].amount = 2;
    ++s.player_power_count;

    dispatch_on_use_card(s, 0, static_cast<uint16_t>(CardId::STRIKE));
    drain_actions(s);
    EXPECT_EQ(s.player_hp, 496)
        << "THORNS skips VulnerablePower.atDamageReceive (NORMAL-only, :66-72)";
}

// Sharp Hide is a MONSTER-owned power, and monster powers are the LAST stage of
// the onUseCard fan-out (UseCardAction.java:41-64) -- so no shared dispatch site
// needed changing for it. Player block gained by the same card still applies.
TEST(GuardianSharpHide, PlayerBlockFromTheSameCardStillAbsorbs) {
    CombatState s = make_guardian_state(21011);
    MonsterState& g = s.monsters[0];
    g.powers[g.power_count].power_id = static_cast<uint16_t>(PowerId::SHARP_HIDE);
    g.powers[g.power_count].amount = 4;
    ++g.power_count;
    s.player_block = 10;

    dispatch_on_use_card(s, 0, static_cast<uint16_t>(CardId::STRIKE));
    drain_actions(s);
    EXPECT_EQ(s.player_hp, 500);
    EXPECT_EQ(s.player_block, 6);
}

// The S2.43 residual (seed STS431342): the Sharp Hide DamageAction is queued
// BEHIND the killing blow, and clearPostCombatActions KEEPS it -- its
// actionType is DAMAGE (GameActionManager.java:130-137) and the dying-owner
// cancel exempts THORNS (DamageAction.java:65,70) -- while endBattle() waits
// on the death animation, so the retaliation resolves before victory
// settlement. The engine's terminal resolver dropped it (USE_CARD/HEAL only)
// and the player finished 4 hp rich.
TEST(GuardianSharpHide, RetaliationQueuedBehindTheLethalBlowStillLands) {
    CombatState s = make_guardian_state(21017, /*player_hp=*/17);
    MonsterState& g = s.monsters[0];
    g.powers[g.power_count].power_id = static_cast<uint16_t>(PowerId::SHARP_HIDE);
    g.powers[g.power_count].amount = 4;
    ++g.power_count;
    g.hp = 5;
    g.block = 0;
    s.player_energy = 3;
    const CardDef* strike = card_def(CardId::STRIKE);
    ASSERT_NE(strike, nullptr);
    s.card_pool[0].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.card_pool[0].cost_now = card_cost(*strike, 0);
    s.card_pool[0].flags = card_flags(*strike, 0);
    s.hand[0] = 0;
    s.hand_count = 1;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::COMBAT_OVER));
    EXPECT_EQ(s.player_hp, 13) << "17 - Sharp Hide 4, landed post-lethal";
}

// The S2.49 pair that proves the survivor set keys on the THORNS exemption
// rather than on "run everything": a dying owner's queued NORMAL hit is
// cancelled inside execute_opcode (damage_attacker_cancelled) even though the
// DAMAGE item survives the clear; its THORNS twin lands.
TEST(GuardianSharpHide, DyingOwnersNormalHitStillCancelsAtTheVictoryTerminal) {
    for (const bool thorns : {false, true}) {
        CombatState s = make_guardian_state(21018, /*player_hp=*/50);
        s.monsters[0].hp = 5;
        ActionQueueItem kill = damage_item(0, 10);
        add_to_bottom(s, kill);
        ActionQueueItem hit{};
        hit.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
        hit.src = 0;  // the (about to be) dying Guardian
        hit.tgt = kActorPlayer;
        hit.amount = 10;
        hit.flags = make_damage_flags(thorns ? DamageType::THORNS
                                             : DamageType::NORMAL);
        add_to_bottom(s, hit);
        s.phase = static_cast<uint8_t>(CombatPhase::RESOLVING);
        pump(s);
        EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::COMBAT_OVER));
        EXPECT_EQ(s.player_hp, thorns ? 40 : 50)
            << "thorns=" << thorns
            << ": only the THORNS twin survives its owner's death";
    }
}

// Terminal-kind scoping: the game calls clearPostCombatActions only from
// damage gated on areMonstersBasicallyDead() -- a DEFEAT has no such call. It
// has no DRAIN either: AbstractPlayer.damage latches the death in the hit that
// lands it (`isDead = true; deathScreen = new DeathScreen(...)`,
// AbstractPlayer.java:1500-1501), the DeathScreen constructor sets
// `AbstractDungeon.screen = DEATH` (DeathScreen.java:86), and that screen's arm
// of AbstractDungeon.update -- `case 17: { deathScreen.update(); break; }`
// (:2092-2095) -- never calls `currMapNode.room.update()`, which is the only
// caller of actionManager.update (AbstractRoom.java:231, :265, :364). So the
// queue FREEZES: not one queued action behind the killing hit resolves, of any
// shape. This test used to assert only the BLOCK arm and was named for the
// USE_CARD/HEAL pair it still (wrongly) let through; the HEAL arm is the one
// that cost a run -- see the Spiker case in beyond_normals_i_test.
TEST(GuardianSharpHide, DefeatTerminalResolvesNothingBehindTheKillingHit) {
    CombatState s = make_guardian_state(21019, /*player_hp=*/5);
    const CardDef* strike = card_def(CardId::STRIKE);
    ASSERT_NE(strike, nullptr);
    s.card_pool[0].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.card_pool[0].cost_now = card_cost(*strike, 0);
    s.card_pool[0].flags = card_flags(*strike, 0);
    s.limbo[0] = 0;
    s.limbo_count = 1;

    ActionQueueItem kill{};
    kill.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    kill.src = 0;
    kill.tgt = kActorPlayer;
    kill.amount = 10;
    kill.flags = make_damage_flags(DamageType::NORMAL);
    add_to_bottom(s, kill);
    ActionQueueItem blk{};
    blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
    blk.src = kActorPlayer;
    blk.tgt = kActorPlayer;
    blk.amount = 3;
    add_to_bottom(s, blk);
    ActionQueueItem heal{};
    heal.opcode = static_cast<uint16_t>(Opcode::HEAL);
    heal.src = kActorPlayer;
    heal.tgt = kActorPlayer;
    heal.amount = 7;
    add_to_bottom(s, heal);
    ActionQueueItem use{};
    use.opcode = static_cast<uint16_t>(Opcode::USE_CARD);
    use.src = kActorPlayer;
    use.amount = 0;  // the limbo card
    add_to_bottom(s, use);

    s.phase = static_cast<uint8_t>(CombatPhase::RESOLVING);
    pump(s);
    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::COMBAT_OVER));
    EXPECT_EQ(s.player_hp, 0)
        << "the queued HEAL is abandoned -- nothing may un-kill the player";
    EXPECT_EQ(s.player_block, 0)
        << "BLOCK is a victory-only survivor; a defeat abandons it";
    EXPECT_EQ(s.limbo_count, 0)
        << "the abandoned UseCardAction leaves the card to the terminal flush";
    ASSERT_EQ(s.discard_count, 1);
    EXPECT_EQ(s.discard[0], 0)
        << "flush_limbo_at_combat_over files it with no RNG and no hooks";
}

// ===========================================================================
// Spot Weakness vs. the ATTACK_BUFF telegraph
// ===========================================================================

// The Guardian is the first monster to telegraph ATTACK_BUFF (Twin Slam,
// TheGuardian.java:204). op_spot_weakness classifies "does this intent deal
// attack damage" from the stored MonsterIntent (SpotWeaknessAction.java:32-40 ->
// getIntentBaseDmg() >= 0, AbstractMonster.java:451-463), and while ATTACK_BUFF
// was missing from that predicate the card silently paid out NOTHING against a
// telegraphed attack. This test exists because that failure mode is silent.
TEST(GuardianSpotWeakness, AttackBuffTelegraphPaysOutStrength) {
    CombatState s = make_guardian_state(21012);
    set_monster_move(s.monsters[0], kTwinSlam, MonsterIntent::ATTACK_BUFF);

    ActionQueueItem sw{};
    sw.opcode = static_cast<uint16_t>(Opcode::SPOT_WEAKNESS);
    sw.src = kActorPlayer;
    sw.tgt = 0;
    sw.amount = 3;
    execute_opcode(s, sw);
    drain_actions(s);

    const PowerSlot* str = find_player_power(s, PowerId::STRENGTH);
    ASSERT_NE(str, nullptr)
        << "ATTACK_BUFF carries a base damage, so Spot Weakness must pay out";
    EXPECT_EQ(str->amount, 3);
}

// The complementary half: the Guardian's two NON-attack telegraphs pay nothing.
TEST(GuardianSpotWeakness, DefendAndBuffTelegraphsPayNothing) {
    for (MonsterIntent intent :
         {MonsterIntent::DEFEND, MonsterIntent::BUFF}) {
        CombatState s = make_guardian_state(21013);
        set_monster_move(s.monsters[0], kChargeUp, intent);

        ActionQueueItem sw{};
        sw.opcode = static_cast<uint16_t>(Opcode::SPOT_WEAKNESS);
        sw.src = kActorPlayer;
        sw.tgt = 0;
        sw.amount = 3;
        execute_opcode(s, sw);
        drain_actions(s);

        EXPECT_EQ(find_player_power(s, PowerId::STRENGTH), nullptr)
            << "intent " << static_cast<int>(intent)
            << ": setMove leaves baseDamage at -1 (AbstractMonster.java:451-463)";
    }
}

// ===========================================================================
// Directed public-API script
// ===========================================================================

TEST(GuardianDirectedScript, AdvanceRunsTheOpeningOffensiveCycle) {
    CombatState s = make_guardian_state(21014);
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    s.turn = 1;
    s.monster_attacks_queued = 1;

    Action action[1] = {make_action(ActionVerb::END_TURN)};
    StepResult result[1]{};
    auto end_turn = [&]() {
        advance(std::span<CombatState>(&s, 1),
                std::span<const Action>(action, 1),
                std::span<StepResult>(result, 1));
        EXPECT_FALSE(result[0].terminal);
    };

    end_turn();  // CHARGE_UP
    EXPECT_EQ(s.monsters[0].block, 9);
    EXPECT_EQ(s.monsters[0].move_history[0], kFierceBash);

    end_turn();  // FIERCE_BASH (36 at A4+)
    EXPECT_EQ(s.player_hp, 500 - 36);
    EXPECT_EQ(s.monsters[0].move_history[0], kVentSteam);

    end_turn();  // VENT_STEAM
    EXPECT_EQ(s.monsters[0].move_history[0], kWhirlwind);
    EXPECT_NE(find_player_power(s, PowerId::VULNERABLE), nullptr);
}

// The flip driven from the PLAYER'S END OF TURN (S2.V3 seed STS237405, floor
// 16, turn 2 -> 3): Combust's DamageAllEnemiesAction takes the Guardian across
// its threshold while the end-turn sequence is still queued. The damage()
// override addToBottom's a ChangeStateAction (TheGuardian.java:288); by the
// time IT resolves, AbstractRoom$1 has already appended EndTurnAction /
// WaitAction / MonsterStartTurnAction, so changeState's own GainBlock(20)
// (:240) lands BEHIND MonsterStartTurnAction's applyPreTurnLogic block clear
// (MonsterGroup.java:98-106) and the Guardian starts its turn -- and the
// player's next one -- holding the 20. The capture shows blk=20 with Sharp Hide
// 4 at the top of turn 3; the engine used to show 0.
TEST(GuardianDirectedScript, DefensiveBlockFromAnEndOfTurnFlipSurvivesTheMonsterTurn) {
    CombatState s = make_guardian_state(21015);
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    s.turn = 1;
    s.monster_attacks_queued = 1;

    // 35 of the 40 already taken during the turn; Mode Shift reads 5.
    hit_guardian(s, 35);
    ASSERT_EQ(s.monsters[0].move_history[0], kChargeUp) << "no flip yet";
    ASSERT_EQ(find_monster_power(s, 0, PowerId::MODE_SHIFT)->amount, 5);

    // Combust 7 (hpLoss 1): atEndOfTurn queues LoseHP(1) then a pure THORNS
    // DamageAllEnemies(7) (CombustPower.java:39-45).
    s.player_powers[s.player_power_count++] =
        PowerSlot{static_cast<uint16_t>(PowerId::COMBUST), 7, 0, 0};
    s.flags = (s.flags & ~kCombatFlagCombustHpLossMask) |
              (1u << kCombatFlagCombustHpLossShift);

    Action action[1] = {make_action(ActionVerb::END_TURN)};
    StepResult result[1]{};
    advance(std::span<CombatState>(&s, 1), std::span<const Action>(action, 1),
            std::span<StepResult>(result, 1));
    EXPECT_FALSE(result[0].terminal);

    const MonsterState& g = s.monsters[0];
    EXPECT_EQ(g.hp, kA20Hp - 35 - 7) << "Combust's 7 landed";
    EXPECT_EQ(s.player_hp, 500 - 1) << "Combust's own HP loss; Close Up deals none";
    EXPECT_EQ(find_monster_power(s, 0, PowerId::MODE_SHIFT), nullptr)
        << "the flip happened: Mode Shift removed (:238)";
    EXPECT_NE(find_monster_power(s, 0, PowerId::SHARP_HIDE), nullptr)
        << "the turn it then took was CLOSE_UP (:246), not the telegraphed Charge Up";
    EXPECT_EQ(g.move_history[0], kRollAttack);
    EXPECT_EQ(g.block, 20)
        << "GainBlock(20) resolved AFTER MonsterStartTurnAction's block clear, "
           "so the Defensive-Mode block is still up at the top of the player's turn";
}

}  // namespace
}  // namespace sts::engine
