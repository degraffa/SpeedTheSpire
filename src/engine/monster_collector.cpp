// The Collector: native move selection over two takeTurn-written latches and
// the derived revive-slot map, the torch-head summons and the post-super
// suicide sweep. See monster_collector.hpp for provenance and the five
// readings.

#include "sts/engine/monster_collector.hpp"

#include <cassert>

#include "sts/engine/action_queue.hpp"      // add_to_bottom/add_to_top, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags, make_spawn_monster_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effect(s), move helpers, kMinionAppliedAmount
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kSpawn = sts::registry::kTheCollectorMoveSpawn;            // 1
constexpr uint8_t kFireball = sts::registry::kTheCollectorMoveFireball;      // 2
constexpr uint8_t kBuff = sts::registry::kTheCollectorMoveBuff;              // 3
constexpr uint8_t kMegaDebuff = sts::registry::kTheCollectorMoveMegaDebuff;  // 4
constexpr uint8_t kRevive = sts::registry::kTheCollectorMoveRevive;          // 5

// The BUFF row's two template steps (monsters.yaml id 43): 0 the block the
// Collector keeps, 1 the Strength the walk gives EVERY live record including
// itself (the Healer kTemplateStep precedent).
constexpr uint8_t kBuffBlockStep = 0;
constexpr uint8_t kBuffStrengthStep = 1;

[[nodiscard]] bool initial_spawn_pending(const MonsterState& m) noexcept {
    return (m.flags & kMonsterFlagCollectorInitialSpawn) != 0u;
}
[[nodiscard]] bool ult_used(const MonsterState& m) noexcept {
    return (m.flags & kMonsterFlagCollectorUltUsed) != 0u;
}

// Slot k's derived map entries -- header note (2).
[[nodiscard]] bool slot_spawned(const CombatState& s, int k) noexcept {
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        if (s.monsters[i].draw_x == kTorchHeadSlotX[k] &&
            s.monsters[i].monster_id ==
                static_cast<uint16_t>(MonsterId::TORCH_HEAD)) {
            return true;
        }
    }
    return false;
}
[[nodiscard]] bool slot_occupant_dying(const CombatState& s, int k) noexcept {
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        if (s.monsters[i].draw_x == kTorchHeadSlotX[k] &&
            s.monsters[i].monster_id ==
                static_cast<uint16_t>(MonsterId::TORCH_HEAD) &&
            s.monsters[i].hp > 0) {
            return false;  // a LIVE occupant -- the newest, by the derivation
        }
    }
    return true;  // no live record at x_k (meaningful only when spawned)
}

// Queue the torch-head spawns for the slot keys in `spawn_mask` (bit k == slot
// k), in key order 1 then 2 -- the HashMap iteration order, header note (1).
// SPAWN passes both bits; REVIVE passes the dead slots' bits. The Automaton's
// queue-time/resolve-time split, verbatim: ctor draws here, init rolls at each
// spawn's resolve, the Collector's own trailing roll after them.
void queue_torch_head_spawns(CombatState& s, uint8_t mi,
                             uint8_t spawn_mask) noexcept {
    const sts::registry::MonsterDef* def =
        sts::registry::monster_def(sts::registry::MonsterId::TORCH_HEAD);
    assert(def != nullptr && "TorchHead has no registry row");
    if (def == nullptr) {
        return;
    }
    int16_t xs[kMonsterCap + 2] = {};
    uint8_t n = s.monster_count;
    for (uint8_t i = 0; i < n && i < kMonsterCap; ++i) {
        xs[i] = s.monsters[i].draw_x;
    }
    uint8_t self_index = mi;

    for (int k = 0; k < 2; ++k) {
        if ((spawn_mask & (1u << k)) == 0u) {
            continue;
        }
        // TorchHead ctor at QUEUE time (TheCollector.java:128/:165): the
        // super(...) argument monsterHpRng.random(38, 40) -- the registry
        // SUPER_ARG_HP row, flat -- then the tiered setHp (TorchHead.java:
        // 50-54). The MathUtils y-jitter is unseeded. The ctor's setMove
        // (:45) is per-record state the spawn-at-hp init reproduces.
        const sts::registry::MonsterRollDef* super_arg = def->roll(0);
        assert(super_arg != nullptr && "TorchHead row lost its SUPER_ARG_HP");
        if (super_arg != nullptr) {
            (void)random(s.monster_hp_rng, super_arg->min(kMonsterAscension),
                         super_arg->max(kMonsterAscension));
        }
        const int32_t hp = random(s.monster_hp_rng,
                                  def->hp_min(kMonsterAscension),
                                  def->hp_max(kMonsterAscension));
        const int16_t x = kTorchHeadSlotX[k];

        uint8_t pos = 0;
        while (pos < n && x > xs[pos]) {
            ++pos;
        }
        for (uint8_t i = n; i > pos; --i) {
            xs[i] = xs[i - 1];
        }
        xs[pos] = x;
        ++n;
        if (pos <= self_index) {
            ++self_index;
        }

        ActionQueueItem spawn{};
        spawn.opcode = static_cast<uint16_t>(Opcode::SPAWN_MONSTER);
        spawn.src = mi;
        spawn.tgt = pos;
        spawn.amount = hp;
        // isMinion = true is the addToTop-at-resolve form -- the explicit
        // APPLY_POWER right behind the spawn, NOT kSpawnApplyMinion (interp.hpp,
        // the S2.24 amendment). No pre-battle (TorchHead has none).
        spawn.flags = make_spawn_monster_flags(
            static_cast<uint16_t>(MonsterId::TORCH_HEAD), x);
        add_to_bottom(s, spawn);

        ActionQueueItem minion{};
        minion.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        minion.src = pos;
        minion.tgt = pos;
        minion.amount = kMinionAppliedAmount;
        minion.flags = make_apply_power_flags(PowerId::MINION);
        add_to_bottom(s, minion);
    }

    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = self_index;
    add_to_bottom(s, roll);
}

}  // namespace

bool collector_is_minion_dead(const CombatState& s) noexcept {
    // isMinionDead (:205-211): any enemySlots value isDying -- derived, header
    // note (2). A never-spawned slot has no map entry and contributes nothing.
    for (int k = 0; k < 2; ++k) {
        if (slot_spawned(s, k) && slot_occupant_dying(s, k)) {
            return true;
        }
    }
    return false;
}

void collector_decide_move(CombatState& s, uint8_t mi, int32_t num) noexcept {
    MonsterState& m = s.monsters[mi];
    // getMove (:181-203).
    if (initial_spawn_pending(m)) {
        set_monster_move(m, kSpawn, MonsterIntent::UNKNOWN);  // (:182-185)
        return;
    }
    if (m.pad0 >= 3 && !ult_used(m)) {
        set_monster_move(m, kMegaDebuff, MonsterIntent::STRONG_DEBUFF);  // (:186-189)
        return;
    }
    if (num <= 25 && collector_is_minion_dead(s) && !last_move_is(m, kRevive)) {
        set_monster_move(m, kRevive, MonsterIntent::UNKNOWN);  // (:190-193)
        return;
    }
    if (num <= 70 && !last_two_moves_are(m, kFireball)) {
        set_monster_move(m, kFireball, MonsterIntent::ATTACK);  // (:194-197)
        return;
    }
    if (!last_move_is(m, kBuff)) {
        set_monster_move(m, kBuff, MonsterIntent::DEFEND_BUFF);  // (:199)
    } else {
        set_monster_move(m, kFireball, MonsterIntent::ATTACK);  // (:201)
    }
}

void collector_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m = MonsterState{};
    m.monster_id = static_cast<uint16_t>(MonsterId::THE_COLLECTOR);
    // super HP argument is a LITERAL 282 (:85) -- no draw; setHp(300)/setHp(282)
    // (:89-95) is the single-arg overload: ONE degenerate monster_hp_rng draw.
    const auto& def = sts::registry::kTheCollector;
    const int32_t hp = random(s.monster_hp_rng, def.hp_min(kMonsterAscension),
                              def.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = m.hp;
    m.draw_x = kCollectorDrawX;  // the ctor's offsetX (:85)
    // initialSpawn = true (:76) and ultUsed = false (:75), field initializers;
    // turnsTaken = 0 (:71) in pad0.
    m.flags |= kMonsterFlagCollectorInitialSpawn;
    // init() -> rollMove: the initialSpawn arm forces SPAWN/UNKNOWN, but the
    // draw is consumed regardless and the stream moves.
    collector_decide_move(s, mi, random(s.ai_rng, 99));
}

void collector_roll_move(CombatState& s, uint8_t mi) noexcept {
    collector_decide_move(s, mi, random(s.ai_rng, 99));
}

void collector_take_turn(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];
    // `++this.turnsTaken;` sits OUTSIDE the switch (:176) -- every turn counts,
    // whatever the move. Incremented FIRST here because nothing below reads it;
    // saturating (only `>= 3` is ever tested).
    if (m.pad0 < 255) {
        ++m.pad0;
    }

    if (move == kSpawn) {
        // Case 1 (:126-134): both slots, key order; initialSpawn = false is a
        // takeTurn-time write -- header note (3).
        m.flags &= ~kMonsterFlagCollectorInitialSpawn;
        queue_torch_head_spawns(s, mi, 0b11);
        return;  // the spawn helper queued the trailing roll
    }
    if (move == kRevive) {
        // Case 5 (:162-169): one fresh head per DYING slot, key order. The
        // dying test is read HERE, at take-turn time, exactly as the Java's
        // loop reads entry.getValue().isDying.
        uint8_t mask = 0;
        for (int k = 0; k < 2; ++k) {
            if (slot_spawned(s, k) && slot_occupant_dying(s, k)) {
                mask |= static_cast<uint8_t>(1u << k);
            }
        }
        queue_torch_head_spawns(s, mi, mask);
        return;
    }
    if (move == kBuff) {
        // Case 3 (:141-149): the block FIRST (its a19 column already carries
        // the takeTurn-time +5), then the Strength template fanned over every
        // !isDead && !isDying && !isEscaping record INCLUDING this one, in
        // slot order -- header note (5).
        queue_monster_move_effect(s, mi, sts::registry::kTheCollector, kBuff,
                                  kBuffBlockStep, mi);
        for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
            // `!m.isDead && !m.isDying && !m.isEscaping` (:147) -- skips dying
            // and escaping, would INCLUDE a half-dead record: that is exactly
            // monster_basically_dead's complement (unreachable half-dead here,
            // spelled with the true predicate rather than an approximation).
            if (monster_basically_dead(s.monsters[i])) {
                continue;
            }
            queue_monster_move_effect(s, mi, sts::registry::kTheCollector,
                                      kBuff, kBuffStrengthStep, i);
        }
    } else if (move == kMegaDebuff) {
        // Case 4 (:152-159): the three debuffs, then `this.ultUsed = true` --
        // a takeTurn-time write, header note (3).
        queue_monster_move_effects(s, mi, sts::registry::kTheCollector, move);
        m.flags |= kMonsterFlagCollectorUltUsed;
    } else {
        queue_monster_move_effects(s, mi, sts::registry::kTheCollector, move);
    }
    // RollMoveAction (:177), OUTSIDE the switch. None of these moves inserts a
    // record, so the Collector's index is still mi.
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

void collector_die_after(CombatState& s, uint8_t mi) noexcept {
    // die() AFTER super.die() (:233-239): the Bronze Automaton's sweep,
    // verbatim -- one add_to_top SUICIDE (relicTrigger TRUE) per surviving
    // record, forward walk == reverse resolve order. A swept torch head runs
    // the full death edge; nothing in this fight holds a death power, and the
    // sweep is pinned by the shared boss-sweep test anyway.
    (void)mi;
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        if (s.monsters[i].hp <= 0) {
            continue;
        }
        ActionQueueItem sweep{};
        sweep.opcode = static_cast<uint16_t>(Opcode::SUICIDE);
        sweep.src = i;
        sweep.tgt = i;
        sweep.flags = 1u;  // die(relicTrigger = true)
        add_to_top(s, sweep);
    }
}

}  // namespace sts::engine
