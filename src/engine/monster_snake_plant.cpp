// The Snake Plant: native move selection and turn body, plus the Malleable
// opener. See monster_snake_plant.hpp for provenance, the two-arm getMove and
// the draw accounting.

#include "sts/engine/monster_snake_plant.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, move-history helpers, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kChompy = sts::registry::kSnakePlantMoveChompyChomps;  // 1
constexpr uint8_t kSpores = sts::registry::kSnakePlantMoveSpores;        // 2

// The engine's live entry point: decide at the fixed ascension.
void snake_plant_get_move(MonsterState& m, int32_t num) noexcept {
    snake_plant_decide_move(m, num, kMonsterAscension);
}

}  // namespace

// getMove (SnakePlant.java:121-142), BOTH arms, because writing only the live
// one would hide what the difference IS -- and because the tier-2 tests have to
// exercise the sub-A17 arm, which is why `ascension` is a parameter rather than
// the kMonsterAscension constant read inline.
//
// Note the two arms share the num < 65 branch VERBATIM (:123-129 and :134-140 in
// the Java are the same three lines twice) -- the decompiler's flattening of one
// `if (asc >= 17)` around a duplicated body. Only the num >= 65 branch differs,
// and only by the added `|| lastMoveBefore(SPORES)` disjunct. `num` is the
// aiRng.random(99) the caller already drew.
void snake_plant_decide_move(MonsterState& m, int32_t num,
                             int32_t ascension) noexcept {
    if (num < 65) {
        // Two Chompies in a row force Spores; otherwise Chompy (telegraphed
        // `3, true` -- the multi-hit marker; the three hits themselves are the
        // registry program's three DAMAGE steps).
        if (last_two_moves_are(m, kChompy)) {
            set_monster_move(m, kSpores, MonsterIntent::STRONG_DEBUFF);
        } else {
            set_monster_move(m, kChompy, MonsterIntent::ATTACK);
        }
        return;
    }
    // num >= 65. At A17+ the plant refuses Spores if Spores was EITHER of the
    // last two decisions (:131); below A17 only the most recent one counts
    // (:141). lastMoveBefore, not lastTwoMoves: the question is about the SECOND
    // ring slot alone, not about both slots agreeing.
    const bool recent_spores =
        ascension >= 17
            ? (last_move_is(m, kSpores) || last_move_before_is(m, kSpores))
            : last_move_is(m, kSpores);
    if (recent_spores) {
        set_monster_move(m, kChompy, MonsterIntent::ATTACK);
    } else {
        set_monster_move(m, kSpores, MonsterIntent::STRONG_DEBUFF);
    }
}

void snake_plant_init(CombatState& s, uint8_t mi) noexcept {
    // The ctor is `super(...)` + setHp(min, max): exactly one monster_hp_rng
    // inclusive draw, currentHealth == maxHealth (AbstractMonster.java:765-775);
    // at A20 the A7 column (78, 82) is the live one (SnakePlant.java:59-63).
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::SNAKE_PLANT);
    const int32_t hp = random(
        s.monster_hp_rng, sts::registry::kSnakePlant.hp_min(kMonsterAscension),
        sts::registry::kSnakePlant.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;  // unused by this monster
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). Unlike the
    // Chosen / Looter / Guardian, this getMove READS num on the first call --
    // there is no forced opener -- so the plant's turn-1 move is seed-dependent
    // and an empty move history sends both history predicates false.
    snake_plant_get_move(m, random(s.ai_rng, 99));
}

void snake_plant_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (SnakePlant.java:69-72): addToBottom ApplyPowerAction(
    // this, this, new MalleablePower(this)). The 1-ARG ctor, so amount 3
    // (MalleablePower.java:22,24-26), and basePower 3 with it -- op_apply_power's
    // new-slot path copies the amount into PowerSlot.counter for this PowerId,
    // which is where basePower lives (power_malleable.hpp). No RNG draw.
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = mi;
    apply.tgt = mi;
    apply.amount = kSnakePlantMalleableAmount;
    apply.flags = make_apply_power_flags(PowerId::MALLEABLE);
    add_to_bottom(s, apply);
}

void snake_plant_roll_move(CombatState& s, uint8_t mi) noexcept {
    const int32_t num = random(s.ai_rng, 99);
    snake_plant_get_move(s.monsters[mi], num);
}

void snake_plant_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (SnakePlant.java:94-118). Both cases are presentation plus the
    // registry program -- Chompy's ChangeStateAction("ATTACK") / WaitAction /
    // per-hit BiteEffect VFX and Spores' bare pair of ApplyPowerActions -- and
    // none of the presentation draws seeded RNG (the BiteEffect's position
    // jitter is unseeded MathUtils). The RollMoveAction at :114 sits OUTSIDE the
    // switch, so both cases reach it.
    queue_monster_move_effects(s, mi, sts::registry::kSnakePlant,
                               s.monsters[mi].move_history[0]);
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
