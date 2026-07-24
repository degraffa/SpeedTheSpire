// B3.6 Ironclad uncommon skills: exact 17-card source roster (the ledger's ~16
// plus Rage -- Rage.java:31 is CardType.SKILL), base/upgraded tier-2 behavior,
// No Draw gating, Dual Wield's DUPLICATE choice (forced/prompted/copies),
// Sentinel's on-exhaust energy, Infernal Blade's cardRandomRng accounting and
// this-turn-only cost, Entrench doubling, Flame Barrier thorns + removal,
// Second Wind exhaust-then-block, Spot Weakness intent gating, and a directed
// public advance()/legal_actions() script. Expected values are hand-computed
// from the cited decompiled Java (see registry/cards.yaml provenance).

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
#include "sts/engine/monster_dispatch.hpp"  // MonsterIntent (Spot Weakness)
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

CombatState MakeThree(int16_t energy = 6) {
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

const PowerSlot* FindPower(const CombatState& s, uint8_t actor, PowerId id) {
    const PowerSlot* slots = actor == kActorPlayer ? s.player_powers
                                                   : s.monsters[actor].powers;
    const uint8_t count = actor == kActorPlayer ? s.player_power_count
                                                : s.monsters[actor].power_count;
    for (uint8_t i = 0; i < count; ++i) {
        if (slots[i].power_id == static_cast<uint16_t>(id)) {
            return &slots[i];
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

void EndTurn(CombatState& s) {
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    pump(s, default_monster_turn);
}

StepResult Step(CombatState& s, Action a) {
    StepResult r{};
    advance(std::span<CombatState>(&s, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&r, 1));
    return r;
}

// ===========================================================================
// Registry roster + encodings
// ===========================================================================

TEST(CardUncommonSkillsRegistry, ExactSourceRosterAndStableIds) {
    // The 17-member roster enumerated from cards/red constructors
    // (CardRarity.UNCOMMON + CardType.SKILL), in addRedCards order. Includes
    // RAGE (Rage.java:31), which the ledger had misfiled under B3.7 powers.
    const std::array<CardId, 17> ids{
        CardId::BATTLE_TRANCE, CardId::BLOODLETTING, CardId::BURNING_PACT,
        CardId::DISARM,        CardId::DUAL_WIELD,   CardId::ENTRENCH,
        CardId::FLAME_BARRIER, CardId::GHOSTLY_ARMOR, CardId::INFERNAL_BLADE,
        CardId::INTIMIDATE,    CardId::POWER_THROUGH, CardId::RAGE,
        CardId::SECOND_WIND,   CardId::SEEING_RED,    CardId::SENTINEL,
        CardId::SHOCKWAVE,     CardId::SPOT_WEAKNESS};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        EXPECT_EQ(static_cast<int>(ids[i]), 51 + static_cast<int>(i));
        const CardDef* d = card_def(ids[i]);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(d->type, CardType::SKILL);
    }
    // Append-only additions: opcodes from 30 (25-29 reserved for B3.17's
    // monster-lifecycle opcodes on a sibling branch), powers from 24 (22-23
    // reserved for B3.17 SPLIT / B3.25 NEXT_TURN_BLOCK).
    EXPECT_EQ(static_cast<uint16_t>(Opcode::DOUBLE_BLOCK), 30);
    EXPECT_EQ(static_cast<uint16_t>(Opcode::BLOCK_PER_NON_ATTACK), 31);
    EXPECT_EQ(static_cast<uint16_t>(Opcode::SPOT_WEAKNESS), 32);
    EXPECT_EQ(static_cast<uint16_t>(Opcode::RANDOM_ATTACK_TO_HAND), 33);
    EXPECT_EQ(static_cast<uint16_t>(PowerId::NO_DRAW), 24);
    EXPECT_EQ(static_cast<uint16_t>(PowerId::FLAME_BARRIER), 25);
}

TEST(CardUncommonSkillsRegistry, DualWieldChooseFlagsAndSentinelProgramPins) {
    // gen.py <-> engine DUPLICATE packing pin: kind bit 2 at extra bit 3 (bit 2
    // stays RANDOM), copies-1 in bits 4-7 (default 0 keeps pre-B3.6 extras
    // byte-identical).
    const CardDef* dw = card_def(CardId::DUAL_WIELD);
    ASSERT_NE(dw, nullptr);
    EXPECT_EQ(static_cast<uint16_t>(dw->steps[0].op),
              static_cast<uint16_t>(Opcode::CHOOSE_CARD));
    EXPECT_EQ(dw->steps[0].extra,
              make_choose_flags(ChoiceKind::DUPLICATE, false, 1));
    EXPECT_EQ(dw->upgraded_steps[0].extra,
              make_choose_flags(ChoiceKind::DUPLICATE, false, 2));
    EXPECT_EQ(choose_kind_from_flags(dw->steps[0].extra), ChoiceKind::DUPLICATE);
    EXPECT_EQ(choose_copies_from_flags(dw->steps[0].extra), 1);
    EXPECT_EQ(choose_copies_from_flags(dw->upgraded_steps[0].extra), 2);
    // Pre-B3.6 kinds keep their packed bytes (Armaments UPGRADE == 2).
    const CardDef* arm = card_def(CardId::ARMAMENTS);
    ASSERT_NE(arm, nullptr);
    EXPECT_EQ(arm->steps[1].extra, 2u);

    // Sentinel's on-exhaust program rows (Sentinel.java:37-43): energy 2 / 3.
    const CardDef* sen = card_def(CardId::SENTINEL);
    ASSERT_NE(sen, nullptr);
    const CardEffectView base = card_on_exhaust_steps(*sen, 0);
    const CardEffectView up = card_on_exhaust_steps(*sen, 1);
    ASSERT_EQ(base.count, 1);
    ASSERT_EQ(up.count, 1);
    EXPECT_EQ(static_cast<uint16_t>(base.steps[0].op),
              static_cast<uint16_t>(Opcode::GAIN_ENERGY));
    EXPECT_EQ(base.steps[0].amount, 2);
    EXPECT_EQ(up.steps[0].amount, 3);
    // Every other B3.6 card has no on-exhaust program.
    EXPECT_EQ(card_on_exhaust_steps(*dw, 0).count, 0);
}

TEST(CardUncommonSkillsRegistry, IroncladAttackPoolMembership) {
    // returnTrulyRandomCardInCombat(ATTACK): RED C/U/R attacks minus HEALING.
    // With B3.8's three non-healing rares (Bludgeon/Fiend Fire/Immolate) not yet
    // landed, the derived pool holds the 14 commons + 11 uncommons; membership
    // self-completes from the color/rarity/type columns when B3.8 lands.
    EXPECT_EQ(kIroncladAttackPoolCount, 25);
    EXPECT_EQ(kIroncladAttackPool[0], CardId::POMMEL_STRIKE);  // lowest id (5)
    EXPECT_EQ(kIroncladAttackPool[24], CardId::WHIRLWIND);     // highest id (50)
    bool has_basic = false;
    bool has_skill = false;
    for (int i = 0; i < kIroncladAttackPoolCount; ++i) {
        const CardId id = kIroncladAttackPool[static_cast<unsigned>(i)];
        if (id == CardId::STRIKE || id == CardId::BASH || id == CardId::DEFEND) {
            has_basic = true;  // BASIC rarity never enters the srcX pools
        }
        const CardDef* d = card_def(id);
        ASSERT_NE(d, nullptr);
        if (d->type != CardType::ATTACK) {
            has_skill = true;
        }
    }
    EXPECT_FALSE(has_basic);
    EXPECT_FALSE(has_skill);
}

// ===========================================================================
// Battle Trance -- draw 3/4, then No Draw for the rest of the turn
// ===========================================================================

TEST(CardUncommonSkillsBattleTrance, DrawsThenBlocksFurtherDrawsUntilEndOfTurn) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::BATTLE_TRANCE);
    AddHand(s, CardId::BATTLE_TRANCE);
    for (int i = 0; i < 8; ++i) {
        AddDrawTop(s, CardId::STRIKE);
    }
    Play(s, 0);
    EXPECT_EQ(s.hand_count, 4) << "second Battle Trance + 3 drawn Strikes";
    const PowerSlot* nd = FindPower(s, kActorPlayer, PowerId::NO_DRAW);
    ASSERT_NE(nd, nullptr);
    EXPECT_EQ(nd->amount, -1) << "NoDrawPower's no-amount marker (ctor :27)";

    // The second Battle Trance: its DrawCardAction is gated off entirely
    // (DrawCardAction.java:69-73), and re-applying No Draw onto a target that
    // already has it is a whole-action no-op (ApplyPowerAction.java:102-105).
    Play(s, 0);
    EXPECT_EQ(s.hand_count, 3) << "No Draw: nothing drawn";
    nd = FindPower(s, kActorPlayer, PowerId::NO_DRAW);
    ASSERT_NE(nd, nullptr);
    EXPECT_EQ(nd->amount, -1) << "no stacking on re-application";

    // atEndOfTurn removes No Draw (NoDrawPower.java:33-37), so the next
    // start-of-turn draw is a full 5.
    EndTurn(s);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::NO_DRAW), nullptr);
    EXPECT_EQ(s.hand_count, 5) << "next turn draws normally";
}

TEST(CardUncommonSkillsBattleTrance, UpgradedDrawsFour) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::BATTLE_TRANCE, 1);
    for (int i = 0; i < 6; ++i) {
        AddDrawTop(s, CardId::STRIKE);
    }
    Play(s, 0);
    EXPECT_EQ(s.hand_count, 4);
    EXPECT_NE(FindPower(s, kActorPlayer, PowerId::NO_DRAW), nullptr);
}

// ===========================================================================
// Bloodletting -- lose 3 HP, gain 2/3 energy
// ===========================================================================

TEST(CardUncommonSkillsBloodletting, BaseAndUpgradedEnergyRows) {
    CombatState b = MakeCombat(3);
    AddHand(b, CardId::BLOODLETTING);
    Play(b);
    EXPECT_EQ(b.player_hp, 77) << "fixed 3 HP loss (LoseHPAction, bypasses block)";
    EXPECT_EQ(b.player_energy, 5) << "cost 0, +2 energy";

    CombatState u = MakeCombat(3);
    AddHand(u, CardId::BLOODLETTING, 1);
    Play(u);
    EXPECT_EQ(u.player_hp, 77);
    EXPECT_EQ(u.player_energy, 6) << "upgraded +3 energy";
}

// ===========================================================================
// Burning Pact -- exhaust one chosen card, then draw 2/3
// ===========================================================================

TEST(CardUncommonSkillsBurningPact, PromptedExhaustThenDraw) {
    CombatState s = MakeCombat(3);
    AddHand(s, CardId::BURNING_PACT);
    const CardPoolIndex strike = AddHand(s, CardId::STRIKE);
    const CardPoolIndex defend = AddHand(s, CardId::DEFEND);
    AddDrawTop(s, CardId::DEFEND);
    AddDrawTop(s, CardId::DEFEND);

    Play(s, 0);
    ASSERT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    ActionMask mask{};
    legal_actions(s, mask);
    ASSERT_TRUE(mask.choice_pending);
    EXPECT_TRUE(mask.can_choose[0]);
    EXPECT_TRUE(mask.can_choose[1]);
    EXPECT_FALSE(mask.can_end_turn);

    Step(s, make_action(ActionVerb::CHOOSE, 0));  // exhaust the Strike
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, strike));
    EXPECT_TRUE(PileHas(s.hand, s.hand_count, defend));
    EXPECT_EQ(s.hand_count, 3) << "Defend + the 2 drawn cards";
    EXPECT_EQ(s.player_energy, 2);
}

TEST(CardUncommonSkillsBurningPact, ForcedExhaustFiresSentinelEnergyBeforeDraw) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat(3);
        AddHand(s, CardId::BURNING_PACT);
        const CardPoolIndex sen = AddHand(s, CardId::SENTINEL, up);
        AddDrawTop(s, CardId::STRIKE);
        AddDrawTop(s, CardId::STRIKE);
        Play(s, 0);
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, sen))
            << "single hand card -> forced exhaust (ExhaustAction no-screen)";
        // 3 - 1 (cost) + 2/3 (Sentinel.triggerOnExhaust, Sentinel.java:37-43).
        EXPECT_EQ(s.player_energy, up == 0 ? 4 : 5);
        EXPECT_EQ(s.hand_count, 2) << "then draw 2";
    }
}

TEST(CardUncommonSkillsBurningPact, UpgradedDrawsThree) {
    CombatState s = MakeCombat(3);
    AddHand(s, CardId::BURNING_PACT, 1);
    AddHand(s, CardId::STRIKE);
    for (int i = 0; i < 3; ++i) {
        AddDrawTop(s, CardId::DEFEND);
    }
    Play(s, 0);  // forced: only the Strike is exhaustable
    EXPECT_EQ(s.exhaust_count, 1);
    EXPECT_EQ(s.hand_count, 3);
}

// ===========================================================================
// Disarm -- enemy loses 2/3 Strength; exhausts
// ===========================================================================

TEST(CardUncommonSkillsDisarm, AppliesNegativeStrengthAndExhausts) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        const CardPoolIndex pi = AddHand(s, CardId::DISARM, up);
        Play(s, 0, 0);
        const PowerSlot* str = FindPower(s, 0, PowerId::STRENGTH);
        ASSERT_NE(str, nullptr);
        EXPECT_EQ(str->amount, up == 0 ? -2 : -3);
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, pi));
        EXPECT_EQ(s.player_energy, 5);
    }
}

TEST(CardUncommonSkillsDisarm, StackingToExactlyZeroRemovesStrengthSlot) {
    // StrengthPower.stackPower:48-53 -- a stack landing on exactly 0 queues the
    // slot's removal (addToTop RemoveSpecificPowerAction).
    CombatState s = MakeCombat();
    AddPower(s, 0, PowerId::STRENGTH, 2);
    AddHand(s, CardId::DISARM);
    Play(s, 0, 0);
    EXPECT_EQ(FindPower(s, 0, PowerId::STRENGTH), nullptr);
}

TEST(CardUncommonSkillsDisarm, ArtifactNullifiesNegativeStrength) {
    // A negative-amount StrengthPower constructs as a DEBUFF (updateDescription
    // :81-89 via ctor :37), so the Artifact interception consumes it.
    CombatState s = MakeCombat();
    AddPower(s, 0, PowerId::ARTIFACT, 1);
    AddHand(s, CardId::DISARM);
    Play(s, 0, 0);
    EXPECT_EQ(FindPower(s, 0, PowerId::STRENGTH), nullptr);
    const PowerSlot* art = FindPower(s, 0, PowerId::ARTIFACT);
    ASSERT_NE(art, nullptr);
    EXPECT_EQ(art->amount, 0) << "one Artifact charge consumed";
}

// ===========================================================================
// Dual Wield -- duplicate an Attack (or Power) card
// ===========================================================================

TEST(CardUncommonSkillsDualWield, ForcedSingleEligibleClonesWithoutReorder) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::DUAL_WIELD);
    const CardPoolIndex strike = AddHand(s, CardId::STRIKE);
    const CardPoolIndex defend = AddHand(s, CardId::DEFEND);
    Play(s, 0);
    ASSERT_EQ(s.hand_count, 3) << "net +1 clone";
    EXPECT_EQ(s.hand[0], strike) << "forced branch: no reorder (:49-57)";
    EXPECT_EQ(s.hand[1], defend);
    EXPECT_EQ(s.card_pool[s.hand[2]].card_id,
              static_cast<uint16_t>(CardId::STRIKE));
    EXPECT_NE(s.hand[2], strike) << "a fresh pool instance";
    EXPECT_EQ(s.player_energy, 5);
}

TEST(CardUncommonSkillsDualWield, NoEligibleCardDoesNothing) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::DUAL_WIELD);
    const CardPoolIndex defend = AddHand(s, CardId::DEFEND);
    Play(s, 0);
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], defend);
    ActionMask mask{};
    legal_actions(s, mask);
    EXPECT_FALSE(mask.choice_pending) << "zero eligible -> no prompt";
}

TEST(CardUncommonSkillsDualWield, PromptedSelectionReordersAndPreservesStats) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::DUAL_WIELD);
    const CardPoolIndex sb = AddHand(s, CardId::SEARING_BLOW, /*upgrade=*/2);
    const CardPoolIndex defend = AddHand(s, CardId::DEFEND);
    const CardPoolIndex pummel = AddHand(s, CardId::PUMMEL);

    Play(s, 0);
    ASSERT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    ActionMask mask{};
    legal_actions(s, mask);
    ASSERT_TRUE(mask.choice_pending);
    EXPECT_TRUE(mask.can_choose[0]);   // Searing Blow (ATTACK)
    EXPECT_FALSE(mask.can_choose[1]);  // Defend (SKILL -- not dual-wieldable)
    EXPECT_TRUE(mask.can_choose[2]);   // Pummel (ATTACK)

    Step(s, make_action(ActionVerb::CHOOSE, 0));  // duplicate the Searing Blow
    // DualWieldAction's screen bookkeeping (:59-92): [other eligibles] +
    // [ineligibles] + [selected + clone].
    ASSERT_EQ(s.hand_count, 4);
    EXPECT_EQ(s.hand[0], pummel);
    EXPECT_EQ(s.hand[1], defend);
    EXPECT_EQ(s.hand[2], sb);
    const CardInstance& clone = s.card_pool[s.hand[3]];
    EXPECT_EQ(clone.card_id, static_cast<uint16_t>(CardId::SEARING_BLOW));
    EXPECT_EQ(clone.upgrade, 2)
        << "makeStatEquivalentCopy preserves timesUpgraded (:826-848)";
    EXPECT_NE(s.hand[3], sb);
}

TEST(CardUncommonSkillsDualWield, UpgradedMakesTwoCopies) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::DUAL_WIELD, 1);
    const CardPoolIndex strike = AddHand(s, CardId::STRIKE);
    Play(s, 0);
    ASSERT_EQ(s.hand_count, 3) << "net +2 clones (magicNumber 2)";
    EXPECT_EQ(s.hand[0], strike);
    EXPECT_EQ(s.card_pool[s.hand[1]].card_id,
              static_cast<uint16_t>(CardId::STRIKE));
    EXPECT_EQ(s.card_pool[s.hand[2]].card_id,
              static_cast<uint16_t>(CardId::STRIKE));
}

// ===========================================================================
// Entrench -- double current block; cost 2 -> 1 on upgrade
// ===========================================================================

TEST(CardUncommonSkillsEntrench, DoublesBlockAndZeroBlockDoesNothing) {
    const CardDef* d = card_def(CardId::ENTRENCH);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(card_cost(*d, 0), 2);
    EXPECT_EQ(card_cost(*d, 1), 1);

    CombatState s = MakeCombat();
    s.player_block = 10;
    AddHand(s, CardId::ENTRENCH);
    Play(s);
    EXPECT_EQ(s.player_block, 20) << "addBlock(currentBlock)";
    EXPECT_EQ(s.player_energy, 4);

    CombatState z = MakeCombat();
    AddHand(z, CardId::ENTRENCH, 1);
    Play(z);
    EXPECT_EQ(z.player_block, 0) << "currentBlock 0 -> nothing (:25-28)";
    EXPECT_EQ(z.player_energy, 5) << "upgraded cost 1";
}

// ===========================================================================
// Flame Barrier -- block + thorns-on-attack power, gone next turn
// ===========================================================================

TEST(CardUncommonSkillsFlameBarrier, BlocksAndReflectsEvenWhenFullyBlocked) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::FLAME_BARRIER);
    AddPower(s, 0, PowerId::VULNERABLE, 2);  // must NOT amplify THORNS reflect
    Play(s);
    EXPECT_EQ(s.player_block, 12);
    const PowerSlot* fb = FindPower(s, kActorPlayer, PowerId::FLAME_BARRIER);
    ASSERT_NE(fb, nullptr);
    EXPECT_EQ(fb->amount, 4);

    // Monster attack for 5: fully blocked, yet the reflect still fires
    // (onAttacked dispatches after decrementBlock regardless of penetration).
    ActionQueueItem hit{};
    hit.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    hit.src = 0;
    hit.tgt = kActorPlayer;
    hit.amount = 5;
    execute_opcode(s, hit);
    pump(s, default_monster_turn);
    EXPECT_EQ(s.player_hp, 80);
    EXPECT_EQ(s.player_block, 7);
    EXPECT_EQ(s.monsters[0].hp, 96)
        << "4 THORNS, not scaled by the attacker's Vulnerable (NORMAL-only hook)";
}

TEST(CardUncommonSkillsFlameBarrier, StacksAndExpiresAtNextTurnStart) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::FLAME_BARRIER);
    AddHand(s, CardId::FLAME_BARRIER, 1);
    for (int i = 0; i < 5; ++i) {
        AddDrawTop(s, CardId::STRIKE);
    }
    Play(s, 0);
    Play(s, 0);
    EXPECT_EQ(s.player_block, 28) << "12 + 16";
    const PowerSlot* fb = FindPower(s, kActorPlayer, PowerId::FLAME_BARRIER);
    ASSERT_NE(fb, nullptr);
    EXPECT_EQ(fb->amount, 10) << "4 + 6 (stackPower additive, :42-50)";

    EndTurn(s);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::FLAME_BARRIER), nullptr)
        << "atStartOfTurn self-removal (:62-64)";
}

// ===========================================================================
// Ghostly Armor -- ethereal block
// ===========================================================================

TEST(CardUncommonSkillsGhostlyArmor, EtherealBlockRows) {
    const CardDef* d = card_def(CardId::GHOSTLY_ARMOR);
    ASSERT_NE(d, nullptr);
    EXPECT_TRUE(has_card_flag(card_flags(*d, 0), CardFlag::ETHEREAL));
    CombatState b = MakeCombat();
    AddHand(b, CardId::GHOSTLY_ARMOR);
    Play(b);
    EXPECT_EQ(b.player_block, 10);
    CombatState u = MakeCombat();
    AddHand(u, CardId::GHOSTLY_ARMOR, 1);
    Play(u);
    EXPECT_EQ(u.player_block, 13);
}

TEST(CardUncommonSkillsGhostlyArmor, ExhaustsIfUnplayedAtEndOfTurn) {
    CombatState s = MakeCombat();
    const CardPoolIndex pi = AddHand(s, CardId::GHOSTLY_ARMOR);
    EndTurn(s);
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, pi));
}

// ===========================================================================
// Infernal Blade -- random attack to hand, costs 0 this turn
// ===========================================================================

TEST(CardUncommonSkillsInfernalBlade, OneDrawCostZeroThisTurnAndReset) {
    CombatState s = MakeCombat();
    const CardPoolIndex ib = AddHand(s, CardId::INFERNAL_BLADE);
    s.card_random_rng = from_seed(42);
    RngStream probe = from_seed(42);
    const int32_t idx = random(probe, kIroncladAttackPoolCount - 1);
    const int32_t before = s.card_random_rng.counter;

    Play(s, 0);
    EXPECT_EQ(s.card_random_rng.counter, before + 1)
        << "returnTrulyRandomCardInCombat: exactly ONE cardRandomRng draw";
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, ib));
    ASSERT_EQ(s.hand_count, 1);
    const CardPoolIndex gen = s.hand[0];
    const CardInstance& c = s.card_pool[gen];
    EXPECT_EQ(c.card_id, static_cast<uint16_t>(
                             kIroncladAttackPool[static_cast<unsigned>(idx)]));
    EXPECT_EQ(c.upgrade, 0) << "library copies are base";
    EXPECT_EQ(c.cost_now, 0) << "setCostForTurn(0)";
    EXPECT_TRUE(has_card_flag(c.flags, CardFlag::COST_MODIFIED_FOR_TURN));
    const CardDef* gd = card_def(static_cast<CardId>(c.card_id));
    ASSERT_NE(gd, nullptr);
    EXPECT_EQ(gd->type, CardType::ATTACK);

    // Unplayed: at end of turn its costForTurn reverts (AbstractRoom.endTurn:
    // 397-405 resets every draw/discard/hand card).
    EndTurn(s);
    EXPECT_EQ(s.card_pool[gen].cost_now, card_cost(*gd, 0));
    EXPECT_FALSE(
        has_card_flag(s.card_pool[gen].flags, CardFlag::COST_MODIFIED_FOR_TURN));
}

TEST(CardUncommonSkillsInfernalBlade, UpgradedCostsZero) {
    const CardDef* d = card_def(CardId::INFERNAL_BLADE);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(card_cost(*d, 0), 1);
    EXPECT_EQ(card_cost(*d, 1), 0);
    EXPECT_TRUE(has_card_flag(card_flags(*d, 1), CardFlag::EXHAUST));
}

// ===========================================================================
// Intimidate -- Weak 1/2 to all (live) enemies; exhausts; free
// ===========================================================================

TEST(CardUncommonSkillsIntimidate, WeakensAllLivingEnemies) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeThree();
        s.monsters[2].hp = 0;  // dead monsters take no ApplyPowerAction
        const CardPoolIndex pi = AddHand(s, CardId::INTIMIDATE, up);
        Play(s, 0);
        const int16_t want = up == 0 ? 1 : 2;
        for (uint8_t m = 0; m < 2; ++m) {
            const PowerSlot* weak = FindPower(s, m, PowerId::WEAK);
            ASSERT_NE(weak, nullptr);
            EXPECT_EQ(weak->amount, want);
        }
        EXPECT_EQ(FindPower(s, 2, PowerId::WEAK), nullptr);
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, pi));
        EXPECT_EQ(s.player_energy, 6) << "cost 0";
    }
}

// ===========================================================================
// Power Through -- 2 Wounds to hand, then 15/20 block
// ===========================================================================

TEST(CardUncommonSkillsPowerThrough, WoundsThenBlock) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::POWER_THROUGH, up);
        Play(s);
        EXPECT_EQ(s.player_block, up == 0 ? 15 : 20);
        ASSERT_EQ(s.hand_count, 2);
        EXPECT_EQ(s.card_pool[s.hand[0]].card_id,
                  static_cast<uint16_t>(CardId::WOUND));
        EXPECT_EQ(s.card_pool[s.hand[1]].card_id,
                  static_cast<uint16_t>(CardId::WOUND));
    }
}

// ===========================================================================
// Rage -- block per Attack played this turn; expires at end of turn
// ===========================================================================

TEST(CardUncommonSkillsRage, BlocksPerAttackAndExpires) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::RAGE);
    AddHand(s, CardId::STRIKE);
    AddHand(s, CardId::DEFEND);
    Play(s, 0);  // Rage: cost 0
    const PowerSlot* rage = FindPower(s, kActorPlayer, PowerId::RAGE);
    ASSERT_NE(rage, nullptr);
    EXPECT_EQ(rage->amount, 3);

    Play(s, 0, 0);  // Strike: an ATTACK -> +3 block after its damage
    EXPECT_EQ(s.monsters[0].hp, 94);
    EXPECT_EQ(s.player_block, 3);

    Play(s, 0);  // Defend: a SKILL -> no Rage block (RagePower.java:42)
    EXPECT_EQ(s.player_block, 8) << "5 Defend block only";

    EndTurn(s);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::RAGE), nullptr)
        << "atEndOfTurn self-removal (RagePower.java:49-52)";
}

TEST(CardUncommonSkillsRage, UpgradedAppliesFive) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::RAGE, 1);
    Play(s);
    const PowerSlot* rage = FindPower(s, kActorPlayer, PowerId::RAGE);
    ASSERT_NE(rage, nullptr);
    EXPECT_EQ(rage->amount, 5);
}

TEST(CardUncommonSkillsRage, BodySlamReadsBlockBeforeRageBlockLands) {
    // The game computes Body Slam's base at use() while Rage's GainBlock rides
    // the LATER UseCardAction -- so a 0-block Body Slam under Rage deals 0 and
    // only then gains the block (the reason RAGE binds ON_USE_CARD, not
    // ON_PLAY_CARD).
    CombatState s = MakeCombat();
    AddPower(s, kActorPlayer, PowerId::RAGE, 3);
    AddHand(s, CardId::BODY_SLAM);
    Play(s, 0, 0);
    EXPECT_EQ(s.monsters[0].hp, 100) << "base == block == 0 at execute";
    EXPECT_EQ(s.player_block, 3) << "Rage block lands after the attack";
}

// ===========================================================================
// Second Wind -- exhaust every non-Attack, 5/7 block per card
// ===========================================================================

TEST(CardUncommonSkillsSecondWind, ExhaustsNonAttacksAndBlocksPerCard) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::SECOND_WIND, up);
        const CardPoolIndex strike = AddHand(s, CardId::STRIKE);
        const CardPoolIndex defend = AddHand(s, CardId::DEFEND);
        const CardPoolIndex dazed = AddHand(s, CardId::DAZED);
        Play(s, 0);
        EXPECT_TRUE(PileHas(s.hand, s.hand_count, strike));
        EXPECT_EQ(s.hand_count, 1);
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, defend));
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, dazed));
        EXPECT_EQ(s.player_block, up == 0 ? 10 : 14) << "2 cards x 5/7";
    }
}

TEST(CardUncommonSkillsSecondWind, DexterityAppliesPerCardAndSentinelFires) {
    CombatState s = MakeCombat();
    AddPower(s, kActorPlayer, PowerId::DEXTERITY, 2);
    AddHand(s, CardId::SECOND_WIND);
    const CardPoolIndex sen = AddHand(s, CardId::SENTINEL);
    const CardPoolIndex defend = AddHand(s, CardId::DEFEND);
    Play(s, 0);
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, sen));
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, defend));
    // this.block is applyPowers block -> Dexterity counts once PER card gain.
    EXPECT_EQ(s.player_block, 14) << "2 x (5 + 2 Dexterity)";
    EXPECT_EQ(s.player_energy, 7) << "6 - 1 + Sentinel's 2 (exhausted mid-sweep)";
}

// ===========================================================================
// Seeing Red -- +2 energy; exhausts; cost 1 -> 0
// ===========================================================================

TEST(CardUncommonSkillsSeeingRed, EnergyRows) {
    CombatState b = MakeCombat(3);
    const CardPoolIndex bp = AddHand(b, CardId::SEEING_RED);
    Play(b);
    EXPECT_EQ(b.player_energy, 4) << "3 - 1 + 2";
    EXPECT_TRUE(PileHas(b.exhaust, b.exhaust_count, bp));

    CombatState u = MakeCombat(3);
    AddHand(u, CardId::SEEING_RED, 1);
    Play(u);
    EXPECT_EQ(u.player_energy, 5) << "3 - 0 + 2";
}

// ===========================================================================
// Sentinel -- 5/8 block (the on-exhaust energy is covered above)
// ===========================================================================

TEST(CardUncommonSkillsSentinel, BlockRows) {
    CombatState b = MakeCombat();
    AddHand(b, CardId::SENTINEL);
    Play(b);
    EXPECT_EQ(b.player_block, 5);
    CombatState u = MakeCombat();
    AddHand(u, CardId::SENTINEL, 1);
    Play(u);
    EXPECT_EQ(u.player_block, 8);
}

// ===========================================================================
// Shockwave -- Weak 3/5 + Vulnerable 3/5 to ALL enemies; exhausts
// ===========================================================================

TEST(CardUncommonSkillsShockwave, DebuffsAllEnemiesAndExhausts) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeThree();
        const CardPoolIndex pi = AddHand(s, CardId::SHOCKWAVE, up);
        Play(s, 0);
        const int16_t want = up == 0 ? 3 : 5;
        for (uint8_t m = 0; m < 3; ++m) {
            const PowerSlot* weak = FindPower(s, m, PowerId::WEAK);
            const PowerSlot* vuln = FindPower(s, m, PowerId::VULNERABLE);
            ASSERT_NE(weak, nullptr);
            ASSERT_NE(vuln, nullptr);
            EXPECT_EQ(weak->amount, want);
            EXPECT_EQ(vuln->amount, want);
        }
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, pi));
        EXPECT_EQ(s.player_energy, 4);
    }
}

// ===========================================================================
// Spot Weakness -- Strength 3/4 if the target intends to attack
// ===========================================================================

TEST(CardUncommonSkillsSpotWeakness, StrengthOnlyAgainstAttackIntents) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        s.monsters[0].intent = static_cast<uint8_t>(MonsterIntent::ATTACK_DEFEND);
        AddHand(s, CardId::SPOT_WEAKNESS, up);
        Play(s, 0, 0);
        const PowerSlot* str = FindPower(s, kActorPlayer, PowerId::STRENGTH);
        ASSERT_NE(str, nullptr);
        EXPECT_EQ(str->amount, up == 0 ? 3 : 4);
    }

    CombatState n = MakeCombat();
    n.monsters[0].intent = static_cast<uint8_t>(MonsterIntent::DEFEND_BUFF);
    AddHand(n, CardId::SPOT_WEAKNESS);
    Play(n, 0, 0);
    EXPECT_EQ(FindPower(n, kActorPlayer, PowerId::STRENGTH), nullptr)
        << "getIntentBaseDmg() < 0 -> nothing (SpotWeaknessAction.java:34-38)";
}

// ===========================================================================
// Directed public-API script (advance()/legal_actions() only)
// ===========================================================================

TEST(CardUncommonSkillsDirected, RageSecondWindSentinelStrikeScript) {
    CombatState s = MakeCombat(/*energy=*/3, /*monster_hp=*/100);
    AddHand(s, CardId::RAGE);         // slot 0
    AddHand(s, CardId::SECOND_WIND);  // slot 1
    AddHand(s, CardId::SENTINEL);     // slot 2
    AddHand(s, CardId::STRIKE);       // slot 3

    ActionMask mask{};
    legal_actions(s, mask);
    ASSERT_TRUE(mask.can_play[0]);

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));  // Rage (free)
    EXPECT_EQ(s.player_energy, 3);
    ASSERT_NE(FindPower(s, kActorPlayer, PowerId::RAGE), nullptr);

    // Second Wind: exhausts the Sentinel (a SKILL) -> +2 energy, +5 block; the
    // Strike (an ATTACK) stays in hand. Rage does NOT trigger (a SKILL play).
    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));
    EXPECT_EQ(s.player_energy, 4) << "3 - 1 + Sentinel's 2";
    EXPECT_EQ(s.player_block, 5);
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.card_pool[s.hand[0]].card_id,
              static_cast<uint16_t>(CardId::STRIKE));

    legal_actions(s, mask);
    ASSERT_TRUE(mask.can_play[0]);
    ASSERT_TRUE(mask.can_play_target[0][0]);
    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));  // Strike: Rage fires
    EXPECT_EQ(s.monsters[0].hp, 94);
    EXPECT_EQ(s.player_block, 8) << "5 + Rage 3 (after the damage)";
    EXPECT_EQ(s.player_energy, 3);
}

}  // namespace
}  // namespace sts::engine
