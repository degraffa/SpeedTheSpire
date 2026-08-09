#pragma once

// The Champ (Champ.java, read in full; registry/monsters.yaml id 42) -- the
// Act-2 boss encounter "Champ" (encounters.yaml id 40, a single EMIT). The one
// City boss with neither minions nor a sim-visible pre-battle action: all of
// its complexity is in getMove. Stats and move effects are generated registry
// data; move SELECTION is native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   Champ.java:47-319; AbstractMonster.java:431-491 (move history, incl.
//   lastMoveBefore), :765-779 (setHp).
//
// FOUR THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) ++numTurns RUNS ON EVERY CALL, FIRST (:262) -- the opposite of the
//     Bronze Automaton, whose increment only two arms reach. So the TAUNT
//     cadence counts EVERY decision (the init roll included), and the counter
//     keeps growing uselessly once thresholdReached kills the ==4 arm.
//     numTurns and forgeTimes share MonsterState.pad0 (low/high nibble; both
//     saturate at 15, harmless -- numTurns only ever tests ==4 below the
//     threshold, forgeTimes only < 2).
//
// (2) THE THRESHOLD IS A ONE-SHOT LATCHED AT DECISION TIME.
//     `currentHealth < maxHealth / 2 && !thresholdReached` (:263, integer
//     division) sets kMonsterFlagChampThreshold and telegraphs ANGER; healing
//     back above half never disarms it. From then on the EXECUTE arm
//     (:268-272) fires whenever neither of the last two decisions was EXECUTE
//     -- the roster's first consumer of lastMoveBefore on a boss -- and the
//     TAUNT arm is dead (its `!thresholdReached` conjunct).
//
// (3) THE FORGE ARM'S ROLL BOUND WIDENS AT A19: num <= 30 at ascension >= 19,
//     num <= 15 below (:278-288), same guards otherwise (!lastMove(STANCE),
//     forgeTimes < 2), and ++forgeTimes happens AT DECISION TIME. The Java's
//     shape is `if (a19) { if (...) return; } else if (...) return;` -- at
//     A19 a failed inner test does NOT retry the narrow bound, which is
//     exactly a single widened bound.
//
// (4) NOTHING ELSE NEEDS STORAGE. firstTurn (:92,156-161) gates only the
//     Champion Belt TalkAction -- presentation, no draw (the Java's quote
//     pickers are unseeded MathUtils). die() (:305-318) is super.die() first,
//     then an unseeded sound coin and onBossVictoryLogic: no sim-visible
//     content on EITHER side of super.die(), so both die slots are explicit
//     nullptr cases in monster_dispatch.cpp.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// The Champ's `offsetX` (Champ.java:95, the ctor's 9th argument).
inline constexpr int16_t kChampDrawX = -90;

// pad0 packing: numTurns in the low nibble, forgeTimes in the high one. Only
// monster_champ.cpp reads them; exposed for the tier-2 tests.
[[nodiscard]] inline uint8_t champ_num_turns(const MonsterState& m) noexcept {
    return static_cast<uint8_t>(m.pad0 & 0x0Fu);
}
[[nodiscard]] inline uint8_t champ_forge_times(const MonsterState& m) noexcept {
    return static_cast<uint8_t>(m.pad0 >> 4);
}

void champ_init(CombatState& state, uint8_t monster_index) noexcept;

void champ_take_turn(CombatState& state, uint8_t monster_index) noexcept;

// The trailing RollMoveAction (:214), queued (outside the switch).
void champ_roll_move(CombatState& state, uint8_t monster_index) noexcept;

// getMove (:261-302). `num` is the aiRng.random(99) the caller already drew.
void champ_decide_move(CombatState& state, uint8_t monster_index,
                       int32_t num) noexcept;

}  // namespace sts::engine
