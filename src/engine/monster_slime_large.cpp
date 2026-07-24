// Large slime native AI, split turn, and damage interrupt (B3.17). See
// monster_slime_large.hpp for provenance and the frozen split semantics.

#include "sts/engine/monster_slime_large.hpp"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kMove1 = 1;  // SLIME_TACKLE / FLAME_TACKLE
constexpr uint8_t kMove2 = 2;  // NORMAL_TACKLE (Acid L only)
constexpr uint8_t kMove3 = 3;  // SPLIT
constexpr uint8_t kMove4 = 4;  // WEAK_LICK / FRAIL_LICK

void init_fields(CombatState& s, uint8_t mi, MonsterId id,
                 int16_t hp) noexcept {
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(id);
    m.hp = hp;
    m.max_hp = hp;
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // this.powers.add(new SplitPower(this)) -- amount -1, display-only marker
    // (AcidSlime_L.java:94 / SpikeSlime_L.java:87; SplitPower.java:19-26).
    m.powers[0].power_id = static_cast<uint16_t>(PowerId::SPLIT);
    m.powers[0].amount = -1;
    m.power_count = 1;
}

void queue_roll_move(CombatState& s, uint8_t mi) noexcept {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    it.src = mi;
    it.tgt = mi;
    add_to_bottom(s, it);
}

// The SPLIT turn's queued action sequence (AcidSlime_L.java:127-136 /
// SpikeSlime_L.java:114-123; anims/HideHealthBar/SFX/Wait are presentation):
//   CannotLose -> Suicide(this, false) -> Spawn(left child) -> Spawn(right
//   child) -> CanLose.
// The children are CONSTRUCTED here (Java: `new XxxSlime_M(saveX -/+ 134, ...,
// 0, this.currentHealth)` runs at takeTurn time), so their HP is the parent's
// CURRENT hp captured NOW -- before the queued suicide zeroes it. Their init()
// aiRng roll runs when each SPAWN_MONSTER resolves (SpawnMonsterAction.java:48).
// Slots per smart positioning over the S1 solo-large-slime layout (see hpp):
// left child at the parent's slot mi (parent shifts to mi+1), right at mi+2.
void queue_split(CombatState& s, uint8_t mi, MonsterId child_id) noexcept {
    const int16_t child_hp = s.monsters[mi].hp;
    uint8_t left_slot = mi;
    uint8_t right_slot = static_cast<uint8_t>(mi + 2);

    // Acid L spawned by Slime Boss sits to the right of the dead boss:
    // Spike descendants (-519/-385/-251) < Acid-left (-14) < boss (0) <
    // Acid parent (120) < Acid-right (254). SpawnMonsterAction therefore
    // inserts Acid-left AT the dead boss's current slot, not at the Acid
    // parent's slot; after that insertion Acid-right appends at the new end.
    // The standalone Large Slime encounter has no boss record and keeps the
    // original mi/mi+2 derivation. This is the B3.20 child-chain positioning
    // case called out by B3.17's handoff.
    if (child_id == MonsterId::ACID_SLIME_MEDIUM) {
        for (uint8_t i = 0; i < s.monster_count; ++i) {
            if (s.monsters[i].monster_id ==
                    static_cast<uint16_t>(MonsterId::SLIME_BOSS) &&
                i < mi) {
                left_slot = i;
                right_slot = static_cast<uint8_t>(s.monster_count + 1);
                break;
            }
        }
    }

    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::CANNOT_LOSE);
    it.src = kActorPlayer;
    it.tgt = kActorPlayer;
    add_to_bottom(s, it);

    it = ActionQueueItem{};
    it.opcode = static_cast<uint16_t>(Opcode::SUICIDE);
    it.src = mi;
    it.tgt = mi;
    it.flags = 0;  // SuicideAction(this, false): NO relic/onDeath triggers
    add_to_bottom(s, it);

    it = ActionQueueItem{};
    it.opcode = static_cast<uint16_t>(Opcode::SPAWN_MONSTER);
    it.src = mi;
    it.tgt = left_slot;
    it.amount = child_hp;
    it.flags = static_cast<uint32_t>(child_id);
    add_to_bottom(s, it);

    it.tgt = right_slot;
    add_to_bottom(s, it);

    it = ActionQueueItem{};
    it.opcode = static_cast<uint16_t>(Opcode::CAN_LOSE);
    it.src = kActorPlayer;
    it.tgt = kActorPlayer;
    add_to_bottom(s, it);
}

}  // namespace

void spike_slime_large_roll_move(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    const int32_t num = random(s.ai_rng, 99);
    // SpikeSlime_L.getMove A17 branch (SpikeSlime_L.java:144-155). No boolean
    // tiebreaks in either branch of this class.
    if (num < 30) {
        if (last_two_moves_are(m, kMove1)) {
            set_monster_move(m, kMove4, MonsterIntent::DEBUFF);
        } else {
            set_monster_move(m, kMove1, MonsterIntent::ATTACK_DEBUFF);
        }
    } else if (last_move_is(m, kMove4)) {
        set_monster_move(m, kMove1, MonsterIntent::ATTACK_DEBUFF);
    } else {
        set_monster_move(m, kMove4, MonsterIntent::DEBUFF);
    }
}

void acid_slime_large_roll_move(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    const int32_t num = random(s.ai_rng, 99);
    // AcidSlime_L.getMove A17 branch (AcidSlime_L.java:156-185). The 0.6f/0.4f
    // tiebreaks are randomBoolean(float) == nextFloat() < chance draws.
    if (num < 40) {
        if (last_two_moves_are(m, kMove1)) {
            if (random_boolean(s.ai_rng, 0.6f)) {
                set_monster_move(m, kMove2, MonsterIntent::ATTACK);
            } else {
                set_monster_move(m, kMove4, MonsterIntent::DEBUFF);
            }
        } else {
            set_monster_move(m, kMove1, MonsterIntent::ATTACK_DEBUFF);
        }
    } else if (num < 70) {
        if (last_two_moves_are(m, kMove2)) {
            if (random_boolean(s.ai_rng, 0.6f)) {
                set_monster_move(m, kMove1, MonsterIntent::ATTACK_DEBUFF);
            } else {
                set_monster_move(m, kMove4, MonsterIntent::DEBUFF);
            }
        } else {
            set_monster_move(m, kMove2, MonsterIntent::ATTACK);
        }
    } else if (last_move_is(m, kMove4)) {
        if (random_boolean(s.ai_rng, 0.4f)) {
            set_monster_move(m, kMove1, MonsterIntent::ATTACK_DEBUFF);
        } else {
            set_monster_move(m, kMove2, MonsterIntent::ATTACK);
        }
    } else {
        set_monster_move(m, kMove4, MonsterIntent::DEBUFF);
    }
}

void spike_slime_large_init(CombatState& s, uint8_t mi) noexcept {
    const auto& def = sts::registry::kSpikeSlimeLarge;
    const int32_t hp = random(s.monster_hp_rng, def.hp_min(kMonsterAscension),
                              def.hp_max(kMonsterAscension));
    init_fields(s, mi, MonsterId::SPIKE_SLIME_LARGE, static_cast<int16_t>(hp));
    spike_slime_large_roll_move(s, mi);  // init() -> rollMove
}

void acid_slime_large_init(CombatState& s, uint8_t mi) noexcept {
    const auto& def = sts::registry::kAcidSlimeLarge;
    const int32_t hp = random(s.monster_hp_rng, def.hp_min(kMonsterAscension),
                              def.hp_max(kMonsterAscension));
    init_fields(s, mi, MonsterId::ACID_SLIME_LARGE, static_cast<int16_t>(hp));
    acid_slime_large_roll_move(s, mi);
}

void spike_slime_large_spawn_at_hp(CombatState& s, uint8_t mi,
                                   int16_t hp) noexcept {
    // 4-arg ctor: maxHealth = currentHealth = newHealth, NO monsterHpRng draw
    // (SpikeSlime_L.java:77-78; AbstractMonster.java:139,150); then init().
    init_fields(s, mi, MonsterId::SPIKE_SLIME_LARGE, hp);
    spike_slime_large_roll_move(s, mi);
}

void acid_slime_large_spawn_at_hp(CombatState& s, uint8_t mi,
                                  int16_t hp) noexcept {
    init_fields(s, mi, MonsterId::ACID_SLIME_LARGE, hp);
    acid_slime_large_roll_move(s, mi);
}

void spike_slime_large_take_turn(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];
    uint8_t roll_tgt = mi;
    if (move == kMove3) {
        queue_split(s, mi, MonsterId::SPIKE_SLIME_MEDIUM);
        // this.setMove(SPLIT_NAME, 3, UNKNOWN) at the end of the split case
        // (SpikeSlime_L.java:124) -- synchronous, before the queue drains.
        set_monster_move(m, kMove3, MonsterIntent::UNKNOWN);
        // The unconditional trailing RollMoveAction (:127) resolves AFTER the
        // spawns have shifted the dead parent from mi to mi+1 (left child
        // inserted at mi), so the queued index is pre-computed. It still rolls
        // -- and draws aiRng -- on the dead parent (RollMoveAction.java:17-21
        // has no liveness check).
        roll_tgt = static_cast<uint8_t>(mi + 1);
    } else {
        queue_monster_move_effects(s, mi, sts::registry::kSpikeSlimeLarge, move);
    }
    // SpikeSlime_L.takeTurn queues ONE RollMoveAction after the switch (:127),
    // for every case including SPLIT.
    queue_roll_move(s, roll_tgt);
}

void acid_slime_large_take_turn(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];
    if (move == kMove3) {
        queue_split(s, mi, MonsterId::ACID_SLIME_MEDIUM);
        // setMove(SPLIT_NAME, 3, UNKNOWN) (AcidSlime_L.java:137). Unlike Spike
        // L, the split case queues NO RollMoveAction (:127-138).
        set_monster_move(m, kMove3, MonsterIntent::UNKNOWN);
        return;
    }
    queue_monster_move_effects(s, mi, sts::registry::kAcidSlimeLarge, move);
    // Cases 1/2/4 each queue their own RollMoveAction (:110,118,124).
    queue_roll_move(s, mi);
}

void large_slime_on_damaged(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    // damage() override, AFTER super.damage() (AcidSlime_L.java:142-152 /
    // SpikeSlime_L.java:130-140):
    //   if (!isDying && (float)currentHealth <= (float)maxHealth / 2.0f
    //       && nextMove != 3 && !splitTriggered)
    // int16 HP converts to float exactly and maxHealth/2.0f is an exact halving,
    // so the float comparison is exactly 2*hp <= max_hp in integers.
    if (m.hp <= 0) {
        return;  // isDying: a lethal hit never re-telegraphs the split
    }
    if (2 * static_cast<int32_t>(m.hp) > static_cast<int32_t>(m.max_hp)) {
        return;
    }
    if (m.move_history[0] == kMove3) {
        return;  // nextMove already SPLIT
    }
    if ((m.flags & kMonsterFlagSplitTriggered) != 0) {
        return;  // one-shot latch
    }
    // Synchronous setMove(SPLIT, UNKNOWN) + createIntent (:146-147), THEN a
    // queued SetMoveAction at the queue bottom (:149) that re-asserts SPLIT
    // after any already-queued RollMoveAction (own-turn thorns interrupt)
    // rolls -- and draws aiRng -- over it.
    set_monster_move(m, kMove3, MonsterIntent::UNKNOWN);
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::SET_MOVE);
    it.src = mi;
    it.tgt = mi;
    it.amount = kMove3;
    it.flags = static_cast<uint32_t>(MonsterIntent::UNKNOWN);
    add_to_bottom(s, it);
    m.flags = static_cast<uint16_t>(m.flags | kMonsterFlagSplitTriggered);
}

}  // namespace sts::engine
