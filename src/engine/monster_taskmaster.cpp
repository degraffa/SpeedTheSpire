// The Taskmaster: the double constructor HP draw, the forced move, and the A18
// self-Strength presence branch. See monster_taskmaster.hpp for provenance and
// the three readings.

#include "sts/engine/monster_taskmaster.hpp"

#include <cassert>

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, set_monster_move, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kScouringWhip =
    sts::registry::kTaskmasterMoveScouringWhip;  // 2
constexpr uint8_t kSuperArgRoll = sts::registry::kTaskmasterRollSuperArgHp;

// getMove (:81-84) ignores `num` and forces move 2 every time. The draw that
// produced `num` is the caller's; this only records the decision.
void taskmaster_get_move(CombatState& s, uint8_t mi) noexcept {
    set_monster_move(s.monsters[mi], kScouringWhip,
                     MonsterIntent::ATTACK_DEBUFF);
}

}  // namespace

void taskmaster_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m = MonsterState{};
    m.monster_id = static_cast<uint16_t>(MonsterId::TASKMASTER);

    // DRAW ONE: the `super(...)` ARGUMENT, `monsterHpRng.random(54, 60)` (:50).
    // Java evaluates constructor arguments before the constructor body, so this
    // precedes the setHp below -- which is exactly what the registry roll's
    // CONSTRUCTOR_BEFORE_HP timing means, and it is asserted here rather than
    // assumed so a row edit that retimes it fails loudly. Its VALUE is discarded;
    // its POSITION in the monster_hp_rng sequence is the whole point (header
    // note (1)).
    const sts::registry::MonsterRollDef* super_arg =
        sts::registry::kTaskmaster.roll(kSuperArgRoll);
    assert(super_arg != nullptr);
    assert(super_arg->stream == sts::registry::MonsterRollStream::MONSTER_HP);
    assert(super_arg->timing ==
           sts::registry::MonsterRollTiming::CONSTRUCTOR_BEFORE_HP);
    (void)random(s.monster_hp_rng, super_arg->min(kMonsterAscension),
                 super_arg->max(kMonsterAscension));

    // DRAW TWO: setHp(57, 64) at ascension >= 8, else (54, 60) (:52-56) -- the
    // a8 column at the fixed A20. This is the one that decides the HP.
    const int32_t hp =
        random(s.monster_hp_rng,
               sts::registry::kTaskmaster.hp_min(kMonsterAscension),
               sts::registry::kTaskmaster.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);

    // init() -> rollMove -> getMove(aiRng.random(99)). The value is discarded --
    // getMove forces move 2 -- but the draw happens and moves the shared stream
    // (the Looter/Guardian/Chosen precedent).
    (void)random(s.ai_rng, 99);
    taskmaster_get_move(s, mi);
}

void taskmaster_roll_move(CombatState& s, uint8_t mi) noexcept {
    (void)random(s.ai_rng, 99);  // rollMove's draw; getMove ignores the value
    taskmaster_get_move(s, mi);
}

void taskmaster_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn case 2 (:66-79), in addToBottom order: playSfx (an SFXAction whose
    // MathUtils.random(1) at :87 is UNSEEDED), AnimateSlowAttackAction,
    // DamageAction(damage.get(1)), MakeTempCardInDiscardAction(Wound,
    // woundCount), and -- at ascension >= 18 only -- ApplyPowerAction(this, this,
    // StrengthPower(this, 1), 1). The first four are the registry program (the
    // two presentation actions queue nothing here); the fifth is the presence
    // branch.
    //
    // The switch has no other case, so a Taskmaster whose nextMove were anything
    // but 2 would queue only the trailing roll -- unreachable, since getMove sets
    // 2 unconditionally, and the effect-program lookup no-ops for an unknown move
    // id anyway.
    queue_monster_move_effects(s, mi, sts::registry::kTaskmaster, kScouringWhip);
    if (kMonsterAscension >= 18) {
        ActionQueueItem str{};
        str.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        str.src = mi;
        str.tgt = mi;  // ApplyPowerAction(this, this, StrengthPower(this, 1), 1)
        str.amount = 1;
        str.flags = make_apply_power_flags(PowerId::STRENGTH);
        add_to_bottom(s, str);
    }
    // The RollMoveAction at :78 sits OUTSIDE the switch.
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
