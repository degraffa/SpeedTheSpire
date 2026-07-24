// B3.5 Ironclad uncommon attacks: exact 11-card source roster, base/upgraded
// tier-2 behavior, per-instance counters, cost hooks, X-cost, and a directed
// public-advance script. Expected values are hand-computed from the cited Java.

#include <array>
#include <cstdint>
#include <span>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

CombatState MakeCombat(int16_t energy = 6, int16_t monster_hp = 100) {
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

CombatState MakeThree(int16_t energy = 3) {
    CombatState s = MakeCombat(energy, 100);
    s.monster_count = 3;
    for (uint8_t i = 0; i < 3; ++i) {
        s.monsters[i].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
        s.monsters[i].hp = 100;
        s.monsters[i].max_hp = 100;
    }
    return s;
}

CardPoolIndex AddCard(CombatState& s, CardId id, uint8_t upgrade = 0) {
    uint8_t pi = 0;
    while (pi < kCardPoolCap &&
           s.card_pool[pi].card_id != static_cast<uint16_t>(CardId::NONE)) {
        ++pi;
    }
    const CardDef* d = card_def(id);
    EXPECT_NE(d, nullptr);
    s.card_pool[pi].card_id = static_cast<uint16_t>(id);
    s.card_pool[pi].upgrade = upgrade;
    s.card_pool[pi].cost_now = card_cost(*d, upgrade);
    s.card_pool[pi].flags = card_flags(*d, upgrade);
    return pi;
}

CardPoolIndex AddHand(CombatState& s, CardId id, uint8_t upgrade = 0) {
    const CardPoolIndex pi = AddCard(s, id, upgrade);
    s.hand[s.hand_count++] = pi;
    return pi;
}

CardPoolIndex AddDrawTop(CombatState& s, CardId id, uint8_t upgrade = 0) {
    const CardPoolIndex pi = AddCard(s, id, upgrade);
    s.draw[s.draw_count++] = pi;
    return pi;
}

void AddPower(CombatState& s, uint8_t actor, PowerId id, int16_t amount) {
    PowerSlot* slots = actor == kActorPlayer ? s.player_powers
                                             : s.monsters[actor].powers;
    uint8_t* count = actor == kActorPlayer ? &s.player_power_count
                                           : &s.monsters[actor].power_count;
    slots[*count] = PowerSlot{static_cast<uint16_t>(id), amount};
    ++*count;
}

const PowerSlot* FindMonsterPower(const CombatState& s, uint8_t actor,
                                  PowerId id) {
    for (uint8_t i = 0; i < s.monsters[actor].power_count; ++i) {
        if (s.monsters[actor].powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.monsters[actor].powers[i];
        }
    }
    return nullptr;
}

bool PileHas(const CardPoolIndex* pile, uint8_t count, CardPoolIndex pi) {
    for (uint8_t i = 0; i < count; ++i) {
        if (pile[i] == pi) {
            return true;
        }
    }
    return false;
}

void Play(CombatState& s, uint8_t hand_slot = 0, uint8_t target = 0) {
    ASSERT_TRUE(queue_card_play(s, hand_slot, target));
    pump(s, default_monster_turn);
}

StepResult Step(CombatState& s, Action a) {
    StepResult r{};
    advance(std::span<CombatState>(&s, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&r, 1));
    return r;
}

TEST(CardUncommonAttacksRegistry, ExactSourceRosterAndStableIds) {
    const std::array<CardId, 11> ids{
        CardId::BLOOD_FOR_BLOOD, CardId::CARNAGE, CardId::DROPKICK,
        CardId::HEMOKINESIS, CardId::PUMMEL, CardId::RAMPAGE,
        CardId::RECKLESS_CHARGE, CardId::SEARING_BLOW, CardId::SEVER_SOUL,
        CardId::UPPERCUT, CardId::WHIRLWIND};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        EXPECT_EQ(static_cast<int>(ids[i]), 40 + static_cast<int>(i));
        const CardDef* d = card_def(ids[i]);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(d->type, CardType::ATTACK);
    }
    EXPECT_EQ(static_cast<uint16_t>(Opcode::DROPKICK), 21);
    EXPECT_EQ(static_cast<uint16_t>(Opcode::DAMAGE_UPGRADE_SCALE), 22);
    EXPECT_EQ(static_cast<uint16_t>(Opcode::DAMAGE_RAMPAGE), 23);
    EXPECT_EQ(static_cast<uint16_t>(Opcode::EXHAUST_NON_ATTACKS), 24);
}

TEST(CardUncommonBloodForBlood, BaseAndUpgradeDamageRows) {
    const CardDef* d = card_def(CardId::BLOOD_FOR_BLOOD);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(card_cost(*d, 0), 4);
    EXPECT_EQ(card_cost(*d, 1), 3);
    CombatState b = MakeCombat();
    AddHand(b, CardId::BLOOD_FOR_BLOOD);
    Play(b);
    EXPECT_EQ(b.monsters[0].hp, 82);
    CombatState u = MakeCombat();
    AddHand(u, CardId::BLOOD_FOR_BLOOD, 1);
    Play(u);
    EXPECT_EQ(u.monsters[0].hp, 78);
}

TEST(CardUncommonBloodForBlood, PositiveHpLossReducesEveryActiveCopyPerEvent) {
    CombatState s = MakeCombat();
    const CardPoolIndex hand = AddHand(s, CardId::BLOOD_FOR_BLOOD);
    const CardPoolIndex draw = AddCard(s, CardId::BLOOD_FOR_BLOOD);
    s.draw[s.draw_count++] = draw;
    const CardPoolIndex discard = AddCard(s, CardId::BLOOD_FOR_BLOOD, 1);
    s.discard[s.discard_count++] = discard;
    const CardPoolIndex exhaust = AddCard(s, CardId::BLOOD_FOR_BLOOD);
    s.exhaust[s.exhaust_count++] = exhaust;

    ActionQueueItem hit{};
    hit.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    hit.src = 0;
    hit.tgt = kActorPlayer;
    hit.amount = 9;
    execute_opcode(s, hit);
    EXPECT_EQ(s.player_hp, 71);
    EXPECT_EQ(s.card_pool[hand].cost_now, 3);
    EXPECT_EQ(s.card_pool[draw].cost_now, 3);
    EXPECT_EQ(s.card_pool[discard].cost_now, 2);
    EXPECT_EQ(s.card_pool[exhaust].cost_now, 4)
        << "exhaust is not scanned by updateCardsOnDamage";

    s.player_block = 20;
    execute_opcode(s, hit);
    EXPECT_EQ(s.player_hp, 71);
    EXPECT_EQ(s.card_pool[hand].cost_now, 3)
        << "fully blocked damage is not an HP-loss event";
}

TEST(CardUncommonBloodForBlood, UpgradePreservesCombatReducedCost) {
    CombatState s = MakeCombat();
    const CardPoolIndex pi = AddHand(s, CardId::BLOOD_FOR_BLOOD);
    s.card_pool[pi].cost_now = 2;
    ASSERT_TRUE(choice_slot_eligible(s, 0, ChoiceKind::UPGRADE));
    apply_choice_selection(s, 0, ChoiceKind::UPGRADE);
    EXPECT_EQ(s.card_pool[pi].upgrade, 1);
    EXPECT_EQ(s.card_pool[pi].cost_now, 1);
}

TEST(CardUncommonCarnage, EtherealBaseAndUpgradedDamage) {
    const CardDef* d = card_def(CardId::CARNAGE);
    ASSERT_NE(d, nullptr);
    EXPECT_TRUE(has_card_flag(card_flags(*d, 0), CardFlag::ETHEREAL));
    CombatState b = MakeCombat();
    AddHand(b, CardId::CARNAGE);
    Play(b);
    EXPECT_EQ(b.monsters[0].hp, 80);
    CombatState u = MakeCombat();
    AddHand(u, CardId::CARNAGE, 1);
    Play(u);
    EXPECT_EQ(u.monsters[0].hp, 72);
}

TEST(CardUncommonCarnage, EtherealExhaustsIfUnplayedAtEndOfTurn) {
    CombatState s = MakeCombat();
    const CardPoolIndex pi = AddHand(s, CardId::CARNAGE);
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    pump(s, default_monster_turn);
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, pi));
}

TEST(CardUncommonDropkick, ConditionCheckedAtExecution) {
    CombatState b = MakeCombat(3);
    AddHand(b, CardId::DROPKICK);
    Play(b);
    EXPECT_EQ(b.monsters[0].hp, 95);
    EXPECT_EQ(b.player_energy, 2);
    EXPECT_EQ(b.hand_count, 0);

    CombatState u = MakeCombat(3);
    AddHand(u, CardId::DROPKICK, 1);
    const CardPoolIndex top = AddDrawTop(u, CardId::STRIKE);
    AddPower(u, 0, PowerId::VULNERABLE, 2);
    Play(u);
    EXPECT_EQ(u.monsters[0].hp, 88) << "8 * 1.5 Vulnerable";
    EXPECT_EQ(u.player_energy, 3) << "pay 1, then gain 1";
    ASSERT_EQ(u.hand_count, 1);
    EXPECT_EQ(u.hand[0], top);
}

TEST(CardUncommonHemokinesis, SelfLossThenBaseAndUpgradedDamage) {
    CombatState b = MakeCombat();
    AddHand(b, CardId::HEMOKINESIS);
    Play(b);
    EXPECT_EQ(b.player_hp, 78);
    EXPECT_EQ(b.monsters[0].hp, 85);
    CombatState u = MakeCombat();
    AddHand(u, CardId::HEMOKINESIS, 1);
    Play(u);
    EXPECT_EQ(u.player_hp, 78);
    EXPECT_EQ(u.monsters[0].hp, 80);
}

TEST(CardUncommonPummel, FourOrFiveHitsAndExhaust) {
    CombatState b = MakeCombat();
    const CardPoolIndex bp = AddHand(b, CardId::PUMMEL);
    Play(b);
    EXPECT_EQ(b.monsters[0].hp, 92);
    EXPECT_TRUE(PileHas(b.exhaust, b.exhaust_count, bp));
    CombatState u = MakeCombat();
    const CardPoolIndex up = AddHand(u, CardId::PUMMEL, 1);
    Play(u);
    EXPECT_EQ(u.monsters[0].hp, 90);
    EXPECT_TRUE(PileHas(u.exhaust, u.exhaust_count, up));
}

TEST(CardUncommonRampage, BaseInstanceScalesFiveAfterEachPlay) {
    CombatState s = MakeCombat();
    const CardPoolIndex pi = AddHand(s, CardId::RAMPAGE);
    Play(s);
    EXPECT_EQ(s.monsters[0].hp, 92);
    EXPECT_EQ(s.card_pool[pi].misc, 5);
    s.hand[s.hand_count++] = pi;
    --s.discard_count;
    s.player_energy = 6;
    Play(s);
    EXPECT_EQ(s.monsters[0].hp, 79) << "second hit is 8+5";
    EXPECT_EQ(s.card_pool[pi].misc, 10);
}

TEST(CardUncommonRampage, UpgradedInstanceScalesEight) {
    CombatState s = MakeCombat();
    const CardPoolIndex pi = AddHand(s, CardId::RAMPAGE, 1);
    Play(s);
    EXPECT_EQ(s.monsters[0].hp, 92);
    EXPECT_EQ(s.card_pool[pi].misc, 8);
    s.hand[s.hand_count++] = pi;
    --s.discard_count;
    s.player_energy = 6;
    Play(s);
    EXPECT_EQ(s.monsters[0].hp, 76) << "second hit is 8+8";
    EXPECT_EQ(s.card_pool[pi].misc, 16);
}

TEST(CardUncommonRecklessCharge, MakesDazedAtRandomDrawSpotBaseAndUpgraded) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::RECKLESS_CHARGE, up);
        AddDrawTop(s, CardId::STRIKE);
        s.card_random_rng = from_seed(42);
        const int32_t before = s.card_random_rng.counter;
        Play(s);
        EXPECT_EQ(s.monsters[0].hp, 100 - (up == 0 ? 7 : 10));
        EXPECT_EQ(s.card_random_rng.counter, before + 1);
        bool found = false;
        for (uint8_t i = 0; i < s.draw_count; ++i) {
            const CardInstance& c = s.card_pool[s.draw[i]];
            if (c.card_id == static_cast<uint16_t>(CardId::DAZED)) {
                found = true;
                EXPECT_TRUE(has_card_flag(c.flags, CardFlag::ETHEREAL));
            }
        }
        EXPECT_TRUE(found);
    }
}

TEST(CardUncommonSearingBlow, UpgradeCountUsesTriangularDamageFormula) {
    const std::array<int, 4> expected{12, 16, 21, 27};
    for (uint8_t n = 0; n < expected.size(); ++n) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::SEARING_BLOW, n);
        Play(s);
        EXPECT_EQ(s.monsters[0].hp, 100 - expected[n]) << "upgrade count " << +n;
    }
}

TEST(CardUncommonSearingBlow, RemainsUpgradeableWithoutSchemaChange) {
    static_assert(sizeof(CardInstance) == 8);
    CombatState s = MakeCombat();
    const CardPoolIndex pi = AddHand(s, CardId::SEARING_BLOW, 1);
    for (uint8_t want = 2; want <= 4; ++want) {
        EXPECT_TRUE(choice_slot_eligible(s, 0, ChoiceKind::UPGRADE));
        apply_choice_selection(s, 0, ChoiceKind::UPGRADE);
        EXPECT_EQ(s.card_pool[pi].upgrade, want);
        EXPECT_EQ(s.card_pool[pi].cost_now, 2);
    }
}

TEST(CardUncommonSeverSoul, ExhaustsAllNonAttacksBeforeBaseOrUpgradedDamage) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::SEVER_SOUL, up);
        const CardPoolIndex attack = AddHand(s, CardId::STRIKE);
        const CardPoolIndex skill = AddHand(s, CardId::DEFEND);
        const CardPoolIndex status = AddHand(s, CardId::DAZED);
        Play(s);
        EXPECT_EQ(s.monsters[0].hp, 100 - (up == 0 ? 16 : 22));
        EXPECT_TRUE(PileHas(s.hand, s.hand_count, attack));
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, skill));
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, status));
        EXPECT_EQ(s.hand_count, 1);
    }
}

TEST(CardUncommonUppercut, BaseAndUpgradeApplyBothDebuffsAfterDamage) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::UPPERCUT, up);
        Play(s);
        EXPECT_EQ(s.monsters[0].hp, 87);
        const PowerSlot* weak = FindMonsterPower(s, 0, PowerId::WEAK);
        const PowerSlot* vuln = FindMonsterPower(s, 0, PowerId::VULNERABLE);
        ASSERT_NE(weak, nullptr);
        ASSERT_NE(vuln, nullptr);
        EXPECT_EQ(weak->amount, up == 0 ? 1 : 2);
        EXPECT_EQ(vuln->amount, up == 0 ? 1 : 2);
    }
}

TEST(CardUncommonWhirlwind, XCostRepeatsAoeAndConsumesAllEnergy) {
    CombatState b = MakeThree(3);
    AddHand(b, CardId::WHIRLWIND);
    Play(b);
    EXPECT_EQ(b.player_energy, 0);
    for (uint8_t i = 0; i < b.monster_count; ++i) {
        EXPECT_EQ(b.monsters[i].hp, 85);
    }
    CombatState u = MakeThree(2);
    AddHand(u, CardId::WHIRLWIND, 1);
    Play(u);
    EXPECT_EQ(u.player_energy, 0);
    for (uint8_t i = 0; i < u.monster_count; ++i) {
        EXPECT_EQ(u.monsters[i].hp, 84);
    }
}

TEST(CardUncommonAttacksDirected, HemokinesisUnlocksReducedBloodForBloodPlay) {
    CombatState s = MakeCombat(/*energy=*/4, /*monster_hp=*/100);
    AddHand(s, CardId::HEMOKINESIS);
    const CardPoolIndex bfb = AddHand(s, CardId::BLOOD_FOR_BLOOD);

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));
    EXPECT_EQ(s.player_hp, 78);
    EXPECT_EQ(s.monsters[0].hp, 85);
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], bfb);
    EXPECT_EQ(s.card_pool[bfb].cost_now, 3)
        << "Hemokinesis HP loss fired Blood for Blood.tookDamage";
    EXPECT_EQ(s.player_energy, 3);

    ActionMask mask{};
    legal_actions(s, mask);
    ASSERT_TRUE(mask.can_play[0]);
    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));
    EXPECT_EQ(s.monsters[0].hp, 67);
    EXPECT_EQ(s.player_energy, 0);
}

}  // namespace
}  // namespace sts::engine
