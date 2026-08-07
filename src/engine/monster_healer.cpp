// The Healer: native move selection, the group-wide needToHeal sum, and the two
// moves that fan one authored step out over the whole live group. See
// monster_healer.hpp for provenance, the two ascension-switched gates and the
// intent collision between moves 2 and 3.

#include "sts/engine/monster_healer.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effect(s), move-history helpers, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kAttack = sts::registry::kHealerMoveAttack;  // 1
constexpr uint8_t kHeal = sts::registry::kHealerMoveHeal;      // 2
constexpr uint8_t kBuff = sts::registry::kHealerMoveBuff;      // 3

// HEAL and BUFF each author exactly ONE template step (monsters.yaml id 36); the
// fan-out below repeats it per live member. Named rather than a bare 0 so the
// coupling to the row is visible from here.
constexpr uint8_t kTemplateStep = 0;

// needToHeal (Healer.java:157-160):
//
//     int needToHeal = 0;
//     for (AbstractMonster m : AbstractDungeon.getMonsters().monsters) {
//         if (m.isDying || m.isEscaping) continue;
//         needToHeal += m.maxHealth - m.currentHealth;
//     }
//
// INCLUDES THE HEALER ITSELF -- no `m == this` exclusion -- so a lone wounded
// Healer heals itself. It visits DEAD RECORDS, which the group never drops
// (MonsterGroup.java:35-40), and rejects them through the isDying/isEscaping pair
// that monster_dead_or_escaped is exactly. Block does NOT count: the sum is
// max_hp - hp and nothing else.
[[nodiscard]] int32_t need_to_heal(const CombatState& s) noexcept {
    int32_t total = 0;
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        const MonsterState& m = s.monsters[i];
        if (monster_basically_dead(m)) {
            continue;
        }
        total += static_cast<int32_t>(m.max_hp) - static_cast<int32_t>(m.hp);
    }
    return total;
}

// The engine's live entry point: decide at the fixed ascension.
void healer_get_move(CombatState& s, uint8_t mi, int32_t num) noexcept {
    healer_decide_move(s, mi, num, kMonsterAscension);
}

}  // namespace

// getMove (Healer.java:156-183), BOTH arms of BOTH ascension gates, because
// writing only the live one would hide what the differences ARE -- they move in
// OPPOSITE directions (see the header) -- and because the tier-2 tests have to
// exercise the sub-A17 arms, which is why `ascension` is a parameter rather than
// the kMonsterAscension constant read inline. `num` is the aiRng.random(99) the
// caller already drew.
void healer_decide_move(CombatState& s, uint8_t mi, int32_t num,
                        int32_t ascension) noexcept {
    MonsterState& m = s.monsters[mi];
    // Gate 1 (:159-167). The threshold RISES at A17, making the heal rarer. The
    // history guard is the SAME on both arms: !lastTwoMoves(HEAL).
    const int32_t heal_threshold = ascension >= 17 ? 20 : 15;
    if (need_to_heal(s) > heal_threshold && !last_two_moves_are(m, kHeal)) {
        // Intent.BUFF, not DEFEND -- and the same intent move 3 telegraphs.
        set_monster_move(m, kHeal, MonsterIntent::BUFF);
        return;
    }
    // Gate 2 (:170-178). The guard TIGHTENS at A17: !lastMove(ATTACK) rejects a
    // single preceding attack, where !lastTwoMoves(ATTACK) needs two.
    const bool attack_allowed = ascension >= 17
                                    ? !last_move_is(m, kAttack)
                                    : !last_two_moves_are(m, kAttack);
    if (num >= 40 && attack_allowed) {
        set_monster_move(m, kAttack, MonsterIntent::ATTACK_DEBUFF);
        return;
    }
    if (!last_two_moves_are(m, kBuff)) {
        set_monster_move(m, kBuff, MonsterIntent::BUFF);  // (:179-181)
        return;
    }
    set_monster_move(m, kAttack, MonsterIntent::ATTACK_DEBUFF);  // (:182)
}

namespace {

// The shared body of HEAL and BUFF (Healer.java:104-107 / :114-117): queue the
// move's single template step ONCE PER LIVE GROUP MEMBER, in slot order,
// retargeted to that member. Liveness is read HERE, at queue time, exactly as the
// Java's loop does -- a member that dies before the queue drains still has its
// action pending (and a HEAL aimed at it is then rejected by op_heal's own
// isDying guard, AbstractMonster.java:385-387).
void fan_out_over_live_group(CombatState& s, uint8_t mi, uint8_t move) noexcept {
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        if (monster_basically_dead(s.monsters[i])) {
            continue;  // `if (m.isDying || m.isEscaping) continue;`
        }
        queue_monster_move_effect(s, mi, sts::registry::kHealer, move,
                                  kTemplateStep, i);
    }
}

}  // namespace

void healer_init(CombatState& s, uint8_t mi) noexcept {
    // The ctor is `super(...)` + setHp(min, max): exactly one monster_hp_rng
    // inclusive draw, currentHealth == maxHealth (AbstractMonster.java:765-775);
    // at A20 the A7 column (50, 58) is the live one (Healer.java:59-63).
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::HEALER);
    const int32_t hp =
        random(s.monster_hp_rng, sts::registry::kHealer.hp_min(kMonsterAscension),
               sts::registry::kHealer.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;  // unused by this monster
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). This getMove
    // READS num (on arm 2), so the opening move is seed-dependent -- but arm 1
    // can preempt it, and at init every member is at FULL HP, so needToHeal is 0
    // and arm 1 never fires on turn 1. That full-HP reading is only true because
    // spawn_group pre-marks every slot of the group as constructed and alive
    // before it runs any init (monster_dispatch.cpp), which is what the game's
    // construct-all-then-MonsterGroup.init() phasing gives for free
    // (MonsterGroup.java:62-66).
    healer_get_move(s, mi, random(s.ai_rng, 99));
}

void healer_roll_move(CombatState& s, uint8_t mi) noexcept {
    const int32_t num = random(s.ai_rng, 99);
    healer_get_move(s, mi, num);
}

void healer_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (Healer.java:87-125). playSfx opens every case and is UNSEEDED
    // (:128), as are the ChangeStateAction("STAFF_RAISE") / WaitAction pairs. The
    // RollMoveAction at :124 sits OUTSIDE the switch, so all three cases reach it.
    const uint8_t move = s.monsters[mi].move_history[0];
    if (move == kHeal || move == kBuff) {
        fan_out_over_live_group(s, mi, move);
    } else {
        // ATTACK (:89-95): DamageAction on damage.get(0) then Frail(player, 2,
        // true), in that order -- the registry program, unchanged.
        queue_monster_move_effects(s, mi, sts::registry::kHealer, move);
    }
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
