// A4.1 acceptance suite: effect interpreter + DAMAGE pipeline
// (design doc §5.5, §6, §10 trap 1).
//
// The damage table checks Strength/Vulnerable/Weak stacking against values
// HAND-COMPUTED from the cited Java (DamageInfo.applyPowers, StrengthPower/
// VulnerablePower/WeakPower.atDamage*), NOT derived from this implementation --
// the arithmetic for the tricky stacked cases is shown inline. The trap-1 test
// pins the float-accumulation-floored-once rule with a case whose correct
// (float) answer differs from the wrong (integer-per-step) answer. The rest are
// per-opcode direct checks plus a pump() wiring regression proving A3.1's pop
// step now actually applies effects.
//
// Power-list order convention used throughout: on an attacker carrying both,
// Strength is stored at index 0 and Weak at index 1, so atDamageGive applies
// Strength (+amount) THEN Weak (*0.75) -- order matters and is fixed here.

#include <cstdint>
#include <cstring>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

// --- Helpers ----------------------------------------------------------------

void add_power(PowerSlot* slots, uint8_t& count, PowerId id, int16_t amount) {
    slots[count].power_id = static_cast<uint16_t>(id);
    slots[count].amount = amount;
    ++count;
}

ActionQueueItem op(Opcode code, uint8_t src, uint8_t tgt, int32_t amount,
                   uint32_t flags = 0) {
    ActionQueueItem i{};
    i.opcode = static_cast<uint16_t>(code);
    i.src = src;
    i.tgt = tgt;
    i.amount = amount;
    i.flags = flags;
    return i;
}

// A minimal live combat: player at 80, one Jaw Worm at 40.
CombatState make_combat() {
    CombatState s{};
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.player_energy = 3;
    s.monster_count = 1;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = 40;
    s.monsters[0].max_hp = 40;
    return s;
}

// --- DAMAGE table -----------------------------------------------------------

struct Case {
    const char* name;
    int base;
    int strength;      // Strength stacks on the ATTACKER (0 = none)
    bool weak;         // Weak on the ATTACKER
    bool vulnerable;   // Vulnerable on the TARGET
    bool player_owned; // true: player -> monster; false: monster -> player
    int expected;      // hand-computed from the cited Java
};

// Every `expected` is derived by hand from the formulas, not from the code:
//   atDamageGive:   Strength => dmg + amount ; Weak => dmg * 0.75f
//   atDamageReceive:Vulnerable => dmg * 1.5f
//   floor once at the end (MathUtils.floor), clamp >= 0.
const Case kCases[] = {
    // no powers: base passes through unchanged.
    {"plain_player", 6, 0, false, false, true, 6},
    {"plain_monster", 12, 0, false, false, false, 12},
    // Strength alone: 6 + 3 = 9.
    {"strength_only", 6, 3, false, false, true, 9},
    // Vulnerable alone: 6 * 1.5 = 9.0 -> 9.
    {"vulnerable_only", 6, 0, false, true, true, 9},
    // Vulnerable with a fractional result: 5 * 1.5 = 7.5 -> floor 7.
    {"vulnerable_floor", 5, 0, false, true, true, 7},
    // Weak alone: 6 * 0.75 = 4.5 -> floor 4.
    {"weak_only", 6, 0, true, false, true, 4},
    // THE named edge case: (7 + 2) * 1.5 = 13.5 -> floor 13 (not 13.5-rounded=14).
    {"str2_vuln_edge_13", 7, 2, false, true, true, 13},
    // Another stacked Str+Vuln I hand-compute: (6 + 3) * 1.5 = 13.5 -> 13.
    {"str3_vuln_stacked", 6, 3, false, true, true, 13},
    // Weak + Strength (order: Str then Weak): (10 + 4) * 0.75 = 10.5 -> floor 10.
    {"weak_strength_stacked", 10, 4, true, false, true, 10},
    // Monster-owned with Strength on the monster and Vulnerable on the player:
    // (5 + 2) * 1.5 = 10.5 -> floor 10 (exercises the monster-owned hook branch).
    {"monster_str_vuln", 5, 2, false, true, false, 10},
};

TEST(DamagePipeline, StrengthVulnerableWeakStackingMatchesHandComputed) {
    for (const Case& c : kCases) {
        CombatState s = make_combat();

        uint8_t attacker;
        uint8_t target;
        PowerSlot* atk_slots;
        uint8_t* atk_count;
        PowerSlot* tgt_slots;
        uint8_t* tgt_count;
        if (c.player_owned) {
            attacker = kActorPlayer;
            target = 0;
            atk_slots = s.player_powers;
            atk_count = &s.player_power_count;
            tgt_slots = s.monsters[0].powers;
            tgt_count = &s.monsters[0].power_count;
        } else {
            attacker = 0;
            target = kActorPlayer;
            atk_slots = s.monsters[0].powers;
            atk_count = &s.monsters[0].power_count;
            tgt_slots = s.player_powers;
            tgt_count = &s.player_power_count;
        }

        // Strength at index 0, then Weak at index 1 (fixed order convention).
        if (c.strength != 0) {
            add_power(atk_slots, *atk_count, PowerId::STRENGTH,
                      static_cast<int16_t>(c.strength));
        }
        if (c.weak) {
            add_power(atk_slots, *atk_count, PowerId::WEAK, 1);
        }
        if (c.vulnerable) {
            add_power(tgt_slots, *tgt_count, PowerId::VULNERABLE, 2);
        }

        const int got = compute_damage(s, attacker, target, c.base);
        EXPECT_EQ(got, c.expected) << c.name;
    }
}

// --- Trap 1: float accumulation, floored ONCE, no integer shortcuts ---------

TEST(DamagePipelineTrap, FloatAccumulationFlooredOnceNoIntegerShortcuts) {
    // Part 1 -- the ledger's named case: base 7, Str 2, Vuln -> 13, not the
    // round-to-14 answer. (7 + 2) * 1.5 = 13.5, MathUtils.floor -> 13.
    {
        CombatState s = make_combat();
        add_power(s.player_powers, s.player_power_count, PowerId::STRENGTH, 2);
        add_power(s.monsters[0].powers, s.monsters[0].power_count,
                  PowerId::VULNERABLE, 2);
        EXPECT_EQ(compute_damage(s, kActorPlayer, 0, 7), 13);
    }

    // Part 2 -- a case that DIVERGES under integer-per-step truncation, making
    // the "float accumulation" rule observable. Player attacker has Weak, the
    // monster target has Vulnerable, base 5:
    //   float, floored once:  5 * 0.75 = 3.75 ; 3.75 * 1.5 = 5.625 ; floor -> 5
    //   integer per step   :  floor(5*0.75)=3 ; floor(3*1.5)=floor(4.5)=4
    // The correct (game) answer is 5; a per-step-integer implementation yields
    // 4. Assert we get 5 AND that it differs from the wrong integer answer.
    {
        CombatState s = make_combat();
        add_power(s.player_powers, s.player_power_count, PowerId::WEAK, 1);
        add_power(s.monsters[0].powers, s.monsters[0].power_count,
                  PowerId::VULNERABLE, 2);

        const int correct = compute_damage(s, kActorPlayer, 0, 5);

        // Reproduce the WRONG integer-per-step pipeline to prove divergence.
        const int wrong_int_per_step =
            static_cast<int>((static_cast<int>(5 * 0.75)) * 1.5);
        EXPECT_EQ(correct, 5);
        EXPECT_EQ(wrong_int_per_step, 4);
        EXPECT_NE(correct, wrong_int_per_step);
    }
}

// --- Per-opcode direct checks -----------------------------------------------

TEST(InterpOpcode, BlockAddsToRecipient) {
    CombatState s = make_combat();
    execute_opcode(s, op(Opcode::BLOCK, kActorPlayer, kActorPlayer, 8));
    EXPECT_EQ(s.player_block, 8);
    execute_opcode(s, op(Opcode::BLOCK, kActorPlayer, kActorPlayer, 5));
    EXPECT_EQ(s.player_block, 13);  // accumulates

    execute_opcode(s, op(Opcode::BLOCK, kActorPlayer, /*tgt monster*/ 0, 9));
    EXPECT_EQ(s.monsters[0].block, 9);
}

TEST(InterpOpcode, ApplyPowerAddsAndStacks) {
    CombatState s = make_combat();
    const uint32_t vuln = make_apply_power_flags(PowerId::VULNERABLE);

    execute_opcode(s, op(Opcode::APPLY_POWER, kActorPlayer, 0, 2, vuln));
    ASSERT_EQ(s.monsters[0].power_count, 1);
    EXPECT_EQ(s.monsters[0].powers[0].power_id,
              static_cast<uint16_t>(PowerId::VULNERABLE));
    EXPECT_EQ(s.monsters[0].powers[0].amount, 2);

    // Re-applying the same power stacks its amount, no new slot.
    execute_opcode(s, op(Opcode::APPLY_POWER, kActorPlayer, 0, 2, vuln));
    EXPECT_EQ(s.monsters[0].power_count, 1);
    EXPECT_EQ(s.monsters[0].powers[0].amount, 4);

    // Applying to the player targets the player's power list.
    execute_opcode(s, op(Opcode::APPLY_POWER, kActorPlayer, kActorPlayer, 3,
                         make_apply_power_flags(PowerId::STRENGTH)));
    ASSERT_EQ(s.player_power_count, 1);
    EXPECT_EQ(s.player_powers[0].power_id,
              static_cast<uint16_t>(PowerId::STRENGTH));
    EXPECT_EQ(s.player_powers[0].amount, 3);
}

TEST(InterpOpcode, GainEnergyAddsToPlayer) {
    CombatState s = make_combat();
    EXPECT_EQ(s.player_energy, 3);
    execute_opcode(s, op(Opcode::GAIN_ENERGY, kActorPlayer, kActorPlayer, 2));
    EXPECT_EQ(s.player_energy, 5);
}

TEST(InterpOpcode, DrawMovesTopOfDrawPileToHand) {
    CombatState s = make_combat();
    // Two cards in the draw pile (pool indices 3 and 4); top is the array end.
    s.draw[0] = 3;
    s.draw[1] = 4;
    s.draw_count = 2;
    s.hand_count = 0;

    execute_opcode(s, op(Opcode::DRAW, kActorPlayer, kActorPlayer, 1));
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.draw_count, 1);
    EXPECT_EQ(s.hand[0], 4);  // drew the top (draw[draw_count-1])

    // Drawing more than remain stops at the empty pile (A4.2 owns reshuffle).
    execute_opcode(s, op(Opcode::DRAW, kActorPlayer, kActorPlayer, 5));
    EXPECT_EQ(s.hand_count, 2);
    EXPECT_EQ(s.draw_count, 0);
    EXPECT_EQ(s.hand[1], 3);

    // Draw on an empty pile is a safe no-op.
    execute_opcode(s, op(Opcode::DRAW, kActorPlayer, kActorPlayer, 3));
    EXPECT_EQ(s.hand_count, 2);
    EXPECT_EQ(s.draw_count, 0);
}

TEST(InterpOpcode, ExhaustMovesCardFromHandToExhaust) {
    CombatState s = make_combat();
    s.hand[0] = 7;
    s.hand[1] = 9;
    s.hand_count = 2;

    execute_opcode(s, op(Opcode::EXHAUST, kActorPlayer, kActorPlayer, /*idx=*/7));
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], 9);  // 9 shifted down into slot 0
    ASSERT_EQ(s.exhaust_count, 1);
    EXPECT_EQ(s.exhaust[0], 7);

    // Exhausting a card not in hand is a safe no-op.
    execute_opcode(s, op(Opcode::EXHAUST, kActorPlayer, kActorPlayer, /*idx=*/55));
    EXPECT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.exhaust_count, 1);
}

// --- ROLL_MOVE is a stubbed no-op (scope-boundary decision) -----------------
// (SHUFFLE_IN was an A4.1 stub too, but A4.2 implemented it -- its behavior is
// now covered by piles_test; only ROLL_MOVE remains a documented no-op here.)

TEST(InterpStub, RollMoveDoesNotMutateState) {
    CombatState s = make_combat();
    // Give the state some non-trivial content to detect any stray write.
    s.player_block = 4;
    add_power(s.player_powers, s.player_power_count, PowerId::STRENGTH, 2);
    s.draw[0] = 1;
    s.draw_count = 1;
    s.discard[0] = 2;
    s.discard_count = 1;
    s.hand[0] = 3;
    s.hand_count = 1;

    CombatState before;
    std::memcpy(&before, &s, sizeof(CombatState));

    execute_opcode(s, op(Opcode::ROLL_MOVE, 0, 0, 0));

    EXPECT_EQ(std::memcmp(&before, &s, sizeof(CombatState)), 0)
        << "ROLL_MOVE stub must not mutate state";
}

TEST(InterpStub, NopAndUnrecognizedOpcodeAreNoOps) {
    CombatState s = make_combat();
    CombatState before;
    std::memcpy(&before, &s, sizeof(CombatState));

    // NOP (opcode 0) and an out-of-range opcode both dispatch to the no-op path.
    execute_opcode(s, op(Opcode::NOP, 0, 0, 999));
    ActionQueueItem junk{};
    junk.opcode = 60000;  // unrecognized
    junk.amount = 123;
    execute_opcode(s, junk);

    EXPECT_EQ(std::memcmp(&before, &s, sizeof(CombatState)), 0);
}

// --- Regression: pump() now actually applies effects ------------------------

TEST(InterpPumpWiring, PumpAppliesQueuedDamageToTarget) {
    CombatState s = make_combat();
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    s.monster_attacks_queued = 1;  // player-turn invariant (A3.1)
    s.turn_has_ended = 0;

    const int16_t hp_before = s.monsters[0].hp;  // 40

    // Queue a player DAMAGE for 6 against monster 0 and pump one step.
    add_to_bottom(s, op(Opcode::DAMAGE, kActorPlayer, /*tgt=*/0, 6));
    const PumpStepResult r = pump_step(s, default_monster_turn);

    EXPECT_EQ(r.outcome, PumpOutcome::RAN_ACTION);
    EXPECT_EQ(s.monsters[0].hp, hp_before - 6)
        << "pump's pop step must dispatch the DAMAGE opcode (A3.1<->A4.1 wiring)";
}

TEST(InterpPumpWiring, PreTurnQueuedEffectAlsoApplies) {
    CombatState s = make_combat();
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    s.monster_attacks_queued = 1;
    s.turn_has_ended = 0;
    s.player_energy = 3;

    add_to_turn_start(s, op(Opcode::GAIN_ENERGY, kActorPlayer, kActorPlayer, 2));
    const PumpStepResult r = pump_step(s, default_monster_turn);

    EXPECT_EQ(r.outcome, PumpOutcome::RAN_PRE_TURN);
    EXPECT_EQ(s.player_energy, 5);
}

}  // namespace
}  // namespace sts::engine
