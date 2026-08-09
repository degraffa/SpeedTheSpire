// The Spiker: a composing A2+A17 Thorns opener, a saturating BUFF counter in
// pad0, and a permanent latch into ATTACK. See monster_spiker.hpp.

#include "sts/engine/monster_spiker.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, history helpers
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kAttack = sts::registry::kSpikerMoveAttack;          // 1
constexpr uint8_t kBuffThorns = sts::registry::kSpikerMoveBuffThorns;  // 2

// getMove (Spiker.java:97-110). `num` is the aiRng.random(99) the caller already
// drew. NOTE THAT THE LATCH IS TESTED FIRST and does not read num: past six
// BUFFs the roll is still made (rollMove always draws) but nothing looks at it.
void spiker_get_move(MonsterState& m, int32_t num) noexcept {
    if (m.pad0 > kSpikerThornsCountLatch) {
        // (:98-101) `thornsCount > 5` -- permanent, never cleared.
        set_monster_move(m, kAttack, MonsterIntent::ATTACK);
        return;
    }
    if (num < 50 && !last_move_is(m, kAttack)) {
        set_monster_move(m, kAttack, MonsterIntent::ATTACK);  // (:105)
        return;
    }
    set_monster_move(m, kBuffThorns, MonsterIntent::BUFF);  // (:109)
}

}  // namespace

uint8_t spiker_thorns_count(const CombatState& s, uint8_t mi) noexcept {
    return mi < kMonsterCap ? s.monsters[mi].pad0 : 0;
}

void spiker_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::SPIKER);
    // setHp (Spiker.java:57-61), the A7 column at kMonsterAscension: ONE
    // monster_hp_rng draw. The `super(..., 56, ...)` argument (:53) is a literal.
    const int32_t rolled = random(s.monster_hp_rng,
                                  sts::registry::kSpiker.hp_min(kMonsterAscension),
                                  sts::registry::kSpiker.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(rolled);
    m.max_hp = static_cast<int16_t>(rolled);
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;  // `private int thornsCount = 0;` (Spiker.java:51)
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). No firstMove
    // latch on this class: the init decision runs the same tree, and with an
    // empty history `!lastMove(ATTACK)` holds, so a Spiker opens on ATTACK
    // exactly when num < 50.
    const int32_t num = random(s.ai_rng, 99);
    spiker_get_move(m, num);
}

void spiker_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (Spiker.java:72-79): one addToBottom ApplyPowerAction(
    // this, this, new ThornsPower(this, A17 ? startingThorns + 3
    //                                       : startingThorns)).
    // 3-arg, so the stack amount is powerToApply.amount -- the same number.
    // spiker_starting_thorns composes the A2 and A17 branches as the Java does;
    // at kMonsterAscension 20 that is 7. No RNG draw.
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = mi;
    apply.tgt = mi;
    apply.amount = spiker_starting_thorns(kMonsterAscension);
    apply.flags = make_apply_power_flags(PowerId::THORNS);
    add_to_bottom(s, apply);
}

void spiker_roll_move(CombatState& s, uint8_t mi) noexcept {
    const int32_t num = random(s.ai_rng, 99);
    spiker_get_move(s.monsters[mi], num);
}

void spiker_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (Spiker.java:81-95). ATTACK is AnimateSlowAttack (presentation)
    // plus one DamageAction; BUFF_THORNS is `++thornsCount` (SYNCHRONOUS, at
    // :87, i.e. at queue time and not when the power lands) followed by the
    // ApplyPowerAction. The counter increment therefore happens BEFORE the
    // trailing roll reads it, which is what makes the sixth BUFF the last one.
    MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];
    if (move == kBuffThorns && m.pad0 < kSpikerThornsCountMax) {
        ++m.pad0;  // saturating (see the header): the only reader tests > 5
    }
    queue_monster_move_effects(s, mi, sts::registry::kSpiker, move);
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
