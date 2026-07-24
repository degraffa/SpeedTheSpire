// Tier-2 Slime Boss coverage (B3.20).
//
// Named cases pin the A4/A9/A19 registry columns, forced-first-move RNG,
// exact split threshold, current-HP large children, the complete B3.17
// large->medium child chain and terminal gate, plus a public advance() script
// through Goop Spray / Preparing / Slam.
//
// Provenance: SlimeBoss.java:84-107,120-160,172-191;
// MakeTempCardInDiscardAction.java:24-31,41-50; SpawnMonsterAction.java:28-59;
// SuicideAction.java:21-36; CannotLoseAction.java / CanLoseAction.java:12-15;
// AbstractMonster.java:139,150,431-437,465-467,712-715,869,925-951. Large
// descendant behavior is the B3.17 Java corpus cited by monster_slime_large.hpp.

#include <cstdint>
#include <span>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_slime.hpp"
#include "sts/engine/monster_slime_boss.hpp"
#include "sts/engine/monster_slime_large.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kSlam = 1;
constexpr uint8_t kPrepSlam = 2;
constexpr uint8_t kSplit = 3;
constexpr uint8_t kSticky = 4;

CombatState make_boss_state(int64_t seed) {
    CombatState s{};
    s.player_hp = 500;
    s.player_max_hp = 500;
    s.monster_count = 1;
    s.monster_hp_rng = from_seed(seed);
    s.ai_rng = from_seed(seed);
    slime_boss_init(s, 0);
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

// Run exactly one selected monster through the real pump. Returns true if the
// pump ever reports COMBAT_OVER while the queued turn/split drains.
bool pump_selected_monster(CombatState& s, uint8_t monster_index) {
    s.monster_attacks_queued = 1;
    s.monster_queue[0] = MonsterQueueItem{monster_index, 0};
    s.monster_queue_count = 1;
    s.turn_has_ended = 0;
    for (;;) {
        const PumpStepResult r = pump_step(s, dispatch_monster_turn);
        if (r.outcome == PumpOutcome::COMBAT_OVER) {
            return true;
        }
        if (r.outcome == PumpOutcome::WAITING_ON_USER) {
            return false;
        }
    }
}

int count_card_in_discard(const CombatState& s, CardId id) {
    int count = 0;
    for (uint8_t i = 0; i < s.discard_count; ++i) {
        if (s.card_pool[s.discard[i]].card_id == static_cast<uint16_t>(id)) {
            ++count;
        }
    }
    return count;
}

TEST(SlimeBossRegistry, NativeEntryAndA4A9A19ColumnsMatchJava) {
    namespace r = sts::registry;

    EXPECT_EQ(static_cast<int>(r::MonsterId::SLIME_BOSS), 11)
        << "append-only after Acid Slime L=10";
    EXPECT_EQ(static_cast<uint8_t>(r::MonsterIntent::STRONG_DEBUFF), 8)
        << "append-only after UNKNOWN=7";
    EXPECT_EQ(r::monster_game_id(r::MonsterId::SLIME_BOSS), "SlimeBoss");
    EXPECT_EQ(r::monster_from_game_id("SlimeBoss"),
              r::MonsterId::SLIME_BOSS);

    const r::MonsterDef& boss = r::kSlimeBoss;
    EXPECT_TRUE(boss.ai_native);
    EXPECT_EQ(boss.roll_count, 0);
    EXPECT_EQ(boss.hp_min(8), 140);
    EXPECT_EQ(boss.hp_max(8), 140);
    EXPECT_EQ(boss.hp_min(9), 150);
    EXPECT_EQ(boss.hp_max(20), 150);

    const r::MonsterMove* slam = boss.move(r::kSlimeBossMoveSlam);
    ASSERT_NE(slam, nullptr);
    ASSERT_EQ(slam->effect_count, 1);
    EXPECT_EQ(slam->intent, r::MonsterIntent::ATTACK);
    EXPECT_EQ(slam->effects[0].op, r::Opcode::DAMAGE);
    EXPECT_EQ(slam->effects[0].amount.at(3), 35);
    EXPECT_EQ(slam->effects[0].amount.at(4), 38);

    const r::MonsterMove* sticky = boss.move(r::kSlimeBossMoveSticky);
    ASSERT_NE(sticky, nullptr);
    ASSERT_EQ(sticky->effect_count, 1);
    EXPECT_EQ(sticky->intent, r::MonsterIntent::STRONG_DEBUFF);
    EXPECT_EQ(sticky->effects[0].op, r::Opcode::MAKE_CARD);
    EXPECT_EQ(sticky->effects[0].amount.at(18), 3);
    EXPECT_EQ(sticky->effects[0].amount.at(19), 5);
    EXPECT_EQ(sticky->effects[0].extra & 0xFFFFu,
              static_cast<uint32_t>(r::CardId::SLIMED));
    EXPECT_EQ((sticky->effects[0].extra >> 16) & 0xFFu,
              static_cast<uint32_t>(CardPile::DISCARD));

    const r::MonsterMove* prep = boss.move(r::kSlimeBossMovePrepSlam);
    const r::MonsterMove* split = boss.move(r::kSlimeBossMoveSplit);
    ASSERT_NE(prep, nullptr);
    ASSERT_NE(split, nullptr);
    EXPECT_EQ(prep->intent, r::MonsterIntent::UNKNOWN);
    EXPECT_EQ(split->intent, r::MonsterIntent::UNKNOWN);
    EXPECT_EQ(prep->effects[0].op, r::Opcode::NOP);
    EXPECT_EQ(split->effects[0].op, r::Opcode::NOP);

    EXPECT_EQ(monster_init_fn(MonsterId::SLIME_BOSS), &slime_boss_init);
    EXPECT_EQ(monster_turn_fn(MonsterId::SLIME_BOSS),
              &slime_boss_take_turn);

    RngStream misc = from_seed(12001);
    ResolvedGroup group{};
    ASSERT_TRUE(resolve_encounter("Slime Boss", misc, group));
    ASSERT_EQ(group.count, 1);
    EXPECT_EQ(group.members[0], "SlimeBoss");
}

TEST(SlimeBossAI, InitUsesFixedA20HpAndOneIgnoredAiRoll) {
    CombatState s{};
    s.monster_count = 1;
    s.monster_hp_rng = from_seed(12002);
    s.ai_rng = from_seed(12002);
    const RngStream hp_before = s.monster_hp_rng;

    slime_boss_init(s, 0);

    const MonsterState& boss = s.monsters[0];
    EXPECT_EQ(boss.monster_id,
              static_cast<uint16_t>(MonsterId::SLIME_BOSS));
    EXPECT_EQ(boss.hp, 150);
    EXPECT_EQ(boss.max_hp, 150);
    EXPECT_EQ(s.monster_hp_rng.counter, hp_before.counter)
        << "setHp(int) is fixed: no monsterHpRng draw";
    EXPECT_EQ(s.monster_hp_rng.s0, hp_before.s0);
    EXPECT_EQ(s.monster_hp_rng.s1, hp_before.s1);
    EXPECT_EQ(s.ai_rng.counter, 1)
        << "init rollMove consumes random(99), then getMove ignores num";
    EXPECT_EQ(boss.move_history[0], kSticky);
    EXPECT_EQ(boss.intent,
              static_cast<uint8_t>(MonsterIntent::STRONG_DEBUFF));
    ASSERT_EQ(boss.power_count, 1);
    EXPECT_EQ(boss.powers[0].power_id,
              static_cast<uint16_t>(PowerId::SPLIT));
    EXPECT_EQ(boss.powers[0].amount, -1);
}

TEST(SlimeBossSplit, ExactHalfThresholdAndLethalHitBehavior) {
    CombatState s = make_boss_state(12003);
    MonsterState& boss = s.monsters[0];
    boss.max_hp = 150;
    boss.hp = 77;

    execute_opcode(s, damage_item(0, 1));  // 76 > 75: no split
    EXPECT_EQ(boss.hp, 76);
    EXPECT_EQ(boss.move_history[0], kSticky);
    EXPECT_EQ(s.action_count, 0);

    execute_opcode(s, damage_item(0, 1));  // 75 == 150/2: split
    EXPECT_EQ(boss.hp, 75);
    EXPECT_EQ(boss.move_history[0], kSplit);
    EXPECT_EQ(boss.intent, static_cast<uint8_t>(MonsterIntent::UNKNOWN));
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(s.action_queue[s.action_head].opcode,
              static_cast<uint16_t>(Opcode::SET_MOVE));

    // No splitTriggered field on SlimeBoss; nextMove==SPLIT is the repeat gate.
    execute_opcode(s, damage_item(0, 1));
    EXPECT_EQ(s.action_count, 1);
    drain_actions(s);
    EXPECT_EQ(boss.move_history[0], kSplit);
    EXPECT_EQ(boss.move_history[1], kSplit)
        << "queued SetMoveAction re-pushes the interrupted move";

    CombatState lethal = make_boss_state(12004);
    lethal.monsters[0].hp = 5;
    execute_opcode(lethal, damage_item(0, 99));
    EXPECT_EQ(lethal.monsters[0].hp, 0);
    EXPECT_EQ(lethal.action_count, 0)
        << "!isDying prevents a lethal hit from telegraphing split";
}

TEST(SlimeBossSplit, CurrentHpLargeChildrenAndCannotLoseWindow) {
    CombatState s = make_boss_state(12005);
    MonsterState& boss = s.monsters[0];
    boss.hp = 61;
    boss.block = 7;
    set_monster_move(boss, kSplit, MonsterIntent::UNKNOWN);
    const int32_t hp_draws = s.monster_hp_rng.counter;
    const int32_t ai_draws = s.ai_rng.counter;

    EXPECT_FALSE(pump_selected_monster(s, 0))
        << "CannotLose suppresses victory between suicide and spawns";

    ASSERT_EQ(s.monster_count, 3);
    EXPECT_EQ(s.monsters[0].monster_id,
              static_cast<uint16_t>(MonsterId::SPIKE_SLIME_LARGE));
    EXPECT_EQ(s.monsters[1].monster_id,
              static_cast<uint16_t>(MonsterId::SLIME_BOSS));
    EXPECT_EQ(s.monsters[2].monster_id,
              static_cast<uint16_t>(MonsterId::ACID_SLIME_LARGE));
    EXPECT_EQ(s.monsters[1].hp, 0);
    EXPECT_EQ(s.monsters[1].block, 7)
        << "SuicideAction bypasses damage and leaves block untouched";
    for (uint8_t child : {uint8_t{0}, uint8_t{2}}) {
        EXPECT_EQ(s.monsters[child].hp, 61);
        EXPECT_EQ(s.monsters[child].max_hp, 61);
        ASSERT_EQ(s.monsters[child].power_count, 1);
        EXPECT_EQ(s.monsters[child].powers[0].power_id,
                  static_cast<uint16_t>(PowerId::SPLIT));
    }
    EXPECT_EQ(s.monster_hp_rng.counter, hp_draws)
        << "4-arg large constructors consume no HP roll";
    EXPECT_EQ(s.ai_rng.counter, ai_draws + 2)
        << "Spike L then Acid L init at spawn resolution";
    EXPECT_EQ(s.flags & kCombatFlagCannotLose, 0u);
}

TEST(SlimeBossSplit, DescendantChainEndsOnlyAfterAllFourMediumsDie) {
    CombatState s = make_boss_state(12006);
    s.monsters[0].hp = 40;
    set_monster_move(s.monsters[0], kSplit, MonsterIntent::UNKNOWN);
    ASSERT_FALSE(pump_selected_monster(s, 0));

    // Spike L is left of the dead boss. At half HP it chains through B3.17.
    execute_opcode(s, damage_item(0, 20));
    drain_actions(s);
    ASSERT_FALSE(pump_selected_monster(s, 0));
    ASSERT_EQ(s.monster_count, 5);
    EXPECT_EQ(s.monsters[0].monster_id,
              static_cast<uint16_t>(MonsterId::SPIKE_SLIME_MEDIUM));
    EXPECT_EQ(s.monsters[1].monster_id,
              static_cast<uint16_t>(MonsterId::SPIKE_SLIME_LARGE));
    EXPECT_EQ(s.monsters[2].monster_id,
              static_cast<uint16_t>(MonsterId::SPIKE_SLIME_MEDIUM));
    EXPECT_EQ(s.monsters[3].monster_id,
              static_cast<uint16_t>(MonsterId::SLIME_BOSS));
    EXPECT_EQ(s.monsters[4].monster_id,
              static_cast<uint16_t>(MonsterId::ACID_SLIME_LARGE));

    // Acid-left is at x=-14, so smart positioning inserts it BEFORE the dead
    // boss at x=0; Acid-right x=254 appends. This is the boss-chain case that
    // cannot reuse the standalone Acid L parent-slot derivation.
    execute_opcode(s, damage_item(4, 20));
    drain_actions(s);
    ASSERT_FALSE(pump_selected_monster(s, 4));
    ASSERT_EQ(s.monster_count, kMonsterCap);
    const MonsterId expected[kMonsterCap] = {
        MonsterId::SPIKE_SLIME_MEDIUM, MonsterId::SPIKE_SLIME_LARGE,
        MonsterId::SPIKE_SLIME_MEDIUM, MonsterId::ACID_SLIME_MEDIUM,
        MonsterId::SLIME_BOSS, MonsterId::ACID_SLIME_LARGE,
        MonsterId::ACID_SLIME_MEDIUM};
    for (uint8_t i = 0; i < kMonsterCap; ++i) {
        EXPECT_EQ(s.monsters[i].monster_id,
                  static_cast<uint16_t>(expected[i]))
            << "slot " << static_cast<int>(i);
    }
    for (uint8_t live : {uint8_t{0}, uint8_t{2}, uint8_t{3}, uint8_t{6}}) {
        EXPECT_EQ(s.monsters[live].hp, 20);
        EXPECT_EQ(s.monsters[live].max_hp, 20);
    }
    for (uint8_t dead : {uint8_t{1}, uint8_t{4}, uint8_t{5}}) {
        EXPECT_EQ(s.monsters[dead].hp, 0);
    }

    execute_opcode(s, damage_item(0, 99));
    execute_opcode(s, damage_item(2, 99));
    execute_opcode(s, damage_item(3, 99));
    EXPECT_NE(pump_step(s, dispatch_monster_turn).outcome,
              PumpOutcome::COMBAT_OVER)
        << "one Acid medium descendant remains alive";
    execute_opcode(s, damage_item(6, 99));
    EXPECT_EQ(pump_step(s, dispatch_monster_turn).outcome,
              PumpOutcome::COMBAT_OVER);
}

TEST(SlimeBossDirectedScript, AdvanceCyclesGoopPrepSlamWithA20Values) {
    CombatState s = make_boss_state(12007);
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    s.turn = 1;
    s.monster_attacks_queued = 1;

    // Keep a real draw pile under the four scripted turns so Goop Spray's
    // statuses stay in discard instead of being reshuffled/drawn immediately.
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
        EXPECT_EQ(s.phase,
                  static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    };

    const int32_t ai_after_init = s.ai_rng.counter;
    end_turn();  // STICKY: +5 Slimed to discard, direct PREP_SLAM
    EXPECT_EQ(s.monsters[0].move_history[0], kPrepSlam);
    EXPECT_EQ(count_card_in_discard(s, CardId::SLIMED), 5);
    EXPECT_EQ(s.player_hp, 500);

    end_turn();  // PREP_SLAM: presentation omitted, direct SLAM
    EXPECT_EQ(s.monsters[0].move_history[0], kSlam);
    EXPECT_EQ(count_card_in_discard(s, CardId::SLIMED), 5);
    EXPECT_EQ(s.player_hp, 500);

    end_turn();  // SLAM: A4+ 38 damage, direct STICKY
    EXPECT_EQ(s.monsters[0].move_history[0], kSticky);
    EXPECT_EQ(s.player_hp, 462);

    end_turn();  // STICKY again
    EXPECT_EQ(s.monsters[0].move_history[0], kPrepSlam);
    EXPECT_EQ(count_card_in_discard(s, CardId::SLIMED), 10);
    EXPECT_EQ(s.ai_rng.counter, ai_after_init)
        << "all post-opener transitions are direct setMove, no more AI rolls";
}

}  // namespace
}  // namespace sts::engine
