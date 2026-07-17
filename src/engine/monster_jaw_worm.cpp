// Jaw Worm AI + monster turn (task A3.2). See monster_jaw_worm.hpp for the
// full provenance, scope boundary, and draw-counting convention.
//
// Provenance: JawWorm.getMove (JawWorm.java:148-184), JawWorm.takeTurn
// (:120-146), AbstractMonster.rollMove/lastMove/lastTwoMoves/setMove/setHp
// (AbstractMonster.java:431-491,712-715,765-775). Design doc §9.

#include "sts/engine/monster_jaw_worm.hpp"

#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

namespace {

// setMove's moveHistory.add (AbstractMonster.java:433-435), against our fixed
// 3-slot most-recent-first ring (move_history[0] = most recent). Only the top
// two entries are ever inspected (lastMove/lastTwoMoves), so three slots are
// sufficient; older entries fall off the end.
void push_move(MonsterState& m, uint8_t move) noexcept {
    m.move_history[2] = m.move_history[1];
    m.move_history[1] = m.move_history[0];
    m.move_history[0] = move;
}

// lastMove(byte) (AbstractMonster.java:469-474): history non-empty and the
// most-recent entry == move. Empty slots read 0, and no move id is 0, so
// `move_history[0] == move` (move in 1..3) already encodes the non-empty check.
bool last_move(const MonsterState& m, uint8_t move) noexcept {
    return m.move_history[0] == move;
}

// lastTwoMoves(byte) (AbstractMonster.java:486-491): history has >= 2 entries
// and BOTH of the two most-recent == move. Same 0-sentinel reasoning as above:
// if fewer than two real moves are present, slot [1] is 0 != move.
bool last_two_moves(const MonsterState& m, uint8_t move) noexcept {
    return m.move_history[0] == move && m.move_history[1] == move;
}

// setMove(nextMove, intent, ...) (AbstractMonster.java:431-445): record the
// decided intent and append the move to history. Runs at DECISION time (inside
// getMove), which is why by the time turn N is executed the history already
// holds turns 1..N's decided moves.
void set_move(MonsterState& m, uint8_t move, MonsterIntent intent) noexcept {
    m.intent = static_cast<uint8_t>(intent);
    push_move(m, move);
}

// JawWorm.getMove for a NON-first move (JawWorm.java:155-183). Draws one
// aiRng.random(99) (already the ~d100 roll passed in as `num` by rollMove) and,
// on the two-way branches, one aiRng.randomBoolean tiebreak -- exactly the
// draws the game takes, in this order. `num` is the value of the random(99)
// draw the caller already performed.
void get_move(CombatState& state, MonsterState& m, int32_t num) noexcept {
    if (num < 25) {
        if (last_move(m, kMoveChomp)) {
            if (random_boolean(state.ai_rng, 0.5625f)) {
                set_move(m, kMoveBellow, MonsterIntent::DEFEND_BUFF);
            } else {
                set_move(m, kMoveThrash, MonsterIntent::ATTACK_DEFEND);
            }
        } else {
            set_move(m, kMoveChomp, MonsterIntent::ATTACK);
        }
    } else if (num < 55) {
        if (last_two_moves(m, kMoveThrash)) {
            if (random_boolean(state.ai_rng, 0.357f)) {
                set_move(m, kMoveChomp, MonsterIntent::ATTACK);
            } else {
                set_move(m, kMoveBellow, MonsterIntent::DEFEND_BUFF);
            }
        } else {
            set_move(m, kMoveThrash, MonsterIntent::ATTACK_DEFEND);
        }
    } else {
        // Source structures this as `else if (lastMove(BELLOW)) {...} else
        // Bellow` (JawWorm.java:175-183); the leading `num >= 55` is implicit in
        // reaching this branch. Semantics identical.
        if (last_move(m, kMoveBellow)) {
            if (random_boolean(state.ai_rng, 0.416f)) {
                set_move(m, kMoveChomp, MonsterIntent::ATTACK);
            } else {
                set_move(m, kMoveThrash, MonsterIntent::ATTACK_DEFEND);
            }
        } else {
            set_move(m, kMoveBellow, MonsterIntent::DEFEND_BUFF);
        }
    }
}

}  // namespace

void jaw_worm_init(CombatState& state, uint8_t monster_index) noexcept {
    MonsterState& m = state.monsters[monster_index];
    m.monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);

    // setHp(42, 46): one inclusive monsterHpRng draw; currentHealth == maxHealth
    // (AbstractMonster.java:765-775; JawWorm.java:81-82 for the A7+ range).
    const int32_t rolled =
        random(state.monster_hp_rng, kJawWormHpMin, kJawWormHpMax);
    m.hp = static_cast<int16_t>(rolled);
    m.max_hp = static_cast<int16_t>(rolled);

    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;

    // init() -> rollMove() -> getMove(aiRng.random(99)) (AbstractMonster.java:
    // 712-715,465-467). The forced-first-move path returns before using the
    // rolled value (JawWorm.java:150-154), but rollMove ALWAYS draws first, so
    // the draw is consumed and discarded here.
    (void)random(state.ai_rng, 99);
    set_move(m, kMoveChomp, MonsterIntent::ATTACK);
}

void jaw_worm_take_turn(CombatState& state, uint8_t monster_index) noexcept {
    MonsterState& m = state.monsters[monster_index];

    // (a) Execute the currently-decided move: no-op in A3.2 (no effect
    //     interpreter until A4.1). The move to execute is m.move_history[0] /
    //     m.intent, both set at its decision time. JawWorm.takeTurn's real
    //     DamageAction/GainBlockAction/ApplyPowerAction enqueues (JawWorm.java:
    //     120-144) land in A4.x, using the kJawWorm* stat constants.
    //
    // (b) Roll the NEXT move: JawWorm.takeTurn ends with an unconditional
    //     RollMoveAction (JawWorm.java:145) -> rollMove() -> getMove(aiRng
    //     .random(99)). One random(99) draw, then get_move may take one
    //     randomBoolean tiebreak.
    const int32_t num = random(state.ai_rng, 99);
    get_move(state, m, num);
}

}  // namespace sts::engine
