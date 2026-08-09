// The Time Eater: the one move tree in this batch that recurses, plus the two
// runtime numbers (the Haste heal and the A19 presence gates) no effect list can
// carry. See monster_time_eater.hpp for provenance, the termination argument and
// the checked negatives.

#include "sts/engine/monster_time_eater.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effect(s), history helpers, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kReverberate = sts::registry::kTimeEaterMoveReverberate;  // 2
constexpr uint8_t kRipple = sts::registry::kTimeEaterMoveRipple;            // 3
constexpr uint8_t kHeadSlam = sts::registry::kTimeEaterMoveHeadSlam;        // 4
constexpr uint8_t kHaste = sts::registry::kTimeEaterMoveHaste;              // 5

// The A19-only step of each gated move -- the index the module stops at below
// A19. Named so the coupling to monsters.yaml id 63 is visible from here.
constexpr uint8_t kRippleStepsBelowA19 = 3;    // Block, Vulnerable, Weak  (Frail is [3])
constexpr uint8_t kHeadSlamStepsBelowA19 = 2;  // Damage, Draw Reduction   (Slimed is [2])
constexpr uint8_t kHasteStepRemoveDebuffs = 0;
constexpr uint8_t kHasteStepRemoveShackled = 1;
constexpr uint8_t kHasteStepBlockA19 = 2;

[[nodiscard]] bool used_haste(const MonsterState& m) noexcept {
    return (m.flags & kMonsterFlagTimeEaterUsedHaste) != 0u;
}

}  // namespace

void time_eater_decide_move(CombatState& s, uint8_t mi, int32_t num) noexcept {
    MonsterState& m = s.monsters[mi];
    // (:178-182) `if (currentHealth < maxHealth / 2 && !usedHaste)`. STRICT `<`
    // and INTEGER division; re-tested on every re-entry below, which is harmless
    // because the latch is already set once it has fired.
    if (m.hp < static_cast<int16_t>(m.max_hp / 2) && !used_haste(m)) {
        m.flags |= kMonsterFlagTimeEaterUsedHaste;
        set_monster_move(m, kHaste, MonsterIntent::BUFF);
        return;
    }
    if (num < 45) {
        if (!last_two_moves_are(m, kReverberate)) {
            set_monster_move(m, kReverberate, MonsterIntent::ATTACK);  // (:185)
            return;
        }
        // `this.getMove(AbstractDungeon.aiRng.random(50, 99));` (:188) -- an
        // EXTRA draw, inclusive of both ends, spent only when the guard blocks.
        time_eater_decide_move(s, mi, random(s.ai_rng, 50, 99));
        return;
    }
    if (num < 80) {
        if (!last_move_is(m, kHeadSlam)) {
            set_monster_move(m, kHeadSlam, MonsterIntent::ATTACK_DEBUFF);  // (:193)
            return;
        }
        // `if (AbstractDungeon.aiRng.randomBoolean(0.66f))` (:196) -- an EXTRA
        // draw, and NOT a re-entry: whichever way it lands, this arm terminates.
        if (random_boolean(s.ai_rng, 0.66f)) {
            set_monster_move(m, kReverberate, MonsterIntent::ATTACK);  // (:197)
            return;
        }
        set_monster_move(m, kRipple, MonsterIntent::DEFEND_DEBUFF);  // (:200)
        return;
    }
    if (!last_move_is(m, kRipple)) {
        set_monster_move(m, kRipple, MonsterIntent::DEFEND_DEBUFF);  // (:204)
        return;
    }
    // `this.getMove(AbstractDungeon.aiRng.random(74));` (:206) -- 0..74, so this
    // re-entry CAN land back in the <45 band. It terminates because the guard
    // that sent it here (lastMove == 3) makes the <45 band's guard false; see the
    // termination argument in the header.
    time_eater_decide_move(s, mi, random(s.ai_rng, 74));
}

void time_eater_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::TIME_EATER);
    // setHp(480)/setHp(456) (:77-81): the single-arg overload, one degenerate
    // monster_hp_rng draw. The HP / A_2_HP field NAMES are misleading -- the
    // branch is `>= 9` (:78) -- exactly as the Hexaghost's are.
    const auto& def = sts::registry::kTimeEater;
    const int32_t rolled = random(s.monster_hp_rng, def.hp_min(kMonsterAscension),
                                  def.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(rolled);
    m.max_hp = m.hp;
    m.block = 0;
    m.flags = 0;  // usedHaste false (:73)
    m.power_count = 0;
    m.pad0 = 0;  // unused by this monster
    m.draw_x = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). This getMove
    // genuinely READS num, and with an empty history no guard can block, so the
    // opener is seed-dependent: REVERBERATE below 45, HEAD_SLAM below 80, RIPPLE
    // above. The Haste arm cannot preempt it -- the boss is at full HP.
    time_eater_decide_move(s, mi, random(s.ai_rng, 99));
}

void time_eater_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // (:102-108). The BGM/scene calls and the UnlockTracker are presentation; the
    // one real line is `addToBottom(new ApplyPowerAction(this, this,
    // new TimeWarpPower(this)))` (:107). The 3-arg ApplyPowerAction takes the
    // stack amount FROM the constructed power, and TimeWarpPower's ctor sets
    // amount = 0 (TimeWarpPower.java:26) -- the counter starts at zero, not at
    // twelve. No ascension branch.
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = mi;
    apply.tgt = mi;
    apply.amount = 0;
    apply.flags = make_apply_power_flags(PowerId::TIME_WARP);
    add_to_bottom(s, apply);
}

void time_eater_roll_move(CombatState& s, uint8_t mi) noexcept {
    time_eater_decide_move(s, mi, random(s.ai_rng, 99));
}

void time_eater_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (:111-155). The firstTurn TalkAction (:112-119) is presentation on
    // an unseeded clock and needs no storage; every ChangeStateAction / WaitAction
    // / VFXAction / ShoutAction in the switch likewise.
    const auto& def = sts::registry::kTimeEater;
    const uint8_t move = s.monsters[mi].move_history[0];
    const bool a19 = kMonsterAscension >= 19;

    if (move == kRipple) {
        // (:129-133). Steps 0..2 always; step 3 (Frail) is an A19 PRESENCE arm,
        // which an effect list cannot express -- hence the per-step form.
        const uint8_t n = a19 ? 4u : kRippleStepsBelowA19;
        for (uint8_t i = 0; i < n; ++i) {
            queue_monster_move_effect(s, mi, def, move, i, kMoveTargetFromStep);
        }
    } else if (move == kHeadSlam) {
        // (:137-142). Steps 0..1 always; step 2 (two Slimed) is A19-only.
        const uint8_t n = a19 ? 3u : kHeadSlamStepsBelowA19;
        for (uint8_t i = 0; i < n; ++i) {
            queue_monster_move_effect(s, mi, def, move, i, kMoveTargetFromStep);
        }
    } else if (move == kHaste) {
        // (:146-151), in addToBottom order.
        queue_monster_move_effect(s, mi, def, move, kHasteStepRemoveDebuffs,
                                  kMoveTargetFromStep);
        queue_monster_move_effect(s, mi, def, move, kHasteStepRemoveShackled,
                                  kMoveTargetFromStep);
        // HealAction(this, this, this.maxHealth / 2 - this.currentHealth) (:149).
        // A RUNTIME amount -- integer division, read HERE at queue time from the
        // live sheet, which is what the Java's expression does too (the action is
        // constructed with the number, not with a lambda). It can be NEGATIVE if
        // the boss is at or above half when Haste resolves; op_heal's
        // `amount <= 0` guard drops that, so a negative heal does nothing rather
        // than damaging the boss. getMove's own gate makes it unreachable in a
        // played-out fight.
        const MonsterState& m = s.monsters[mi];
        ActionQueueItem heal{};
        heal.opcode = static_cast<uint16_t>(Opcode::HEAL);
        heal.src = mi;
        heal.tgt = mi;
        heal.amount = static_cast<int32_t>(m.max_hp / 2) - static_cast<int32_t>(m.hp);
        add_to_bottom(s, heal);
        if (a19) {
            queue_monster_move_effect(s, mi, def, move, kHasteStepBlockA19,
                                      kMoveTargetFromStep);
        }
    } else {
        // REVERBERATE (:122-125) -- three flat damage steps, the whole program.
        queue_monster_move_effects(s, mi, def, move);
    }

    // RollMoveAction (:154), OUTSIDE the switch, so every case reaches it.
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
