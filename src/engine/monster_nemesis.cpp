// The Nemesis: the two-draw getMove, the scythe cooldown and the every-other-
// round Intangible. See monster_nemesis.hpp for provenance and the five
// readings this body leans on.

#include "sts/engine/monster_nemesis.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // move helpers, queue_monster_move_effects
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kTriAttack = sts::registry::kNemesisMoveTriAttack;  // 2
constexpr uint8_t kScythe = sts::registry::kNemesisMoveScythe;        // 3
constexpr uint8_t kTriBurn = sts::registry::kNemesisMoveTriBurn;      // 4

[[nodiscard]] bool has_intangible(const CombatState& s, uint8_t mi) noexcept {
    const MonsterState& m = s.monsters[mi];
    for (uint8_t i = 0; i < m.power_count; ++i) {
        if (m.powers[i].power_id ==
            static_cast<uint16_t>(PowerId::INTANGIBLE_MONSTER)) {
            return true;
        }
    }
    return false;
}

}  // namespace

void nemesis_decide_move(CombatState& s, uint8_t mi, int32_t num) noexcept {
    MonsterState& m = s.monsters[mi];
    // `--this.scytheCooldown;` (:147) -- UNCONDITIONAL, at the top, before the
    // firstMove arm. Header note (4).
    nemesis_set_scythe_cooldown(m, nemesis_scythe_cooldown(m) - 1);
    const auto cooldown = [&m]() noexcept { return nemesis_scythe_cooldown(m); };
    const auto take_scythe = [&]() noexcept {
        set_monster_move(m, kScythe, MonsterIntent::ATTACK);
        nemesis_set_scythe_cooldown(m, kNemesisScytheCooldown);
    };

    if ((m.flags & kMonsterFlagNemesisFirstMove) != 0u) {
        // (:148-156). `firstMove = false` FIRST, then a bare 50/50 on `num`; no
        // history guard and no cooldown consultation on either branch.
        m.flags &= ~kMonsterFlagNemesisFirstMove;
        if (num < 50) {
            set_monster_move(m, kTriAttack, MonsterIntent::ATTACK);
        } else {
            set_monster_move(m, kTriBurn, MonsterIntent::DEBUFF);
        }
        return;
    }
    if (num < 30) {
        // (:157-171).
        if (!last_move_is(m, kScythe) && cooldown() <= 0) {
            take_scythe();
        } else if (random_boolean(s.ai_rng)) {  // (:161)
            if (!last_two_moves_are(m, kTriAttack)) {
                set_monster_move(m, kTriAttack, MonsterIntent::ATTACK);
            } else {
                set_monster_move(m, kTriBurn, MonsterIntent::DEBUFF);
            }
        } else if (!last_move_is(m, kTriBurn)) {
            set_monster_move(m, kTriBurn, MonsterIntent::DEBUFF);
        } else {
            set_monster_move(m, kTriAttack, MonsterIntent::ATTACK);
        }
        return;
    }
    if (num < 65) {
        // (:172-184).
        if (!last_two_moves_are(m, kTriAttack)) {
            set_monster_move(m, kTriAttack, MonsterIntent::ATTACK);
        } else if (random_boolean(s.ai_rng)) {  // (:175)
            if (cooldown() > 0) {
                set_monster_move(m, kTriBurn, MonsterIntent::DEBUFF);
            } else {
                take_scythe();
            }
        } else {
            set_monster_move(m, kTriBurn, MonsterIntent::DEBUFF);
        }
        return;
    }
    // (:185-192).
    if (!last_move_is(m, kTriBurn)) {
        set_monster_move(m, kTriBurn, MonsterIntent::DEBUFF);
        return;
    }
    // `if (AbstractDungeon.aiRng.randomBoolean() && this.scytheCooldown <= 0)`
    // (:187). THE DRAW IS THE LEFT OPERAND and Java evaluates it first, so it is
    // spent even when the cooldown is about to refuse -- header note (3). The
    // local is what forbids the short-circuit C++ would otherwise apply if the
    // operands were swapped.
    const bool coin = random_boolean(s.ai_rng);
    if (coin && cooldown() <= 0) {
        take_scythe();
    } else {
        set_monster_move(m, kTriAttack, MonsterIntent::ATTACK);
    }
}

void nemesis_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m = MonsterState{};
    m.monster_id = static_cast<uint16_t>(MonsterId::NEMESIS);
    // The `super(...)` HP argument is the LITERAL 185 -- no draw -- and the
    // setHp chain (:78-82) is the ONE monster_hp_rng draw. Both arms are the
    // SINGLE-argument overload, which is literally setHp(hp, hp)
    // (AbstractMonster.java:777-779), so the range is degenerate and the draw
    // still happens.
    const int32_t hp = random(s.monster_hp_rng,
                              sts::registry::kNemesis.hp_min(kMonsterAscension),
                              sts::registry::kNemesis.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    // firstMove = true (:65) and scytheCooldown = 0 (:56), both field
    // initializers. The cooldown's zero is already the MonsterState{} default;
    // the latch is written explicitly because SET means "still pending".
    m.flags |= kMonsterFlagNemesisFirstMove;
    nemesis_set_scythe_cooldown(m, 0);
    // init() -> rollMove -> getMove(aiRng.random(99)). The firstMove arm reads
    // `num`, so the opening telegraph is seed-dependent -- and the cooldown is
    // already -1 (floored to 0) by the time it returns.
    nemesis_decide_move(s, mi, random(s.ai_rng, 99));
}

void nemesis_take_turn(CombatState& s, uint8_t mi) noexcept {
    const uint8_t move = s.monsters[mi].move_history[0];
    // All three cases are pure registry programs: SCYTHE is one DamageAction
    // (ChangeState / playSfx / Wait are presentation and playSfx's
    // MathUtils.random(1) is UNSEEDED), TRI_ATTACK is the three-step loop, and
    // TRI_BURN is one MakeTempCardInDiscardAction with a count.
    if (move == kScythe || move == kTriAttack || move == kTriBurn) {
        queue_monster_move_effects(s, mi, sts::registry::kNemesis, move);
    }
    // OUTSIDE the switch (:114-116): re-arm Intangible whenever it is absent.
    // The read is at QUEUE time, exactly where the Java's `hasPower` is, so a
    // removal that has already resolved this round is visible here.
    if (!has_intangible(s, mi)) {
        ActionQueueItem intan{};
        intan.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        intan.src = mi;  // ApplyPowerAction(this, this, ...)
        intan.tgt = mi;
        intan.amount = 1;  // new IntangiblePower(this, 1)
        intan.flags = make_apply_power_flags(PowerId::INTANGIBLE_MONSTER);
        add_to_bottom(s, intan);
    }
    // RollMoveAction (:117), also outside the switch and after the Intangible.
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

void nemesis_roll_move(CombatState& s, uint8_t mi) noexcept {
    nemesis_decide_move(s, mi, random(s.ai_rng, 99));
}

}  // namespace sts::engine
