// Slime Boss native AI and split machinery (B3.20). See the public header for
// the full Java provenance and queue/RNG contract.

#include "sts/engine/monster_slime_boss.hpp"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kSlam = 1;
constexpr uint8_t kPrepSlam = 2;
constexpr uint8_t kSplit = 3;
constexpr uint8_t kSticky = 4;

void queue_boss_split(CombatState& s, uint8_t mi) noexcept {
    // The two 4-arg child constructors run at takeTurn time, so both capture
    // the boss's CURRENT HP before SuicideAction zeroes it. Their init() rolls
    // happen later when SPAWN_MONSTER resolves.
    const int16_t child_hp = s.monsters[mi].hp;

    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::CANNOT_LOSE);
    it.src = kActorPlayer;
    it.tgt = kActorPlayer;
    add_to_bottom(s, it);

    it = ActionQueueItem{};
    it.opcode = static_cast<uint16_t>(Opcode::SUICIDE);
    it.src = mi;
    it.tgt = mi;
    it.flags = 0;  // SuicideAction(this, false)
    add_to_bottom(s, it);

    // Smart-positioning derivation from the explicit Java coordinates:
    //   Spike L -385 < boss 0 < Acid L +120.
    // Spike inserts at the boss's current slot (shifting the dead boss right);
    // Acid then inserts after both records. Indices are pre-computed because
    // this engine stores indices where Java actions retain object references.
    const uint8_t spike_slot = mi;
    const uint8_t acid_slot = static_cast<uint8_t>(mi + 2);

    it = ActionQueueItem{};
    it.opcode = static_cast<uint16_t>(Opcode::SPAWN_MONSTER);
    it.src = mi;
    it.tgt = spike_slot;
    it.amount = child_hp;
    it.flags = static_cast<uint32_t>(MonsterId::SPIKE_SLIME_LARGE);
    add_to_bottom(s, it);

    it.tgt = acid_slot;
    it.flags = static_cast<uint32_t>(MonsterId::ACID_SLIME_LARGE);
    add_to_bottom(s, it);

    it = ActionQueueItem{};
    it.opcode = static_cast<uint16_t>(Opcode::CAN_LOSE);
    it.src = kActorPlayer;
    it.tgt = kActorPlayer;
    add_to_bottom(s, it);
}

}  // namespace

void slime_boss_init(CombatState& s, uint8_t mi) noexcept {
    const auto& def = sts::registry::kSlimeBoss;
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::SLIME_BOSS);
    // SlimeBoss uses setHp(int), not setHp(min,max): no monsterHpRng draw.
    m.hp = static_cast<int16_t>(def.hp_min(kMonsterAscension));
    m.max_hp = m.hp;
    m.block = 0;
    m.flags = 0;
    m.power_count = 1;
    m.pad0 = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    m.powers[0].power_id = static_cast<uint16_t>(PowerId::SPLIT);
    m.powers[0].amount = -1;

    // AbstractMonster.init -> rollMove -> aiRng.random(99), then
    // SlimeBoss.getMove ignores `num` and forces STICKY on firstTurn.
    (void)random(s.ai_rng, 99);
    set_monster_move(m, kSticky, MonsterIntent::STRONG_DEBUFF);
}

void slime_boss_take_turn(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    switch (m.move_history[0]) {
        case kSticky:
            queue_monster_move_effects(s, mi, sts::registry::kSlimeBoss,
                                       kSticky);
            set_monster_move(m, kPrepSlam, MonsterIntent::UNKNOWN);
            return;
        case kPrepSlam:
            // Only presentation actions precede this direct setMove.
            set_monster_move(m, kSlam, MonsterIntent::ATTACK);
            return;
        case kSlam:
            queue_monster_move_effects(s, mi, sts::registry::kSlimeBoss,
                                       kSlam);
            set_monster_move(m, kSticky, MonsterIntent::STRONG_DEBUFF);
            return;
        case kSplit:
            queue_boss_split(s, mi);
            // SlimeBoss.takeTurn's split case re-pushes SPLIT synchronously.
            set_monster_move(m, kSplit, MonsterIntent::UNKNOWN);
            return;
        default:
            return;
    }
}

void slime_boss_on_damaged(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    // SlimeBoss.damage, after super.damage:
    // !isDying && currentHealth <= maxHealth/2 && nextMove != SPLIT.
    if (m.hp <= 0 ||
        2 * static_cast<int32_t>(m.hp) > static_cast<int32_t>(m.max_hp) ||
        m.move_history[0] == kSplit) {
        return;
    }
    set_monster_move(m, kSplit, MonsterIntent::UNKNOWN);
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::SET_MOVE);
    it.src = mi;
    it.tgt = mi;
    it.amount = kSplit;
    it.flags = static_cast<uint32_t>(MonsterIntent::UNKNOWN);
    add_to_bottom(s, it);
}

}  // namespace sts::engine
