// The Masked Bandits trio. Provenance, the draw accounting, the deathReact
// verified-negative and the move graphs are in monster_bandits.hpp.

#include "sts/engine/monster_bandits.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, move helpers
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kPointySpecial =
    sts::registry::kBanditPointyMovePointySpecial;                      // 1

constexpr uint8_t kCrossSlash = sts::registry::kBanditLeaderMoveCrossSlash;  // 1
constexpr uint8_t kMock = sts::registry::kBanditLeaderMoveMock;              // 2
constexpr uint8_t kAgonizingSlash =
    sts::registry::kBanditLeaderMoveAgonizingSlash;                          // 3

constexpr uint8_t kMaul = sts::registry::kBanditBearMoveMaul;      // 1
constexpr uint8_t kBearHug = sts::registry::kBanditBearMoveBearHug;  // 2
constexpr uint8_t kLunge = sts::registry::kBanditBearMoveLunge;    // 3

// The trio's shared queued-SetMoveAction tail (Torch Head shape): every
// takeTurn ends in one of these, never a RollMoveAction.
void queue_set_move(CombatState& s, uint8_t mi, uint8_t move,
                    MonsterIntent intent) noexcept {
    ActionQueueItem set_move{};
    set_move.opcode = static_cast<uint16_t>(Opcode::SET_MOVE);
    set_move.src = mi;
    set_move.tgt = mi;
    set_move.amount = move;
    set_move.flags = static_cast<uint32_t>(intent);
    add_to_bottom(s, set_move);
}

// The shared ctor shape: one monster_hp_rng draw (the Pointy's tier columns
// are degenerate min == max, which reproduces the single-arg setHp(hp) ==
// setHp(hp, hp) draw, AbstractMonster.java:765-775), then init()'s rollMove --
// one ai_rng.random(99) whose num every bandit getMove ignores.
void bandit_init_common(CombatState& s, uint8_t mi, MonsterId id,
                        const sts::registry::MonsterDef& def, uint8_t opener,
                        MonsterIntent opener_intent) noexcept {
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
    (void)random(s.ai_rng, 99);  // rollMove's draw, discarded by getMove
    set_monster_move(m, opener, opener_intent);
}

}  // namespace

void bandit_pointy_init(CombatState& s, uint8_t mi) noexcept {
    // getMove (BanditPointy.java:97-100): setMove(1, ATTACK, base, 2, true) --
    // the 2-hit telegraph -- regardless of num.
    bandit_init_common(s, mi, MonsterId::BANDIT_POINTY,
                       sts::registry::kBanditPointy, kPointySpecial,
                       MonsterIntent::ATTACK);
}

void bandit_pointy_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (:61-67): ChangeState/Wait presentation, two DamageActions on
    // damage.get(0) (the registry program's two DAMAGE steps), then the queued
    // SetMoveAction(1, ATTACK, base, 2, true).
    queue_monster_move_effects(s, mi, sts::registry::kBanditPointy,
                               kPointySpecial);
    queue_set_move(s, mi, kPointySpecial, MonsterIntent::ATTACK);
}

void bandit_leader_init(CombatState& s, uint8_t mi) noexcept {
    // getMove (BanditLeader.java:150-152): setMove(2, Intent.UNKNOWN) -- the
    // opener is always MOCK.
    bandit_init_common(s, mi, MonsterId::BANDIT_LEADER,
                       sts::registry::kBanditLeader, kMock,
                       MonsterIntent::UNKNOWN);
}

void bandit_leader_take_turn(CombatState& s, uint8_t mi) noexcept {
    const MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];
    // MOCK's TalkAction pair (:92-102) -- including the bearLives scan that
    // picks the LINE -- is pure presentation; only CROSS_SLASH and
    // AGONIZING_SLASH have registry programs.
    queue_monster_move_effects(s, mi, sts::registry::kBanditLeader, move);
    if (move == kMock) {
        // case 2 (:103): SetMoveAction(3, ATTACK_DEBUFF, agonize base).
        queue_set_move(s, mi, kAgonizingSlash, MonsterIntent::ATTACK_DEBUFF);
        return;
    }
    if (move == kAgonizingSlash) {
        // case 3 (:111): SetMoveAction(1, ATTACK, slash base).
        queue_set_move(s, mi, kCrossSlash, MonsterIntent::ATTACK);
        return;
    }
    // case 1 (:114-123). The branch runs at QUEUE time, and lastTwoMoves(1)
    // reads a history whose [0] is the CURRENT cross slash -- so at A17+ the
    // leader re-slashes until two land in a row, then telegraphs Agonizing.
    if (kMonsterAscension >= 17 && !last_two_moves_are(m, kCrossSlash)) {
        queue_set_move(s, mi, kCrossSlash, MonsterIntent::ATTACK);
        return;
    }
    queue_set_move(s, mi, kAgonizingSlash, MonsterIntent::ATTACK_DEBUFF);
}

void bandit_bear_init(CombatState& s, uint8_t mi) noexcept {
    // getMove (BanditBear.java:136-138): setMove(2, STRONG_DEBUFF) -- the
    // opener is always BEAR_HUG.
    bandit_init_common(s, mi, MonsterId::BANDIT_BEAR,
                       sts::registry::kBanditBear, kBearHug,
                       MonsterIntent::STRONG_DEBUFF);
}

void bandit_bear_take_turn(CombatState& s, uint8_t mi) noexcept {
    const uint8_t move = s.monsters[mi].move_history[0];
    queue_monster_move_effects(s, mi, sts::registry::kBanditBear, move);
    if (move == kLunge) {
        // case 3 (:99): SetMoveAction(1, ATTACK, maul base).
        queue_set_move(s, mi, kMaul, MonsterIntent::ATTACK);
        return;
    }
    // cases 2 and 1 (:85, :92): both chain into
    // SetMoveAction(3, ATTACK_DEFEND, lunge base).
    queue_set_move(s, mi, kLunge, MonsterIntent::ATTACK_DEFEND);
}

}  // namespace sts::engine
