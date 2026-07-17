#pragma once

// Jaw Worm monster registry entry (design doc §9 walking skeleton). A hand-
// coded module for the M1 skeleton's single enemy; Stage B replaces per-monster
// hand code with the registry/YAML-driven table (design doc §6). jaw_worm_take_turn
// matches the pump's MonsterTurnFn signature exactly (action_queue.hpp) and is
// passed straight to pump() as its step-5 monster-turn hook.
//
// Provenance (read from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * com.megacrit.cardcrawl.monsters.exordium.JawWorm -- getMove decision tree
//     (JawWorm.java:148-184), takeTurn's unconditional RollMoveAction
//     (:120-146), A20 stat table / setHp(42,46) (:81-104), move ids
//     CHOMP=1/BELLOW=2/THRASH=3 (:65-67).
//   * com.megacrit.cardcrawl.monsters.AbstractMonster -- rollMove() ==
//     getMove(aiRng.random(99)) (:465-467), init() calls rollMove() once before
//     the first turn (:712-715), setMove appends nextMove to moveHistory at
//     DECISION time (:431-437), lastMove/lastTwoMoves history predicates
//     (:469-491), setHp(min,max) == monsterHpRng.random(min,max) with
//     currentHealth = maxHealth (:765-775).
//
// SCOPE: jaw_worm_take_turn ENQUEUES the current move's real effects (Chomp
// damage, Bellow Strength+block, Thrash damage+block) in JawWorm.takeTurn's
// addToBottom order (JawWorm.java:120-146), then rolls the next move. The move
// to execute is the decided move in move_history[0]; effects are queued (never
// applied inline) so they resolve through the pump exactly like a card's.
//
// DRAW-COUNTING CONVENTION (must match tests/fixtures/gen_jaw_worm_fixture.py to
// the draw): jaw_worm_init performs decision #1 -- rollMove's forced-first-move
// path, which STILL consumes one aiRng.random(99) draw (rollMove always draws)
// but ignores its value and forces Chomp. Each jaw_worm_take_turn call executes
// the currently-decided move (enqueues its effects, see SCOPE) then rolls the
// NEXT move:
// always one aiRng.random(99) draw, plus on the tiebreak branches one
// aiRng.randomBoolean draw -- so the aiRng counter advances by 1 or 2 per turn.
// monsterHpRng is drawn exactly once, in jaw_worm_init.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Jaw Worm move ids -- byte-identical to JawWorm.java:65-67 so fixture rows and
// move_history bytes cross-reference the source directly. 0 stays the
// move_history empty-slot sentinel (no move id is 0), which lets last_move /
// last_two_moves test move_history slots without a separate length field.
inline constexpr uint8_t kMoveChomp = 1;
inline constexpr uint8_t kMoveBellow = 2;
inline constexpr uint8_t kMoveThrash = 3;

// Telegraphed intent for MonsterState.intent (uint8_t). The game's
// AbstractMonster.Intent enum is player-facing telegraphing, not mechanics
// (JawWorm.getMove passes ATTACK / DEFEND_BUFF / ATTACK_DEFEND). This minimal
// enum captures exactly the three Jaw Worm uses; NONE is the value-init default.
enum class MonsterIntent : uint8_t {
    NONE = 0,
    ATTACK = 1,         // Chomp
    DEFEND_BUFF = 2,    // Bellow
    ATTACK_DEFEND = 3,  // Thrash
};

// Jaw Worm A20 stats -- JawWorm.java ascension>=17 branch (:86-91) plus the
// ascension>=7 HP range setHp(42,46) (:81-82). Constants live here so the move
// enqueues and any other consumer reference them without re-deriving from the
// Java.
inline constexpr int kJawWormHpMin = 42;       // A_2_HP_MIN (>=7 branch)
inline constexpr int kJawWormHpMax = 46;       // A_2_HP_MAX
inline constexpr int kJawWormChompDmg = 12;    // A_2_CHOMP_DMG
inline constexpr int kJawWormThrashDmg = 7;    // THRASH_DMG
inline constexpr int kJawWormThrashBlock = 5;  // THRASH_BLOCK
inline constexpr int kJawWormBellowStr = 5;    // A_17_BELLOW_STR
inline constexpr int kJawWormBellowBlock = 9;  // A_17_BELLOW_BLOCK

// Construct the Jaw Worm in monster slot `monster_index`: set id, roll HP from
// monster_hp_rng (one inclusive random(42,46) draw -> hp == max_hp), zero
// block/flags/powers/history, and perform decision #1 (the forced-first-move
// Chomp), consuming one ai_rng.random(99) draw whose value is ignored.
void jaw_worm_init(CombatState& state, uint8_t monster_index) noexcept;

// One monster turn (design doc §5.2 step 5; JawWorm.takeTurn). Signature is
// MonsterTurnFn-compatible so it is passed directly as pump()'s take_turn hook.
// Executes the currently-decided move (enqueues its real effects, see SCOPE) then
// rolls the next move via the getMove decision tree on ai_rng, updating
// move_history and intent.
void jaw_worm_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
