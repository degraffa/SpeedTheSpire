#pragma once

// Flight -- native power-hook body (registry/powers.yaml id 94,
// PowerId::FLIGHT).
//
// The Byrd's halving shield (FlightPower.java, read in full). Three live hooks
// and one damage-pipeline case:
//
//   * AT_START_OF_TURN   (:47-51)  amount = storedAmount -- a FULL REFRESH.
//   * ON_ATTACKED        (:65-73)  a real, NON-LETHAL hit sheds one stack.
//   * ON_POWER_REMOVED   (:75-78)  onRemove -> the owner's GROUNDED changeState.
//   * atDamageFinalReceive (:53-63) halves incoming damage -- NOT here; it is a
//                                   case in interp/interp_damage.cpp, because
//                                   that pass is a pure float function of the
//                                   slot, not a queued response.
//
// PowerSlot.counter IS storedAmount. FlightPower's `storedAmount` is a private
// field written once in the ctor and never reassigned (:24,31), so it is written
// where every other ctor field is: op_apply_power's NEW-SLOT path
// (interp/interp_powers.cpp), which sets counter = amount for this PowerId --
// the Panache precedent. Applying Flight therefore needs no special helper; an
// ordinary APPLY_POWER item is complete, and a re-application stacks `amount`
// while leaving the refresh target where the first instance set it, which is
// what AbstractPower's un-overridden stackPower does.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_flight(CombatState& state, Hook hook,
                         const HookContext& ctx) noexcept;

}  // namespace sts::engine
