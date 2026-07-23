#pragma once

// Cultist monster module (B3.13). Stats/move-effects are DATA from
// registry/monsters.yaml (generated kCultist); move *selection* is the native
// getMove below (design §4.2), matching the jaw_worm_* split.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * Cultist.getMove (Cultist.java:145-153): firstMove -> INCANTATION(3, BUFF),
//     then DARK_STRIKE(1, ATTACK) forever. Deterministic -- rollMove's num is
//     drawn (aiRng.random(99)) but ignored. takeTurn ends with an unconditional
//     RollMoveAction (Cultist.java:108).
//   * ctor setHp branches (Cultist.java:59-63): A7 setHp(50,56) else (48,54);
//     one monster_hp_rng draw, currentHealth == maxHealth.
//   * INCANTATION applies new RitualPower(this, ritualAmount(+1 at A17), false)
//     (Cultist.java:95-99) -- a monster (onPlayer==false) Ritual: +Strength each
//     round AFTER the first (skipFirst). The RITUAL power carries the ramp (its
//     native at_end_of_round body, power_hooks.cpp); the Cultist only SETS the
//     skipFirst flag when it casts Incantation (see cultist_take_turn).
//   * AbstractMonster.rollMove == getMove(aiRng.random(99)) (:465-467); init()
//     rolls once before turn 1 (:712-715); setHp (:765-775).
//
// DRAW-COUNTING (matches tests/fixtures/gen_cultist_fixture.py): cultist_init =
// decision #1 (forced Incantation, 1 aiRng.random(99) draw, value IGNORED); one
// monster_hp_rng draw (HP). Each cultist_take_turn executes the current move then
// rolls the next: exactly 1 aiRng.random(99) draw, no tiebreak booleans (the tree
// is deterministic) -- so aiRng.counter advances by exactly 1 per turn.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Construct the Cultist in slot `monster_index`: id, HP roll (monster_hp_rng),
// zeroed bookkeeping, and decision #1 (forced Incantation; one ignored
// ai_rng.random(99) draw).
void cultist_init(CombatState& state, uint8_t monster_index) noexcept;

// One Cultist turn (Cultist.takeTurn): enqueue the current move's effects
// (Dark Strike damage / Incantation's Ritual apply) then roll the next move
// (always Dark Strike after turn 1). MonsterTurnFn-compatible.
void cultist_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
