#pragma once

// The Bronze Orb (BronzeOrb.java, read in full; registry/monsters.yaml id 41)
// -- the Bronze Automaton's minion, and the game's ONE card thief. Appears in
// NO encounter: it exists only through the boss's SPAWN_ORBS
// (SpawnMonsterAction(m, true), BronzeAutomaton.java:116,122), so its ctor
// draws happen at the SPAWNER'S queue time and its own entry point here is the
// spawn-at-hp init.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), all read in full:
//   BronzeOrb.java:29-102; ApplyStasisAction.java (whole file -- the theft is
//   Opcode::APPLY_STASIS, interp.hpp); StasisPower.java (whole file --
//   powers.yaml id 98); MonsterGroup.java:108-115 (getMonster);
//   CardGroup.java:498-500, :526-538 (the pick overloads).
//
// THREE THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) usedStasis IS WRITTEN AT DECISION TIME, INSIDE getMove (:87-90): the
//     latch flips when STASIS is TELEGRAPHED, not when it executes -- so a
//     hypothetical interrupt between the roll and the turn does not offer a
//     second theft. Per-RECORD (each of the two orbs steals once):
//     kMonsterFlagBronzeOrbUsedStasis.
//
// (2) SUPPORT_BEAM'S RECIPIENT IS THE BOSS BY ID, LIVENESS UNTESTED.
//     GainBlockAction(getMonsters().getMonster("BronzeAutomaton"), this, 12)
//     (:69); getMonster returns the FIRST record with that id, dead included
//     (MonsterGroup.java:108-115). A monster move step can only spell
//     SELF/PLAYER, so the row authors the BLOCK as a SELF template and the
//     take-turn body retargets it at the boss's slot (the Healer fan-out
//     precedent, narrowed to one fixed recipient). Unreachable on a dead boss
//     -- its death sweep suicides every orb -- and op_block's isDying
//     recipient read covers the gap anyway.
//
// (3) THE OPENER IS SEED-DEPENDENT: the spawn-resolve init roll enters getMove
//     with usedStasis clear, so num >= 25 telegraphs STASIS (75%) and num < 25
//     falls through to BEAM (lastTwoMoves is vacuously false on an empty
//     history). A freshly spawned orb can therefore steal on its very first
//     turn, which is the fight's signature opening.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Encounter-path init: the full ctor (super-arg draw + tiered setHp draw) plus
// the init roll. No encounter spawns a Bronze Orb today -- this exists so the
// dispatch table's "every registry monster has an init fn" claim stays true,
// and it is exactly what the ctor does wherever it runs.
void bronze_orb_init(CombatState& state, uint8_t monster_index) noexcept;

// Mid-combat spawn init (SpawnMonsterAction.update -> init()): HP arrives
// PRE-DRAWN (the spawner consumed both ctor draws at queue time); this spends
// exactly ONE ai_rng roll and decides the opener.
void bronze_orb_spawn_at_hp(CombatState& state, uint8_t monster_index,
                            int16_t hp) noexcept;

void bronze_orb_take_turn(CombatState& state, uint8_t monster_index) noexcept;

// The trailing RollMoveAction (:76), queued (outside the switch).
void bronze_orb_roll_move(CombatState& state, uint8_t monster_index) noexcept;

// getMove (:86-101). `num` is the aiRng.random(99) the caller already drew.
void bronze_orb_decide_move(CombatState& state, uint8_t monster_index,
                            int32_t num) noexcept;

}  // namespace sts::engine
