// B3.1 interpreter / card-mechanics extensions (tier-2, constructed states).
//
// Each mechanic added by B3.1 gets a directed test whose expected values are
// hand-computed from the cited Java, NOT read back from the implementation:
//   * ALL_ENEMY targeting     -- AoE fans out over LIVE monsters only, computing
//                                a separate DamageInfo per target
//                                (DamageAllEnemiesAction.update:56-83).
//   * RANDOM_ENEMY (multi-hit) -- one card_random_rng draw per resolved hit,
//                                excluding dead monsters
//                                (AttackDamageRandomEnemyAction.update).
//   * X-cost                  -- consumes ALL energy, repeats the program
//                                energyOnUse times (WhirlwindAction.update).
//   * flag bits               -- EXHAUST routes the played card to exhaust;
//                                UNPLAYABLE bars it from legal_actions.
//   * MAKE_CARD               -- card creation into hand/discard/draw, incl. the
//                                hand-full -> discard spill
//                                (MakeTempCardInHandAction.update:71-77), the
//                                random draw-pile spot (CardGroup.addToRandomSpot)
//                                and ShowCardAndAddToHandEffect's Corruption
//                                sweep (AbstractPlayer.onCardDrawOrDiscard).
//   * SET_COST                -- the cost_now write primitive (clamped to u8).
//   * upgrade plumbing        -- card_effect_steps/card_cost/card_flags select
//                                base vs. upgraded by the CardInstance.upgrade bit.

#include <cstdint>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/piles.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

// A minimal combat with `id` (cost `cost`) as the sole hand card (pool index 0),
// one target monster, player-turn invariant set (a post-play pump resolves the
// card and returns to WAITING_ON_USER without a monster turn). Mirrors
// cards_test.cpp's MakeCardTestState.
CombatState MakeState(CardId id, uint8_t cost, int16_t energy = 3,
                      int16_t monster_hp = 50) {
    CombatState s{};
    s.card_pool[0].card_id = static_cast<uint16_t>(id);
    s.card_pool[0].cost_now = cost;
    s.hand[0] = 0;
    s.hand_count = 1;
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.player_energy = energy;
    s.monster_count = 1;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = monster_hp;
    s.monsters[0].max_hp = monster_hp;
    s.monster_attacks_queued = 1;
    s.turn_has_ended = 0;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    return s;
}

ActionQueueItem MakeItem(Opcode op, uint8_t tgt, int32_t amount,
                         uint8_t src = kActorPlayer, uint32_t flags = 0) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(op);
    it.src = src;
    it.tgt = tgt;
    it.amount = amount;
    it.flags = flags;
    return it;
}

// --- ALL_ENEMY targeting ----------------------------------------------------

TEST(CardExtAoE, AllEnemiesHitsOnlyLiveMonsters) {
    CombatState s{};
    s.monster_count = 3;
    s.monsters[0].hp = 10;
    s.monsters[1].hp = 0;   // dead -> skipped (isDeadOrEscaped)
    s.monsters[2].hp = 7;

    execute_opcode(s, MakeItem(Opcode::DAMAGE, kActorAllEnemies, 5));

    EXPECT_EQ(s.monsters[0].hp, 5);   // 10 - 5
    EXPECT_EQ(s.monsters[1].hp, 0);   // dead, never touched
    EXPECT_EQ(s.monsters[2].hp, 2);   // 7 - 5
}

TEST(CardExtAoE, AllEnemiesComputesADamageInfoPerTarget) {
    // Vulnerable on monster 0 only: the AoE must apply x1.5 to it and not to the
    // other (proving a separate DamageInfo/multiDamage per target, not one
    // shared number).
    CombatState s{};
    s.monster_count = 2;
    s.monsters[0].hp = 30;
    s.monsters[0].powers[0].power_id = static_cast<uint16_t>(PowerId::VULNERABLE);
    s.monsters[0].powers[0].amount = 2;
    s.monsters[0].power_count = 1;
    s.monsters[1].hp = 30;

    execute_opcode(s, MakeItem(Opcode::DAMAGE, kActorAllEnemies, 10));

    EXPECT_EQ(s.monsters[0].hp, 30 - 15);  // 10 * 1.5 (Vulnerable)
    EXPECT_EQ(s.monsters[1].hp, 30 - 10);  // 10 flat
}

// --- RANDOM_ENEMY (per-hit roll) --------------------------------------------

TEST(CardExtRandom, RandomEnemyRollsOncePerHitAndExcludesDead) {
    CombatState s{};
    s.monster_count = 3;
    s.monsters[0].hp = 100;
    s.monsters[1].hp = 0;    // dead: never a valid random target
    s.monsters[2].hp = 100;
    s.card_random_rng = from_seed(555);

    const ActionQueueItem hit = MakeItem(Opcode::DAMAGE, kActorRandomEnemy, 1);
    for (int i = 0; i < 10; ++i) {
        const int32_t before = s.card_random_rng.counter;
        execute_opcode(s, hit);
        EXPECT_EQ(s.card_random_rng.counter, before + 1)
            << "each resolved random hit consumes exactly one card_random_rng draw";
    }

    EXPECT_EQ(s.monsters[1].hp, 0) << "the dead monster is never hit";
    // 10 hits of 1 damage landed on the two live monsters.
    EXPECT_EQ(s.monsters[0].hp + s.monsters[2].hp, 200 - 10);
}

// --- X-cost -----------------------------------------------------------------

TEST(CardExtXCost, ConsumesAllEnergyAndRepeatsProgram) {
    // A card flagged X-cost repeats its whole program energyOnUse times and
    // zeroes energy (WhirlwindAction). Exercised on a real Strike instance
    // (DAMAGE 6) flagged X-cost with cost_now 0 (as gen maps `cost: -1`): energy
    // 3 -> 3 repeats -> 18 damage, energy 0.
    CombatState s = MakeState(CardId::STRIKE, /*cost=*/0, /*energy=*/3);
    s.card_pool[0].flags = card_flag_bit(CardFlag::XCOST);

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);

    EXPECT_EQ(s.player_energy, 0);          // all energy spent
    EXPECT_EQ(s.monsters[0].hp, 50 - 18);   // DAMAGE 6 x 3
    EXPECT_EQ(s.discard_count, 1);          // still discarded (no exhaust flag)
    EXPECT_EQ(s.cards_played_this_turn, 1);
}

TEST(CardExtXCost, ZeroEnergyPlaysButDoesNothing) {
    CombatState s = MakeState(CardId::STRIKE, /*cost=*/0, /*energy=*/0);
    s.card_pool[0].flags = card_flag_bit(CardFlag::XCOST);

    // X-cost cards carry cost_now 0, so they are affordable even at 0 energy
    // (the game's costForTurn == -1, energy >= -1).
    ActionMask mask{};
    legal_actions(s, mask);
    EXPECT_TRUE(mask.can_play[0]);

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.player_energy, 0);
    EXPECT_EQ(s.monsters[0].hp, 50);        // 0 repeats
    EXPECT_EQ(s.discard_count, 1);
}

// --- Flag bits: exhaust routing + unplayable gating -------------------------

TEST(CardExtFlags, ExhaustFlagRoutesPlayedCardToExhaust) {
    CombatState s = MakeState(CardId::STRIKE, 1);
    s.card_pool[0].flags = card_flag_bit(CardFlag::EXHAUST);

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);

    EXPECT_EQ(s.monsters[0].hp, 50 - 6);    // effect still resolves
    ASSERT_EQ(s.exhaust_count, 1);
    EXPECT_EQ(s.exhaust[0], 0);             // to exhaust
    EXPECT_EQ(s.discard_count, 0);          // NOT discard
    EXPECT_EQ(s.player_energy, 3 - 1);
}

TEST(CardExtFlags, UnplayableFlagBarsLegalAction) {
    CombatState s = MakeState(CardId::STRIKE, 1);

    ActionMask mask{};
    legal_actions(s, mask);
    EXPECT_TRUE(mask.can_play[0]) << "affordable, playable by default";

    s.card_pool[0].flags = card_flag_bit(CardFlag::UNPLAYABLE);
    legal_actions(s, mask);
    EXPECT_FALSE(mask.can_play[0]) << "unplayable regardless of energy";
}

// --- MAKE_CARD --------------------------------------------------------------

TEST(CardExtMakeCard, IntoHandWhenRoom) {
    CombatState s{};
    s.card_pool[0].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.card_pool[1].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.hand[0] = 0;
    s.hand[1] = 1;
    s.hand_count = 2;

    execute_opcode(s, MakeItem(Opcode::MAKE_CARD, kActorPlayer, /*count=*/2,
                               static_cast<uint8_t>(CardPile::HAND),
                               make_make_card_flags(
                                   static_cast<uint16_t>(CardId::STRIKE))));

    EXPECT_EQ(s.hand_count, 4);
    EXPECT_EQ(s.discard_count, 0);
    // The two created cards took fresh pool rows and are STRIKE with base cost.
    for (uint8_t i = 2; i < 4; ++i) {
        const CardPoolIndex idx = s.hand[i];
        EXPECT_EQ(s.card_pool[idx].card_id, static_cast<uint16_t>(CardId::STRIKE));
        EXPECT_EQ(s.card_pool[idx].cost_now, 1);   // Strike base cost
    }
}

// A card created INTO THE HAND goes through ShowCardAndAddToHandEffect
// (ShowCardAndAddToHandEffect.java:26-51 / :53-73): hand.addToHand(card) (:43 /
// :65) and then AbstractPlayer.onCardDrawOrDiscard() (:47 / :69), whose
// Corruption branch (AbstractPlayer.java:1348-1352) calls modifyCostForCombat(-9)
// on every SKILL in hand whose costForTurn != 0. On a costForTurn > 0 card that
// takes modifyCostForCombat's FIRST arm (AbstractCard.java:2013-2022), which
// ends `this.cost = this.costForTurn` -- so BOTH fields land on 0 and the
// created skill is 0-cost for the COMBAT, not for this turn. The constructor's
// own trailing `setCostForTurn(-9)` (:48-50 / :70-72) then finds costForTurn
// already 0 and changes nothing.
//
// Witness: capture s2v3_wave1_STS239327_ps13, floor 50, the Act-3 double boss --
// pre-fix the sim's hand carried Armaments@1 and Ghostly Armor@1 where the
// capture had @0, on every record from seq 598 to 635.
TEST(CardExtMakeCard, IntoHandUnderCorruptionZeroesASkillForTheCombat) {
    CombatState s{};
    s.player_powers[0].power_id = static_cast<uint16_t>(PowerId::CORRUPTION);
    s.player_powers[0].amount = -1;  // CorruptionPower.java:27 -- presence, not amount
    s.player_power_count = 1;

    // The witness card itself (SKILL, registry cost 2), then an ATTACK and a
    // POWER of nonzero cost as the type negatives (:1350's `type != SKILL`),
    // then the same SKILL created straight into the DISCARD.
    execute_opcode(s, MakeItem(Opcode::MAKE_CARD, kActorPlayer, 1,
                               static_cast<uint8_t>(CardPile::HAND),
                               make_make_card_flags(
                                   static_cast<uint16_t>(CardId::FLAME_BARRIER))));
    execute_opcode(s, MakeItem(Opcode::MAKE_CARD, kActorPlayer, 1,
                               static_cast<uint8_t>(CardPile::HAND),
                               make_make_card_flags(
                                   static_cast<uint16_t>(CardId::BASH))));
    execute_opcode(s, MakeItem(Opcode::MAKE_CARD, kActorPlayer, 1,
                               static_cast<uint8_t>(CardPile::HAND),
                               make_make_card_flags(
                                   static_cast<uint16_t>(CardId::INFLAME))));
    execute_opcode(s, MakeItem(Opcode::MAKE_CARD, kActorPlayer, 1,
                               static_cast<uint8_t>(CardPile::DISCARD),
                               make_make_card_flags(
                                   static_cast<uint16_t>(CardId::FLAME_BARRIER))));

    ASSERT_EQ(s.hand_count, 3);
    const CardPoolIndex skill = s.hand[0];
    EXPECT_EQ(s.card_pool[skill].card_id,
              static_cast<uint16_t>(CardId::FLAME_BARRIER));
    ASSERT_EQ(card_cost(*card_def(CardId::FLAME_BARRIER), 0), 2)
        << "the sweep is only observable on a nonzero-cost skill";
    EXPECT_EQ(s.card_pool[skill].cost_now, 0)
        << "modifyCostForCombat(-9): costForTurn = cost = 0";
    EXPECT_FALSE(has_card_flag(s.card_pool[skill].flags,
                               CardFlag::COST_MODIFIED_FOR_TURN))
        << "permanent for the combat, not a this-turn discount";

    // The end-of-turn sweep restores AbstractCard.cost (resetAttributes,
    // AbstractCard.java:2043) -- which modifyCostForCombat also set to 0, so the
    // discount SURVIVES the turn boundary. This is the assertion that separates
    // this fix from a setCostForTurn(0)-shaped one.
    reset_cost_for_turn(s, skill);
    EXPECT_EQ(s.card_pool[skill].cost_now, 0)
        << "cost, not just costForTurn, was written -- nothing to restore";

    EXPECT_EQ(s.card_pool[s.hand[1]].cost_now,
              card_cost(*card_def(CardId::BASH), 0))
        << "only SKILLs are swept (:1350) -- the ATTACK keeps cost 2";
    EXPECT_EQ(s.card_pool[s.hand[2]].cost_now,
              card_cost(*card_def(CardId::INFLAME), 0))
        << "only SKILLs are swept (:1350) -- the POWER keeps cost 1";
    ASSERT_EQ(s.discard_count, 1);
    EXPECT_EQ(s.card_pool[s.discard[0]].cost_now,
              card_cost(*card_def(CardId::FLAME_BARRIER), 0))
        << "a discard-pile creation is ShowCardAndAddToDiscardEffect, which "
           "never calls onCardDrawOrDiscard";

    // The hand-cap spill is the same discard-side effect: a created skill that
    // lands in the discard because the hand is full keeps its cost
    // (MakeTempCardInHandAction.update:71-77 -> addToDiscard, :132-140).
    CombatState full{};
    full.player_powers[0].power_id = static_cast<uint16_t>(PowerId::CORRUPTION);
    full.player_powers[0].amount = -1;
    full.player_power_count = 1;
    for (int i = 0; i < kHandCap; ++i) {
        full.card_pool[i].card_id = static_cast<uint16_t>(CardId::STRIKE);
        full.card_pool[i].cost_now = 1;
        full.hand[i] = static_cast<CardPoolIndex>(i);
    }
    full.hand_count = kHandCap;
    execute_opcode(full, MakeItem(Opcode::MAKE_CARD, kActorPlayer, 1,
                                  static_cast<uint8_t>(CardPile::HAND),
                                  make_make_card_flags(
                                      static_cast<uint16_t>(CardId::FLAME_BARRIER))));
    ASSERT_EQ(full.discard_count, 1);
    EXPECT_EQ(full.card_pool[full.discard[0]].cost_now,
              card_cost(*card_def(CardId::FLAME_BARRIER), 0))
        << "the spill arm has no hand sweep";

    // Without Corruption the created skill keeps its registry cost.
    CombatState plain{};
    execute_opcode(plain, MakeItem(Opcode::MAKE_CARD, kActorPlayer, 1,
                                   static_cast<uint8_t>(CardPile::HAND),
                                   make_make_card_flags(
                                       static_cast<uint16_t>(CardId::FLAME_BARRIER))));
    ASSERT_EQ(plain.hand_count, 1);
    EXPECT_EQ(plain.card_pool[plain.hand[0]].cost_now,
              card_cost(*card_def(CardId::FLAME_BARRIER), 0));
}

// The sweep is over the WHOLE hand, not just the newcomer (:1349 iterates
// hand.group), and it fires from every engine site that models
// ShowCardAndAddToHandEffect. Dual Wield is the shape where the newcomer itself
// can never move -- DualWieldAction only clones an ATTACK or a POWER
// (DualWieldAction.java:38-45) -- so the only thing the sweep can do there is
// reach a SKILL already sitting in hand with a live cost.
TEST(CardExtMakeCard, HandSweepUnderCorruptionReachesTheWholeHand) {
    CombatState s{};
    s.player_powers[0].power_id = static_cast<uint16_t>(PowerId::CORRUPTION);
    s.player_powers[0].amount = -1;
    s.player_power_count = 1;
    // A SKILL parked in hand at a live cost -- the state Snecko Eye's Confusion
    // roll leaves behind when it lands after Corruption's own onCardDraw.
    s.card_pool[0].card_id = static_cast<uint16_t>(CardId::FLAME_BARRIER);
    s.card_pool[0].cost_now = 2;
    s.card_pool[0].flags = card_flags(*card_def(CardId::FLAME_BARRIER), 0);
    s.hand[0] = 0;
    s.hand_count = 1;

    execute_opcode(s, MakeItem(Opcode::MAKE_CARD, kActorPlayer, 1,
                               static_cast<uint8_t>(CardPile::HAND),
                               make_make_card_flags(
                                   static_cast<uint16_t>(CardId::BASH))));

    ASSERT_EQ(s.hand_count, 2);
    EXPECT_EQ(s.card_pool[0].cost_now, 0)
        << "the resident skill is swept even though the newcomer is an ATTACK";
    EXPECT_FALSE(has_card_flag(s.card_pool[0].flags,
                               CardFlag::COST_MODIFIED_FOR_TURN));
    EXPECT_EQ(s.card_pool[s.hand[1]].cost_now,
              card_cost(*card_def(CardId::BASH), 0));
}

TEST(CardExtMakeCard, IntoHandSpillsToDiscardWhenHandFull) {
    CombatState s{};
    for (int i = 0; i < kHandCap; ++i) {
        s.card_pool[i].card_id = static_cast<uint16_t>(CardId::STRIKE);
        s.hand[i] = static_cast<CardPoolIndex>(i);
    }
    s.hand_count = static_cast<uint8_t>(kHandCap);

    execute_opcode(s, MakeItem(Opcode::MAKE_CARD, kActorPlayer, /*count=*/1,
                               static_cast<uint8_t>(CardPile::HAND),
                               make_make_card_flags(
                                   static_cast<uint16_t>(CardId::STRIKE))));

    EXPECT_EQ(s.hand_count, kHandCap);      // hand did not grow past the cap
    ASSERT_EQ(s.discard_count, 1);          // overflow spilled to discard
    const CardPoolIndex idx = s.discard[0];
    EXPECT_EQ(s.card_pool[idx].card_id, static_cast<uint16_t>(CardId::STRIKE));
}

TEST(CardExtMakeCard, IntoDiscardAndDrawTop) {
    CombatState s{};
    execute_opcode(s, MakeItem(Opcode::MAKE_CARD, kActorPlayer, 2,
                               static_cast<uint8_t>(CardPile::DISCARD),
                               make_make_card_flags(
                                   static_cast<uint16_t>(CardId::DEFEND))));
    ASSERT_EQ(s.discard_count, 2);
    EXPECT_EQ(s.card_pool[s.discard[0]].card_id,
              static_cast<uint16_t>(CardId::DEFEND));

    execute_opcode(s, MakeItem(Opcode::MAKE_CARD, kActorPlayer, 1,
                               static_cast<uint8_t>(CardPile::DRAW),
                               make_make_card_flags(
                                   static_cast<uint16_t>(CardId::BASH))));
    ASSERT_EQ(s.draw_count, 1);
    EXPECT_EQ(s.card_pool[s.draw[s.draw_count - 1]].card_id,
              static_cast<uint16_t>(CardId::BASH));   // onto the top
}

TEST(CardExtMakeCard, DrawRandomInsertsWithOneCardRandomRngDraw) {
    CombatState s{};
    for (int i = 0; i < 3; ++i) {
        s.card_pool[i].card_id = static_cast<uint16_t>(CardId::STRIKE);
        s.draw[i] = static_cast<CardPoolIndex>(i);
    }
    s.draw_count = 3;
    s.card_random_rng = from_seed(42);
    const int32_t before = s.card_random_rng.counter;

    execute_opcode(s, MakeItem(Opcode::MAKE_CARD, kActorPlayer, 1,
                               static_cast<uint8_t>(CardPile::DRAW_RANDOM),
                               make_make_card_flags(
                                   static_cast<uint16_t>(CardId::DEFEND))));

    EXPECT_EQ(s.card_random_rng.counter, before + 1)  // one draw (addToRandomSpot)
        << "a random draw-pile insert consumes exactly one card_random_rng draw";
    ASSERT_EQ(s.draw_count, 4);
    // The created DEFEND (pool row 3) is somewhere in the 4-card draw pile.
    bool found = false;
    for (uint8_t i = 0; i < s.draw_count; ++i) {
        if (s.card_pool[s.draw[i]].card_id ==
            static_cast<uint16_t>(CardId::DEFEND)) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(CardExtMakeCard, EmptyDrawRandomAppendsWithoutADraw) {
    CombatState s{};
    s.card_random_rng = from_seed(42);
    const int32_t before = s.card_random_rng.counter;

    execute_opcode(s, MakeItem(Opcode::MAKE_CARD, kActorPlayer, 1,
                               static_cast<uint8_t>(CardPile::DRAW_RANDOM),
                               make_make_card_flags(
                                   static_cast<uint16_t>(CardId::STRIKE))));

    EXPECT_EQ(s.card_random_rng.counter, before)  // empty pile -> no draw
        << "addToRandomSpot on an empty pile appends without an RNG draw";
    EXPECT_EQ(s.draw_count, 1);
}

// --- SET_COST ---------------------------------------------------------------

TEST(CardExtSetCost, WritesAndClampsCostNow) {
    CombatState s{};
    s.card_pool[5].card_id = static_cast<uint16_t>(CardId::BASH);
    s.card_pool[5].cost_now = 2;

    execute_opcode(s, MakeItem(Opcode::SET_COST, kActorPlayer, 0, /*src=*/5));
    EXPECT_EQ(s.card_pool[5].cost_now, 0);

    execute_opcode(s, MakeItem(Opcode::SET_COST, kActorPlayer, -3, /*src=*/5));
    EXPECT_EQ(s.card_pool[5].cost_now, 0);      // clamped up to 0

    execute_opcode(s, MakeItem(Opcode::SET_COST, kActorPlayer, 300, /*src=*/5));
    EXPECT_EQ(s.card_pool[5].cost_now, 255);    // clamped to u8

    // A cost modifier is honored at play: a Bash whose cost_now was set to 0.
    CombatState p = MakeState(CardId::BASH, /*cost=*/2, /*energy=*/1);
    execute_opcode(p, MakeItem(Opcode::SET_COST, kActorPlayer, 0, /*src=*/0));
    EXPECT_EQ(p.card_pool[0].cost_now, 0);
    ASSERT_TRUE(queue_card_play(p, 0, 0));
    pump(p);
    EXPECT_EQ(p.player_energy, 1);              // 1 - 0, the modifier was honored
}

// --- Upgrade plumbing (two-row lookup by the upgrade bit) --------------------

TEST(CardExtUpgrade, HelpersSelectBaseVsUpgradedByBit) {
    CardDef d{};
    d.id = CardId::BASH;
    d.base_cost = 2;
    d.flags = 0;
    d.step_count = 1;
    // CardEffectStep is the GENERATED type, so its `op` is sts::registry::Opcode
    // (byte-pinned equal to the engine's; the drift-pin in cards.hpp).
    d.steps[0] = CardEffectStep{sts::registry::Opcode::DAMAGE, 8, 0,
                                StepTarget::CARD_TARGET};
    d.upgraded_cost = 1;
    d.upgraded_flags = card_flag_bit(CardFlag::EXHAUST);
    d.upgraded_step_count = 1;
    d.upgraded_steps[0] = CardEffectStep{sts::registry::Opcode::DAMAGE, 10, 0,
                                         StepTarget::CARD_TARGET};

    const CardEffectView base = card_effect_steps(d, 0);
    ASSERT_EQ(base.count, 1);
    EXPECT_EQ(base.steps[0].amount, 8);
    EXPECT_EQ(card_cost(d, 0), 2);
    EXPECT_EQ(card_flags(d, 0), 0);

    const CardEffectView up = card_effect_steps(d, 1);
    ASSERT_EQ(up.count, 1);
    EXPECT_EQ(up.steps[0].amount, 10);
    EXPECT_EQ(card_cost(d, 1), 1);
    EXPECT_EQ(card_flags(d, 1), card_flag_bit(CardFlag::EXHAUST));

    // The count is honored (a >1 upgrade level still selects the upgraded row).
    EXPECT_EQ(card_effect_steps(d, 3).steps[0].amount, 10);
}

TEST(CardExtUpgrade, ResolveUsesTheUpgradeSelectedProgram) {
    // Playing an upgraded Strike resolves through the upgrade-selected program
    // and deals 9 (upgradeDamage(3): 6+3, Strike_Red.java:57-62) -- proving
    // resolve_card_play reads the upgrade count. This test once pinned 6 here
    // under a comment claiming the skeleton cards had no distinct `upgraded:`
    // block; that gap WAS the G6 §8.0 class-(a) divergence (Strike+ dealt 6
    // live), fixed with the five starter `upgraded:` rows. Full per-card
    // upgraded coverage lives in cards_test.cpp (CardTableUpgraded.*).
    CombatState s = MakeState(CardId::STRIKE, 1);
    s.card_pool[0].upgrade = 1;

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 9);
}

}  // namespace
}  // namespace sts::engine
