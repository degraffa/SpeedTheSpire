// B3.7 Red uncommon Power cards. Every expectation is hand-derived from the
// corresponding decompiled card/power Java source cited beside the registry row.
#include <array>
#include <cstdint>
#include <span>

#include "gtest/gtest.h"

#include "sts/engine/advance.hpp"
#include "sts/engine/action_queue.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/piles.hpp"
#include "sts/engine/power_hooks.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

PowerSlot* FindPower(CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.player_powers[i];
        }
    }
    return nullptr;
}

CombatState MakeCombat(int16_t energy = 3, int16_t monster_hp = 80) {
    CombatState s{};
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.player_energy = energy;
    s.monster_count = 1;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = monster_hp;
    s.monsters[0].max_hp = monster_hp;
    s.monster_attacks_queued = 1;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    return s;
}

CardPoolIndex AddHand(CombatState& s, CardId id, uint8_t upgrade = 0) {
    const CardDef* def = card_def(id);
    EXPECT_NE(def, nullptr);
    CardPoolIndex pi = 0;
    while (pi < kCardPoolCap &&
           s.card_pool[pi].card_id != static_cast<uint16_t>(CardId::NONE)) {
        ++pi;
    }
    EXPECT_LT(pi, kCardPoolCap);
    s.card_pool[pi].card_id = static_cast<uint16_t>(id);
    s.card_pool[pi].upgrade = upgrade;
    s.card_pool[pi].cost_now = card_cost(*def, upgrade);
    s.card_pool[pi].flags = card_flags(*def, upgrade);
    s.hand[s.hand_count++] = pi;
    return pi;
}

void Step(CombatState& s, Action a) {
    StepResult out{};
    advance(std::span<CombatState>(&s, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&out, 1));
}
void Play(CombatState& s, uint8_t hand_slot = 0) {
    Step(s, make_action(ActionVerb::PLAY_CARD, hand_slot, 0));
}
void DrainActions(CombatState& s) {
    ActionQueueItem it{};
    while (pop_action_front(s, it)) {
        execute_opcode(s, it);
    }
}

TEST(CardUncommonPowersRegistry, ExactPowerRosterAndRows) {
    struct Row { CardId card; PowerId power; uint8_t base_cost; int16_t base; int16_t upgraded; };
    constexpr std::array<Row, 8> rows{{
        {CardId::COMBUST, PowerId::COMBUST, 1, 5, 7},
        {CardId::DARK_EMBRACE, PowerId::DARK_EMBRACE, 2, 1, 1},
        {CardId::EVOLVE, PowerId::EVOLVE, 1, 1, 2},
        {CardId::FEEL_NO_PAIN, PowerId::FEEL_NO_PAIN, 1, 3, 4},
        {CardId::FIRE_BREATHING, PowerId::FIRE_BREATHING, 1, 6, 10},
        {CardId::INFLAME, PowerId::STRENGTH, 1, 2, 3},
        {CardId::METALLICIZE, PowerId::METALLICIZE, 1, 3, 4},
        {CardId::RUPTURE, PowerId::RUPTURE, 1, 1, 2},
    }};
    EXPECT_EQ(static_cast<uint16_t>(CardId::COMBUST), 68);
    EXPECT_EQ(static_cast<uint16_t>(CardId::RUPTURE), 75);
    EXPECT_EQ(static_cast<uint16_t>(PowerId::EVOLVE), 26);
    EXPECT_EQ(static_cast<uint16_t>(PowerId::FIRE_BREATHING), 27);
    for (const Row& row : rows) {
        const CardDef* def = card_def(row.card);
        ASSERT_NE(def, nullptr);
        EXPECT_EQ(def->type, CardType::POWER);
        EXPECT_EQ(card_cost(*def, 0), row.base_cost);
        EXPECT_EQ(card_effect_steps(*def, 0).steps[0].amount, row.base);
        EXPECT_EQ(card_effect_steps(*def, 1).steps[0].amount, row.upgraded);
    }
    const CardDef* dark = card_def(CardId::DARK_EMBRACE);
    ASSERT_NE(dark, nullptr);
    EXPECT_EQ(card_cost(*dark, 1), 1);
    EXPECT_EQ(card_def(CardId::RAGE)->type, CardType::SKILL)
        << "Rage is source-enumerated as B3.6's Skill, not a Power card";
}

TEST(CardUncommonPowersPlayPath, PowerBecomesPowerAndNeverDiscards) {
    CombatState s = MakeCombat();
    const CardPoolIndex pi = AddHand(s, CardId::INFLAME, 1);
    Play(s);
    ASSERT_NE(FindPower(s, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(FindPower(s, PowerId::STRENGTH)->amount, 3);
    EXPECT_EQ(s.hand_count, 0);
    EXPECT_EQ(s.discard_count, 0);
    EXPECT_EQ(s.exhaust_count, 0);
    EXPECT_EQ(s.card_pool[pi].card_id, static_cast<uint16_t>(CardId::INFLAME));
    EXPECT_EQ(s.player_energy, 2);
}

TEST(CardUncommonPowersCombust, StacksDamageAndHpLossPerApplication) {
    CombatState s = MakeCombat(/*energy=*/3, /*monster_hp=*/50);
    AddHand(s, CardId::COMBUST);
    AddHand(s, CardId::COMBUST, 1);
    Play(s, 0);
    Play(s, 0);
    ASSERT_NE(FindPower(s, PowerId::COMBUST), nullptr);
    EXPECT_EQ(FindPower(s, PowerId::COMBUST)->amount, 12);
    dispatch_at_end_of_turn(s);
    ASSERT_EQ(s.action_count, 2);
    DrainActions(s);
    EXPECT_EQ(s.player_hp, 78) << "CombustPower.stackPower increments hpLoss";
    EXPECT_EQ(s.monsters[0].hp, 38);
}

TEST(CardUncommonPowersDarkEmbrace, DrawsWhenAnotherCardExhausts) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::DARK_EMBRACE);
    Play(s);
    const CardPoolIndex exhausted = AddHand(s, CardId::STRIKE);
    const CardPoolIndex draw = 1;
    s.card_pool[draw].card_id = static_cast<uint16_t>(CardId::DEFEND);
    s.card_pool[draw].cost_now = 1;
    s.draw[s.draw_count++] = draw;
    exhaust_card(s, exhausted);
    DrainActions(s);
    EXPECT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.card_pool[s.hand[0]].card_id, static_cast<uint16_t>(CardId::DEFEND));
}

TEST(CardUncommonPowersEvolve, StatusDrawChainsExtraDrawAndUpgradeScales) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::EVOLVE, up);
        Play(s);
        s.card_pool[1].card_id = static_cast<uint16_t>(CardId::WOUND);
        s.card_pool[1].cost_now = 0;
        s.card_pool[2].card_id = static_cast<uint16_t>(CardId::STRIKE);
        s.card_pool[2].cost_now = 1;
        s.draw[0] = 2;
        s.draw[1] = 1;  // Wound first, then Strike.
        s.draw_count = 2;
        ActionQueueItem draw{};
        draw.opcode = static_cast<uint16_t>(Opcode::DRAW);
        draw.src = kActorPlayer;
        draw.tgt = kActorPlayer;
        draw.amount = 1;
        execute_opcode(s, draw);
        DrainActions(s);
        EXPECT_EQ(s.hand_count, 2);
        EXPECT_EQ(s.card_pool[s.hand[0]].card_id, static_cast<uint16_t>(CardId::WOUND));
        EXPECT_EQ(s.card_pool[s.hand[1]].card_id, static_cast<uint16_t>(CardId::STRIKE));
    }
}

TEST(CardUncommonPowersFeelNoPainDarkEmbrace, SameExhaustUsesPowerListOrder) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::FEEL_NO_PAIN);
    AddHand(s, CardId::DARK_EMBRACE);
    Play(s, 0);
    Play(s, 0);
    const CardPoolIndex victim = AddHand(s, CardId::STRIKE);
    const CardPoolIndex draw = 3;
    s.card_pool[draw].card_id = static_cast<uint16_t>(CardId::DEFEND);
    s.card_pool[draw].cost_now = 1;
    s.draw[s.draw_count++] = draw;
    exhaust_card(s, victim);
    ASSERT_EQ(s.action_count, 2);
    const ActionQueueItem& first = s.action_queue[s.action_head];
    EXPECT_EQ(static_cast<Opcode>(first.opcode), Opcode::BLOCK);
    DrainActions(s);
    EXPECT_EQ(s.player_block, 3);
    EXPECT_EQ(s.hand_count, 1);
}

TEST(CardUncommonPowersFireBreathing, StatusAndCurseDrawDealThornsDamage) {
    for (CardId drawn : {CardId::WOUND, CardId::CLUMSY}) {
        CombatState s = MakeCombat(/*energy=*/3, /*monster_hp=*/30);
        AddHand(s, CardId::FIRE_BREATHING, 1);
        Play(s);
        s.card_pool[1].card_id = static_cast<uint16_t>(drawn);
        s.card_pool[1].cost_now = 0;
        s.draw[s.draw_count++] = 1;
        ActionQueueItem draw{};
        draw.opcode = static_cast<uint16_t>(Opcode::DRAW);
        draw.src = kActorPlayer;
        draw.tgt = kActorPlayer;
        draw.amount = 1;
        execute_opcode(s, draw);
        DrainActions(s);
        EXPECT_EQ(s.monsters[0].hp, 20);
    }
}

TEST(CardUncommonPowersMetallicize, GainsBaseAndUpgradedBlockAtTurnEnd) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::METALLICIZE, up);
        Play(s);
        dispatch_at_end_of_turn_pre_card(s);
        DrainActions(s);
        EXPECT_EQ(s.player_block, up == 0 ? 3 : 4);
    }
}

TEST(CardUncommonPowersRupture, SelfHpLossGrantsUpgradedStrength) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::RUPTURE, 1);
    Play(s);
    ActionQueueItem loss{};
    loss.opcode = static_cast<uint16_t>(Opcode::LOSE_HP);
    loss.src = kActorPlayer;
    loss.tgt = kActorPlayer;
    loss.amount = 3;
    execute_opcode(s, loss);
    DrainActions(s);
    ASSERT_NE(FindPower(s, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(FindPower(s, PowerId::STRENGTH)->amount, 2);
}

TEST(CardUncommonPowersDeathGuards, DarkEmbraceAndCombustDoNothingAfterCombat) {
    CombatState dark = MakeCombat();
    AddHand(dark, CardId::DARK_EMBRACE);
    Play(dark);
    dark.monsters[0].hp = 0;
    const CardPoolIndex exhausted = AddHand(dark, CardId::STRIKE);
    exhaust_card(dark, exhausted);
    EXPECT_EQ(dark.action_count, 0) << "DarkEmbracePower checks monstersBasicallyDead";

    CombatState combust = MakeCombat();
    AddHand(combust, CardId::COMBUST);
    Play(combust);
    combust.monsters[0].hp = 0;
    dispatch_at_end_of_turn(combust);
    EXPECT_EQ(combust.action_count, 0) << "CombustPower checks monstersBasicallyDead";
}

TEST(CardUncommonPowersEvolve, NoDrawSuppressesStatusResponse) {
    CombatState s = MakeCombat();
    s.player_powers[0] = PowerSlot{static_cast<uint16_t>(PowerId::EVOLVE), 2};
    s.player_powers[1] = PowerSlot{static_cast<uint16_t>(PowerId::NO_DRAW), -1};
    s.player_power_count = 2;
    s.card_pool[0].card_id = static_cast<uint16_t>(CardId::WOUND);
    dispatch_on_card_draw(s, 0, s.card_pool[0].card_id);
    EXPECT_EQ(s.action_count, 0) << "EvolvePower.hasPower(No Draw) guard";
}
TEST(CardUncommonPowersDirected, InflameFeelNoPainSecondWindScript) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::INFLAME);
    AddHand(s, CardId::FEEL_NO_PAIN);
    AddHand(s, CardId::SECOND_WIND);
    AddHand(s, CardId::DAZED);
    AddHand(s, CardId::STRIKE);
    Play(s, 0);
    Play(s, 0);
    Play(s, 0);
    ASSERT_NE(FindPower(s, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(FindPower(s, PowerId::STRENGTH)->amount, 2);
    EXPECT_EQ(s.player_block, 8) << "Feel No Pain 3 plus Second Wind 5";
    EXPECT_EQ(s.player_energy, 0);
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.card_pool[s.hand[0]].card_id, static_cast<uint16_t>(CardId::STRIKE));
}

}  // namespace
}  // namespace sts::engine