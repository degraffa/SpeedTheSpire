#pragma once

// Jaw Worm monster module. Stats, HP ranges, and move effects are DATA from
// registry/monsters.yaml, code-generated into the constexpr monster table
// (<build>/generated/sts/registry/monster_table.hpp, Stage B design §4.2/§4.3)
// -- there is no hand-written stat table anymore. Move *selection* (the getMove
// decision tree) stays native per design §4.2's budget: the AbstractMonster
// move-history machinery doesn't fit the table shape. jaw_worm_take_turn
// matches the pump's MonsterTurnFn signature exactly (action_queue.hpp) and is
// passed straight to pump() as its step-5 monster-turn hook.
//
// Provenance (read from D:\STS_BG_Mod\SlayTheSpireDecompiled; stat/move numbers
// are cited per-column in registry/monsters.yaml):
//   * com.megacrit.cardcrawl.monsters.exordium.JawWorm -- getMove decision tree
//     (JawWorm.java:148-184), takeTurn's unconditional RollMoveAction
//     (:120-146), ascension stat branches (:79-104), move ids
//     CHOMP=1/BELLOW=2/THRASH=3 (:65-67).
//   * com.megacrit.cardcrawl.monsters.AbstractMonster -- rollMove() ==
//     getMove(aiRng.random(99)) (:465-467), init() calls rollMove() once before
//     the first turn (:712-715), setMove appends nextMove to moveHistory at
//     DECISION time (:431-437), lastMove/lastTwoMoves history predicates
//     (:469-491), setHp(min,max) == monsterHpRng.random(min,max) with
//     currentHealth = maxHealth (:765-775).
//
// SCOPE: jaw_worm_take_turn ENQUEUES the current move's effect program from the
// generated table (Chomp damage, Bellow Strength+block, Thrash damage+block) in
// JawWorm.takeTurn's addToBottom order (JawWorm.java:120-146), then rolls the
// next move. The move to execute is the decided move in move_history[0];
// effects are queued (never applied inline) so they resolve through the pump
// exactly like a card's.
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
#include "sts/registry/monster_table.hpp"  // generated: kJawWorm def (build tree)

namespace sts::engine {

// The skeleton plays S1's fixed difficulty: Ironclad / Act 1 / A20 (design doc
// §9; stage-b §1.1). The generated table carries the full per-ascension-tier
// columns; this constant selects which column the skeleton combat reads until
// the run layer threads a real ascension through combat_begin.
inline constexpr int32_t kSkeletonAscension = 20;

// Jaw Worm move ids -- byte-identical to JawWorm.java:65-67 (generated from
// monsters.yaml move_id), so fixture rows and move_history bytes cross-reference
// the source directly. 0 stays the move_history empty-slot sentinel (no move id
// is 0), which lets last_move / last_two_moves test move_history slots without
// a separate length field.
inline constexpr uint8_t kMoveChomp = sts::registry::kJawWormMoveChomp;
inline constexpr uint8_t kMoveBellow = sts::registry::kJawWormMoveBellow;
inline constexpr uint8_t kMoveThrash = sts::registry::kJawWormMoveThrash;

// Telegraphed intent for MonsterState.intent (uint8_t) -- generated alongside
// the monster table. The game's AbstractMonster.Intent enum is player-facing
// telegraphing, not mechanics; NONE is the value-init default.
using MonsterIntent = sts::registry::MonsterIntent;

// Jaw Worm A20 HP range -- the a7 column of registry/monsters.yaml
// (JawWorm.java:81-82 setHp(42,46) for ascension >= 7), resolved at the
// skeleton's fixed ascension. Exposed as named constants because tests assert
// the rolled HP lands in this range; move-effect amounts have no such
// re-export -- jaw_worm_take_turn reads them straight from the table.
inline constexpr int kJawWormHpMin =
    sts::registry::kJawWorm.hp_min(kSkeletonAscension);
inline constexpr int kJawWormHpMax =
    sts::registry::kJawWorm.hp_max(kSkeletonAscension);

// Construct the Jaw Worm in monster slot `monster_index`: set id, roll HP from
// monster_hp_rng (one inclusive random(42,46) draw -> hp == max_hp), zero
// block/flags/powers/history, and perform decision #1 (the forced-first-move
// Chomp), consuming one ai_rng.random(99) draw whose value is ignored.
void jaw_worm_init(CombatState& state, uint8_t monster_index) noexcept;

// One monster turn (design doc §5.2 step 5; JawWorm.takeTurn). Signature is
// MonsterTurnFn-compatible so it is passed directly as pump()'s take_turn hook.
// Executes the currently-decided move (enqueues its effect program from the
// generated table, see SCOPE) then rolls the next move via the getMove decision
// tree on ai_rng, updating move_history and intent.
void jaw_worm_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
