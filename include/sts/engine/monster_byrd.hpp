#pragma once

// The Byrd (the Act-2 groups "3 Byrds" and "Chosen and Byrds"). Stats and move
// effects are generated registry data (monsters.yaml id 28); move SELECTION is
// native, and so is the airborne latch its whole tree hangs on.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   Byrd.java:34-226; FlightPower.java:18-78;
//   AbstractMonster.java:135-155 (ctor), 705-715 (init/rollMove),
//   765-775 (setHp), 431-491 (moveHistory), 620-700 (damage -> onAttacked).
//
// THE TWO-STATE MACHINE. A Byrd is airborne or grounded, and the two states have
// completely different move sets:
//
//   AIRBORNE (kMonsterFlagByrdFlying set) -- getMove's whole tree (:186-215):
//     num < 50 : lastTwoMoves(PECK) ? (rb(0.4)   ? SWOOP : CAW ) : PECK
//     num < 70 : lastMove(SWOOP)    ? (rb(0.375) ? CAW   : PECK) : SWOOP
//     else     : lastMove(CAW)      ? (rb(0.2857)? SWOOP : PECK) : CAW
//   GROUNDED -- HEADBUTT, unconditionally, with `num` never read (:217).
//
// The transition DOWN is not the Byrd's decision at all: it is FlightPower
// running out. Flight sheds a stack per non-lethal hit (FlightPower.java:65-73),
// and its onRemove queues changeState("GROUNDED") (:75-78), which setMoves the
// STUNNED turn and clears the latch (Byrd.java:162-170). The transition UP is
// the Byrd's: HEADBUTT telegraphs GO_AIRBORNE, whose body re-grants Flight.
// So the grounded cycle is exactly STUNNED -> HEADBUTT -> GO_AIRBORNE -> flying.
//
// FIVE THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) `isFlying` IS A REAL LATCH AND MUST NOT BE DERIVED FROM THE FLIGHT POWER.
//     The two diverge in both directions. Case 2 sets isFlying SYNCHRONOUSLY at
//     :124, one queue slot AHEAD of the ApplyPowerAction that re-grants Flight
//     at :126 -- so between them the Byrd is flying with no Flight power. And
//     changeState("GROUNDED") runs from a QUEUED ChangeStateAction, so between
//     the power's removal and that action the Byrd has no Flight and is still
//     marked flying. It lives in kMonsterFlagByrdFlying, and it starts SET
//     (the field initializer is `= true`, :72).
//
// (2) HEADBUTT IS THE ONE MOVE THAT DOES NOT ROLL. Case 5 (:117-122) ends in a
//     synchronous `setMove(GO_AIRBORNE, UNKNOWN)` and a bare `return` -- it
//     never reaches the trailing RollMoveAction at :145. That return is
//     load-bearing twice over: it spends no ai_rng draw, and the setMove pushes
//     move id 2 onto the history ring at that instant, so the NEXT decision sees
//     GO_AIRBORNE as the last move. (Contrast the Shelled Parasite's STUNNED
//     case, which setMoves and then STILL falls through to its roll.)
//
// (3) AI-RNG ACCOUNTING, AND WHY THE BRANCH DRAWS ARE THE HARD PART. init()
//     spends TWO draws: rollMove's random(99), which the firstMove branch never
//     reads, and then that branch's own randomBoolean(0.375) (:179). Afterwards
//     each airborne roll spends random(99), plus a SECOND draw -- a randomBoolean
//     -- only when its arm's history predicate HOLDS (:189, :199, :208). A
//     grounded roll spends the random(99) and nothing else, and HEADBUTT's turn
//     spends nothing at all. Java's `if (pred) { rb(...) }` shape means the
//     boolean is drawn strictly inside the guard, so a Byrd whose history does
//     not match burns one draw per turn and one that does burns two.
//     playRandomBirdSFx (:148-150) and the ctor's animation seek (:98) roll
//     libGDX MathUtils -- an UNSEEDED generator -- and cost nothing.
//     One monster_hp_rng draw in the ctor, over the A7 column (26, 33).
//
// (4) PECK'S HIT COUNT IS ASCENSION-VARYING AND THE SCHEMA CANNOT SAY SO.
//     peckCount is 5 base / 6 from A2 (:55,57,86,90), and the effect list can
//     tier an AMOUNT but not a STEP COUNT, so the row authors six DAMAGE steps
//     -- exact at the engine's fixed A20, wrong at base. monsters.yaml records
//     the limitation on the move. peckDmg itself is 1 at every ascension.
//
// (5) NO damage() OVERRIDE, NO SPAWN PATH. Byrd.java declares usePreBattleAction,
//     takeTurn, playRandomBirdSFx, changeState, getMove and die -- no damage(),
//     so no on_monster_damaged case. Nothing splits or summons a Byrd; the
//     three-Byrd group builds all three at spawn time.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Byrd.isFlying (Byrd.java:72). Set == airborne. Read by the Byrd's own getMove
// and written by GO_AIRBORNE / the GROUNDED change of state.
[[nodiscard]] inline bool byrd_is_flying(const MonsterState& m) noexcept {
    return (m.flags & kMonsterFlagByrdFlying) != 0u;
}

// changeState("GROUNDED")'s `isFlying = false` (Byrd.java:165). Called by
// Flight's ON_POWER_REMOVED body (src/engine/powers/power_flight.cpp), which
// owns the other half of that method -- the queued STUNNED telegraph.
inline void clear_byrd_flying(MonsterState& m) noexcept {
    m.flags &= ~kMonsterFlagByrdFlying;
}

void byrd_init(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (Byrd.java:101-104): queue ApplyPowerAction(this, this,
// FlightPower(flightAmt)) -- 4 at A20 (:83). No RNG draw.
void byrd_use_pre_battle_action(CombatState& state,
                                uint8_t monster_index) noexcept;

// The trailing RollMoveAction (Byrd.java:145). Reached by cases 1/2/3/4/6 --
// NOT by HEADBUTT, which returns early.
void byrd_roll_move(CombatState& state, uint8_t monster_index) noexcept;

void byrd_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
