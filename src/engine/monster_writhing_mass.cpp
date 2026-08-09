// The Writhing Mass: native (recursive) move selection, the two-power opener and
// the in-combat master-deck write. See monster_writhing_mass.hpp for provenance,
// the move-id-0 argument and the draw accounting.

#include "sts/engine/monster_writhing_mass.hpp"

#include <cassert>

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, move-history helpers, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kBigHit = sts::registry::kWrithingMassMoveBigHit;            // 0
constexpr uint8_t kMultiHit = sts::registry::kWrithingMassMoveMultiHit;        // 1
constexpr uint8_t kAttackBlock = sts::registry::kWrithingMassMoveAttackBlock;  // 2
constexpr uint8_t kAttackDebuff = sts::registry::kWrithingMassMoveAttackDebuff;// 3
constexpr uint8_t kMegaDebuff = sts::registry::kWrithingMassMoveMegaDebuff;    // 4

// getMove (WrithingMass.java:145-194), transcribed with its recursion intact.
//
// `depth` exists only for the assert (monster_writhing_mass.hpp): it is not a
// cap, and no branch consults it, so the decision this function makes is
// identical with or without it.
//
// EVERY RE-ENTRY DRAWS FIRST. `getMove(aiRng.random(a, b))` evaluates its
// argument before the call, so the draw is charged to the CALLING frame; the
// order of draws is therefore exactly the Java's, tiebreak-then-num where both
// happen.
//
// createIntent() (:193) is called at the tail of every non-firstMove return,
// INCLUDING once per unwinding recursive frame. It repaints the telegraph from
// the already-decided move, so every frame writes the same values and the end
// state is identical; the engine's intent field is written by set_monster_move at
// decision time and nothing here needs to repeat it.
void writhing_mass_get_move(CombatState& s, MonsterState& m, int32_t num,
                            int32_t depth) noexcept {
    assert(depth < kWrithingMassMaxGetMoveDepth &&
           "WrithingMass.getMove recursion ran away -- see "
           "kWrithingMassMaxGetMoveDepth");
    if (depth >= kWrithingMassMaxGetMoveDepth) {
        // Release-build backstop for the assert above. Unreachable in practice;
        // picking the arm the tree falls back to most often keeps a corrupted
        // build in the space of legal moves rather than in an undecided one.
        set_monster_move(m, kAttackBlock, MonsterIntent::ATTACK_DEFEND);
        return;
    }
    // The FIRST decision (:146-157). `firstMove = false` is set SYNCHRONOUSLY at
    // the top, so a recursive re-entry from this branch is impossible -- and in
    // fact this branch never recurses: it returns unconditionally, which is what
    // guarantees the main tree below always runs against a NON-EMPTY move
    // history (the move-id-0 argument).
    //
    // It is also the ONLY return path that does not call createIntent (:156).
    // Presentation-equivalent: setMove already wrote the intent.
    if ((m.pad0 & kWrithingMassPadFirstMove) != 0u) {
        m.pad0 = static_cast<uint8_t>(m.pad0 & ~kWrithingMassPadFirstMove);
        if (num < 33) {
            set_monster_move(m, kMultiHit, MonsterIntent::ATTACK);
        } else if (num < 66) {
            set_monster_move(m, kAttackBlock, MonsterIntent::ATTACK_DEFEND);
        } else {
            set_monster_move(m, kAttackDebuff, MonsterIntent::ATTACK_DEBUFF);
        }
        return;
    }
    if (num < 10) {
        // (:158-163). The ONLY reader of lastMove(BIG_HIT) -- i.e. the only place
        // the id-0/empty-slot ambiguity could ever bite -- and it is reachable
        // only from here, past the firstMove return above.
        if (!last_move_is(m, kBigHit)) {
            set_monster_move(m, kBigHit, MonsterIntent::ATTACK);
        } else {
            writhing_mass_get_move(s, m, random(s.ai_rng, 10, 99), depth + 1);
        }
    } else if (num < 20) {
        // (:164-171). The Parasite gate. `usedMegaDebuff` is set in takeTurn, not
        // here, so a MEGA_DEBUFF that has been DECIDED but not yet RESOLVED is
        // still blocked -- by the lastMove(4) half of the same condition.
        if ((m.pad0 & kWrithingMassPadUsedMegaDebuff) == 0u &&
            !last_move_is(m, kMegaDebuff)) {
            set_monster_move(m, kMegaDebuff, MonsterIntent::STRONG_DEBUFF);
        } else if (random_boolean(s.ai_rng, 0.1f)) {
            set_monster_move(m, kBigHit, MonsterIntent::ATTACK);
        } else {
            writhing_mass_get_move(s, m, random(s.ai_rng, 20, 99), depth + 1);
        }
    } else if (num < 40) {
        // (:172-179). The one arm that can spend TWO draws before re-entering:
        // the randomBoolean, then the new num. And random(19) WIDENS the range
        // back below 20, so this is the arm that makes termination probabilistic
        // rather than structural.
        if (!last_move_is(m, kAttackDebuff)) {
            set_monster_move(m, kAttackDebuff, MonsterIntent::ATTACK_DEBUFF);
        } else if (random_boolean(s.ai_rng, 0.4f)) {
            writhing_mass_get_move(s, m, random(s.ai_rng, 19), depth + 1);
        } else {
            writhing_mass_get_move(s, m, random(s.ai_rng, 40, 99), depth + 1);
        }
    } else if (num < 70) {
        // (:180-187).
        if (!last_move_is(m, kMultiHit)) {
            set_monster_move(m, kMultiHit, MonsterIntent::ATTACK);
        } else if (random_boolean(s.ai_rng, 0.3f)) {
            set_monster_move(m, kAttackBlock, MonsterIntent::ATTACK_DEFEND);
        } else {
            writhing_mass_get_move(s, m, random(s.ai_rng, 39), depth + 1);
        }
    } else if (!last_move_is(m, kAttackBlock)) {
        // (:188-192). The Java flattens the `num >= 70` test into the else-if
        // chain; reaching here IS that test.
        set_monster_move(m, kAttackBlock, MonsterIntent::ATTACK_DEFEND);
    } else {
        writhing_mass_get_move(s, m, random(s.ai_rng, 69), depth + 1);
    }
}

}  // namespace

void writhing_mass_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::WRITHING_MASS);
    // setHp(175) / setHp(160) -- the ONE-ARG overload (WrithingMass.java:60-64),
    // which is `setHp(hp, hp)` (AbstractMonster.java:777-779) and therefore
    // STILL ONE monsterHpRng draw over a degenerate range. The Hexaghost shape;
    // do NOT carry the Transient's or the Maw's skip-the-draw reasoning over.
    const int32_t hp = random(
        s.monster_hp_rng, sts::registry::kWrithingMass.hp_min(kMonsterAscension),
        sts::registry::kWrithingMass.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    // `firstMove = true` (:44), `usedMegaDebuff = false` (:45).
    m.pad0 = kWrithingMassPadFirstMove;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). The value IS
    // read -- the firstMove branch splits it into thirds -- so the opening
    // telegraph is seed-dependent, and it consumes the firstMove latch.
    writhing_mass_get_move(s, m, random(s.ai_rng, 99), /*depth=*/0);
}

void writhing_mass_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (WrithingMass.java:80-83), TWO addToBottom actions IN
    // THIS ORDER -- Reactive FIRST, Malleable SECOND. The order is the
    // power-list application order, which is the order every hook fan-out walks,
    // so on a single hit the re-roll is queued before the block. Neither draws
    // RNG.
    ActionQueueItem reactive{};
    reactive.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    reactive.src = mi;
    reactive.tgt = mi;
    reactive.amount = kReactiveNoAmount;
    reactive.flags = make_apply_power_flags(PowerId::REACTIVE);
    add_to_bottom(s, reactive);

    ActionQueueItem malleable{};
    malleable.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    malleable.src = mi;
    malleable.tgt = mi;
    // The 1-ARG MalleablePower ctor: amount 3, and basePower 3 alongside it --
    // op_apply_power's new-slot path copies the amount into PowerSlot.counter for
    // this PowerId (power_malleable.hpp), so the item carries no counter operand.
    malleable.amount = kWrithingMassMalleableAmount;
    malleable.flags = make_apply_power_flags(PowerId::MALLEABLE);
    add_to_bottom(s, malleable);
}

void writhing_mass_roll_move(CombatState& s, uint8_t mi) noexcept {
    const int32_t num = random(s.ai_rng, 99);
    writhing_mass_get_move(s, s.monsters[mi], num, /*depth=*/0);
}

void writhing_mass_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (WrithingMass.java:86-123). The ChangeState / Wait /
    // AnimateSlow-/FastAttack / FastShake around the real steps are presentation
    // and draw no seeded RNG. The RollMoveAction at :122 sits OUTSIDE the switch,
    // so all five cases reach it -- and unlike every other monster in this batch
    // it is a QUEUED roll rather than an inline one, because Reactive can queue
    // more of them mid-card and they must all resolve in queue order.
    MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];
    if (move == kMegaDebuff) {
        // `this.usedMegaDebuff = true;` (:116) -- SYNCHRONOUS, set before the
        // OBTAIN_CARD is even queued, so a Reactive re-roll landing later in the
        // SAME player card already sees the gate closed. That is what makes the
        // Parasite exactly-once-per-combat rather than once-per-resolution.
        m.pad0 = static_cast<uint8_t>(m.pad0 | kWrithingMassPadUsedMegaDebuff);
    }
    // Every case's real content is the row's program, MEGA_DEBUFF's OBTAIN_CARD
    // included: the CardId rides in the step's `extra` and op_obtain_card accrues
    // it into CombatState.pending_obtain, which the run layer drains through
    // add_card_to_master_deck each pump step.
    queue_monster_move_effects(s, mi, sts::registry::kWrithingMass, move);

    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
