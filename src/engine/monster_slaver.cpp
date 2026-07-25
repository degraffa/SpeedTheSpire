// The two Act-1 slavers: native move selection + turn bodies. See
// monster_slaver.hpp for provenance, the per-stream draw accounting, and why the
// Red Slaver's two latches need only one scratch bit between them.

#include "sts/engine/monster_slaver.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, set_monster_move, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kBlueStab = sts::registry::kSlaverBlueMoveStab;        // 1
constexpr uint8_t kBlueRake = sts::registry::kSlaverBlueMoveRake;        // 4
constexpr uint8_t kRedStab = sts::registry::kSlaverRedMoveStab;          // 1
constexpr uint8_t kRedEntangle = sts::registry::kSlaverRedMoveEntangle;  // 2
constexpr uint8_t kRedScrape = sts::registry::kSlaverRedMoveScrape;      // 3

// Both slaver ctors are `super(...)` + setHp(min, max): exactly one
// monster_hp_rng inclusive draw, currentHealth == maxHealth
// (AbstractMonster.java:765-775). Neither adds a starting power.
void init_common(CombatState& s, uint8_t mi, MonsterId id,
                 const sts::registry::MonsterDef& def) noexcept {
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(id);
    const int32_t hp = random(s.monster_hp_rng, def.hp_min(kMonsterAscension),
                              def.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
}

// The trailing RollMoveAction both takeTurn bodies queue (SlaverBlue.java:89 /
// SlaverRed.java:110). Resolved by monster_roll_move_fn at dequeue time.
void queue_roll_move(CombatState& s, uint8_t mi) noexcept {
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

// SlaverBlue.getMove (SlaverBlue.java:110-129), transcribed branch for branch.
// `num` is the aiRng.random(99) the caller already drew.
void blue_get_move(MonsterState& m, int32_t num) noexcept {
    if (num >= 40 && !last_two_moves_are(m, kBlueStab)) {
        set_monster_move(m, kBlueStab, MonsterIntent::ATTACK);  // (:112-114)
        return;
    }
    if (kMonsterAscension >= 17) {
        // At A17+ Rake is gated on the LAST move only, so Stab/Rake alternate
        // strictly once the first branch stops firing (:116-122).
        if (!last_move_is(m, kBlueRake)) {
            set_monster_move(m, kBlueRake, MonsterIntent::ATTACK_DEBUFF);
            return;
        }
        set_monster_move(m, kBlueStab, MonsterIntent::ATTACK);
        return;
    }
    if (!last_two_moves_are(m, kBlueRake)) {  // (:124-127)
        set_monster_move(m, kBlueRake, MonsterIntent::ATTACK_DEBUFF);
        return;
    }
    set_monster_move(m, kBlueStab, MonsterIntent::ATTACK);  // (:128)
}

// SlaverRed.getMove BELOW the firstTurn branch (SlaverRed.java:145-165). The
// firstTurn branch (:140-143) is the init-only case and lives in slaver_red_init.
void red_get_move(MonsterState& m, int32_t num) noexcept {
    const bool used_entangle = (m.pad0 & kSlaverRedPad0UsedEntangle) != 0u;
    if (num >= 75 && !used_entangle) {
        // Entangle is a once-per-combat move: the latch is never cleared (:145-147).
        set_monster_move(m, kRedEntangle, MonsterIntent::STRONG_DEBUFF);
        return;
    }
    if (num >= 55 && used_entangle && !last_two_moves_are(m, kRedStab)) {
        set_monster_move(m, kRedStab, MonsterIntent::ATTACK);  // (:149-152)
        return;
    }
    if (kMonsterAscension >= 17) {
        if (!last_move_is(m, kRedScrape)) {  // (:153-159)
            set_monster_move(m, kRedScrape, MonsterIntent::ATTACK_DEBUFF);
            return;
        }
        set_monster_move(m, kRedStab, MonsterIntent::ATTACK);
        return;
    }
    if (!last_two_moves_are(m, kRedScrape)) {  // (:161-164)
        set_monster_move(m, kRedScrape, MonsterIntent::ATTACK_DEBUFF);
        return;
    }
    set_monster_move(m, kRedStab, MonsterIntent::ATTACK);  // (:165)
}

}  // namespace

// --- Blue Slaver -------------------------------------------------------------

void slaver_blue_init(CombatState& s, uint8_t mi) noexcept {
    init_common(s, mi, MonsterId::SLAVER_BLUE, sts::registry::kSlaverBlue);
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). With an
    // empty move history both history predicates are false, so the opener is
    // Stab at num >= 40 and Rake below it.
    const int32_t num = random(s.ai_rng, 99);
    blue_get_move(s.monsters[mi], num);
}

void slaver_blue_roll_move(CombatState& s, uint8_t mi) noexcept {
    const int32_t num = random(s.ai_rng, 99);
    blue_get_move(s.monsters[mi], num);
}

void slaver_blue_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (SlaverBlue.java:69-90): case STAB queues an AnimateSlowAttack
    // (presentation) then the DamageAction; case RAKE queues the DamageAction
    // then the ApplyPowerAction(Weak). Both are the registry program in
    // addToBottom order. The trailing RollMoveAction (:89) is queued for BOTH
    // cases -- it sits after the switch, not inside it.
    queue_monster_move_effects(s, mi, sts::registry::kSlaverBlue,
                               s.monsters[mi].move_history[0]);
    queue_roll_move(s, mi);
}

// --- Red Slaver --------------------------------------------------------------

void slaver_red_init(CombatState& s, uint8_t mi) noexcept {
    init_common(s, mi, MonsterId::SLAVER_RED, sts::registry::kSlaverRed);
    // The rollMove draw still happens; SlaverRed.getMove's firstTurn branch
    // (:140-143) then DISCARDS it and forces Stab. firstTurn is cleared by that
    // same call and can never be true again, so no latch is stored for it.
    (void)random(s.ai_rng, 99);
    set_monster_move(s.monsters[mi], kRedStab, MonsterIntent::ATTACK);
}

void slaver_red_roll_move(CombatState& s, uint8_t mi) noexcept {
    const int32_t num = random(s.ai_rng, 99);
    red_get_move(s.monsters[mi], num);
}

void slaver_red_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (SlaverRed.java:80-111). Case ENTANGLE queues the ChangeState /
    // EntangleEffect presentation and the ApplyPowerAction(Entangled), then sets
    // usedEntangle SYNCHRONOUSLY (:90) -- ahead of the queued RollMoveAction
    // below, which is exactly the call that must observe it. Cases STAB and
    // SCRAPE are their registry programs. The RollMoveAction (:110) sits after
    // the switch and is queued for every case.
    MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];
    queue_monster_move_effects(s, mi, sts::registry::kSlaverRed, move);
    if (move == kRedEntangle) {
        m.pad0 = static_cast<uint8_t>(m.pad0 | kSlaverRedPad0UsedEntangle);
    }
    queue_roll_move(s, mi);
}

}  // namespace sts::engine
