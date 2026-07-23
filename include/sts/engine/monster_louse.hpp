#pragma once

// Louse monster modules (B3.13): LouseNormal + LouseDefensive. Structurally
// identical AI (LouseNormal.java / LouseDefensive.java differ only in move 4:
// Strengthen(+Strength, BUFF) vs Weaken(+Weak to player, DEBUFF)); one shared
// getMove/turn parameterised on the def + move-4 intent. Stats/effects are DATA
// (generated kLouseNormal/kLouseDefensive); selection is native (design §4.2).
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * ctor (LouseNormal.java:50-62 / LouseDefensive.java:53-65): setHp (A7 column
//     at A20; one monster_hp_rng draw) THEN biteDamage = monsterHpRng.random(A2?
//     6,8 : 5,7) -- a SECOND monster_hp_rng draw IN THE CTOR. The rolled bite dmg
//     is stored in MonsterState.pad0 (per-instance; not a table column).
//   * usePreBattleAction (:64-73 / :67-76): CurlUpPower(monsterHpRng.random(A17?
//     9,12 : A7? 4,8 : 3,7)) -- a THIRD monster_hp_rng draw, in the pre-battle
//     phase (after all ctors+init). At A20 -> random(9,12).
//   * getMove (:128-153 / :131-156): move ids BITE=3, STRENGTHEN/WEAKEN=4. A20
//     takes the A17 branch: num<25 -> lastMove(4)? BITE : move4; elif
//     lastTwoMoves(3) -> move4 else BITE. ONE aiRng.random(99) draw, num USED, no
//     tiebreak booleans -- so aiRng.counter advances by exactly 1 per rollMove.
//     Unlike Cultist/Jaw Worm there is NO forced first move: init's rollMove runs
//     the real tree on its drawn num.
//   * takeTurn (:75-104 / :78-107): BITE -> DamageAction(biteDamage); move4 ->
//     Strength/Weak apply; then unconditional RollMoveAction.
//
// DRAW-COUNTING (matches tests/fixtures/gen_louse_fixture.py): init = 2
// monster_hp_rng draws (HP, bite) + 1 ai_rng.random(99) (decision #1, num USED);
// pre-battle = 1 monster_hp_rng draw (curl-up). Each take-turn = 1 ai_rng.random(99).

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Construct the louse in slot `monster_index`: id, HP roll + bite roll (two
// monster_hp_rng draws), zeroed bookkeeping, and decision #1 (getMove on one
// ai_rng.random(99) draw whose value IS used).
void louse_normal_init(CombatState& state, uint8_t monster_index) noexcept;
void louse_defensive_init(CombatState& state, uint8_t monster_index) noexcept;

// One louse turn: enqueue the current move (BITE's rolled damage / Strengthen /
// Weaken) then roll the next move. MonsterTurnFn-compatible.
void louse_normal_take_turn(CombatState& state, uint8_t monster_index) noexcept;
void louse_defensive_take_turn(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction: apply CurlUpPower with a monster_hp_rng-rolled block amount.
// Shared by both louse variants (identical curl-up). MonsterPreBattleFn-compatible.
void louse_use_pre_battle_action(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
