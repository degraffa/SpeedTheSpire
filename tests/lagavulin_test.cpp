// Tier-2 Lagavulin coverage: the sleeping Exordium elite.
//
// Named cases pin the registry columns (HP A8, Strong Attack A3, the A18 -2
// debuff column), the sleeping armour that grows 8 per round and stops the
// moment the shell opens, both wake paths (three idle turns, or the first hit
// that actually moves HP), the post-wake move cadence, the lethal-hit branch of
// changeState's !isDying guard, and the awake `Lagavulin(false)` ctor variant.
//
// No committed RNG fixture accompanies this suite, and that is a property of the
// class rather than an omission: Lagavulin.getMove (Lagavulin.java:212-227)
// never reads the rollMove argument, so there is no aiRng-driven branch for a
// seed sweep to explore. What IS observable is the number of aiRng draws --
// rollMove draws unconditionally (AbstractMonster.java:712-714) and the third
// idle turn is the one path that queues no RollMoveAction -- so the draw counter
// is asserted on every turn instead.
//
// Provenance: Lagavulin.java:54-58,60-66,73-100,102-114,116-174,176-195,
// 197-210,212-227; MetallicizePower.java:19-42; AbstractCreature.java:548-553;
// MonsterGroup.java:98-105,290-304; MonsterStartTurnAction.java:22 (uncalled --
// why monster block never decays); ReducePowerAction.java:35-53;
// SetMoveAction.java:52-56; AbstractMonster.java:431-437,465-491,712-715,765-775;
// MonsterHelper.java:439-441,445-447.

#include <cstdint>
#include <span>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_lagavulin.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr MonsterId kLagavulinId = MonsterId::LAGAVULIN;

constexpr uint8_t kDebuff = sts::registry::kLagavulinMoveDebuff;        // 1
constexpr uint8_t kStrongAtk = sts::registry::kLagavulinMoveStrongAtk;  // 3
constexpr uint8_t kOpen = sts::registry::kLagavulinMoveOpen;            // 4
constexpr uint8_t kIdle = sts::registry::kLagavulinMoveIdle;            // 5

// The A20 resolved columns (kMonsterAscension): A8 HP, A3 attack, A18 debuff.
constexpr int32_t kA20AttackDamage = 20;
constexpr int32_t kA20Debuff = -2;
constexpr int32_t kArmor = 8;

CombatState make_state() {
    CombatState s{};
    s.player_hp = 500;
    s.player_max_hp = 500;
    s.monster_count = 1;
    return s;
}

void drain_actions(CombatState& s) {
    while (s.action_count > 0) {
        ActionQueueItem item{};
        ASSERT_TRUE(pop_action_front(s, item));
        execute_opcode(s, item);
    }
}

// Spawn + preBattlePrep, with the queued pre-battle actions resolved: the state
// a player faces on turn 1.
CombatState spawn_asleep(int64_t seed) {
    CombatState s = make_state();
    s.monster_hp_rng = from_seed(seed);
    s.ai_rng = from_seed(seed);
    spawn_group(s, std::span<const MonsterId>(&kLagavulinId, 1));
    use_pre_battle_actions(s);
    drain_actions(s);
    return s;
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

// One monster turn through the real pump, with everything it queues resolved.
// turn_has_ended stays 0, so the pump never runs the start-of-turn sequence --
// this isolates takeTurn from the end-of-round Metallicize tick.
void run_monster_turn(CombatState& s, uint8_t mi) {
    s.monster_attacks_queued = 1;
    s.monster_queue[0] = MonsterQueueItem{mi, 0};
    s.monster_queue_count = 1;
    s.turn_has_ended = 0;
    for (;;) {
        const PumpStepResult r = pump_step(s, dispatch_monster_turn);
        if (r.outcome == PumpOutcome::WAITING_ON_USER ||
            r.outcome == PumpOutcome::COMBAT_OVER) {
            return;
        }
    }
}

[[nodiscard]] const PowerSlot* find_monster_power(const CombatState& s,
                                                  uint8_t mi, PowerId id) {
    for (uint8_t i = 0; i < s.monsters[mi].power_count; ++i) {
        if (s.monsters[mi].powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.monsters[mi].powers[i];
        }
    }
    return nullptr;
}

[[nodiscard]] int32_t player_power_amount(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return s.player_powers[i].amount;
        }
    }
    return 0;
}

// --- Registry -----------------------------------------------------------------

TEST(LagavulinRegistry, EliteRowAndAscensionColumnsMatchJava) {
    namespace r = sts::registry;

    EXPECT_EQ(r::monster_game_id(r::MonsterId::LAGAVULIN), "Lagavulin");
    EXPECT_EQ(r::monster_from_game_id("Lagavulin"), r::MonsterId::LAGAVULIN);

    // Appended intents (pinned, never renumbered).
    EXPECT_EQ(static_cast<uint8_t>(r::MonsterIntent::SLEEP), 9);
    EXPECT_EQ(static_cast<uint8_t>(r::MonsterIntent::STUN), 10);

    const r::MonsterDef& lag = r::kLagavulin;
    EXPECT_TRUE(lag.ai_native);
    EXPECT_EQ(lag.roll_count, 0) << "the ctor's only draw is setHp itself";
    EXPECT_EQ(lag.enemy_type, r::MonsterEnemyType::ELITE);  // Lagavulin.java:75
    EXPECT_FALSE(lag.is_boss());

    // setHp(109,111) below A8, setHp(112,115) at A8+ (Lagavulin.java:77-81).
    EXPECT_EQ(lag.hp_min(7), 109);
    EXPECT_EQ(lag.hp_max(7), 111);
    EXPECT_EQ(lag.hp_min(8), 112);
    EXPECT_EQ(lag.hp_max(8), 115);
    EXPECT_EQ(lag.hp_min(20), 112);
    EXPECT_EQ(lag.hp_max(20), 115);

    // attackDmg = ascensionLevel >= 3 ? 20 : 18 (Lagavulin.java:82).
    const r::MonsterMove* atk = lag.move(kStrongAtk);
    ASSERT_NE(atk, nullptr);
    EXPECT_EQ(atk->intent, r::MonsterIntent::ATTACK);
    ASSERT_EQ(atk->effect_count, 1);
    EXPECT_EQ(atk->effects[0].op, r::Opcode::DAMAGE);
    EXPECT_EQ(atk->effects[0].target, r::MonsterMoveTarget::PLAYER);
    EXPECT_EQ(atk->effects[0].amount.at(2), 18);
    EXPECT_EQ(atk->effects[0].amount.at(3), 20);
    EXPECT_EQ(atk->effects[0].amount.at(20), kA20AttackDamage);

    // debuff = ascensionLevel >= 18 ? -2 : -1 (Lagavulin.java:83), applied as
    // Dexterity THEN Strength (takeTurn addToBottom order, :123-124).
    const r::MonsterMove* debuff = lag.move(kDebuff);
    ASSERT_NE(debuff, nullptr);
    EXPECT_EQ(debuff->intent, r::MonsterIntent::STRONG_DEBUFF);
    ASSERT_EQ(debuff->effect_count, 2);
    EXPECT_EQ(debuff->effects[0].op, r::Opcode::APPLY_POWER);
    EXPECT_EQ(debuff->effects[0].target, r::MonsterMoveTarget::PLAYER);
    EXPECT_EQ(debuff->effects[0].extra & 0xFFFFu,
              static_cast<uint32_t>(PowerId::DEXTERITY));
    EXPECT_EQ(debuff->effects[1].extra & 0xFFFFu,
              static_cast<uint32_t>(PowerId::STRENGTH));
    for (uint8_t i = 0; i < 2; ++i) {
        EXPECT_EQ(debuff->effects[i].amount.at(17), -1) << "effect " << int{i};
        EXPECT_EQ(debuff->effects[i].amount.at(18), -2) << "effect " << int{i};
        EXPECT_EQ(debuff->effects[i].amount.at(20), kA20Debuff)
            << "effect " << int{i};
    }

    // The two non-acting moves carry the telegraph only.
    const r::MonsterMove* idle = lag.move(kIdle);
    const r::MonsterMove* open = lag.move(kOpen);
    ASSERT_NE(idle, nullptr);
    ASSERT_NE(open, nullptr);
    EXPECT_EQ(idle->intent, r::MonsterIntent::SLEEP);
    EXPECT_EQ(open->intent, r::MonsterIntent::STUN);
    EXPECT_EQ(idle->effects[0].op, r::Opcode::NOP);
    EXPECT_EQ(open->effects[0].op, r::Opcode::NOP);
    // OPEN_NATURAL (6) is deliberately absent: nothing in the build sets it.
    EXPECT_EQ(lag.move(6), nullptr);
    EXPECT_EQ(lag.move_count, 4);

    EXPECT_EQ(monster_init_fn(MonsterId::LAGAVULIN), &lagavulin_init);
    EXPECT_EQ(monster_turn_fn(MonsterId::LAGAVULIN), &lagavulin_take_turn);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::LAGAVULIN),
              &lagavulin_use_pre_battle_action);
    // Not mid-combat spawnable, and it rolls inline rather than queueing rolls.
    EXPECT_EQ(monster_spawn_at_hp_fn(MonsterId::LAGAVULIN), nullptr);
    EXPECT_EQ(monster_roll_move_fn(MonsterId::LAGAVULIN), nullptr);

    // Metallicize joins the oracle on the game's POWER_ID string.
    EXPECT_EQ(sts::registry::power_game_id(PowerId::METALLICIZE), "Metallicize");

    RngStream misc = from_seed(15001);
    ResolvedGroup group{};
    ASSERT_TRUE(resolve_encounter("Lagavulin", misc, group));
    ASSERT_EQ(group.count, 1);
    EXPECT_EQ(group.members[0], "Lagavulin");
}

// --- Spawn + preBattlePrep ----------------------------------------------------

TEST(LagavulinInit, AsleepCtorDrawsHpOnceAndTelegraphsSleep) {
    CombatState s = make_state();
    s.monster_hp_rng = from_seed(15002);
    s.ai_rng = from_seed(15002);

    lagavulin_init(s, 0);

    const MonsterState& m = s.monsters[0];
    EXPECT_EQ(m.monster_id, static_cast<uint16_t>(MonsterId::LAGAVULIN));
    EXPECT_GE(m.hp, 112);
    EXPECT_LE(m.hp, 115);
    EXPECT_EQ(m.hp, m.max_hp);
    EXPECT_EQ(s.monster_hp_rng.counter, 1) << "setHp(min,max) is one draw";
    EXPECT_EQ(s.ai_rng.counter, 1)
        << "init rollMove always draws; Lagavulin.getMove discards the value";
    EXPECT_EQ(m.move_history[0], kIdle);
    EXPECT_EQ(m.intent, static_cast<uint8_t>(MonsterIntent::SLEEP));
    EXPECT_TRUE((m.flags & kMonsterFlagLagavulinAsleep) != 0u);
    EXPECT_FALSE((m.flags & kMonsterFlagLagavulinIsOut) != 0u);
    EXPECT_FALSE((m.flags & kMonsterFlagLagavulinOutTriggered) != 0u);
    EXPECT_EQ(m.power_count, 0) << "Metallicize arrives at preBattlePrep, not ctor";
    EXPECT_EQ(m.block, 0);
}

TEST(LagavulinInit, PreBattleQueuesEightBlockThenMetallicizeAndDrawsNothing) {
    CombatState s = make_state();
    s.monster_hp_rng = from_seed(15003);
    s.ai_rng = from_seed(15003);
    spawn_group(s, std::span<const MonsterId>(&kLagavulinId, 1));
    const int32_t hp_draws = s.monster_hp_rng.counter;
    const int32_t ai_draws = s.ai_rng.counter;

    use_pre_battle_actions(s);

    // GainBlockAction then ApplyPowerAction, in that addToBottom order.
    ASSERT_EQ(s.action_count, 2);
    EXPECT_EQ(s.action_queue[s.action_head].opcode,
              static_cast<uint16_t>(Opcode::BLOCK));
    EXPECT_NE(s.action_queue[s.action_head].flags & kBlockNoPowers, 0u)
        << "a direct GainBlockAction runs no applyPowers pass";
    EXPECT_EQ(s.monster_hp_rng.counter, hp_draws)
        << "Lagavulin's usePreBattleAction rolls nothing (unlike the Louse)";
    EXPECT_EQ(s.ai_rng.counter, ai_draws);

    drain_actions(s);
    EXPECT_EQ(s.monsters[0].block, kArmor);
    const PowerSlot* met = find_monster_power(s, 0, PowerId::METALLICIZE);
    ASSERT_NE(met, nullptr);
    EXPECT_EQ(met->amount, kArmor);
}

// --- Sleeping: block each round, wake on the third idle ------------------------

TEST(LagavulinSleep, ArmourGrowsEightEachRoundAndStopsWhenTheShellOpens) {
    CombatState s = spawn_asleep(15004);
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    s.turn = 1;
    s.monster_attacks_queued = 1;
    ASSERT_EQ(s.monsters[0].block, kArmor);  // 8 on the player's first turn

    // A real draw pile so the scripted END_TURNs have cards to draw.
    const CardDef* strike = card_def(CardId::STRIKE);
    ASSERT_NE(strike, nullptr);
    for (uint8_t i = 0; i < 30; ++i) {
        s.card_pool[i].card_id = static_cast<uint16_t>(CardId::STRIKE);
        s.card_pool[i].cost_now = card_cost(*strike, 0);
        s.card_pool[i].flags = card_flags(*strike, 0);
        s.draw[i] = i;
    }
    s.draw_count = 30;

    Action action[1] = {make_action(ActionVerb::END_TURN)};
    StepResult result[1]{};
    auto end_turn = [&]() {
        advance(std::span<CombatState>(&s, 1),
                std::span<const Action>(action, 1),
                std::span<StepResult>(result, 1));
        EXPECT_FALSE(result[0].terminal);
    };

    int32_t ai_draws = s.ai_rng.counter;

    end_turn();  // idle 1: re-telegraph SLEEP, roll, then Metallicize ticks
    EXPECT_EQ(s.monsters[0].move_history[0], kIdle);
    EXPECT_EQ(s.monsters[0].intent, static_cast<uint8_t>(MonsterIntent::SLEEP));
    EXPECT_EQ(s.monsters[0].block, 2 * kArmor);
    EXPECT_EQ(s.ai_rng.counter, ai_draws + 1);
    EXPECT_EQ(s.player_hp, 500) << "a sleeping Lagavulin deals no damage";
    ai_draws = s.ai_rng.counter;

    end_turn();  // idle 2
    EXPECT_EQ(s.monsters[0].move_history[0], kIdle);
    EXPECT_EQ(s.monsters[0].block, 3 * kArmor);
    EXPECT_EQ(s.ai_rng.counter, ai_draws + 1);
    ai_draws = s.ai_rng.counter;

    end_turn();  // idle 3: open, shed Metallicize, SetMoveAction(STRONG_ATK)
    EXPECT_EQ(s.monsters[0].move_history[0], kStrongAtk);
    EXPECT_EQ(s.monsters[0].intent, static_cast<uint8_t>(MonsterIntent::ATTACK));
    EXPECT_TRUE((s.monsters[0].flags & kMonsterFlagLagavulinIsOut) != 0u);
    EXPECT_TRUE((s.monsters[0].flags & kMonsterFlagLagavulinOutTriggered) != 0u);
    EXPECT_EQ(find_monster_power(s, 0, PowerId::METALLICIZE), nullptr)
        << "ReducePowerAction sheds all 8 stacks (Lagavulin.java:188)";
    EXPECT_EQ(s.monsters[0].block, 3 * kArmor)
        << "the armour it already has stays -- monster block never decays";
    EXPECT_EQ(s.ai_rng.counter, ai_draws)
        << "the third idle queues no RollMoveAction (no inner case 3)";
    EXPECT_EQ(s.player_hp, 500);

    end_turn();  // turn 4: the first Strong Attack, A3 column
    EXPECT_EQ(s.player_hp, 500 - kA20AttackDamage);
    EXPECT_EQ(s.monsters[0].block, 3 * kArmor) << "Metallicize is gone";
}

// --- Waking on damage ---------------------------------------------------------

TEST(LagavulinWake, OnlyRealHpLossWakesItAndOnlyOnce) {
    CombatState s = spawn_asleep(15005);
    const int16_t full_hp = s.monsters[0].hp;

    // A hit the sleeping armour absorbs entirely moves no HP: currentHealth ==
    // previousHealth, so the wake branch is not taken (Lagavulin.java:201).
    execute_opcode(s, damage_item(0, kArmor));
    EXPECT_EQ(s.monsters[0].hp, full_hp);
    EXPECT_EQ(s.monsters[0].block, 0);
    EXPECT_EQ(s.monsters[0].move_history[0], kIdle);
    EXPECT_FALSE((s.monsters[0].flags & kMonsterFlagLagavulinOutTriggered) != 0u);
    EXPECT_EQ(s.action_count, 0);
    ASSERT_NE(find_monster_power(s, 0, PowerId::METALLICIZE), nullptr);

    // The next hit reaches HP -> setMove(OPEN, STUN), latch, ChangeState(OPEN).
    execute_opcode(s, damage_item(0, 6));
    EXPECT_EQ(s.monsters[0].hp, full_hp - 6);
    EXPECT_EQ(s.monsters[0].move_history[0], kOpen);
    EXPECT_EQ(s.monsters[0].intent, static_cast<uint8_t>(MonsterIntent::STUN));
    EXPECT_TRUE((s.monsters[0].flags & kMonsterFlagLagavulinOutTriggered) != 0u);
    EXPECT_TRUE((s.monsters[0].flags & kMonsterFlagLagavulinIsOut) != 0u);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(s.action_queue[s.action_head].opcode,
              static_cast<uint16_t>(Opcode::REDUCE_POWER));
    drain_actions(s);
    EXPECT_EQ(find_monster_power(s, 0, PowerId::METALLICIZE), nullptr);

    // isOutTriggered makes it one-shot: further hits change no move.
    execute_opcode(s, damage_item(0, 6));
    EXPECT_EQ(s.monsters[0].move_history[0], kOpen);
    EXPECT_EQ(s.action_count, 0);

    // The stunned turn spends itself on the STUNNED text, then rolls an attack.
    const int32_t ai_draws = s.ai_rng.counter;
    run_monster_turn(s, 0);
    EXPECT_EQ(s.player_hp, 500) << "the stun turn deals no damage";
    EXPECT_EQ(s.monsters[0].move_history[0], kStrongAtk);
    EXPECT_EQ(s.ai_rng.counter, ai_draws + 1);

    run_monster_turn(s, 0);
    EXPECT_EQ(s.player_hp, 500 - kA20AttackDamage);
}

TEST(LagavulinWake, ALethalHitLatchesButNeverOpensTheShell) {
    CombatState s = spawn_asleep(15006);
    s.monsters[0].block = 0;
    s.monsters[0].hp = 5;

    execute_opcode(s, damage_item(0, 99));

    EXPECT_EQ(s.monsters[0].hp, 0);
    EXPECT_TRUE((s.monsters[0].flags & kMonsterFlagLagavulinOutTriggered) != 0u)
        << "damage() latches before changeState is reached";
    EXPECT_FALSE((s.monsters[0].flags & kMonsterFlagLagavulinIsOut) != 0u)
        << "changeState's !isDying guard (Lagavulin.java:184)";
    EXPECT_EQ(s.action_count, 0) << "no ReducePowerAction on a dying elite";
    EXPECT_NE(find_monster_power(s, 0, PowerId::METALLICIZE), nullptr);
}

// --- Post-wake cadence --------------------------------------------------------

TEST(LagavulinCadence, TwoStrongAttacksThenSiphonSoulForever) {
    CombatState s = spawn_asleep(15007);
    // Wake it the fast way: three idle turns.
    for (int i = 0; i < 3; ++i) {
        run_monster_turn(s, 0);
    }
    ASSERT_EQ(s.monsters[0].move_history[0], kStrongAtk);

    // getMove: debuffTurnCount < 2 -> STRONG_ATK; at 2 -> DEBUFF, which resets
    // the counter (Lagavulin.java:120,129,213-223).
    const uint8_t expected[9] = {kStrongAtk, kStrongAtk, kDebuff,
                                 kStrongAtk, kStrongAtk, kDebuff,
                                 kStrongAtk, kStrongAtk, kDebuff};
    int32_t hp = s.player_hp;
    int32_t debuff_turns = 0;
    for (int i = 0; i < 9; ++i) {
        const uint8_t acting = s.monsters[0].move_history[0];
        EXPECT_EQ(acting, expected[i]) << "turn " << i;
        const int32_t ai_draws = s.ai_rng.counter;
        run_monster_turn(s, 0);
        EXPECT_EQ(s.ai_rng.counter, ai_draws + 1)
            << "every post-wake turn queues a RollMoveAction, turn " << i;
        if (acting == kStrongAtk) {
            hp -= kA20AttackDamage;
        } else {
            ++debuff_turns;
        }
        EXPECT_EQ(s.player_hp, hp) << "turn " << i;
        // A18 column: -2 Dexterity AND -2 Strength per Siphon Soul.
        EXPECT_EQ(player_power_amount(s, PowerId::DEXTERITY),
                  kA20Debuff * debuff_turns);
        EXPECT_EQ(player_power_amount(s, PowerId::STRENGTH),
                  kA20Debuff * debuff_turns);
    }
    EXPECT_EQ(debuff_turns, 3);
}

// --- The `new Lagavulin(false)` variant ---------------------------------------

TEST(LagavulinAwakeVariant, EventCtorSkipsSleepAndPushesTwoMoveHistoryEntries) {
    CombatState s = make_state();
    s.monster_hp_rng = from_seed(15008);
    s.ai_rng = from_seed(15008);

    lagavulin_init_awake(s, 0);

    const MonsterState& m = s.monsters[0];
    EXPECT_EQ(s.monster_hp_rng.counter, 1) << "same setHp draw as the elite";
    EXPECT_EQ(s.ai_rng.counter, 1);
    EXPECT_FALSE((m.flags & kMonsterFlagLagavulinAsleep) != 0u);
    EXPECT_TRUE((m.flags & kMonsterFlagLagavulinIsOut) != 0u);
    EXPECT_TRUE((m.flags & kMonsterFlagLagavulinOutTriggered) != 0u);
    // isOut at init time -> getMove takes the awake branch immediately.
    EXPECT_EQ(m.move_history[0], kStrongAtk);

    lagavulin_use_pre_battle_action(s, 0);
    EXPECT_EQ(s.action_count, 0) << "no armour and no Metallicize when awake";
    EXPECT_EQ(m.power_count, 0);
    EXPECT_EQ(m.block, 0);
    // setMove pushes history, so the init telegraph is still visible behind it
    // (AbstractMonster.java:431-437).
    EXPECT_EQ(m.move_history[0], kDebuff);
    EXPECT_EQ(m.move_history[1], kStrongAtk);
    EXPECT_EQ(m.intent, static_cast<uint8_t>(MonsterIntent::STRONG_DEBUFF));

    // It opens on Siphon Soul and never idles: the damage wake is already
    // latched, so a hit changes no move.
    run_monster_turn(s, 0);
    EXPECT_EQ(player_power_amount(s, PowerId::DEXTERITY), kA20Debuff);
    EXPECT_EQ(player_power_amount(s, PowerId::STRENGTH), kA20Debuff);
    EXPECT_EQ(s.monsters[0].move_history[0], kStrongAtk);
    execute_opcode(s, damage_item(0, 4));
    EXPECT_EQ(s.monsters[0].move_history[0], kStrongAtk);
    EXPECT_EQ(s.action_count, 0);
}

}  // namespace
}  // namespace sts::engine
