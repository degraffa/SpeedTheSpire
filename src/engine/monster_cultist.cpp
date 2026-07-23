// Cultist AI + monster turn (B3.13). Move selection (getMove) is native code;
// stats and move-effect programs come from the generated monster table
// (registry/monsters.yaml). See monster_cultist.hpp for provenance, scope, and
// the draw-counting convention.

#include "sts/engine/monster_cultist.hpp"

#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, set_monster_move, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/registry/monster_table.hpp"   // kCultist def + move-id constants

namespace sts::engine {

namespace {

constexpr uint8_t kMoveDarkStrike = sts::registry::kCultistMoveDarkStrike;   // 1
constexpr uint8_t kMoveIncantation = sts::registry::kCultistMoveIncantation; // 3

// A7 HP column (Cultist.java:59-60 setHp(50,56)) resolved at the skeleton A20.
constexpr int kHpMin = sts::registry::kCultist.hp_min(kMonsterAscension);
constexpr int kHpMax = sts::registry::kCultist.hp_max(kMonsterAscension);

}  // namespace

void cultist_init(CombatState& state, uint8_t monster_index) noexcept {
    MonsterState& m = state.monsters[monster_index];
    m.monster_id = static_cast<uint16_t>(MonsterId::CULTIST);

    // setHp(50,56): one inclusive monster_hp_rng draw; currentHealth == maxHealth.
    const int32_t rolled = random(state.monster_hp_rng, kHpMin, kHpMax);
    m.hp = static_cast<int16_t>(rolled);
    m.max_hp = static_cast<int16_t>(rolled);

    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;

    // init() -> rollMove() -> getMove(aiRng.random(99)). firstMove forces
    // INCANTATION and returns before using the roll, but rollMove ALWAYS draws.
    (void)random(state.ai_rng, 99);
    set_monster_move(m, kMoveIncantation, MonsterIntent::BUFF);
}

void cultist_take_turn(CombatState& state, uint8_t monster_index) noexcept {
    MonsterState& m = state.monsters[monster_index];
    const uint8_t move = m.move_history[0];

    // Incantation applies RitualPower(this, ..., onPlayer=false), whose first
    // atEndOfRound tick is skipped (RitualPower.skipFirst). Set the skip flag now,
    // as the Ritual is applied this turn; the RITUAL native at_end_of_round body
    // consumes it (power_hooks.cpp). The APPLY_POWER itself is the table effect
    // enqueued just below.
    if (move == kMoveIncantation) {
        m.flags |= kMonsterFlagRitualSkip;
    }

    // (a) Enqueue the current move's effects (Dark Strike damage / Incantation's
    //     Ritual apply) in Cultist.takeTurn addToBottom order.
    queue_monster_move_effects(state, monster_index, sts::registry::kCultist, move);

    // (b) Roll the next move: unconditional RollMoveAction (Cultist.java:108) ->
    //     one aiRng.random(99) draw. getMove is past firstMove now, so it always
    //     decides DARK_STRIKE (num ignored) -- no tiebreak booleans.
    (void)random(state.ai_rng, 99);
    set_monster_move(m, kMoveDarkStrike, MonsterIntent::ATTACK);
}

}  // namespace sts::engine
