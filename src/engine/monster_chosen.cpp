// The Chosen: native move selection and turn body. See monster_chosen.hpp for
// provenance, the two-arm getMove, the draw accounting, and why `usedHex` needs
// no state bit at A20.

#include "sts/engine/monster_chosen.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, move-history helpers, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kZap = sts::registry::kChosenMoveZap;                // 1
constexpr uint8_t kDrain = sts::registry::kChosenMoveDrain;            // 2
constexpr uint8_t kDebilitate = sts::registry::kChosenMoveDebilitate;  // 3
constexpr uint8_t kHex = sts::registry::kChosenMoveHex;                // 4
constexpr uint8_t kPoke = sts::registry::kChosenMovePoke;              // 5

// getMove's A17+ arm (Chosen.java:153-173), MINUS step 1 -- the `!usedHex` HEX
// opener, which only ever fires on init()'s rollMove and is telegraphed there
// directly (see the header). Everything below is the arm as written, branch for
// branch. `num` is the aiRng.random(99) the caller already drew.
void chosen_get_move(MonsterState& m, int32_t num) noexcept {
    if (!last_move_is(m, kDebilitate) && !last_move_is(m, kDrain)) {
        // (:159-166). Note this is `!lastMove(3) && !lastMove(2)` -- TWO
        // one-move-back tests against different moves, NOT lastTwoMoves; the ring
        // slot it reads is [0] both times.
        if (num < 50) {
            set_monster_move(m, kDebilitate, MonsterIntent::ATTACK_DEBUFF);
        } else {
            set_monster_move(m, kDrain, MonsterIntent::DEBUFF);
        }
        return;
    }
    if (num < 40) {
        set_monster_move(m, kZap, MonsterIntent::ATTACK);  // (:167-169)
        return;
    }
    // (:171): setMove(POKE, ATTACK, damage.get(2).base, 2, true) -- the `2, true`
    // is the multi-hit TELEGRAPH; the two hits themselves are the registry
    // program's two DAMAGE steps.
    set_monster_move(m, kPoke, MonsterIntent::ATTACK);
}

}  // namespace

void chosen_init(CombatState& s, uint8_t mi) noexcept {
    // The ctor is `super(...)` + setHp(min, max): exactly one monster_hp_rng
    // inclusive draw, currentHealth == maxHealth (AbstractMonster.java:765-775).
    // At A20 the A7 column (98, 103) is the live one -- the branch is
    // `ascensionLevel >= 7` despite the A_2_HP_* constant names (Chosen.java:
    // 45-46, 80-84).
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::CHOSEN);
    const int32_t hp =
        random(s.monster_hp_rng, sts::registry::kChosen.hp_min(kMonsterAscension),
               sts::registry::kChosen.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). The draw
    // ALWAYS happens; getMove's A17+ arm then returns from the `!usedHex` branch
    // (:154-158) without ever reading num -- the Guardian / Red Slaver / Looter
    // discarded-draw precedent. So the Chosen's opening move is HEX at every
    // seed, and the stream still advances by one.
    (void)random(s.ai_rng, 99);
    set_monster_move(m, kHex, MonsterIntent::STRONG_DEBUFF);
}

void chosen_roll_move(CombatState& s, uint8_t mi) noexcept {
    const int32_t num = random(s.ai_rng, 99);
    chosen_get_move(s.monsters[mi], num);
}

void chosen_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (Chosen.java:105-138). Every case is presentation plus the
    // registry program -- Zap's FastShake, Poke's AnimateSlowAttack,
    // Debilitate's AnimateSlowAttack, and Hex's Talk/ChangeState("ATTACK")/Wait
    // are all animation, and none of them draws seeded RNG. The RollMoveAction
    // at :137 sits OUTSIDE the switch, so all five cases reach it.
    queue_monster_move_effects(s, mi, sts::registry::kChosen,
                               s.monsters[mi].move_history[0]);
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
