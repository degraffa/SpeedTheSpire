// B3.23 potions (tier-2, constructed states). Each Ironclad potion's USE effect
// + potency at the S1 A20 bracket, the trap-14 rejection-sampling identity roll,
// and the A11 potion-slot count. Every number is hand-derived from the cited
// decompiled Java (registry/potions.yaml carries the per-potion provenance).
//
// SCOPE. Potions whose USE the frozen opcode set + an already-registered power
// (Strength/Vulnerable/Weak/Artifact/Metallicize) express are DATA programs and
// are checked end-to-end (queue -> pump -> effect). BLOOD_POTION's percent heal
// is a native body and is checked directly. The remaining native potions grant
// powers not yet in powers.yaml (B3.4) or need verbs owned by other tasks
// (in-combat CHOOSE = B3.4, recursive play, run-layer mutation, combat escape);
// their bodies are DEFERRED (potions.cpp), so here they are checked at the
// registry level -- correct rarity/potency/native flag -- with the runtime
// effect landing with its dependency (B3.23 Log).

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

TEST(Potions, ExplosivePotionHitsAllEnemies) {
    CombatState s = MakeCombat(3, 50);  // three live monsters, no powers
    ASSERT_TRUE(use_potion(s, PotionId::EXPLOSIVE_POTION, 0));
    drain_actions(s);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(s.monsters[i].hp, 40) << "monster " << i;  // 50 - 10
    }
    EXPECT_EQ(potion_def(PotionId::EXPLOSIVE_POTION)->potency, 10);
}

TEST(Potions, FirePotionDamagesTarget) {
    CombatState s = MakeCombat(2, 50);
    ASSERT_TRUE(use_potion(s, PotionId::FIRE_POTION, 1));  // target monster slot 1
    drain_actions(s);
    EXPECT_EQ(s.monsters[0].hp, 50);          // untouched
    EXPECT_EQ(s.monsters[1].hp, 30);          // 50 - 20
    EXPECT_EQ(potion_def(PotionId::FIRE_POTION)->potency, 20);
}

TEST(Potions, FirePotionScaledByTargetVulnerable) {
    // THORNS applyEnemyPowersOnly: enemy Vulnerable (target-side) scales it x1.5;
    // the player has no Strength, so NORMAL and THORNS coincide on the number.
    CombatState s = MakeCombat(1, 50);
    give_monster_power(s, 0, PowerId::VULNERABLE, 1);
    ASSERT_TRUE(use_potion(s, PotionId::FIRE_POTION, 0));
    drain_actions(s);
    EXPECT_EQ(s.monsters[0].hp, 20);  // 50 - floor(20 * 1.5) = 50 - 30
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

TEST(Potions, DeferredNativePotionIsNoOpInCombat) {
    // A still-deferred native potion (Duplication -- blocked on the recursive-play
    // opcode) must not touch combat state until its dependency lands.
    CombatState s = MakeCombat();
    const CombatState before = s;
    ASSERT_TRUE(use_potion(s, PotionId::DUPLICATION_POTION, 0));
    EXPECT_EQ(s.action_count, 0);
    EXPECT_EQ(s.player_power_count, before.player_power_count);
    EXPECT_EQ(s.player_hp, before.player_hp);
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
