#pragma once

// The Snake Plant (the solo Act-2 STRONG group "Snake Plant",
// MonsterHelper.java:492-494). Stats and move effects are generated registry
// data (monsters.yaml id 33); move SELECTION is native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   SnakePlant.java:34-143; MalleablePower.java:12-83;
//   FrailPower.java / WeakPower.java (the two SPORES debuffs, already registered);
//   AbstractMonster.java:705-715 (init/rollMove), 765-775 (setHp),
//   431-491 (moveHistory / lastMove / lastMoveBefore / lastTwoMoves);
//   RollMoveAction.java:17-21.
//
// THE MOVE TREE (getMove, SnakePlant.java:121-142) IS TWO WHOLE ARMS, one per
// side of `ascensionLevel >= 17`, and the engine's fixed difficulty is A20, so
// the A17+ arm (:122-133) is the live one. Transcribed, with the sub-A17 arm
// beside it because the ONLY difference is one disjunct:
//
//   num <  65:  lastTwoMoves(CHOMPY) ? SPORES : CHOMPY      (both arms, same)
//   num >= 65:  A17+ : lastMove(SPORES) || lastMoveBefore(SPORES) ? CHOMPY : SPORES
//               base : lastMove(SPORES)                     ? CHOMPY : SPORES
//
// So A17 makes the plant refuse Spores when Spores was TWO decisions ago as well
// as one -- it looks two moves back on the num >= 65 arm and one on the
// num < 65 arm, which is why lastMoveBefore is needed and lastTwoMoves is not
// interchangeable with it (lastTwoMoves demands BOTH slots match; lastMoveBefore
// asks only about the second).
//
// FOUR THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) AI-RNG ACCOUNTING. One ai_rng.random(99) at init() -- and unlike the
//     Chosen's or the Looter's, it is NOT discarded: getMove reads num on the
//     very first call, so the opening move is seed-dependent. Then exactly one
//     ai_rng.random(99) per turn via the trailing RollMoveAction (:114), which
//     sits OUTSIDE takeTurn's switch so BOTH move bodies reach it. No branch
//     spends a second draw: no nested randomBoolean, no recursion. One
//     monster_hp_rng draw in the ctor, over the A7 column (78, 82). The BiteEffect
//     jitter inside Chompy Chomps (:100) is libGDX MathUtils -- UNSEEDED, no
//     seeded cost.
//
// (2) CHOMPY CHOMPS IS THREE SEPARATE HITS. The loop at :99-104 queues numBlows
//     == 3 individual DamageActions on damage.get(0) (Hexaghost's Inferno
//     precedent), so block, a halving power and a lethal clamp apply PER HIT --
//     and, with Malleable on the plant itself, a player's own multi-hit attack
//     escalates the plant's block per hit for the same reason. numBlows is a
//     local `int numBlows = 3` and CHOMPY_AMT is 3 at every ascension, so unlike
//     the Byrd's peckCount this needs no per-tier step count and the registry
//     row's three DAMAGE steps are exact everywhere.
//
// (3) SPORES IS FRAIL THEN WEAK, IN THAT ORDER (:107-110), both (player, 2,
//     true). The `true` is isSourceMonster, which is what makes the duration
//     debuffs skip their first decrement; op_apply_power derives it from the
//     monster source, so the registry steps carry no extra column.
//
// (4) NO ROLL-MOVE SPECIAL CASE, NO damage() CONSEQUENCE, NO SPAWN PATH.
//     SnakePlant.java declares usePreBattleAction, changeState, damage, takeTurn
//     and getMove. changeState's only key is "ATTACK" (:75-82), two spine calls;
//     damage() (:85-91) is `super.damage(info)` plus the standard non-THORNS,
//     output > 0 hit animation, so an empty on_monster_damaged hook is the
//     complete translation. There is no die() override at all. Nothing splits or
//     summons a Snake Plant.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// MalleablePower's 1-arg ctor default (MalleablePower.java:22 STARTING_BLOCK,
// :24-26): the Snake Plant applies `new MalleablePower(this)`, so amount 3, with
// basePower 3 alongside it (which is PowerSlot.counter -- power_malleable.hpp).
// Flat at every ascension.
inline constexpr int32_t kSnakePlantMalleableAmount = 3;

// getMove's decision (SnakePlant.java:121-142) as a pure function of the move
// history, the rolled `num`, and the ASCENSION -- which is a parameter rather
// than the fixed kMonsterAscension precisely so the tier-2 tests can exercise the
// sub-A17 arm, whose only difference is the `|| lastMoveBefore(SPORES)` disjunct
// on the num >= 65 branch. The engine always calls it at kMonsterAscension.
void snake_plant_decide_move(MonsterState& m, int32_t num,
                             int32_t ascension) noexcept;

void snake_plant_init(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (SnakePlant.java:69-72): queue ApplyPowerAction(this, this,
// new MalleablePower(this)) -- the 1-ARG ctor, so amount 3. No RNG draw.
void snake_plant_use_pre_battle_action(CombatState& state,
                                       uint8_t monster_index) noexcept;

// The trailing RollMoveAction (SnakePlant.java:114), reached by both takeTurn
// cases.
void snake_plant_roll_move(CombatState& state, uint8_t monster_index) noexcept;

void snake_plant_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
