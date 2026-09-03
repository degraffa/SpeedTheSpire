#pragma once

// The Writhing Mass (the solo Act-3 STRONG group "Writhing Mass",
// encounters.yaml id 54). Stats and move effects are generated registry data
// (monsters.yaml id 57); move SELECTION is native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   WrithingMass.java:37-195; ReactivePower.java; MalleablePower.java (reused
//   unchanged from S2.22); AddCardToDeckAction.java:12-26;
//   ShowCardAndObtainEffect.java:30-45 (the Omamori gate); Parasite.java;
//   AbstractMonster.java:705-715 (init/rollMove), 431-491 (moveHistory),
//   765-779 (setHp BOTH overloads); RollMoveAction.java:17-21.
//
// SEVEN THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) ITS FIRST MOVE ID IS **0**, AND 0 IS ALSO THE move_history EMPTY-SLOT
//     SENTINEL. BIG_HIT is `(byte) 0` (WrithingMass.java:48). The registry
//     loader rejected move_id 0 until this batch; it now rejects only negatives,
//     and the reachability argument that makes 0 safe HERE is written out in the
//     monsters.yaml row and in emit/monsters.py. The short version: the one
//     lastMove(0) call sits behind a `firstMove` branch that returns
//     unconditionally, so by the time it runs the history is never empty; and
//     this class never calls lastTwoMoves or lastMoveBefore at all.
//
// (2) getMove IS RECURSIVE, AND THE RECURSION IS REAL. Four of the five arms can
//     re-enter with a freshly drawn `num`, and the `num < 40` arm's
//     `getMove(aiRng.random(19))` WIDENS the range back down, so termination is
//     probabilistic rather than structural. It is written as genuine recursion,
//     not as a capped loop: a cap would be a behaviour change. A generous depth
//     bound is ASSERTED instead, so a real runaway is loud in a debug build and
//     is not silently truncated into a different monster.
//
// (3) IT IS THE SHARPEST ai_rng CONSUMER IN ACTS 1-3. Each level of recursion
//     costs one draw to produce the new `num`, each randomBoolean tiebreak (0.1,
//     0.4, 0.3) costs one, and the `num < 40` / 0.4 arm costs BOTH. Contrast the
//     Transient in the same batch, which spends exactly one draw per combat.
//
// (4) REACTIVE MAKES THE PLAYER'S OWN ATTACKS RE-ROLL IT. ReactivePower
//     (powers.yaml 105, game_id "Compulsive") queues a ROLL_MOVE on every real,
//     non-lethal, NORMAL hit -- so a three-hit attack re-rolls the move three
//     times, each an independent recursive getMove. That is why this monster
//     registers a MonsterRollMoveFn: its rolls must resolve from the action
//     queue, in order, interleaved with the player's card, rather than inline in
//     its own turn body (the large-slime precedent).
//
// (5) THE RE-ROLL CAN GRANT THE PARASITE ON THE PLAYER'S TURN. The reactive
//     roll is a full rollMove with `firstMove` already spent, so MEGA_DEBUFF is
//     selectable from it -- and MEGA_DEBUFF's effect is a master-deck write.
//     Directed test, not a curiosity.
//
// (6) ONE monster_hp_rng DRAW. The ctor calls setHp(175) / setHp(160)
//     (:60-64) -- the ONE-ARG overload, which is `setHp(hp, hp)`
//     (AbstractMonster.java:777-779) and still draws. The Hexaghost shape, and
//     the OPPOSITE of the Transient and the Maw in this same batch.
//
// (7) NO A17 BRANCH ANYWHERE IN THE CLASS. The only monster in the batch without
//     one; a real absence, not an oversight.
//
// STORAGE. `firstMove` (:44) and `usedMegaDebuff` (:45) are two one-bit latches
// in MonsterState.pad0, which this monster type owns in full and subdivides (the
// Slaver precedent). No new MonsterState.flags bit is spent.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// `private boolean firstMove = true;` (WrithingMass.java:44) -- SET == the very
// first decision has not been made yet, so the opening telegraph comes from the
// thirds-of-num branch rather than the main tree. Consumed on the init rollMove
// and never set again.
inline constexpr uint8_t kWrithingMassPadFirstMove = 0x01u;

// `private boolean usedMegaDebuff = false;` (:45) -- SET == the Parasite has
// been granted, which permanently closes the MEGA_DEBUFF gate (:165). Set
// SYNCHRONOUSLY in takeTurn (:116), not at the decision.
inline constexpr uint8_t kWrithingMassPadUsedMegaDebuff = 0x02u;

// MalleablePower's 1-arg ctor default (MalleablePower.java:22,24-26): the
// Writhing Mass applies `new MalleablePower(this)`, so amount 3 with basePower 3
// alongside it -- identical to the Snake Plant's, which is why powers.yaml id 95
// needed no change for this second granter.
inline constexpr int32_t kWrithingMassMalleableAmount = 3;

// ReactivePower's ctor assigns NO amount (ReactivePower.java:24-31), so the
// applied instance carries AbstractPower's field-initializer -1
// (AbstractPower.java:65) -- the Confusion / Shifting spelling. Not 0, not 1.
inline constexpr int32_t kReactiveNoAmount = -1;

// The recursion depth this module refuses to exceed. NOT a behaviour cap: every
// path that reaches it would already be a ~1-in-10^12 tail, and the assert is
// there so a genuine runaway is loud rather than silently becoming a different
// monster. Chosen well above anything a real stream produces.
inline constexpr int32_t kWrithingMassMaxGetMoveDepth = 64;

void writhing_mass_init(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (WrithingMass.java:80-83): ApplyPowerAction Reactive THEN
// ApplyPowerAction Malleable, in that order -- which is the power-list order
// every hook fan-out walks. No RNG draw.
void writhing_mass_use_pre_battle_action(CombatState& state,
                                         uint8_t monster_index) noexcept;

// The queued-roll body: the trailing RollMoveAction (:122) AND every ROLL_MOVE
// Reactive queues from the player's turn. Registered as this monster's
// MonsterRollMoveFn.
void writhing_mass_roll_move(CombatState& state,
                             uint8_t monster_index) noexcept;

void writhing_mass_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
