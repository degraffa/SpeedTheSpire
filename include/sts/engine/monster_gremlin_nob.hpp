#pragma once

// Gremlin Nob monster module. Stats/move-effects are DATA from
// registry/monsters.yaml (generated kGremlinNob); move *selection* is the native
// getMove below (design §4.2), matching the cultist_*/louse_* split.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled, read in full):
//   * GremlinNob ctor (GremlinNob.java:60-84): ELITE (:63); canVuln comes from
//     the 3-arg ctor's `setVuln`, and the 2-arg ctor -- the only one
//     MonsterHelper's "Gremlin Nob" group uses (MonsterHelper.java:436-438) --
//     passes true (:56-58). setHp is (85,90) from A8 else (82,86) (:67-71): one
//     monster_hp_rng draw, currentHealth == maxHealth. damage[0] = rushDmg,
//     damage[1] = bashDmg, 16/8 from A3 else 14/6 (:72-80).
//   * takeTurn (GremlinNob.java:86-113): BELLOW applies AngerPower(this, 3) at
//     A18 else 2 (:92-96); SKULL_BASH deals damage[1] then, when canVuln,
//     VulnerablePower(player, 2, true) (:99-105); BULL_RUSH deals damage[0]
//     (:106-110). Every case is followed by an unconditional RollMoveAction
//     (:112) -- so exactly one ai_rng.random(99) draw per turn.
//   * getMove (GremlinNob.java:126-170): the FIRST decision is forced BELLOW
//     (usedBellow, :128-132). From A18 (:133-150) the tree uses NO randomness:
//     `!lastMove(2) && !lastMoveBefore(2)` -> SKULL_BASH; else `lastTwoMoves(1)`
//     -> SKULL_BASH; else BULL_RUSH. Below A18 (:151-169) the first branch is
//     `num < 33` instead. rollMove always draws (AbstractMonster.java:465-467),
//     so the draw happens either way.
//   * AbstractMonster.lastMove/lastMoveBefore/lastTwoMoves (:469-491), init()
//     (:712-715), setHp (:765-775).
//
// The Anger power itself (registry id ANGER, native body powers/power_anger.cpp)
// is what makes a played SKILL give the Nob Strength; this module only applies it.
//
// DRAW-COUNTING: gremlin_nob_init = one monster_hp_rng draw (HP) + one
// ai_rng.random(99) draw whose value is IGNORED (the forced Bellow returns before
// reading num). Each gremlin_nob_take_turn ends with exactly one more
// ai_rng.random(99) draw, whose value is likewise unused at the engine's fixed
// A20 difficulty (the A18 branch is deterministic) -- so ai_rng.counter advances
// by exactly 1 per turn.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Construct the Gremlin Nob in slot `monster_index`: id, HP roll
// (monster_hp_rng), zeroed bookkeeping, and the forced first decision (Bellow;
// one ignored ai_rng.random(99) draw).
void gremlin_nob_init(CombatState& state, uint8_t monster_index) noexcept;

// One Gremlin Nob turn (GremlinNob.takeTurn): enqueue the decided move's effects
// then roll the next move. MonsterTurnFn-compatible.
void gremlin_nob_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
