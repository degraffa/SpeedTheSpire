#pragma once

// Small/medium slime monster modules (B3.14). HP and move effects are generated
// registry data; history-dependent getMove selection is native. The fixed S1
// difficulty is A20, so each function follows the cited A17 normal-monster AI
// branch while still resolving HP/damage from the base/A2/A7 table columns.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   SpikeSlime_S.java:42-76; SpikeSlime_M.java:51-117;
//   AcidSlime_S.java:45-95; AcidSlime_M.java:56-168;
//   AbstractMonster.java:431-491,712-715,765-775;
//   MakeTempCardInDiscardAction.java:24-50;
//   FrailPower.java:25-54; WeakPower.java:27-60.
// All named classes were read in full before implementation.
//
// Draw accounting at A20:
//   * every ctor: exactly 1 monster_hp_rng inclusive HP draw;
//   * every init(): exactly 1 ai_rng.random(99) draw;
//   * Spike S/M and Acid M: each turn ends in RollMoveAction -> at least one
//     ai_rng.random(99); Acid M conditionally adds one randomBoolean draw;
//   * Acid S takeTurn sets its alternating next move directly and consumes NO
//     ai_rng after init (the initial random(99) is drawn but ignored at A17).

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

void spike_slime_small_init(CombatState& state, uint8_t monster_index) noexcept;
void spike_slime_medium_init(CombatState& state, uint8_t monster_index) noexcept;
void acid_slime_small_init(CombatState& state, uint8_t monster_index) noexcept;
void acid_slime_medium_init(CombatState& state, uint8_t monster_index) noexcept;

void spike_slime_small_take_turn(CombatState& state,
                                 uint8_t monster_index) noexcept;
void spike_slime_medium_take_turn(CombatState& state,
                                  uint8_t monster_index) noexcept;
void acid_slime_small_take_turn(CombatState& state,
                                uint8_t monster_index) noexcept;
void acid_slime_medium_take_turn(CombatState& state,
                                 uint8_t monster_index) noexcept;

}  // namespace sts::engine
