// Tier-2 Act-3 Beyond boss coverage (S2.28): the Awakened One (two phases,
// Curiosity/Unawakened, Void insertion, Cultist adds), the Time Eater (Time
// Warp / Draw Reduction / Slimed), and Donu and Deca.
//
// WHAT THIS FILE PINS, and why each part is here rather than assumed:
//
//   * STAT/MOVE TABLES, per boss, at EVERY ascension branch the Java has --
//     including the two branches whose constant NAMES lie (TimeEater's
//     HP/A_2_HP pair gates on >= 9; its A_2_REVERB_DMG/A_2_HEAD_SLAM_DMG pair
//     gates on >= 4), so the tier boundary is asserted, not the value alone.
//   * THE AWAKENED ONE'S PHASE TRANSITION, which lives in damage() rather than
//     die(): the half-death that latches halfDead, the hand-fired death
//     fan-outs (paid ONCE at the half-death because MonsterDieFn's veto
//     suppressed super.die(), TWICE at the real death because nothing does),
//     the selective power purge, the double setMove, and the Rebirth turn's
//     [roll][heal][can-lose] resolve order.
//   * TIME WARP'S TURN ECONOMY: the counter ticks once per card with no
//     filter, nothing decays it across rounds, and the 12th play zeroes it,
//     clears the pending card plays, queues the end-turn sentinel and pays
//     every monster RECORD +2 Strength.
//   * RNG DRAW COUNTS. All four ctors draw monster_hp_rng over a DEGENERATE
//     range -- setHp(int) is setHp(hp, hp) (AbstractMonster.java:777-779) and
//     the two-arg body draws unconditionally (:765-767; Random.java:58-61) --
//     and the Time Eater's getMove is the one tree in the batch whose draw
//     count depends on the move history (re-entries at TimeEater.java:188,
//     :207; the randomBoolean at :196).
//   * ENCOUNTER COMPOSITIONS, spawn-order-exact: "Awakened One" is two
//     Cultists THEN the boss, and "Donu and Deca" spawns Deca FIRST despite
//     the key's name (MonsterHelper.java:585-593).
//
// The A20 double-boss ROUTE (a20.yaml row 20) is run-layer machinery and is
// pinned in run_advance_test.cpp (BossVictory.TheA20DoubleBossInterposes...);
// this file owns the fights themselves.
//
// Provenance: AwakenedOne.java:67-377; TimeEater.java:49-222; Donu.java:33-145;
// Deca.java:36-157; CuriosityPower.java; UnawakenedPower.java;
// TimeWarpPower.java; DrawReductionPower.java; AbstractMonster.java:765-779;
// MonsterHelper.java:585-593; ProceedButton.java:99-113,210-220.

#include <cstdint>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_awakened_one.hpp"
#include "sts/engine/monster_cultist.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_donu_deca.hpp"
#include "sts/engine/monster_time_eater.hpp"
#include "sts/engine/power_hooks.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/relic_hooks.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

namespace r = sts::registry;
constexpr int32_t kA20 = kMonsterAscension;

// --- shared helpers (the city_normals idiom) --------------------------------

CombatState MakeState(uint8_t monsters = 1) {
    CombatState s{};
    s.player_hp = 400;
    s.player_max_hp = 400;
    s.player_energy = 3;
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

void player_attacks(CombatState& s, uint8_t mi, int32_t base) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    it.src = kActorPlayer;
    it.tgt = mi;
    it.amount = base;
    it.flags = make_damage_flags(DamageType::NORMAL);
    execute_opcode(s, it);
}

void fill_draw_pile(CombatState& s, int n) {
    for (int i = 0; i < n; ++i) {
        s.card_pool[i].card_id = static_cast<uint16_t>(CardId::STRIKE);
        s.draw[s.draw_count++] = static_cast<CardPoolIndex>(i);
    }
}

int count_in_pile(const CombatState& s, const CardPoolIndex* pile,
                  uint8_t count, CardId id) {
    int n = 0;
    for (uint8_t i = 0; i < count; ++i) {
        if (s.card_pool[pile[i]].card_id == static_cast<uint16_t>(id)) {
            ++n;
        }
    }
    return n;
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

void mirror_relic(CombatState& s, RelicId id, int16_t counter = -1) {
    s.relics[s.relic_count].relic_id = static_cast<uint16_t>(id);
    s.relics[s.relic_count].counter = counter;
    ++s.relic_count;
}

// ============================================================================
// 1. Registry -- stat and move tables, every ascension branch, per boss
// ============================================================================

TEST(BeyondBossesRegistry, AwakenedOneTableAcrossEveryAscensionBranch) {
    EXPECT_EQ(static_cast<int>(r::MonsterId::AWAKENED_ONE), 62);
    EXPECT_EQ(r::monster_from_game_id("AwakenedOne"), r::MonsterId::AWAKENED_ONE);
    const auto& d = r::kAwakenedOne;
    EXPECT_TRUE(d.ai_native);
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::BOSS);  // AwakenedOne.java:128
    EXPECT_EQ(d.roll_count, 0) << "no EXTRA ctor rolls; the hp draw is the hp "
                                  "column's own (degenerate) range";

    // setHp branch at >= 9 (:110-114). Degenerate ranges: min == max at every
    // tier, because the draw exists and the value does not vary.
    EXPECT_EQ(d.hp_min(0), 300);
    EXPECT_EQ(d.hp_max(0), 300);
    EXPECT_EQ(d.hp_min(8), 300);
    EXPECT_EQ(d.hp_min(9), 320);
    EXPECT_EQ(d.hp_max(9), 320);
    EXPECT_EQ(d.hp_min(kA20), 320);

    // Five DamageInfos (:131-135), NO ascension branch on any of them.
    EXPECT_EQ(step_count(d, r::kAwakenedOneMoveSlash), 1);
    EXPECT_EQ(step_amount(d, r::kAwakenedOneMoveSlash, 0, 0), 20);
    EXPECT_EQ(step_amount(d, r::kAwakenedOneMoveSlash, 0, kA20), 20);
    // Soul Strike: FOUR separate 6s (:169-171), not one 24.
    EXPECT_EQ(step_count(d, r::kAwakenedOneMoveSoulStrike), 4);
    for (uint8_t k = 0; k < 4; ++k) {
        EXPECT_EQ(step_amount(d, r::kAwakenedOneMoveSoulStrike, k, kA20), 6);
    }
    EXPECT_EQ(step_count(d, r::kAwakenedOneMoveDarkEcho), 1);
    EXPECT_EQ(step_amount(d, r::kAwakenedOneMoveDarkEcho, 0, kA20), 40);
    // Sludge: 18 then the Void into a random draw-pile spot (:193-194).
    const r::MonsterMove* sludge = d.move(r::kAwakenedOneMoveSludge);
    ASSERT_NE(sludge, nullptr);
    EXPECT_EQ(sludge->intent, MonsterIntent::ATTACK_DEBUFF);
    EXPECT_EQ(sludge->effect_count, 2);
    EXPECT_EQ(sludge->effects[0].amount.at(kA20), 18);
    EXPECT_EQ(sludge->effects[1].op, r::Opcode::MAKE_CARD);
    EXPECT_EQ(sludge->effects[1].extra & 0xFFFFu,
              static_cast<uint32_t>(CardId::VOID));
    EXPECT_EQ(sludge->effects[1].extra >> 16,
              static_cast<uint32_t>(CardPile::DRAW_RANDOM));
    EXPECT_EQ(sludge->effects[1].amount.at(kA20), 1);
    // Tackle: THREE separate 10s (:199-203).
    EXPECT_EQ(step_count(d, r::kAwakenedOneMoveTackle), 3);
    EXPECT_EQ(step_amount(d, r::kAwakenedOneMoveTackle, 2, kA20), 10);
    // Rebirth: the changeState program (:225-226) -- a full HEAL (the same two
    // numbers as the hp column) then CAN_LOSE, intent UNKNOWN (:309).
    const r::MonsterMove* rebirth = d.move(r::kAwakenedOneMoveRebirth);
    ASSERT_NE(rebirth, nullptr);
    EXPECT_EQ(rebirth->intent, MonsterIntent::UNKNOWN);
    ASSERT_EQ(rebirth->effect_count, 2);
    EXPECT_EQ(rebirth->effects[0].op, r::Opcode::HEAL);
    EXPECT_EQ(rebirth->effects[0].amount.at(0), 300);
    EXPECT_EQ(rebirth->effects[0].amount.at(9), 320);
    EXPECT_EQ(rebirth->effects[1].op, r::Opcode::CAN_LOSE);
}

TEST(BeyondBossesRegistry, TimeEaterTableAndItsTwoMisleadingTierNames) {
    EXPECT_EQ(static_cast<int>(r::MonsterId::TIME_EATER), 63);
    EXPECT_EQ(r::monster_from_game_id("TimeEater"), r::MonsterId::TIME_EATER);
    const auto& d = r::kTimeEater;
    EXPECT_TRUE(d.ai_native);
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::BOSS);  // TimeEater.java:87

    // THE HP FIELD NAMES LIE: HP / A_2_HP (:56-57) but the branch is `>= 9`
    // (:78). Assert the boundary, not just the endpoints.
    EXPECT_EQ(d.hp_min(0), 456);
    EXPECT_EQ(d.hp_min(8), 456) << "the HP branch is >= 9, not >= 2";
    EXPECT_EQ(d.hp_min(9), 480);
    EXPECT_EQ(d.hp_max(kA20), 480);

    // ...and the DAMAGE names lie the same way: A_2_REVERB_DMG / A_2_HEAD_SLAM
    // (:64,:67) but the branch is `>= 4` (:90-96), the boss-damage tier.
    EXPECT_EQ(step_count(d, r::kTimeEaterMoveReverberate), 3);
    EXPECT_EQ(step_amount(d, r::kTimeEaterMoveReverberate, 0, 3), 7)
        << "the damage branch is >= 4, not >= 2";
    EXPECT_EQ(step_amount(d, r::kTimeEaterMoveReverberate, 0, 4), 8);
    EXPECT_EQ(step_amount(d, r::kTimeEaterMoveReverberate, 2, kA20), 8);

    // Ripple: block 20 flat, Vulnerable/Weak 1, Frail 1 (the A19 arm -- the
    // MODULE gates its presence; the row authors the step). DEFEND_DEBUFF is
    // this batch's whole MonsterIntent grant.
    const r::MonsterMove* ripple = d.move(r::kTimeEaterMoveRipple);
    ASSERT_NE(ripple, nullptr);
    EXPECT_EQ(ripple->intent, MonsterIntent::DEFEND_DEBUFF);
    EXPECT_EQ(static_cast<int>(MonsterIntent::DEFEND_DEBUFF), 15);
    ASSERT_EQ(ripple->effect_count, 4);
    EXPECT_EQ(ripple->effects[0].op, r::Opcode::BLOCK);
    EXPECT_EQ(ripple->effects[0].amount.at(kA20), 20);
    EXPECT_EQ(ripple->effects[1].extra,
              static_cast<uint32_t>(PowerId::VULNERABLE));
    EXPECT_EQ(ripple->effects[2].extra, static_cast<uint32_t>(PowerId::WEAK));
    EXPECT_EQ(ripple->effects[3].extra, static_cast<uint32_t>(PowerId::FRAIL));

    // Head Slam: 26/32 at the >= 4 boundary, Draw Reduction 1, then the A19
    // two Slimed into the DISCARD (:141-142).
    const r::MonsterMove* slam = d.move(r::kTimeEaterMoveHeadSlam);
    ASSERT_NE(slam, nullptr);
    EXPECT_EQ(slam->intent, MonsterIntent::ATTACK_DEBUFF);
    EXPECT_EQ(slam->effects[0].amount.at(3), 26);
    EXPECT_EQ(slam->effects[0].amount.at(4), 32);
    EXPECT_EQ(slam->effects[1].extra,
              static_cast<uint32_t>(PowerId::DRAW_REDUCTION));
    EXPECT_EQ(slam->effects[1].amount.at(kA20), 1);
    EXPECT_EQ(slam->effects[2].op, r::Opcode::MAKE_CARD);
    EXPECT_EQ(slam->effects[2].extra & 0xFFFFu,
              static_cast<uint32_t>(CardId::SLIMED));
    EXPECT_EQ(slam->effects[2].extra >> 16,
              static_cast<uint32_t>(CardPile::DISCARD));
    EXPECT_EQ(slam->effects[2].amount.at(kA20), 2);

    // Haste: RemoveDebuffs, the redundant Shackled remove the Java queues
    // anyway, and the A19 block that reads the HEAD SLAM damage field (:151) --
    // so its tiers must match slam's exactly. The heal is NOT in the row (a
    // runtime amount; monster_time_eater.cpp queues it between steps 1 and 2).
    const r::MonsterMove* haste = d.move(r::kTimeEaterMoveHaste);
    ASSERT_NE(haste, nullptr);
    EXPECT_EQ(haste->intent, MonsterIntent::BUFF);
    ASSERT_EQ(haste->effect_count, 3);
    EXPECT_EQ(haste->effects[0].op, r::Opcode::REMOVE_DEBUFFS);
    EXPECT_EQ(haste->effects[1].op, r::Opcode::REMOVE_POWER);
    EXPECT_EQ(haste->effects[1].extra,
              static_cast<uint32_t>(PowerId::SHACKLED));
    EXPECT_EQ(haste->effects[2].op, r::Opcode::BLOCK);
    EXPECT_EQ(haste->effects[2].amount.at(3), 26);
    EXPECT_EQ(haste->effects[2].amount.at(4), 32);
}

TEST(BeyondBossesRegistry, DonuAndDecaTablesIncludingTheRealMoveIdZero) {
    EXPECT_EQ(static_cast<int>(r::MonsterId::DONU), 64);
    EXPECT_EQ(static_cast<int>(r::MonsterId::DECA), 65);
    EXPECT_EQ(r::monster_from_game_id("Donu"), r::MonsterId::DONU);
    EXPECT_EQ(r::monster_from_game_id("Deca"), r::MonsterId::DECA);

    for (const auto* d : {&r::kDonu, &r::kDeca}) {
        EXPECT_TRUE(d->ai_native);
        EXPECT_EQ(d->enemy_type, r::MonsterEnemyType::BOSS);
        EXPECT_EQ(d->hp_min(0), 250);
        EXPECT_EQ(d->hp_min(8), 250);
        EXPECT_EQ(d->hp_min(9), 265);
        EXPECT_EQ(d->hp_max(kA20), 265);
        // BEAM is a REAL move id 0 -- the sentinel-collision case the loader
        // bound was relaxed for. Both getMoves read no history, so the
        // collision is inert; this lookup working at all is the pin.
        EXPECT_EQ(r::kDonuMoveBeam, 0);
        EXPECT_EQ(r::kDecaMoveBeam, 0);
        const r::MonsterMove* beam = d->move(0);
        ASSERT_NE(beam, nullptr);
        // beamDmg: 10, 12 from A4 (Donu.java:68 / Deca.java:72). Two hits.
        EXPECT_EQ(beam->effects[0].amount.at(3), 10);
        EXPECT_EQ(beam->effects[0].amount.at(4), 12);
        EXPECT_EQ(beam->effects[1].amount.at(kA20), 12);
    }

    // Donu's Beam is plain ATTACK; Deca's is ATTACK_DEBUFF and adds 2 Dazed to
    // the DISCARD (Deca.java:118).
    EXPECT_EQ(r::kDonu.move(0)->intent, MonsterIntent::ATTACK);
    EXPECT_EQ(r::kDonu.move(0)->effect_count, 2);
    const r::MonsterMove* dbeam = r::kDeca.move(0);
    EXPECT_EQ(dbeam->intent, MonsterIntent::ATTACK_DEBUFF);
    ASSERT_EQ(dbeam->effect_count, 3);
    EXPECT_EQ(dbeam->effects[2].op, r::Opcode::MAKE_CARD);
    EXPECT_EQ(dbeam->effects[2].extra & 0xFFFFu,
              static_cast<uint32_t>(CardId::DAZED));
    EXPECT_EQ(dbeam->effects[2].extra >> 16,
              static_cast<uint32_t>(CardPile::DISCARD));
    EXPECT_EQ(dbeam->effects[2].amount.at(kA20), 2);

    // Circle: BUFF (Donu.java:129 -- NOT the DEFEND_DEBUFF the dispatching
    // brief claimed), one authored template step of Strength 3 the module fans
    // out per RECORD. Square: DEFEND_BUFF (the live A20 arm of Deca's
    // ascension-switched telegraph, :139-141), block 16 + Plated Armor 3.
    const r::MonsterMove* circle = r::kDonu.move(r::kDonuMoveCircleOfProtection);
    ASSERT_NE(circle, nullptr);
    EXPECT_EQ(circle->intent, MonsterIntent::BUFF);
    ASSERT_EQ(circle->effect_count, 1);
    EXPECT_EQ(circle->effects[0].extra,
              static_cast<uint32_t>(PowerId::STRENGTH));
    EXPECT_EQ(circle->effects[0].amount.at(0), 3);
    const r::MonsterMove* square = r::kDeca.move(r::kDecaMoveSquareOfProtection);
    ASSERT_NE(square, nullptr);
    EXPECT_EQ(square->intent, MonsterIntent::DEFEND_BUFF);
    ASSERT_EQ(square->effect_count, 2);
    EXPECT_EQ(square->effects[0].op, r::Opcode::BLOCK);
    EXPECT_EQ(square->effects[0].amount.at(kA20), 16);
    EXPECT_EQ(square->effects[1].extra,
              static_cast<uint32_t>(PowerId::PLATED_ARMOR));
    EXPECT_EQ(square->effects[1].amount.at(kA20), 3);
}

// ============================================================================
// 2. Ctor RNG -- fixed HP, real stream movement
// ============================================================================

TEST(BeyondBosses, EveryCtorSpendsExactlyOneDegenerateHpDraw) {
    // Fixed value at every seed, ONE counter step at every seed: the
    // observable is the stream, not the number.
    for (int64_t seed : {1, 7, 999, 424242}) {
        CombatState s = MakeSeeded(seed, 4);
        awakened_one_init(s, 0);
        EXPECT_EQ(s.monster_hp_rng.counter, 1);
        EXPECT_EQ(s.monsters[0].hp, 320);  // A9+ column at kMonsterAscension
        time_eater_init(s, 1);
        EXPECT_EQ(s.monster_hp_rng.counter, 2);
        EXPECT_EQ(s.monsters[1].hp, 480);
        donu_init(s, 2);
        EXPECT_EQ(s.monster_hp_rng.counter, 3);
        EXPECT_EQ(s.monsters[2].hp, 265);
        deca_init(s, 3);
        EXPECT_EQ(s.monster_hp_rng.counter, 4);
        EXPECT_EQ(s.monsters[3].hp, 265);
    }
}

// ============================================================================
// 3. Encounter compositions -- spawn-order-exact
// ============================================================================

TEST(BeyondBosses, AwakenedOneEncounterIsTwoCultistsThenTheBoss) {
    CombatState s = MakeSeeded(11, 0);
    ResolvedGroup grp{};
    ASSERT_TRUE(resolve_encounter("Awakened One", s.misc_rng, grp));
    ASSERT_EQ(grp.count, 3);
    EXPECT_EQ(grp.members[0], "Cultist");
    EXPECT_EQ(grp.members[1], "Cultist");
    EXPECT_EQ(grp.members[2], "AwakenedOne");

    const MonsterId ids[3] = {MonsterId::CULTIST, MonsterId::CULTIST,
                              MonsterId::AWAKENED_ONE};
    s.monster_count = 0;
    spawn_group(s, ids);
    ASSERT_EQ(s.monster_count, 3);
    EXPECT_EQ(s.monsters[2].monster_id,
              static_cast<uint16_t>(MonsterId::AWAKENED_ONE));
    // The Cultists' RANGED rolls go first, in spawn order, then the boss's
    // degenerate one: exactly three monster_hp_rng draws.
    EXPECT_EQ(s.monster_hp_rng.counter, 3);
    EXPECT_EQ(s.monsters[2].hp, 320);
}

TEST(BeyondBosses, DonuAndDecaSpawnsDecaFirstDespiteTheKeysName) {
    CombatState s = MakeSeeded(11, 0);
    ResolvedGroup grp{};
    ASSERT_TRUE(resolve_encounter("Donu and Deca", s.misc_rng, grp));
    ASSERT_EQ(grp.count, 2);
    EXPECT_EQ(grp.members[0], "Deca") << "MonsterHelper.java:592";
    EXPECT_EQ(grp.members[1], "Donu");

    ResolvedGroup te{};
    ASSERT_TRUE(resolve_encounter("Time Eater", s.misc_rng, te));
    ASSERT_EQ(te.count, 1);
    EXPECT_EQ(te.members[0], "TimeEater");
}

// ============================================================================
// 4. The Awakened One -- pre-battle, both trees, the phase transition
// ============================================================================

// Build the boss alone at slot 0 with its pre-battle grants resolved and the
// room's cannotLose latch set, the state a real fight starts in.
CombatState make_awakened(int64_t seed) {
    CombatState s = MakeSeeded(seed);
    s.monster_count = 1;
    awakened_one_init(s, 0);
    awakened_one_use_pre_battle_action(s, 0);
    drain(s);
    return s;
}

TEST(AwakenedOne, PreBattleGrantsAndTheCannotLoseLatch) {
    CombatState s = make_awakened(3);
    EXPECT_NE(s.flags & kCombatFlagCannotLose, 0u)
        << "a DIRECT field write (:143), not a queued CannotLoseAction";
    // A19+ amounts (:144-153): Regenerate 15, Curiosity 2, Unawakened, and the
    // A4 Strength 2.
    EXPECT_EQ(monster_power(s, 0, PowerId::REGENERATE_MONSTER), 15);
    EXPECT_EQ(monster_power(s, 0, PowerId::CURIOSITY), 2);
    EXPECT_EQ(monster_power(s, 0, PowerId::UNAWAKENED), -1)
        << "amount -1 is the game's own value (UnawakenedPower.java:21), so "
           "the absent-power sentinel is sidestepped below";
    // -1 is also this helper's miss value, so pin presence separately.
    bool has_unawakened = false;
    for (uint8_t i = 0; i < s.monsters[0].power_count; ++i) {
        has_unawakened = has_unawakened ||
                         s.monsters[0].powers[i].power_id ==
                             static_cast<uint16_t>(PowerId::UNAWAKENED);
    }
    EXPECT_TRUE(has_unawakened);
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), 2);
    // Opener: phase 1's firstTurn arm ignores num -- SLASH at every seed.
    EXPECT_EQ(s.monsters[0].move_history[0], r::kAwakenedOneMoveSlash);
    EXPECT_EQ(s.ai_rng.counter, 1) << "the ignored draw is still spent";
}

TEST(AwakenedOne, PhaseOneTreeMatchesTheJavaGuards) {
    CombatState s = make_awakened(3);
    MonsterState& m = s.monsters[0];
    // firstTurn was cleared inside getMove (:247). num < 25: Soul Strike
    // unless the LAST move was Soul Strike (:251-255).
    awakened_one_decide_move(s, 0, 10);
    EXPECT_EQ(m.move_history[0], r::kAwakenedOneMoveSoulStrike);
    awakened_one_decide_move(s, 0, 10);
    EXPECT_EQ(m.move_history[0], r::kAwakenedOneMoveSlash)
        << "lastMove(2) blocks a repeat";
    // num >= 25: Slash unless the last TWO were Slash (:256-259).
    awakened_one_decide_move(s, 0, 80);
    EXPECT_EQ(m.move_history[0], r::kAwakenedOneMoveSlash)
        << "one Slash in history does not block";
    awakened_one_decide_move(s, 0, 80);
    EXPECT_EQ(m.move_history[0], r::kAwakenedOneMoveSoulStrike)
        << "lastTwoMoves(1) blocks the third";
}

TEST(AwakenedOne, HalfDeathTransitionPurgesAndTurnsTheBossAround) {
    CombatState s = make_awakened(3);
    // A debuff to purge, on top of the pre-battle sheet.
    give_monster_power(s, 0, PowerId::VULNERABLE, 3);
    player_attacks(s, 0, 1000);

    const MonsterState& m = s.monsters[0];
    EXPECT_EQ(m.hp, 0);
    EXPECT_TRUE(monster_half_dead(m)) << "cannotLose -> halfDead (:292-294)";
    EXPECT_TRUE(monster_dead_or_escaped(m)) << "half-dead IS dead to targeting";
    EXPECT_FALSE(monster_basically_dead(m)) << "...and ALIVE to the fight";
    // The purge (:302-308): every DEBUFF + {Curiosity, Unawakened, Shackled}.
    // STRENGTH AND REGENERATE SURVIVE -- the live gameplay fact.
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), 2);
    EXPECT_EQ(monster_power(s, 0, PowerId::REGENERATE_MONSTER), 15);
    EXPECT_EQ(monster_power(s, 0, PowerId::CURIOSITY), -1);
    EXPECT_EQ(monster_power(s, 0, PowerId::VULNERABLE), -1);
    for (uint8_t i = 0; i < m.power_count; ++i) {
        EXPECT_NE(m.powers[i].power_id,
                  static_cast<uint16_t>(PowerId::UNAWAKENED));
    }
    // The synchronous setMove (:309) and the phase flags (:314-315).
    EXPECT_EQ(m.move_history[0], r::kAwakenedOneMoveRebirth);
    EXPECT_EQ(m.flags & kMonsterFlagAwakenedForm1, 0u);
    EXPECT_NE(m.flags & kMonsterFlagAwakenedFirstTurn, 0u);
    // The queue: ClearCardQueue at the TOP (:301), the backstop SetMoveAction
    // at the bottom (:312).
    ASSERT_GE(s.action_count, 2);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::CLEAR_CARD_QUEUE));
    EXPECT_EQ(queued(s, s.action_count - 1).opcode,
              static_cast<uint16_t>(Opcode::SET_MOVE));
    EXPECT_EQ(queued(s, s.action_count - 1).amount, r::kAwakenedOneMoveRebirth);
}

TEST(AwakenedOne, RebirthTurnHealsClearsTheLatchesAndOpensOnDarkEcho) {
    CombatState s = make_awakened(3);
    player_attacks(s, 0, 1000);
    drain(s);  // settle the transition's queued items
    ASSERT_EQ(s.monsters[0].move_history[0], r::kAwakenedOneMoveRebirth);

    const int32_t ai_before = s.ai_rng.counter;
    awakened_one_take_turn(s, 0);
    // [roll][heal][can_lose], the changeState resolve order (:225-226 land
    // BEHIND the takeTurn-queued RollMoveAction :207).
    ASSERT_EQ(s.action_count, 3);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE));
    EXPECT_EQ(queued(s, 1).opcode, static_cast<uint16_t>(Opcode::HEAL));
    EXPECT_EQ(queued(s, 2).opcode, static_cast<uint16_t>(Opcode::CAN_LOSE));
    drain(s);

    const MonsterState& m = s.monsters[0];
    EXPECT_EQ(m.hp, 320) << "HealAction(this, this, maxHealth) from 0 (:225)";
    EXPECT_FALSE(monster_half_dead(m)) << "op_heal clears the bit with the heal";
    EXPECT_EQ(s.flags & kCombatFlagCannotLose, 0u) << "CanLoseAction re-armed";
    // The roll ran while firstTurn was still set (phase 2's arm does NOT clear
    // it, :262-265), so the opener is Dark Echo -- and the draw was spent.
    EXPECT_EQ(m.move_history[0], r::kAwakenedOneMoveDarkEcho);
    EXPECT_EQ(s.ai_rng.counter, ai_before + 1);

    // The Dark Echo turn clears the latch at takeTurn (:183), and the phase-2
    // tree takes over: num < 50 -> Sludge (no history of it yet).
    awakened_one_take_turn(s, 0);
    EXPECT_EQ(m.flags & kMonsterFlagAwakenedFirstTurn, 0u)
        << "cleared SYNCHRONOUSLY mid-case (:183), before the queue drains";
    drain(s);  // includes the queued roll
}

TEST(AwakenedOne, PhaseTwoTreeMatchesTheJavaGuards) {
    CombatState s = make_awakened(3);
    MonsterState& m = s.monsters[0];
    m.flags &= ~(kMonsterFlagAwakenedForm1 | kMonsterFlagAwakenedFirstTurn);
    // num < 50: Sludge unless the last TWO were Sludge (:267-271).
    awakened_one_decide_move(s, 0, 10);
    EXPECT_EQ(m.move_history[0], r::kAwakenedOneMoveSludge);
    awakened_one_decide_move(s, 0, 10);
    EXPECT_EQ(m.move_history[0], r::kAwakenedOneMoveSludge);
    awakened_one_decide_move(s, 0, 10);
    EXPECT_EQ(m.move_history[0], r::kAwakenedOneMoveTackle)
        << "lastTwoMoves(6) blocks the third Sludge";
    // num >= 50: Tackle unless the last TWO were Tackle (:272-276).
    awakened_one_decide_move(s, 0, 90);
    EXPECT_EQ(m.move_history[0], r::kAwakenedOneMoveTackle);
    awakened_one_decide_move(s, 0, 90);
    EXPECT_EQ(m.move_history[0], r::kAwakenedOneMoveSludge)
        << "lastTwoMoves(8) blocks the third Tackle";
}

TEST(AwakenedOne, SludgeInsertsTheVoidAtOneCardRandomDrawPerCopy) {
    CombatState s = make_awakened(3);
    fill_draw_pile(s, 5);
    set_monster_move(s.monsters[0], r::kAwakenedOneMoveSludge,
                     MonsterIntent::ATTACK_DEBUFF);
    const int32_t before = s.card_random_rng.counter;
    awakened_one_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(count_in_pile(s, s.draw, s.draw_count, CardId::VOID), 1);
    EXPECT_EQ(s.card_random_rng.counter, before + 1)
        << "CardGroup.addToRandomSpot: one draw per copy (CardGroup.java:463-469)";
}

TEST(AwakenedOne, DeathFanOutsFireOnceAtHalfDeathAndTwiceAtTheRealDeath) {
    // Gremlin Horn binds onMonsterDeath, so it is the observable for the
    // hand-fired fan-outs. A live Cultist keeps its areMonstersBasicallyDead
    // guard open at both edges.
    CombatState s = MakeSeeded(3, 2);
    cultist_init(s, 0);
    awakened_one_init(s, 1);
    awakened_one_use_pre_battle_action(s, 1);
    drain(s);
    mirror_relic(s, RelicId::GREMLIN_HORN);
    fill_draw_pile(s, 6);
    const int16_t energy0 = s.player_energy;

    // HALF-DEATH: die() is vetoed wholesale (:356-357), so the ONLY fan-out is
    // the damage() override's hand-fired one (:295-300) -- the horn pays ONCE.
    player_attacks(s, 1, 1000);
    drain(s);
    EXPECT_EQ(s.player_energy, energy0 + 1);
    EXPECT_TRUE(monster_half_dead(s.monsters[1]));

    // Walk through Rebirth so cannotLose clears.
    awakened_one_take_turn(s, 1);
    drain(s);
    ASSERT_EQ(s.monsters[1].hp, 320);
    ASSERT_EQ(s.flags & kCombatFlagCannotLose, 0u);

    // REAL DEATH: super.die() runs the fan-outs (#1) and the damage() override
    // falls back into the same block and re-fires them (#2) -- the horn pays
    // TWICE. Faithful, deliberately not "fixed" (AwakenedOne.java:291-300).
    const int16_t energy1 = s.player_energy;
    player_attacks(s, 1, 1000);
    drain(s);
    EXPECT_EQ(s.player_energy, energy1 + 2)
        << "Gremlin Horn pays twice on the phase-2 death";
    EXPECT_FALSE(monster_half_dead(s.monsters[1]));
    // ...and the surviving Cultist FLED (die()'s post-super walk, :366-369).
    EXPECT_TRUE(monster_escaped(s.monsters[0]));
}

// ============================================================================
// 5. The Time Eater -- the recursive tree, the Haste latch, the two A19 arms
// ============================================================================

CombatState make_time_eater(int64_t seed) {
    CombatState s = MakeSeeded(seed);
    s.monster_count = 1;
    time_eater_init(s, 0);
    time_eater_use_pre_battle_action(s, 0);
    drain(s);
    return s;
}

TEST(TimeEater, PreBattleAppliesTimeWarpAtZero) {
    CombatState s = make_time_eater(5);
    EXPECT_EQ(monster_power(s, 0, PowerId::TIME_WARP), 0)
        << "the 1-arg ctor starts the counter at 0 (TimeWarpPower.java:26)";
}

TEST(TimeEater, MoveBandsAndTheHistoryGuardedReentries) {
    CombatState s = make_time_eater(5);
    MonsterState& m = s.monsters[0];
    // Clean history per branch probe.
    auto reset = [&m]() {
        m.move_history[0] = 0;
        m.move_history[1] = 0;
        m.move_history[2] = 0;
    };
    // The three bands, unguarded: <45 Reverberate, <80 Head Slam, else Ripple.
    reset();
    time_eater_decide_move(s, 0, 0);
    EXPECT_EQ(m.move_history[0], r::kTimeEaterMoveReverberate);
    reset();
    time_eater_decide_move(s, 0, 45);
    EXPECT_EQ(m.move_history[0], r::kTimeEaterMoveHeadSlam);
    reset();
    time_eater_decide_move(s, 0, 80);
    EXPECT_EQ(m.move_history[0], r::kTimeEaterMoveRipple);

    // <45 with two Reverberates in history: ONE extra draw (random(50,99),
    // :188) and the result is anything the 50..99 window picks -- never a
    // third Reverberate.
    reset();
    m.move_history[0] = r::kTimeEaterMoveReverberate;
    m.move_history[1] = r::kTimeEaterMoveReverberate;
    int32_t before = s.ai_rng.counter;
    time_eater_decide_move(s, 0, 10);
    EXPECT_EQ(s.ai_rng.counter, before + 1);
    EXPECT_NE(m.move_history[0], r::kTimeEaterMoveReverberate);

    // <80 with a Head Slam last: the randomBoolean(0.66) arm (:196) -- one
    // extra draw, terminating either way.
    reset();
    m.move_history[0] = r::kTimeEaterMoveHeadSlam;
    before = s.ai_rng.counter;
    time_eater_decide_move(s, 0, 60);
    EXPECT_EQ(s.ai_rng.counter, before + 1);
    EXPECT_NE(m.move_history[0], r::kTimeEaterMoveHeadSlam);

    // >= 80 with a Ripple last: re-enter with random(74) (:206). The <45
    // band's guard needs TWO Reverberates, so one re-entry always lands.
    reset();
    m.move_history[0] = r::kTimeEaterMoveRipple;
    before = s.ai_rng.counter;
    time_eater_decide_move(s, 0, 95);
    EXPECT_EQ(s.ai_rng.counter, before + 1);
    EXPECT_NE(m.move_history[0], r::kTimeEaterMoveRipple);
}

TEST(TimeEater, HasteGateIsStrictHalfAndOneShot) {
    CombatState s = make_time_eater(5);
    MonsterState& m = s.monsters[0];
    // EXACTLY half is NOT below half: strict `<` on integer division (:178).
    m.hp = 240;  // 480 / 2
    time_eater_decide_move(s, 0, 0);
    EXPECT_NE(m.move_history[0], r::kTimeEaterMoveHaste);
    m.hp = 239;
    time_eater_decide_move(s, 0, 0);
    EXPECT_EQ(m.move_history[0], r::kTimeEaterMoveHaste);
    EXPECT_NE(m.flags & kMonsterFlagTimeEaterUsedHaste, 0u);
    // One-shot: still below half, the latch holds.
    time_eater_decide_move(s, 0, 0);
    EXPECT_NE(m.move_history[0], r::kTimeEaterMoveHaste);
}

TEST(TimeEater, HasteTurnPurgesDebuffsHealsToHalfAndBlocksAtA19) {
    CombatState s = make_time_eater(5);
    MonsterState& m = s.monsters[0];
    give_monster_power(s, 0, PowerId::VULNERABLE, 2);
    give_monster_power(s, 0, PowerId::SHACKLED, 4);
    m.hp = 100;
    time_eater_decide_move(s, 0, 0);  // latches + telegraphs Haste
    ASSERT_EQ(m.move_history[0], r::kTimeEaterMoveHaste);
    time_eater_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::VULNERABLE), -1)
        << "RemoveDebuffsAction (:147)";
    EXPECT_EQ(monster_power(s, 0, PowerId::SHACKLED), -1)
        << "removed by the debuff sweep; the named remove (:148) is a no-op";
    EXPECT_EQ(m.hp, 240) << "maxHealth / 2 - currentHealth from 100 (:149)";
    EXPECT_EQ(m.block, 32) << "A19 GainBlockAction(headSlamDmg) (:150-151)";
    EXPECT_EQ(monster_power(s, 0, PowerId::TIME_WARP), 0)
        << "Time Warp is a BUFF; the sweep does not touch it";
}

TEST(TimeEater, HeadSlamAndRippleCarryTheirA19Arms) {
    CombatState s = make_time_eater(5);
    set_monster_move(s.monsters[0], r::kTimeEaterMoveHeadSlam,
                     MonsterIntent::ATTACK_DEBUFF);
    time_eater_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(s.player_hp, 400 - 32);
    EXPECT_EQ(player_power_amount(s, PowerId::DRAW_REDUCTION), 1);
    EXPECT_EQ(count_in_pile(s, s.discard, s.discard_count, CardId::SLIMED), 2)
        << "the A19 MakeTempCardInDiscardAction(Slimed, 2) (:141-142)";

    set_monster_move(s.monsters[0], r::kTimeEaterMoveRipple,
                     MonsterIntent::DEFEND_DEBUFF);
    time_eater_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(s.monsters[0].block, 20);
    EXPECT_EQ(player_power_amount(s, PowerId::VULNERABLE), 1);
    EXPECT_EQ(player_power_amount(s, PowerId::WEAK), 1);
    EXPECT_EQ(player_power_amount(s, PowerId::FRAIL), 1)
        << "the A19 Frail arm (:132-133)";
}

// ============================================================================
// 6. Time Warp -- the turn economy
// ============================================================================

TEST(TimeWarp, TwelfthPlayEndsTheTurnAndPaysEveryRecordStrength) {
    CombatState s = MakeSeeded(5, 2);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::TIME_EATER);
    s.monsters[0].hp = 480;
    s.monsters[0].max_hp = 480;
    s.monsters[1].hp = 0;  // a dead record: queued its item, no-ops at resolve
    give_monster_power(s, 0, PowerId::TIME_WARP, 0);

    // Eleven plays: the counter ticks, nothing else happens. No card-type
    // filter of any kind -- a STATUS play counts (:52-55).
    for (int i = 0; i < 11; ++i) {
        dispatch_on_after_use_card(s, 0, static_cast<uint16_t>(
                                             i % 2 == 0 ? CardId::STRIKE
                                                        : CardId::SLIMED));
    }
    EXPECT_EQ(monster_power(s, 0, PowerId::TIME_WARP), 11);
    EXPECT_EQ(s.action_count, 0);
    EXPECT_EQ(s.card_queue_count, 0);

    // The 12th: zero the counter, END the player's turn synchronously (the
    // queued plays are cleared and the end-turn sentinel is next), then +2
    // Strength per monster RECORD.
    dispatch_on_after_use_card(s, 0, static_cast<uint16_t>(CardId::STRIKE));
    EXPECT_EQ(monster_power(s, 0, PowerId::TIME_WARP), 0);
    ASSERT_EQ(s.card_queue_count, 1);
    EXPECT_EQ(s.card_queue[0].card_index, kEndTurnSentinel)
        << "callEndTurnEarlySequence (:63): the sentinel is the next card item";
    ASSERT_EQ(s.action_count, 2) << "one APPLY_POWER per RECORD, dead included";
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), 2);
    EXPECT_EQ(monster_power(s, 1, PowerId::STRENGTH), -1)
        << "the dead record's item no-opped at resolve (ApplyPowerAction's "
           "isDeadOrEscaped early-out)";
}

TEST(TimeWarp, NothingDecaysTheCounterAcrossRounds) {
    CombatState s = MakeSeeded(5);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::TIME_EATER);
    s.monsters[0].hp = 480;
    s.monsters[0].max_hp = 480;
    give_monster_power(s, 0, PowerId::TIME_WARP, 0);
    for (int i = 0; i < 5; ++i) {
        dispatch_on_after_use_card(s, 0, static_cast<uint16_t>(CardId::STRIKE));
    }
    dispatch_at_end_of_round(s);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::TIME_WARP), 5)
        << "not turn-based: a 5-card turn and a 7-card turn together trip it";
    for (int i = 0; i < 7; ++i) {
        dispatch_on_after_use_card(s, 0, static_cast<uint16_t>(CardId::STRIKE));
    }
    EXPECT_EQ(monster_power(s, 0, PowerId::TIME_WARP), 0) << "the 12th fired";
}

// ============================================================================
// 7. Draw Reduction -- the derived hand size and the justApplied skip
// ============================================================================

void apply_power_to_player(CombatState& s, uint8_t src, PowerId id,
                           int32_t amount) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    it.src = src;
    it.tgt = kActorPlayer;
    it.amount = amount;
    it.flags = make_apply_power_flags(id);
    execute_opcode(s, it);
}

TEST(DrawReduction, HandSizeDerivesFromPresenceAndTheSkipIsOneShot) {
    CombatState s = MakeSeeded(5);
    s.monsters[0].hp = 480;
    s.monsters[0].max_hp = 480;
    ASSERT_EQ(game_hand_size(s), 5);

    apply_power_to_player(s, 0, PowerId::DRAW_REDUCTION, 1);
    EXPECT_EQ(player_power_amount(s, PowerId::DRAW_REDUCTION), 1);
    EXPECT_EQ(player_power_counter(s, PowerId::DRAW_REDUCTION), 1)
        << "justApplied latches on the NEW-SLOT path";
    EXPECT_EQ(game_hand_size(s), 4) << "onInitialApplication --gameHandSize";

    // Round 1: the skip -- the latch clears, the stack does NOT tick.
    dispatch_at_end_of_round(s);
    drain(s);
    EXPECT_EQ(player_power_amount(s, PowerId::DRAW_REDUCTION), 1);
    EXPECT_EQ(player_power_counter(s, PowerId::DRAW_REDUCTION), 0);

    // A SECOND application stacks the amount but re-arms NOTHING, and the
    // hand shrinks by only the one card -- onInitialApplication fires only on
    // the first application (AbstractCreature.addPower :506-513).
    apply_power_to_player(s, 0, PowerId::DRAW_REDUCTION, 1);
    EXPECT_EQ(player_power_amount(s, PowerId::DRAW_REDUCTION), 2);
    EXPECT_EQ(player_power_counter(s, PowerId::DRAW_REDUCTION), 0)
        << "the stacking path does not rewrite the latch";
    EXPECT_EQ(game_hand_size(s), 4) << "presence, not stack count";

    // Round 2: tick to 1. Round 3: tick to 0 -> removed -> the hand returns.
    dispatch_at_end_of_round(s);
    drain(s);
    EXPECT_EQ(player_power_amount(s, PowerId::DRAW_REDUCTION), 1);
    dispatch_at_end_of_round(s);
    drain(s);
    EXPECT_EQ(player_power_amount(s, PowerId::DRAW_REDUCTION), -1);
    EXPECT_EQ(game_hand_size(s), 5) << "the balanced pair, derived";
}

// ============================================================================
// 8. Donu and Deca -- the out-of-phase alternation and the unguarded walks
// ============================================================================

CombatState make_shapes(int64_t seed) {
    CombatState s = MakeSeeded(seed, 2);
    deca_init(s, 0);   // spawn order: Deca FIRST (MonsterHelper.java:592)
    donu_init(s, 1);
    deca_use_pre_battle_action(s, 0);
    donu_use_pre_battle_action(s, 1);
    drain(s);
    return s;
}

TEST(DonuDeca, ArtifactPreBattleAndTheOutOfPhaseOpeners) {
    CombatState s = make_shapes(7);
    EXPECT_EQ(monster_power(s, 0, PowerId::ARTIFACT), 3) << "A19+: 3, else 2";
    EXPECT_EQ(monster_power(s, 1, PowerId::ARTIFACT), 3);
    // Deca's isAttacking initialises TRUE (:74), Donu's FALSE (:70).
    EXPECT_EQ(s.monsters[0].move_history[0], r::kDecaMoveBeam);
    EXPECT_EQ(s.monsters[1].move_history[0], r::kDonuMoveCircleOfProtection);
    EXPECT_EQ(s.ai_rng.counter, 2)
        << "both init rolls spent their draw and ignored it";
}

TEST(DonuDeca, ThePairAlternatesForeverAndEachTurnSpendsOneDraw) {
    CombatState s = make_shapes(7);
    for (int turn = 0; turn < 6; ++turn) {
        const int32_t before = s.ai_rng.counter;
        deca_take_turn(s, 0);
        donu_take_turn(s, 1);
        drain(s);
        EXPECT_EQ(s.ai_rng.counter, before + 2)
            << "one queued RollMoveAction each; the draw is spent, the value "
               "ignored";
        // Swap every turn, permanently out of phase.
        const bool deca_beams = (turn % 2) == 1;
        EXPECT_EQ(s.monsters[0].move_history[0],
                  deca_beams ? r::kDecaMoveBeam
                             : r::kDecaMoveSquareOfProtection);
        EXPECT_EQ(s.monsters[1].move_history[0],
                  deca_beams ? r::kDonuMoveCircleOfProtection
                             : r::kDonuMoveBeam);
    }
}

TEST(DonuDeca, CircleOfProtectionFansOutPerRecordIncludingItself) {
    CombatState s = make_shapes(7);
    set_monster_move(s.monsters[1], r::kDonuMoveCircleOfProtection,
                     MonsterIntent::BUFF);
    donu_take_turn(s, 1);
    drain(s);
    // Strength is a BUFF: Artifact does not intercept it.
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), 3);
    EXPECT_EQ(monster_power(s, 1, PowerId::STRENGTH), 3) << "including itself";
}

TEST(DonuDeca, SquareOfProtectionInterleavesBlockAndArmorPerMember) {
    CombatState s = make_shapes(7);
    set_monster_move(s.monsters[0], r::kDecaMoveSquareOfProtection,
                     MonsterIntent::DEFEND_BUFF);
    deca_take_turn(s, 0);
    // block(0), armor(0), block(1), armor(1) -- the A19 armor sits INSIDE the
    // same loop body (Deca.java:122-128) -- then the trailing roll.
    ASSERT_EQ(s.action_count, 5);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::BLOCK));
    EXPECT_EQ(queued(s, 0).tgt, 0);
    EXPECT_EQ(queued(s, 1).opcode, static_cast<uint16_t>(Opcode::APPLY_POWER));
    EXPECT_EQ(queued(s, 1).tgt, 0);
    EXPECT_EQ(queued(s, 2).opcode, static_cast<uint16_t>(Opcode::BLOCK));
    EXPECT_EQ(queued(s, 2).tgt, 1);
    EXPECT_EQ(queued(s, 3).opcode, static_cast<uint16_t>(Opcode::APPLY_POWER));
    EXPECT_EQ(queued(s, 3).tgt, 1);
    EXPECT_EQ(queued(s, 4).opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE));
    drain(s);
    EXPECT_EQ(s.monsters[0].block, 16);
    EXPECT_EQ(s.monsters[1].block, 16);
    EXPECT_EQ(monster_power(s, 0, PowerId::PLATED_ARMOR), 3);
    EXPECT_EQ(monster_power(s, 1, PowerId::PLATED_ARMOR), 3)
        << "the first producer in the game that plates ANOTHER monster";
}

TEST(DonuDeca, TheUnguardedWalksNoOpOnACorpseAtResolveTime) {
    CombatState s = make_shapes(7);
    s.monsters[1].hp = 0;  // Donu is a corpse (plain dead, not half-dead)
    set_monster_move(s.monsters[0], r::kDecaMoveSquareOfProtection,
                     MonsterIntent::DEFEND_BUFF);
    deca_take_turn(s, 0);
    ASSERT_EQ(s.action_count, 5)
        << "queue-time walk is UNGUARDED: the corpse still gets its items";
    drain(s);
    EXPECT_EQ(s.monsters[0].block, 16);
    EXPECT_EQ(monster_power(s, 0, PowerId::PLATED_ARMOR), 3);
    EXPECT_EQ(s.monsters[1].block, 0)
        << "GainBlockAction's isDying/isDead read rejects it";
    EXPECT_EQ(monster_power(s, 1, PowerId::PLATED_ARMOR), -1)
        << "ApplyPowerAction's isDeadOrEscaped early-out rejects it";
}

TEST(DonuDeca, AHalfDeadRecipientGainsBlockButNoPower) {
    // The two resolve-time guards genuinely differ: GainBlockAction tests
    // isDying/isDead (a half-dead monster is neither), ApplyPowerAction tests
    // isDeadOrEscaped (a half-dead monster IS). Deca plating a half-dead ally
    // is the state that tells them apart.
    CombatState s = make_shapes(7);
    s.monsters[1].hp = 0;
    s.monsters[1].flags |= kMonsterFlagHalfDead;
    set_monster_move(s.monsters[0], r::kDecaMoveSquareOfProtection,
                     MonsterIntent::DEFEND_BUFF);
    deca_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(s.monsters[1].block, 16) << "isDying/isDead: not rejected";
    EXPECT_EQ(monster_power(s, 1, PowerId::PLATED_ARMOR), -1)
        << "isDeadOrEscaped: rejected";
}

TEST(DonuDeca, DecaBeamDazesTheDiscard) {
    CombatState s = make_shapes(7);
    set_monster_move(s.monsters[0], r::kDecaMoveBeam,
                     MonsterIntent::ATTACK_DEBUFF);
    deca_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(s.player_hp, 400 - 24) << "two hits of 12 (A4+ beamDmg)";
    EXPECT_EQ(count_in_pile(s, s.discard, s.discard_count, CardId::DAZED), 2);
}

}  // namespace
}  // namespace sts::engine
