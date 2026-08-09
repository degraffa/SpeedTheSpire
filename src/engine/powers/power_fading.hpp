#pragma once

// Fading -- native power-hook body (registry/powers.yaml id 103,
// PowerId::FADING).
//
// The Transient's countdown to nothing (FadingPower.java, 48 lines, read in
// full; the class body is :16-47). ONE live hook, and it is the FIRST BINDER of Hook::DURING_TURN (15) --
// the S2.2F framework hook whose dispatch site landed with no consumer:
//
//   * DURING_TURN (:37-46)  amount == 1 (and the owner not already dying)
//                           -> SuicideAction(owner), the ONE-ARG ctor;
//                           otherwise ReducePowerAction(owner, owner, 1).
//
// It modifies no damage number and no block number, so none of the
// count-guarded switches in interp_damage.cpp / interp_block.cpp gains a case.
//
// FIVE THINGS TO GET RIGHT.
//
// (1) IT FIRES AFTER takeTurn, NOT BEFORE. GameActionManager.java:322-323 is
//     literally `m.takeTurn(); m.applyTurnPowers();`, and applyTurnPowers
//     (AbstractCreature.java:535-539) is the duringTurn walk. So the Transient
//     ATTACKS ON THE TURN IT DIES: at A20 (Fading 6) it lands six attacks at
//     40/50/60/70/80/90 and then explodes. Anything this body queues therefore
//     sits BEHIND everything takeTurn queued -- including a trailing
//     RollMoveAction, if the owner had one (the Transient does not).
//
// (2) THE SUICIDE IS THE ONE-ARG SuicideAction(m), WHICH DEFAULTS
//     triggerRelics TO **TRUE** (SuicideAction.java:17-19). That is the opposite
//     of the splitting slimes, which pass false. So this is a FULL death: the
//     dying monster's own die() body runs (with its veto), then its powers'
//     onDeath, then the player's relics' onMonsterDeath, then the post-super
//     body. The SUICIDE opcode (27) carries the selector in `flags` bit 0 and
//     this body SETS it.
//
// (3) IT IS A DEATH, NOT AN ESCAPE. Nothing here touches isEscaping, so
//     kMonsterFlagEscaped stays clear and the reward path counts the Transient as
//     killed. (The combat-end check treats a death and an escape alike; reward
//     assembly does not, which is why the distinction is worth stating.)
//
// (4) THE `!isDying` GUARD IS REAL. `if (this.amount == 1 && !this.owner.isDying)`
//     -- an owner already dying this tick must not queue a second death. The
//     engine's stand-in for isDying at a monster that has been zeroed is hp <= 0
//     (monster_dead_or_escaped / monster_basically_dead, combat_state.hpp).
//
// (5) THE `else` COVERS TWO CASES, NOT ONE -- amount >= 2, AND amount == 1 with
//     an owner already dying. The death arm's guard is a CONJUNCTION, so the
//     isDying case falls through to the reduce rather than out of the body, and
//     that reduce is the one call that can take a Fading slot to zero and reach
//     ReducePowerAction's removal path (ReducePowerAction.java:45-51). It is
//     unobservable -- the owner is mid-death and the slot goes with it -- but the
//     body queues it anyway, because "unreachable, so I returned instead" is how
//     an ordering divergence gets built on top of a correct-sounding claim. An
//     earlier draft of this file asserted the removal path unreachable AND
//     returned; the assertion was wrong about the Java in exactly the way that
//     made the missing arm look deliberate.
//
// GOLD: SuicideAction.update also sets `m.gold = 0` before die()
// (SuicideAction.java:31). CHECKED AND FOUND DEAD -- no room or dungeon in the
// decompiled tree reads a monster's gold field, and combat gold rewards come
// from an already-rolled treasure_rng int. So a faded-out Transient and a killed
// one pay the same, there is no CombatState gold field to zero, and the negative
// is pinned by a named test rather than left as an unstated assumption. See the
// gold paragraph in the monsters.yaml id-55 provenance.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_fading(CombatState& state, Hook hook,
                         const HookContext& ctx) noexcept;

}  // namespace sts::engine
