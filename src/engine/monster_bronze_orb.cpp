// The Bronze Orb: native move selection (the decision-time usedStasis latch),
// the boss-targeted SUPPORT_BEAM retarget and the APPLY_STASIS move. See
// monster_bronze_orb.hpp for provenance and the three readings.

#include "sts/engine/monster_bronze_orb.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effect(s), move helpers
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kBeam = sts::registry::kBronzeOrbMoveBeam;                // 1
constexpr uint8_t kSupportBeam = sts::registry::kBronzeOrbMoveSupportBeam;  // 2
constexpr uint8_t kStasis = sts::registry::kBronzeOrbMoveStasis;            // 3

// The SUPPORT_BEAM row's single template step (monsters.yaml id 41): the BLOCK
// the module retargets at the boss. Named so the coupling to the row is
// visible (the Healer kTemplateStep precedent).
constexpr uint8_t kSupportBeamBlockStep = 0;

[[nodiscard]] bool used_stasis(const MonsterState& m) noexcept {
    return (m.flags & kMonsterFlagBronzeOrbUsedStasis) != 0u;
}

void queue_roll_move(CombatState& s, uint8_t mi) noexcept {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    it.src = mi;
    it.tgt = mi;
    add_to_bottom(s, it);
}

}  // namespace

void bronze_orb_decide_move(CombatState& s, uint8_t mi, int32_t num) noexcept {
    MonsterState& m = s.monsters[mi];
    // getMove (:86-101).
    if (!used_stasis(m) && num >= 25) {
        // (:87-91): the latch flips AT DECISION TIME -- header note (1).
        set_monster_move(m, kStasis, MonsterIntent::STRONG_DEBUFF);
        m.flags |= kMonsterFlagBronzeOrbUsedStasis;
        return;
    }
    if (num >= 70 && !last_two_moves_are(m, kSupportBeam)) {
        set_monster_move(m, kSupportBeam, MonsterIntent::DEFEND);  // (:92-95)
        return;
    }
    if (!last_two_moves_are(m, kBeam)) {
        set_monster_move(m, kBeam, MonsterIntent::ATTACK);  // (:96-99)
        return;
    }
    set_monster_move(m, kSupportBeam, MonsterIntent::DEFEND);  // (:100)
}

void bronze_orb_init(CombatState& s, uint8_t mi) noexcept {
    // The full ctor: the super(...) argument monsterHpRng.random(52, 58) (:48,
    // flat -- the registry SUPER_ARG_HP row), its value discarded, then the
    // tiered setHp (:49-53). Unreachable from any encounter today; kept exact
    // so the init-fn table's claim holds (header).
    MonsterState& m = s.monsters[mi];
    m = MonsterState{};
    m.monster_id = static_cast<uint16_t>(MonsterId::BRONZE_ORB);
    const auto& def = sts::registry::kBronzeOrb;
    const sts::registry::MonsterRollDef* super_arg = def.roll(0);
    if (super_arg != nullptr) {
        (void)random(s.monster_hp_rng, super_arg->min(kMonsterAscension),
                     super_arg->max(kMonsterAscension));
    }
    const int32_t hp = random(s.monster_hp_rng, def.hp_min(kMonsterAscension),
                              def.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = m.hp;
    bronze_orb_decide_move(s, mi, random(s.ai_rng, 99));
}

void bronze_orb_spawn_at_hp(CombatState& s, uint8_t mi, int16_t hp) noexcept {
    // SpawnMonsterAction.update -> init(): the record exists (the spawn path
    // wrote id/draw_x), HP arrived pre-drawn, and the ONE ai_rng roll decides
    // the opener -- header note (3).
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::BRONZE_ORB);
    m.hp = hp;
    m.max_hp = hp;
    bronze_orb_decide_move(s, mi, random(s.ai_rng, 99));
}

void bronze_orb_roll_move(CombatState& s, uint8_t mi) noexcept {
    bronze_orb_decide_move(s, mi, random(s.ai_rng, 99));
}

void bronze_orb_take_turn(CombatState& s, uint8_t mi) noexcept {
    const uint8_t move = s.monsters[mi].move_history[0];
    if (move == kSupportBeam) {
        // GainBlockAction(getMonster("BronzeAutomaton"), this, 12) (:69): the
        // FIRST record with the boss's id, dead records included
        // (MonsterGroup.java:108-115) -- header note (2). The template step is
        // retargeted at that slot; with no boss record at all (unreachable:
        // nothing else spawns orbs) the Java would NPE and this body queues
        // nothing, the documented divergence for an impossible state.
        uint8_t boss = kMonsterCap;
        for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
            if (s.monsters[i].monster_id ==
                static_cast<uint16_t>(MonsterId::BRONZE_AUTOMATON)) {
                boss = i;
                break;
            }
        }
        if (boss < kMonsterCap) {
            queue_monster_move_effect(s, mi, sts::registry::kBronzeOrb,
                                      kSupportBeam, kSupportBeamBlockStep,
                                      boss);
        }
    } else {
        // BEAM's damage, or STASIS's authored APPLY_STASIS step (opcode 71 --
        // the queue helper stamps src = mi, the future power owner).
        queue_monster_move_effects(s, mi, sts::registry::kBronzeOrb, move);
    }
    queue_roll_move(s, mi);  // RollMoveAction (:76), outside the switch
}

}  // namespace sts::engine
