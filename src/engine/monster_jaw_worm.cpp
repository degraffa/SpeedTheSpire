// Jaw Worm AI + monster turn. Move selection (getMove) is native code below;
// stats and move-effect programs come from the generated monster table
// (registry/monsters.yaml). See monster_jaw_worm.hpp for the full provenance,
// scope boundary, and draw-counting convention.
//
// Provenance: JawWorm.getMove (JawWorm.java:148-184), JawWorm.takeTurn
// (:120-146), AbstractMonster.rollMove/lastMove/lastTwoMoves/setMove/setHp
// (AbstractMonster.java:431-491,712-715,765-775). Design doc §9.

#include "sts/engine/monster_jaw_worm.hpp"

#include "sts/engine/action_queue.hpp"  // add_to_bottom, ActionQueueItem, kActorPlayer
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

// Enqueue the CURRENT move's effect program from the generated monster table,
// in JawWorm.takeTurn's addToBottom order (JawWorm.java:120-146) -- the table
// rows carry the steps in exactly that order, and the amounts are the
// per-ascension columns resolved at the skeleton's fixed A20 (chompDmg 12,
// bellowStr 5 / bellowBlock 9, thrashDmg 7 / thrashBlock 5; per-column
// citations in registry/monsters.yaml). The move to execute is
// m.move_history[0] (decided at its roll time). Effects are QUEUED
// (add_to_bottom), never applied inline, so they resolve through the pump
// priority loop exactly like a card's effects -- the monster's Strength is read
// by the DAMAGE pipeline (compute_damage) at resolution, so Chomp/Thrash pick
// up any Strength from a prior Bellow automatically.
void queue_move_effects(CombatState& state, uint8_t mi, uint8_t move) noexcept {
    const sts::registry::MonsterMove* mv = sts::registry::kJawWorm.move(move);
    if (mv == nullptr) {
        return;  // unknown/empty move id: nothing decided yet (defensive)
    }
    for (uint8_t i = 0; i < mv->effect_count; ++i) {
        const sts::registry::MonsterMoveEffect& e = mv->effects[i];
        ActionQueueItem it{};
        // sts::registry::Opcode is pinned byte-equal to interp.hpp's Opcode
        // (static_asserts in cards.hpp), so the raw cast dispatches correctly.
        it.opcode = static_cast<uint16_t>(e.op);
        it.src = mi;
        it.tgt = (e.target == sts::registry::MonsterMoveTarget::SELF)
                     ? mi
                     : kActorPlayer;
        it.amount = e.amount.at(kSkeletonAscension);
        it.flags = e.extra;  // APPLY_POWER: PowerId (make_apply_power_flags packing)
        add_to_bottom(state, it);
    }
}

}  // namespace

void jaw_worm_init(CombatState& state, uint8_t monster_index) noexcept {
    MonsterState& m = state.monsters[monster_index];
    m.monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);

    // setHp(42, 46): one inclusive monsterHpRng draw; currentHealth == maxHealth
    // (AbstractMonster.java:765-775). The range is the table's a7 column
    // (JawWorm.java:81-82) resolved at the skeleton's fixed A20.
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

    // (a) Execute the currently-decided move: QUEUE its real DamageAction/
    //     GainBlockAction/ApplyPowerAction effects in JawWorm.takeTurn order
    //     (JawWorm.java:120-146). The move to execute is m.move_history[0],
    //     decided at its roll time.
    queue_move_effects(state, monster_index, m.move_history[0]);

    // (b) Roll the NEXT move: JawWorm.takeTurn ends with an unconditional
    //     RollMoveAction (JawWorm.java:145) -> rollMove() -> getMove(aiRng
    //     .random(99)). One random(99) draw, then get_move may take one
    //     randomBoolean tiebreak. The roll runs here (synchronously) while the
    //     effects above are still queued; since the roll only touches ai_rng /
    //     move_history / intent and the effects only touch hp/block/powers, their
    //     relative order does not change the resolved state.
    const int32_t num = random(state.ai_rng, 99);
    get_move(state, m, num);
}

}  // namespace sts::engine
