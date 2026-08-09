#pragma once

// The Orb Walker -- the Act-3 weak-pool encounter "Orb Walker"
// (encounters.yaml id 45) and the Mysterious Sphere event's "2 Orb Walkers"
// (id 61). Stats and move effects are generated registry data (monsters.yaml
// id 50); move SELECTION is native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   OrbWalker.java:30-134; GenericStrengthUpPower.java (powers.yaml id 101);
//   MakeTempCardInDiscardAndDeckAction.java:30-36; CardGroup.java:463-469
//   (addToRandomSpot); AbstractMonster.java:705-715, 765-775, 431-491.
//
// (1) TWO monster_hp_rng DRAWS, AND THE FIRST ONE IS THROWN AWAY. The ctor is
//
//     super(NAME, ID, AbstractDungeon.monsterHpRng.random(90, 96), ...);   // :53
//     if (ascensionLevel >= 7) setHp(92, 102); else setHp(90, 96);         // :54-58
//
//     Java evaluates the argument list BEFORE the constructor body, so the
//     super-argument roll happens FIRST and its value is immediately overwritten
//     by setHp. It still consumed a value, so an Orb Walker costs TWO draws and
//     "2 Orb Walkers" costs FOUR -- and every later monster_hp_rng roll on that
//     floor is offset accordingly. That ordering is the registry's
//     CONSTRUCTOR_BEFORE_HP timing (S2.2F), not a hard-coded burn here: the
//     discarded-PICK burn path (burn_unspawned_ctor_rolls) has to reproduce the
//     same order, and it reads the same rolls table.
//
//     The discarded roll is TIER-INDEPENDENT (a bare 90..96 with no ascension
//     branch) while the setHp roll is tiered. At A20 that is 90..96 then 92..102.
//
// (2) LASER MAKES TWO BURNS, IN TWO PILES, AND ONLY ONE OF THEM DRAWS.
//     MakeTempCardInDiscardAndDeckAction (read in full) builds two INDEPENDENT
//     copies: a ShowCardAndAddToDrawPileEffect with randomSpot = true -- so
//     CardGroup.addToRandomSpot, one card_random_rng draw, or zero on an empty
//     draw pile -- and THEN a ShowCardAndAddToDiscardEffect, which draws nothing.
//     The registry program spells that as two MAKE_CARD steps in that order.
//
// (3) CASE 1 HAS NO `break` (OrbWalker.java:95) AND IT DOES NOT MATTER. It is the
//     last case in the switch, so the fall-through reaches only the trailing
//     RollMoveAction (:97) that case 2 reaches anyway. Recorded so nobody
//     "fixes" the transcription in a way that changes behaviour.
//
// getMove (:100-113) is a plain move-history tree with no ascension branch and
// no extra draw:
//     num < 40 : !lastTwoMoves(CLAW)  ? CLAW  : LASER
//     else     : !lastTwoMoves(LASER) ? LASER : CLAW

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// GenericStrengthUpPower's amount at the Orb Walker's pre-battle
// (OrbWalker.java:76,78): 5 from A17, 3 below. A flat restated pair, unlike the
// Spiker's composing arm.
inline constexpr int32_t kOrbWalkerStrengthUpBase = 3;  // OrbWalker.java:78
inline constexpr int32_t kOrbWalkerStrengthUpA17 = 5;   // OrbWalker.java:76

[[nodiscard]] constexpr int32_t orb_walker_strength_up(int32_t ascension) noexcept {
    return ascension >= 17 ? kOrbWalkerStrengthUpA17 : kOrbWalkerStrengthUpBase;
}

void orb_walker_init(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (OrbWalker.java:73-80): one ApplyPowerAction of
// GenericStrengthUpPower(MOVES[0], A17 ? 5 : 3) on itself. No RNG draw. The
// power's own atEndOfRound is what turns that into Strength, every round, for
// the whole fight (powers.yaml id 101 -- a data hook, not native).
void orb_walker_use_pre_battle_action(CombatState& state,
                                      uint8_t monster_index) noexcept;

// The trailing RollMoveAction (OrbWalker.java:97), reached by both cases (case 1
// by falling through). Draws one ai_rng value, which getMove reads.
void orb_walker_roll_move(CombatState& state, uint8_t monster_index) noexcept;

void orb_walker_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
