// Small/medium slime native AI and turns (B3.14). See monster_slime.hpp for
// provenance and exact per-stream draw accounting.

#include "sts/engine/monster_slime.hpp"

#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kMove1 = 1;
constexpr uint8_t kMove2 = 2;
constexpr uint8_t kMove4 = 4;

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

void spike_small_roll(CombatState& s, MonsterState& m) noexcept {
    // rollMove always draws num even though SpikeSlime_S.getMove ignores it.
    (void)random(s.ai_rng, 99);
    set_monster_move(m, kMove1, MonsterIntent::ATTACK);
}

void spike_medium_roll(CombatState& s, MonsterState& m) noexcept {
    const int32_t num = random(s.ai_rng, 99);
    // SpikeSlime_M.getMove A17 branch (A20).
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

void acid_small_initial_roll(CombatState& s, MonsterState& m) noexcept {
    // A17 getMove ignores num. Empty history is not lastTwoMoves(TACKLE), so the
    // first intent is LICK. Later takeTurn calls setMove directly (no RollMove).
    (void)random(s.ai_rng, 99);
    set_monster_move(m, kMove2, MonsterIntent::DEBUFF);
}

void acid_medium_roll(CombatState& s, MonsterState& m) noexcept {
    const int32_t num = random(s.ai_rng, 99);
    // AcidSlime_M.getMove A17 branch. Preserve the source's different boolean
    // APIs: the first tie uses nextBoolean(), while 0.5/0.4 use nextFloat().
    if (num < 40) {
        if (last_two_moves_are(m, kMove1)) {
            if (random_boolean(s.ai_rng)) {
                set_monster_move(m, kMove2, MonsterIntent::ATTACK);
            } else {
                set_monster_move(m, kMove4, MonsterIntent::DEBUFF);
            }
        } else {
            set_monster_move(m, kMove1, MonsterIntent::ATTACK_DEBUFF);
        }
    } else if (num < 80) {
        if (last_two_moves_are(m, kMove2)) {
            if (random_boolean(s.ai_rng, 0.5f)) {
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

}  // namespace

void spike_slime_small_init(CombatState& s, uint8_t mi) noexcept {
    init_common(s, mi, MonsterId::SPIKE_SLIME_SMALL,
                sts::registry::kSpikeSlimeSmall);
    spike_small_roll(s, s.monsters[mi]);
}

void spike_slime_medium_init(CombatState& s, uint8_t mi) noexcept {
    init_common(s, mi, MonsterId::SPIKE_SLIME_MEDIUM,
                sts::registry::kSpikeSlimeMedium);
    spike_medium_roll(s, s.monsters[mi]);
}

void acid_slime_small_init(CombatState& s, uint8_t mi) noexcept {
    init_common(s, mi, MonsterId::ACID_SLIME_SMALL,
                sts::registry::kAcidSlimeSmall);
    acid_small_initial_roll(s, s.monsters[mi]);
}

void acid_slime_medium_init(CombatState& s, uint8_t mi) noexcept {
    init_common(s, mi, MonsterId::ACID_SLIME_MEDIUM,
                sts::registry::kAcidSlimeMedium);
    acid_medium_roll(s, s.monsters[mi]);
}

namespace {
// B3.17 split-spawn seam (see monster_slime.hpp): the 4-arg ctor sets
// hp = max_hp = newHealth with NO monster_hp_rng draw (AcidSlime_M.java:65-66 /
// SpikeSlime_M.java:60-61 -> AbstractMonster.java:139,150); init() then does
// the single aiRng rollMove (AbstractMonster.java:712-715).
void spawn_fields_at_hp(CombatState& s, uint8_t mi, MonsterId id,
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
}
}  // namespace

void spike_slime_medium_spawn_at_hp(CombatState& s, uint8_t mi,
                                    int16_t hp) noexcept {
    spawn_fields_at_hp(s, mi, MonsterId::SPIKE_SLIME_MEDIUM, hp);
    spike_medium_roll(s, s.monsters[mi]);
}

void acid_slime_medium_spawn_at_hp(CombatState& s, uint8_t mi,
                                   int16_t hp) noexcept {
    spawn_fields_at_hp(s, mi, MonsterId::ACID_SLIME_MEDIUM, hp);
    acid_medium_roll(s, s.monsters[mi]);
}

void spike_slime_small_take_turn(CombatState& s, uint8_t mi) noexcept {
    queue_monster_move_effects(s, mi, sts::registry::kSpikeSlimeSmall,
                               s.monsters[mi].move_history[0]);
    spike_small_roll(s, s.monsters[mi]);
}

void spike_slime_medium_take_turn(CombatState& s, uint8_t mi) noexcept {
    queue_monster_move_effects(s, mi, sts::registry::kSpikeSlimeMedium,
                               s.monsters[mi].move_history[0]);
    spike_medium_roll(s, s.monsters[mi]);
}

void acid_slime_small_take_turn(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];
    queue_monster_move_effects(s, mi, sts::registry::kAcidSlimeSmall, move);
    // AcidSlime_S.takeTurn sets the other move directly; no RollMoveAction and
    // therefore no ai_rng draw after the initial getMove.
    if (move == kMove1) {
        set_monster_move(m, kMove2, MonsterIntent::DEBUFF);
    } else {
        set_monster_move(m, kMove1, MonsterIntent::ATTACK);
    }
}

void acid_slime_medium_take_turn(CombatState& s, uint8_t mi) noexcept {
    queue_monster_move_effects(s, mi, sts::registry::kAcidSlimeMedium,
                               s.monsters[mi].move_history[0]);
    acid_medium_roll(s, s.monsters[mi]);
}

}  // namespace sts::engine
