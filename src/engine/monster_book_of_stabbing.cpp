// The Book of Stabbing: the growing stab counter, its saturating pad0 storage
// and the Painful Stabs opener. See monster_book_of_stabbing.hpp for provenance
// and the four readings.

#include "sts/engine/monster_book_of_stabbing.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effect(s), move helpers, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kStab = sts::registry::kBookOfStabbingMoveStab;         // 1
constexpr uint8_t kBigStab = sts::registry::kBookOfStabbingMoveBigStab;   // 2

// STAB authors exactly ONE template step (monsters.yaml id 39); the loop below
// repeats it stabCount times. Named rather than a bare 0 so the coupling to the
// row is visible from here (the Healer's precedent).
constexpr uint8_t kStabTemplateStep = 0;

// PainfulStabsPower's explicitly assigned amount (PainfulStabsPower.java:29).
// Distinct from Minion, which never assigns one and inherits the same -1; both
// take op_apply_power's `amount == -1` non-stacking path.
constexpr int32_t kPainfulStabsAppliedAmount = -1;

// `++this.stabCount` with the byte's ceiling made explicit. See header note (3):
// 255 is not reachable in play, and a silent wrap would be far worse than a
// stuck maximum.
void increment_stab_count(MonsterState& m) noexcept {
    if (m.pad0 < 255u) {
        m.pad0 = static_cast<uint8_t>(m.pad0 + 1);
    }
}

}  // namespace

void book_of_stabbing_decide_move(CombatState& s, uint8_t mi, int32_t num,
                                  int32_t ascension) noexcept {
    // getMove (:129-150), all four paths, with the A18 gate spelled on both
    // sides -- writing only the live arm would hide that the increment is what
    // the ascension changes, not the move choice. `ascension` is a parameter
    // rather than the kMonsterAscension constant so the tier-2 suite can drive
    // the sub-A18 arms (the Healer's shape).
    MonsterState& m = s.monsters[mi];
    if (num < 15) {
        if (last_move_is(m, kBigStab)) {
            increment_stab_count(m);  // ++stabCount BEFORE the setMove (:133)
            set_monster_move(m, kStab, MonsterIntent::ATTACK);
            return;
        }
        set_monster_move(m, kBigStab, MonsterIntent::ATTACK);
        if (ascension >= 18) {
            increment_stab_count(m);  // (:137-139) -- AFTER the setMove
        }
        return;
    }
    if (last_two_moves_are(m, kStab)) {
        set_monster_move(m, kBigStab, MonsterIntent::ATTACK);
        if (ascension >= 18) {
            increment_stab_count(m);  // (:143-145)
        }
        return;
    }
    increment_stab_count(m);  // (:147)
    set_monster_move(m, kStab, MonsterIntent::ATTACK);
}

void book_of_stabbing_init(CombatState& s, uint8_t mi) noexcept {
    // The ctor's `super(...)` HP argument is a LITERAL 164 -- no draw -- and the
    // setHp chain (:62-66) is the ONE monster_hp_rng draw, over the a8 column at
    // the fixed A20.
    MonsterState& m = s.monsters[mi];
    m = MonsterState{};
    m.monster_id = static_cast<uint16_t>(MonsterId::BOOK_OF_STABBING);
    const int32_t hp =
        random(s.monster_hp_rng,
               sts::registry::kBookOfStabbing.hp_min(kMonsterAscension),
               sts::registry::kBookOfStabbing.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    m.pad0 = kBookOfStabbingStartStabCount;  // stabCount = 1 (:50)
    // init() -> rollMove -> getMove(aiRng.random(99)). getMove READS num (the
    // < 15 test) AND WRITES stabCount, so at A20 this first decision already
    // takes the counter to 2 whichever arm it lands in -- header note (1).
    book_of_stabbing_decide_move(s, mi, random(s.ai_rng, 99), kMonsterAscension);
}

void book_of_stabbing_roll_move(CombatState& s, uint8_t mi) noexcept {
    book_of_stabbing_decide_move(s, mi, random(s.ai_rng, 99), kMonsterAscension);
}

void book_of_stabbing_use_pre_battle_action(CombatState& s,
                                            uint8_t mi) noexcept {
    // usePreBattleAction (:78-81): addToBottom ApplyPowerAction(this, this,
    // new PainfulStabsPower(this)) -- the 3-arg ctor, so the applied stack is
    // powerToApply.amount, which PainfulStabsPower sets to -1 (:29). No RNG.
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = mi;
    apply.tgt = mi;
    apply.amount = kPainfulStabsAppliedAmount;
    apply.flags = make_apply_power_flags(PowerId::PAINFUL_STABS);
    add_to_bottom(s, apply);
}

void book_of_stabbing_take_turn(CombatState& s, uint8_t mi) noexcept {
    const MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];
    if (move == kStab) {
        // case STAB (:86-93): ChangeState + Wait (presentation), then a loop of
        // `stabCount` SFXAction/DamageAction pairs. The SFXAction's
        // MathUtils.random(0, 3) (:90) is UNSEEDED and costs nothing. Each
        // repetition is a SEPARATE DamageAction, so block, per-hit powers and
        // Painful Stabs' own per-hit Wound all apply per stab.
        //
        // The count is read HERE, from pad0, and the trailing ROLL_MOVE that can
        // change it is queued behind every one of these -- header note (2).
        const uint8_t hits = m.pad0;
        for (uint8_t i = 0; i < hits; ++i) {
            queue_monster_move_effect(s, mi, sts::registry::kBookOfStabbing,
                                      kStab, kStabTemplateStep,
                                      kMoveTargetFromStep);
        }
    } else {
        // case BIG_STAB (:95-100): one DamageAction on damage.get(1). The
        // registry program, unchanged.
        queue_monster_move_effects(s, mi, sts::registry::kBookOfStabbing,
                                   kBigStab);
    }
    // The RollMoveAction at :102 sits OUTSIDE the switch, so both cases reach it.
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
