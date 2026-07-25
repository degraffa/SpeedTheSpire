#pragma once

// The two Act-1 slavers (MonsterHelper.getEncounter "Blue Slaver" /
// "Red Slaver", MonsterHelper.java:391-393,406-408; both also reachable through
// the Exordium Thugs group's bottomGetStrongHumanoid, :816-829). HP ranges and
// move effects are generated registry data; move SELECTION is native, per design
// doc B §4.2's budget ("move effects as data, selection native where the table
// shape doesn't fit"). The fixed S1 difficulty is A20, so each body follows the
// cited A17/A7/A2 branch while still resolving its numbers from the base/A2/A7/
// A17 table columns.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), both read in full:
//   SlaverBlue.java:27-136; SlaverRed.java:32-173;
//   EntanglePower.java:15-47; AbstractMonster.java:431-491,705-715,765-775.
//
// THREE THINGS WORTH KNOWING BEFORE READING THE BODIES
//
// (1) BOTH SLAVERS ROLL EVERY TURN, AND BOTH READ THE ROLL. Each takeTurn ends
//     in a QUEUED RollMoveAction (SlaverBlue.java:89, SlaverRed.java:110), so the
//     draw accounting at A20 is: one monster_hp_rng draw per ctor (setHp,
//     AbstractMonster.java:765-775), one ai_rng.random(99) at init() (rollMove,
//     :705-715), and one further ai_rng.random(99) per turn resolved AT DEQUEUE
//     TIME. Unlike Lagavulin / Slime Boss / Hexaghost, whose getMove ignores its
//     `num`, both getMove overrides here compare it against a threshold -- 40 for
//     the Blue Slaver's Stab, 75 and 55 for the Red Slaver's Entangle and Stab --
//     so the rolled value genuinely selects the move.
//
// (2) THE RED SLAVER'S TWO LATCHES NEED ONE BYTE BETWEEN THEM, NOT TWO.
//     `firstTurn` (SlaverRed.java:56,77,140-141) is true exactly once, at the
//     init() rollMove, and is cleared by that same call; init IS that occasion,
//     so it needs no storage at all -- slaver_red_init decides Stab directly and
//     slaver_red_roll_move (every later roll) starts below the branch.
//     `usedEntangle` (:55,90,145,149) DOES outlive its turn and cannot be
//     recovered from the 3-slot move history, so it lives in MonsterState.pad0,
//     the monster-type-scoped scratch byte (combat_state.hpp). It is set
//     SYNCHRONOUSLY inside takeTurn (:90), before the ApplyPowerAction it queued
//     resolves -- which matters, because the queued RollMoveAction behind it must
//     already see the latch.
//
// (3) NEITHER SLAVER OVERRIDES damage() OR usePreBattleAction. Neither class
//     declares either method, so neither registers a post-damage hook nor a
//     pre-battle action; their entries in those switches stay with the shared
//     `default:`. What they DO both override is die(), whose whole body past
//     super.die() is playDeathSfx (SlaverBlue.java:131-135 / SlaverRed.java:
//     168-172) -- and that rolls MathUtils.random, libgdx's global generator,
//     not one of the game's seeded streams, so it moves nothing this engine
//     models. The same is true of playSfx on the attack turns.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// MonsterState.pad0 bit 0 for the Red Slaver: SlaverRed.usedEntangle
// (SlaverRed.java:55). pad0 is monster-type-scoped scratch with no global
// layout, so this bit means nothing on any other monster id.
inline constexpr uint8_t kSlaverRedPad0UsedEntangle = 0x01u;

void slaver_blue_init(CombatState& state, uint8_t monster_index) noexcept;
void slaver_red_init(CombatState& state, uint8_t monster_index) noexcept;

void slaver_blue_take_turn(CombatState& state, uint8_t monster_index) noexcept;
void slaver_red_take_turn(CombatState& state, uint8_t monster_index) noexcept;

// The queued RollMoveAction bodies (SlaverBlue.java:89 / SlaverRed.java:110):
// one ai_rng.random(99) followed by the class's getMove decision tree.
// Registered in monster_roll_move_fn because the draw must land at DEQUEUE time
// -- the slaver's own attack can queue player Thorns/Flame Barrier damage ahead
// of it, and RollMoveAction has no liveness check (RollMoveAction.java:17-21).
void slaver_blue_roll_move(CombatState& state, uint8_t monster_index) noexcept;
void slaver_red_roll_move(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
