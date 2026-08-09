// The Repulsor: one HP draw, a two-move history tree, and a DAZE that costs two
// card_random_rng draws. See monster_repulsor.hpp for provenance.

#include "sts/engine/monster_repulsor.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, history helpers
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kDaze = sts::registry::kRepulsorMoveDaze;      // 1
constexpr uint8_t kAttack = sts::registry::kRepulsorMoveAttack;  // 2

// getMove (Repulsor.java:75-82) in full. `num` is the aiRng.random(99) the
// caller already drew, and it IS read -- both here and on the init roll, which
// is why this monster has no separate firstMove entry point.
//
// The `&&` order matters for nothing (neither operand draws), but the DEFAULT
// arm does: anything that is not "a low roll and the last move was not ATTACK"
// is a DAZE, so two ATTACKs never run back to back and DAZE is what a Repulsor
// mostly does.
void repulsor_get_move(MonsterState& m, int32_t num) noexcept {
    if (num < 20 && !last_move_is(m, kAttack)) {
        set_monster_move(m, kAttack, MonsterIntent::ATTACK);  // (:77)
        return;
    }
    set_monster_move(m, kDaze, MonsterIntent::DEBUFF);  // (:81)
}

}  // namespace

void repulsor_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::REPULSOR);
    // setHp (Repulsor.java:50-54), the A7 column at kMonsterAscension: ONE
    // monster_hp_rng draw. The `super(..., 35, ...)` maxHealth argument (:45) is
    // a literal and costs nothing -- contrast the Orb Walker, whose super
    // argument is itself a draw.
    const int32_t rolled = random(s.monster_hp_rng,
                                  sts::registry::kRepulsor.hp_min(kMonsterAscension),
                                  sts::registry::kRepulsor.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(rolled);
    m.max_hp = static_cast<int16_t>(rolled);
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). There is no
    // firstMove latch on this class, so the init decision runs the same tree
    // every later decision runs -- and with an empty history `!lastMove(ATTACK)`
    // is true, so a Repulsor opens on ATTACK exactly when num < 20.
    const int32_t num = random(s.ai_rng, 99);
    repulsor_get_move(m, num);
}

void repulsor_roll_move(CombatState& s, uint8_t mi) noexcept {
    const int32_t num = random(s.ai_rng, 99);
    repulsor_get_move(s.monsters[mi], num);
}

void repulsor_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (Repulsor.java:59-73). ATTACK is AnimateSlowAttack (presentation)
    // plus one DamageAction; DAZE is one MakeTempCardInDrawPileAction of 2 --
    // both are the registry program. The RollMoveAction at :72 is outside the
    // switch and is QUEUED, not rolled inline: it must resolve after whatever
    // the move body queued.
    queue_monster_move_effects(s, mi, sts::registry::kRepulsor,
                               s.monsters[mi].move_history[0]);
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
