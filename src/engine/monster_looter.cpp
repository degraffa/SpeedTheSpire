// The Looter: native state-machine move selection, gold-steal accounting, and
// the first reachable Act-1 escape. See monster_looter.hpp for provenance, the
// draw accounting, and why pad0 (slashCount) doubles as the steal count.

#include "sts/engine/monster_looter.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, set_monster_move, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kMug = sts::registry::kLooterMoveMug;              // 1
constexpr uint8_t kSmokeBomb = sts::registry::kLooterMoveSmokeBomb;  // 2
constexpr uint8_t kEscape = sts::registry::kLooterMoveEscape;        // 3
constexpr uint8_t kLunge = sts::registry::kLooterMoveLunge;          // 4

// slashCount saturates at its machine maximum (Mug, Mug, Lunge == 3); the Java
// increments unboundedly but only the == 2 comparison is ever read (:100), and
// saturation keeps looter_stolen_gold honest if a hand-built state loops moves.
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

}  // namespace

void looter_init(CombatState& s, uint8_t mi) noexcept {
    // The ctor is `super(...)` + setHp(min, max): exactly one monster_hp_rng
    // inclusive draw, currentHealth == maxHealth (AbstractMonster.java:765-775);
    // at A20 the A7 column (46, 50) is the live one (Looter.java:65).
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::LOOTER);
    const int32_t hp =
        random(s.monster_hp_rng, sts::registry::kLooter.hp_min(kMonsterAscension),
               sts::registry::kLooter.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;  // slashCount / steal count (Looter.java:56-57)
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). The draw
    // still happens; getMove (Looter.java:176-179) then IGNORES num and forces
    // the opening Mug -- the Red Slaver / Guardian discarded-draw precedent.
    (void)random(s.ai_rng, 99);
    set_monster_move(m, kMug, MonsterIntent::ATTACK);
}

void looter_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (Looter.java:84-86): addToBottom ApplyPowerAction(this,
    // this, new ThieveryPower(this, goldAmt)). A pure marker power -- the steal
    // rides the attacks, not a hook on this row. No RNG draw.
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = mi;
    apply.tgt = mi;
    apply.amount = kLooterGoldAmt;
    apply.flags = make_apply_power_flags(PowerId::THIEVERY);
    add_to_bottom(s, apply);
}

void looter_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (Looter.java:88-135). No trailing RollMoveAction -- every case
    // decides the next move itself, so ai_rng moves ONLY at the two
    // randomBoolean sites below.
    MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];
    switch (move) {
        case kMug: {
            // (:91-110). The first-Mug talk gate rolls BEFORE the attack is
            // queued: `slashCount == 0 && aiRng.randomBoolean(0.6f)` -- Java's
            // && short-circuits, so later Mugs draw NOTHING here. The
            // TalkAction it gates is presentation; the DRAW is what matters.
            if (m.pad0 == 0) {
                (void)random_boolean(s.ai_rng, 0.6f);
            }
            // The anonymous stolen-gold accrual action (:97) + the 3-arg
            // DamageAction (:98). The accrual is synchronous here (pad0 is the
            // steal count): nothing between its queue slot and the damage can
            // observe stolenGold or change the amount -- see note (2) in the
            // header. The damage itself is the registry program.
            bump_slash_count(m);
            queue_monster_move_effects(s, mi, sts::registry::kLooter, kMug);
            if (m.pad0 == 2) {
                // (:100-107): the 50/50 -- true means Smoke Bomb next (a
                // SYNCHRONOUS setMove, :102), false means Lunge next (a QUEUED
                // SetMoveAction, :105). Both land before the next turn reads
                // the move; the sync/queued split is kept because it is what
                // the Java does and the queued form costs nothing.
                if (random_boolean(s.ai_rng, 0.5f)) {
                    set_monster_move(m, kSmokeBomb, MonsterIntent::DEFEND);
                } else {
                    queue_set_move(s, mi, kLunge, MonsterIntent::ATTACK);
                }
            } else {
                // (:108): first Mug re-telegraphs Mug (queued SetMoveAction).
                queue_set_move(s, mi, kMug, MonsterIntent::ATTACK);
            }
            return;
        }
        case kLunge:
            // (:111-118): ++slashCount (:113 -- BEFORE the queueing this time,
            // an ordering nothing can observe), the accrual + 3-arg
            // DamageAction on damage.get(1), then a SYNCHRONOUS
            // setMove(SMOKE_BOMB, DEFEND) (:117). No aiRng draw.
            bump_slash_count(m);
            queue_monster_move_effects(s, mi, sts::registry::kLooter, kLunge);
            set_monster_move(m, kSmokeBomb, MonsterIntent::DEFEND);
            return;
        case kSmokeBomb:
            // (:120-125): Talk (presentation), GainBlockAction(this, this, 6)
            // -- the registry program -- then the queued SetMoveAction(3,
            // Intent.ESCAPE) (:123).
            queue_monster_move_effects(s, mi, sts::registry::kLooter,
                                       kSmokeBomb);
            queue_set_move(s, mi, kEscape, MonsterIntent::ESCAPE);
            return;
        case kEscape:
            // (:126-133): room.mugged is set SYNCHRONOUSLY (:128), before the
            // queued EscapeAction resolves; then EscapeAction(this) (:130) and
            // the re-telegraphing SetMoveAction(3, Intent.ESCAPE) (:131),
            // which still resolves on an escaping monster (SetMoveAction has
            // no liveness check). The Talk/VFX either side roll nothing.
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

}  // namespace sts::engine
