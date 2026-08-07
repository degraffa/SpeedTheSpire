// Gremlin Nob AI + monster turn. Move selection (getMove) is native code;
// stats and move-effect programs come from the generated monster table
// (registry/monsters.yaml). See monster_gremlin_nob.hpp for provenance, scope,
// and the draw-counting convention.

#include "sts/engine/monster_gremlin_nob.hpp"

#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, move-history helpers, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/registry/monster_table.hpp"   // kGremlinNob + move-id constants

namespace sts::engine {

namespace {

constexpr uint8_t kBullRush = sts::registry::kGremlinNobMoveBullRush;    // 1
constexpr uint8_t kSkullBash = sts::registry::kGremlinNobMoveSkullBash;  // 2
constexpr uint8_t kBellow = sts::registry::kGremlinNobMoveBellow;        // 3

// AbstractMonster.lastMoveBefore (AbstractMonster.java:437-444) used to be a
// file-local copy here, justified by the Nob's A18 branch being its only reader
// in the Act-1 roster. The Snake Plant's A17 arm (SnakePlant.java:132) made that
// two, so the helper moved up to monster_dispatch.hpp beside lastMove /
// lastTwoMoves and this copy is deleted (rule of two, conventions §7).

// Both branches of getMove share this tail (GremlinNob.java:142-150 and
// :160-168): two Bull Rushes in a row force a Skull Bash, otherwise Bull Rush.
void nob_bash_or_rush(MonsterState& m) noexcept {
    if (last_two_moves_are(m, kBullRush)) {
        // canVuln is true for every reachable encounter, so the ATTACK_DEBUFF
        // telegraph is the live one (GremlinNob.java:143-147).
        set_monster_move(m, kSkullBash, MonsterIntent::ATTACK_DEBUFF);
    } else {
        set_monster_move(m, kBullRush, MonsterIntent::ATTACK);
    }
}

// getMove(num) at the engine's fixed A20 difficulty: the A18 branch
// (GremlinNob.java:133-150). `num` is drawn by the caller (rollMove always
// draws) but this branch never reads it -- the sub-A18 `num < 33` test at :152
// is the only consumer, and A20 does not take that path.
void gremlin_nob_get_move(MonsterState& m) noexcept {
    if (m.move_history[0] == 0) {
        // usedBellow (GremlinNob.java:128-132): the very first decision is a
        // forced Bellow. An empty move ring is exactly "getMove has not run yet".
        set_monster_move(m, kBellow, MonsterIntent::BUFF);
        return;
    }
    if (!last_move_is(m, kSkullBash) && !last_move_before_is(m, kSkullBash)) {
        set_monster_move(m, kSkullBash, MonsterIntent::ATTACK_DEBUFF);
        return;
    }
    nob_bash_or_rush(m);
}

}  // namespace

void gremlin_nob_init(CombatState& state, uint8_t monster_index) noexcept {
    const auto& def = sts::registry::kGremlinNob;
    MonsterState& m = state.monsters[monster_index];
    m.monster_id = static_cast<uint16_t>(MonsterId::GREMLIN_NOB);

    // setHp(85,90) at A20: one inclusive monster_hp_rng draw, currentHealth ==
    // maxHealth (AbstractMonster.setHp).
    const int32_t rolled = random(state.monster_hp_rng,
                                  def.hp_min(kMonsterAscension),
                                  def.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(rolled);
    m.max_hp = static_cast<int16_t>(rolled);

    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;

    // init() -> rollMove() -> getMove(aiRng.random(99)). The forced Bellow
    // returns before touching num, but rollMove ALWAYS draws.
    (void)random(state.ai_rng, 99);
    gremlin_nob_get_move(m);
}

void gremlin_nob_take_turn(CombatState& state, uint8_t monster_index) noexcept {
    MonsterState& m = state.monsters[monster_index];
    const uint8_t move = m.move_history[0];

    // (a) The decided move's effects, in takeTurn addToBottom order: Bellow's
    //     Anger apply, Skull Bash's damage + Vulnerable, Bull Rush's damage.
    queue_monster_move_effects(state, monster_index, sts::registry::kGremlinNob,
                               move);

    // (b) The unconditional trailing RollMoveAction (GremlinNob.java:112) ->
    //     one aiRng.random(99) draw.
    (void)random(state.ai_rng, 99);
    gremlin_nob_get_move(m);
}

}  // namespace sts::engine
