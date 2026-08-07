#pragma once

// Malleable -- native power-hook body (registry/powers.yaml id 95,
// PowerId::MALLEABLE).
//
// The Snake Plant's escalating retaliation shield (MalleablePower.java, read in
// full). Three live hooks and NO damage-pipeline case:
//
//   * ON_ATTACKED     (:62-75)  a real, SURVIVABLE hit queues Block `amount`,
//                               then escalates amount by 1 SYNCHRONOUSLY.
//   * AT_END_OF_TURN  (:44-51)  MONSTER owner: amount = basePower -- the reset.
//   * AT_END_OF_ROUND (:53-60)  PLAYER owner: the exact mirror. No S1/S2 effect
//                               grants Malleable to the player, so this arm is
//                               DEAD -- bound and documented rather than dropped,
//                               because it is real Java and costs one branch.
//
// It modifies no damage number in any pass (onAttacked returns `damageAmount`
// unchanged, :74) and no block number either -- it PRODUCES block rather than
// scaling one -- so none of the four count-guarded switches in interp_damage.cpp
// / interp_block.cpp gains a case.
//
// PowerSlot.counter IS basePower. MalleablePower's `basePower` is a private field
// (:21) set in the ctor (:33) and thereafter written ONLY by stackPower (:81), so
// it lives where every other ctor-set second number does: op_apply_power's
// NEW-SLOT path writes counter = amount for this PowerId -- the Flight and
// Panache precedent.
//
// UNLIKE FLIGHT, THE STACKING PATH MUST CARRY THE COUNTER TOO, and that is the
// one place this power is not a copy of an existing shape. Flight does not
// override stackPower, so a second application raises the live amount and leaves
// the refresh target where the first instance set it. Malleable DOES override it
// (:79-82):
//
//     public void stackPower(int stackAmount) {
//         this.amount += stackAmount;
//         this.basePower += stackAmount;
//     }
//
// -- so a re-application permanently raises the RESET TARGET as well. op_apply_
// power's stacking branch has a MALLEABLE case for exactly that. Unreachable in
// S1/S2 (the Snake Plant is solo and applies it once, SnakePlant.java:69-72), and
// implemented anyway because it is one line and its absence would be a silent
// wrong answer the day a second granter lands.
//
// THE ESCALATION IS SYNCHRONOUS, THE BLOCK IS QUEUED, and that split is
// observable. `++this.amount` (:72) runs inside onAttacked while the
// GainBlockAction is addToBot'd (:70) -- so a multi-hit attack against the Snake
// Plant escalates once per hit and the queued blocks then land at the ESCALATING
// values (3, then 4, then 5 for a triple-hit), not three times at 3.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_malleable(CombatState& state, Hook hook,
                            const HookContext& ctx) noexcept;

}  // namespace sts::engine
