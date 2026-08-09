#pragma once

// Intangible (the MONSTER-side class) -- native power-hook body
// (registry/powers.yaml id 107, PowerId::INTANGIBLE_MONSTER).
//
// TWO CLASSES, TWO ROWS. IntangiblePower's POWER_ID is "Intangible"
// (IntangiblePower.java:19); IntangiblePlayerPower's is "IntangiblePlayer"
// (powers.yaml id 29). The oracle joins on that literal, so they cannot share a
// row -- and they do not behave the same either:
//
//                        id 29 (player)              id 107 (monster)
//   decay hook           atEndOfROUND                atEndOfTURN
//   justApplied latch    none                        yes (ctor, :33)
//   zero-amount arm      collapsed into REDUCE       spelled as a REMOVE
//
// The decay-hook difference is the load-bearing one. For a MONSTER owner,
// atEndOfTurn is dispatch_at_end_of_round's FIRST walk
// (MonsterGroup.applyEndOfTurnPowers:290-304 -> applyEndOfTurnTriggers), a whole
// phase ahead of the atEndOfRound walk id 29 rides on. Binding this to
// AT_END_OF_ROUND would tick it in the wrong phase.
//
// ONE hook here:
//
//   * AT_END_OF_TURN (:54-66)  `if (justApplied) { justApplied = false; return; }`
//                              then, at amount 0, a RemoveSpecificPowerAction,
//                              else a ReducePowerAction of 1.
//
// NATIVE for the latch, exactly as DrawReductionPower (id 111) is: `justApplied`
// is a per-INSTANCE bool the ctor sets unconditionally (:33), so it lives in
// PowerSlot.counter (1 == still just-applied), written on op_apply_power's
// NEW-SLOT path. A data at_end_of_turn program cannot skip a tick.
//
// THE RHYTHM THIS PRODUCES, at the Nemesis's amount 1: applied on the monster's
// turn N; the round-N tick spends the latch; the round-N+1 tick reduces to zero
// and op_reduce_power removes the slot, at the start of the player's turn N+2;
// the Nemesis re-arms it on its own turn N+2 because takeTurn's guard
// (Nemesis.java:114) finds it absent. One real-damage turn in three.
//
// THE DAMAGE CAP IS NOT HERE. atDamageFinalReceive (:42-47) is a
// damage-pipeline pass and lives in interp_damage.cpp beside INTANGIBLE's, and
// the Nemesis's own damage() override adds a second, TYPE-AGNOSTIC cap that
// catches THORNS and HP_LOSS -- see monster_nemesis.hpp note (2).

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_intangible_monster(CombatState& state, Hook hook,
                                     const HookContext& ctx) noexcept;

}  // namespace sts::engine
