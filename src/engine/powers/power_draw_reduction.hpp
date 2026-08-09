#pragma once

// Draw Reduction -- native power-hook body (registry/powers.yaml id 111,
// PowerId::DRAW_REDUCTION).
//
// The Time Eater's Head Slam debuff (DrawReductionPower.java, read in full).
// THREE Java overrides; only ONE of them is a hook here, and the other two are
// the interesting half.
//
//   (a) onInitialApplication (:31-34)  --AbstractDungeon.player.gameHandSize
//   (c) onRemove             (:47-50)  ++AbstractDungeon.player.gameHandSize
//
// (a) and (c) are a BALANCED PAIR around the power's lifetime, so the engine
// DERIVES them instead of storing them: game_hand_size() (action_queue.hpp)
// subtracts 1 while the PLAYER carries this power. That is the same
// derive-don't-store call energyMaster / masterHandSize already make, and it is
// EXACT rather than merely convenient -- the pair cannot get out of balance
// because it has no independent state to get out of balance with.
//
// The derivation also captures FOR FREE the quirk a literal port gets wrong:
// onInitialApplication fires ONLY on the first application. A second Head Slam
// stacks `amount` to 2 (AbstractPower's un-overridden additive stackPower) while
// the hand shrinks by exactly ONE card, because addPower hands the amount to the
// live object and discards the freshly constructed one, ctor body and all
// (AbstractCreature.java:506-513). "One card while ANY stack is present" is
// literally what the presence test computes.
//
// Because the pair is derived, this row binds NO ON_POWER_REMOVED hook: there is
// nothing left for onRemove to do. That is a deliberate absence, not a gap.
//
//   (b) atEndOfRound (:36-45)  IS the hook:
//           if (this.justApplied) { this.justApplied = false; return; }
//           this.addToBot(new ReducePowerAction(owner, owner, POWER_ID, 1));
//
// `justApplied` is a private bool set true at the FIELD INITIALIZER (:17) and
// cleared only in that branch -- a per-INSTANCE latch, so it lives in
// PowerSlot.counter (1 == still just-applied), written on op_apply_power's
// NEW-SLOT path exactly like the three duration debuffs' identically-shaped
// latch. Like theirs, the STACKING path deliberately does not rewrite it, which
// is what stops a re-application re-arming the skip. Net effect: a Head Slam
// landed on turn N first ticks down at the END of turn N+1, so the player loses
// a card for two draws, not one.
//
// ReducePowerAction removes rather than reduces once the request meets the stack
// (ReducePowerAction.java:45-51), so the power takes itself off at 1 -> 0 through
// remove_slot_at, and the derived hand size comes back with it.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_draw_reduction(CombatState& state, Hook hook,
                                 const HookContext& ctx) noexcept;

}  // namespace sts::engine
