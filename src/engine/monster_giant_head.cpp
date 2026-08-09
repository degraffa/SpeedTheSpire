// The Giant Head: the countdown, the pre-battle Slow, and the IT_IS_TIME ramp.
// See monster_giant_head.hpp for provenance and the five readings this body
// leans on.

#include "sts/engine/monster_giant_head.hpp"

#include <cassert>

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // move helpers, queue_monster_move_effects
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kGlare = sts::registry::kGiantHeadMoveGlare;          // 1
constexpr uint8_t kItIsTime = sts::registry::kGiantHeadMoveItIsTime;    // 2
constexpr uint8_t kCount = sts::registry::kGiantHeadMoveCount;          // 3

// IT_IS_TIME's single authored step (monsters.yaml id 58): the tiered
// `startingDeathDmg` (30, 40 from A3), which IS damage.get(1), i.e. the ramp at
// count == 0. Named rather than a bare index so the coupling to the row is
// visible from here (the Gremlin Leader's kEncourage*Step precedent).
constexpr uint8_t kItIsTimeDamageStep = 0;

void queue_roll_move(CombatState& s, uint8_t mi) noexcept {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    it.src = mi;
    it.tgt = mi;
    add_to_bottom(s, it);
}

}  // namespace

int32_t giant_head_it_is_time_damage(int32_t count, int32_t ascension) noexcept {
    // `index = 1 - count; if (index > 7) index = 7;` then damage.get(index),
    // whose value is startingDeathDmg + (index - 1) * 5 (GiantHead.java:71-77,
    // :106-110). Written in the index domain rather than as `sdd - count * 5`
    // BECAUSE OF THE CLAMP: the two agree for every count the getMove floor can
    // produce, and disagree the moment something drives count below -6. The
    // clamp is in the Java, so it is here.
    int32_t index = 1 - count;
    if (index > kGiantHeadMaxDamageIndex) {
        index = kGiantHeadMaxDamageIndex;
    }
    if (index < 1) {
        index = 1;  // count >= 1 never reaches IT_IS_TIME; defensive floor
    }
    const sts::registry::MonsterMove* mv =
        sts::registry::kGiantHead.move(kItIsTime);
    assert(mv != nullptr && mv->effect_count > kItIsTimeDamageStep);
    const int32_t starting =
        mv->effects[kItIsTimeDamageStep].amount.at(ascension);
    return starting + (index - 1) * 5;
}

void giant_head_decide_move(CombatState& s, uint8_t mi, int32_t num,
                            int32_t ascension) noexcept {
    MonsterState& m = s.monsters[mi];
    int32_t count = giant_head_count(m);
    // getMove (:153-174). Every path WRITES count; the two arms differ in where
    // the write sits relative to the decision, and that is reproduced literally.
    if (count <= 1) {
        // (:155-160). The floor, then a forced IT_IS_TIME whose telegraphed
        // amount is a function of the count AFTER the decrement.
        if (count > kGiantHeadMinCount) {
            --count;
        }
        giant_head_set_count(m, count);
        set_monster_move(m, kItIsTime, MonsterIntent::ATTACK);
        // The telegraph amount (`startingDeathDmg - count * 5`, :159) is not
        // stored anywhere -- MonsterState has no telegraphed-amount field and
        // takeTurn recomputes it from the same count -- so it is derived, not
        // remembered. Asserted here so the two derivations stay one derivation.
        (void)giant_head_it_is_time_damage(count, ascension);
        return;
    }
    // (:162-173). The decrement happens BEFORE `num` is read, so a Glare/Count
    // decision still spends a countdown turn.
    --count;
    giant_head_set_count(m, count);
    if (num < 50) {
        if (!last_two_moves_are(m, kGlare)) {
            set_monster_move(m, kGlare, MonsterIntent::DEBUFF);
        } else {
            set_monster_move(m, kCount, MonsterIntent::ATTACK);
        }
    } else if (!last_two_moves_are(m, kCount)) {
        set_monster_move(m, kCount, MonsterIntent::ATTACK);
    } else {
        set_monster_move(m, kGlare, MonsterIntent::DEBUFF);
    }
}

void giant_head_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m = MonsterState{};
    m.monster_id = static_cast<uint16_t>(MonsterId::GIANT_HEAD);
    // The ctor's `super(...)` HP argument is the LITERAL 500 -- no draw -- and
    // the setHp chain (:64-68) is the ONE monster_hp_rng draw, over a DEGENERATE
    // range at both tiers. Random.random(int,int) still increments the counter
    // for min == max (Random.java:58-61), so the draw is taken.
    const int32_t hp = random(s.monster_hp_rng,
                              sts::registry::kGiantHead.hp_min(kMonsterAscension),
                              sts::registry::kGiantHead.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    // count = 5, the FIELD INITIALIZER (:53) -- set BEFORE the first rollMove,
    // and NOT yet touched by the A18 pre-battle decrement (header note (2)).
    giant_head_set_count(m, kGiantHeadStartCount);
    // init() -> rollMove -> getMove(aiRng.random(99)). At count 5 the ordinary
    // arm decides, so the opening move is Glare below 50 and Count at or above
    // it, both history guards vacuously true.
    giant_head_decide_move(s, mi, random(s.ai_rng, 99), kMonsterAscension);
}

void giant_head_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (:80-86), in the Java's order:
    //     addToBottom(new ApplyPowerAction(this, this, new SlowPower(this, 0)));
    //     if (ascensionLevel >= 18) --this.count;
    ActionQueueItem slow{};
    slow.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    slow.src = mi;  // ApplyPowerAction(this, this, ...) -- source IS the target
    slow.tgt = mi;
    // AMOUNT ZERO, verbatim (:82). op_apply_power's new-slot path stores it, and
    // a live slot at 0 is what the player's first card stacks onto. Not a
    // missing argument and not a no-op application -- see power_slow.hpp.
    slow.amount = 0;
    slow.flags = make_apply_power_flags(PowerId::SLOW);
    add_to_bottom(s, slow);

    if (kMonsterAscension >= kGiantHeadCountAscension) {
        MonsterState& m = s.monsters[mi];
        // `--this.count` with NO floor test on this path (:84) -- the floor
        // lives only in getMove. At the fixed A20 this takes the post-init 4
        // down to 3.
        giant_head_set_count(m, giant_head_count(m) - 1);
    }
}

void giant_head_take_turn(CombatState& s, uint8_t mi) noexcept {
    const uint8_t move = s.monsters[mi].move_history[0];
    if (move == kItIsTime) {
        // case IT_IS_TIME (:103-111): ShoutAction(getTimeQuote()) -- an UNSEEDED
        // MathUtils list pick, so no draw -- then ONE DamageAction on
        // damage.get(index). The amount is per-instance (it depends on `count`),
        // so it is queued directly rather than through the table -- the
        // Darkling's NIP shape -- while the BASE still comes from the row.
        ActionQueueItem dmg{};
        dmg.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
        dmg.src = mi;
        dmg.tgt = kActorPlayer;
        dmg.amount = giant_head_it_is_time_damage(
            giant_head_count(s.monsters[mi]), kMonsterAscension);
        add_to_bottom(s, dmg);
    } else if (move == kGlare) {
        // case GLARE (:91-95): playSfx (UNSEEDED) + ShoutAction + the Weak.
        queue_monster_move_effects(s, mi, sts::registry::kGiantHead, kGlare);
    } else {
        // case COUNT (:97-101): playSfx (UNSEEDED) + ShoutAction + damage.get(0).
        queue_monster_move_effects(s, mi, sts::registry::kGiantHead, kCount);
    }
    // RollMoveAction (:113) sits OUTSIDE the switch, so every move reaches it --
    // including IT_IS_TIME, which is what keeps the ramp climbing.
    queue_roll_move(s, mi);
}

void giant_head_roll_move(CombatState& s, uint8_t mi) noexcept {
    giant_head_decide_move(s, mi, random(s.ai_rng, 99), kMonsterAscension);
}

}  // namespace sts::engine
