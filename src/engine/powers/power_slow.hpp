#pragma once

// Slow -- native power-hook body (registry/powers.yaml id 106, PowerId::SLOW).
//
// The Giant Head's card tax (SlowPower.java, 63 lines, read in full). TWO hooks
// and one damage-pipeline case:
//
//   * ON_AFTER_USE_CARD (:51-54)  every card the player plays stacks +1 on the
//                                 OWNER, with no card-type test of any kind.
//   * AT_END_OF_ROUND   (:38-41)  `this.amount = 0` -- a SYNCHRONOUS RESET, not
//                                 a removal and not a ReducePowerAction.
//   * atDamageReceive   (:56-62)  +10% incoming NORMAL damage per stack, which
//                                 lives in interp_damage.cpp's at_damage_receive
//                                 like every other damage-pipeline power.
//
// NATIVE FOR THE RESET, precisely. The stack-up half IS expressible as a data
// hook -- it is one APPLY_POWER of SLOW at amount 1 onto SELF -- but the reset
// is not: no opcode assigns a literal amount, and REDUCE_POWER down to zero
// DELETES the slot (op_reduce_power -> remove_slot_at), which is the one thing
// atEndOfRound must not do. The slot has to SURVIVE at zero, because the Giant
// Head's pre-battle application is itself at zero and the whole power is a
// per-turn counter that lives for the fight. Both hooks are therefore in one
// body, so the pair reads as the single class it is.
//
// THE OWNER IS A MONSTER, which is why the hook is ON_AFTER_USE_CARD (16) and
// not ON_USE_CARD (1). onAfterUseCard fires from UseCardAction.update after the
// played card's program has resolved and walks PLAYER POWERS then MONSTER POWERS
// (UseCardAction.java:79-88); onUseCard fires from the constructor, before the
// card does anything, and walks a different participant list. The distinction is
// observable: a card that kills the Giant Head does not stack Slow on the way
// out, and a card played the turn Slow is reset counts from zero.
//
// AMOUNT ZERO IS A REAL STATE. GiantHead.usePreBattleAction applies
// `new SlowPower(this, 0)` (GiantHead.java:82), op_apply_power's new-slot path
// stores the 0 verbatim, and atDamageReceive at 0 multiplies by 1.0f. A reader
// who "fixes" the zero into a one changes the Giant Head's opening turn.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_slow(CombatState& state, Hook hook,
                       const HookContext& ctx) noexcept;

}  // namespace sts::engine
