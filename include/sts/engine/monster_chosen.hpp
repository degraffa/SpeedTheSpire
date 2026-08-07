#pragma once

// The Chosen (MonsterHelper.getEncounter "Chosen", and the Act-2 groups "Chosen
// and Byrds" and "Cultist and Chosen"). Stats and move effects are generated
// registry data (monsters.yaml id 27); move SELECTION is native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   Chosen.java:35-214; HexPower.java:19-43;
//   AbstractMonster.java:135-155 (ctor), 705-715 (init/rollMove),
//   765-775 (setHp), 431-491 (moveHistory / lastMove / lastTwoMoves).
//
// THE MOVE TREE (getMove, Chosen.java:151-197) IS TWO WHOLE ARMS, one per side
// of `ascensionLevel >= 17`, and the engine's fixed difficulty is A20, so the
// A17+ arm (:153-173) is the live one. Transcribed:
//
//   1. !usedHex          -> HEX, and latch usedHex.        (:154-158)
//   2. !lastMove(DEBILITATE) && !lastMove(DRAIN):
//        num < 50        -> DEBILITATE                     (:160-162)
//        else            -> DRAIN                          (:164)
//   3. otherwise:
//        num < 40        -> ZAP                            (:167-169)
//        else            -> POKE (2 hits)                  (:171)
//
// FOUR THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) `usedHex` NEEDS NO STATE. Step 1 is false on exactly one call -- init()'s
//     rollMove -- and true on every later one, so at A20 it IS "is this the init
//     call", which this module's init/roll split already answers structurally.
//     The Red Slaver's `firstTurn` needs no storage for the same reason
//     (monster_slaver.hpp). chosen_init therefore telegraphs HEX directly and
//     chosen_roll_move implements only steps 2 and 3. The batch was granted a
//     flag bit for this and RELEASED it; combat_state.hpp records why, and what
//     would make it real again (the sub-A17 arm, where `firstTurn` forces a POKE
//     opener and pushes HEX to the SECOND decision -- a genuine second-call
//     latch that no entry point distinguishes).
//
// (2) `firstTurn` IS DEAD CODE AT A17+, not merely unused. The A17 arm never
//     reads it (:153-173 has no mention), so the field is written by the
//     initializer at :69 and read only by the sub-A17 arm at :174. Nothing to
//     model, and nothing missing.
//
// (3) AI-RNG ACCOUNTING. One ai_rng.random(99) at init(), DISCARDED -- step 1
//     fires before `num` is ever read (the Guardian / Red Slaver / Looter
//     precedent) -- and then exactly one ai_rng.random(99) per turn from turn 2
//     on, via the trailing RollMoveAction (:137) that every takeTurn case falls
//     through to. No branch spends a second draw: unlike the Byrd's and the
//     Parasite's, this getMove has no nested randomBoolean and no recursion.
//     One monster_hp_rng draw in the ctor, over the A7 column (98, 103).
//
// (4) NO PRE-BATTLE ACTION, NO damage() CONSEQUENCE, NO SPAWN PATH. Chosen.java
//     declares takeTurn, changeState, getMove, damage and die -- there is no
//     usePreBattleAction at all. changeState's only key is "ATTACK" (:141-149),
//     two spine calls; damage() (:199-207) is the standard hit animation past
//     super.damage(); die() (:209-213) is a sound. Nothing splits or summons a
//     Chosen. So it registers an init, a turn and a roll, and nothing else.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

void chosen_init(CombatState& state, uint8_t monster_index) noexcept;

// The trailing RollMoveAction (Chosen.java:137), reached by every takeTurn case.
void chosen_roll_move(CombatState& state, uint8_t monster_index) noexcept;

void chosen_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
