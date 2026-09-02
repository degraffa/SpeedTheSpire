#pragma once

// The Guardian: the offensive/defensive mode state machine, its growing
// mode-shift threshold, and the Mode Shift / Sharp Hide bookkeeping.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled; files read in full):
//   TheGuardian.java:92-120 (ctor: EnemyType.BOSS; a DESCENDING ascension chain
//     -- A19+ 250 HP / threshold 40, A9+ 250 / 35, else 240 / 30; A4+ Fierce Bash
//     36 and Roll 10, else 32 / 9; damage list [fierceBash, roll, whirlwind 5,
//     twinSlam 8]),
//     :122-132 (usePreBattleAction: ApplyPower ModeShift(dmgThreshold) on itself,
//     then ChangeState "Reset Threshold"),
//     :134-169 (takeTurn dispatch), :171-223 (the six move bodies),
//     :226-232 (getMove: the ONLY move roll, at init -- isOpen decides between
//     CHARGE_UP and ROLL_ATTACK and the rolled `num` is discarded),
//     :234-273 (changeState: Defensive Mode / Offensive Mode / Reset Threshold),
//     :275-292 (damage(): the cumulative-damage accumulator that drives the flip);
//   ModeShiftPower.java:11-31, SharpHidePower.java:22-50;
//   AbstractMonster.java:99 (EnemyType default), 431-437 (setMove/moveHistory),
//   465-467 (init -> rollMove), 712-715.
//
// WHAT IS NATIVE AND WHY. The move EFFECTS are registry data (monsters.yaml id
// 21). What cannot be a table is the SELECTION: the Guardian has no move tree at
// all. getMove runs once, at init; every later transition is a direct setMove in
// a takeTurn body or inside changeState, and the branch between the two cycles
//     offensive  CHARGE_UP -> FIERCE_BASH -> VENT_STEAM -> WHIRLWIND -> (loop)
//     defensive  CLOSE_UP  -> ROLL_ATTACK -> TWIN_SLAM   -> WHIRLWIND -> offensive
// is driven by CUMULATIVE DAMAGE TAKEN, not by move history -- and the amount of
// damage needed GROWS by dmgThresholdIncrease == 10 on every flip (:61,245). At
// the engine's fixed A20 the flip points are therefore 40, 50, 60, ... .
//
// HOW THE ACCUMULATOR IS STORED, and why it is not derived from the power.
// Java keeps `dmgTaken` and ModeShiftPower.amount as two redundant views of the
// same quantity, updated from the same delta (:280-283). We store neither
// directly: `pad0` holds the HP BASELINE the current accumulation window started
// from, so dmgTaken == baseline - hp, and the Mode Shift slot's amount is the
// remaining threshold. The baseline is re-glued to the current HP on every
// damage event that Java's guard would have REJECTED, which reproduces exactly
// the windows in which Java's dmgTaken stays put:
//   * Defensive Mode (isOpen false) -- damage taken while closed never counts;
//   * after the threshold is reached but before the queued change of state
//     resolves (closeUpTriggered);
//   * while the Mode Shift power is absent. This last one is the subtle case and
//     it is deliberate. On the return to Offensive Mode, changeState sets isOpen
//     true SYNCHRONOUSLY (:262) but only QUEUES the new ModeShiftPower and the
//     "Reset Threshold" that zeroes dmgTaken (:254-255). A player with Thorns or
//     Flame Barrier reflects damage into that window; Java accumulates it into
//     dmgTaken and then DISCARDS it when Reset Threshold resolves. Gluing the
//     baseline while the power is absent reproduces that discard, which is why
//     this engine needs no queued "reset threshold" action.
//     The one place the two models could part company: Java re-tests
//     `dmgTaken >= dmgThreshold` on each of those reflected hits (:285), so a
//     SINGLE reflect at or above the whole grown threshold would flip Java
//     straight back to Defensive Mode before the discard ever ran. That needs
//     >= 40 reflected damage from one hit against a 250 HP sheet, and each
//     reflect source queues its own hit: the largest in Ironclad S1 content is
//     upgraded Flame Barrier's 6 (cards.yaml FLAME_BARRIER), with Bronze
//     Scales' Thorns 3 behind it. Unreachable rather than merely unlikely --
//     revisit if a bigger single reflect ever lands.
// Deriving dmgTaken from ModeShiftPower.amount ALONE would not work: in that
// same window the power does not exist, so its amount cannot record the reflect
// damage that Java is (transiently) counting.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Constructor + usePreBattleAction + init(), folded: the fixed registry HP sheet
// (setHp(int) -- a fixed value, but STILL ONE monsterHpRng draw:
// AbstractMonster.java:765-779 + Random.java:58-61), the Mode Shift power at this ascension's
// starting threshold, and the one aiRng.random(99) draw whose value getMove
// discards in favour of the forced opening CHARGE_UP.
void guardian_init(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (TheGuardian.java:122-132) minus presentation and the
// UnlockTracker call: the Mode Shift power at this ascension's starting
// threshold, plus the "Reset Threshold" that zeroes the accumulator. Runs in the
// player's preBattlePrep phase, after every monster's ctor + init(); draws no RNG.
void guardian_use_pre_battle_action(CombatState& state,
                                    uint8_t monster_index) noexcept;

// The native takeTurn state machine: both cycles, plus Twin Slam's queued return
// to Offensive Mode (TheGuardian.java:193-199,253-266).
void guardian_take_turn(CombatState& state, uint8_t monster_index) noexcept;

// TheGuardian.damage (:275-292), run after the hit fully lands: accumulate
// toward the mode-shift threshold and, on reaching it, queue the Defensive-Mode
// change of state -- a MONSTER_CHANGE_STATE item, i.e. the Java's
// ChangeStateAction (:288), whose body is guardian_change_state below.
//
// This is the ONE registered damage() override that wants the SIZE of the hit
// rather than the resulting state. It reconstructs the delta from the `pad0` HP
// baseline described above; if on_monster_damaged ever carries the lost HP
// directly, that argument is what this should read.
void guardian_on_damaged(CombatState& state, uint8_t monster_index) noexcept;

// The state ids guardian_change_state understands (the `amount` of a
// MONSTER_CHANGE_STATE item aimed at a Guardian slot). Only Defensive Mode is
// reached through a queued action today; Offensive Mode's latches are applied
// at the Twin Slam take-turn site (see monster_guardian.cpp) and "Reset
// Threshold" needs no item at all (the accumulator note above).
inline constexpr int32_t kGuardianStateDefensiveMode = 1;

// changeState (TheGuardian.java:234-273) at ChangeStateAction resolve time.
// DEFENSIVE_MODE (:237-251): grow the threshold, setMove(CLOSE_UP), clear isOpen
// -- all synchronous in the Java -- and addToBottom RemoveSpecificPower("Mode
// Shift") + GainBlock(20), which therefore land behind whatever the queue holds
// NOW. That "now" is the whole point of routing through a queued item: when the
// flip comes from an end-of-turn power, MonsterStartTurnAction's block clear is
// already queued and the 20 block lands after it, as it does in the game
// (S2.V3 seed STS237405). Unknown state ids are ignored.
void guardian_change_state(CombatState& state, uint8_t monster_index,
                           int32_t state_id) noexcept;

// This ascension's STARTING mode-shift threshold, before any flip has grown it
// (TheGuardian.java:97-106). Exposed for the tier-2 test, which pins all three
// ascension columns.
[[nodiscard]] int32_t guardian_base_mode_shift_threshold(
    int32_t ascension) noexcept;

// The threshold a monster record must currently cross to flip: the base for its
// ascension plus 10 per Defensive-Mode entry so far (dmgThresholdIncrease,
// TheGuardian.java:61,245). Exposed so the tier-2 test can assert the GROWTH
// rather than only the first flip.
[[nodiscard]] int32_t guardian_mode_shift_threshold(
    const MonsterState& monster) noexcept;

}  // namespace sts::engine
