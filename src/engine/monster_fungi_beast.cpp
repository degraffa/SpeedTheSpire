// The Fungi Beast: native move selection, turn body and pre-battle Spore Cloud.
// See monster_fungi_beast.hpp for provenance, the per-stream draw accounting and
// the animation-only damage() finding.

#include "sts/engine/monster_fungi_beast.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, set_monster_move, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kBite = sts::registry::kFungiBeastMoveBite;  // 1
constexpr uint8_t kGrow = sts::registry::kFungiBeastMoveGrow;  // 2

// FungiBeast.VULN_AMT (FungiBeast.java:52) -- the Spore Cloud stack, a fixed 2
// with no ascension branch (:77).
constexpr int32_t kSporeCloudAmount = 2;

// FungiBeast.getMove (FungiBeast.java:100-113), transcribed branch for branch.
// `num` is the aiRng.random(99) the caller already drew.
void fungi_get_move(MonsterState& m, int32_t num) noexcept {
    if (num < 60) {
        if (last_two_moves_are(m, kBite)) {
            set_monster_move(m, kGrow, MonsterIntent::BUFF);  // (:103-104)
        } else {
            set_monster_move(m, kBite, MonsterIntent::ATTACK);  // (:106)
        }
        return;
    }
    if (last_move_is(m, kGrow)) {
        set_monster_move(m, kBite, MonsterIntent::ATTACK);  // (:108-109)
        return;
    }
    set_monster_move(m, kGrow, MonsterIntent::BUFF);  // (:111)
}

}  // namespace

void fungi_beast_init(CombatState& s, uint8_t mi) noexcept {
    // The ctor is `super(...)` + setHp(min, max): exactly one monster_hp_rng
    // inclusive draw, currentHealth == maxHealth (AbstractMonster.java:765-775).
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::FUNGI_BEAST);
    const int32_t hp =
        random(s.monster_hp_rng, sts::registry::kFungiBeast.hp_min(kMonsterAscension),
               sts::registry::kFungiBeast.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). With an
    // empty move history both predicates are false, so the opener is Bite below
    // 60 and Grow at 60+.
    const int32_t num = random(s.ai_rng, 99);
    fungi_get_move(m, num);
}

void fungi_beast_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (:75-78): addToBottom ApplyPowerAction(this, this, new
    // SporeCloudPower(this, 2)). Applied to ITSELF; released onto the player when
    // it dies (power_spore_cloud.cpp). No RNG draw.
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = mi;
    apply.tgt = mi;
    apply.amount = kSporeCloudAmount;
    apply.flags = make_apply_power_flags(PowerId::SPORE_CLOUD);
    add_to_bottom(s, apply);
}

void fungi_beast_roll_move(CombatState& s, uint8_t mi) noexcept {
    const int32_t num = random(s.ai_rng, 99);
    fungi_get_move(s.monsters[mi], num);
}

void fungi_beast_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (:80-98): case BITE queues a ChangeState + WaitAction
    // (presentation) then the DamageAction; case GROW queues the
    // ApplyPowerAction(Strength) on itself, at strAmt or strAmt+1 from A17 --
    // both are the registry program. The trailing RollMoveAction (:97) is
    // OUTSIDE the switch, so both cases reach it.
    queue_monster_move_effects(s, mi, sts::registry::kFungiBeast,
                               s.monsters[mi].move_history[0]);
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
