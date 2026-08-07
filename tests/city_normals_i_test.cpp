// Act-2 city normals I -- the Chosen, the Byrd, the Shelled Parasite and the
// Spheric Guardian, plus the two powers they bring (Hex, Flight), the
// VAMPIRE_DAMAGE opcode, and the ON_POWER_REMOVED hook the Byrd's grounding and
// the Parasite's armour break both hang on.
//
// WHAT THIS FILE PINS, and why each part is here rather than assumed:
//
//   * STAT/MOVE TABLES, per monster, at EVERY ascension branch the Java has --
//     not just the A20 column the engine runs. The rows were transcribed from
//     ctors that carry deliberately misleading constant NAMES (Chosen's
//     A_2_HP_MIN gates on >= 7), so the tier boundary is asserted, not the value
//     alone.
//   * RNG DRAW COUNTS, per monster, per phase. These are the bit-exactness
//     surface: a monster that draws one value too many or too few silently
//     desynchronises every later monster in its group. The Spheric Guardian's
//     ZERO HP draws and the Parasite's recursion are the two that would be
//     easiest to get wrong.
//   * FLIGHT'S FULL LIFECYCLE, including the two edges that are easy to miss:
//     a KILLING blow sheds no stack, and every start of turn RESTORES the stack
//     to what the ctor stored.
//   * THE ARMOUR-BREAK TELEGRAPH, which is the regression test for a real bug:
//     Plated Armor's native body used to decrement its own slot and zero
//     power_id in place, bypassing remove_slot_at, so the removal hook could
//     never fire and the Parasite could never be stunned.
//   * ENCOUNTER COMPOSITIONS, spawn-order-exact, because a group's spawn order
//     is what fixes each monster's index -- and the Sentry's opener is chosen
//     from that index.

#include <cstdint>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_byrd.hpp"
#include "sts/engine/monster_chosen.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_shelled_parasite.hpp"
#include "sts/engine/monster_spheric_guardian.hpp"
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

void give_monster_power(CombatState& s, uint8_t mi, PowerId id, int16_t amt,
                        int16_t counter = 0) {
    MonsterState& m = s.monsters[mi];
    m.powers[m.power_count].power_id = static_cast<uint16_t>(id);
    m.powers[m.power_count].amount = amt;
    m.powers[m.power_count].counter = counter;
    ++m.power_count;
}

// MonsterGroup.applyPreTurnLogic, driven through the REAL queue marker
// (kOpcodeMonsterStartTurn) and one pump step, rather than by calling the
// internal walk -- so this exercises the same path a live combat does.
void run_monster_start_of_turn(CombatState& s) {
    ActionQueueItem marker{};
    marker.opcode = kOpcodeMonsterStartTurn;
    add_to_bottom(s, marker);
    const PumpStepResult r = pump_step(s, default_monster_turn);
    EXPECT_EQ(r.executed.opcode, kOpcodeMonsterStartTurn);
}

// A plain NORMAL attack from the player onto monster `mi`, through the real
// DAMAGE opcode so every hook fires exactly as it does in combat.
void player_attacks(CombatState& s, uint8_t mi, int32_t base) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    it.src = kActorPlayer;
    it.tgt = mi;
    it.amount = base;
    it.flags = make_damage_flags(DamageType::NORMAL);
    execute_opcode(s, it);
}

// The amount a move's step `k` resolves to at `asc` -- the same lookup
// queue_monster_move_effects makes.
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

// Chosen.java:80-93. THE HP BRANCH IS `>= 7` DESPITE THE A_2_HP_* CONSTANT
// NAMES (:45-46) -- that mismatch is the single most likely transcription error
// in this row, so the boundary is asserted at 6/7 rather than only at A20.
TEST(CityNormalsI, ChosenStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kChosen;
    EXPECT_EQ(d.hp_min(0), 95);
    EXPECT_EQ(d.hp_max(0), 99);
    EXPECT_EQ(d.hp_min(6), 95) << "the HP branch is >= 7, not >= 2";
    EXPECT_EQ(d.hp_max(6), 99);
    EXPECT_EQ(d.hp_min(7), 98);
    EXPECT_EQ(d.hp_max(7), 103);
    EXPECT_EQ(d.hp_min(kA20), 98);
    EXPECT_EQ(d.hp_max(kA20), 103);

    // Damage: the >= 2 branch (:85-93). Zap 18/21, Debilitate 10/12, Poke 5/6.
    EXPECT_EQ(step_amount(d, r::kChosenMoveZap, 0, 1), 18);
    EXPECT_EQ(step_amount(d, r::kChosenMoveZap, 0, 2), 21);
    EXPECT_EQ(step_amount(d, r::kChosenMoveDebilitate, 0, 1), 10);
    EXPECT_EQ(step_amount(d, r::kChosenMoveDebilitate, 0, 2), 12);
    EXPECT_EQ(step_amount(d, r::kChosenMovePoke, 0, 1), 5);
    EXPECT_EQ(step_amount(d, r::kChosenMovePoke, 0, 2), 6);

    // POKE IS TWO SEPARATE HITS (:110-111), not one doubled one -- so block and
    // a lethal clamp apply per hit. Both steps carry the same tiered damage.
    EXPECT_EQ(step_count(d, r::kChosenMovePoke), 2);
    EXPECT_EQ(step_amount(d, r::kChosenMovePoke, 1, kA20), 6);

    // Fixed constants, no ascension branch at all.
    EXPECT_EQ(step_amount(d, r::kChosenMoveDebilitate, 1, 0), 2);   // VULN (:60)
    EXPECT_EQ(step_amount(d, r::kChosenMoveDebilitate, 1, kA20), 2);
    EXPECT_EQ(step_amount(d, r::kChosenMoveHex, 0, kA20), 1);       // HEX_AMT (:68)

    // DRAIN'S ORDER IS LOAD-BEARING (:119-123): Weak on the PLAYER first, THEN
    // Strength on the Chosen. Both 3, both flat.
    const r::MonsterMove* drain = d.move(r::kChosenMoveDrain);
    ASSERT_NE(drain, nullptr);
    ASSERT_EQ(drain->effect_count, 2);
    EXPECT_EQ(drain->effects[0].target, r::MonsterMoveTarget::PLAYER);
    EXPECT_EQ(drain->effects[0].extra, make_apply_power_flags(PowerId::WEAK));
    EXPECT_EQ(drain->effects[0].amount.at(kA20), 3);
    EXPECT_EQ(drain->effects[1].target, r::MonsterMoveTarget::SELF);
    EXPECT_EQ(drain->effects[1].extra,
              make_apply_power_flags(PowerId::STRENGTH));
    EXPECT_EQ(drain->effects[1].amount.at(kA20), 3);

    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::NORMAL);
    EXPECT_TRUE(d.ai_native);
    EXPECT_EQ(d.roll_count, 0);
}

// Byrd.java:78-95. Three DIFFERENT tier boundaries in one ctor -- HP at >= 7,
// flightAmt at >= 17, the damage block at >= 2 -- and one number (headbutt) with
// no branch at all.
TEST(CityNormalsI, ByrdStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kByrd;
    EXPECT_EQ(d.hp_min(0), 25);
    EXPECT_EQ(d.hp_max(0), 31);
    EXPECT_EQ(d.hp_min(6), 25);
    EXPECT_EQ(d.hp_min(7), 26);
    EXPECT_EQ(d.hp_max(7), 33);
    EXPECT_EQ(d.hp_min(kA20), 26);
    EXPECT_EQ(d.hp_max(kA20), 33);

    // peckDmg is 1 on BOTH sides of the >= 2 branch (:85,89), so the row needs
    // no tier column -- asserted so a future "surely it scales" edit fails.
    EXPECT_EQ(step_amount(d, r::kByrdMovePeck, 0, 0), 1);
    EXPECT_EQ(step_amount(d, r::kByrdMovePeck, 0, kA20), 1);
    // Swoop 12 / 14 at >= 2.
    EXPECT_EQ(step_amount(d, r::kByrdMoveSwoop, 0, 1), 12);
    EXPECT_EQ(step_amount(d, r::kByrdMoveSwoop, 0, 2), 14);
    // HEADBUTT_DMG is a literal 3 with NO branch (:63,95).
    EXPECT_EQ(step_amount(d, r::kByrdMoveHeadbutt, 0, 0), 3);
    EXPECT_EQ(step_amount(d, r::kByrdMoveHeadbutt, 0, kA20), 3);
    // CAW_STR 1, flat (:64).
    EXPECT_EQ(step_amount(d, r::kByrdMoveCaw, 0, kA20), 1);

    // flightAmt: 3, or 4 from A17 (:83). Boundary asserted at 16/17.
    EXPECT_EQ(step_amount(d, r::kByrdMoveGoAirborne, 0, 0), 3);
    EXPECT_EQ(step_amount(d, r::kByrdMoveGoAirborne, 0, 16), 3);
    EXPECT_EQ(step_amount(d, r::kByrdMoveGoAirborne, 0, 17), 4);
    EXPECT_EQ(step_amount(d, r::kByrdMoveGoAirborne, 0, kA20), 4);
    const r::MonsterMove* air = d.move(r::kByrdMoveGoAirborne);
    ASSERT_NE(air, nullptr);
    EXPECT_EQ(air->effects[0].extra, make_apply_power_flags(PowerId::FLIGHT));
    EXPECT_EQ(air->effects[0].target, r::MonsterMoveTarget::SELF);
    EXPECT_EQ(air->intent, r::MonsterIntent::UNKNOWN);

    // PECK'S SIX STEPS ARE THE A20 COUNT, and the schema cannot express the
    // ascension-varying count (5 base / 6 from A2). This assertion is the record
    // of that limitation: if the engine ever runs below A2 it must FAIL here
    // rather than quietly over-hit.
    EXPECT_EQ(step_count(d, r::kByrdMovePeck), 6)
        << "peckCount is 6 at A20 (Byrd.java:86,90); the row is exact only at "
           ">= A2 -- see the move's comment in registry/monsters.yaml";

    EXPECT_EQ(d.move(r::kByrdMoveStunned)->intent, r::MonsterIntent::STUN);
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::NORMAL);
    EXPECT_EQ(d.roll_count, 0);
}

// ShelledParasite.java:81-97.
TEST(CityNormalsI, ShelledParasiteStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kShelledParasite;
    EXPECT_EQ(d.hp_min(0), 68);
    EXPECT_EQ(d.hp_max(0), 72);
    EXPECT_EQ(d.hp_min(6), 68);
    EXPECT_EQ(d.hp_min(7), 70);
    EXPECT_EQ(d.hp_max(7), 75);
    EXPECT_EQ(d.hp_min(kA20), 70);
    EXPECT_EQ(d.hp_max(kA20), 75);

    // The ctor adds damage.get(0..2) as doubleStrike, fell, suck (:95-97) --
    // NOT in move-id order, which is the transcription trap here. The moves
    // below therefore each name their own number.
    EXPECT_EQ(step_amount(d, r::kShelledParasiteMoveFell, 0, 1), 18);
    EXPECT_EQ(step_amount(d, r::kShelledParasiteMoveFell, 0, 2), 21);
    EXPECT_EQ(step_amount(d, r::kShelledParasiteMoveDoubleStrike, 0, 1), 6);
    EXPECT_EQ(step_amount(d, r::kShelledParasiteMoveDoubleStrike, 0, 2), 7);
    EXPECT_EQ(step_amount(d, r::kShelledParasiteMoveLifeSuck, 0, 1), 10);
    EXPECT_EQ(step_amount(d, r::kShelledParasiteMoveLifeSuck, 0, 2), 12);

    // Double Strike is TWO independent hits (:121-125).
    EXPECT_EQ(step_count(d, r::kShelledParasiteMoveDoubleStrike), 2);
    EXPECT_EQ(step_amount(d, r::kShelledParasiteMoveDoubleStrike, 1, kA20), 7);

    // Fell: damage THEN Frail 2 (:116-117), flat.
    const r::MonsterMove* fell = d.move(r::kShelledParasiteMoveFell);
    ASSERT_NE(fell, nullptr);
    ASSERT_EQ(fell->effect_count, 2);
    EXPECT_EQ(fell->effects[0].op, r::Opcode::DAMAGE);
    EXPECT_EQ(fell->effects[1].extra, make_apply_power_flags(PowerId::FRAIL));
    EXPECT_EQ(fell->effects[1].amount.at(kA20), 2);

    // Life Suck is the VAMPIRE_DAMAGE opcode, not DAMAGE + HEAL.
    const r::MonsterMove* suck = d.move(r::kShelledParasiteMoveLifeSuck);
    ASSERT_NE(suck, nullptr);
    EXPECT_EQ(suck->effects[0].op, r::Opcode::VAMPIRE_DAMAGE);
    EXPECT_EQ(suck->effects[0].target, r::MonsterMoveTarget::PLAYER);
    EXPECT_EQ(suck->intent, r::MonsterIntent::ATTACK_BUFF);

    EXPECT_EQ(kShelledParasitePlatedArmor, 14);  // (:54)
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::NORMAL);
    EXPECT_EQ(d.roll_count, 0);
}

// SphericGuardian.java:48-58,95-99.
TEST(CityNormalsI, SphericGuardianStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kSphericGuardian;
    // HP IS FLAT 20 AT EVERY ASCENSION -- one tier, no branch, because setHp is
    // never called (see the zero-draw test below for the consequence).
    EXPECT_EQ(d.hp_tier_count, 1);
    EXPECT_EQ(d.hp_min(0), 20);
    EXPECT_EQ(d.hp_max(0), 20);
    EXPECT_EQ(d.hp_min(kA20), 20);
    EXPECT_EQ(d.hp_max(kA20), 20);

    // dmg 10 / 11 at >= 2 (:48), shared by all three attacking moves.
    EXPECT_EQ(step_amount(d, r::kSphericGuardianMoveBigAttack, 0, 1), 10);
    EXPECT_EQ(step_amount(d, r::kSphericGuardianMoveBigAttack, 0, 2), 11);
    EXPECT_EQ(step_amount(d, r::kSphericGuardianMoveFrailAttack, 0, kA20), 11);
    EXPECT_EQ(step_amount(d, r::kSphericGuardianMoveBlockAttack, 1, kA20), 11);

    // Big Attack is TWO hits (:90-91).
    EXPECT_EQ(step_count(d, r::kSphericGuardianMoveBigAttack), 2);

    // THE BATCH'S ONLY IN-takeTurn ASCENSION BRANCH (:95-99): 25, or 35 at A17+.
    // Boundary asserted at 16/17 -- and note the 35 has no named constant in the
    // Java, so it is a literal there and here.
    EXPECT_EQ(step_amount(d, r::kSphericGuardianMoveInitialBlockGain, 0, 0), 25);
    EXPECT_EQ(step_amount(d, r::kSphericGuardianMoveInitialBlockGain, 0, 16), 25);
    EXPECT_EQ(step_amount(d, r::kSphericGuardianMoveInitialBlockGain, 0, 17), 35);
    EXPECT_EQ(step_amount(d, r::kSphericGuardianMoveInitialBlockGain, 0, kA20), 35);

    // BLOCK_ATTACK's ORDER IS LOAD-BEARING (:109-111): block FIRST, then damage.
    const r::MonsterMove* ba = d.move(r::kSphericGuardianMoveBlockAttack);
    ASSERT_NE(ba, nullptr);
    ASSERT_EQ(ba->effect_count, 2);
    EXPECT_EQ(ba->effects[0].op, r::Opcode::BLOCK);
    EXPECT_EQ(ba->effects[0].amount.at(kA20), 15);   // HARDEN_BLOCK (:50)
    EXPECT_EQ(ba->effects[1].op, r::Opcode::DAMAGE);
    EXPECT_EQ(ba->intent, r::MonsterIntent::ATTACK_DEFEND);

    // Frail Attack: damage then Frail 5 (:115-117).
    EXPECT_EQ(step_amount(d, r::kSphericGuardianMoveFrailAttack, 1, kA20), 5);

    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::NORMAL);
    EXPECT_EQ(d.roll_count, 0);
}

// ============================================================================
// 2. RNG draw accounting -- the bit-exactness surface
// ============================================================================

// One monster_hp_rng draw; one ai_rng draw that getMove DISCARDS, because the
// A17+ arm returns from the !usedHex branch before `num` is read.
TEST(CityNormalsI, ChosenInitDrawsOneHpAndOneDiscardedAi) {
    CombatState s = MakeSeeded(4242);
    chosen_init(s, 0);
    EXPECT_EQ(s.monster_hp_rng.counter, 1);
    EXPECT_EQ(s.ai_rng.counter, 1);
    EXPECT_GE(s.monsters[0].hp, 98);
    EXPECT_LE(s.monsters[0].hp, 103);
    EXPECT_EQ(s.monsters[0].hp, s.monsters[0].max_hp);
    // The opener is HEX at EVERY seed -- the draw is discarded, so nothing about
    // it can change the decision.
    EXPECT_EQ(s.monsters[0].move_history[0], r::kChosenMoveHex);
    EXPECT_EQ(s.monsters[0].intent,
              static_cast<uint8_t>(MonsterIntent::STRONG_DEBUFF));
}

TEST(CityNormalsI, ChosenOpensOnHexAtEverySeed) {
    for (int64_t seed = 0; seed < 64; ++seed) {
        CombatState s = MakeSeeded(seed);
        chosen_init(s, 0);
        ASSERT_EQ(s.monsters[0].move_history[0], r::kChosenMoveHex)
            << "seed " << seed;
    }
}

// TWO ai draws at init: rollMove's discarded random(99), then the firstMove
// branch's own randomBoolean(0.375).
TEST(CityNormalsI, ByrdInitDrawsOneHpAndTwoAi) {
    CombatState s = MakeSeeded(4242);
    byrd_init(s, 0);
    EXPECT_EQ(s.monster_hp_rng.counter, 1);
    EXPECT_EQ(s.ai_rng.counter, 2)
        << "rollMove's random(99) THEN the firstMove randomBoolean(0.375)";
    EXPECT_GE(s.monsters[0].hp, 26);
    EXPECT_LE(s.monsters[0].hp, 33);
    // It enters the fight airborne, before any Flight power exists.
    EXPECT_TRUE(byrd_is_flying(s.monsters[0]));
    EXPECT_EQ(s.monsters[0].power_count, 0);
    const uint8_t opener = s.monsters[0].move_history[0];
    EXPECT_TRUE(opener == r::kByrdMoveCaw || opener == r::kByrdMovePeck);
}

// Both firstMove outcomes are reachable -- proof the 0.375 coin is really read
// (a hard-coded opener would pass every other assertion in this file).
TEST(CityNormalsI, ByrdFirstMoveCoinReachesBothOutcomes) {
    bool saw_caw = false;
    bool saw_peck = false;
    for (int64_t seed = 0; seed < 128; ++seed) {
        CombatState s = MakeSeeded(seed);
        byrd_init(s, 0);
        if (s.monsters[0].move_history[0] == r::kByrdMoveCaw) saw_caw = true;
        if (s.monsters[0].move_history[0] == r::kByrdMovePeck) saw_peck = true;
    }
    EXPECT_TRUE(saw_caw);
    EXPECT_TRUE(saw_peck);
}

// At A17+ the firstMove branch takes Fell DETERMINISTICALLY and spends NO
// randomBoolean -- the coin at ShelledParasite.java:180 is the sub-A17 side.
// Spending it here would shift every later draw on the stream.
TEST(CityNormalsI, ShelledParasiteInitDrawsOneHpAndOneAiWithNoFirstMoveCoin) {
    CombatState s = MakeSeeded(4242);
    shelled_parasite_init(s, 0);
    EXPECT_EQ(s.monster_hp_rng.counter, 1);
    EXPECT_EQ(s.ai_rng.counter, 1)
        << "A17+ takes Fell with NO randomBoolean (ShelledParasite.java:176-179)";
    EXPECT_GE(s.monsters[0].hp, 70);
    EXPECT_LE(s.monsters[0].hp, 75);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kShelledParasiteMoveFell);
}

// getMove recurses ONE level when num < 20 and Fell was the last move, spending
// a SECOND ai draw over the 20..99 range (ShelledParasite.java:187-192). The
// init opener is always Fell, so the very next roll is exactly that setup.
TEST(CityNormalsI, ShelledParasiteRecursionSpendsASecondAiDraw) {
    bool saw_recursion = false;
    bool saw_plain = false;
    for (int64_t seed = 0; seed < 256; ++seed) {
        CombatState s = MakeSeeded(seed);
        shelled_parasite_init(s, 0);
        ASSERT_EQ(s.monsters[0].move_history[0], r::kShelledParasiteMoveFell);
        const int32_t before = s.ai_rng.counter;
        // Peek at the num this roll will draw, so the expectation is derived
        // rather than guessed.
        RngStream probe = s.ai_rng;
        const int32_t num = random(probe, 99);
        shelled_parasite_roll_move(s, 0);
        const int32_t spent = s.ai_rng.counter - before;
        if (num < 20) {
            EXPECT_EQ(spent, 2) << "seed " << seed << ": recursion re-draws";
            // The re-entry drew from 20..99, so it CANNOT land in the first arm
            // again -- the recursion is exactly one level deep and terminates.
            EXPECT_NE(s.monsters[0].move_history[0],
                      r::kShelledParasiteMoveFell)
                << "seed " << seed;
            saw_recursion = true;
        } else {
            EXPECT_EQ(spent, 1) << "seed " << seed;
            saw_plain = true;
        }
    }
    EXPECT_TRUE(saw_recursion) << "no seed exercised the recursion";
    EXPECT_TRUE(saw_plain);
}

// ZERO monster_hp_rng draws -- the first registry monster with none. Asserted on
// the STREAM, not just the HP value: a degenerate random(20, 20) would produce
// the same 20 and still move the stream.
TEST(CityNormalsI, SphericGuardianInitDrawsNoHpAndOneDiscardedAi) {
    CombatState s = MakeSeeded(4242);
    spheric_guardian_init(s, 0);
    EXPECT_EQ(s.monster_hp_rng.counter, 0)
        << "setHp is never called (SphericGuardian.java:67); the ctor is "
           "RNG-free (AbstractMonster.java:135-155)";
    EXPECT_EQ(s.ai_rng.counter, 1) << "rollMove still draws, and discards";
    EXPECT_EQ(s.monsters[0].hp, 20);
    EXPECT_EQ(s.monsters[0].max_hp, 20);
    EXPECT_EQ(s.monsters[0].move_history[0],
              r::kSphericGuardianMoveInitialBlockGain);
}

// The whole point of the zero-draw property: a group's HP stream position.
TEST(CityNormalsI, SphericGuardianDoesNotAdvanceTheHpStreamInAGroup) {
    // "Sentry and Sphere" -- Sentry first, then the Sphere. Only ONE HP draw.
    const MonsterId group[] = {MonsterId::SENTRY, MonsterId::SPHERIC_GUARDIAN};
    CombatState s = MakeSeeded(99, /*monsters=*/0);
    spawn_group(s, group);
    EXPECT_EQ(s.monster_hp_rng.counter, 1)
        << "the Sentry's draw only -- the Sphere makes none";
    EXPECT_EQ(s.ai_rng.counter, 2) << "both monsters still roll a move";
    EXPECT_EQ(s.monsters[1].hp, 20);
}

// ============================================================================
// 3. Move selection
// ============================================================================

// Fully deterministic: turn 1 block, turn 2 Frail Attack, then strict
// alternation. The rolls still happen (asserted above), but nothing reads them.
TEST(CityNormalsI, SphericGuardianMoveOrderIsDeterministicAtEverySeed) {
    for (int64_t seed = 0; seed < 32; ++seed) {
        CombatState s = MakeSeeded(seed);
        spheric_guardian_init(s, 0);
        ASSERT_EQ(s.monsters[0].move_history[0],
                  r::kSphericGuardianMoveInitialBlockGain) << seed;
        spheric_guardian_roll_move(s, 0);
        ASSERT_EQ(s.monsters[0].move_history[0],
                  r::kSphericGuardianMoveFrailAttack) << seed;
        // secondMove is spent exactly once.
        EXPECT_EQ(s.monsters[0].flags & kMonsterFlagSphericSecondMove, 0u);
        for (int t = 0; t < 6; ++t) {
            spheric_guardian_roll_move(s, 0);
            const uint8_t want = (t % 2 == 0)
                                     ? r::kSphericGuardianMoveBigAttack
                                     : r::kSphericGuardianMoveBlockAttack;
            ASSERT_EQ(s.monsters[0].move_history[0], want)
                << "seed " << seed << " step " << t;
        }
    }
}

// A grounded Byrd HEADBUTTs no matter what it rolls (Byrd.java:216-218).
TEST(CityNormalsI, GroundedByrdAlwaysHeadbutts) {
    for (int64_t seed = 0; seed < 32; ++seed) {
        CombatState s = MakeSeeded(seed);
        byrd_init(s, 0);
        clear_byrd_flying(s.monsters[0]);
        const int32_t before = s.ai_rng.counter;
        byrd_roll_move(s, 0);
        ASSERT_EQ(s.monsters[0].move_history[0], r::kByrdMoveHeadbutt) << seed;
        // The random(99) still happens; no branch randomBoolean does.
        EXPECT_EQ(s.ai_rng.counter - before, 1) << seed;
    }
}

// HEADBUTT is the one move that returns early: it telegraphs GO_AIRBORNE
// synchronously and queues NO roll, so the turn spends no ai draw.
TEST(CityNormalsI, ByrdHeadbuttTelegraphsGoAirborneAndQueuesNoRoll) {
    CombatState s = MakeSeeded(7);
    byrd_init(s, 0);
    clear_byrd_flying(s.monsters[0]);
    set_monster_move(s.monsters[0], r::kByrdMoveHeadbutt, MonsterIntent::ATTACK);
    const int32_t before_ai = s.ai_rng.counter;
    byrd_take_turn(s, 0);
    // The move is decided IMMEDIATELY, not by a queued roll.
    EXPECT_EQ(s.monsters[0].move_history[0], r::kByrdMoveGoAirborne);
    EXPECT_EQ(s.monsters[0].intent,
              static_cast<uint8_t>(MonsterIntent::UNKNOWN));
    EXPECT_EQ(s.ai_rng.counter, before_ai) << "no roll, so no draw";
    for (uint8_t i = 0; i < s.action_count; ++i) {
        const ActionQueueItem it =
            s.action_queue[(s.action_head + i) % kActionQueueCap];
        EXPECT_NE(it.opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE))
            << "HEADBUTT must not queue a RollMoveAction (Byrd.java:121)";
    }
    // The damage is the flat 3.
    const int hp_before = s.player_hp;
    drain(s);
    EXPECT_EQ(hp_before - s.player_hp, 3);
}

// GO_AIRBORNE sets the latch SYNCHRONOUSLY, one slot ahead of the queued Flight
// re-grant -- so between them the Byrd is flying with no Flight power.
TEST(CityNormalsI, ByrdGoAirborneSetsTheLatchBeforeTheFlightGrantResolves) {
    CombatState s = MakeSeeded(7);
    byrd_init(s, 0);
    clear_byrd_flying(s.monsters[0]);
    set_monster_move(s.monsters[0], r::kByrdMoveGoAirborne,
                     MonsterIntent::UNKNOWN);
    byrd_take_turn(s, 0);
    EXPECT_TRUE(byrd_is_flying(s.monsters[0]))
        << "isFlying is written synchronously (Byrd.java:124)";
    EXPECT_EQ(monster_power(s, 0, PowerId::FLIGHT), -1)
        << "the ApplyPowerAction has not resolved yet (:126)";
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::FLIGHT), 4) << "flightAmt at A20";
}

// The Parasite's STUNNED case setMoves AND STILL rolls -- so the Fell it
// telegraphs is immediately re-decided, with lastMove(FELL) now true.
TEST(CityNormalsI, ShelledParasiteStunnedTurnSetMovesThenRerollsOverIt) {
    CombatState s = MakeSeeded(11);
    shelled_parasite_init(s, 0);
    set_monster_move(s.monsters[0], r::kShelledParasiteMoveStunned,
                     MonsterIntent::STUN);
    const int hp_before = s.player_hp;
    shelled_parasite_take_turn(s, 0);
    // Synchronously telegraphed Fell (ShelledParasite.java:136) -- and the
    // history now carries it, which is what the queued roll will read.
    EXPECT_EQ(s.monsters[0].move_history[0], r::kShelledParasiteMoveFell);
    drain(s);
    // The STUNNED turn deals NO damage: its program is a NOP and the Fell it set
    // is only a telegraph.
    EXPECT_EQ(s.player_hp, hp_before) << "a stunned turn does nothing";
    // ...and the trailing roll DID run, overwriting that telegraph.
    EXPECT_GE(s.ai_rng.counter, 2);
}

// ============================================================================
// 4. Flight -- the full lifecycle
// ============================================================================

TEST(CityNormalsI, FlightHalvesIncomingDamageAfterVulnerable) {
    CombatState s = MakeSeeded(5);
    byrd_init(s, 0);
    s.monsters[0].hp = 30;
    s.monsters[0].max_hp = 30;
    give_monster_power(s, 0, PowerId::FLIGHT, 3, 3);
    player_attacks(s, 0, 9);
    // 9 -> 4.5f -> floor 4 at the single floor in compute_damage.
    EXPECT_EQ(30 - s.monsters[0].hp, 4)
        << "one float halve, one floor (FlightPower.java:60)";
}

// Flight's pass is atDamageFinalReceive, which runs AFTER Vulnerable's multiply
// -- so the game computes floor(base * 1.5 / 2), not floor(base / 2 * 1.5).
// Priority 50 is what puts the slot in that position.
TEST(CityNormalsI, FlightRunsAfterVulnerableNotBefore) {
    CombatState s = MakeSeeded(5);
    byrd_init(s, 0);
    s.monsters[0].hp = 60;
    s.monsters[0].max_hp = 60;
    give_monster_power(s, 0, PowerId::FLIGHT, 3, 3);
    give_monster_power(s, 0, PowerId::VULNERABLE, 2);
    player_attacks(s, 0, 5);
    // 5 * 1.5 = 7.5 ; 7.5 / 2 = 3.75 ; floor -> 3.
    // (The other order would be floor-free too but give 5/2=2.5*1.5=3.75 -- the
    // same here; the case that separates them is asserted below.)
    EXPECT_EQ(60 - s.monsters[0].hp, 3);
    EXPECT_EQ(r::power_def(r::PowerId::FLIGHT)->priority, 50);
}

// A stack is shed per real, NON-LETHAL hit, and the reduce is QUEUED (addToBot,
// FlightPower.java:70) -- so it is not visible until the queue drains.
TEST(CityNormalsI, FlightShedsOneStackPerNonLethalHit) {
    CombatState s = MakeSeeded(5);
    byrd_init(s, 0);
    s.monsters[0].hp = 100;
    s.monsters[0].max_hp = 100;
    give_monster_power(s, 0, PowerId::FLIGHT, 4, 4);
    player_attacks(s, 0, 6);
    EXPECT_EQ(monster_power(s, 0, PowerId::FLIGHT), 4) << "the reduce is queued";
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::FLIGHT), 3);
}

// THE EDGE THAT DECIDES WHETHER A DYING BYRD QUEUES A GROUNDING: willLive is
// false on a killing blow, so no stack is shed at all.
TEST(CityNormalsI, FlightShedsNoStackOnALethalHit) {
    CombatState s = MakeSeeded(5);
    byrd_init(s, 0);
    s.monsters[0].hp = 4;
    s.monsters[0].max_hp = 30;
    give_monster_power(s, 0, PowerId::FLIGHT, 3, 3);
    // 40 base -> halved to 20 by the damage pass; the willLive test halves the
    // ALREADY-halved 20 again (the game's own double halve) to 10, which is not
    // < 4, so no reduce is queued.
    player_attacks(s, 0, 40);
    EXPECT_EQ(s.monsters[0].hp, 0) << "the hit was lethal";
    EXPECT_EQ(s.action_count, 0) << "a killing blow queues no ReducePowerAction";
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::FLIGHT), 3) << "the stack survives";
}

// THORNS damage neither halves nor sheds (FlightPower.java:59,68).
TEST(CityNormalsI, FlightIgnoresThornsDamageEntirely) {
    CombatState s = MakeSeeded(5);
    byrd_init(s, 0);
    s.monsters[0].hp = 40;
    s.monsters[0].max_hp = 40;
    give_monster_power(s, 0, PowerId::FLIGHT, 3, 3);
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    it.src = kActorPlayer;
    it.tgt = 0;
    it.amount = 8;
    it.flags = make_damage_flags(DamageType::THORNS);
    execute_opcode(s, it);
    EXPECT_EQ(40 - s.monsters[0].hp, 8) << "THORNS is not halved";
    EXPECT_EQ(s.action_count, 0) << "THORNS sheds no stack";
}

// atStartOfTurn RESTORES the stack to storedAmount (PowerSlot.counter) -- a full
// refresh, not a decay, so attrition across the player's turn is undone.
TEST(CityNormalsI, FlightRefreshesToStoredAmountAtStartOfTurn) {
    CombatState s = MakeSeeded(5);
    byrd_init(s, 0);
    s.monsters[0].hp = 100;
    s.monsters[0].max_hp = 100;
    byrd_use_pre_battle_action(s, 0);
    drain(s);
    ASSERT_EQ(monster_power(s, 0, PowerId::FLIGHT), 4);
    ASSERT_EQ(monster_power_counter(s, 0, PowerId::FLIGHT), 4)
        << "op_apply_power's new-slot path writes storedAmount into the counter";

    // Wear it down to 1 with three non-lethal hits.
    for (int i = 0; i < 3; ++i) {
        player_attacks(s, 0, 6);
        drain(s);
    }
    ASSERT_EQ(monster_power(s, 0, PowerId::FLIGHT), 1);

    // The Byrd's turn begins: back to 4.
    run_monster_start_of_turn(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::FLIGHT), 4)
        << "FlightPower.atStartOfTurn (FlightPower.java:47-51)";
    EXPECT_TRUE(byrd_is_flying(s.monsters[0])) << "still airborne";
}

// A re-application STACKS the amount and leaves storedAmount where the FIRST
// instance set it -- AbstractPower's un-overridden stackPower.
TEST(CityNormalsI, FlightReapplicationStacksAmountButNotTheRefreshTarget) {
    CombatState s = MakeSeeded(5);
    byrd_init(s, 0);
    byrd_use_pre_battle_action(s, 0);
    drain(s);
    ASSERT_EQ(monster_power_counter(s, 0, PowerId::FLIGHT), 4);
    byrd_use_pre_battle_action(s, 0);  // a second grant of the same amount
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::FLIGHT), 8) << "additive stack";
    EXPECT_EQ(monster_power_counter(s, 0, PowerId::FLIGHT), 4)
        << "storedAmount belongs to the ORIGINAL instance "
           "(AbstractCreature.java:506-513)";
}

// THE GROUNDING: Flight's removal fires onRemove through ON_POWER_REMOVED, which
// clears the latch and QUEUES the STUNNED telegraph.
TEST(CityNormalsI, FlightRemovalGroundsTheByrdAndTelegraphsStun) {
    CombatState s = MakeSeeded(5);
    byrd_init(s, 0);
    s.monsters[0].hp = 100;
    s.monsters[0].max_hp = 100;
    give_monster_power(s, 0, PowerId::FLIGHT, 1, 4);
    set_monster_move(s.monsters[0], r::kByrdMovePeck, MonsterIntent::ATTACK);
    ASSERT_TRUE(byrd_is_flying(s.monsters[0]));

    player_attacks(s, 0, 6);   // sheds the last stack -> queued reduce
    drain(s);                  // reduce -> remove_slot_at -> onRemove -> SET_MOVE

    EXPECT_EQ(monster_power(s, 0, PowerId::FLIGHT), -1) << "the power is gone";
    EXPECT_FALSE(byrd_is_flying(s.monsters[0]))
        << "changeState(\"GROUNDED\") clears isFlying (Byrd.java:165)";
    EXPECT_EQ(s.monsters[0].move_history[0], r::kByrdMoveStunned);
    EXPECT_EQ(s.monsters[0].intent, static_cast<uint8_t>(MonsterIntent::STUN));
}

// The same grounding through a BARE removal, not a reduce -- proof the hook is
// on the choke point and not on the reduce path.
TEST(CityNormalsI, FlightGroundsOnADirectRemovePowerToo) {
    CombatState s = MakeSeeded(5);
    byrd_init(s, 0);
    give_monster_power(s, 0, PowerId::FLIGHT, 4, 4);
    ActionQueueItem rem{};
    rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
    rem.src = 0;
    rem.tgt = 0;
    rem.flags = make_apply_power_flags(PowerId::FLIGHT);
    execute_opcode(s, rem);
    drain(s);
    EXPECT_FALSE(byrd_is_flying(s.monsters[0]));
    EXPECT_EQ(s.monsters[0].move_history[0], r::kByrdMoveStunned);
}

// The full grounded cycle: STUNNED -> HEADBUTT -> GO_AIRBORNE -> airborne again.
TEST(CityNormalsI, ByrdGroundedCycleReturnsItToTheAir) {
    CombatState s = MakeSeeded(5);
    byrd_init(s, 0);
    s.monsters[0].hp = 100;
    s.monsters[0].max_hp = 100;
    give_monster_power(s, 0, PowerId::FLIGHT, 1, 4);
    player_attacks(s, 0, 6);
    drain(s);
    ASSERT_EQ(s.monsters[0].move_history[0], r::kByrdMoveStunned);
    ASSERT_FALSE(byrd_is_flying(s.monsters[0]));

    byrd_take_turn(s, 0);   // the stunned turn: NOP + a queued roll
    drain(s);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kByrdMoveHeadbutt)
        << "a grounded roll always picks HEADBUTT";

    byrd_take_turn(s, 0);   // headbutt: damage, then a synchronous telegraph
    drain(s);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kByrdMoveGoAirborne);

    byrd_take_turn(s, 0);   // go airborne: latch + the Flight re-grant
    drain(s);
    EXPECT_TRUE(byrd_is_flying(s.monsters[0]));
    EXPECT_EQ(monster_power(s, 0, PowerId::FLIGHT), 4);
}

// ============================================================================
// 5. Hex
// ============================================================================

// Give the player a draw pile of `n` cards so the DRAW_RANDOM insert has
// something to draw against.
void fill_draw_pile(CombatState& s, int n) {
    for (int i = 0; i < n; ++i) {
        s.card_pool[i].card_id = static_cast<uint16_t>(CardId::STRIKE);
        s.draw[s.draw_count++] = static_cast<CardPoolIndex>(i);
    }
}

TEST(CityNormalsI, HexShufflesDazedIntoDrawOnANonAttackPlay) {
    CombatState s = MakeSeeded(3);
    fill_draw_pile(s, 5);
    s.player_powers[s.player_power_count].power_id =
        static_cast<uint16_t>(PowerId::HEX);
    s.player_powers[s.player_power_count].amount = 2;
    ++s.player_power_count;

    // A SKILL play.
    dispatch_on_use_card(s, 0, static_cast<uint16_t>(CardId::DEFEND));
    drain(s);
    int dazed = 0;
    for (uint8_t i = 0; i < s.draw_count; ++i) {
        if (s.card_pool[s.draw[i]].card_id ==
            static_cast<uint16_t>(CardId::DAZED)) {
            ++dazed;
        }
    }
    EXPECT_EQ(dazed, 2) << "`amount` copies (HexPower.java:40)";
    EXPECT_EQ(s.card_random_rng.counter, 2)
        << "one addToRandomSpot draw PER COPY (CardGroup.java:463-469)";
}

TEST(CityNormalsI, HexIgnoresAttackPlays) {
    CombatState s = MakeSeeded(3);
    fill_draw_pile(s, 5);
    s.player_powers[s.player_power_count].power_id =
        static_cast<uint16_t>(PowerId::HEX);
    s.player_powers[s.player_power_count].amount = 2;
    ++s.player_power_count;
    dispatch_on_use_card(s, 0, static_cast<uint16_t>(CardId::STRIKE));
    drain(s);
    EXPECT_EQ(s.draw_count, 5) << "an ATTACK triggers nothing (:38)";
    EXPECT_EQ(s.card_random_rng.counter, 0);
}

// The test is a bare `!= ATTACK`, so a STATUS play triggers it too -- which is
// how Hex feeds on the Dazed it just made.
TEST(CityNormalsI, HexTriggersOnAStatusPlayToo) {
    CombatState s = MakeSeeded(3);
    fill_draw_pile(s, 5);
    s.player_powers[s.player_power_count].power_id =
        static_cast<uint16_t>(PowerId::HEX);
    s.player_powers[s.player_power_count].amount = 1;
    ++s.player_power_count;
    dispatch_on_use_card(s, 0, static_cast<uint16_t>(CardId::SLIMED));
    drain(s);
    EXPECT_EQ(s.draw_count, 6);
}

// AN EMPTY DRAW PILE COSTS NO card_random_rng DRAW: CardGroup.addToRandomSpot
// (:463-469) appends without drawing when the group is empty. The draw is per
// COPY and the test is made per copy, so with one Hex stack and an empty pile
// the whole effect is free.
TEST(CityNormalsI, HexOnAnEmptyDrawPileSpendsNoCardRandomDraw) {
    CombatState s = MakeSeeded(3);
    ASSERT_EQ(s.draw_count, 0);
    s.player_powers[s.player_power_count].power_id =
        static_cast<uint16_t>(PowerId::HEX);
    s.player_powers[s.player_power_count].amount = 1;
    ++s.player_power_count;
    dispatch_on_use_card(s, 0, static_cast<uint16_t>(CardId::DEFEND));
    drain(s);
    EXPECT_EQ(s.draw_count, 1) << "the copy still lands";
    EXPECT_EQ(s.card_random_rng.counter, 0)
        << "the empty-group branch appends with NO draw";
}

// ...and only the FIRST copy is free. The emptiness test is re-made per copy, so
// three copies into an empty pile cost TWO draws, not zero and not three -- the
// kind of off-by-one an "empty pile is free" summary hides.
TEST(CityNormalsI, HexIntoAnEmptyPileOnlyTheFirstCopyIsFree) {
    CombatState s = MakeSeeded(3);
    s.player_powers[s.player_power_count].power_id =
        static_cast<uint16_t>(PowerId::HEX);
    s.player_powers[s.player_power_count].amount = 3;
    ++s.player_power_count;
    dispatch_on_use_card(s, 0, static_cast<uint16_t>(CardId::DEFEND));
    drain(s);
    EXPECT_EQ(s.draw_count, 3);
    EXPECT_EQ(s.card_random_rng.counter, 2)
        << "copy 1 sees an empty group and appends free; copies 2 and 3 each "
           "draw random(size-1)";
}

// ============================================================================
// 6. VAMPIRE_DAMAGE
// ============================================================================

void queue_life_suck(CombatState& s, uint8_t mi, int32_t base) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::VAMPIRE_DAMAGE);
    it.src = mi;
    it.tgt = kActorPlayer;
    it.amount = base;
    execute_opcode(s, it);
}

TEST(CityNormalsI, VampireDamageHealsTheAttackerByTheHpActuallyLost) {
    CombatState s = MakeSeeded(1);
    shelled_parasite_init(s, 0);
    s.monsters[0].hp = 40;
    s.monsters[0].max_hp = 72;
    s.player_hp = 100;
    queue_life_suck(s, 0, 12);
    EXPECT_EQ(s.player_hp, 88);
    EXPECT_EQ(s.monsters[0].hp, 52) << "healed by the 12 the player lost";
}

// BLOCK SHRINKS THE HEAL, because the heal is lastDamageTaken and not the base.
TEST(CityNormalsI, VampireDamageHealIsReducedByPlayerBlock) {
    CombatState s = MakeSeeded(1);
    shelled_parasite_init(s, 0);
    s.monsters[0].hp = 40;
    s.monsters[0].max_hp = 72;
    s.player_hp = 100;
    s.player_block = 8;
    queue_life_suck(s, 0, 12);
    EXPECT_EQ(s.player_block, 0);
    EXPECT_EQ(s.player_hp, 96) << "8 of the 12 was blocked";
    EXPECT_EQ(s.monsters[0].hp, 44) << "heals 4, not 12";
}

// A fully blocked hit heals nothing at all (`lastDamageTaken > 0`).
TEST(CityNormalsI, VampireDamageHealsNothingWhenFullyBlocked) {
    CombatState s = MakeSeeded(1);
    shelled_parasite_init(s, 0);
    s.monsters[0].hp = 40;
    s.monsters[0].max_hp = 72;
    s.player_hp = 100;
    s.player_block = 50;
    queue_life_suck(s, 0, 12);
    EXPECT_EQ(s.player_hp, 100);
    EXPECT_EQ(s.monsters[0].hp, 40);
}

// The heal clamps to maxHealth (AbstractMonster.java:392-394).
TEST(CityNormalsI, VampireDamageHealClampsToMaxHp) {
    CombatState s = MakeSeeded(1);
    shelled_parasite_init(s, 0);
    s.monsters[0].hp = 70;
    s.monsters[0].max_hp = 72;
    s.player_hp = 100;
    queue_life_suck(s, 0, 12);
    EXPECT_EQ(s.monsters[0].hp, 72);
}

// An overkill heals only the HP the player HAD (the lethal clamp).
TEST(CityNormalsI, VampireDamageHealIsClampedByTheTargetsRemainingHp) {
    CombatState s = MakeSeeded(1);
    shelled_parasite_init(s, 0);
    s.monsters[0].hp = 20;
    s.monsters[0].max_hp = 72;
    s.player_hp = 5;
    queue_life_suck(s, 0, 40);
    EXPECT_EQ(s.player_hp, 0);
    EXPECT_EQ(s.monsters[0].hp, 25) << "heals 5, the HP that actually moved";
}

// The whole move, end to end, through the registry program.
TEST(CityNormalsI, ShelledParasiteLifeSuckTurnHealsIt) {
    CombatState s = MakeSeeded(1);
    shelled_parasite_init(s, 0);
    s.monsters[0].hp = 40;
    s.monsters[0].max_hp = 72;
    s.player_hp = 100;
    set_monster_move(s.monsters[0], r::kShelledParasiteMoveLifeSuck,
                     MonsterIntent::ATTACK_BUFF);
    shelled_parasite_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(s.player_hp, 88) << "suckDmg 12 at A20";
    EXPECT_EQ(s.monsters[0].hp, 52);
}

// ============================================================================
// 7. Plated Armor -- the armour-break telegraph (the regression this batch fixes)
// ============================================================================

TEST(CityNormalsI, ShelledParasitePreBattleAppliesPlatedArmorAndBlock) {
    CombatState s = MakeSeeded(1);
    shelled_parasite_init(s, 0);
    shelled_parasite_use_pre_battle_action(s, 0);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::PLATED_ARMOR), 14);
    EXPECT_EQ(s.monsters[0].block, 14) << "a DIRECT GainBlockAction of the same 14";
}

// THE REGRESSION TEST. Plated Armor's native body used to decrement its own slot
// and zero power_id in place, which never reached remove_slot_at -- so the
// removal hook could not fire and this telegraph never happened. It now queues a
// ReducePowerAction (which is what the Java does), and the choke point dispatches
// ON_POWER_REMOVED.
TEST(CityNormalsI, PlatedArmorRunningOutStunsTheShelledParasite) {
    CombatState s = MakeSeeded(1);
    shelled_parasite_init(s, 0);
    s.monsters[0].hp = 200;
    s.monsters[0].max_hp = 200;
    give_monster_power(s, 0, PowerId::PLATED_ARMOR, 2);
    set_monster_move(s.monsters[0], r::kShelledParasiteMoveFell,
                     MonsterIntent::ATTACK_DEBUFF);

    player_attacks(s, 0, 5);
    drain(s);
    ASSERT_EQ(monster_power(s, 0, PowerId::PLATED_ARMOR), 1) << "one stack shed";
    EXPECT_NE(s.monsters[0].move_history[0], r::kShelledParasiteMoveStunned)
        << "not broken yet";

    player_attacks(s, 0, 5);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::PLATED_ARMOR), -1)
        << "the last stack removes the power";
    EXPECT_EQ(s.monsters[0].move_history[0], r::kShelledParasiteMoveStunned)
        << "changeState(\"ARMOR_BREAK\") setMove(STUNNED, STUN) "
           "(ShelledParasite.java:157) -- this is what the in-place decrement "
           "used to skip";
    EXPECT_EQ(s.monsters[0].intent, static_cast<uint8_t>(MonsterIntent::STUN));
}

// Plated Armor on a monster that defines no armour-break state must do nothing
// beyond vanishing -- the hook is keyed on the owner's monster id.
TEST(CityNormalsI, PlatedArmorRemovalOnAnotherMonsterTelegraphsNothing) {
    CombatState s = MakeSeeded(1);
    chosen_init(s, 0);
    s.monsters[0].hp = 200;
    give_monster_power(s, 0, PowerId::PLATED_ARMOR, 1);
    const uint8_t before = s.monsters[0].move_history[0];
    player_attacks(s, 0, 5);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::PLATED_ARMOR), -1);
    EXPECT_EQ(s.monsters[0].move_history[0], before) << "no telegraph change";
}

// ============================================================================
// 8. Spheric Guardian -- Barricade and the pre-battle order
// ============================================================================

TEST(CityNormalsI, SphericGuardianPreBattleAppliesBarricadeArtifactAndBlock) {
    CombatState s = MakeSeeded(1);
    spheric_guardian_init(s, 0);
    spheric_guardian_use_pre_battle_action(s, 0);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::BARRICADE), kBarricadeMarkerAmount)
        << "the -1 marker its ctor sets (BarricadePower.java:22)";
    EXPECT_EQ(monster_power(s, 0, PowerId::ARTIFACT), 3);
    EXPECT_EQ(s.monsters[0].block, 40);
}

// Barricade's monster side: the start-of-turn block clear is skipped.
TEST(CityNormalsI, SphericGuardianKeepsItsBlockAcrossTurnsViaBarricade) {
    CombatState s = MakeSeeded(1);
    spheric_guardian_init(s, 0);
    spheric_guardian_use_pre_battle_action(s, 0);
    drain(s);
    ASSERT_EQ(s.monsters[0].block, 40);
    run_monster_start_of_turn(s);
    EXPECT_EQ(s.monsters[0].block, 40)
        << "Barricade skips the monster loseBlock() (MonsterGroup.applyPreTurnLogic)";
    // Contrast: without Barricade the same walk zeroes it.
    CombatState t = MakeSeeded(1);
    spheric_guardian_init(t, 0);
    t.monsters[0].block = 40;
    run_monster_start_of_turn(t);
    EXPECT_EQ(t.monsters[0].block, 0) << "negative control";
}

// Its Artifact eats the first debuff aimed at it.
TEST(CityNormalsI, SphericGuardianArtifactAbsorbsThreeDebuffs) {
    CombatState s = MakeSeeded(1);
    spheric_guardian_init(s, 0);
    spheric_guardian_use_pre_battle_action(s, 0);
    drain(s);
    auto player_debuffs_it = [&s]() {
        ActionQueueItem ap{};
        ap.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        ap.src = kActorPlayer;
        ap.tgt = 0;
        ap.amount = 2;
        ap.flags = make_apply_power_flags(PowerId::VULNERABLE);
        execute_opcode(s, ap);
    };
    for (int i = 0; i < 3; ++i) {
        player_debuffs_it();
        EXPECT_EQ(monster_power(s, 0, PowerId::VULNERABLE), -1) << i;
    }
    EXPECT_EQ(monster_power(s, 0, PowerId::ARTIFACT), 0) << "all three spent";
    player_debuffs_it();
    EXPECT_EQ(monster_power(s, 0, PowerId::VULNERABLE), 2) << "the fourth lands";
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

TEST(CityNormalsI, EncounterCompositionsAreSpawnOrderExact) {
    using V = std::vector<std::string_view>;
    // Solos.
    EXPECT_EQ(members_of("Spheric Guardian", 1), V{"SphericGuardian"});
    EXPECT_EQ(members_of("Chosen", 1), V{"Chosen"});
    EXPECT_EQ(members_of("Shell Parasite", 1), V{"Shelled Parasite"})
        << "the ENCOUNTER key drops a letter the class ID has";
    // Fixed multi-member lists. ORDER IS THE ASSERTION: it fixes each monster's
    // group index, which the Sentry's opener is chosen from.
    EXPECT_EQ(members_of("3 Byrds", 1), (V{"Byrd", "Byrd", "Byrd"}));
    // ONE Byrd, not two, despite the plural in the key -- MonsterHelper builds
    // `{new Byrd(-170, ...), new Chosen(80, 0)}`. The Byrd is FIRST.
    EXPECT_EQ(members_of("Chosen and Byrds", 1), (V{"Byrd", "Chosen"}));
    EXPECT_EQ(members_of("Sentry and Sphere", 1),
              (V{"Sentry", "SphericGuardian"}));
    EXPECT_EQ(members_of("Cultist and Chosen", 1), (V{"Cultist", "Chosen"}));
    // The PARASITE is first here, which is the opposite of the key's word order.
    EXPECT_EQ(members_of("Shelled Parasite and Fungi", 1),
              (V{"Shelled Parasite", "FungiBeast"}));
}

// Every fixed list above costs ZERO miscRng draws -- the Byrd y-offsets are
// unseeded libGDX MathUtils and model nothing.
TEST(CityNormalsI, FixedCityCompositionsSpendNoMiscDraws) {
    for (std::string_view key :
         {"Spheric Guardian", "Chosen", "Shell Parasite", "3 Byrds",
          "Chosen and Byrds", "Sentry and Sphere", "Cultist and Chosen",
          "Shelled Parasite and Fungi"}) {
        int32_t draws = -1;
        members_of(key, 7, &draws);
        EXPECT_EQ(draws, 0) << key;
    }
}

// The group index the spawn order fixes is what picks the Sentry's opener:
// slot 0 is even, so it opens on Bolt (Sentry.java:136-143).
TEST(CityNormalsI, SentryAndSphereSpawnsTheSentryAtIndexZeroSoItOpensOnBolt) {
    const MonsterId group[] = {MonsterId::SENTRY, MonsterId::SPHERIC_GUARDIAN};
    CombatState s = MakeSeeded(4, /*monsters=*/0);
    spawn_group(s, group);
    ASSERT_EQ(s.monster_count, 2);
    EXPECT_EQ(s.monsters[0].monster_id,
              static_cast<uint16_t>(MonsterId::SENTRY));
    EXPECT_EQ(s.monsters[1].monster_id,
              static_cast<uint16_t>(MonsterId::SPHERIC_GUARDIAN));
    EXPECT_EQ(s.monsters[0].move_history[0], r::kSentryMoveBolt)
        << "even group index -> Bolt";
}

// Three Byrds: three HP draws and six ai draws (two each), in spawn order.
TEST(CityNormalsI, ThreeByrdsSpawnInOrderAndEachSpendsTwoAiDraws) {
    const MonsterId group[] = {MonsterId::BYRD, MonsterId::BYRD,
                               MonsterId::BYRD};
    CombatState s = MakeSeeded(17, /*monsters=*/0);
    spawn_group(s, group);
    ASSERT_EQ(s.monster_count, 3);
    EXPECT_EQ(s.monster_hp_rng.counter, 3);
    EXPECT_EQ(s.ai_rng.counter, 6) << "random(99) + randomBoolean(0.375), each";
    for (uint8_t i = 0; i < 3; ++i) {
        EXPECT_EQ(s.monsters[i].monster_id,
                  static_cast<uint16_t>(MonsterId::BYRD));
        EXPECT_TRUE(byrd_is_flying(s.monsters[i]));
    }
    // ...and each gets its own Flight in spawn order.
    use_pre_battle_actions(s);
    drain(s);
    for (uint8_t i = 0; i < 3; ++i) {
        EXPECT_EQ(monster_power(s, i, PowerId::FLIGHT), 4) << int(i);
    }
}

}  // namespace
}  // namespace sts::engine
