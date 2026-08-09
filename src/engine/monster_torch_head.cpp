// The Torch Head: the ctor-telegraph double push, the one lifetime ai_rng
// draw, and the SetMoveAction re-telegraph. See monster_torch_head.hpp.

#include "sts/engine/monster_torch_head.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, move helpers
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kTackle = sts::registry::kTorchHeadMoveTackle;  // 1

}  // namespace

void torch_head_init(CombatState& s, uint8_t mi) noexcept {
    // The full ctor: super-arg draw (flat (38,40), the registry SUPER_ARG_HP
    // row), the ctor's OWN setMove push (:45), the tiered setHp (:50-54), then
    // init()'s rollMove -- one discarded draw and the second push (:81-83).
    // Unreachable from any encounter today (the Bronze Orb's reasoning).
    MonsterState& m = s.monsters[mi];
    m = MonsterState{};
    m.monster_id = static_cast<uint16_t>(MonsterId::TORCH_HEAD);
    const auto& def = sts::registry::kTorchHead;
    const sts::registry::MonsterRollDef* super_arg = def.roll(0);
    if (super_arg != nullptr) {
        (void)random(s.monster_hp_rng, super_arg->min(kMonsterAscension),
                     super_arg->max(kMonsterAscension));
    }
    set_monster_move(m, kTackle, MonsterIntent::ATTACK);  // ctor setMove (:45)
    const int32_t hp = random(s.monster_hp_rng, def.hp_min(kMonsterAscension),
                              def.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = m.hp;
    (void)random(s.ai_rng, 99);                           // rollMove's draw
    set_monster_move(m, kTackle, MonsterIntent::ATTACK);  // getMove (:82)
}

void torch_head_spawn_at_hp(CombatState& s, uint8_t mi, int16_t hp) noexcept {
    // The ctor's setMove ran at CONSTRUCTION (the spawner's queue time), but
    // it writes only this record's own history -- so replaying it here, at
    // the spawn's resolve, is byte-identical: nothing read the record in
    // between (it did not exist). Then init()'s one discarded draw and the
    // second push -- header note (1).
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::TORCH_HEAD);
    m.hp = hp;
    m.max_hp = hp;
    set_monster_move(m, kTackle, MonsterIntent::ATTACK);  // ctor setMove (:45)
    (void)random(s.ai_rng, 99);                           // rollMove's draw
    set_monster_move(m, kTackle, MonsterIntent::ATTACK);  // getMove (:82)
}

void torch_head_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (:58-66): one DamageAction, then the queued SetMoveAction(1,
    // ATTACK, 7) -- NO trailing RollMoveAction, so no ai_rng draw ever again
    // (header note (2)).
    queue_monster_move_effects(s, mi, sts::registry::kTorchHead, kTackle);
    ActionQueueItem set_move{};
    set_move.opcode = static_cast<uint16_t>(Opcode::SET_MOVE);
    set_move.src = mi;
    set_move.tgt = mi;
    set_move.amount = kTackle;
    set_move.flags = static_cast<uint32_t>(MonsterIntent::ATTACK);
    add_to_bottom(s, set_move);
}

}  // namespace sts::engine
