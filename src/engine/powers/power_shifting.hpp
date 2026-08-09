#pragma once

// Shifting -- native power-hook body (registry/powers.yaml id 104,
// PowerId::SHIFTING).
//
// The Transient's Strength swap (ShiftingPower.java, 48 lines, read in full).
// ONE live hook:
//
//   * ON_ATTACKED (:32-41)  a hit that got through queues -damageAmount
//                           Strength on the owner AND (unless it has Artifact)
//                           +damageAmount Shackled, which hands the Strength
//                           back at the owner's end of turn.
//
// IT IS NOT A DAMAGE-REDUCTION POWER, which is the thing the name invites you to
// assume. onAttacked returns `damageAmount` UNCHANGED (:40), it overrides no
// atDamage* and no modifyBlock, so none of the count-guarded switches in
// interp_damage.cpp / interp_block.cpp gains a case and no number anywhere is
// scaled by it. What it does is make the Transient's own Strength dive by
// whatever the player just dealt and climb back at end of turn -- and since the
// Transient's damage is a fixed ramp rather than a Strength-scaled number, the
// whole effect is invisible in its output and entirely visible in the power
// list, which the oracle compares every turn. So it has to be exact.
//
// SIX LOAD-BEARING DETAILS.
//
// (1) THE SECOND HALF IS SHACKLED (id 78, GainStrengthPower), already
//     registered with its exact at_end_of_turn program (apply Strength `amount`,
//     then remove itself). Nothing about that row changes here; this power just
//     applies it.
//
// (2) BOTH APPLICATIONS ARE addToTOP, AND THE ORDER FLIPS. The Java pushes
//     StrengthPower(-d) first and GainStrengthPower(+d) second (:34,36), and a
//     front insertion twice means the SECOND push resolves FIRST -- so the
//     executed order is Shackled, then -Strength. Inside a multi-hit attack the
//     whole swing therefore lands between hits rather than after the attack.
//
// (3) `damageAmount > 0` ONLY. A hit the owner's block fully absorbed does
//     nothing -- the dispatcher passes the POST-block number.
//
// (4) THE Artifact GUARD IS ON THE SHACKLED HALF ALONE (:35-37). With Artifact
//     the owner takes the -Strength and never gets it back. Nothing in Acts 1-3
//     gives a Transient Artifact, so the arm is dead -- and it is bound anyway,
//     the Malleable dead-arm precedent: it is real Java, it costs one branch, and
//     its absence would be a silent wrong answer rather than a missing feature.
//     Note this is a plain presence test, NOT the debuff-nullify path: -Strength
//     is a DEBUFF and op_apply_power's Artifact interception will separately
//     consume a stack for it. The Java has the same double relationship.
//
// (5) `isPostActionPower = true` (:27) IS UNMODELLED, and that is a statement
//     rather than an omission. It is an AbstractPower field that affects when
//     AbstractCreature.updatePowers runs the object's update(), i.e. animation
//     ordering; it has no effect on the action queue or on any number. Angry
//     carries it too (powers.yaml id 71's provenance says so) and is likewise
//     unaffected.
//
// (6) THE GUARD SET IS THE INTERESTING PART, AND IT IS WHY THIS POWER NEEDED A
//     FRAMEWORK CHANGE.
//
//     Every other landed ON_ATTACKED binder -- Thorns, Sharp Hide, Angry,
//     Flight, Malleable, and this batch's Reactive -- gates its own body on the
//     damage TYPE (`!= THORNS && != HP_LOSS`, or `== NORMAL`) and on
//     `info.owner != null`. Because all of them did, op_damage's dispatch could
//     hoist that test to the call site and fire ON_ATTACKED only for NORMAL
//     damage with src != tgt, which is exactly what it does
//     (interp/interp_damage.cpp) and exactly what the comment there claimed was
//     general.
//
//     ShiftingPower has NEITHER GATE. Its condition is the bare `damageAmount >
//     0`, and the Java's walk over onAttacked is unconditional across every
//     DamageType (AbstractMonster.damage, AbstractMonster.java:666-668). So in
//     the real game a Flame Barrier reflect, a Thorns tick or an HP_LOSS effect
//     landing on a Transient DOES swing its Strength -- reachable, playable, and
//     not modelled by the NORMAL-only dispatch.
//
//     The fix is deliberately NOT to widen dispatch_on_attacked. Widening it
//     would re-route every existing binder through a gate it currently gets for
//     free, changing six landed powers to fix one. Instead the framework grows a
//     SECOND, narrow walk -- dispatch_on_attacked_type_tolerant (power_hooks.hpp)
//     -- fired from the paths the NORMAL-only gate excludes, and restricted to
//     the powers that declare no type guard of their own. Today that set is
//     exactly {SHIFTING}, enumerated with the reason each other binder is out.
//     The two walks together are observationally identical to the full widening,
//     and the divergence they close is pinned by a named test.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_shifting(CombatState& state, Hook hook,
                           const HookContext& ctx) noexcept;

}  // namespace sts::engine
