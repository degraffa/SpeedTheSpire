// The Mugger: native state-machine move selection, seeded sound draws, gold-steal
// accounting and the Act-2 escape. See monster_mugger.hpp for provenance, the
// draw accounting, and every place this diverges from the Looter it looks like.

#include "sts/engine/monster_mugger.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, set_monster_move, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kMug = sts::registry::kMuggerMoveMug;              // 1
constexpr uint8_t kSmokeBomb = sts::registry::kMuggerMoveSmokeBomb;  // 2
constexpr uint8_t kEscape = sts::registry::kMuggerMoveEscape;        // 3
constexpr uint8_t kBigSwipe = sts::registry::kMuggerMoveBigswipe;    // 4

// slashCount saturates at its machine maximum (Mug, Mug, Big Swipe == 3); the
// Java increments unboundedly but only the == 1 and == 2 comparisons are ever
// read (:91,:98), and saturation keeps the steal count honest if a hand-built
// state loops moves. The Looter's bump_slash_count, verbatim -- kept as a
// private copy rather than shared, because the two classes' counters are
// separate fields whose ONLY commonality is the number 3.
void bump_slash_count(MonsterState& m) noexcept {
    if (m.pad0 < 3) {
        m.pad0 = static_cast<uint8_t>(m.pad0 + 1);
    }
}

void queue_set_move(CombatState& s, uint8_t mi, uint8_t move,
                    MonsterIntent intent) noexcept {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::SET_MOVE);
    it.src = mi;
    it.tgt = mi;
    it.amount = move;
    it.flags = static_cast<uint32_t>(intent);
    add_to_bottom(s, it);
}

// playSfx (Mugger.java:138-145): `int roll = AbstractDungeon.aiRng.random(2);`
// then one of two SFXActions. The BRANCH is presentation; the DRAW is state, and
// it is SEEDED -- the single sharpest divergence from the Looter, whose playSfx
// rolls unseeded MathUtils (Looter.java:137-143). random(2) is inclusive 0..2
// (stage-a trap 3), so it is a 3-way draw feeding a 2-way branch; the third
// outcome simply shares the second's sound. Reproduced as the draw it is.
void play_sfx(CombatState& s) noexcept {
    (void)random(s.ai_rng, 2);
}

}  // namespace

void mugger_init(CombatState& s, uint8_t mi) noexcept {
    // The ctor is `super(...)` + setHp(min, max): exactly one monster_hp_rng
    // inclusive draw, currentHealth == maxHealth (AbstractMonster.java:765-775);
    // at A20 the A7 column (50, 54) is the live one (Mugger.java:62-66).
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::MUGGER);
    const int32_t hp =
        random(s.monster_hp_rng, sts::registry::kMugger.hp_min(kMonsterAscension),
               sts::registry::kMugger.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;  // slashCount / steal count (Mugger.java:54)
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). The draw
    // still happens; getMove (Mugger.java:167-170) then IGNORES num and forces
    // the opening Mug -- the Looter / Red Slaver / Guardian discarded-draw
    // precedent.
    (void)random(s.ai_rng, 99);
    set_monster_move(m, kMug, MonsterIntent::ATTACK);
}

void mugger_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (Mugger.java:81-84): addToBottom ApplyPowerAction(this,
    // this, new ThieveryPower(this, goldAmt)). A pure marker power -- the steal
    // rides the attacks, not a hook on this row. No RNG draw.
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = mi;
    apply.tgt = mi;
    apply.amount = kMuggerGoldAmt;
    apply.flags = make_apply_power_flags(PowerId::THIEVERY);
    add_to_bottom(s, apply);
}

void mugger_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (Mugger.java:86-136). No trailing RollMoveAction -- every case
    // decides the next move itself, so ai_rng moves only at the playSfx draws and
    // the two randomBoolean sites below.
    MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];
    switch (move) {
        case kMug: {
            // (:89-108). playSfx runs FIRST, before the talk gate -- so the
            // random(2) precedes the 0.6 coin on the stream (:90 then :91).
            play_sfx(s);
            // THE TALK GATE IS ON THE SECOND MUG. `slashCount == 1 &&
            // aiRng.randomBoolean(0.6f)` (:91): pad0 is still the count BEFORE
            // this attack, so == 1 means one Mug has already landed. Java's &&
            // short-circuits, so the coin is drawn on that Mug ALONE -- not the
            // first, which is where the Looter's identical-looking gate sits
            // (Looter.java:92, `slashCount == 0`). The TalkAction it gates is
            // presentation; the DRAW is what matters.
            if (m.pad0 == 1) {
                (void)random_boolean(s.ai_rng, 0.6f);
            }
            // The queued stolen-gold accrual (Mugger$1, site :95) + the 3-arg
            // DamageAction (:96). The accrual is the pad0 bump here: nothing
            // between its queue slot and the damage can observe stolenGold or
            // change the amount, because both read the same purse at the same
            // instant (monster_mugger.hpp note 2). The damage is the registry
            // program. NOTE the Java bumps slashCount AFTER queueing (:97) --
            // the bump is hoisted above the queue call here because the queue
            // call cannot observe it, and hoisting keeps the "steal happened"
            // write adjacent to the accrual it models.
            bump_slash_count(m);
            queue_monster_move_effects(s, mi, sts::registry::kMugger, kMug);
            if (m.pad0 == 2) {
                // (:98-105): the 50/50 -- true means Smoke Bomb next (a
                // SYNCHRONOUS setMove, :100), false means Big Swipe next (a
                // QUEUED SetMoveAction, :103). Both land before the next turn
                // reads the move; the sync/queued split is kept because it is
                // what the Java does and the queued form costs nothing.
                if (random_boolean(s.ai_rng, 0.5f)) {
                    set_monster_move(m, kSmokeBomb, MonsterIntent::DEFEND);
                } else {
                    queue_set_move(s, mi, kBigSwipe, MonsterIntent::ATTACK);
                }
            } else {
                // (:106): the first Mug re-telegraphs Mug (queued SetMoveAction).
                queue_set_move(s, mi, kMug, MonsterIntent::ATTACK);
            }
            return;
        }
        case kBigSwipe:
            // (:109-117): playSfx (another seeded random(2), :110), then
            // ++slashCount (:111 -- BEFORE the queueing this time, an ordering
            // nothing can observe), the accrual + 3-arg DamageAction on
            // damage.get(1) (:113-114), then a SYNCHRONOUS setMove(SMOKE_BOMB,
            // DEFEND) (:115). No randomBoolean anywhere in this case.
            play_sfx(s);
            bump_slash_count(m);
            queue_monster_move_effects(s, mi, sts::registry::kMugger, kBigSwipe);
            set_monster_move(m, kSmokeBomb, MonsterIntent::DEFEND);
            return;
        case kSmokeBomb:
            // (:118-126): GainBlockAction(this, this, escapeDef [+6 at A17]) --
            // the registry program -- then the queued SetMoveAction(3,
            // Intent.ESCAPE) (:124). NO TalkAction (the Looter's sibling case has
            // one) and NO RNG at all.
            queue_monster_move_effects(s, mi, sts::registry::kMugger, kSmokeBomb);
            queue_set_move(s, mi, kEscape, MonsterIntent::ESCAPE);
            return;
        case kEscape:
            // (:127-134): room.mugged is set SYNCHRONOUSLY (:129), before the
            // queued EscapeAction resolves; then EscapeAction(this) (:131) and
            // the re-telegraphing SetMoveAction(3, Intent.ESCAPE) (:132), which
            // still resolves on an escaping monster (SetMoveAction has no
            // liveness check) -- and which is also what keeps this record
            // invisible to a Centurion's Protect (op_block_random_monster reads
            // the TELEGRAPH). The Talk/VFX either side roll nothing.
            s.flags |= kCombatFlagMugged;
            {
                ActionQueueItem esc{};
                esc.opcode = static_cast<uint16_t>(Opcode::ESCAPE);
                esc.src = mi;
                esc.tgt = mi;
                add_to_bottom(s, esc);
            }
            queue_set_move(s, mi, kEscape, MonsterIntent::ESCAPE);
            return;
        default:
            return;  // no decided move (defensive; init always telegraphs Mug)
    }
}

void mugger_die(CombatState& s, uint8_t /*mi*/) noexcept {
    // die() (Mugger.java:156-165) runs playDeathSfx FIRST, and playDeathSfx
    // (:147-154) is `int roll = AbstractDungeon.aiRng.random(2);` -- SEEDED. One
    // draw, unconditionally, on every Mugger death. The shake and time-scale
    // lines are animation; the stolen-gold return (:161-163) is the reward
    // layer's read of this record, not a combat-time write; and `super.die()`
    // (:164) is the power/relic fan-out the death edge already dispatches right
    // after this.
    (void)random(s.ai_rng, 2);
}

}  // namespace sts::engine
