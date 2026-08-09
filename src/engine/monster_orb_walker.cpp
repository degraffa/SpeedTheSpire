// The Orb Walker: the batch's sharpest stream trap (a discarded super-argument
// HP roll drawn BEFORE the setHp roll), a Burn that lands in two piles, and a
// pre-battle power that ramps Strength every round. See monster_orb_walker.hpp.

#include "sts/engine/monster_orb_walker.hpp"

#include <cassert>

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, history helpers
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kLaser = sts::registry::kOrbWalkerMoveLaser;  // 1
constexpr uint8_t kClaw = sts::registry::kOrbWalkerMoveClaw;    // 2
constexpr uint8_t kSuperArgRoll = sts::registry::kOrbWalkerRollSuperArgHp;

// getMove (OrbWalker.java:100-113). `num` is the aiRng.random(99) the caller
// already drew; no ascension branch, no second draw on any path.
void orb_walker_get_move(MonsterState& m, int32_t num) noexcept {
    if (num < 40) {
        if (!last_two_moves_are(m, kClaw)) {
            set_monster_move(m, kClaw, MonsterIntent::ATTACK);  // (:104)
        } else {
            set_monster_move(m, kLaser, MonsterIntent::ATTACK_DEBUFF);  // (:106)
        }
        return;
    }
    if (!last_two_moves_are(m, kLaser)) {
        set_monster_move(m, kLaser, MonsterIntent::ATTACK_DEBUFF);  // (:109)
        return;
    }
    set_monster_move(m, kClaw, MonsterIntent::ATTACK);  // (:112)
}

}  // namespace

void orb_walker_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::ORB_WALKER);

    // (1) THE SUPER-ARGUMENT DRAW, FIRST AND DISCARDED (OrbWalker.java:53). Its
    // range is registry data with timing CONSTRUCTOR_BEFORE_HP, not a literal
    // here, so this call site and burn_unspawned_ctor_rolls read the same table
    // and cannot drift on either the range or the ORDER.
    const sts::registry::MonsterRollDef* super_roll =
        sts::registry::kOrbWalker.roll(kSuperArgRoll);
    assert(super_roll != nullptr);
    assert(super_roll->stream == sts::registry::MonsterRollStream::MONSTER_HP);
    assert(super_roll->timing ==
           sts::registry::MonsterRollTiming::CONSTRUCTOR_BEFORE_HP);
    (void)random(s.monster_hp_rng, super_roll->min(kMonsterAscension),
                 super_roll->max(kMonsterAscension));

    // (2) setHp (:54-58): the SECOND draw, over the tiered column, and its value
    // is the one that survives.
    const int32_t rolled = random(s.monster_hp_rng,
                                  sts::registry::kOrbWalker.hp_min(kMonsterAscension),
                                  sts::registry::kOrbWalker.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(rolled);
    m.max_hp = static_cast<int16_t>(rolled);
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). No firstMove
    // latch: with an empty history both lastTwoMoves tests are false, so the init
    // decision is CLAW below 40 and LASER at or above it.
    const int32_t num = random(s.ai_rng, 99);
    orb_walker_get_move(m, num);
}

void orb_walker_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (OrbWalker.java:73-80): one addToBottom
    // ApplyPowerAction(this, this, new GenericStrengthUpPower(this, MOVES[0],
    // A17 ? 5 : 3)). 3-arg, so the stack amount is powerToApply.amount -- the
    // same number. MOVES[0] is only the power's DISPLAY name (the ctor's
    // `newName`); the POWER_ID is shared, which is why one registry row covers
    // every user. No RNG draw.
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = mi;
    apply.tgt = mi;
    apply.amount = orb_walker_strength_up(kMonsterAscension);
    apply.flags = make_apply_power_flags(PowerId::GENERIC_STRENGTH_UP);
    add_to_bottom(s, apply);
}

void orb_walker_roll_move(CombatState& s, uint8_t mi) noexcept {
    const int32_t num = random(s.ai_rng, 99);
    orb_walker_get_move(s.monsters[mi], num);
}

void orb_walker_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (OrbWalker.java:82-98). CLAW is AnimateSlowAttack + one
    // DamageAction; LASER is ChangeState/Wait + one DamageAction + the
    // two-pile Burn -- all registry program, in addToBottom order. The
    // RollMoveAction at :97 is outside the switch and is QUEUED: it must resolve
    // after the two MAKE_CARD items, one of which draws card_random_rng.
    queue_monster_move_effects(s, mi, sts::registry::kOrbWalker,
                               s.monsters[mi].move_history[0]);
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
