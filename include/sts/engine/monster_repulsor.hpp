#pragma once

// The Repulsor -- one of the three "ancient shapes" the Act-3 encounters
// "3 Shapes" (encounters.yaml id 46), "4 Shapes" (49) and "Sphere and 2 Shapes"
// (51) draw from. Stats and move effects are generated registry data
// (monsters.yaml id 51); move SELECTION is native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   Repulsor.java:26-83; MakeTempCardInDrawPileAction.java;
//   CardGroup.java:463-469 (addToRandomSpot -- the per-copy card_random_rng
//   draw); AbstractMonster.java:705-715 (init/rollMove), 765-775 (setHp),
//   431-491 (moveHistory).
//
// THE SIMPLEST MONSTER IN THE BATCH, and worth stating so the reader does not go
// looking for a catch:
//
//   * ONE monster_hp_rng draw. `super(..., 35, ...)` (:45) passes a literal, so
//     it costs nothing; the single setHp (:50-54) is the whole HP cost.
//   * NO usePreBattleAction -- the class does not declare one, so
//     AbstractMonster's empty body stands (AbstractMonster.java:953-954) and the
//     dispatch table's Repulsor entry is a spelled-out nullptr.
//   * NO ascension branch in getMove, no recursion, no extra draw.
//   * NO die() override and NO damage() override.
//
// WHAT IS WORTH KNOWING: getMove (:75-82) is
//     if (num < 20 && !lastMove(ATTACK)) ATTACK; else DAZE;
// so DAZE is the DEFAULT arm and a Repulsor spends roughly four turns in five
// shuffling Dazed into the draw pile. Each DAZE is TWO Dazed cards at two
// independent random draw-pile positions -- two sequential card_random_rng draws
// (or zero, on an empty draw pile) -- which is the only stream this monster
// touches besides ai_rng.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

void repulsor_init(CombatState& state, uint8_t monster_index) noexcept;

// The trailing RollMoveAction (Repulsor.java:72), reached by both cases. Draws
// one ai_rng value, which getMove DOES read (unlike the Exploder's).
void repulsor_roll_move(CombatState& state, uint8_t monster_index) noexcept;

void repulsor_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
