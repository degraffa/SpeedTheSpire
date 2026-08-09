#pragma once

// The Torch Head (TorchHead.java, read in full; registry/monsters.yaml id 44)
// -- The Collector's minion. Appears in NO encounter: it exists only through
// the Collector's SPAWN and REVIVE moves (SpawnMonsterAction(m, true),
// TheCollector.java:130,168), so its ctor draws happen at the SPAWNER'S queue
// time and its live entry point here is the spawn-at-hp init.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   TorchHead.java:27-85; TheCollector.java:124-178 (the spawn sites);
//   AbstractMonster.java:431-491, :705-715 (init -> rollMove), :765-779.
//
// TWO THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) THE CTOR ITSELF CALLS setMove (:45) -- the only monster in the roster
//     that telegraphs from its constructor -- and init()'s rollMove then
//     telegraphs AGAIN (:81-83, ignoring num). So a freshly spawned Torch
//     Head's move history reads [TACKLE, TACKLE], its one ai_rng draw's value
//     decides nothing, and both pushes are modelled because move history is
//     oracle-visible state.
//
// (2) takeTurn HAS NO TRAILING RollMoveAction: it re-telegraphs with a queued
//     SetMoveAction(1, ATTACK, 7) (:63) -- the Transient's shape. A Torch
//     Head therefore spends exactly ONE ai_rng draw in its whole life (the
//     spawn-resolve init roll) and registers NO monster_roll_move_fn.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Encounter-path init: the full ctor (super-arg draw + ctor setMove + tiered
// setHp draw) plus the init roll. No encounter spawns one today; kept exact so
// the init-fn table's claim holds (the Bronze Orb's reasoning).
void torch_head_init(CombatState& state, uint8_t monster_index) noexcept;

// Mid-combat spawn init: HP arrives pre-drawn; the ctor's setMove push, then
// exactly ONE ai_rng roll whose getMove re-sets the same move -- note (1).
void torch_head_spawn_at_hp(CombatState& state, uint8_t monster_index,
                            int16_t hp) noexcept;

void torch_head_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
