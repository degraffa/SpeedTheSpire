#pragma once

// The Maw (the solo Act-3 STRONG group "Maw", encounters.yaml id 50). Stats and
// move effects are generated registry data (monsters.yaml id 56); move SELECTION
// is native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   Maw.java:34-143; WeakPower.java / FrailPower.java / StrengthPower.java (all
//   three already registered); AbstractMonster.java:135-155 (the zero-RNG ctor),
//   705-715 (init/rollMove), 431-491 (moveHistory / lastMove);
//   RollMoveAction.java:17-21.
//
// THE MOVE TREE (getMove, Maw.java:117-136) IS FOUR ORDERED GATES WITH A
// SIDE EFFECT AT THE TOP:
//
//   ++turnCount;                                     // EVERY call, init included
//   !roared                         -> ROAR
//   num < 50 && !lastMove(NOMNOMNOM)-> NOMNOMNOM, hit count = turnCount / 2
//   lastMove(SLAM) || lastMove(NOM) -> DROOL
//                                   -> SLAM
//
// NO ASCENSION BRANCH ANYWHERE IN IT. Every A17 difference this monster has is an
// AMOUNT (strUp and terrifyDur, both +2), and both live in the registry columns.
//
// FIVE THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) ZERO monster_hp_rng DRAWS, AND NO A7 TIER. setHp is NEVER called: the ctor
//     hands maxHealth 300 straight to super (:61) and AbstractMonster's ctor
//     (:135-155) assigns `currentHealth = maxHealth` with no RNG in it. The
//     Spheric Guardian shape -- and, unusually for a normal, there is no
//     ascension HP branch at all. Contrast the Spire Growth and the Writhing
//     Mass in this SAME BATCH, which call the one-arg setHp and DO draw.
//
// (2) `roared` IS SET IN takeTurn, NOT IN getMove (:92). So the gate stays shut
//     until the roar has actually RESOLVED, which -- since init's rollMove runs
//     before any turn -- makes the opening telegraph unconditionally ROAR, at
//     every ascension and every seed.
//
// (3) `turnCount` STARTS AT ONE (:56) AND IS PRE-INCREMENTED ON EVERY getMove
//     INCLUDING THE INIT ROLL. Walk it through: init rolls, turnCount 1 -> 2,
//     ROAR. Turn 1's takeTurn roars and sets `roared`, then its trailing
//     RollMoveAction takes turnCount 2 -> 3, so the first NOMNOMNOM the monster
//     can telegraph bites 3/2 == ONE time. It grows to two bites at turnCount
//     4-5, three at 6-7, and so on -- and Strength applies PER BITE, which is
//     what makes a Drooled-up Maw's late Nom the fight's spike.
//
// (4) THE NOMNOMNOM HIT COUNT IS NOT A TIER COLUMN AND CANNOT BE. It is
//     `turnCount / 2` -- per-instance, unbounded, and a function of how long the
//     combat has run. The registry row authors ONE template DAMAGE step and this
//     module emits it that many times through the shared per-step helper
//     queue_monster_move_effect (the Healer's fan-out precedent; no new helper).
//     The 3-arg vs 5-arg setMove split at :124-128 is a TELEGRAPH difference
//     only -- one bite shows a bare number, two or more show "5 x N" -- and the
//     engine's intent field carries neither, so it costs nothing here.
//
// (5) ZERO EXTRA ai_rng DRAWS. One random(99) at init (READ -- gate 2 consults
//     it, though at turn 1 gate 1 answers first), then one per turn from the
//     trailing RollMoveAction (:113). No randomBoolean, no recursion.
//
// die() (:138-142) is `super.die()` plus one UNSEEDED sound, so no MonsterDieFn.
// There is no damage() override at all.
//
// STORAGE. `roared` is kMonsterFlagMawRoared (combat_state.hpp), a DELIBERATE
// reuse of the large slimes' split bit under the type-scoped policy. `turnCount`
// takes the WHOLE of MonsterState.pad0 and saturates at 255 rather than wrapping:
// the Java int is unbounded, but 255 is 127 bites of 5 in a single turn before
// any Strength, so a combat that reaches it has long since ended. The saturation
// is a statement that the case is unreachable, and a named test says so.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// `private int turnCount = 1;` (Maw.java:56) -- the value pad0 is initialised to,
// BEFORE the init rollMove pre-increments it.
inline constexpr uint8_t kMawInitialTurnCount = 1;

// getMove's decision (Maw.java:117-136) as a pure function of the record: it
// reads and WRITES `m` (the turnCount pre-increment is part of the decision, not
// a caller's job) and takes `num`. No ascension parameter, because the tree has
// no ascension branch -- stating that by omission is the honest spelling.
void maw_decide_move(MonsterState& m, int32_t num) noexcept;

void maw_init(CombatState& state, uint8_t monster_index) noexcept;

// The trailing RollMoveAction (Maw.java:113), reached by all four cases.
void maw_roll_move(CombatState& state, uint8_t monster_index) noexcept;

void maw_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
