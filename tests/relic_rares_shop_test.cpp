// B3.26 rare + shop relics: source-complete Ironclad roster and tier-2
// constructed-state behavior. Expected values are hand-derived from the Java
// citations carried by registry/relics.yaml. Cross-domain reward/shop/campfire
// UI effects are asserted as explicit registry-only boundaries here; their
// run-layer gates/acquisition effects are covered in relic_pools_test.cpp.

#include <array>
#include <cstdint>
#include <limits>
#include <span>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/potions.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/relic_hooks.hpp"
#include "sts/engine/relics.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

constexpr uint16_t kOp(Opcode op) {
    return static_cast<uint16_t>(op);
}

CombatState MakeState(uint8_t monster_count = 1, int16_t monster_hp = 100) {
    CombatState s{};
    s.player_hp = 70;
    s.player_max_hp = 80;
    s.player_energy = 3;
    s.monster_count = monster_count;
    for (uint8_t i = 0; i < monster_count; ++i) {
        s.monsters[i].monster_id =
            static_cast<uint16_t>(MonsterId::JAW_WORM);
        s.monsters[i].hp = monster_hp;
        s.monsters[i].max_hp = monster_hp;
    }
    s.monster_attacks_queued = 1;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    return s;
}

RelicSlot& AddRelic(
    CombatState& s, RelicId id,
    int16_t counter = std::numeric_limits<int16_t>::min()) {
    EXPECT_LT(s.relic_count, kRelicCap);
    RelicSlot& slot = s.relics[s.relic_count++];
    slot.relic_id = static_cast<uint16_t>(id);
    const RelicDef* def = relic_def(id);
    EXPECT_NE(def, nullptr);
    slot.counter =
        counter == std::numeric_limits<int16_t>::min()
            ? (def == nullptr ? int16_t{-1} : def->initial_counter)
            : counter;
    return slot;
}

void AddPower(CombatState& s, uint8_t actor, PowerId id, int16_t amount) {
    PowerSlot* slots =
        actor == kActorPlayer ? s.player_powers : s.monsters[actor].powers;
    uint8_t* count = actor == kActorPlayer ? &s.player_power_count
                                           : &s.monsters[actor].power_count;
    ASSERT_LT(*count, kPowerCap);
    slots[*count] = PowerSlot{static_cast<uint16_t>(id), amount};
    ++*count;
}

const PowerSlot* FindPower(const CombatState& s, uint8_t actor, PowerId id) {
    const PowerSlot* slots =
        actor == kActorPlayer ? s.player_powers : s.monsters[actor].powers;
    const uint8_t count = actor == kActorPlayer
                              ? s.player_power_count
                              : s.monsters[actor].power_count;
    for (uint8_t i = 0; i < count; ++i) {
        if (slots[i].power_id == static_cast<uint16_t>(id)) {
            return &slots[i];
        }
    }
    return nullptr;
}

CardPoolIndex AddCard(CombatState& s, CardId id, bool to_hand = true) {
    CardPoolIndex pi = 0;
    while (pi < kCardPoolCap &&
           s.card_pool[pi].card_id != static_cast<uint16_t>(CardId::NONE)) {
        ++pi;
    }
    EXPECT_LT(pi, kCardPoolCap);
    const CardDef* def = card_def(id);
    EXPECT_NE(def, nullptr);
    s.card_pool[pi].card_id = static_cast<uint16_t>(id);
    s.card_pool[pi].cost_now = def == nullptr ? 0 : card_cost(*def, 0);
    s.card_pool[pi].flags = def == nullptr ? 0 : card_flags(*def, 0);
    if (to_hand) {
        s.hand[s.hand_count++] = pi;
    }
    return pi;
}

ActionQueueItem Queued(const CombatState& s, uint8_t offset = 0) {
    return s.action_queue[
        static_cast<uint8_t>((s.action_head + offset) % kActionQueueCap)];
}

void Drain(CombatState& s) {
    ActionQueueItem item{};
    while (pop_action_front(s, item)) {
        execute_opcode(s, item);
    }
}

void StepCombat(CombatState& s, Action action) {
    StepResult result{};
    std::span<CombatState> states(&s, 1);
    std::span<const Action> actions(&action, 1);
    std::span<StepResult> results(&result, 1);
    advance(states, actions, results);
}

void Damage(CombatState& s, uint8_t src, uint8_t tgt, int amount,
            DamageType type = DamageType::NORMAL) {
    ActionQueueItem hit{};
    hit.opcode = kOp(Opcode::DAMAGE);
    hit.src = src;
    hit.tgt = tgt;
    hit.amount = amount;
    hit.flags = make_damage_flags(type);
    execute_opcode(s, hit);
}

void ApplyPower(CombatState& s, uint8_t src, uint8_t tgt, PowerId id,
                int amount) {
    ActionQueueItem apply{};
    apply.opcode = kOp(Opcode::APPLY_POWER);
    apply.src = src;
    apply.tgt = tgt;
    apply.amount = amount;
    apply.flags = make_apply_power_flags(id);
    execute_opcode(s, apply);
}

TEST(RelicRareShopRegistry, ExactSourceRosterTierAndPoolOrders) {
    // 25 shared rare add() rows + three red rows. Captain's Wheel is included
    // from source enumeration although the task prose omitted it.
    constexpr std::array<RelicId, 28> rare{{
        RelicId::BIRD_FACED_URN, RelicId::CALIPERS,
        RelicId::CAPTAINS_WHEEL, RelicId::CHAMPION_BELT,
        RelicId::CHARONS_ASHES, RelicId::DEAD_BRANCH,
        RelicId::DU_VU_DOLL, RelicId::FOSSILIZED_HELIX,
        RelicId::GAMBLING_CHIP, RelicId::GINGER, RelicId::GIRYA,
        RelicId::ICE_CREAM, RelicId::INCENSE_BURNER,
        RelicId::LIZARD_TAIL, RelicId::MAGIC_FLOWER, RelicId::MANGO,
        RelicId::OLD_COIN, RelicId::PEACE_PIPE, RelicId::POCKETWATCH,
        RelicId::PRAYER_WHEEL, RelicId::SHOVEL,
        RelicId::STONE_CALENDAR, RelicId::THREAD_AND_NEEDLE,
        RelicId::TORII, RelicId::TUNGSTEN_ROD, RelicId::TURNIP,
        RelicId::UNCEASING_TOP, RelicId::WING_BOOTS,
    }};
    constexpr std::array<RelicId, 17> shop{{
        RelicId::THE_ABACUS, RelicId::BRIMSTONE, RelicId::CAULDRON,
        RelicId::CHEMICAL_X, RelicId::CLOCKWORK_SOUVENIR,
        RelicId::DOLLYS_MIRROR, RelicId::FROZEN_EYE,
        RelicId::HAND_DRILL, RelicId::MEDICAL_KIT,
        RelicId::MEMBERSHIP_CARD, RelicId::ORANGE_PELLETS,
        RelicId::ORRERY, RelicId::PRISMATIC_SHARD,
        RelicId::SLING_OF_COURAGE, RelicId::STRANGE_SPOON,
        RelicId::TOOLBOX, RelicId::LEES_WAFFLE,
    }};
    std::array<bool, 28> rare_order{};
    std::array<bool, 17> shop_order{};
    for (RelicId id : rare) {
        const RelicDef* def = relic_def(id);
        ASSERT_NE(def, nullptr);
        EXPECT_EQ(def->tier, RelicTier::RARE);
        ASSERT_GE(def->pool_order, 0);
        ASSERT_LT(def->pool_order, static_cast<int>(rare_order.size()));
        EXPECT_FALSE(rare_order[static_cast<std::size_t>(def->pool_order)]);
        rare_order[static_cast<std::size_t>(def->pool_order)] = true;
    }
    for (RelicId id : shop) {
        const RelicDef* def = relic_def(id);
        ASSERT_NE(def, nullptr);
        EXPECT_EQ(def->tier, RelicTier::SHOP);
        ASSERT_GE(def->pool_order, 0);
        ASSERT_LT(def->pool_order, static_cast<int>(shop_order.size()));
        EXPECT_FALSE(shop_order[static_cast<std::size_t>(def->pool_order)]);
        shop_order[static_cast<std::size_t>(def->pool_order)] = true;
    }
    for (bool present : rare_order) {
        EXPECT_TRUE(present);
    }
    for (bool present : shop_order) {
        EXPECT_TRUE(present);
    }
}

TEST(RelicRareShopRegistry, DeadBranchPoolMatchesIndependentJavaSourceOrder) {
    // Hand-derived from CardLibrary.addRedCards HashMap iteration, then each
    // rarity group's CardGroup.addToBottom reversal, concatenated common ->
    // uncommon -> rare. This deliberately does not derive expected order from
    // the generated table under test.
    constexpr std::array<CardId, 48> expected{{
        CardId::SWORD_BOOMERANG, CardId::PERFECTED_STRIKE,
        CardId::HEAVY_BLADE, CardId::WILD_STRIKE, CardId::HEADBUTT,
        CardId::HAVOC, CardId::ARMAMENTS, CardId::CLOTHESLINE,
        CardId::TWIN_STRIKE, CardId::POMMEL_STRIKE, CardId::THUNDERCLAP,
        CardId::CLASH, CardId::SHRUG_IT_OFF, CardId::TRUE_GRIT,
        CardId::BODY_SLAM, CardId::IRON_WAVE, CardId::FLEX,
        CardId::WARCRY, CardId::CLEAVE, CardId::ANGER, CardId::UPPERCUT,
        CardId::GHOSTLY_ARMOR, CardId::DROPKICK, CardId::CARNAGE,
        CardId::BLOODLETTING, CardId::SECOND_WIND, CardId::SEARING_BLOW,
        CardId::BATTLE_TRANCE, CardId::SENTINEL, CardId::ENTRENCH,
        CardId::RAGE, CardId::DISARM, CardId::SEEING_RED,
        CardId::WHIRLWIND, CardId::SEVER_SOUL, CardId::RAMPAGE,
        CardId::SHOCKWAVE, CardId::BURNING_PACT, CardId::PUMMEL,
        CardId::FLAME_BARRIER, CardId::BLOOD_FOR_BLOOD,
        CardId::INTIMIDATE, CardId::HEMOKINESIS,
        CardId::RECKLESS_CHARGE, CardId::INFERNAL_BLADE,
        CardId::DUAL_WIELD, CardId::POWER_THROUGH, CardId::SPOT_WEAKNESS,
    }};
    static_assert(kIroncladCombatCardPoolCount == expected.size());
    EXPECT_EQ(kIroncladCombatCardPool, expected);
}

TEST(RelicRareData, FossilThreadAndClockworkApplyTheirPowers) {
    CombatState s = MakeState();
    ActionQueueItem prior{};
    prior.opcode = kOp(Opcode::DRAW);
    prior.src = kActorPlayer;
    prior.tgt = kActorPlayer;
    prior.amount = 1;
    add_to_bottom(s, prior);
    AddRelic(s, RelicId::FOSSILIZED_HELIX);
    AddRelic(s, RelicId::THREAD_AND_NEEDLE);
    AddRelic(s, RelicId::CLOCKWORK_SOUVENIR);
    dispatch_relics_at_battle_start(s, s.relics, s.relic_count);
    ASSERT_EQ(s.action_count, 4);
    EXPECT_EQ(apply_power_id_from_flags(Queued(s, 0).flags), PowerId::ARTIFACT);
    EXPECT_EQ(apply_power_id_from_flags(Queued(s, 1).flags),
              PowerId::PLATED_ARMOR);
    EXPECT_EQ(Queued(s, 2).opcode, kOp(Opcode::DRAW));
    EXPECT_EQ(apply_power_id_from_flags(Queued(s, 3).flags), PowerId::BUFFER)
        << "Helix is addToBot; Clockwork and Thread are addToTop";
    Drain(s);
    ASSERT_NE(FindPower(s, kActorPlayer, PowerId::BUFFER), nullptr);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::BUFFER)->amount, 1);
    ASSERT_NE(FindPower(s, kActorPlayer, PowerId::PLATED_ARMOR), nullptr);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::PLATED_ARMOR)->amount, 4);
    ASSERT_NE(FindPower(s, kActorPlayer, PowerId::ARTIFACT), nullptr);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::ARTIFACT)->amount, 1);
}

TEST(RelicRareData, CharonsAshesDealsThreeFlatDamageToAll) {
    CombatState s = MakeState(2);
    AddRelic(s, RelicId::CHARONS_ASHES);
    AddPower(s, 0, PowerId::VULNERABLE, 2);
    ActionQueueItem prior{};
    prior.opcode = kOp(Opcode::DRAW);
    add_to_bottom(s, prior);
    dispatch_relics_on_exhaust(
        s, s.relics, s.relic_count, static_cast<uint16_t>(CardId::SEEING_RED));
    ASSERT_EQ(s.action_count, 2);
    EXPECT_EQ(Queued(s, 0).opcode, kOp(Opcode::DAMAGE))
        << "Charon's Ashes uses addToTop";
    EXPECT_EQ(Queued(s, 1).opcode, kOp(Opcode::DRAW));
    Drain(s);
    EXPECT_EQ(s.monsters[0].hp, 97);
    EXPECT_EQ(s.monsters[1].hp, 97);
}

TEST(RelicShopData, BrimstoneAbacusAndHandDrillAreLive) {
    CombatState s = MakeState(2);
    AddRelic(s, RelicId::BRIMSTONE);
    AddRelic(s, RelicId::THE_ABACUS);
    AddRelic(s, RelicId::HAND_DRILL);
    ActionQueueItem prior{};
    prior.opcode = kOp(Opcode::DRAW);
    add_to_bottom(s, prior);
    dispatch_relics_at_turn_start(s, s.relics, s.relic_count);
    ASSERT_EQ(s.action_count, 3);
    EXPECT_EQ(Queued(s, 0).tgt, kActorPlayer);
    EXPECT_EQ(Queued(s, 0).amount, 2);
    EXPECT_EQ(Queued(s, 1).tgt, kActorAllEnemies);
    EXPECT_EQ(Queued(s, 1).amount, 1);
    EXPECT_EQ(Queued(s, 2).opcode, kOp(Opcode::DRAW))
        << "Brimstone top-inserts monsters then player, so player resolves first";
    Drain(s);
    ASSERT_NE(FindPower(s, kActorPlayer, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::STRENGTH)->amount, 2);
    EXPECT_EQ(FindPower(s, 0, PowerId::STRENGTH)->amount, 1);
    EXPECT_EQ(FindPower(s, 1, PowerId::STRENGTH)->amount, 1);

    AddPower(s, kActorPlayer, PowerId::DEXTERITY, 3);
    AddPower(s, kActorPlayer, PowerId::FRAIL, 2);
    dispatch_relics_on_shuffle(s, s.relics, s.relic_count);
    ASSERT_EQ(Queued(s).flags & kBlockNoPowers, kBlockNoPowers);
    Drain(s);
    EXPECT_EQ(s.player_block, 6)
        << "Abacus is direct GainBlockAction, unaffected by Dexterity/Frail";

    s.monsters[0].block = 4;
    Damage(s, kActorPlayer, 0, 4);
    ASSERT_EQ(s.monsters[0].block, 0);
    Drain(s);
    ASSERT_NE(FindPower(s, 0, PowerId::VULNERABLE), nullptr);
    EXPECT_EQ(FindPower(s, 0, PowerId::VULNERABLE)->amount, 2);
}

TEST(RelicShopData, HandDrillQueuedDebuffSkipsADeadMonster) {
    CombatState s = MakeState(2);
    AddRelic(s, RelicId::HAND_DRILL);
    s.monsters[0].hp = 4;
    s.monsters[0].block = 4;
    Damage(s, kActorPlayer, 0, 8);
    ASSERT_EQ(s.monsters[0].hp, 0);
    ASSERT_EQ(s.action_count, 1);
    Drain(s);
    EXPECT_EQ(FindPower(s, 0, PowerId::VULNERABLE), nullptr)
        << "ApplyPowerAction drops a queued effect whose target died";
    EXPECT_GT(s.monsters[1].hp, 0);
}

TEST(RelicRareCounters, CaptainsWheelAndGiryaApplyExactAmounts) {
    CombatState s = MakeState();
    RelicSlot& wheel = AddRelic(s, RelicId::CAPTAINS_WHEEL);
    AddRelic(s, RelicId::DU_VU_DOLL, 3);
    AddRelic(s, RelicId::GIRYA, 2);
    dispatch_relics_at_battle_start(s, s.relics, s.relic_count);
    EXPECT_EQ(wheel.counter, 0);
    ASSERT_EQ(s.action_count, 2);
    EXPECT_EQ(Queued(s, 0).amount, 2);
    EXPECT_EQ(Queued(s, 1).amount, 3)
        << "Girya and Du-Vu Doll both addToTop, reversing acquisition order";
    Drain(s);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::STRENGTH)->amount, 5);
    dispatch_relics_at_turn_start(s, s.relics, s.relic_count);
    dispatch_relics_at_turn_start(s, s.relics, s.relic_count);
    EXPECT_EQ(wheel.counter, 2);
    dispatch_relics_at_turn_start(s, s.relics, s.relic_count);
    EXPECT_EQ(wheel.counter, -1);
    Drain(s);
    EXPECT_EQ(s.player_block, 18);
}

TEST(RelicRareCounters, IncenseBurnerAppliesIntangibleEverySixthTurn) {
    CombatState s = MakeState();
    RelicSlot& burner = AddRelic(s, RelicId::INCENSE_BURNER, 5);
    dispatch_relics_at_turn_start(s, s.relics, s.relic_count);
    EXPECT_EQ(burner.counter, 0);
    Drain(s);
    ASSERT_NE(FindPower(s, kActorPlayer, PowerId::INTANGIBLE), nullptr);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::INTANGIBLE)->amount, 1);
}

TEST(RelicRareTurnLoop, IceCreamAndCalipersRewriteOnlyOwnedBranches) {
    CombatState s = MakeState();
    AddRelic(s, RelicId::ICE_CREAM);
    AddRelic(s, RelicId::CALIPERS);
    s.turn = 1;
    s.turn_has_ended = 1;
    s.player_energy = 2;
    s.player_block = 20;
    EXPECT_EQ(pump_step(s, default_monster_turn).outcome,
              PumpOutcome::STARTED_TURN);
    EXPECT_EQ(s.player_energy, 5);
    EXPECT_EQ(s.player_block, 5);

    CombatState baseline = MakeState();
    baseline.turn = 1;
    baseline.turn_has_ended = 1;
    baseline.player_energy = 2;
    baseline.player_block = 20;
    EXPECT_EQ(pump_step(baseline, default_monster_turn).outcome,
              PumpOutcome::STARTED_TURN);
    EXPECT_EQ(baseline.player_energy, kIroncladBaseEnergy);
    EXPECT_EQ(baseline.player_block, 0);

    CombatState cap = MakeState();
    AddRelic(cap, RelicId::ICE_CREAM);
    cap.turn = 1;
    cap.turn_has_ended = 1;
    cap.player_energy = 998;
    (void)pump_step(cap, default_monster_turn);
    EXPECT_EQ(cap.player_energy, 999);
}

TEST(RelicRareOpening, PreBattlePreDrawBattleAndPostDrawTimingIsLive) {
    CombatState s = MakeState();
    RelicSlot& chip = AddRelic(s, RelicId::GAMBLING_CHIP);
    RelicSlot& wheel = AddRelic(s, RelicId::CAPTAINS_WHEEL);
    AddRelic(s, RelicId::TOOLBOX);
    dispatch_relics_at_pre_battle(s, s.relics, s.relic_count);
    s.turn = 0;
    s.turn_has_ended = 1;
    EXPECT_EQ(pump_step(s, default_monster_turn).outcome,
              PumpOutcome::STARTED_TURN);
    EXPECT_EQ(s.turn, 1);
    EXPECT_EQ(chip.counter, -1)
        << "Gambling Chip's private activated state never leaks into counter";
    EXPECT_EQ(wheel.counter, 1)
        << "battle-start arm precedes opening turn-start increment";
    ASSERT_EQ(s.action_count, 2);
    EXPECT_EQ(Queued(s).opcode, kOp(Opcode::DRAW));
    EXPECT_EQ(Queued(s, 1).opcode, kOp(Opcode::CHOOSE_CARD));
    EXPECT_TRUE(choose_is_optional(Queued(s, 1).flags))
        << "ordinary opening draw precedes Gambling Chip's optional prompt";
}

TEST(RelicRareOpening, GamblingChipDiscardsAnyCountThenDrawsThatCount) {
    CombatState s = MakeState();
    RelicSlot& chip = AddRelic(s, RelicId::GAMBLING_CHIP);
    const CardPoolIndex strike = AddCard(s, CardId::STRIKE);
    const CardPoolIndex defend = AddCard(s, CardId::DEFEND);
    const CardPoolIndex bash = AddCard(s, CardId::BASH);
    const CardPoolIndex cleave = AddCard(s, CardId::CLEAVE, false);
    const CardPoolIndex flex = AddCard(s, CardId::FLEX, false);
    s.draw[s.draw_count++] = cleave;
    s.draw[s.draw_count++] = flex;
    dispatch_relics_at_battle_start_pre_draw(s, s.relics, s.relic_count);
    dispatch_relics_at_turn_start_post_draw(s, s.relics, s.relic_count);

    ActionMask mask{};
    legal_actions(s, mask);
    ASSERT_TRUE(mask.choice_pending);
    EXPECT_TRUE(mask.choice_can_confirm) << "zero selections are legal";
    EXPECT_EQ(chip.counter, -1);
    StepCombat(s, make_action(ActionVerb::CHOOSE, 1));  // discard Defend
    ASSERT_EQ(s.discard_count, 1);
    EXPECT_EQ(s.discard[0], defend);
    StepCombat(s, make_action(ActionVerb::CHOOSE, 1));  // compacted slot: Bash
    ASSERT_EQ(s.discard_count, 2);
    EXPECT_EQ(s.discard[1], bash);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(Queued(s).amount, 2);

    StepCombat(s, make_action(ActionVerb::CHOOSE, kChoiceConfirmSlot));
    EXPECT_EQ(s.action_count, 0);
    ASSERT_EQ(s.hand_count, 3);
    EXPECT_EQ(s.hand[0], strike);
    EXPECT_EQ(s.hand[1], flex);
    EXPECT_EQ(s.hand[2], cleave);
    EXPECT_EQ(s.draw_count, 0);
    EXPECT_EQ(chip.counter, -1);
}

TEST(RelicRareRandom, DeadBranchUsesOneCardRandomDrawAndGeneratedPool) {
    CombatState s = MakeState();
    AddRelic(s, RelicId::DEAD_BRANCH);
    s.card_random_rng = from_seed(12345);
    RngStream expected_rng = s.card_random_rng;
    const int32_t expected_index =
        random(expected_rng, kIroncladCombatCardPoolCount - 1);
    const CardId expected =
        kIroncladCombatCardPool[static_cast<unsigned>(expected_index)];
    dispatch_relics_on_exhaust(
        s, s.relics, s.relic_count, static_cast<uint16_t>(CardId::SEEING_RED));
    EXPECT_EQ(s.card_random_rng.counter, 1);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(Queued(s).opcode, kOp(Opcode::MAKE_CARD));
    Drain(s);
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(static_cast<CardId>(s.card_pool[s.hand[0]].card_id), expected);

    CombatState dead = MakeState(1, 0);
    AddRelic(dead, RelicId::DEAD_BRANCH);
    dead.card_random_rng = from_seed(12345);
    dispatch_relics_on_exhaust(
        dead, dead.relics, dead.relic_count,
        static_cast<uint16_t>(CardId::SEEING_RED));
    EXPECT_EQ(dead.action_count, 0);
    EXPECT_EQ(dead.card_random_rng.counter, 0);
}

TEST(RelicRareCounters, DuVuDollAndPocketwatchUsePersistentSlots) {
    CombatState s = MakeState();
    AddRelic(s, RelicId::DU_VU_DOLL, 2);
    RelicSlot& watch = AddRelic(s, RelicId::POCKETWATCH);
    dispatch_relics_at_battle_start(s, s.relics, s.relic_count);
    Drain(s);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::STRENGTH)->amount, 2);
    EXPECT_EQ(watch.counter, -1);
    dispatch_relics_at_turn_start_post_draw(s, s.relics, s.relic_count);
    EXPECT_EQ(watch.counter, 0);
    EXPECT_EQ(s.action_count, 0) << "opening turn is skipped";
    for (int i = 0; i < 3; ++i) {
        dispatch_relics_on_play_card(
            s, s.relics, s.relic_count,
            static_cast<uint16_t>(CardId::STRIKE));
    }
    dispatch_relics_at_turn_start_post_draw(s, s.relics, s.relic_count);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(Queued(s).opcode, kOp(Opcode::DRAW));
    EXPECT_EQ(Queued(s).amount, 3);
    s.action_count = 0;
    s.action_head = 0;
    s.action_tail = 0;
    for (int i = 0; i < 4; ++i) {
        dispatch_relics_on_play_card(
            s, s.relics, s.relic_count,
            static_cast<uint16_t>(CardId::STRIKE));
    }
    dispatch_relics_at_turn_start_post_draw(s, s.relics, s.relic_count);
    EXPECT_EQ(s.action_count, 0);
}

TEST(RelicRareCounters, StoneCalendarFiresOnceAtEndOfTurnSeven) {
    CombatState s = MakeState(2);
    RelicSlot& calendar = AddRelic(s, RelicId::STONE_CALENDAR);
    dispatch_relics_at_battle_start(s, s.relics, s.relic_count);
    for (int turn = 1; turn <= 7; ++turn) {
        dispatch_relics_at_turn_start(s, s.relics, s.relic_count);
        dispatch_relics_on_player_end_turn(s, s.relics, s.relic_count);
        Drain(s);
        EXPECT_EQ(s.monsters[0].hp, turn < 7 ? 100 : 48);
    }
    EXPECT_EQ(calendar.counter, 7)
        << "Stone Calendar remains at 7 after its end-turn pulse";
    dispatch_relics_at_turn_start(s, s.relics, s.relic_count);
    EXPECT_EQ(calendar.counter, 8);
    dispatch_relics_on_player_end_turn(s, s.relics, s.relic_count);
    Drain(s);
    EXPECT_EQ(s.monsters[0].hp, 48);
    EXPECT_EQ(s.monsters[1].hp, 48);
    dispatch_relics_on_victory(s, s.relics, s.relic_count);
    EXPECT_EQ(calendar.counter, -1);
}

TEST(RelicRarePump, UnceasingTopDrawsOnlyAtTheTrueIdleBoundary) {
    CombatState s = MakeState();
    RelicSlot& top = AddRelic(s, RelicId::UNCEASING_TOP);
    s.turn = 1;
    dispatch_relics_at_pre_battle(s, s.relics, s.relic_count);
    dispatch_relics_at_turn_start(s, s.relics, s.relic_count);
    const CardPoolIndex pi = AddCard(s, CardId::STRIKE, false);
    s.draw[s.draw_count++] = pi;
    pump(s, default_monster_turn);
    EXPECT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], pi);
    EXPECT_EQ(top.counter, -1)
        << "Unceasing Top's private booleans do not alter public counter";
    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));

    s.hand_count = 0;
    const CardPoolIndex again = AddCard(s, CardId::DEFEND, false);
    s.draw[s.draw_count++] = again;
    pump(s, default_monster_turn);
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], again) << "a second empty hand retriggers this turn";
    EXPECT_EQ(top.counter, -1);

    CombatState no_draw = MakeState();
    RelicSlot& blocked_top = AddRelic(no_draw, RelicId::UNCEASING_TOP);
    no_draw.turn = 1;
    dispatch_relics_at_turn_start(
        no_draw, no_draw.relics, no_draw.relic_count);
    const CardPoolIndex blocked = AddCard(no_draw, CardId::STRIKE, false);
    no_draw.draw[no_draw.draw_count++] = blocked;
    AddPower(no_draw, kActorPlayer, PowerId::NO_DRAW, -1);
    dispatch_relics_on_refresh_hand(
        no_draw, no_draw.relics, no_draw.relic_count);
    EXPECT_EQ(no_draw.action_count, 0);
    EXPECT_EQ(blocked_top.counter, -1);
}

TEST(RelicRareDamage, IntangibleThenBlockThenBuffer) {
    CombatState intangible = MakeState();
    AddPower(intangible, kActorPlayer, PowerId::INTANGIBLE, 1);
    intangible.player_block = 5;
    Damage(intangible, 0, kActorPlayer, 20);
    EXPECT_EQ(intangible.player_hp, 70);
    EXPECT_EQ(intangible.player_block, 4)
        << "Intangible caps to one before decrementBlock";
    Damage(intangible, 0, kActorPlayer, 7, DamageType::THORNS);
    EXPECT_EQ(intangible.player_block, 3)
        << "the AbstractPlayer guard also caps THORNS";

    ActionQueueItem intangible_loss{};
    intangible_loss.opcode = kOp(Opcode::LOSE_HP);
    intangible_loss.src = kActorPlayer;
    intangible_loss.tgt = kActorPlayer;
    intangible_loss.amount = 9;
    execute_opcode(intangible, intangible_loss);
    EXPECT_EQ(intangible.player_hp, 69)
        << "HP_LOSS bypasses Block but is still capped by Intangible";

    CombatState buffer = MakeState();
    AddPower(buffer, kActorPlayer, PowerId::BUFFER, 1);
    buffer.player_block = 2;
    Damage(buffer, 0, kActorPlayer, 5);
    EXPECT_EQ(buffer.player_hp, 70);
    EXPECT_EQ(buffer.player_block, 0);
    ASSERT_EQ(buffer.action_count, 1);
    EXPECT_EQ(Queued(buffer).opcode, kOp(Opcode::REDUCE_POWER));
    Drain(buffer);
    EXPECT_EQ(FindPower(buffer, kActorPlayer, PowerId::BUFFER), nullptr);

    CombatState buffered_loss = MakeState();
    AddPower(buffered_loss, kActorPlayer, PowerId::BUFFER, 1);
    ActionQueueItem loss{};
    loss.opcode = kOp(Opcode::LOSE_HP);
    loss.src = kActorPlayer;
    loss.tgt = kActorPlayer;
    loss.amount = 9;
    execute_opcode(buffered_loss, loss);
    EXPECT_EQ(buffered_loss.player_hp, 70);
    ASSERT_EQ(buffered_loss.action_count, 1)
        << "Buffer also intercepts LoseHPAction's HP_LOSS DamageInfo";
    Drain(buffered_loss);
    EXPECT_EQ(FindPower(buffered_loss, kActorPlayer, PowerId::BUFFER), nullptr);
}

TEST(RelicRareDamage, ToriiPrecedesTungstenAndTungstenCoversHpLoss) {
    CombatState s = MakeState();
    AddRelic(s, RelicId::TORII);
    AddRelic(s, RelicId::TUNGSTEN_ROD);
    Damage(s, 0, kActorPlayer, 4);
    EXPECT_EQ(s.player_hp, 70) << "Torii 4->1, then Tungsten 1->0";
    Damage(s, 0, kActorPlayer, 6);
    EXPECT_EQ(s.player_hp, 65) << "Torii skips 6; Tungsten 6->5";

    ActionQueueItem loss{};
    loss.opcode = kOp(Opcode::LOSE_HP);
    loss.src = kActorPlayer;
    loss.tgt = kActorPlayer;
    loss.amount = 2;
    execute_opcode(s, loss);
    EXPECT_EQ(s.player_hp, 64) << "Tungsten also modifies HP_LOSS";
}

TEST(RelicSpecialDependency, OddMushroomUsesPlayerVulnerableOnePointTwoFive) {
    CombatState s = MakeState();
    AddRelic(s, RelicId::ODD_MUSHROOM);
    AddPower(s, kActorPlayer, PowerId::VULNERABLE, 2);
    Damage(s, 0, kActorPlayer, 8);
    EXPECT_EQ(s.player_hp, 60) << "floor(8 * 1.25) = 10, not 12";

    CombatState baseline = MakeState();
    AddPower(baseline, kActorPlayer, PowerId::VULNERABLE, 2);
    Damage(baseline, 0, kActorPlayer, 8);
    EXPECT_EQ(baseline.player_hp, 58) << "ordinary Vulnerable is 1.5x";
}

TEST(RelicRareDamage, LizardTailRevivesOnceAndMagicFlowerBoostsHeal) {
    CombatState s = MakeState();
    AddRelic(s, RelicId::LIZARD_TAIL);
    AddRelic(s, RelicId::MAGIC_FLOWER);
    s.player_hp = 10;
    Damage(s, 0, kActorPlayer, 20);
    EXPECT_EQ(s.player_hp, 60)
        << "max/2 = 40, Magic Flower rounds 1.5x to 60";
    EXPECT_EQ(s.relics[0].counter, -2);
    Damage(s, 0, kActorPlayer, 100);
    EXPECT_EQ(s.player_hp, 0) << "used tail cannot trigger twice";
}

TEST(RelicRareHeal, MagicFlowerAppliesToSharedCombatHealSeam) {
    CombatState direct = MakeState();
    AddRelic(direct, RelicId::MAGIC_FLOWER);
    direct.player_hp = 40;
    heal_player_with_relics(direct, 3);
    EXPECT_EQ(direct.player_hp, 45) << "round(3 * 1.5) = 5";

    CombatState potion = MakeState();
    AddRelic(potion, RelicId::MAGIC_FLOWER);
    potion.player_hp = 40;
    ASSERT_TRUE(use_potion(potion, PotionId::BLOOD_POTION, 0));
    EXPECT_EQ(potion.player_hp, 64)
        << "20% of 80 = 16; Magic Flower makes the heal 24";
}

TEST(RelicRareApplyPower, ChampionBeltGingerAndTurnipPrecedeArtifact) {
    CombatState immune = MakeState();
    AddRelic(immune, RelicId::GINGER);
    AddRelic(immune, RelicId::TURNIP);
    AddPower(immune, kActorPlayer, PowerId::ARTIFACT, 2);
    ApplyPower(immune, 0, kActorPlayer, PowerId::WEAK, 2);
    ApplyPower(immune, 0, kActorPlayer, PowerId::FRAIL, 2);
    EXPECT_EQ(FindPower(immune, kActorPlayer, PowerId::WEAK), nullptr);
    EXPECT_EQ(FindPower(immune, kActorPlayer, PowerId::FRAIL), nullptr);
    EXPECT_EQ(FindPower(immune, kActorPlayer, PowerId::ARTIFACT)->amount, 2)
        << "relic immunity returns before consuming Artifact";

    CombatState belt = MakeState();
    AddRelic(belt, RelicId::CHAMPION_BELT);
    AddPower(belt, 0, PowerId::ARTIFACT, 1);
    ApplyPower(belt, kActorPlayer, 0, PowerId::VULNERABLE, 2);
    EXPECT_EQ(FindPower(belt, 0, PowerId::VULNERABLE), nullptr);
    EXPECT_EQ(FindPower(belt, 0, PowerId::ARTIFACT)->amount, 0);
    Drain(belt);
    EXPECT_EQ(FindPower(belt, 0, PowerId::WEAK), nullptr)
        << "Champion Belt must not trigger against an Artifact target";

    CombatState exposed = MakeState();
    AddRelic(exposed, RelicId::CHAMPION_BELT);
    ActionQueueItem prior{};
    prior.opcode = kOp(Opcode::DRAW);
    add_to_bottom(exposed, prior);
    ApplyPower(exposed, kActorPlayer, 0, PowerId::VULNERABLE, 2);
    ASSERT_EQ(exposed.action_count, 2);
    EXPECT_EQ(Queued(exposed, 0).opcode, kOp(Opcode::DRAW));
    EXPECT_EQ(apply_power_id_from_flags(Queued(exposed, 1).flags),
              PowerId::WEAK) << "Champion Belt uses addToBot";
    Drain(exposed);
    ASSERT_NE(FindPower(exposed, 0, PowerId::VULNERABLE), nullptr);
    ASSERT_NE(FindPower(exposed, 0, PowerId::WEAK), nullptr);
    EXPECT_EQ(FindPower(exposed, 0, PowerId::WEAK)->amount, 1);
}

TEST(RelicShopCards, MedicalKitMakesStatusesPlayableAndExhaustsThem) {
    CombatState s = MakeState();
    const CardPoolIndex wound = AddCard(s, CardId::WOUND);
    ActionMask mask{};
    legal_actions(s, mask);
    EXPECT_FALSE(mask.can_play[0]);
    AddRelic(s, RelicId::MEDICAL_KIT);
    legal_actions(s, mask);
    EXPECT_TRUE(mask.can_play[0]);
    resolve_card_play(s, CardQueueItem{wound, 0});
    EXPECT_EQ(s.limbo_count, 1)
        << "UseCardAction finalizes the exhaust after earlier effects";
    Drain(s);
    EXPECT_EQ(s.hand_count, 0);
    EXPECT_EQ(s.exhaust_count, 1);
    EXPECT_EQ(s.exhaust[0], wound);
}

TEST(RelicShopCards, ChemicalXAddsTwoRepetitionsWithoutExtraSpend) {
    CombatState s = MakeState(1, 100);
    s.player_energy = 1;
    AddRelic(s, RelicId::CHEMICAL_X);
    const CardPoolIndex whirlwind = AddCard(s, CardId::WHIRLWIND);
    resolve_card_play(s, CardQueueItem{whirlwind, 0});
    EXPECT_EQ(s.player_energy, 0);
    ASSERT_EQ(s.action_count, 3);
    Drain(s);
    EXPECT_EQ(s.monsters[0].hp, 85);
}

TEST(RelicShopCards, HavocAutoplayWhirlwindUsesEnergyButSpendsNone) {
    CombatState s = MakeState(1, 100);
    s.player_energy = 3;
    AddRelic(s, RelicId::CHEMICAL_X);
    const CardPoolIndex havoc = AddCard(s, CardId::HAVOC);
    s.card_pool[havoc].cost_now = 0;  // upgraded/free Havoc leaves energy at 3
    const CardPoolIndex whirlwind = AddCard(s, CardId::WHIRLWIND, false);
    s.draw[s.draw_count++] = whirlwind;

    resolve_card_play(s, CardQueueItem{havoc, 0});
    pump(s, default_monster_turn);
    EXPECT_EQ(s.monsters[0].hp, 75)
        << "current energy 3 + Chemical X 2 = five Whirlwind hits";
    EXPECT_EQ(s.player_energy, 3)
        << "PlayTopCardAction autoplay never spends the X-cost energy";
    ASSERT_EQ(s.exhaust_count, 1);
    EXPECT_EQ(s.exhaust[0], whirlwind);
}

TEST(RelicShopCards, StrangeSpoonConsumesExactlyOneConditionalRngDraw) {
    CombatState s = MakeState();
    AddRelic(s, RelicId::STRANGE_SPOON);
    const CardPoolIndex infernal = AddCard(s, CardId::INFERNAL_BLADE);
    s.card_random_rng = from_seed(77);
    RngStream expected = s.card_random_rng;
    const int32_t attack_index =
        random(expected, kIroncladAttackPoolCount - 1);
    const bool prevented = random_boolean(expected);
    const CardId expected_attack =
        kIroncladAttackPool[static_cast<std::size_t>(attack_index)];
    resolve_card_play(s, CardQueueItem{infernal, 0});
    EXPECT_EQ(s.card_random_rng.counter, 0)
        << "neither Infernal Blade nor Spoon rolls at card-use enqueue time";
    ASSERT_EQ(s.action_count, 2);
    EXPECT_EQ(Queued(s, 0).opcode, kOp(Opcode::RANDOM_ATTACK_TO_HAND));
    EXPECT_EQ(Queued(s, 1).opcode, kOp(Opcode::FINALIZE_CARD));
    EXPECT_EQ(s.limbo_count, 1);
    Drain(s);
    EXPECT_EQ(s.card_random_rng.counter, 2)
        << "random attack resolves before Spoon's UseCardAction boolean";
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(static_cast<CardId>(s.card_pool[s.hand[0]].card_id),
              expected_attack);
    if (prevented) {
        EXPECT_EQ(s.discard_count, 1);
        EXPECT_EQ(s.exhaust_count, 0);
    } else {
        EXPECT_EQ(s.discard_count, 0);
        EXPECT_EQ(s.exhaust_count, 1);
    }

    CombatState ordinary = MakeState();
    AddRelic(ordinary, RelicId::STRANGE_SPOON);
    const CardPoolIndex strike = AddCard(ordinary, CardId::STRIKE);
    ordinary.card_random_rng = from_seed(77);
    resolve_card_play(ordinary, CardQueueItem{strike, 0});
    EXPECT_EQ(ordinary.card_random_rng.counter, 0)
        << "a non-exhausting card consumes no Spoon draw";
}

TEST(RelicShopCards, OrangePelletsRemovesLiveDebuffsAtResolution) {
    CombatState s = MakeState();
    RelicSlot& pellets = AddRelic(s, RelicId::ORANGE_PELLETS);
    AddPower(s, kActorPlayer, PowerId::FRAIL, 2);
    AddPower(s, kActorPlayer, PowerId::WEAK, 2);
    AddPower(s, kActorPlayer, PowerId::STRENGTH, 3);
    // Construct the two private Java booleans whose POWER-card positive route
    // becomes public once B3.7 lands; playing an ATTACK completes the set.
    s.flags |= kCombatFlagOrangePelletsSkill |
               kCombatFlagOrangePelletsPower;
    dispatch_relics_on_use_card(
        s, s.relics, s.relic_count, static_cast<uint16_t>(CardId::STRIKE), 0);
    EXPECT_EQ(pellets.counter, -1);
    EXPECT_EQ(s.flags & kCombatFlagOrangePelletsMask, 0u);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(Queued(s).opcode, kOp(Opcode::REMOVE_POWER));
    Drain(s);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::FRAIL), nullptr);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::WEAK), nullptr);
    ASSERT_NE(FindPower(s, kActorPlayer, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::STRENGTH)->amount, 3);
}

TEST(RelicShopCombat, SlingOfCourageRequiresEliteRoomContext) {
    CombatState elite = MakeState();
    AddRelic(elite, RelicId::SLING_OF_COURAGE);
    elite.flags |= kCombatFlagElite;
    dispatch_relics_at_battle_start(
        elite, elite.relics, elite.relic_count);
    Drain(elite);
    ASSERT_NE(FindPower(elite, kActorPlayer, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(FindPower(elite, kActorPlayer, PowerId::STRENGTH)->amount, 2);

    CombatState normal = MakeState();
    AddRelic(normal, RelicId::SLING_OF_COURAGE);
    dispatch_relics_at_battle_start(
        normal, normal.relics, normal.relic_count);
    EXPECT_EQ(normal.action_count, 0);
}

TEST(RelicRareShopBoundaries, RegistryOnlyCrossDomainRowsAreCombatNoOps) {
    CombatState s = MakeState();
    // B4.5 reward: Prayer Wheel. B4.9 campfire: Peace Pipe/Shovel. B4.4 map:
    // Wing Boots. B4.8/shop choice/price: Cauldron, Dolly, Membership, Orrery.
    // B4.5 cross-color rewards: Prismatic Shard. Frozen Eye is observation-only
    // because draw order is already represented. Toolbox is the sole dependency
    // boundary: B3.10/B3.11 own its colorless rows and B4.5 owns choose-one-of-
    // three reward-style screen mechanics; its pre-draw hook is registered here.
    for (RelicId id :
         {RelicId::PEACE_PIPE, RelicId::PRAYER_WHEEL, RelicId::SHOVEL,
          RelicId::WING_BOOTS, RelicId::CAULDRON,
          RelicId::DOLLYS_MIRROR, RelicId::FROZEN_EYE,
          RelicId::MEMBERSHIP_CARD, RelicId::ORRERY,
          RelicId::PRISMATIC_SHARD, RelicId::TOOLBOX}) {
        AddRelic(s, id);
    }
    dispatch_relics_at_pre_battle(s, s.relics, s.relic_count);
    dispatch_relics_at_battle_start_pre_draw(
        s, s.relics, s.relic_count);
    dispatch_relics_at_battle_start(s, s.relics, s.relic_count);
    dispatch_relics_at_turn_start(s, s.relics, s.relic_count);
    dispatch_relics_at_turn_start_post_draw(
        s, s.relics, s.relic_count);
    dispatch_relics_on_victory(s, s.relics, s.relic_count);
    EXPECT_EQ(s.action_count, 0);
    EXPECT_EQ(s.player_hp, 70);
}

TEST(RelicRareShopBoundaries, BirdFacedUrnIgnoresNonPowerCards) {
    CombatState s = MakeState();
    RelicSlot& urn_slot = AddRelic(s, RelicId::BIRD_FACED_URN);
    AddRelic(s, RelicId::MAGIC_FLOWER);
    s.player_hp = 40;
    dispatch_relics_on_use_card(
        s, s.relics, s.relic_count, static_cast<uint16_t>(CardId::STRIKE), 0);
    EXPECT_EQ(s.player_hp, 40);
    EXPECT_EQ(s.action_count, 0);

    ActionQueueItem prior{};
    prior.opcode = kOp(Opcode::DRAW);
    add_to_bottom(s, prior);
    RelicHookContext power_ctx{};
    power_ctx.card_type = static_cast<uint8_t>(CardType::POWER);
    dispatch_native_relic_hook(s, RelicHook::ON_USE_CARD,
                               RelicId::BIRD_FACED_URN, urn_slot, power_ctx);
    EXPECT_EQ(s.player_hp, 40) << "Bird-Faced Urn queues rather than heals inline";
    ASSERT_EQ(s.action_count, 2);
    EXPECT_EQ(Queued(s, 0).opcode, kOp(Opcode::HEAL));
    EXPECT_EQ(Queued(s, 1).opcode, kOp(Opcode::DRAW));
    Drain(s);
    EXPECT_EQ(s.player_hp, 43) << "base heal 2 becomes 3 through Magic Flower";
    const RelicDef* urn = relic_def(RelicId::BIRD_FACED_URN);
    ASSERT_NE(urn, nullptr);
    EXPECT_NE(urn->hook_binding(
                  static_cast<sts::registry::RelicHook>(
                      RelicHook::ON_USE_CARD)),
              nullptr);
}

}  // namespace
}  // namespace sts::engine
