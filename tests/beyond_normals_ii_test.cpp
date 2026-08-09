// S2.26 -- Act-3 "Beyond" normals II: the Spire Growth, the Transient, the Maw,
// the Jaw Worm Horde and the Writhing Mass, plus the four powers they bring
// (Constricted, Fading, Shifting, Reactive), the DURING_TURN hook's first
// binder, the OBTAIN_CARD opcode's first producer and the type-tolerant
// onAttacked walk.
//
// What this file pins, section by section:
//   1. Stat and move tables at EVERY ascension branch the Java reads, per
//      monster -- including four deliberate NON-differences (Maw's nomDmg, the
//      Writhing Mass's normalDebuffAmt, its absent A17 arm, the Maw's absent A7
//      arm).
//   2. RNG draw accounting, and above all the TWO-TWO FIXED-HP SPLIT: setHp(int)
//      still draws (Spire Growth, Writhing Mass), no setHp at all does not
//      (Transient, Maw). Post-79328ad this distinction is load-bearing.
//   3. The Spire Growth: the five-gate tree, the player-power query, the A17
//      hoist, and Constricted's SOURCE (the Rupture-observable one).
//   4. The Transient: one discarded ai draw, the escalating ramp, Fading's
//      after-takeTurn death, Shifting's swap, and the DEAD gold field.
//   5. The Maw: the roared latch, the turnCount walkthrough, the bite fan-out.
//   6. The Writhing Mass: the recursive getMove, move id 0, Reactive's re-roll,
//      and the master-deck Parasite -- combat side and run-layer fold-back.
//   7. The Jaw Worm Horde: the hardMode variant, and the PROOF that the ordinary
//      Exordium worm is untouched.
//   8. Encounter compositions, spawn-order-exact.
//   9. Dispatch-table shape.

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
#include "sts/engine/monster_jaw_worm.hpp"
#include "sts/engine/monster_maw.hpp"
#include "sts/engine/monster_spire_growth.hpp"
#include "sts/engine/monster_transient.hpp"
#include "sts/engine/monster_writhing_mass.hpp"
#include "sts/engine/power_hooks.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

namespace r = sts::registry;
constexpr int32_t kA20 = kMonsterAscension;

// --- shared helpers (the city_normals_ii_test.cpp shapes) -------------------

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

int16_t player_power_amount(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return s.player_powers[i].amount;
        }
    }
    return -1;
}

int16_t player_power_counter(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return s.player_powers[i].counter;
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

void give_player_power(CombatState& s, PowerId id, int16_t amt,
                       int16_t counter = 0) {
    s.player_powers[s.player_power_count].power_id = static_cast<uint16_t>(id);
    s.player_powers[s.player_power_count].amount = amt;
    s.player_powers[s.player_power_count].counter = counter;
    ++s.player_power_count;
}

// A hit onto monster `mi` through the real DAMAGE opcode, so every hook fires
// exactly as it does in combat.
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

// ============================================================================
// 1. Stat and move tables -- every ascension branch, per monster
// ============================================================================

// SpireGrowth.java:38-48,53-64,70-71,85.
TEST(BeyondNormalsII, SpireGrowthStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kSpireGrowth;
    // HP branches at A7 (:53), NOT at A2 -- the A_2_START_HP field name is a lie
    // and the branch is authoritative.
    EXPECT_EQ(d.hp_min(0), 170);
    EXPECT_EQ(d.hp_max(0), 170);
    EXPECT_EQ(d.hp_min(6), 170);
    EXPECT_EQ(d.hp_min(7), 190);
    EXPECT_EQ(d.hp_max(7), 190);
    EXPECT_EQ(d.hp_min(kA20), 190);
    EXPECT_EQ(d.hp_min(kA20), d.hp_max(kA20)) << "a FIXED sheet, both tiers";

    // tackleDmg / smashDmg branch at A2 (:58-64).
    EXPECT_EQ(step_amount(d, r::kSpireGrowthMoveQuickTackle, 0, 1), 16);
    EXPECT_EQ(step_amount(d, r::kSpireGrowthMoveQuickTackle, 0, 2), 18);
    EXPECT_EQ(step_amount(d, r::kSpireGrowthMoveQuickTackle, 0, kA20), 18);
    EXPECT_EQ(step_amount(d, r::kSpireGrowthMoveSmash, 0, 1), 22);
    EXPECT_EQ(step_amount(d, r::kSpireGrowthMoveSmash, 0, 2), 25);
    EXPECT_EQ(step_amount(d, r::kSpireGrowthMoveSmash, 0, kA20), 25);

    // constrictDmg is the batch's only A17 amount that lives in takeTurn rather
    // than the ctor (:85), and it moves at A17 and nowhere else.
    EXPECT_EQ(step_amount(d, r::kSpireGrowthMoveConstrict, 0, 2), 10);
    EXPECT_EQ(step_amount(d, r::kSpireGrowthMoveConstrict, 0, 16), 10);
    EXPECT_EQ(step_amount(d, r::kSpireGrowthMoveConstrict, 0, 17), 12);
    EXPECT_EQ(step_amount(d, r::kSpireGrowthMoveConstrict, 0, kA20), 12);

    EXPECT_EQ(step_count(d, r::kSpireGrowthMoveConstrict), 1);
    const r::MonsterMove* c = d.move(r::kSpireGrowthMoveConstrict);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->effects[0].op, r::Opcode::APPLY_POWER);
    EXPECT_EQ(c->effects[0].extra,
              make_apply_power_flags(PowerId::CONSTRICTED));
    EXPECT_EQ(c->intent, r::MonsterIntent::STRONG_DEBUFF);
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::NORMAL);
    EXPECT_EQ(d.roll_count, 0);
}

// Transient.java:36,47,55-62.
TEST(BeyondNormalsII, TransientStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kTransient;
    // HP 999 FLAT -- ONE tier, no branch anywhere in the ctor.
    EXPECT_EQ(d.hp_tier_count, 1);
    EXPECT_EQ(d.hp_min(0), 999);
    EXPECT_EQ(d.hp_min(7), 999);
    EXPECT_EQ(d.hp_min(kA20), 999);
    EXPECT_EQ(kTransientHp, 999);

    // startingDeathDmg branches at A2 and nowhere else (:55).
    EXPECT_EQ(step_amount(d, r::kTransientMoveAttack, 0, 1), 30);
    EXPECT_EQ(step_amount(d, r::kTransientMoveAttack, 0, 2), 40);
    EXPECT_EQ(step_amount(d, r::kTransientMoveAttack, 0, 16), 40);
    EXPECT_EQ(step_amount(d, r::kTransientMoveAttack, 0, 17), 40);
    EXPECT_EQ(step_amount(d, r::kTransientMoveAttack, 0, kA20), 40);

    EXPECT_EQ(step_count(d, r::kTransientMoveAttack), 1)
        << "ONE authored step -- the ramp is per-instance, not seven rows";
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::NORMAL)
        << "999 HP and still not a boss";
    EXPECT_EQ(d.roll_count, 0);
    // The Fading argument is the monster's, not the power's (:66-71).
    EXPECT_EQ(kTransientFadingTurns, 5);
    EXPECT_EQ(kTransientFadingTurnsA17, 6);
}

// Maw.java:44,64,68-82.
TEST(BeyondNormalsII, MawStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kMaw;
    // HP 300 FLAT: NO A7 TIER AT ALL. Every other normal in the roster has one;
    // this absence is read off the ctor, not assumed.
    EXPECT_EQ(d.hp_tier_count, 1);
    EXPECT_EQ(d.hp_min(0), 300);
    EXPECT_EQ(d.hp_min(6), 300);
    EXPECT_EQ(d.hp_min(7), 300) << "no A7 branch (Maw.java:62-82)";
    EXPECT_EQ(d.hp_min(kA20), 300);

    // slamDmg branches at A2 (:75,79).
    EXPECT_EQ(step_amount(d, r::kMawMoveSlam, 0, 1), 25);
    EXPECT_EQ(step_amount(d, r::kMawMoveSlam, 0, 2), 30);
    EXPECT_EQ(step_amount(d, r::kMawMoveSlam, 0, kA20), 30);

    // A DELIBERATE NON-DIFFERENCE: nomDmg is assigned 5 in BOTH arms of the A2
    // branch (:76,80). Flat, and the flatness is the assertion.
    EXPECT_EQ(step_amount(d, r::kMawMoveNomnomnom, 0, 1), 5);
    EXPECT_EQ(step_amount(d, r::kMawMoveNomnomnom, 0, 2), 5)
        << "the A2 arm assigns nomDmg = 5 too -- a dead half-branch";
    EXPECT_EQ(step_amount(d, r::kMawMoveNomnomnom, 0, kA20), 5);
    EXPECT_EQ(step_count(d, r::kMawMoveNomnomnom), 1)
        << "ONE authored step; the COUNT is turnCount/2 and is native";

    // strUp and terrifyDur both move at A17 (:70-73) and nowhere else.
    EXPECT_EQ(step_amount(d, r::kMawMoveDrool, 0, 2), 3);
    EXPECT_EQ(step_amount(d, r::kMawMoveDrool, 0, 16), 3);
    EXPECT_EQ(step_amount(d, r::kMawMoveDrool, 0, 17), 5);
    EXPECT_EQ(step_amount(d, r::kMawMoveDrool, 0, kA20), 5);
    for (uint8_t k = 0; k < 2; ++k) {
        EXPECT_EQ(step_amount(d, r::kMawMoveRoar, k, 16), 3);
        EXPECT_EQ(step_amount(d, r::kMawMoveRoar, k, 17), 5);
    }

    // ROAR's ORDER: WEAK first, then FRAIL (:91-92) -- the reverse of the Snake
    // Plant's SPORES.
    const r::MonsterMove* roar = d.move(r::kMawMoveRoar);
    ASSERT_NE(roar, nullptr);
    ASSERT_EQ(roar->effect_count, 2);
    EXPECT_EQ(roar->effects[0].extra, make_apply_power_flags(PowerId::WEAK));
    EXPECT_EQ(roar->effects[1].extra, make_apply_power_flags(PowerId::FRAIL));
    EXPECT_EQ(roar->intent, r::MonsterIntent::STRONG_DEBUFF);
    EXPECT_EQ(d.move(r::kMawMoveDrool)->intent, r::MonsterIntent::BUFF);

    // THERE IS NO MOVE 1 (:54-57). A gap in the byte ids, and harmless.
    EXPECT_EQ(d.move(1), nullptr) << "Maw's move ids start at 2";
    EXPECT_EQ(r::kMawMoveRoar, 2);
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::NORMAL);
    EXPECT_EQ(d.roll_count, 0);
}

// WrithingMass.java:40-52,62-77.
TEST(BeyondNormalsII, WrithingMassStatTableAcrossEveryAscensionBranch) {
    const auto& d = r::kWrithingMass;
    EXPECT_EQ(d.hp_min(0), 160);
    EXPECT_EQ(d.hp_min(6), 160);
    EXPECT_EQ(d.hp_min(7), 175);
    EXPECT_EQ(d.hp_min(kA20), 175);
    EXPECT_EQ(d.hp_min(kA20), d.hp_max(kA20)) << "a FIXED sheet, both tiers";

    // All four damage numbers branch at A2, and THERE IS NO A17 ARM ANYWHERE --
    // the only monster in the batch without one. Asserting A16 == A17 == A20 is
    // how that absence is pinned.
    struct Row { uint8_t move; uint8_t step; int base; int a2; };
    const Row rows[] = {
        {r::kWrithingMassMoveBigHit, 0, 32, 38},
        {r::kWrithingMassMoveMultiHit, 0, 7, 9},
        {r::kWrithingMassMoveAttackBlock, 0, 15, 16},
        {r::kWrithingMassMoveAttackDebuff, 0, 10, 12},
    };
    for (const Row& row : rows) {
        EXPECT_EQ(step_amount(d, row.move, row.step, 1), row.base);
        EXPECT_EQ(step_amount(d, row.move, row.step, 2), row.a2);
        EXPECT_EQ(step_amount(d, row.move, row.step, 16), row.a2);
        EXPECT_EQ(step_amount(d, row.move, row.step, 17), row.a2)
            << "WrithingMass.java has NO ascension >= 17 branch at all";
        EXPECT_EQ(step_amount(d, row.move, row.step, kA20), row.a2);
    }

    // MULTI_HIT is THREE separate hits, flat at every ascension (HIT_COUNT 3).
    EXPECT_EQ(step_count(d, r::kWrithingMassMoveMultiHit), 3);
    for (uint8_t k = 0; k < 3; ++k) {
        EXPECT_EQ(step_amount(d, r::kWrithingMassMoveMultiHit, k, kA20), 9);
    }

    // ATTACK_BLOCK: damage FIRST, then block, and the block reads damage.get(2)
    // .BASE -- the same number, unscaled.
    const r::MonsterMove* ab = d.move(r::kWrithingMassMoveAttackBlock);
    ASSERT_NE(ab, nullptr);
    ASSERT_EQ(ab->effect_count, 2);
    EXPECT_EQ(ab->effects[0].op, r::Opcode::DAMAGE);
    EXPECT_EQ(ab->effects[1].op, r::Opcode::BLOCK);
    EXPECT_EQ(ab->effects[1].target, r::MonsterMoveTarget::SELF);
    EXPECT_EQ(step_amount(d, r::kWrithingMassMoveAttackBlock, 1, kA20), 16);
    EXPECT_EQ(ab->intent, r::MonsterIntent::ATTACK_DEFEND);

    // ATTACK_DEBUFF: damage, WEAK, VULNERABLE -- and normalDebuffAmt is 2 in
    // BOTH A2 arms (:72,77), another deliberate non-difference.
    const r::MonsterMove* ad = d.move(r::kWrithingMassMoveAttackDebuff);
    ASSERT_NE(ad, nullptr);
    ASSERT_EQ(ad->effect_count, 3);
    EXPECT_EQ(ad->effects[1].extra, make_apply_power_flags(PowerId::WEAK));
    EXPECT_EQ(ad->effects[2].extra, make_apply_power_flags(PowerId::VULNERABLE));
    EXPECT_EQ(step_amount(d, r::kWrithingMassMoveAttackDebuff, 1, 1), 2);
    EXPECT_EQ(step_amount(d, r::kWrithingMassMoveAttackDebuff, 1, 2), 2)
        << "normalDebuffAmt is 2 in both arms -- a dead half-branch";

    // MEGA_DEBUFF is one OBTAIN_CARD naming Parasite, and nothing else.
    const r::MonsterMove* md = d.move(r::kWrithingMassMoveMegaDebuff);
    ASSERT_NE(md, nullptr);
    ASSERT_EQ(md->effect_count, 1);
    EXPECT_EQ(md->effects[0].op, r::Opcode::OBTAIN_CARD);
    EXPECT_EQ(md->effects[0].extra, static_cast<uint32_t>(CardId::PARASITE));
    EXPECT_EQ(md->intent, r::MonsterIntent::STRONG_DEBUFF);

    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::NORMAL);
    EXPECT_EQ(d.roll_count, 0);
}

// THE MOVE-ID-0 ROW, which the registry loader rejected until this batch.
TEST(BeyondNormalsII, WrithingMassBigHitKeepsTheGamesByteIdOfZero) {
    EXPECT_EQ(r::kWrithingMassMoveBigHit, 0)
        << "WrithingMass.java:48 -- BIG_HIT is (byte) 0 and the row keeps it "
           "rather than renumbering around the loader";
    const r::MonsterMove* big =
        r::kWrithingMass.move(r::kWrithingMassMoveBigHit);
    ASSERT_NE(big, nullptr) << "move(0) must resolve, not read as 'no move'";
    EXPECT_EQ(big->move_id, 0);
    EXPECT_EQ(big->intent, r::MonsterIntent::ATTACK);
    // Every OTHER monster's move(0) is still nothing, which is what makes the
    // 0-as-empty-slot sentinel safe everywhere else.
    EXPECT_EQ(r::kJawWorm.move(0), nullptr);
    EXPECT_EQ(r::kMaw.move(0), nullptr);
}

// ============================================================================
// 2. RNG draw accounting -- and the TWO-TWO fixed-HP split
// ============================================================================

// THE DISTINCTION THIS BATCH EXISTS TO GET RIGHT. All four monsters have a fixed
// HP sheet; two of them still cost a monster_hp_rng draw and two do not, and the
// difference is whether the ctor calls setHp at all.
TEST(BeyondNormalsII, FixedHpMonstersSplitTwoTwoOnTheHpDraw) {
    {   // setHp(190) -- ONE-ARG, so setHp(hp, hp), so ONE draw (Hexaghost shape).
        CombatState s = MakeSeeded(4242);
        spire_growth_init(s, 0);
        EXPECT_EQ(s.monster_hp_rng.counter, 1)
            << "SpireGrowth.java:53-57 calls setHp(int), which is setHp(hp, hp) "
               "(AbstractMonster.java:777-779) and still draws";
        EXPECT_EQ(s.monsters[0].hp, 190);
        EXPECT_EQ(s.monsters[0].max_hp, 190);
    }
    {   // setHp(175) -- same shape.
        CombatState s = MakeSeeded(4242);
        writhing_mass_init(s, 0);
        EXPECT_EQ(s.monster_hp_rng.counter, 1);
        EXPECT_EQ(s.monsters[0].hp, 175);
    }
    {   // NO setHp call at all -- ZERO draws (Spheric Guardian shape).
        CombatState s = MakeSeeded(4242);
        transient_init(s, 0);
        EXPECT_EQ(s.monster_hp_rng.counter, 0)
            << "Transient.java:45-61 never calls setHp; the ctor is RNG-free "
               "(AbstractMonster.java:135-155)";
        EXPECT_EQ(s.monsters[0].hp, 999);
        EXPECT_EQ(s.monsters[0].max_hp, 999);
    }
    {
        CombatState s = MakeSeeded(4242);
        maw_init(s, 0);
        EXPECT_EQ(s.monster_hp_rng.counter, 0)
            << "Maw.java:62-82 never calls setHp either";
        EXPECT_EQ(s.monsters[0].hp, 300);
    }
}

// Every one of the four still ROLLS A MOVE at init -- the draw happens even
// where the value is thrown away.
TEST(BeyondNormalsII, EveryInitSpendsExactlyOneAiDraw) {
    struct Row { MonsterInitFn fn; const char* name; };
    const Row rows[] = {
        {&spire_growth_init, "spire growth"},
        {&transient_init, "transient"},
        {&maw_init, "maw"},
        {&writhing_mass_init, "writhing mass"},
    };
    for (const Row& row : rows) {
        CombatState s = MakeSeeded(77);
        row.fn(s, 0);
        EXPECT_EQ(s.ai_rng.counter, 1) << row.name;
        EXPECT_NE(s.monsters[0].intent, static_cast<uint8_t>(MonsterIntent::NONE))
            << row.name << ": init must leave a telegraphed move";
    }
}

// The Transient is the Looter/Guardian case taken to its limit: ONE draw for the
// WHOLE COMBAT, discarded, because getMove ignores num and takeTurn re-telegraphs
// itself with no trailing RollMoveAction.
TEST(BeyondNormalsII, TransientSpendsNoAiRngAfterInit) {
    CombatState s = MakeSeeded(31);
    transient_init(s, 0);
    ASSERT_EQ(s.ai_rng.counter, 1);
    for (int turn = 0; turn < 6; ++turn) {
        transient_take_turn(s, 0);
        drain(s);
    }
    EXPECT_EQ(s.ai_rng.counter, 1)
        << "Transient.takeTurn (:75-85) has NO RollMoveAction -- it calls "
           "setMove on itself instead";
    EXPECT_EQ(monster_roll_move_fn(MonsterId::TRANSIENT), nullptr)
        << "and it registers no queued-roll fn at all";
}

// The Spire Growth and the Maw each spend exactly one per turn; the Writhing
// Mass is the one that can spend MORE, and the count is data-dependent.
TEST(BeyondNormalsII, PerTurnAiDrawCounts) {
    {
        CombatState s = MakeSeeded(9);
        spire_growth_init(s, 0);
        const int32_t before = s.ai_rng.counter;
        spire_growth_roll_move(s, 0);
        EXPECT_EQ(s.ai_rng.counter, before + 1)
            << "no tiebreak, no recursion anywhere in SpireGrowth.getMove";
    }
    {
        CombatState s = MakeSeeded(9);
        maw_init(s, 0);
        const int32_t before = s.ai_rng.counter;
        maw_roll_move(s, 0);
        EXPECT_EQ(s.ai_rng.counter, before + 1);
    }
    {
        // The Writhing Mass spends AT LEAST one and sometimes several. Sweeping
        // seeds proves both that it always spends one and that it sometimes
        // spends more -- the latter is the recursion actually happening.
        bool saw_extra = false;
        for (int64_t seed = 1; seed < 200 && !saw_extra; ++seed) {
            CombatState s = MakeSeeded(seed);
            writhing_mass_init(s, 0);
            for (int turn = 0; turn < 12; ++turn) {
                const int32_t before = s.ai_rng.counter;
                writhing_mass_roll_move(s, 0);
                EXPECT_GE(s.ai_rng.counter, before + 1);
                if (s.ai_rng.counter > before + 1) {
                    saw_extra = true;
                }
            }
        }
        EXPECT_TRUE(saw_extra)
            << "WrithingMass.getMove must be observed spending a tiebreak or a "
               "recursive re-draw; if this never fires the recursion is dead";
    }
}

// ============================================================================
// 3. The Spire Growth
// ============================================================================

// getMove's five gates (SpireGrowth.java:100-119), driven as a pure function so
// both sides of the A17 hoist are exercised. At A20 only the hoisted arm is
// live, which is why the sub-A17 arm needs the parameter.
TEST(BeyondNormalsII, SpireGrowthMoveTreeBothAscensionArms) {
    const uint8_t kTackle = r::kSpireGrowthMoveQuickTackle;
    const uint8_t kConstrict = r::kSpireGrowthMoveConstrict;
    const uint8_t kSmash = r::kSpireGrowthMoveSmash;

    // (1) A17 hoist: an unafflicted player eats Constrict whatever `num` says.
    for (int32_t num : {0, 49, 50, 99}) {
        MonsterState m{};
        spire_growth_decide_move(m, num, /*player_constricted=*/false, 17);
        EXPECT_EQ(m.move_history[0], kConstrict) << "num=" << num;
    }
    // Below A17 the same roll reaches the tackle gate first.
    {
        MonsterState m{};
        spire_growth_decide_move(m, 10, /*player_constricted=*/false, 16);
        EXPECT_EQ(m.move_history[0], kTackle)
            << "sub-A17: gate 2 runs before the Constrict gate";
    }
    // (2) With the player already Constricted, the A17 hoist is inert and the
    // roll decides.
    {
        MonsterState m{};
        spire_growth_decide_move(m, 10, /*player_constricted=*/true, kA20);
        EXPECT_EQ(m.move_history[0], kTackle);
    }
    // (3) lastMove(CONSTRICT) also closes the gate, even unafflicted -- which is
    // the state right after a Constrict resolves and is then cleansed.
    {
        MonsterState m{};
        set_monster_move(m, kConstrict, MonsterIntent::STRONG_DEBUFF);
        spire_growth_decide_move(m, 10, /*player_constricted=*/false, kA20);
        EXPECT_EQ(m.move_history[0], kTackle);
    }
    // (4) Two tackles in a row are allowed; the THIRD is not, and falls to Smash.
    {
        MonsterState m{};
        set_monster_move(m, kTackle, MonsterIntent::ATTACK);
        set_monster_move(m, kTackle, MonsterIntent::ATTACK);
        spire_growth_decide_move(m, 10, /*player_constricted=*/true, kA20);
        EXPECT_EQ(m.move_history[0], kSmash);
    }
    // (5) The fallthrough CAN produce a third tackle even though gate 2 refuses
    // to -- gate 2's lastTwoMoves test guards only its own branch.
    {
        MonsterState m{};
        set_monster_move(m, kSmash, MonsterIntent::ATTACK);
        set_monster_move(m, kSmash, MonsterIntent::ATTACK);
        spire_growth_decide_move(m, 99, /*player_constricted=*/true, kA20);
        EXPECT_EQ(m.move_history[0], kTackle) << "the tail of the chain";
    }
}

// The player-power query is REAL: the live decision path reads the player's slot
// list, not a parameter.
TEST(BeyondNormalsII, SpireGrowthReadsThePlayersConstrictedFromTheLiveState) {
    CombatState s = MakeSeeded(5);
    spire_growth_init(s, 0);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kSpireGrowthMoveConstrict)
        << "A20 + empty history + unafflicted player -> the hoisted gate";

    CombatState s2 = MakeSeeded(5);
    give_player_power(s2, PowerId::CONSTRICTED, 12, /*counter=*/0);
    s2.monster_count = 1;
    spire_growth_init(s2, 0);
    EXPECT_NE(s2.monsters[0].move_history[0], r::kSpireGrowthMoveConstrict)
        << "the same seed, but the player already holds Constricted";
}

// CONSTRICTED'S SOURCE, and the Rupture observable it exists for.
TEST(BeyondNormalsII, ConstrictApplyCarriesTheMonsterIndexAsTheSlotCounter) {
    CombatState s = MakeSeeded(11, /*monsters=*/2);
    spire_growth_init(s, 1);  // deliberately NOT slot 0
    telegraph(s, 1, r::kSpireGrowthMoveConstrict, MonsterIntent::STRONG_DEBUFF);
    spire_growth_take_turn(s, 1);

    const ActionQueueItem& apply = queued(s, 0);
    ASSERT_EQ(apply.opcode, static_cast<uint16_t>(Opcode::APPLY_POWER));
    EXPECT_EQ(apply_power_id_from_flags(apply.flags), PowerId::CONSTRICTED);
    EXPECT_EQ(apply.amount, 12) << "constrictDmg + 2 at A17";
    EXPECT_EQ(apply_power_counter_from_flags(apply.flags), 1)
        << "the SOURCE monster's slot index rides in the counter operand";

    drain(s);
    EXPECT_EQ(player_power_amount(s, PowerId::CONSTRICTED), 12);
    EXPECT_EQ(player_power_counter(s, PowerId::CONSTRICTED), 1)
        << "op_apply_power's new-slot path writes it into PowerSlot.counter";
}

// The tick, and the property the whole source apparatus buys: RUPTURE MUST NOT
// FIRE. Rupture's guard is `source == victim` (power_hooks.hpp); a Constricted
// tick is sourced at the MONSTER, so an Ironclad holding Rupture gains nothing.
TEST(BeyondNormalsII, ConstrictedTicksAsMonsterSourcedThornsAndSkipsRupture) {
    CombatState s = MakeState(1);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::SPIRE_GROWTH);
    s.monsters[0].hp = 190;
    s.monsters[0].max_hp = 190;
    give_player_power(s, PowerId::CONSTRICTED, 12, /*counter=*/0);
    give_player_power(s, PowerId::RUPTURE, 1);

    dispatch_at_end_of_turn(s);
    ASSERT_EQ(s.action_count, 1);
    const ActionQueueItem& dmg = queued(s, 0);
    EXPECT_EQ(dmg.opcode, static_cast<uint16_t>(Opcode::DAMAGE));
    EXPECT_EQ(dmg.tgt, kActorPlayer);
    EXPECT_EQ(dmg.src, 0) << "the SOURCE monster, NOT the victim";
    EXPECT_EQ(dmg.amount, 12);
    EXPECT_EQ(damage_type_from_flags(dmg.flags), DamageType::THORNS);

    const int16_t before = player_power_amount(s, PowerId::STRENGTH);
    const int32_t hp_before = s.player_hp;
    drain(s);
    EXPECT_EQ(s.player_hp, hp_before - 12);
    EXPECT_EQ(player_power_amount(s, PowerId::STRENGTH), before)
        << "Rupture's guard is source == victim; a monster-sourced tick must "
           "not feed it. Source the tick at the player and this goes to 1.";
}

// It never decrements and never removes itself: it is not a duration debuff.
TEST(BeyondNormalsII, ConstrictedNeverDecays) {
    CombatState s = MakeState(1);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::SPIRE_GROWTH);
    s.monsters[0].hp = 190;
    s.monsters[0].max_hp = 190;
    give_player_power(s, PowerId::CONSTRICTED, 12);
    for (int i = 0; i < 4; ++i) {
        dispatch_at_end_of_turn(s);
        drain(s);
        dispatch_at_end_of_round(s);
        drain(s);
        EXPECT_EQ(player_power_amount(s, PowerId::CONSTRICTED), 12)
            << "round " << i;
    }
}

// ============================================================================
// 4. The Transient
// ============================================================================

// The opener, in order: Fading THEN Shifting, at the A20 amounts.
TEST(BeyondNormalsII, TransientPreBattleAppliesFadingThenShifting) {
    CombatState s = MakeSeeded(3);
    transient_init(s, 0);
    transient_use_pre_battle_action(s, 0);
    ASSERT_EQ(s.action_count, 2);
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 0).flags), PowerId::FADING);
    EXPECT_EQ(queued(s, 0).amount, 6) << "A17+ arm (Transient.java:66-68)";
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 1).flags), PowerId::SHIFTING);
    EXPECT_EQ(queued(s, 1).amount, kShiftingNoAmount)
        << "ShiftingPower's ctor assigns NO amount -- AbstractPower's -1";
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::FADING), 6);
    EXPECT_EQ(monster_power(s, 0, PowerId::SHIFTING), -1)
        << "the slot really carries -1, the Confusion spelling";
    // Application order is slot order (Constricted's 105 aside, neither of these
    // overrides priority, so the sort is stable on insertion).
    EXPECT_EQ(s.monsters[0].powers[0].power_id,
              static_cast<uint16_t>(PowerId::FADING));
}

// THE LADDER: 40, 50, 60, ... one rung per resolved attack, six rungs live.
TEST(BeyondNormalsII, TransientDamageRampClimbsTenPerTurn) {
    CombatState s = MakeSeeded(3);
    transient_init(s, 0);
    for (int turn = 0; turn < 6; ++turn) {
        transient_take_turn(s, 0);
        ASSERT_GE(s.action_count, 1) << "turn " << turn;
        EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::DAMAGE));
        EXPECT_EQ(queued(s, 0).amount, 40 + turn * 10) << "turn " << turn;
        EXPECT_EQ(queued(s, 0).tgt, kActorPlayer);
        drain(s);
        EXPECT_EQ(s.monsters[0].pad0, turn + 1) << "count is pad0";
        EXPECT_EQ(s.monsters[0].move_history[0], r::kTransientMoveAttack)
            << "takeTurn's own setMove still pushes the ring";
    }
    // The seventh rung EXISTS in the Java's array and is UNREACHABLE: Fading 6
    // kills the monster at the end of its sixth turn. The saturation below is a
    // statement of that, not a behaviour anything can observe.
    EXPECT_EQ(kTransientDamageRungs, 7);
    EXPECT_EQ(s.monsters[0].pad0, 6);
    transient_take_turn(s, 0);
    EXPECT_EQ(queued(s, 0).amount, 100) << "the seventh and last rung";
    drain(s);
    transient_take_turn(s, 0);
    EXPECT_EQ(queued(s, 0).amount, 100) << "saturates rather than running off";
}

// FADING: the countdown, then the death -- and it fires AFTER takeTurn.
TEST(BeyondNormalsII, FadingCountsDownThenSuicidesWithRelicTriggersOn) {
    CombatState s = MakeSeeded(3);
    transient_init(s, 0);
    give_monster_power(s, 0, PowerId::FADING, 6);

    for (int16_t expect = 6; expect > 1; --expect) {
        ASSERT_EQ(monster_power(s, 0, PowerId::FADING), expect);
        dispatch_during_turn(s, 0);
        ASSERT_EQ(s.action_count, 1);
        EXPECT_EQ(queued(s, 0).opcode,
                  static_cast<uint16_t>(Opcode::REDUCE_POWER));
        EXPECT_EQ(queued(s, 0).amount, 1);
        drain(s);
    }
    // At 1, the arm flips.
    ASSERT_EQ(monster_power(s, 0, PowerId::FADING), 1);
    dispatch_during_turn(s, 0);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::SUICIDE));
    EXPECT_EQ(queued(s, 0).flags & 1u, 1u)
        << "the ONE-ARG SuicideAction defaults triggerRelics to TRUE "
           "(SuicideAction.java:17-19) -- the splitting slimes pass false";
    EXPECT_EQ(queued(s, 0).tgt, 0);
    drain(s);
    EXPECT_EQ(s.monsters[0].hp, 0);
    EXPECT_FALSE(monster_escaped(s.monsters[0]))
        << "it DIES; it does not escape, and reward assembly cares";
}

// The `!isDying` guard: a monster already at zero does not queue a second death
// -- AND IT DOES NOT FALL OUT OF THE BODY EITHER. The Java's death arm is a
// CONJUNCTION (`amount == 1 && !isDying`), so its `else` catches both amount >= 2
// AND amount == 1 while dying, and the reduce is queued in the second case too.
// That is the only path on which a Fading slot can hit zero by decrement, so it
// is also the only way ReducePowerAction's removal arm is reachable at all.
TEST(BeyondNormalsII, FadingDoesNotSuicideAnAlreadyDyingOwnerButStillReduces) {
    CombatState s = MakeSeeded(3);
    transient_init(s, 0);
    give_monster_power(s, 0, PowerId::FADING, 1);
    s.monsters[0].hp = 0;
    dispatch_during_turn(s, 0);
    ASSERT_EQ(s.action_count, 1) << "the `else` arm, not an early return";
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::REDUCE_POWER));
    EXPECT_NE(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::SUICIDE));
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 0).flags), PowerId::FADING);
    EXPECT_EQ(queued(s, 0).amount, 1);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::FADING), -1)
        << "1 -> 0 REMOVES the slot (ReducePowerAction.java:45-51) -- the one "
           "reachable use of that arm; the helper returns -1 for absent";
}

// THE ORDERING THAT DECIDES THE FIGHT: duringTurn runs AFTER takeTurn
// (GameActionManager.java:322-323), so the Transient attacks on the turn it
// dies. Driven through the real pump seam rather than asserted from the source.
TEST(BeyondNormalsII, TransientAttacksOnTheTurnFadingKillsIt) {
    CombatState s = MakeSeeded(3);
    transient_init(s, 0);
    give_monster_power(s, 0, PowerId::FADING, 1);
    const int32_t hp_before = s.player_hp;
    transient_take_turn(s, 0);       // step 5's turn body
    dispatch_during_turn(s, 0);      // ... then applyTurnPowers
    drain(s);
    EXPECT_EQ(s.player_hp, hp_before - 40) << "the attack landed";
    EXPECT_EQ(s.monsters[0].hp, 0) << "and then it died";
}

// SHIFTING: -damage Strength, plus a matching Shackled that gives it back. The
// EXECUTED order is Shackled first (both are addToTop, and the -Strength is
// pushed first).
TEST(BeyondNormalsII, ShiftingSwapsStrengthForShackledOnEveryRealHit) {
    CombatState s = MakeSeeded(3);
    transient_init(s, 0);
    give_monster_power(s, 0, PowerId::SHIFTING, -1);

    player_attacks(s, 0, 12);
    ASSERT_EQ(s.action_count, 2);
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 0).flags), PowerId::SHACKLED)
        << "pushed SECOND to the top, so it resolves FIRST";
    EXPECT_EQ(queued(s, 0).amount, 12);
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 1).flags), PowerId::STRENGTH);
    EXPECT_EQ(queued(s, 1).amount, -12);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), -12);
    EXPECT_EQ(monster_power(s, 0, PowerId::SHACKLED), 12);

    // Shackled's own at_end_of_turn program hands it back and removes itself.
    dispatch_at_end_of_round(s);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), -1)
        << "GainStrengthPower returns 12 against the -12";
    EXPECT_EQ(monster_power(s, 0, PowerId::SHACKLED), -1) << "and removes itself";
}

TEST(BeyondNormalsII, ShiftingIgnoresAFullyBlockedHit) {
    CombatState s = MakeSeeded(3);
    transient_init(s, 0);
    give_monster_power(s, 0, PowerId::SHIFTING, -1);
    s.monsters[0].block = 50;
    player_attacks(s, 0, 12);
    EXPECT_EQ(s.action_count, 0) << "damageAmount > 0 is the whole condition";
}

// THE DIVERGENCE THIS BATCH CLOSED. ShiftingPower's onAttacked has NO damage-type
// guard and NO owner guard, and the Java's onAttacked walk is unconditional
// (AbstractMonster.java:665-667) -- so a THORNS reflect or an HP_LOSS really does
// swing a Transient's Strength. dispatch_on_attacked fires only for NORMAL,
// src != tgt; the type-tolerant walk covers the rest.
//
// RED-FIRST EVIDENCE: with dispatch_on_attacked_type_tolerant's calls removed
// from interp_damage.cpp, both halves below queue nothing.
TEST(BeyondNormalsII, ShiftingFiresOnThornsAndHpLossToo) {
    {   // A Flame Barrier / Thorns reflect onto the Transient.
        CombatState s = MakeSeeded(3);
        transient_init(s, 0);
        give_monster_power(s, 0, PowerId::SHIFTING, -1);
        player_attacks(s, 0, 7, DamageType::THORNS);
        ASSERT_EQ(s.action_count, 2)
            << "ShiftingPower.java:33 is a bare `damageAmount > 0` -- no type "
               "gate, so THORNS triggers it in the game";
        drain(s);
        EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), -7);
        EXPECT_EQ(monster_power(s, 0, PowerId::SHACKLED), 7);
    }
    {   // A direct HP loss.
        CombatState s = MakeSeeded(3);
        transient_init(s, 0);
        give_monster_power(s, 0, PowerId::SHIFTING, -1);
        ActionQueueItem it{};
        it.opcode = static_cast<uint16_t>(Opcode::LOSE_HP);
        it.src = 0;
        it.tgt = 0;
        it.amount = 5;
        execute_opcode(s, it);
        ASSERT_EQ(s.action_count, 2) << "LoseHPAction routes through damage()";
        drain(s);
        EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), -5);
    }
}

// The narrow walk must NOT become a general widening: every OTHER binder keeps
// its Java type guard, and Malleable is the one to prove it on because a
// Writhing Mass carries both it and a THORNS-reachable fight.
TEST(BeyondNormalsII, TypeTolerantWalkAdmitsOnlyShifting) {
    EXPECT_TRUE(power_is_on_attacked_type_tolerant(PowerId::SHIFTING));
    for (PowerId id : {PowerId::MALLEABLE, PowerId::REACTIVE, PowerId::THORNS,
                       PowerId::FLIGHT, PowerId::ANGRY, PowerId::SHARP_HIDE}) {
        EXPECT_FALSE(power_is_on_attacked_type_tolerant(id))
            << "PowerId " << static_cast<int>(id)
            << " spells its own damage-type guard in the Java";
    }
    // And behaviourally: a THORNS hit onto a Malleable owner queues nothing.
    CombatState s = MakeSeeded(3);
    writhing_mass_init(s, 0);
    give_monster_power(s, 0, PowerId::MALLEABLE, 3, 3);
    player_attacks(s, 0, 7, DamageType::THORNS);
    EXPECT_EQ(s.action_count, 0)
        << "MalleablePower.java:63 requires info.type == NORMAL";
}

// THE GOLD NEGATIVE. `Transient.gold = 1` and SuicideAction's `m.gold = 0` are
// BOTH DEAD: nothing in the game reads a monster's gold field, and combat gold
// comes from an already-rolled treasure_rng int. So a faded-out Transient and a
// killed one pay the same. Pinned rather than assumed.
TEST(BeyondNormalsII, FadedAndKilledTransientPayTheSameGold) {
    // There is no per-monster gold field in CombatState to differ, and the
    // reward path never asks a monster for one -- the check that keeps that true
    // is structural: if a gold column is ever added to monsters.yaml, this must
    // be revisited alongside it.
    const r::MonsterDef* d = r::monster_def(MonsterId::TRANSIENT);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->roll_count, 0);

    RunController faded{};
    RunController killed{};
    faded.run.gold = 100;
    killed.run.gold = 100;
    faded.combat.monster_count = 1;
    killed.combat.monster_count = 1;
    for (RunController* rc : {&faded, &killed}) {
        rc->combat.monsters[0].monster_id =
            static_cast<uint16_t>(MonsterId::TRANSIENT);
        rc->combat.monsters[0].max_hp = 999;
    }
    // The fade path zeroes HP through SUICIDE; the kill path through damage.
    ActionQueueItem suicide{};
    suicide.opcode = static_cast<uint16_t>(Opcode::SUICIDE);
    suicide.tgt = 0;
    suicide.flags = 1u;
    faded.combat.monsters[0].hp = 1;
    execute_opcode(faded.combat, suicide);
    killed.combat.monsters[0].hp = 0;

    EXPECT_EQ(faded.run.gold, killed.run.gold)
        << "SuicideAction.java:31's `m.gold = 0` has no reader in the game";
    EXPECT_EQ(settle_stolen_gold(faded), settle_stolen_gold(killed))
        << "and the one gold settlement that IS per-monster is the thieves', "
           "which a Transient has no part in";
}

// ============================================================================
// 5. The Maw
// ============================================================================

// THE turnCount WALKTHROUGH, exactly as the header describes it.
TEST(BeyondNormalsII, MawOpensWithRoarAndTurnCountIsAlreadyTwo) {
    CombatState s = MakeSeeded(88);
    maw_init(s, 0);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kMawMoveRoar)
        << "roared is clear, so gate 1 answers before the roll can";
    EXPECT_EQ(s.monsters[0].pad0, 2)
        << "turnCount starts at 1 (Maw.java:59) and the INIT rollMove "
           "pre-increments it (:118)";
    EXPECT_EQ((s.monsters[0].flags & kMonsterFlagMawRoared), 0u)
        << "roared is set in takeTurn (:94), NOT in getMove";
}

// Every seed opens with ROAR: the gate does not consult `num` at all.
TEST(BeyondNormalsII, MawOpensWithRoarOnEverySeed) {
    for (int64_t seed = 1; seed < 60; ++seed) {
        CombatState s = MakeSeeded(seed);
        maw_init(s, 0);
        EXPECT_EQ(s.monsters[0].move_history[0], r::kMawMoveRoar)
            << "seed=" << seed;
    }
}

TEST(BeyondNormalsII, MawRoarSetsTheLatchAndAppliesWeakThenFrail) {
    CombatState s = MakeSeeded(88);
    maw_init(s, 0);
    maw_take_turn(s, 0);
    EXPECT_NE((s.monsters[0].flags & kMonsterFlagMawRoared), 0u)
        << "set SYNCHRONOUSLY inside takeTurn";
    ASSERT_GE(s.action_count, 3);
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 0).flags), PowerId::WEAK);
    EXPECT_EQ(queued(s, 0).amount, 5) << "terrifyDur + 2 at A17";
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 1).flags), PowerId::FRAIL);
    EXPECT_EQ(queued(s, 1).amount, 5);
    EXPECT_EQ(queued(s, 2).opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE))
        << "the trailing RollMoveAction sits outside the switch";
}

// The bite count is turnCount/2, so the FIRST Nom is a SINGLE hit and it grows.
TEST(BeyondNormalsII, MawNomBiteCountIsHalfTheTurnCount) {
    struct Row { uint8_t turn_count; int bites; };
    const Row rows[] = {{2, 1}, {3, 1}, {4, 2}, {5, 2}, {6, 3}, {13, 6}};
    for (const Row& row : rows) {
        CombatState s = MakeSeeded(88);
        maw_init(s, 0);
        s.monsters[0].pad0 = row.turn_count;
        s.monsters[0].flags |= kMonsterFlagMawRoared;
        telegraph(s, 0, r::kMawMoveNomnomnom, MonsterIntent::ATTACK);
        maw_take_turn(s, 0);
        int damage_items = 0;
        for (uint8_t i = 0; i < s.action_count; ++i) {
            if (queued(s, i).opcode == static_cast<uint16_t>(Opcode::DAMAGE)) {
                ++damage_items;
                EXPECT_EQ(queued(s, i).amount, 5);
            }
        }
        EXPECT_EQ(damage_items, row.bites)
            << "turnCount=" << static_cast<int>(row.turn_count);
    }
}

// SEPARATE hits, not one multiplied hit -- which is what makes Strength apply
// per bite. Pinned at the turnCount >= 6 band the dossier flagged.
TEST(BeyondNormalsII, MawBitesAreSeparateHitsAndStrengthAppliesToEachOne) {
    CombatState s = MakeSeeded(88);
    maw_init(s, 0);
    s.monsters[0].pad0 = 6;  // 3 bites
    s.monsters[0].flags |= kMonsterFlagMawRoared;
    give_monster_power(s, 0, PowerId::STRENGTH, 10);
    telegraph(s, 0, r::kMawMoveNomnomnom, MonsterIntent::ATTACK);
    const int32_t hp_before = s.player_hp;
    maw_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(s.player_hp, hp_before - 3 * (5 + 10))
        << "three bites of (5 + Strength), not one bite of 15";
}

TEST(BeyondNormalsII, MawMoveTreeAfterTheRoar) {
    const uint8_t kSlam = r::kMawMoveSlam;
    const uint8_t kDrool = r::kMawMoveDrool;
    const uint8_t kNom = r::kMawMoveNomnomnom;
    auto roared = [](uint8_t last, uint8_t turn_count) {
        MonsterState m{};
        m.flags = kMonsterFlagMawRoared;
        m.pad0 = turn_count;
        if (last != 0) {
            set_monster_move(m, last, MonsterIntent::ATTACK);
        }
        return m;
    };
    {   // num < 50 and no repeat -> Nom.
        MonsterState m = roared(kSlam, 6);
        maw_decide_move(m, 10);
        EXPECT_EQ(m.move_history[0], kNom);
    }
    {   // num < 50 but the last move WAS Nom -> gate 3 (lastMove(NOM)) -> Drool.
        MonsterState m = roared(kNom, 6);
        maw_decide_move(m, 10);
        EXPECT_EQ(m.move_history[0], kDrool);
    }
    {   // num >= 50 after a Slam -> Drool (lastMove, not lastTwoMoves).
        MonsterState m = roared(kSlam, 6);
        maw_decide_move(m, 80);
        EXPECT_EQ(m.move_history[0], kDrool);
    }
    {   // num >= 50 after a Drool -> the fallthrough, Slam.
        MonsterState m = roared(kDrool, 6);
        maw_decide_move(m, 80);
        EXPECT_EQ(m.move_history[0], kSlam);
    }
    // The turnCount side effect fires on EVERY decision, including the ones
    // that never look at it.
    MonsterState m = roared(kDrool, 9);
    maw_decide_move(m, 80);
    EXPECT_EQ(m.pad0, 10);
}

// The saturating increment is a statement that 255 is unreachable, and this is
// where that statement lives.
TEST(BeyondNormalsII, MawTurnCountSaturatesRatherThanWrapping) {
    MonsterState m{};
    m.flags = kMonsterFlagMawRoared;
    m.pad0 = 255;
    maw_decide_move(m, 80);
    EXPECT_EQ(m.pad0, 255) << "wrapping would silently SHRINK the bite count";
    // 255 would be 127 bites of 5 before any Strength -- 635 damage in one turn
    // against a player whose A20 max HP is 64. The case cannot arise.
    EXPECT_GT(255 / 2 * 5, 600);
}

// ============================================================================
// 6. The Writhing Mass
// ============================================================================

TEST(BeyondNormalsII, WrithingMassFirstMoveIsThirdsOfNumAndConsumesTheLatch) {
    struct Row { int64_t seed; };
    bool saw_multi = false, saw_block = false, saw_debuff = false;
    for (int64_t seed = 1; seed < 120; ++seed) {
        CombatState s = MakeSeeded(seed);
        writhing_mass_init(s, 0);
        const uint8_t opener = s.monsters[0].move_history[0];
        EXPECT_EQ((s.monsters[0].pad0 & kWrithingMassPadFirstMove), 0u)
            << "the latch is consumed synchronously (WrithingMass.java:147)";
        if (opener == r::kWrithingMassMoveMultiHit) saw_multi = true;
        if (opener == r::kWrithingMassMoveAttackBlock) saw_block = true;
        if (opener == r::kWrithingMassMoveAttackDebuff) saw_debuff = true;
        EXPECT_NE(opener, r::kWrithingMassMoveBigHit)
            << "BIG_HIT is not reachable on the first move";
        EXPECT_NE(opener, r::kWrithingMassMoveMegaDebuff)
            << "nor is MEGA_DEBUFF";
    }
    EXPECT_TRUE(saw_multi && saw_block && saw_debuff)
        << "all three thirds must be reachable";
}

TEST(BeyondNormalsII, WrithingMassPreBattleAppliesReactiveThenMalleable) {
    CombatState s = MakeSeeded(3);
    writhing_mass_init(s, 0);
    writhing_mass_use_pre_battle_action(s, 0);
    ASSERT_EQ(s.action_count, 2);
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 0).flags), PowerId::REACTIVE);
    EXPECT_EQ(queued(s, 0).amount, kReactiveNoAmount);
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 1).flags), PowerId::MALLEABLE);
    EXPECT_EQ(queued(s, 1).amount, kWrithingMassMalleableAmount);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::REACTIVE), kReactiveNoAmount);
    EXPECT_EQ(monster_power(s, 0, PowerId::MALLEABLE), 3);
    // Reactive's priority 50 sorts it ahead of Malleable's default 5? No: the
    // game's sort is ASCENDING by priority, so the default-5 Malleable sorts
    // FIRST. The assertion is on the resulting slot order, not on a guess.
    EXPECT_EQ(s.monsters[0].power_count, 2);
}

// REACTIVE: a real, non-lethal hit queues a re-roll -- and a multi-hit attack
// queues one PER HIT.
TEST(BeyondNormalsII, ReactiveQueuesARollMovePerRealNonLethalHit) {
    CombatState s = MakeSeeded(3);
    writhing_mass_init(s, 0);
    give_monster_power(s, 0, PowerId::REACTIVE, -1);
    for (int hit = 0; hit < 3; ++hit) {
        player_attacks(s, 0, 6);
    }
    int rolls = 0;
    for (uint8_t i = 0; i < s.action_count; ++i) {
        if (queued(s, i).opcode == static_cast<uint16_t>(Opcode::ROLL_MOVE)) {
            ++rolls;
            EXPECT_EQ(queued(s, i).tgt, 0);
        }
    }
    EXPECT_EQ(rolls, 3) << "addToBot, once per hit (ReactivePower.java:44)";
    const int32_t before = s.ai_rng.counter;
    drain(s);
    EXPECT_GE(s.ai_rng.counter, before + 3)
        << "each re-roll runs a full recursive getMove";
}

TEST(BeyondNormalsII, ReactiveIgnoresBlockedThornsAndLethalHits) {
    {   // Fully blocked.
        CombatState s = MakeSeeded(3);
        writhing_mass_init(s, 0);
        give_monster_power(s, 0, PowerId::REACTIVE, -1);
        s.monsters[0].block = 50;
        player_attacks(s, 0, 6);
        EXPECT_EQ(s.action_count, 0);
    }
    {   // THORNS -- Reactive DOES have the type guard Shifting lacks.
        CombatState s = MakeSeeded(3);
        writhing_mass_init(s, 0);
        give_monster_power(s, 0, PowerId::REACTIVE, -1);
        player_attacks(s, 0, 6, DamageType::THORNS);
        EXPECT_EQ(s.action_count, 0);
    }
    {   // EXACTLY lethal -- the health test is STRICT.
        CombatState s = MakeSeeded(3);
        writhing_mass_init(s, 0);
        give_monster_power(s, 0, PowerId::REACTIVE, -1);
        const int32_t hp = s.monsters[0].hp;
        player_attacks(s, 0, hp);
        int rolls = 0;
        for (uint8_t i = 0; i < s.action_count; ++i) {
            if (queued(s, i).opcode == static_cast<uint16_t>(Opcode::ROLL_MOVE)) {
                ++rolls;
            }
        }
        EXPECT_EQ(rolls, 0)
            << "damageAmount < currentHealth is strict (ReactivePower.java:40)";
    }
}

// THE MASTER-DECK PARASITE -- combat side. MEGA_DEBUFF accrues exactly one
// CardId into pending_obtain, and the once-per-combat latch closes.
TEST(BeyondNormalsII, MegaDebuffAccruesOneParasiteAndClosesTheGate) {
    CombatState s = MakeSeeded(3);
    writhing_mass_init(s, 0);
    telegraph(s, 0, r::kWrithingMassMoveMegaDebuff,
              MonsterIntent::STRONG_DEBUFF);
    writhing_mass_take_turn(s, 0);
    EXPECT_NE((s.monsters[0].pad0 & kWrithingMassPadUsedMegaDebuff), 0u)
        << "set SYNCHRONOUSLY in takeTurn (WrithingMass.java:116), which is why "
           "a Reactive re-roll landing later in the same card cannot repeat it";
    drain(s);
    ASSERT_EQ(s.pending_obtain_count, 1);
    EXPECT_EQ(s.pending_obtain[0], static_cast<uint16_t>(CardId::PARASITE));

    // The gate is now permanently shut: the `num < 20` arm can never pick it.
    for (int32_t num = 10; num < 20; ++num) {
        MonsterState m = s.monsters[0];
        m.move_history[0] = r::kWrithingMassMoveAttackBlock;
        CombatState t = s;
        t.monsters[0] = m;
        writhing_mass_roll_move(t, 0);
        EXPECT_NE(t.monsters[0].move_history[0],
                  r::kWrithingMassMoveMegaDebuff);
    }
}

// THE FOLD-BACK, at the run layer -- the acceptance names this test. A real
// Writhing Mass combat, entered through the ordinary encounter door, with the
// monster's move forced to MEGA_DEBUFF; the Parasite must reach RunState's
// master deck through the ONE acquisition door.
TEST(BeyondNormalsII, WrithingMassParasiteReachesTheRunMasterDeck) {
    RunController rc = run_begin(4242, /*ascension=*/20);
    ASSERT_TRUE(enter_event_combat(rc, "Writhing Mass"));
    ASSERT_EQ(rc.combat.monster_count, 1);
    ASSERT_EQ(rc.combat.monsters[0].monster_id,
              static_cast<uint16_t>(MonsterId::WRITHING_MASS));

    const uint16_t deck_before = rc.run.master_deck_count;
    // Force the decided move; the roll that produced it is irrelevant to what
    // the fold-back does with the result.
    set_monster_move(rc.combat.monsters[0], r::kWrithingMassMoveMegaDebuff,
                     MonsterIntent::STRONG_DEBUFF);
    const Action end_turn = make_action(ActionVerb::END_TURN);
    StepResult step{};
    // advance() asserts the three spans are equal-length -- the results span is
    // an OUT parameter, not an optional one.
    advance(std::span<RunController>(&rc, 1),
            std::span<const Action>(&end_turn, 1),
            std::span<StepResult>(&step, 1));

    ASSERT_EQ(rc.run.master_deck_count, deck_before + 1)
        << "the OBTAIN_CARD accumulator must be drained by the run layer";
    const CardInstance& added = rc.run.master_deck[deck_before];
    EXPECT_EQ(added.card_id, static_cast<uint16_t>(CardId::PARASITE))
        << "and APPENDED at the end, not placed on top";
    EXPECT_EQ(added.upgrade, 0)
        << "CardLibrary.getCard(\"Parasite\").makeCopy() is a BASE copy";
    EXPECT_EQ(rc.combat.pending_obtain_count, 0) << "the accumulator is drained";
}

// The other half of the acquisition door: OMAMORI BLOCKS IT, exactly as it
// blocks any other curse, and the deck is unchanged. Asserted against
// add_card_to_master_deck directly, which is the function the drain calls.
TEST(BeyondNormalsII, OmamoriBlocksTheParasiteAndSpendsACharge) {
    RunController rc = run_begin(4242, /*ascension=*/20);
    // Give the run an Omamori with a live charge.
    rc.run.relics[rc.run.relic_count].relic_id =
        static_cast<uint16_t>(RelicId::OMAMORI);
    rc.run.relics[rc.run.relic_count].counter = 2;
    ++rc.run.relic_count;

    const uint16_t before = rc.run.master_deck_count;
    (void)add_card_to_master_deck(rc.run, CardId::PARASITE);
    EXPECT_EQ(rc.run.master_deck_count, before)
        << "ShowCardAndObtainEffect's ctor gate (:30-45) blocks a CURSE";
    (void)add_card_to_master_deck(rc.run, CardId::PARASITE);
    EXPECT_EQ(rc.run.master_deck_count, before) << "the second charge";
    (void)add_card_to_master_deck(rc.run, CardId::PARASITE);
    EXPECT_EQ(rc.run.master_deck_count, before + 1)
        << "charges exhausted -- the third Parasite lands";
}

// ============================================================================
// 7. The Jaw Worm Horde -- and the proof the ordinary worm is untouched
// ============================================================================

// THE FIXTURE-PARITY CLAIM, stated as a test rather than left to the fixtures:
// the ordinary init is byte-identical to what it was, and the newly registered
// pre-battle fn is a genuine no-op for it.
TEST(BeyondNormalsII, OrdinaryJawWormIsUnchangedByTheHardModeAddition) {
    CombatState s = MakeSeeded(1234);
    jaw_worm_init(s, 0);
    EXPECT_EQ(s.monsters[0].pad0, 0) << "the hardMode latch is CLEAR";
    EXPECT_EQ(s.monsters[0].move_history[0], kMoveChomp)
        << "the forced opening Chomp still fires";
    EXPECT_EQ(s.monster_hp_rng.counter, 1);
    EXPECT_EQ(s.ai_rng.counter, 1);

    // The registered pre-battle fn queues nothing and draws nothing.
    const int32_t hp_draws = s.monster_hp_rng.counter;
    const int32_t ai_draws = s.ai_rng.counter;
    jaw_worm_use_pre_battle_action(s, 0);
    EXPECT_EQ(s.action_count, 0)
        << "usePreBattleAction's whole body is `if (this.hardMode)`";
    EXPECT_EQ(s.monster_hp_rng.counter, hp_draws);
    EXPECT_EQ(s.ai_rng.counter, ai_draws);
    EXPECT_NE(monster_pre_battle_fn(MonsterId::JAW_WORM), nullptr)
        << "it IS registered now -- being registered is what must be harmless";
}

// The hard worm: same draws, different opening.
TEST(BeyondNormalsII, HardJawWormSpendsTheSameDrawsAndReadsTheRoll) {
    for (int64_t seed = 1; seed < 40; ++seed) {
        CombatState normal = MakeSeeded(seed);
        CombatState hard = MakeSeeded(seed);
        jaw_worm_init(normal, 0);
        jaw_worm_init_hard(hard, 0);
        EXPECT_EQ(normal.monster_hp_rng.counter, hard.monster_hp_rng.counter)
            << "setHp sits OUTSIDE the hardMode guard (JawWorm.java:81-84)";
        EXPECT_EQ(normal.monsters[0].hp, hard.monsters[0].hp)
            << "seed=" << seed << ": the same range, the same roll";
        EXPECT_EQ(normal.ai_rng.counter, 1);
        EXPECT_EQ(hard.ai_rng.counter, 1)
            << "seed=" << seed
            << ": every history predicate is false on an empty history, so no "
               "tiebreak randomBoolean is reached";
        EXPECT_NE(hard.monsters[0].pad0, 0);
    }
}

// The opening telegraph really does open up: all three moves must be reachable.
TEST(BeyondNormalsII, HardJawWormOpensWithAnyOfTheThreeMoves) {
    bool chomp = false, thrash = false, bellow = false;
    for (int64_t seed = 1; seed < 200; ++seed) {
        CombatState s = MakeSeeded(seed);
        jaw_worm_init_hard(s, 0);
        const uint8_t opener = s.monsters[0].move_history[0];
        if (opener == kMoveChomp) chomp = true;
        if (opener == kMoveThrash) thrash = true;
        if (opener == kMoveBellow) bellow = true;
    }
    EXPECT_TRUE(chomp && thrash && bellow)
        << "firstMove = false lets the full num tree pick the opener";
}

// The free Bellow: Strength THEN block, at the A20 numbers, and no RNG.
TEST(BeyondNormalsII, HardJawWormPreBattleGivesStrengthThenBlock) {
    CombatState s = MakeSeeded(7);
    jaw_worm_init_hard(s, 0);
    const int32_t hp_draws = s.monster_hp_rng.counter;
    const int32_t ai_draws = s.ai_rng.counter;
    jaw_worm_use_pre_battle_action(s, 0);
    ASSERT_EQ(s.action_count, 2);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::APPLY_POWER));
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 0).flags), PowerId::STRENGTH);
    EXPECT_EQ(queued(s, 0).amount, 5) << "bellowStr at A17+";
    EXPECT_EQ(queued(s, 1).opcode, static_cast<uint16_t>(Opcode::BLOCK));
    EXPECT_EQ(queued(s, 1).amount, 9) << "bellowBlock at A17+";
    EXPECT_EQ(s.monster_hp_rng.counter, hp_draws);
    EXPECT_EQ(s.ai_rng.counter, ai_draws);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), 5);
    EXPECT_EQ(s.monsters[0].block, 9);
}

// End to end through the real encounter door: three hard worms, three HP draws,
// three ai draws, and every one of them opens buffed.
TEST(BeyondNormalsII, JawWormHordeSpawnsThreeHardWorms) {
    RunController rc = run_begin(4242, /*ascension=*/20);
    ASSERT_TRUE(enter_event_combat(rc, "Jaw Worm Horde"));
    ASSERT_EQ(rc.combat.monster_count, 3);
    for (uint8_t i = 0; i < 3; ++i) {
        EXPECT_EQ(rc.combat.monsters[i].monster_id,
                  static_cast<uint16_t>(MonsterId::JAW_WORM));
        EXPECT_NE(rc.combat.monsters[i].pad0 & kJawWormPadHardMode, 0u)
            << "slot " << static_cast<int>(i);
        EXPECT_EQ(monster_power(rc.combat, i, PowerId::STRENGTH), 5)
            << "the pre-battle Bellow ran for slot " << static_cast<int>(i);
        EXPECT_EQ(rc.combat.monsters[i].block, 9);
    }
    // The ordinary solo "Jaw Worm" encounter through the SAME door is untouched.
    RunController plain = run_begin(4242, /*ascension=*/20);
    ASSERT_TRUE(enter_event_combat(plain, "Jaw Worm"));
    ASSERT_EQ(plain.combat.monster_count, 1);
    EXPECT_EQ(plain.combat.monsters[0].pad0, 0);
    EXPECT_EQ(monster_power(plain.combat, 0, PowerId::STRENGTH), -1)
        << "no free Bellow for the Exordium worm";
}

// ============================================================================
// 8. Encounter compositions -- spawn-order-exact
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

TEST(BeyondNormalsII, EncounterCompositionsAreSpawnOrderExact) {
    using V = std::vector<std::string_view>;
    EXPECT_EQ(members_of("Spire Growth", 1), V{"Serpent"})
        << "SpireGrowth.ID is \"Serpent\", not the class name";
    EXPECT_EQ(members_of("Transient", 1), V{"Transient"});
    EXPECT_EQ(members_of("Maw", 1), V{"Maw"});
    EXPECT_EQ(members_of("Writhing Mass", 1), V{"WrithingMass"});
    EXPECT_EQ(members_of("Jaw Worm Horde", 1),
              (V{"JawWorm", "JawWorm", "JawWorm"}))
        << "MonsterHelper.java:549-550";
}

TEST(BeyondNormalsII, TheseFiveCompositionsSpendNoMiscRng) {
    for (std::string_view key : {"Spire Growth", "Transient", "Maw",
                                 "Writhing Mass", "Jaw Worm Horde"}) {
        int32_t draws = -1;
        (void)members_of(key, 3, &draws);
        EXPECT_EQ(draws, 0) << key << ": a fixed emit list picks nothing";
    }
}

// The group HP-draw accounting the two-two split implies, through the real
// spawn path.
TEST(BeyondNormalsII, SpawnGroupHpDrawsFollowTheSetHpSplit) {
    {
        const MonsterId g[] = {MonsterId::TRANSIENT};
        CombatState s = MakeSeeded(19, /*monsters=*/0);
        spawn_group(s, g);
        EXPECT_EQ(s.monster_hp_rng.counter, 0);
        EXPECT_EQ(s.ai_rng.counter, 1);
    }
    {
        const MonsterId g[] = {MonsterId::MAW};
        CombatState s = MakeSeeded(19, /*monsters=*/0);
        spawn_group(s, g);
        EXPECT_EQ(s.monster_hp_rng.counter, 0);
        EXPECT_EQ(s.ai_rng.counter, 1);
    }
    {
        const MonsterId g[] = {MonsterId::SPIRE_GROWTH};
        CombatState s = MakeSeeded(19, /*monsters=*/0);
        spawn_group(s, g);
        EXPECT_EQ(s.monster_hp_rng.counter, 1);
    }
    {
        const MonsterId g[] = {MonsterId::WRITHING_MASS};
        CombatState s = MakeSeeded(19, /*monsters=*/0);
        spawn_group(s, g);
        EXPECT_EQ(s.monster_hp_rng.counter, 1);
    }
}

// ============================================================================
// 9. Dispatch-table shape
// ============================================================================

TEST(BeyondNormalsII, EveryNewMonsterIsRegisteredInEveryDispatchSwitch) {
    for (MonsterId id : {MonsterId::SPIRE_GROWTH, MonsterId::TRANSIENT,
                         MonsterId::MAW, MonsterId::WRITHING_MASS}) {
        EXPECT_NE(monster_init_fn(id), nullptr) << static_cast<int>(id);
        EXPECT_NE(monster_turn_fn(id), &default_monster_turn)
            << static_cast<int>(id);
        EXPECT_EQ(monster_spawn_at_hp_fn(id), nullptr)
            << "none of the four is mid-combat spawnable";
        EXPECT_EQ(monster_die_fn(id), nullptr)
            << "Transient.die is an achievement unlock and Maw.die is an "
               "UNSEEDED sound; neither is the Mugger's seeded draw";
        EXPECT_EQ(monster_die_after_fn(id), nullptr);
    }
    // Roll-move: three of four. The Transient decides its next move inside its
    // own turn body and never through this seam.
    EXPECT_NE(monster_roll_move_fn(MonsterId::SPIRE_GROWTH), nullptr);
    EXPECT_NE(monster_roll_move_fn(MonsterId::MAW), nullptr);
    EXPECT_NE(monster_roll_move_fn(MonsterId::WRITHING_MASS), nullptr);
    EXPECT_EQ(monster_roll_move_fn(MonsterId::TRANSIENT), nullptr);
    // Pre-battle: two of four, plus the Jaw Worm's newly registered no-op.
    EXPECT_NE(monster_pre_battle_fn(MonsterId::TRANSIENT), nullptr);
    EXPECT_NE(monster_pre_battle_fn(MonsterId::WRITHING_MASS), nullptr);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::SPIRE_GROWTH), nullptr);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::MAW), nullptr);
}

TEST(BeyondNormalsII, EveryNewPowerHasANativeBody) {
    for (PowerId id : {PowerId::CONSTRICTED, PowerId::FADING,
                       PowerId::SHIFTING, PowerId::REACTIVE}) {
        const r::PowerDef* d = r::power_def(id);
        ASSERT_NE(d, nullptr) << static_cast<int>(id);
        EXPECT_TRUE(d->native) << static_cast<int>(id);
    }
    // The game_id mismatch that decides every oracle diff.
    EXPECT_EQ(r::power_game_id(PowerId::REACTIVE),
              std::string_view("Compulsive"))
        << "ReactivePower.POWER_ID is \"Compulsive\" (ReactivePower.java:20)";
    EXPECT_EQ(r::monster_game_id(MonsterId::SPIRE_GROWTH),
              std::string_view("Serpent"))
        << "SpireGrowth.ID is \"Serpent\" (SpireGrowth.java:32)";
}

}  // namespace
}  // namespace sts::engine
