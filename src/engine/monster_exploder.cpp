// The Exploder: an HP draw over a range that is degenerate below A7 (and still
// draws), a discarded ai_rng roll, a storage-free turnCount, and a pre-battle
// fuse whose detonation lives in ExplosivePower. See monster_exploder.hpp.

#include "sts/engine/monster_exploder.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, history helpers
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kAttack = sts::registry::kExploderMoveAttack;  // 1
constexpr uint8_t kBlock = sts::registry::kExploderMoveBlock;    // 2

// getMove (Exploder.java:86-92), with `turnCount` derived from the move history
// rather than stored -- the derivation and why the obvious form of it is wrong
// are argued at length in monster_exploder.hpp. It takes no `num` parameter
// because the Java reads none; the caller draws and discards, which is where
// that draw is spelled out.
void exploder_get_move(MonsterState& m) noexcept {
    // `turnCount >= 2`, exactly. move 2 is absorbing, so once it has been
    // decided the first disjunct carries the answer forever; before that, two
    // consecutive ATTACK decisions are what two taken turns look like.
    if (last_move_is(m, kBlock) || last_two_moves_are(m, kAttack)) {
        set_monster_move(m, kBlock, MonsterIntent::UNKNOWN);  // (:90)
        return;
    }
    set_monster_move(m, kAttack, MonsterIntent::ATTACK);  // (:88)
}

}  // namespace

void exploder_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::EXPLODER);
    // setHp (Exploder.java:56-60): the A7 column {30,35} at kMonsterAscension.
    // BELOW A7 the range is the degenerate {30,30} -- and it STILL DRAWS, which
    // is why the registry spells that column out and why this call is an
    // unconditional random() rather than a "skip when min == max" shortcut. The
    // Spheric Guardian's zero-draw init is a different situation entirely (setHp
    // is never called there); do not carry its reasoning over.
    const int32_t rolled = random(s.monster_hp_rng,
                                  sts::registry::kExploder.hp_min(kMonsterAscension),
                                  sts::registry::kExploder.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(rolled);
    m.max_hp = static_cast<int16_t>(rolled);
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). getMove
    // never reads num on ANY branch, so this value is discarded -- but the draw
    // happens and moves the stream its groupmates share.
    (void)random(s.ai_rng, 99);
    exploder_get_move(m);  // turnCount 0 -> ATTACK
}

void exploder_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (Exploder.java:64-67): one addToBottom
    // ApplyPowerAction(this, this, new ExplosivePower(this, 3)). The 3-arg form,
    // so the stack amount is powerToApply.amount -- the same 3. No RNG draw.
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = mi;
    apply.tgt = mi;
    apply.amount = kExploderExplosiveAmount;
    apply.flags = make_apply_power_flags(PowerId::EXPLOSIVE);
    add_to_bottom(s, apply);
}

void exploder_roll_move(CombatState& s, uint8_t mi) noexcept {
    (void)random(s.ai_rng, 99);  // drawn and discarded (see the header)
    exploder_get_move(s.monsters[mi]);
}

void exploder_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (Exploder.java:69-84). ATTACK is AnimateSlowAttack (presentation)
    // plus one DamageAction; move 2 is a literal empty case, which the registry
    // carries as a NOP step. `++turnCount` at :71 has no storage here -- see the
    // header. The RollMoveAction at :82 is outside the switch and is QUEUED, so
    // it resolves after the move body and BEFORE ExplosivePower's duringTurn
    // items, which applyTurnPowers appends afterwards.
    queue_monster_move_effects(s, mi, sts::registry::kExploder,
                               s.monsters[mi].move_history[0]);
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
