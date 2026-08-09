#pragma once

// The Exploder -- the second of the three "ancient shapes" ("3 Shapes" / "4
// Shapes" / "Sphere and 2 Shapes"). Stats and move effects are generated
// registry data (monsters.yaml id 52); move SELECTION is native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   Exploder.java:26-93; ExplosivePower.java (powers.yaml id 100, the
//   self-destruct); SuicideAction.java; random/Random.java:58-61 (why
//   setHp(30,30) still draws); AbstractCreature.java:535-539 (applyTurnPowers);
//   GameActionManager.java:305-324 (where applyTurnPowers is called from).
//
// THREE THINGS TO KNOW BEFORE READING THE BODY.
//
// (1) THE SELF-DESTRUCT IS NOT A MOVE. Move 2 -- the Java constant is literally
//     named BLOCK (:48) -- is an EMPTY CASE (:78-80): it gains no block, deals no
//     damage, and does nothing at all. The 30-damage explosion belongs to
//     ExplosivePower.duringTurn, which fires from applyTurnPowers IMMEDIATELY
//     AFTER takeTurn (GameActionManager.java:322-323) and therefore lands its
//     items BEHIND everything the turn queued, including the trailing
//     RollMoveAction. Cadence with the pre-battle Explosive 3:
//
//         init      turnCount 0 -> telegraph ATTACK
//         turn 1    ATTACK, then the fuse 3 -> 2, telegraph ATTACK
//         turn 2    ATTACK, then the fuse 2 -> 1, telegraph move 2
//         turn 3    nothing, then the fuse is 1: Suicide + 30 THORNS to the player
//
// (2) setHp(30, 30) STILL CONSUMES A monster_hp_rng DRAW. Random.random(int,int)
//     is `start + nextInt(end - start + 1)` with an unconditional ++counter
//     (random/Random.java:58-61), so a degenerate range is still a draw. This is
//     the OPPOSITE of the Spheric Guardian (monsters.yaml id 30), which skips the
//     draw because setHp is never CALLED. The two look alike in a stat table and
//     are not the same thing. (At kMonsterAscension 20 the live column is the A7
//     {30,35} one anyway, so only a sub-A7 fixture could ever see the degenerate
//     range -- which is exactly why it is easy to get wrong.)
//
// (3) getMove NEVER READS `num`, BUT THE ROLL STILL HAPPENS. Every branch of
//     Exploder.getMove (:86-92) is a `turnCount` test. The rollMove draw is made
//     and discarded -- the Guardian / Spheric Guardian precedent -- and that is
//     precisely why this must be native code: the draw moves the shared ai_rng
//     stream, and in a "4 Shapes" group three other monsters see the difference.
//
// `turnCount` NEEDS NO STORAGE, AND THE OBVIOUS DERIVATION IS WRONG.
//
// getMove is `turnCount < 2 ? ATTACK : move 2`, and turnCount is incremented at
// the TOP of takeTurn (:71), so at each DECISION point turnCount is exactly the
// number of turns already taken. Reading that off the 3-slot move history:
//
//     decision      history[0], history[1]   turnCount   wanted
//     init           -,   -                      0        ATTACK
//     turn 1 tail    1,   -                      1        ATTACK
//     turn 2 tail    1,   1                      2        move 2
//     turn 3 tail    2,   1                      3        move 2
//     turn 4 tail    2,   2                      4        move 2
//
// so `last_two_moves_are(m, ATTACK)` alone -- the predicate that suggests itself,
// and the one the scouting note proposed -- gives the RIGHT answer on the first
// three rows and the WRONG one from turn 3 on, where the history no longer holds
// two consecutive ATTACKs. The fix is that move 2 is ABSORBING: once it is
// decided it is decided forever, so
//
//     next = (last_move_is(m, BLOCK) || last_two_moves_are(m, ATTACK)) ? BLOCK
//                                                                     : ATTACK
//
// is exact at every decision, for all time. It is worth the paragraph because a
// `turnCount >= 2` field would have been a byte of state whose only reader is
// this one line -- the Chosen `usedHex` / Snecko `firstTurn` precedent -- and
// because the near-miss version passes a two-turn test and fails a four-turn one.
// NO MonsterState.flags BIT AND NO pad0 SLOT ARE SPENT BY THIS MONSTER.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// ExplosivePower's starting fuse: `new ExplosivePower(this, 3)`
// (Exploder.java:66; EXPLODE_BASE at :49), flat at every ascension.
inline constexpr int32_t kExploderExplosiveAmount = 3;

void exploder_init(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (Exploder.java:64-67): one ApplyPowerAction of
// ExplosivePower(3) on itself. No RNG draw.
void exploder_use_pre_battle_action(CombatState& state,
                                    uint8_t monster_index) noexcept;

// The trailing RollMoveAction (Exploder.java:82), reached by both cases. Draws
// one ai_rng value and DISCARDS it (see note 3 above).
void exploder_roll_move(CombatState& state, uint8_t monster_index) noexcept;

void exploder_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
