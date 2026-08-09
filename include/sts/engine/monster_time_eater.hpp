#pragma once

// The Time Eater (the Act-3 solo boss encounter "Time Eater",
// MonsterHelper.java:585-587; encounters.yaml row 59). Stats and move effects are
// generated registry data (monsters.yaml id 63); move SELECTION and the two
// runtime numbers are native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   TimeEater.java:44-223; TimeWarpPower.java (see powers/power_time_warp.hpp);
//   DrawReductionPower.java (powers/power_draw_reduction.hpp);
//   RemoveDebuffsAction.java; HealAction.java:13-38 ->
//   AbstractMonster.heal (AbstractMonster.java:383-399);
//   AbstractMonster.java:765-779 (setHp), :431-491 (move history).
//
// ============================================================================
// THE MOVE TREE IS THE HARD PART, AND IT IS THE ONLY ONE IN THIS BATCH THAT
// SPENDS EXTRA ai_rng DRAWS
// ============================================================================
//
//   getMove(num):                                          (TimeEater.java:177-206)
//     currentHealth < maxHealth/2 && !usedHaste
//                         -> usedHaste = true; HASTE(5); return
//     num <  45 : !lastTwoMoves(2) -> REVERBERATE(2)
//                 else             -> getMove(aiRng.random(50, 99))   // EXTRA DRAW
//     num <  80 : !lastMove(4)     -> HEAD_SLAM(4)
//                 else             -> aiRng.randomBoolean(0.66f)      // EXTRA DRAW
//                                       ? REVERBERATE(2) : RIPPLE(3)
//     num >= 80 : !lastMove(3)     -> RIPPLE(3)
//                 else             -> getMove(aiRng.random(74))       // EXTRA DRAW
//
// FOUR readings that matter:
//
// (1) THE RECURSION TERMINATES, and the argument is worth writing down because
//     the code does not make it obvious. `random(50,99)` re-enters ABOVE the <45
//     band, so that arm cannot immediately repeat itself; `random(74)` re-enters
//     BELOW 80 and CAN land back in the <45 band. What bounds it is that the
//     three history guards are mutually exclusive -- lastTwoMoves(2) implies
//     lastMove == 2, which makes both lastMove(4) and lastMove(3) false -- so at
//     most ONE guard can be blocking at a time and the second visit always
//     terminates. Depth is therefore at most 2 re-entries. Every re-entry re-runs
//     the Haste gate, which is harmless: the latch is already set if it fired.
//
// (2) THE HASTE GATE IS STRICT `<` WITH INTEGER DIVISION -- maxHealth/2 is 228 at
//     base and 240 at A9 -- and it is tested in getMove, i.e. at the TRAILING
//     RollMoveAction, NOT on the damage edge. So Haste is telegraphed the turn
//     AFTER the crossing, never on it. `usedHaste` is a one-shot latch
//     (kMonsterFlagTimeEaterUsedHaste): the boss hastes at most once per fight
//     however far it is knocked back down.
//
// (3) THE HASTE HEAL IS `maxHealth / 2 - currentHealth`, INTEGER, AND CAN BE
//     NEGATIVE. Nothing in the class clamps it; op_heal's `amount <= 0` guard
//     drops a negative one. It is unreachable through getMove's own gate (the
//     latch only arms below half), but it is reachable in a hand-built state, and
//     a NEGATIVE heal must do nothing rather than damage the boss. The amount is
//     a runtime read, which is why the HASTE row authors no HEAL step.
//
// (4) THE ONLY ASCENSION THRESHOLDS ARE A4 AND A19. There is NO A9 branch on any
//     move and NO A17 branch anywhere in the class. A4 moves two DAMAGE numbers
//     (reverbDmg 7->8, headSlamDmg 26->32, :90-96); A19 ADDS THREE STEPS that do
//     not exist below it -- Ripple's Frail, Head Slam's two Slimed, Haste's
//     block. Those three are PRESENCE facts, not amounts, so the effect list
//     cannot gate them and this module queues them per-step (the Snecko's A17
//     Weak precedent). A9 moves only the HP sheet.
//
// ----------------------------------------------------------------------------
// RNG ACCOUNTING
// ----------------------------------------------------------------------------
// ONE monster_hp_rng draw in the ctor -- setHp(480)/setHp(456) is the single-arg
// overload, which still draws over a degenerate range (see monster_awakened_one.hpp
// for the full reading; the dossier says the opposite).
// ONE ai_rng draw at init and ONE per turn through the trailing RollMoveAction
// (:154), PLUS the extra draws of (1) above. Every extra draw sits INSIDE its
// arm's history guard, exactly as the Java nests them, so the number of draws a
// turn costs depends on the move history -- which is precisely why this cannot
// be an ai table.
// The A19 Head Slam spends NO card_random_rng: MakeTempCardInDiscardAction
// appends to the discard pile without a position draw.
//
// ----------------------------------------------------------------------------
// NEGATIVES, each checked rather than assumed
// ----------------------------------------------------------------------------
// * `firstTurn` (:73,112-119) gates a TalkAction ONLY -- presentation, no seeded
//   draw, no queued combat effect -- so it needs no storage bit at all. The
//   WATCHER branch inside it (:113) is dead in an Ironclad run twice over.
// * die() (:211-221) has NO combat-visible content: the `!cannotLose` guard is
//   never false-triggered here (nothing sets cannotLose in a Time Eater room),
//   the two lines before super.die() are a shake and a rumble, and
//   onBossVictoryLogic / onFinalBossVictoryLogic carry only achievements. So this
//   monster registers NEITHER a MonsterDieFn NOR a MonsterDieAfterFn, and that is
//   spelled as explicit nullptr cases rather than left to a `default:`.
// * damage() (:158-166) is super.damage plus a hit animation -- no override
//   content -- so no on_monster_damaged body either.
// * Shackled is removed TWICE by Haste (RemoveDebuffsAction, then an explicit
//   RemoveSpecificPowerAction). The second is a no-op at every reachable state;
//   both are authored because both are what the Java queues.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// getMove (TimeEater.java:177-206). `num` is the aiRng.random(99) the caller
// already drew; `s` is threaded because three arms may spend a SECOND (and, once,
// a THIRD) draw, and `ascension` is a parameter rather than the fixed
// kMonsterAscension so the tier-2 tests can exercise the sub-A4/sub-A19 columns
// -- the HASTE arm reads the HP sheet, which is ascension-dependent.
void time_eater_decide_move(CombatState& state, uint8_t monster_index,
                            int32_t num) noexcept;

void time_eater_init(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (:102-108): one addToBottom ApplyPowerAction granting itself
// Time Warp. No ascension branch, no RNG.
void time_eater_use_pre_battle_action(CombatState& state,
                                      uint8_t monster_index) noexcept;

// The QUEUED trailing RollMoveAction (:154).
void time_eater_roll_move(CombatState& state, uint8_t monster_index) noexcept;

void time_eater_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
