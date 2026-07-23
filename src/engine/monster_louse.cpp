// Louse AI + monster turn + curl-up pre-battle (B3.13). Move selection is native;
// stats/effects come from the generated table. See monster_louse.hpp for
// provenance, scope, and the draw-counting convention.

#include "sts/engine/monster_louse.hpp"

#include <cassert>

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem, kActorPlayer
#include "sts/engine/interp.hpp"            // Opcode
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, move-history helpers, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/registry/monster_table.hpp"   // kLouseNormal/kLouseDefensive + move-id constants

namespace sts::engine {

namespace {

// Move ids are identical across both louse variants (BITE=3, move-4=Strengthen/
// Weaken); use the LouseNormal constants as the shared names.
constexpr uint8_t kBite = sts::registry::kLouseNormalMoveBite;         // 3
constexpr uint8_t kMove4 = sts::registry::kLouseNormalMoveStrengthen;  // 4
constexpr uint8_t kBiteRoll = sts::registry::kLouseNormalRollBiteDamage;
constexpr uint8_t kCurlUpRoll = sts::registry::kLouseNormalRollCurlUp;
static_assert(kBiteRoll == sts::registry::kLouseDefensiveRollBiteDamage);
static_assert(kCurlUpRoll == sts::registry::kLouseDefensiveRollCurlUp);

// getMove (Louse*.getMove): the skeleton A20 takes the A17 branch. One aiRng draw
// (num) already performed by the caller; num is USED, no extra booleans.
void louse_get_move(MonsterState& m, int32_t num,
                    MonsterIntent move4_intent) noexcept {
    if (num < 25) {
        if (last_move_is(m, kMove4)) {
            set_monster_move(m, kBite, MonsterIntent::ATTACK);
        } else {
            set_monster_move(m, kMove4, move4_intent);
        }
    } else if (last_two_moves_are(m, kBite)) {
        set_monster_move(m, kMove4, move4_intent);
    } else {
        set_monster_move(m, kBite, MonsterIntent::ATTACK);
    }
}

void louse_init_impl(CombatState& state, uint8_t mi, MonsterId id,
                     const sts::registry::MonsterDef& def,
                     MonsterIntent move4_intent) noexcept {
    MonsterState& m = state.monsters[mi];
    m.monster_id = static_cast<uint16_t>(id);

    // (1) setHp: A7 column at A20; one monster_hp_rng draw, currentHealth==maxHealth.
    const int32_t rolled = random(state.monster_hp_rng,
                                  def.hp_min(kMonsterAscension),
                                  def.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(rolled);
    m.max_hp = static_cast<int16_t>(rolled);

    // (2) biteDamage: a SECOND monster_hp_rng draw IN THE CTOR
    // (LouseNormal.java:60 / LouseDefensive.java:63). The inclusive base/A2
    // ranges and CONSTRUCTOR_AFTER_HP timing are registry data.
    const sts::registry::MonsterRollDef* bite_roll = def.roll(kBiteRoll);
    assert(bite_roll != nullptr);
    assert(bite_roll->stream == sts::registry::MonsterRollStream::MONSTER_HP);
    assert(bite_roll->timing ==
           sts::registry::MonsterRollTiming::CONSTRUCTOR_AFTER_HP);
    const int32_t bite = random(state.monster_hp_rng,
                                bite_roll->min(kMonsterAscension),
                                bite_roll->max(kMonsterAscension));

    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = static_cast<uint8_t>(bite);
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;

    // init() rollMove: getMove(aiRng.random(99)). No forced first move -- the real
    // tree runs on the drawn num.
    const int32_t num = random(state.ai_rng, 99);
    louse_get_move(m, num, move4_intent);
}

void louse_take_turn_impl(CombatState& state, uint8_t mi,
                          const sts::registry::MonsterDef& def,
                          MonsterIntent move4_intent) noexcept {
    MonsterState& m = state.monsters[mi];
    const uint8_t move = m.move_history[0];

    if (move == kBite) {
        // BITE: DamageAction with the per-instance rolled bite damage (pad0). Not a
        // table column, so queued directly (the table's placeholder amount is 0).
        ActionQueueItem it{};
        it.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
        it.src = mi;
        it.tgt = kActorPlayer;
        it.amount = static_cast<int32_t>(m.pad0);
        add_to_bottom(state, it);
    } else {
        // STRENGTHEN / WEAKEN (move 4): the table APPLY_POWER effect.
        queue_monster_move_effects(state, mi, def, move);
    }

    // RollMoveAction -> getMove(aiRng.random(99)), num used.
    const int32_t num = random(state.ai_rng, 99);
    louse_get_move(m, num, move4_intent);
}

}  // namespace

void louse_normal_init(CombatState& state, uint8_t monster_index) noexcept {
    louse_init_impl(state, monster_index, MonsterId::LOUSE_NORMAL,
                    sts::registry::kLouseNormal, MonsterIntent::BUFF);
}

void louse_defensive_init(CombatState& state, uint8_t monster_index) noexcept {
    louse_init_impl(state, monster_index, MonsterId::LOUSE_DEFENSIVE,
                    sts::registry::kLouseDefensive, MonsterIntent::DEBUFF);
}

void louse_normal_take_turn(CombatState& state, uint8_t monster_index) noexcept {
    louse_take_turn_impl(state, monster_index, sts::registry::kLouseNormal,
                         MonsterIntent::BUFF);
}

void louse_defensive_take_turn(CombatState& state, uint8_t monster_index) noexcept {
    louse_take_turn_impl(state, monster_index, sts::registry::kLouseDefensive,
                         MonsterIntent::DEBUFF);
}

void louse_use_pre_battle_action(CombatState& state,
                                 uint8_t monster_index) noexcept {
    MonsterState& m = state.monsters[monster_index];
    const MonsterId id = static_cast<MonsterId>(m.monster_id);
    const sts::registry::MonsterDef* def = sts::registry::monster_def(id);
    assert(def != nullptr);
    const sts::registry::MonsterRollDef* curl_roll = def->roll(kCurlUpRoll);
    assert(curl_roll != nullptr);
    assert(curl_roll->stream == sts::registry::MonsterRollStream::MONSTER_HP);
    assert(curl_roll->timing == sts::registry::MonsterRollTiming::PRE_BATTLE);

    // Louse*.usePreBattleAction: one monster_hp_rng roll from the registry's
    // base/A7/A17 inclusive range, then addToBottom ApplyPowerAction(CurlUp).
    const int32_t block = random(state.monster_hp_rng,
                                 curl_roll->min(kMonsterAscension),
                                 curl_roll->max(kMonsterAscension));
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = monster_index;
    apply.tgt = monster_index;
    apply.amount = block;
    apply.flags = make_apply_power_flags(PowerId::CURL_UP);
    add_to_bottom(state, apply);
}

}  // namespace sts::engine
