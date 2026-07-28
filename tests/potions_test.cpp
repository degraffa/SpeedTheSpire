// B3.23 potions (tier-2, constructed states). Each Ironclad potion's USE effect
// + potency at the S1 A20 bracket, the trap-14 rejection-sampling identity roll,
// and the A11 potion-slot count. Every number is hand-derived from the cited
// decompiled Java (registry/potions.yaml carries the per-potion provenance).
//
// SCOPE. Potions whose USE the frozen opcode set + an already-registered power
// (Strength/Vulnerable/Weak/Artifact/Metallicize) express are DATA programs and
// are checked end-to-end (queue -> pump -> effect). BLOOD_POTION's percent heal
// is a native body and is checked directly, as is BLESSING_OF_THE_FORGE's
// Armaments+ CHOOSE_CARD queue. The remaining native potions need verbs the
// engine does not have yet (an in-combat card-CHOOSE screen, recursive play,
// cost randomization, the out-of-combat revive); their bodies are DEFERRED
// (potions.cpp), so here they are checked at the registry level -- correct
// rarity/potency/native flag -- with the runtime effect landing alongside its
// verb. A deferred potion is now REFUSED by use_potion rather than no-op'd, and the
// implemented-ness gate that drives run-layer legality is checked here too
// (potion_use_implemented).

#include <cstdint>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/potions.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

// --- helpers -----------------------------------------------------------------

// Drain ONLY the main action queue (pop + execute), so a potion's queued USE
// effects resolve without triggering monster / start-of-turn logic.
void drain_actions(CombatState& s) {
    ActionQueueItem it{};
    while (pop_action_front(s, it)) {
        execute_opcode(s, it);
    }
}

int player_power_stack(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return s.player_powers[i].amount;
        }
    }
    return -1;  // absent
}

int monster_power_stack(const CombatState& s, uint8_t m, PowerId id) {
    for (uint8_t i = 0; i < s.monsters[m].power_count; ++i) {
        if (s.monsters[m].powers[i].power_id == static_cast<uint16_t>(id)) {
            return s.monsters[m].powers[i].amount;
        }
    }
    return -1;
}

void give_monster_power(CombatState& s, uint8_t m, PowerId id, int16_t amt) {
    s.monsters[m].powers[s.monsters[m].power_count].power_id =
        static_cast<uint16_t>(id);
    s.monsters[m].powers[s.monsters[m].power_count].amount = amt;
    ++s.monsters[m].power_count;
}

// Player-turn state with `n` live monsters (each JAW_WORM, given hp).
CombatState MakeCombat(int n = 1, int16_t monster_hp = 50) {
    CombatState s{};
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.player_energy = 3;
    s.player_block = 0;
    s.monster_count = static_cast<uint8_t>(n);
    for (int i = 0; i < n; ++i) {
        s.monsters[i].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
        s.monsters[i].hp = monster_hp;
        s.monsters[i].max_hp = monster_hp;
    }
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    return s;
}

// Put `n` distinct cards (STRIKE) into the draw pile so DRAW has something.
void seed_draw_pile(CombatState& s, int n) {
    for (int i = 0; i < n; ++i) {
        s.card_pool[i].card_id = static_cast<uint16_t>(CardId::STRIKE);
        s.draw[i] = static_cast<CardPoolIndex>(i);
    }
    s.draw_count = static_cast<uint8_t>(n);
}

// --- DATA potions: effect + potency (S1 A20; potency is ascension-independent) -

TEST(Potions, HeartOfIronGrantsMetallicize) {
    CombatState s = MakeCombat();
    ASSERT_TRUE(use_potion(s, PotionId::HEART_OF_IRON, 0));
    drain_actions(s);
    EXPECT_EQ(player_power_stack(s, PowerId::METALLICIZE), 6);
    EXPECT_EQ(potion_def(PotionId::HEART_OF_IRON)->potency, 6);
}

TEST(Potions, BlockPotionGainsBlock) {
    CombatState s = MakeCombat();
    ASSERT_TRUE(use_potion(s, PotionId::BLOCK_POTION, 0));
    drain_actions(s);
    EXPECT_EQ(s.player_block, 12);
    EXPECT_EQ(potion_def(PotionId::BLOCK_POTION)->potency, 12);
}

TEST(Potions, EnergyPotionGainsEnergy) {
    CombatState s = MakeCombat();
    s.player_energy = 3;
    ASSERT_TRUE(use_potion(s, PotionId::ENERGY_POTION, 0));
    drain_actions(s);
    EXPECT_EQ(s.player_energy, 5);  // 3 + potency 2
    EXPECT_EQ(potion_def(PotionId::ENERGY_POTION)->potency, 2);
}

// NOTE: this fixture is power-free on BOTH sides, so it is deliberately BLIND
// to the row's damage TYPING -- 10 lands as 10 whether the step is NORMAL or
// THORNS. The three tests after it are the typing pins; this one only pins
// the fan-out and the potency.
TEST(Potions, ExplosivePotionHitsAllEnemies) {
    CombatState s = MakeCombat(3, 50);  // three live monsters, no powers
    ASSERT_TRUE(use_potion(s, PotionId::EXPLOSIVE_POTION, 0));
    drain_actions(s);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(s.monsters[i].hp, 40) << "monster " << i;  // 50 - 10
    }
    EXPECT_EQ(potion_def(PotionId::EXPLOSIVE_POTION)->potency, 10);
}

// ExplosivePotion.use (ExplosivePotion.java:52) builds its matrix with
// DamageInfo.createDamageMatrix(potency, /*isPureDamage=*/true) -- the PURE
// branch skips info.applyPowers entirely (DamageInfo.java:126-134), so neither
// the player's Strength/Weak nor the target's Vulnerable ever touches the
// number -- and hands it to DamageAllEnemiesAction with source == NULL, whose
// per-monster hit is `damage(new DamageInfo(null, dmg, NORMAL))`
// (DamageAllEnemiesAction.java:82). The null owner fails every registered
// onAttacked power's `info.owner != null` gate (CurlUpPower.java:38 -- and
// THORNS/FLAME_BARRIER/ANGRY carry the same gate), so nothing triggers.
//
// The registry row is DELIBERATELY typed THORNS: in this engine's model,
// THORNS means exactly skip-applyPowers + skip-onAttacked (interp_damage.cpp
// op_damage), which coincides bit-for-bit with the game's pure+null-source
// NORMAL for the entire currently-registered content set. The substitution's
// limit (an owner-INsensitive onAttacked power would break the coincidence)
// is recorded on the row and in the ledger's Deferred obligations.

// NEGATIVE CONTROL baked into the expectation: NORMAL-typed (the row's state
// before this change) this reads 50 - floor(10 * 1.5) = 35.
TEST(Potions, ExplosivePotionIgnoresTargetVulnerable) {
    CombatState s = MakeCombat(1, 50);
    give_monster_power(s, 0, PowerId::VULNERABLE, 1);
    ASSERT_TRUE(use_potion(s, PotionId::EXPLOSIVE_POTION, 0));
    drain_actions(s);
    EXPECT_EQ(s.monsters[0].hp, 40);  // 50 - 10 flat, not 35
}

// NORMAL-typed this read 50 - (10 + 3) = 37: the pure matrix never runs
// applyPowers, so the player's Strength cannot ride along.
TEST(Potions, ExplosivePotionIgnoresPlayerStrength) {
    CombatState s = MakeCombat(1, 50);
    s.player_powers[s.player_power_count].power_id =
        static_cast<uint16_t>(PowerId::STRENGTH);
    s.player_powers[s.player_power_count].amount = 3;
    ++s.player_power_count;
    ASSERT_TRUE(use_potion(s, PotionId::EXPLOSIVE_POTION, 0));
    drain_actions(s);
    EXPECT_EQ(s.monsters[0].hp, 40);  // 50 - 10 flat, not 37
}

// The null-source half on its own: Curl Up's onAttacked gate is
// `info.owner != null && info.type == NORMAL` (CurlUpPower.java:38); the
// game's null source fails it, so the Louse stays curled and gains no block.
// NORMAL-typed, the sim's on-attacked dispatch triggered it (block gained,
// power consumed) -- the wrongly-spent Curl Up behind the STS00509 floor-11
// triple-Louse residual, where the sim killed a Louse the game left at 3 HP.
TEST(Potions, ExplosivePotionDoesNotTriggerCurlUp) {
    CombatState s = MakeCombat(1, 50);
    give_monster_power(s, 0, PowerId::CURL_UP, 10);
    ASSERT_TRUE(use_potion(s, PotionId::EXPLOSIVE_POTION, 0));
    drain_actions(s);
    EXPECT_EQ(s.monsters[0].hp, 40);   // the 10 still lands
    EXPECT_EQ(s.monsters[0].block, 0)  // ...but Curl Up never fires
        << "null-source hit must not trigger onAttacked (CurlUpPower.java:38)";
    EXPECT_EQ(monster_power_stack(s, 0, PowerId::CURL_UP), 10)
        << "Curl Up is not consumed by a null-source hit";
}

TEST(Potions, FirePotionDamagesTarget) {
    CombatState s = MakeCombat(2, 50);
    ASSERT_TRUE(use_potion(s, PotionId::FIRE_POTION, 1));  // target monster slot 1
    drain_actions(s);
    EXPECT_EQ(s.monsters[0].hp, 50);          // untouched
    EXPECT_EQ(s.monsters[1].hp, 30);          // 50 - 20
    EXPECT_EQ(potion_def(PotionId::FIRE_POTION)->potency, 20);
}

// FirePotion.use (FirePotion.java:43-47) builds `new DamageInfo(player, potency,
// DamageType.THORNS)` and calls info.applyEnemyPowersOnly(target) -- NOT
// applyPowers. The number is flat `potency`, and TWO independent reasons say so:
//
//   (a) THE TYPE. VulnerablePower.atDamageReceive (VulnerablePower.java:62-73)
//       is gated `if (type == DamageType.NORMAL)`, and so are StrengthPower's
//       and WeakPower's atDamageGive hooks. A THORNS DamageInfo cannot be
//       scaled by any of them.
//   (b) applyEnemyPowersOnly ITSELF (DamageInfo.java:102-120) runs only the
//       TARGET's powers -- never the owner's, so player Strength/Weak are not
//       even consulted -- and both of its loops pass `this.output` (never
//       reassigned) rather than the running `tmp`, each iteration OVERWRITING
//       tmp. With no powers on the target tmp stays `base`; with powers only
//       `powers[last].atDamageFinalReceive(base, type)` survives. The only
//       atDamageFinalReceive overriders in the tree are Flight, Forcefield and
//       the two Intangibles -- none reachable in Act 1 Exordium. That quirk is
//       deliberately NOT modeled; it belongs to whoever lands the first Act-2
//       atDamageFinalReceive power.
//
// So Strength on the player and Vulnerable on the target must BOTH leave the
// number at exactly 20. NEGATIVE CONTROL for the damage TYPE: with the registry
// row NORMAL-typed (its state before this change) the pipeline reads
// 50 - floor((20 + 3) * 1.5) = 50 - 34, i.e. 16.
TEST(Potions, FirePotionIsFlatAndUnscaledByStrengthOrVulnerable) {
    CombatState s = MakeCombat(1, 50);
    s.player_powers[s.player_power_count].power_id =
        static_cast<uint16_t>(PowerId::STRENGTH);
    s.player_powers[s.player_power_count].amount = 3;
    ++s.player_power_count;
    give_monster_power(s, 0, PowerId::VULNERABLE, 1);
    ASSERT_TRUE(use_potion(s, PotionId::FIRE_POTION, 0));
    drain_actions(s);
    EXPECT_EQ(s.monsters[0].hp, 30);  // 50 - 20 flat
}

// The owner-side half of (b), on its own: Weak on the PLAYER is an atDamageGive
// hook, and applyEnemyPowersOnly never runs owner powers at all. NEGATIVE
// CONTROL: NORMAL-typed this reads 50 - floor(20 * 0.75) = 50 - 15, i.e. 35.
TEST(Potions, FirePotionIsUnscaledByPlayerWeak) {
    CombatState s = MakeCombat(1, 50);
    s.player_powers[s.player_power_count].power_id =
        static_cast<uint16_t>(PowerId::WEAK);
    s.player_powers[s.player_power_count].amount = 2;
    ++s.player_power_count;
    ASSERT_TRUE(use_potion(s, PotionId::FIRE_POTION, 0));
    drain_actions(s);
    EXPECT_EQ(s.monsters[0].hp, 30);  // 50 - 20 flat
}

TEST(Potions, StrengthPotionGrantsStrength) {
    CombatState s = MakeCombat();
    ASSERT_TRUE(use_potion(s, PotionId::STRENGTH_POTION, 0));
    drain_actions(s);
    EXPECT_EQ(player_power_stack(s, PowerId::STRENGTH), 2);
    EXPECT_EQ(potion_def(PotionId::STRENGTH_POTION)->potency, 2);
}

TEST(Potions, SwiftPotionDrawsCards) {
    CombatState s = MakeCombat();
    seed_draw_pile(s, 5);
    s.hand_count = 0;
    ASSERT_TRUE(use_potion(s, PotionId::SWIFT_POTION, 0));
    drain_actions(s);
    EXPECT_EQ(s.hand_count, 3);       // drew potency 3
    EXPECT_EQ(s.draw_count, 2);
    EXPECT_EQ(potion_def(PotionId::SWIFT_POTION)->potency, 3);
}

TEST(Potions, WeakPotionAppliesWeakToTarget) {
    CombatState s = MakeCombat(2, 50);
    ASSERT_TRUE(use_potion(s, PotionId::WEAK_POTION, 1));
    drain_actions(s);
    EXPECT_EQ(monster_power_stack(s, 1, PowerId::WEAK), 3);
    EXPECT_EQ(monster_power_stack(s, 0, PowerId::WEAK), -1);
    EXPECT_EQ(potion_def(PotionId::WEAK_POTION)->potency, 3);
}

TEST(Potions, FearPotionAppliesVulnerableToTarget) {
    CombatState s = MakeCombat(1, 50);
    ASSERT_TRUE(use_potion(s, PotionId::FEAR_POTION, 0));
    drain_actions(s);
    EXPECT_EQ(monster_power_stack(s, 0, PowerId::VULNERABLE), 3);
    EXPECT_EQ(potion_def(PotionId::FEAR_POTION)->potency, 3);
}

TEST(Potions, AncientPotionGrantsArtifact) {
    CombatState s = MakeCombat();
    ASSERT_TRUE(use_potion(s, PotionId::ANCIENT_POTION, 0));
    drain_actions(s);
    EXPECT_EQ(player_power_stack(s, PowerId::ARTIFACT), 1);
    EXPECT_EQ(potion_def(PotionId::ANCIENT_POTION)->potency, 1);
}

// --- NATIVE with body: Blood Potion percent heal -----------------------------

TEST(Potions, BloodPotionHealsPercentOfMaxHp) {
    CombatState s = MakeCombat();
    s.player_max_hp = 80;
    s.player_hp = 40;
    ASSERT_TRUE(use_potion(s, PotionId::BLOOD_POTION, 0));  // native: applied inline
    EXPECT_EQ(s.action_count, 0) << "native heal queues nothing";
    EXPECT_EQ(s.player_hp, 56);  // 40 + floor(80 * 20/100) = 40 + 16
    EXPECT_EQ(potion_def(PotionId::BLOOD_POTION)->potency, 20);
}

TEST(Potions, BloodPotionHealClampsToMaxHp) {
    CombatState s = MakeCombat();
    s.player_max_hp = 80;
    s.player_hp = 70;
    ASSERT_TRUE(use_potion(s, PotionId::BLOOD_POTION, 0));
    EXPECT_EQ(s.player_hp, 80);  // 70 + 16 = 86, clamped to 80
}

// --- NATIVE with body: Blessing of the Forge (Armaments+ in a bottle) ---------
//
// BlessingOfTheForge.use (BlessingOfTheForge.java:43-47) addToBot's
// ArmamentsAction(true); its armamentsPlus branch (ArmamentsAction.java:34-44)
// upgrades every canUpgrade() hand card with no select screen. That is exactly
// the CHOOSE_CARD{upgrade, amount 99} program Armaments+ authors, so the potion
// queues one and the interpreter's forced path does the rest.

void seed_hand_card(CombatState& s, uint8_t pi, CardId id, uint8_t upgrade = 0) {
    const CardDef* def = card_def(id);
    ASSERT_NE(def, nullptr);
    s.card_pool[pi].card_id = static_cast<uint16_t>(id);
    s.card_pool[pi].upgrade = upgrade;
    s.card_pool[pi].cost_now = card_cost(*def, upgrade);
    s.card_pool[pi].flags = card_flags(*def, upgrade);
    s.hand[s.hand_count++] = pi;
}

// choice_slot_eligible (src/engine/interp/interp_cards.cpp) now implements
// AbstractCard.canUpgrade (AbstractCard.java:672-680) in full -- CURSE and
// STATUS are rejected before the !upgraded test -- so the curse/status case this
// note previously left unpinned is asserted below.

TEST(Potions, BlessingOfTheForgeUpgradesEveryUpgradeableHandCard) {
    CombatState s = MakeCombat();
    seed_hand_card(s, 0, CardId::STRIKE);
    seed_hand_card(s, 1, CardId::DEFEND);
    seed_hand_card(s, 2, CardId::BASH, /*upgrade=*/1);  // already upgraded
    seed_hand_card(s, 3, CardId::CLEAVE);

    ASSERT_TRUE(use_potion(s, PotionId::BLESSING_OF_THE_FORGE, 0));
    ASSERT_EQ(s.action_count, 1) << "one queued ArmamentsAction-equivalent";
    const ActionQueueItem& item = s.action_queue[s.action_head];
    EXPECT_EQ(item.opcode, static_cast<uint16_t>(Opcode::CHOOSE_CARD));
    EXPECT_EQ(choose_kind_from_flags(item.flags), ChoiceKind::UPGRADE);
    EXPECT_FALSE(choose_is_random(item.flags));
    EXPECT_FALSE(choice_requires_user(s, item))
        << "armamentsPlus opens no hand-select screen";

    drain_actions(s);
    EXPECT_EQ(s.card_pool[0].upgrade, 1);
    EXPECT_EQ(s.card_pool[1].upgrade, 1);
    EXPECT_EQ(s.card_pool[2].upgrade, 1) << "already upgraded: unchanged";
    EXPECT_EQ(s.card_pool[3].upgrade, 1);
    EXPECT_EQ(s.hand_count, 4) << "upgrading in place never moves a card";
    // The upgrade re-seeds the instance from the registry's upgraded row.
    EXPECT_EQ(s.card_pool[1].cost_now, card_cost(*card_def(CardId::DEFEND), 1));
}

TEST(Potions, BlessingOfTheForgeWithNothingUpgradeableIsInert) {
    // ArmamentsAction's armamentsPlus branch simply finds no canUpgrade() card
    // and finishes; no prompt, no other effect (getPotency is 0).
    CombatState s = MakeCombat();
    seed_hand_card(s, 0, CardId::STRIKE, /*upgrade=*/1);
    seed_hand_card(s, 1, CardId::DEFEND, /*upgrade=*/1);
    ASSERT_TRUE(use_potion(s, PotionId::BLESSING_OF_THE_FORGE, 0));
    const ActionQueueItem& item = s.action_queue[s.action_head];
    EXPECT_FALSE(choice_requires_user(s, item));
    drain_actions(s);
    EXPECT_EQ(s.card_pool[0].upgrade, 1);
    EXPECT_EQ(s.card_pool[1].upgrade, 1);
    EXPECT_EQ(s.hand_count, 2);
    EXPECT_EQ(s.player_hp, 80) << "no other side effect (potency 0)";
}

TEST(Potions, BlessingOfTheForgeSkipsCursesAndStatuses) {
    // armamentsPlus upgrades every canUpgrade() card (ArmamentsAction.java:36-44);
    // a curse or a status is never one of them (AbstractCard.java:672-680).
    CombatState s = MakeCombat();
    seed_hand_card(s, 0, CardId::STRIKE);
    seed_hand_card(s, 1, CardId::WRITHE);  // CURSE (innate -- opens in hand)
    seed_hand_card(s, 2, CardId::WOUND);   // STATUS
    seed_hand_card(s, 3, CardId::DEFEND);

    ASSERT_TRUE(use_potion(s, PotionId::BLESSING_OF_THE_FORGE, 0));
    const ActionQueueItem& item = s.action_queue[s.action_head];
    EXPECT_FALSE(choice_requires_user(s, item));

    drain_actions(s);
    EXPECT_EQ(s.card_pool[0].upgrade, 1);
    EXPECT_EQ(s.card_pool[1].upgrade, 0) << "a curse is not upgradeable";
    EXPECT_EQ(s.card_pool[2].upgrade, 0) << "a status is not upgradeable";
    EXPECT_EQ(s.card_pool[3].upgrade, 1);
    EXPECT_EQ(s.hand_count, 4);
}

TEST(Potions, BlessingOfTheForgeWithOnlyCursesAndStatusesIsInert) {
    // The eligible count is ZERO, not 3 -- the forced sweep finds nothing and the
    // potion is a no-op (and, per choice_requires_user, still opens no screen).
    CombatState s = MakeCombat();
    seed_hand_card(s, 0, CardId::WRITHE);
    seed_hand_card(s, 1, CardId::WOUND);
    seed_hand_card(s, 2, CardId::SLIMED);

    ASSERT_TRUE(use_potion(s, PotionId::BLESSING_OF_THE_FORGE, 0));
    const ActionQueueItem& item = s.action_queue[s.action_head];
    EXPECT_FALSE(choice_requires_user(s, item));
    drain_actions(s);
    for (uint8_t i = 0; i < 3; ++i) {
        EXPECT_EQ(s.card_pool[i].upgrade, 0) << "pool row " << int{i};
    }
    EXPECT_EQ(s.hand_count, 3);
}

// --- NATIVE with body: Elixir (Purity in a bottle) ---------------------------
//
// Elixir.use (Elixir.java:44-49) addToBot's ExhaustAction(false, true, true) --
// the (isRandom, anyNumber, canPickZero) ctor, which forwards amount 99
// (ExhaustAction.java:56-58). With anyNumber true the whole-hand branch
// (:80-89) is unreachable and with isRandom false the getRandomCard branch
// (:90-94) is too, so what is left is the OPTIONAL zero-to-99 screen at :96-99
// and the pick-order moveToExhaustPile at :102-108. That is Purity's authored
// program with a bigger amount (registry/cards.yaml:1856).

TEST(Potions, ElixirQueuesTheOptionalExhaustScreen) {
    CombatState s = MakeCombat();
    seed_hand_card(s, 0, CardId::STRIKE);
    seed_hand_card(s, 1, CardId::DEFEND);

    ASSERT_TRUE(use_potion(s, PotionId::ELIXIR, 0));
    ASSERT_EQ(s.action_count, 1) << "one queued ExhaustAction-equivalent";
    const ActionQueueItem& item = s.action_queue[s.action_head];
    EXPECT_EQ(item.opcode, static_cast<uint16_t>(Opcode::CHOOSE_CARD));
    EXPECT_EQ(choose_kind_from_flags(item.flags), ChoiceKind::EXHAUST);
    EXPECT_TRUE(choose_is_optional(item.flags));
    EXPECT_FALSE(choose_is_random(item.flags))
        << "isRandom false -- Elixir spends no rng on any path";
    EXPECT_EQ(item.amount, 99)
        << "ExhaustAction's 3-arg ctor forwards 99, and the pick cap is "
           "compared against it -- it must not be tidied down to the hand cap";
    EXPECT_TRUE(choice_requires_user(s, item))
        << "canPickZero: the screen is ended by the confirm button only";
    EXPECT_EQ(s.card_random_rng.counter, 0);
}

// anyNumber makes the "hand.size() <= amount -> exhaust everything, no screen"
// branch unreachable, so even a single-card hand still prompts and may
// legitimately take nothing.
TEST(Potions, ElixirStillPromptsWithASingleCardInHand) {
    CombatState s = MakeCombat();
    seed_hand_card(s, 0, CardId::STRIKE);
    ASSERT_TRUE(use_potion(s, PotionId::ELIXIR, 0));
    const ActionQueueItem& item = s.action_queue[s.action_head];
    EXPECT_TRUE(choice_requires_user(s, item));
    EXPECT_EQ(s.hand_count, 1) << "nothing is exhausted before the confirm";
    EXPECT_EQ(s.exhaust_count, 0);
}

// ExhaustAction.java:76-79: an EMPTY hand ends the action immediately. That is
// the action's only short-circuit, so it is also the only way the pump does not
// block on this item.
TEST(Potions, ElixirWithAnEmptyHandBlocksOnNothing) {
    CombatState s = MakeCombat();
    ASSERT_EQ(s.hand_count, 0);
    ASSERT_TRUE(use_potion(s, PotionId::ELIXIR, 0));
    const ActionQueueItem& item = s.action_queue[s.action_head];
    EXPECT_FALSE(choice_requires_user(s, item));
    drain_actions(s);
    EXPECT_EQ(s.exhaust_count, 0);
    EXPECT_EQ(s.card_random_rng.counter, 0);
}

// The confirm applies the selection IN PICK ORDER (:102-105), so the exhaust
// pile records the order the player picked, NOT hand order. Picking slot 2 then
// slot 0 must put CLEAVE into the exhaust pile ahead of STRIKE.
TEST(Potions, ElixirExhaustsThePicksInPickOrderAndLeavesTheRest) {
    CombatState s = MakeCombat();
    seed_hand_card(s, 0, CardId::STRIKE);
    seed_hand_card(s, 1, CardId::DEFEND);
    seed_hand_card(s, 2, CardId::CLEAVE);

    ASSERT_TRUE(use_potion(s, PotionId::ELIXIR, 0));
    ActionQueueItem& item = s.action_queue[s.action_head];
    ASSERT_TRUE(optional_choice_slot_legal(s, item, 2));
    toggle_optional_choice_slot(s, item, 2);   // pick CLEAVE first
    // Selecting lifts the card out of the hand and appends it to the suffix, so
    // STRIKE is now slot 0 still and DEFEND slot 1.
    ASSERT_TRUE(optional_choice_slot_legal(s, item, 0));
    toggle_optional_choice_slot(s, item, 0);   // then STRIKE
    EXPECT_EQ(choose_selected_count(item.flags), 2);

    resolve_optional_choice(s, item);

    ASSERT_EQ(s.exhaust_count, 2);
    EXPECT_EQ(s.card_pool[s.exhaust[0]].card_id,
              static_cast<uint16_t>(CardId::CLEAVE)) << "picked first";
    EXPECT_EQ(s.card_pool[s.exhaust[1]].card_id,
              static_cast<uint16_t>(CardId::STRIKE)) << "picked second";
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.card_pool[s.hand[0]].card_id,
              static_cast<uint16_t>(CardId::DEFEND)) << "unpicked, still held";
    EXPECT_EQ(s.card_random_rng.counter, 0);
}

// canPickZero: confirming with nothing selected exhausts nothing at all. The
// Java walks an empty selectedCards.group and the potion is simply spent.
TEST(Potions, ElixirConfirmedWithNoPicksExhaustsNothing) {
    CombatState s = MakeCombat();
    seed_hand_card(s, 0, CardId::STRIKE);
    seed_hand_card(s, 1, CardId::DEFEND);

    ASSERT_TRUE(use_potion(s, PotionId::ELIXIR, 0));
    const ActionQueueItem& item = s.action_queue[s.action_head];
    resolve_optional_choice(s, item);

    EXPECT_EQ(s.exhaust_count, 0);
    EXPECT_EQ(s.hand_count, 2);
    EXPECT_EQ(potion_def(PotionId::ELIXIR)->potency, 0);
}

// --- NATIVE with body: the four "discover" potions ---------------------------
//
// One shape, one argument changed. Each use() is a single addToBot:
//   AttackPotion.java:40-42     DiscoveryAction(CardType.ATTACK, potency)
//   SkillPotion.java:40-42      DiscoveryAction(CardType.SKILL, potency)
//   PowerPotion.java:40-42      DiscoveryAction(CardType.POWER, potency)
//   ColorlessPotion.java:38-40  DiscoveryAction(true, potency)
// All four getPotency return 1. They ride the already-live DISCOVERY opcode,
// extended here with a POOL SELECTOR (item.src) and a COPY COUNT (item.tgt).

const CardId* discovery_pool_cards(DiscoveryPool p, int& count) {
    switch (p) {
        case DiscoveryPool::ATTACK:
            count = kIroncladAttackPoolCount;
            return kIroncladAttackPool.data();
        case DiscoveryPool::SKILL:
            count = kIroncladSkillPoolCount;
            return kIroncladSkillPool.data();
        case DiscoveryPool::POWER:
            count = kIroncladPowerPoolCount;
            return kIroncladPowerPool.data();
        case DiscoveryPool::COLORLESS:
            count = kColorlessCombatPoolCount;
            return kColorlessCombatPool.data();
        default:
            count = kIroncladCombatPoolCount;
            return kIroncladCombatPool.data();
    }
}

TEST(Potions, DiscoverPotionsSelectTheirOwnPoolAndCarryPotencyAsTheCopyCount) {
    struct Row { PotionId id; DiscoveryPool pool; };
    const Row rows[] = {
        {PotionId::ATTACK_POTION, DiscoveryPool::ATTACK},
        {PotionId::SKILL_POTION, DiscoveryPool::SKILL},
        {PotionId::POWER_POTION, DiscoveryPool::POWER},
        {PotionId::COLORLESS_POTION, DiscoveryPool::COLORLESS},
    };
    for (const Row& r : rows) {
        CombatState s = MakeCombat();
        ASSERT_TRUE(use_potion(s, r.id, 0)) << static_cast<int>(r.id);
        ASSERT_EQ(s.action_count, 1);
        const ActionQueueItem& item = s.action_queue[s.action_head];
        EXPECT_EQ(item.opcode, static_cast<uint16_t>(Opcode::DISCOVERY));
        EXPECT_EQ(discovery_pool(item), r.pool)
            << "potion " << static_cast<int>(r.id);
        EXPECT_EQ(discovery_copies(item), 1)
            << "getPotency is 1 -- DiscoveryAction's amount is the COPY count";
        EXPECT_FALSE(discovery_choice_prepared(item))
            << "the offer is generated when the item reaches the pump";
        EXPECT_EQ(potion_def(r.id)->potency, 1);
    }
}

// The offer is three DISTINCT cardIDs drawn from THAT potion's pool, one
// card_random_rng draw per attempt (DiscoveryAction.java:105-120 / :89-103 --
// `while (derp.size() != 3)` with a `continue` on a duplicate cardID). The
// expected values are re-derived from an independent stream, and every offered
// card is asserted to be a member of the right pool -- which is the assertion
// that would catch a mis-wired selector.
TEST(Potions, DiscoverPotionOffersComeFromTheRightPoolWithExactRngAccounting) {
    struct Row { PotionId id; DiscoveryPool pool; };
    const Row rows[] = {
        {PotionId::ATTACK_POTION, DiscoveryPool::ATTACK},
        {PotionId::SKILL_POTION, DiscoveryPool::SKILL},
        {PotionId::POWER_POTION, DiscoveryPool::POWER},
        {PotionId::COLORLESS_POTION, DiscoveryPool::COLORLESS},
    };
    int seeds_with_duplicate_retry = 0;
    for (const Row& r : rows) {
        int pool_count = 0;
        const CardId* pool = discovery_pool_cards(r.pool, pool_count);
        ASSERT_GE(pool_count, kDiscoveryChoiceCount);

        for (int64_t seed = 1; seed <= 24; ++seed) {
            CombatState s = MakeCombat();
            s.card_random_rng = from_seed(seed);
            ASSERT_TRUE(use_potion(s, r.id, 0));
            ActionQueueItem& item = s.action_queue[s.action_head];

            RngStream probe = from_seed(seed);
            CardId expected[kDiscoveryChoiceCount]{};
            uint8_t n = 0;
            int draws = 0;
            while (n < kDiscoveryChoiceCount) {
                const int32_t pick =
                    random(probe, static_cast<int32_t>(pool_count) - 1);
                ++draws;
                const CardId cid = pool[static_cast<unsigned>(pick)];
                bool dupe = false;
                for (uint8_t i = 0; i < n; ++i) {
                    dupe = dupe || expected[i] == cid;
                }
                if (!dupe) {
                    expected[n++] = cid;
                }
            }
            if (draws > kDiscoveryChoiceCount) {
                ++seeds_with_duplicate_retry;
            }

            prepare_discovery_choice(s, item);

            EXPECT_EQ(s.card_random_rng.counter, probe.counter)
                << "potion " << static_cast<int>(r.id) << " seed " << seed
                << ": a rejected duplicate still costs its draw";
            for (uint8_t i = 0; i < kDiscoveryChoiceCount; ++i) {
                const CardId got = discovery_choice_card(item, i);
                EXPECT_EQ(got, expected[i])
                    << "potion " << static_cast<int>(r.id) << " slot " << int{i};
                bool in_pool = false;
                for (int k = 0; k < pool_count; ++k) {
                    in_pool = in_pool || pool[k] == got;
                }
                EXPECT_TRUE(in_pool)
                    << "offer slot " << int{i} << " left its pool";
            }
        }
    }
    EXPECT_GT(seeds_with_duplicate_retry, 0)
        << "the seed battery must cover at least one rejected duplicate";
}

// Power Potion is the one that needed a NEW generated pool. Membership is what
// makes the pool right, so it is asserted from the card table rather than
// trusted: every member must be a RED, non-BASIC, non-HEALING POWER.
TEST(Potions, PowerPotionPoolIsExactlyTheRedNonHealingPowers) {
    ASSERT_GE(kIroncladPowerPoolCount, kDiscoveryChoiceCount)
        << "the rejection sampler cannot terminate on a pool of fewer than 3";
    for (int i = 0; i < kIroncladPowerPoolCount; ++i) {
        const CardDef* d = card_def(kIroncladPowerPool[static_cast<unsigned>(i)]);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(d->type, CardType::POWER)
            << "pool index " << i << " is not a POWER";
    }
    // And it is a strict subset of the full combat pool it filters.
    for (int i = 0; i < kIroncladPowerPoolCount; ++i) {
        const CardId id = kIroncladPowerPool[static_cast<unsigned>(i)];
        bool found = false;
        for (int k = 0; k < kIroncladCombatPoolCount; ++k) {
            found = found || kIroncladCombatPool[static_cast<unsigned>(k)] == id;
        }
        EXPECT_TRUE(found) << "pool index " << i << " is not in the combat pool";
    }
}

// The chosen card arrives as `amount` stat-equivalent copies, each at cost 0 for
// the turn (DiscoveryAction.java:55-62, :65-81). Potency 1 is one copy.
TEST(Potions, DiscoverPotionCreatesOneCostZeroCopyOfTheChosenCard) {
    CombatState s = MakeCombat();
    s.card_random_rng = from_seed(5);
    ASSERT_TRUE(use_potion(s, PotionId::SKILL_POTION, 0));
    ActionQueueItem& item = s.action_queue[s.action_head];
    prepare_discovery_choice(s, item);
    const CardId chosen = discovery_choice_card(item, 1);

    resolve_discovery_choice(s, item, 1);

    ASSERT_EQ(s.hand_count, 1);
    const CardInstance& made = s.card_pool[s.hand[0]];
    EXPECT_EQ(made.card_id, static_cast<uint16_t>(chosen));
    EXPECT_EQ(made.upgrade, 0) << "makeStatEquivalentCopy of the BASE offer";
    EXPECT_EQ(made.cost_now, 0);
    const CardDef* d = card_def(chosen);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(has_card_flag(made.flags, CardFlag::COST_MODIFIED_FOR_TURN),
              d->base_cost != 0)
        << "setCostForTurn is a no-op on an already-0 cost";
}

// SACRED BARK'S SHAPE. AbstractPotion.getPotency doubles potency, and for these
// four potency IS DiscoveryAction's `amount`, i.e. the number of copies -- not
// the offer size and not the pool. The relic has no engine hook yet (there is no
// potency site at all), so the doubled value is stamped directly to pin the
// encoding the hook will drive. `amount == 2` with hand + 2 <= 10 puts BOTH
// copies in hand (:72-74).
TEST(Potions, DiscoverPotionWithTwoCopiesMakesTwoOfTheSameCard) {
    CombatState s = MakeCombat();
    s.card_random_rng = from_seed(5);
    ASSERT_TRUE(use_potion(s, PotionId::SKILL_POTION, 0));
    ActionQueueItem& item = s.action_queue[s.action_head];
    item.tgt = 2;  // Sacred Bark: potency 1 -> 2
    ASSERT_EQ(discovery_copies(item), 2);
    prepare_discovery_choice(s, item);
    const CardId chosen = discovery_choice_card(item, 0);

    resolve_discovery_choice(s, item, 0);

    ASSERT_EQ(s.hand_count, 2);
    EXPECT_EQ(s.card_pool[s.hand[0]].card_id, static_cast<uint16_t>(chosen));
    EXPECT_EQ(s.card_pool[s.hand[1]].card_id, static_cast<uint16_t>(chosen))
        << "both copies are of the SAME card";
    EXPECT_NE(s.hand[0], s.hand[1]) << "two distinct instances";
    EXPECT_EQ(s.card_pool[s.hand[0]].cost_now, 0);
    EXPECT_EQ(s.card_pool[s.hand[1]].cost_now, 0);
}

// The amount == 2 fan-out's middle case (:75-77): at hand size 9 the first copy
// fills the hand and the SECOND goes to the discard pile. That is exactly
// sequential add-with-spill, which is why the branch table is not transcribed.
TEST(Potions, DiscoverPotionSecondCopySpillsToDiscardAtAFullHand) {
    CombatState s = MakeCombat();
    for (uint8_t i = 0; i < 9; ++i) {
        seed_hand_card(s, static_cast<uint8_t>(30 + i), CardId::STRIKE);
    }
    ASSERT_EQ(s.hand_count, 9);
    s.card_random_rng = from_seed(5);
    ASSERT_TRUE(use_potion(s, PotionId::SKILL_POTION, 0));
    ActionQueueItem& item = s.action_queue[s.action_head];
    item.tgt = 2;
    prepare_discovery_choice(s, item);
    const CardId chosen = discovery_choice_card(item, 0);

    resolve_discovery_choice(s, item, 0);

    EXPECT_EQ(s.hand_count, 10) << "the first copy fills the hand";
    EXPECT_EQ(s.card_pool[s.hand[9]].card_id, static_cast<uint16_t>(chosen));
    ASSERT_EQ(s.discard_count, 1) << "the second copy spills";
    EXPECT_EQ(s.card_pool[s.discard[0]].card_id, static_cast<uint16_t>(chosen));
}

// The Discovery CARD must be untouched by the new operands. An AUTHORED
// DISCOVERY step carries the actor sentinels in src/tgt (queue_effect_step
// writes them for every step), and those are exactly the defaults -- full RED
// combat pool, one copy -- so no previously-authored item changed meaning.
TEST(Potions, AnAuthoredDiscoveryItemDefaultsToTheFullCombatPoolAndOneCopy) {
    ActionQueueItem authored{};
    authored.opcode = static_cast<uint16_t>(Opcode::DISCOVERY);
    authored.src = kActorPlayer;
    authored.tgt = kActorPlayer;
    EXPECT_EQ(discovery_pool(authored), DiscoveryPool::COMBAT);
    EXPECT_EQ(discovery_copies(authored), 1);
}

// --- NATIVE with body: Liquid Memories (ChoiceKind::DISCARD_TO_HAND_FREE) ----
//
// LiquidMemories.use (LiquidMemories.java:37-40) is one addToBot of
// BetterDiscardPileToHandAction(potency, 0) -- the (numberOfCards, newCost)
// ctor, setCost true / newCost 0 / optional FALSE. getPotency is 1.

void seed_discard_card(CombatState& s, uint8_t pi, CardId id) {
    const CardDef* def = card_def(id);
    ASSERT_NE(def, nullptr);
    s.card_pool[pi].card_id = static_cast<uint16_t>(id);
    s.card_pool[pi].cost_now = card_cost(*def, 0);
    s.card_pool[pi].flags = card_flags(*def, 0);
    s.discard[s.discard_count++] = pi;
}

TEST(Potions, LiquidMemoriesQueuesAMandatoryDiscardSourceChoice) {
    CombatState s = MakeCombat();
    seed_discard_card(s, 40, CardId::BASH);
    seed_discard_card(s, 41, CardId::CLEAVE);
    seed_discard_card(s, 42, CardId::STRIKE);

    ASSERT_TRUE(use_potion(s, PotionId::LIQUID_MEMORIES, 0));
    ASSERT_EQ(s.action_count, 1);
    const ActionQueueItem& item = s.action_queue[s.action_head];
    EXPECT_EQ(item.opcode, static_cast<uint16_t>(Opcode::CHOOSE_CARD));
    EXPECT_EQ(choose_kind_from_flags(item.flags),
              ChoiceKind::DISCARD_TO_HAND_FREE);
    EXPECT_EQ(choice_source(choose_kind_from_flags(item.flags)),
              ChoiceSource::DISCARD);
    EXPECT_FALSE(choose_is_optional(item.flags)) << "optional is false on this ctor";
    EXPECT_FALSE(choose_is_random(item.flags));
    EXPECT_EQ(item.amount, 1) << "potency is numberOfCards";
    EXPECT_TRUE(choice_requires_user(s, item))
        << "3 discard cards > 1 wanted -> a real grid select";
    EXPECT_EQ(potion_def(PotionId::LIQUID_MEMORIES)->potency, 1);
}

// The chosen discard card enters the hand at cost 0 FOR THE TURN -- setCostForTurn
// (:66/:95), not a permanent write. COST_MODIFIED_FOR_TURN must therefore be SET
// (the opposite of Snecko Oil / Confusion), so the end-of-turn sweep restores the
// real cost.
TEST(Potions, LiquidMemoriesMovesThePickedCardToHandAtCostZeroThisTurn) {
    CombatState s = MakeCombat();
    seed_discard_card(s, 40, CardId::BASH);    // cost 2
    seed_discard_card(s, 41, CardId::CLEAVE);  // cost 1
    seed_discard_card(s, 42, CardId::STRIKE);

    ASSERT_TRUE(use_potion(s, PotionId::LIQUID_MEMORIES, 0));
    const ActionQueueItem& item = s.action_queue[s.action_head];
    apply_choice_selection(s, /*slot=*/0, choose_kind_from_flags(item.flags),
                           /*copies=*/1, /*prompted=*/true);  // the Bash

    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], 40);
    EXPECT_EQ(s.card_pool[40].cost_now, 0);
    EXPECT_TRUE(has_card_flag(s.card_pool[40].flags,
                              CardFlag::COST_MODIFIED_FOR_TURN))
        << "setCostForTurn is THIS-TURN -- the base cost must be restorable";
    ASSERT_EQ(s.discard_count, 2) << "and it leaves the discard pile";
    EXPECT_EQ(s.discard[0], 41);
    EXPECT_EQ(s.discard[1], 42);
    EXPECT_EQ(s.card_random_rng.counter, 0) << "this action spends no rng";
}

// `discardPile.size() <= numberOfCards && !optional` (:57-75) is the FORCED,
// screen-less branch: the whole discard pile moves, in DISCARD ORDER, with no
// prompt. With potency 1 and a 1-card discard that means no prompt at all.
TEST(Potions, LiquidMemoriesWithASingleDiscardCardIsForcedAndUnprompted) {
    CombatState s = MakeCombat();
    seed_discard_card(s, 40, CardId::BASH);

    ASSERT_TRUE(use_potion(s, PotionId::LIQUID_MEMORIES, 0));
    const ActionQueueItem& item = s.action_queue[s.action_head];
    EXPECT_FALSE(choice_requires_user(s, item)) << "size <= numberOfCards";
    drain_actions(s);

    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], 40);
    EXPECT_EQ(s.card_pool[40].cost_now, 0);
    EXPECT_EQ(s.discard_count, 0);
    EXPECT_EQ(s.card_random_rng.counter, 0);
}

// `discardPile.isEmpty() || numberOfCards <= 0` (:53-56) ends the action with
// nothing done -- and, here, without blocking the pump.
TEST(Potions, LiquidMemoriesWithAnEmptyDiscardIsASilentNoOp) {
    CombatState s = MakeCombat();
    ASSERT_EQ(s.discard_count, 0);
    ASSERT_TRUE(use_potion(s, PotionId::LIQUID_MEMORIES, 0));
    const ActionQueueItem& item = s.action_queue[s.action_head];
    EXPECT_FALSE(choice_requires_user(s, item));
    drain_actions(s);
    EXPECT_EQ(s.hand_count, 0);
    EXPECT_EQ(s.discard_count, 0);
}

// THE TRAP. The per-card body is `if (hand.size() < 10) { addToHand;
// setCostForTurn(0); discardPile.removeCard; }` -- the guard wraps the REMOVAL,
// so a card that does not fit STAYS IN THE DISCARD PILE at its original cost.
// It is emphatically NOT MakeTempCardInHandAction's spill-to-discard.
TEST(Potions, LiquidMemoriesLeavesTheCardInTheDiscardWhenTheHandIsFull) {
    CombatState s = MakeCombat();
    for (uint8_t i = 0; i < 10; ++i) {
        seed_hand_card(s, static_cast<uint8_t>(50 + i), CardId::STRIKE);
    }
    ASSERT_EQ(s.hand_count, 10);
    seed_discard_card(s, 40, CardId::BASH);
    const uint8_t base_cost = s.card_pool[40].cost_now;
    ASSERT_GT(base_cost, 0);

    ASSERT_TRUE(use_potion(s, PotionId::LIQUID_MEMORIES, 0));
    drain_actions(s);

    EXPECT_EQ(s.hand_count, 10) << "nothing was added";
    ASSERT_EQ(s.discard_count, 1) << "and nothing was removed either";
    EXPECT_EQ(s.discard[0], 40);
    EXPECT_EQ(s.card_pool[40].cost_now, base_cost)
        << "the re-cost is inside the same guard as the move";
    EXPECT_FALSE(has_card_flag(s.card_pool[40].flags,
                               CardFlag::COST_MODIFIED_FOR_TURN));
}

// SACRED BARK'S SHAPE: potency IS numberOfCards, so 1 -> 2 makes the grid select
// require exactly TWO picks and widens the forced branch to a 2-card discard.
// The relic has no engine hook, so the doubled amount is stamped directly.
TEST(Potions, LiquidMemoriesWithADoubledPotencyForcesATwoCardDiscardPile) {
    CombatState s = MakeCombat();
    seed_discard_card(s, 40, CardId::BASH);
    seed_discard_card(s, 41, CardId::CLEAVE);

    ASSERT_TRUE(use_potion(s, PotionId::LIQUID_MEMORIES, 0));
    ActionQueueItem& item = s.action_queue[s.action_head];
    item.amount = 2;  // Sacred Bark: potency 1 -> 2
    EXPECT_FALSE(choice_requires_user(s, item))
        << "discardPile.size() <= numberOfCards -> forced, no screen";

    drain_actions(s);
    ASSERT_EQ(s.hand_count, 2) << "the whole discard pile moves";
    EXPECT_EQ(s.hand[0], 40) << "in DISCARD ORDER";
    EXPECT_EQ(s.hand[1], 41);
    EXPECT_EQ(s.discard_count, 0);
    EXPECT_EQ(s.card_pool[40].cost_now, 0);
    EXPECT_EQ(s.card_pool[41].cost_now, 0);
}

// --- NATIVE with body: Gambler's Brew (ChoiceKind::HAND_TO_DISCARD_THEN_DRAW) -
//
// GamblersBrew.use (GamblersBrew.java:36-41) queues GamblingChipAction(player,
// true) -- the SAME action the Gambling Chip relic queues with notChip false --
// but only when the hand is non-empty. The relic half is covered in
// relic_rares_shop_test.

TEST(Potions, GamblersBrewQueuesTheOptionalDiscardScreen) {
    CombatState s = MakeCombat();
    seed_hand_card(s, 0, CardId::STRIKE);
    seed_hand_card(s, 1, CardId::DEFEND);

    ASSERT_TRUE(use_potion(s, PotionId::GAMBLERS_BREW, 0));
    ASSERT_EQ(s.action_count, 1);
    const ActionQueueItem& item = s.action_queue[s.action_head];
    EXPECT_EQ(item.opcode, static_cast<uint16_t>(Opcode::CHOOSE_CARD));
    EXPECT_EQ(choose_kind_from_flags(item.flags),
              ChoiceKind::HAND_TO_DISCARD_THEN_DRAW);
    EXPECT_TRUE(choose_is_optional(item.flags));
    EXPECT_FALSE(choose_is_random(item.flags));
    EXPECT_EQ(item.amount, 99) << "GamblingChipAction's own literal";
    EXPECT_TRUE(choice_requires_user(s, item));
    EXPECT_EQ(potion_def(PotionId::GAMBLERS_BREW)->potency, 0);
}

// `if (!AbstractDungeon.player.hand.isEmpty())` (GamblersBrew.java:38) is on the
// POTION, not inside the action, so an empty hand queues NOTHING AT ALL -- not
// even a screen that immediately closes. (The relic has no such guard.)
TEST(Potions, GamblersBrewWithAnEmptyHandQueuesNothing) {
    CombatState s = MakeCombat();
    ASSERT_EQ(s.hand_count, 0);
    ASSERT_TRUE(use_potion(s, PotionId::GAMBLERS_BREW, 0));
    EXPECT_EQ(s.action_count, 0) << "the guard is on the potion";
}

// The confirm discards every pick IN PICK ORDER and then queues ONE
// DrawCardAction sized by the pick count, at the TOP of the queue
// (GamblingChipAction.java:53-58).
TEST(Potions, GamblersBrewDiscardsThePicksInOrderThenDrawsThatMany) {
    CombatState s = MakeCombat();
    seed_hand_card(s, 0, CardId::STRIKE);
    seed_hand_card(s, 1, CardId::DEFEND);
    seed_hand_card(s, 2, CardId::CLEAVE);
    for (uint8_t i = 0; i < 4; ++i) {  // four cards to draw back from
        s.card_pool[20 + i].card_id = static_cast<uint16_t>(CardId::BASH);
        s.draw[i] = static_cast<CardPoolIndex>(20 + i);
    }
    s.draw_count = 4;

    ASSERT_TRUE(use_potion(s, PotionId::GAMBLERS_BREW, 0));
    ActionQueueItem item = s.action_queue[s.action_head];
    toggle_optional_choice_slot(s, item, 2);  // pick Cleave first
    toggle_optional_choice_slot(s, item, 0);  // then Strike
    ASSERT_EQ(choose_selected_count(item.flags), 2);
    // The engine pops the finished CHOOSE_CARD before resolving, which is what
    // makes the action's addToTop land on the real head; mirror that here.
    ActionQueueItem consumed{};
    ASSERT_TRUE(pop_action_front(s, consumed));
    resolve_optional_choice(s, item);

    ASSERT_EQ(s.discard_count, 2);
    EXPECT_EQ(s.card_pool[s.discard[0]].card_id,
              static_cast<uint16_t>(CardId::CLEAVE)) << "picked first";
    EXPECT_EQ(s.card_pool[s.discard[1]].card_id,
              static_cast<uint16_t>(CardId::STRIKE)) << "picked second";
    ASSERT_EQ(s.hand_count, 1) << "before the draw resolves";
    EXPECT_EQ(s.card_pool[s.hand[0]].card_id,
              static_cast<uint16_t>(CardId::DEFEND));

    ASSERT_EQ(s.action_count, 1) << "ONE DrawCardAction, not one per card";
    const ActionQueueItem& draw = s.action_queue[s.action_head];
    EXPECT_EQ(draw.opcode, static_cast<uint16_t>(Opcode::DRAW));
    EXPECT_EQ(draw.amount, 2) << "selectedCards.size(), read at confirm";

    drain_actions(s);
    EXPECT_EQ(s.hand_count, 3) << "1 kept + 2 drawn back";
    EXPECT_EQ(s.draw_count, 2);
}

// `if (!selectedCards.group.isEmpty())` (GamblingChipAction.java:52) wraps BOTH
// halves: an empty confirm queues no draw and discards nothing.
TEST(Potions, GamblersBrewConfirmedWithNoPicksDoesNothingAtAll) {
    CombatState s = MakeCombat();
    seed_hand_card(s, 0, CardId::STRIKE);
    seed_hand_card(s, 1, CardId::DEFEND);
    s.card_pool[20].card_id = static_cast<uint16_t>(CardId::BASH);
    s.draw[0] = 20;
    s.draw_count = 1;

    ASSERT_TRUE(use_potion(s, PotionId::GAMBLERS_BREW, 0));
    const ActionQueueItem item = s.action_queue[s.action_head];
    ActionQueueItem consumed{};
    ASSERT_TRUE(pop_action_front(s, consumed));
    resolve_optional_choice(s, item);

    EXPECT_EQ(s.action_count, 0) << "no DrawCardAction was queued";
    EXPECT_EQ(s.discard_count, 0);
    EXPECT_EQ(s.hand_count, 2);
    EXPECT_EQ(s.draw_count, 1);
}

// --- Un-deferred power-granting potions (now DATA APPLY_POWER programs) -------
// Powers registered by the potion-support-powers follow-up (Dexterity, Lose
// Dexterity, Thorns, Plated Armor, Regen, Ritual; Steroid reuses LoseStrength).

TEST(Potions, DexterityPotionGrantsDexterity) {
    CombatState s = MakeCombat();
    ASSERT_TRUE(use_potion(s, PotionId::DEXTERITY_POTION, 0));
    drain_actions(s);
    EXPECT_EQ(player_power_stack(s, PowerId::DEXTERITY), 2);
    EXPECT_EQ(potion_def(PotionId::DEXTERITY_POTION)->potency, 2);
}

TEST(Potions, SteroidPotionGrantsStrengthAndLoseStrength) {
    CombatState s = MakeCombat();
    ASSERT_TRUE(use_potion(s, PotionId::STEROID_POTION, 0));
    drain_actions(s);
    // Strength + LoseStrength (temporary this turn); LoseStrength is id 13 (Flex).
    EXPECT_EQ(player_power_stack(s, PowerId::STRENGTH), 5);
    EXPECT_EQ(player_power_stack(s, PowerId::LOSE_STRENGTH), 5);
}

TEST(Potions, SpeedPotionGrantsDexterityAndLoseDexterity) {
    CombatState s = MakeCombat();
    ASSERT_TRUE(use_potion(s, PotionId::SPEED_POTION, 0));
    drain_actions(s);
    EXPECT_EQ(player_power_stack(s, PowerId::DEXTERITY), 5);
    EXPECT_EQ(player_power_stack(s, PowerId::LOSE_DEXTERITY), 5);
}

TEST(Potions, RegenPotionGrantsRegen) {
    CombatState s = MakeCombat();
    ASSERT_TRUE(use_potion(s, PotionId::REGEN_POTION, 0));
    drain_actions(s);
    EXPECT_EQ(player_power_stack(s, PowerId::REGEN), 5);
}

TEST(Potions, LiquidBronzeGrantsThorns) {
    CombatState s = MakeCombat();
    ASSERT_TRUE(use_potion(s, PotionId::LIQUID_BRONZE, 0));
    drain_actions(s);
    EXPECT_EQ(player_power_stack(s, PowerId::THORNS), 3);
}

TEST(Potions, EssenceOfSteelGrantsPlatedArmor) {
    CombatState s = MakeCombat();
    ASSERT_TRUE(use_potion(s, PotionId::ESSENCE_OF_STEEL, 0));
    drain_actions(s);
    EXPECT_EQ(player_power_stack(s, PowerId::PLATED_ARMOR), 4);
}

TEST(Potions, CultistPotionGrantsRitual) {
    CombatState s = MakeCombat();
    ASSERT_TRUE(use_potion(s, PotionId::CULTIST_POTION, 0));
    drain_actions(s);
    EXPECT_EQ(player_power_stack(s, PowerId::RITUAL), 1);
}

// --- Snecko Oil (RANDOMIZE_HAND_COST, opcode 60) -----------------------------
//
// SneckoOil.use (SneckoOil.java:41-46) is exactly two addToBots:
//     addToBot(new DrawCardAction(player, this.potency));   // potency 5
//     addToBot(new RandomizeHandCostAction());
// so the DRAW resolves FIRST and the randomize walks the POST-draw hand. It
// re-costs the WHOLE hand, not merely the drawn cards -- the single fact most
// easily got backwards, and the one these tests exist to pin.

// The reference stream, re-derived independently of the engine:
// RandomizeHandCostAction.update (:26-38) spends one card_random_rng random(3)
// per hand card whose BASE cost is non-negative, in hand order, and writes only
// when the roll differs from that base cost.
int expected_cost_after_roll(RngStream& ref, int base_cost) {
    const int32_t rolled = random(ref, 3);
    return rolled == base_cost ? base_cost : static_cast<int>(rolled);
}

TEST(Potions, SneckoOilDrawsFiveThenRandomizesTheWholeHand) {
    CombatState s = MakeCombat();
    // Two cards already in hand. STRIKE and DEFEND both cost 1.
    s.card_pool[20].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.card_pool[20].cost_now = 1;
    s.card_pool[21].card_id = static_cast<uint16_t>(CardId::DEFEND);
    s.card_pool[21].cost_now = 1;
    s.hand[0] = 20;
    s.hand[1] = 21;
    s.hand_count = 2;
    seed_draw_pile(s, 5);  // five STRIKEs, cost 1 each

    s.card_random_rng = from_seed(29);
    RngStream ref = from_seed(29);

    ASSERT_TRUE(use_potion(s, PotionId::SNECKO_OIL, 0));
    drain_actions(s);

    ASSERT_EQ(s.hand_count, 7) << "potency 5 drawn on top of the 2 already held";
    EXPECT_EQ(s.draw_count, 0);
    // The pre-existing hand cards come FIRST in hand order, so they are rolled
    // first -- the randomize is not restricted to the drawn five.
    for (uint8_t i = 0; i < 7; ++i) {
        EXPECT_EQ(s.card_pool[s.hand[i]].cost_now,
                  expected_cost_after_roll(ref, 1))
            << "hand slot " << static_cast<int>(i);
    }
    EXPECT_EQ(s.card_random_rng.counter, 7)
        << "one draw per hand card with a non-negative base cost";
    EXPECT_EQ(s.card_random_rng.s0, ref.s0);
    EXPECT_EQ(s.card_random_rng.s1, ref.s1);
    EXPECT_EQ(potion_def(PotionId::SNECKO_OIL)->potency, 5);
}

// `card.cost < 0` short-circuits the whole per-card body, so an unplayable
// status costs NO draw at all -- the `||` is short-circuit and the assignment
// sits in its RIGHT operand (RandomizeHandCostAction.java:30).
TEST(Potions, SneckoOilSpendsNoDrawOnAnUnplayableCard) {
    CombatState s = MakeCombat();
    s.card_pool[20].card_id = static_cast<uint16_t>(CardId::WOUND);
    s.card_pool[20].cost_now = card_cost(*card_def(CardId::WOUND), 0);
    s.card_pool[20].flags = card_flags(*card_def(CardId::WOUND), 0);
    ASSERT_TRUE(has_card_flag(s.card_pool[20].flags, CardFlag::UNPLAYABLE));
    s.card_pool[21].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.card_pool[21].cost_now = 1;
    s.hand[0] = 20;
    s.hand[1] = 21;
    s.hand_count = 2;
    // Empty draw pile: DRAW with nothing to draw and nothing to reshuffle adds
    // no cards and spends no rng, so the only draws here are the randomize's.
    s.card_random_rng = from_seed(29);

    ASSERT_TRUE(use_potion(s, PotionId::SNECKO_OIL, 0));
    drain_actions(s);

    EXPECT_EQ(s.card_random_rng.counter, 1) << "only the STRIKE is rolled";
    EXPECT_EQ(s.card_pool[20].cost_now,
              card_cost(*card_def(CardId::WOUND), 0))
        << "the Wound's cost is untouched";
}

// The write is `costForTurn = cost = newCost` -- PERMANENT for the instance --
// so COST_MODIFIED_FOR_TURN must be CLEARED, not set, or the end-of-turn sweep
// would restore the registry cost and silently undo the potion.
TEST(Potions, SneckoOilCostIsPermanentNotThisTurnOnly) {
    CombatState s = MakeCombat();
    // Seed hunt: a roll that actually differs from BASH's base cost of 2.
    int64_t seed = 0;
    int32_t rolled = 0;
    for (int64_t candidate = 1; candidate < 200; ++candidate) {
        RngStream probe = from_seed(candidate);
        const int32_t r = random(probe, 3);
        if (r != 2) {
            seed = candidate;
            rolled = r;
            break;
        }
    }
    ASSERT_NE(seed, 0);

    s.card_pool[20].card_id = static_cast<uint16_t>(CardId::BASH);
    s.card_pool[20].cost_now = card_cost(*card_def(CardId::BASH), 0);
    ASSERT_EQ(s.card_pool[20].cost_now, 2);
    s.hand[0] = 20;
    s.hand_count = 1;
    s.card_random_rng = from_seed(seed);

    ASSERT_TRUE(use_potion(s, PotionId::SNECKO_OIL, 0));
    drain_actions(s);

    EXPECT_EQ(s.card_pool[20].cost_now, rolled);
    EXPECT_FALSE(has_card_flag(s.card_pool[20].flags,
                               CardFlag::COST_MODIFIED_FOR_TURN))
        << "the new cost is card.cost, not costForTurn alone";
}

// The two-step program is the potion's whole body: DRAW then RANDOMIZE_HAND_COST,
// in that queue order. Pinning the QUEUE (not just the outcome) is what catches
// a re-authoring that randomizes before drawing.
TEST(Potions, SneckoOilQueuesDrawAheadOfTheRandomize) {
    CombatState s = MakeCombat();
    ASSERT_TRUE(use_potion(s, PotionId::SNECKO_OIL, 0));
    ASSERT_EQ(s.action_count, 2);
    const ActionQueueItem& first = s.action_queue[s.action_head];
    const ActionQueueItem& second =
        s.action_queue[(s.action_head + 1) % kActionQueueCap];
    EXPECT_EQ(first.opcode, static_cast<uint16_t>(Opcode::DRAW));
    EXPECT_EQ(first.amount, 5);
    EXPECT_EQ(second.opcode,
              static_cast<uint16_t>(Opcode::RANDOMIZE_HAND_COST));
}

// --- Registry-level coverage of the DEFERRED native potions ------------------
// Their runtime bodies land with their dependency (B3.4 powers / CHOOSE verb /
// run layer); the registry row (rarity, potency, native flag) is complete now.

TEST(Potions, AllThirtyThreeRegistered) {
    for (int id = 1; id <= kPotionPoolSize; ++id) {
        const PotionDef* d = potion_def(static_cast<PotionId>(id));
        ASSERT_NE(d, nullptr) << "potion id " << id;
        EXPECT_EQ(static_cast<int>(d->id), id);
    }
    EXPECT_EQ(potion_def(PotionId::NONE), nullptr);
    EXPECT_EQ(potion_def(static_cast<PotionId>(kPotionPoolSize + 1)), nullptr);
}

TEST(Potions, RarityAndPotencyTable) {
    struct Row { PotionId id; PotionRarity rarity; int potency; bool native; };
    const Row rows[] = {
        {PotionId::BLOOD_POTION, PotionRarity::COMMON, 20, true},
        {PotionId::ELIXIR, PotionRarity::UNCOMMON, 0, true},
        {PotionId::HEART_OF_IRON, PotionRarity::RARE, 6, false},
        // Un-deferred by the potion-support-powers follow-up -> DATA (native false).
        {PotionId::DEXTERITY_POTION, PotionRarity::COMMON, 2, false},
        {PotionId::STEROID_POTION, PotionRarity::COMMON, 5, false},
        {PotionId::SPEED_POTION, PotionRarity::COMMON, 5, false},
        {PotionId::REGEN_POTION, PotionRarity::UNCOMMON, 5, false},
        {PotionId::LIQUID_BRONZE, PotionRarity::UNCOMMON, 3, false},
        {PotionId::ESSENCE_OF_STEEL, PotionRarity::UNCOMMON, 4, false},
        {PotionId::CULTIST_POTION, PotionRarity::RARE, 1, false},
        // Still native + deferred (recursive-play opcode, not powers.yaml).
        {PotionId::DUPLICATION_POTION, PotionRarity::UNCOMMON, 1, true},
        {PotionId::FRUIT_JUICE, PotionRarity::RARE, 5, true},
        {PotionId::FAIRY_POTION, PotionRarity::RARE, 30, true},
        {PotionId::SMOKE_BOMB, PotionRarity::RARE, 0, true},
        {PotionId::ENTROPIC_BREW, PotionRarity::RARE, 0, true},
    };
    for (const Row& r : rows) {
        const PotionDef* d = potion_def(r.id);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(d->rarity, r.rarity) << "id " << static_cast<int>(r.id);
        EXPECT_EQ(d->potency, r.potency) << "id " << static_cast<int>(r.id);
        EXPECT_EQ(d->native, r.native) << "id " << static_cast<int>(r.id);
    }
}

// --- The implemented-ness gate (potion legality trap) ------------------------

TEST(Potions, DeferredNativePotionIsRefusedNotSilentlyNoOped) {
    // A still-deferred native potion (Duplication -- blocked on the recursive-
    // play opcode) must FAIL rather than quietly do nothing: run_advance's
    // step_potion reads the false return as "the use did not happen" and keeps
    // the slot, so the player can never burn a potion for no effect.
    CombatState s = MakeCombat();
    const CombatState before = s;
    EXPECT_FALSE(potion_use_implemented(PotionId::DUPLICATION_POTION));
    EXPECT_FALSE(use_potion(s, PotionId::DUPLICATION_POTION, 0));
    EXPECT_EQ(s.action_count, 0);
    EXPECT_EQ(s.player_power_count, before.player_power_count);
    EXPECT_EQ(s.player_hp, before.player_hp);
}

TEST(Potions, ImplementedNessIsRegistryDrivenForDataPotions) {
    // Every non-`native` row is a data effect program and therefore runs -- this
    // is the self-healing half of the gate: un-deferring a potion in
    // registry/potions.yaml makes it legal with no code change.
    for (int i = 1; i <= kPotionPoolSize; ++i) {
        const PotionId id = static_cast<PotionId>(i);
        const PotionDef* d = potion_def(id);
        ASSERT_NE(d, nullptr);
        if (!d->native) {
            EXPECT_TRUE(potion_use_implemented(id))
                << "data potion id " << i << " must be usable";
        }
    }
    EXPECT_FALSE(potion_use_implemented(PotionId::NONE));
}

TEST(Potions, ImplementedAndDeferredNativeRosters) {
    // The `native` rows are the hand-written ones, so they are named explicitly.
    for (PotionId id : {PotionId::BLOOD_POTION, PotionId::BLESSING_OF_THE_FORGE,
                        PotionId::ELIXIR, PotionId::ATTACK_POTION,
                        PotionId::SKILL_POTION, PotionId::POWER_POTION,
                        PotionId::COLORLESS_POTION, PotionId::LIQUID_MEMORIES,
                        PotionId::GAMBLERS_BREW, PotionId::FRUIT_JUICE,
                        PotionId::ENTROPIC_BREW, PotionId::SMOKE_BOMB}) {
        EXPECT_TRUE(potion_use_implemented(id))
            << "native id " << static_cast<int>(id) << " has a body";
    }
    // The card-CHOOSE group, recursive play, cost randomization, and the
    // out-of-combat revive are all still deferred.
    // SNECKO_OIL LEFT this list when RANDOMIZE_HAND_COST (opcode 60) landed: its
    // row is now a DATA program, so the gate answers from the registry.
    EXPECT_TRUE(potion_use_implemented(PotionId::SNECKO_OIL));
    for (PotionId id : {PotionId::DISTILLED_CHAOS,
                        PotionId::DUPLICATION_POTION,
                        PotionId::FAIRY_POTION}) {
        EXPECT_FALSE(potion_use_implemented(id))
            << "deferred id " << static_cast<int>(id) << " must not be usable";
        CombatState s = MakeCombat();
        EXPECT_FALSE(use_potion(s, id, 0));
        EXPECT_EQ(s.action_count, 0);
    }
}

// --- A11 potion-slot count (design §5.4; AbstractPlayer.java:211-213) ---------

TEST(Potions, PotionSlotCount) {
    EXPECT_EQ(potion_slot_count(0), 3);    // base
    EXPECT_EQ(potion_slot_count(10), 3);   // still 3 below A11
    EXPECT_EQ(potion_slot_count(11), 2);   // A11: one fewer
    EXPECT_EQ(potion_slot_count(20), 2);   // S1 A20 bracket = 2
}

// --- Identity roll (trap 14; AbstractDungeon.java:829-850) --------------------

TEST(Potions, TierGateBoundaries) {
    EXPECT_EQ(potion_tier_for_roll(0), PotionRarity::COMMON);
    EXPECT_EQ(potion_tier_for_roll(64), PotionRarity::COMMON);
    EXPECT_EQ(potion_tier_for_roll(65), PotionRarity::UNCOMMON);
    EXPECT_EQ(potion_tier_for_roll(89), PotionRarity::UNCOMMON);
    EXPECT_EQ(potion_tier_for_roll(90), PotionRarity::RARE);
    EXPECT_EQ(potion_tier_for_roll(99), PotionRarity::RARE);
}

// Independent hand-derivation of returnRandomPotion over an identical stream:
// replay the exact draw sequence (one d100 tier roll, then reject-sample
// random(poolSize-1) until the rolled potion's rarity matches), counting draws.
// Then assert the engine function lands on the same PotionId AND consumes
// EXACTLY that many draws (rng.counter delta) -- the trap-14 acceptance.
void CheckIdentityRoll(int64_t seed) {
    RngStream ref = from_seed(seed);
    RngStream sut = from_seed(seed);

    const int roll = random(ref, 0, 99);
    const PotionRarity tier = potion_tier_for_roll(roll);
    int expected_draws = 1;  // the tier roll
    PotionId expected = static_cast<PotionId>(random(ref, kPotionPoolSize - 1) + 1);
    ++expected_draws;
    while (potion_def(expected)->rarity != tier) {
        expected = static_cast<PotionId>(random(ref, kPotionPoolSize - 1) + 1);
        ++expected_draws;
    }

    const int32_t before = sut.counter;
    const PotionId got = return_random_potion(sut);
    const int32_t consumed = sut.counter - before;

    EXPECT_EQ(got, expected) << "seed " << seed;
    EXPECT_EQ(consumed, expected_draws) << "seed " << seed << " draw count";
    EXPECT_EQ(potion_def(got)->rarity, tier) << "seed " << seed << " rarity";
    // The two streams landed on identical engine state (same draws consumed).
    EXPECT_EQ(sut.s0, ref.s0) << "seed " << seed;
    EXPECT_EQ(sut.s1, ref.s1) << "seed " << seed;
}

TEST(Potions, IdentityRollConsumesExactlyObservedDraws) {
    // A spread of seeds so the rejection loop runs 0, 1, and several times, and
    // all three tiers are hit across the set.
    for (int64_t seed : {1LL, 2LL, 7LL, 13LL, 42LL, 99LL, 123LL, 777LL, 2024LL,
                         31337LL}) {
        CheckIdentityRoll(seed);
    }
}

}  // namespace
}  // namespace sts::engine
