#pragma once

// The Spheric Guardian (the Act-2 encounters "Spheric Guardian" and "Sentry and
// Sphere", and the Act-3 "Sphere and 2 Shapes"). Stats and move effects are
// generated registry data (monsters.yaml id 30); move SELECTION is native --
// and, unusually, it is native despite reading no randomness at all.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   SphericGuardian.java:34-173; BarricadePower.java:11-31; ArtifactPower.java;
//   AbstractMonster.java:135-155 (ctor -- the zero-RNG path), 705-715
//   (init/rollMove), 765-775 (setHp, NOT called here), 431-491 (moveHistory).
//
// THE MOVE ORDER IS FULLY DETERMINISTIC (getMove, SphericGuardian.java:145-162):
//   turn 1        -> INITIAL_BLOCK_GAIN   (firstMove)
//   turn 2        -> FRAIL_ATTACK         (secondMove)
//   thereafter    -> lastMove(BIG_ATTACK) ? BLOCK_ATTACK : BIG_ATTACK
// which settles into a strict BIG_ATTACK / BLOCK_ATTACK alternation.
//
// FOUR THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) IT DRAWS ZERO monster_hp_rng, AND IT IS THE FIRST REGISTRY MONSTER THAT
//     DOES. setHp is NEVER called: the ctor hands maxHealth 20 straight to super
//     (:67) and AbstractMonster's ctor (AbstractMonster.java:135-155) assigns
//     `currentHealth = maxHealth` with no RNG anywhere in it. So the HP is a flat
//     20 at every ascension, and -- the part that matters for bit-exactness --
//     spawning one does not advance the HP stream. Group draw counts follow:
//     "Spheric Guardian" solo costs 0 HP draws and "Sentry and Sphere" costs 1,
//     the Sentry's. The init below therefore SKIPS the draw rather than calling
//     random() over a degenerate 20..20 range, which would still consume a value.
//     (This is a DIFFERENT situation from Hexaghost's fixed HP, which is reached
//     through setHp and does draw; do not carry that row's reasoning over.)
//
// (2) getMove NEVER READS `num`, BUT THE ROLLS STILL HAPPEN. Every branch
//     (:147-161) is a latch or a history test. The rollMove draws are made and
//     discarded -- the Guardian / Red Slaver / Looter precedent -- and that is
//     precisely why this must be native code rather than an empty ai table: the
//     draws move the shared ai_rng stream and any other monster in the group
//     sees the difference.
//
// (3) `firstMove` NEEDS NO STORAGE AND `secondMove` DOES. firstMove is consumed
//     on init()'s rollMove, which is a distinct entry point here (the Red Slaver
//     precedent). secondMove is consumed one decision LATER -- on the FIRST
//     QUEUED roll -- and nothing structural distinguishes that call from the
//     third, so it needs a real latch: kMonsterFlagSphericSecondMove, set at init
//     and cleared when it fires.
//
// (4) THE PRE-BATTLE BLOCK IS PERMANENT, AND BARRICADE IS WHAT MAKES IT SO.
//     usePreBattleAction (:77-82) applies Barricade, then Artifact(3), then a
//     direct GainBlockAction(40), in that order. The monster side of Barricade's
//     presence test is already live in apply_pre_turn_logic
//     (src/engine/action_queue.cpp) -- it skips the start-of-turn loseBlock()
//     for a monster carrying the power -- so no new hook was needed; this is
//     simply its first monster owner. Barricade's slot amount is the -1 MARKER
//     its ctor sets (BarricadePower.java:22), never a magnitude.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// SphericGuardian's fixed HP (SphericGuardian.java:67) -- the ctor argument, at
// every ascension, with no setHp call and so no monster_hp_rng draw.
inline constexpr int16_t kSphericGuardianHp = 20;

// ARTIFACT_AMT (:53) and STARTING_BLOCK_AMT (:54).
inline constexpr int32_t kSphericGuardianArtifact = 3;
inline constexpr int32_t kSphericGuardianStartingBlock = 40;

// BarricadePower's ctor amount (BarricadePower.java:22): a -1 MARKER, not a
// magnitude. Every presence test for it asks "is the slot there".
inline constexpr int32_t kBarricadeMarkerAmount = -1;

void spheric_guardian_init(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (SphericGuardian.java:77-82): Barricade, Artifact(3), then
// a direct GainBlockAction(40), in that order. No RNG draw.
void spheric_guardian_use_pre_battle_action(CombatState& state,
                                            uint8_t monster_index) noexcept;

// The trailing RollMoveAction (SphericGuardian.java:120), reached by all four
// cases. Draws and discards one ai_rng value.
void spheric_guardian_roll_move(CombatState& state,
                                uint8_t monster_index) noexcept;

void spheric_guardian_take_turn(CombatState& state,
                                uint8_t monster_index) noexcept;

}  // namespace sts::engine
