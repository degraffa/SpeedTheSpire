#pragma once

// The Centurion (the Act-2 STRONG group "Centurion and Healer",
// MonsterHelper.java:498-500, where it spawns FIRST). Stats and move effects are
// generated registry data (monsters.yaml id 35); move SELECTION is native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   Centurion.java:29-177; GainBlockRandomMonsterAction.java:19-42;
//   AbstractMonster.java:705-715 (init/rollMove), 765-775 (setHp), 431-491;
//   MonsterGroup.java:35-40 (dead records are never removed);
//   RollMoveAction.java:17-21.
//
// THE MOVE TREE (getMove, Centurion.java:132-160) HAS NO ASCENSION BRANCH, and
// it is the first getMove in the roster that reads a GROUP-WIDE property:
//
//   aliveCount = |{ m in group : !m.isDying && !m.isEscaping }|   -- INCLUDING SELF
//
//   1. num >= 65 && !lastTwoMoves(PROTECT) && !lastTwoMoves(FURY):
//          aliveCount > 1 ? PROTECT (DEFEND) : FURY (ATTACK, 3 hits)
//   2. !lastTwoMoves(SLASH):  SLASH (ATTACK)
//   3. otherwise:             aliveCount > 1 ? PROTECT : FURY
//
// Arms 1 and 3 are the SAME two lines with the SAME duplicated aliveCount walk
// (:134-138 and :150-154 are textually identical) -- the decompiler's rendering
// of one helper inlined twice. They are written once here.
//
// FIVE THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) THE ALIVE COUNT INCLUDES ITSELF, so a SOLO Centurion has aliveCount == 1
//     and can never choose PROTECT -- it furies instead, every time either arm
//     asks. That is the whole behavioural point of the pair encounter: kill the
//     Healer and the Centurion stops blocking and starts hitting three times.
//     The predicate is `isDying || isEscaping` (NOT hp alone, and NOT the
//     halfDead-bearing targeting predicate), which is monster_basically_dead.
//
// (2) AI-RNG ACCOUNTING, AND IT IS NOT CONSTANT PER TURN. One
//     ai_rng.random(99) at init() -- READ, not discarded, so the opening move is
//     seed-dependent -- then one per turn through the trailing RollMoveAction
//     (:107), which sits OUTSIDE takeTurn's switch so all three cases reach it.
//     ON TOP OF THAT, a resolved PROTECT may spend a SECOND draw: the
//     GainBlockRandomMonsterAction picks its recipient with one
//     aiRng.random(size-1), but ONLY when the valid list is non-empty
//     (GainBlockRandomMonsterAction.java:36) -- a Centurion with no valid ally
//     spends none. That draw happens at EXECUTE time inside the
//     BLOCK_RANDOM_MONSTER opcode (opcode 67, src/engine/interp/interp_block.cpp),
//     not here.
//
// (3) playSfx IS UNSEEDED. `MathUtils.random(1)` (:113) is libGDX's global
//     generator, so SLASH's one call and FURY's three cost NOTHING on the seeded
//     streams -- the opposite of the Mugger's identically-shaped playSfx. Its
//     third branch (:118-119) is also UNREACHABLE: random(1) returns 0 or 1 and
//     the method tests == 0, == 1, else. Recorded, not modelled.
//
// (4) FURY IS THREE SEPARATE HITS AND STAYS THREE AT A2. The loop at :96-102
//     queues furyHits individual DamageActions on damage.get(1), and the ctor
//     assigns `furyHits = 3` on BOTH sides of the `ascensionLevel >= 2` branch
//     (:66 and :70) -- the same value written twice. So unlike the Byrd's
//     peckCount this is a per-tier NON-difference, and the registry row's three
//     DAMAGE steps are exact at every ascension. Worth a deliberate negative
//     test rather than silence.
//
// (5) NO PRE-BATTLE ACTION, NO damage() CONSEQUENCE, NO SPAWN PATH, NO DIE BODY.
//     Centurion.java declares takeTurn, playSfx, changeState, getMove, damage and
//     die. changeState's only key is "MACE_HIT" (:123-130), spine calls;
//     damage() (:163-170) is super.damage plus the standard non-THORNS,
//     output > 0 hit animation; die() (:172-176) is a time-scale and a shake
//     before super.die() -- animation ONLY, no seeded draw, so it registers no
//     MonsterDieFn (contrast the Mugger's, which does). Nothing splits or
//     summons a Centurion.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

void centurion_init(CombatState& state, uint8_t monster_index) noexcept;

// The trailing RollMoveAction (Centurion.java:107), reached by all three takeTurn
// cases.
void centurion_roll_move(CombatState& state, uint8_t monster_index) noexcept;

void centurion_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
