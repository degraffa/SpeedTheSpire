// Donu and Deca: the out-of-phase alternation, the two unguarded group walks and
// Deca's ascension-switched telegraph. See monster_donu_deca.hpp for provenance,
// the move-id-0 collision and why the walks stay literal.

#include "sts/engine/monster_donu_deca.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effect(s), kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kDonuBeam = sts::registry::kDonuMoveBeam;                  // 0
constexpr uint8_t kDonuCircle = sts::registry::kDonuMoveCircleOfProtection;  // 2
constexpr uint8_t kDecaBeam = sts::registry::kDecaMoveBeam;                  // 0
constexpr uint8_t kDecaSquare = sts::registry::kDecaMoveSquareOfProtection;  // 2

// The single authored template step of each protect move, plus Deca's A19-only
// second one. Named so the coupling to monsters.yaml ids 64/65 is visible here.
constexpr uint8_t kProtectTemplateStep = 0;
constexpr uint8_t kDecaPlatedArmorStep = 1;

// ArtifactPower(this, asc >= 19 ? 3 : 2) -- identical in both classes
// (Donu.java:94-98, Deca.java:101-105). ARTIFACT_AMT 2 (Donu :44, Deca :48).
constexpr int32_t kArtifactAmount = 2;
constexpr int32_t kArtifactAmountA19 = 3;

[[nodiscard]] bool is_attacking(const MonsterState& m) noexcept {
    return (m.flags & kMonsterFlagShapeAttacking) != 0u;
}

void queue_artifact(CombatState& s, uint8_t mi) noexcept {
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = mi;
    apply.tgt = mi;
    apply.amount = kMonsterAscension >= 19 ? kArtifactAmountA19 : kArtifactAmount;
    apply.flags = make_apply_power_flags(PowerId::ARTIFACT);
    add_to_bottom(s, apply);
}

void queue_roll(CombatState& s, uint8_t mi) noexcept {
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

// The shared ctor tail of both classes: one degenerate monster_hp_rng draw, then
// the init rollMove. `attacking` is the field initialiser, which is the ONLY
// difference between the two ctors that matters -- Deca true, Donu false -- and
// is what puts the pair permanently out of phase.
void shape_init(CombatState& s, uint8_t mi, MonsterId id,
                const sts::registry::MonsterDef& def, bool attacking) noexcept {
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(id);
    // setHp(265)/setHp(250): the single-arg overload, i.e. setHp(hp, hp)
    // (AbstractMonster.java:777-779), whose two-arg body draws unconditionally
    // even over a degenerate range. Fixed HP, real stream movement.
    const int32_t rolled = random(s.monster_hp_rng, def.hp_min(kMonsterAscension),
                                  def.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(rolled);
    m.max_hp = m.hp;
    m.block = 0;
    m.flags = attacking ? kMonsterFlagShapeAttacking : 0u;
    m.power_count = 0;
    m.pad0 = 0;  // unused by either
    m.draw_x = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
}

}  // namespace

// --- Donu ---------------------------------------------------------------------

void donu_decide_move(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    if (is_attacking(m)) {
        set_monster_move(m, kDonuBeam, MonsterIntent::ATTACK);  // (:127)
        return;
    }
    // Intent.BUFF (:129) -- NOT DEFEND_DEBUFF. The dispatching brief said
    // otherwise; the tree was re-read before MonsterIntent 15 was spent on the
    // Time Eater's Ripple instead.
    set_monster_move(m, kDonuCircle, MonsterIntent::BUFF);
}

void donu_init(CombatState& s, uint8_t mi) noexcept {
    shape_init(s, mi, MonsterId::DONU, sts::registry::kDonu, /*attacking=*/false);
    // `this.isAttacking = false;` (:70), then AbstractMonster.init -> rollMove ->
    // getMove(aiRng.random(99)). The draw is spent and the VALUE IS IGNORED, so
    // Donu's opener is always CIRCLE OF PROTECTION.
    (void)random(s.ai_rng, 99);
    donu_decide_move(s, mi);
}

void donu_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    queue_artifact(s, mi);  // (:93-99). No BGM lines here; Deca's carries them.
}

void donu_roll_move(CombatState& s, uint8_t mi) noexcept {
    (void)random(s.ai_rng, 99);  // RollMoveAction -> rollMove: the draw is real
    donu_decide_move(s, mi);
}

void donu_take_turn(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];
    if (move == kDonuCircle) {
        // (:113-118). The per-member SFXAction at :115 is presentation. UNGUARDED:
        // one item per monster RECORD, in slot order, INCLUDING ITSELF and
        // including corpses -- ApplyPowerAction's own isDeadOrEscaped early-out
        // rejects those at resolve time (op_apply_power).
        for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
            queue_monster_move_effect(s, mi, sts::registry::kDonu, move,
                                      kProtectTemplateStep, i);
        }
        m.flags |= kMonsterFlagShapeAttacking;  // `isAttacking = true;` (:118)
    } else {
        // BEAM (:105-111): two flat damage steps, the whole program.
        queue_monster_move_effects(s, mi, sts::registry::kDonu, move);
        m.flags &= ~kMonsterFlagShapeAttacking;  // `isAttacking = false;` (:110)
    }
    queue_roll(s, mi);  // RollMoveAction (:121), OUTSIDE the switch
}

// --- Deca ---------------------------------------------------------------------

void deca_decide_move(CombatState& s, uint8_t mi, int32_t ascension) noexcept {
    MonsterState& m = s.monsters[mi];
    if (is_attacking(m)) {
        set_monster_move(m, kDecaBeam, MonsterIntent::ATTACK_DEBUFF);  // (:137)
        return;
    }
    // THE ASCENSION-SWITCHED TELEGRAPH (:139-141). Both arms are written, not
    // just the live one, because writing only the A19 arm would hide that the
    // difference exists -- and because the registry row can carry only one intent
    // per move, so this is where the fact lives.
    set_monster_move(m, kDecaSquare,
                     ascension >= 19 ? MonsterIntent::DEFEND_BUFF
                                     : MonsterIntent::DEFEND);
}

void deca_init(CombatState& s, uint8_t mi) noexcept {
    shape_init(s, mi, MonsterId::DECA, sts::registry::kDeca, /*attacking=*/true);
    // `this.isAttacking = true;` (:74) -- the OPPOSITE of Donu's initialiser, and
    // the whole reason the pair alternates out of phase. The init draw is spent
    // and ignored, so Deca's opener is always BEAM.
    (void)random(s.ai_rng, 99);
    deca_decide_move(s, mi, kMonsterAscension);
}

void deca_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // (:97-108). The unsilenceBGM / fadeOutAmbiance / playBgmInstantly lines and
    // the UnlockTracker are presentation -- Deca carries them and Donu does not,
    // which is the pair's only pre-battle asymmetry.
    queue_artifact(s, mi);
}

void deca_roll_move(CombatState& s, uint8_t mi) noexcept {
    (void)random(s.ai_rng, 99);
    deca_decide_move(s, mi, kMonsterAscension);
}

void deca_take_turn(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];
    if (move == kDecaSquare) {
        // (:122-128). UNGUARDED, and the two steps INTERLEAVE PER MEMBER because
        // the A19 Plated Armor sits inside the same loop body as the block --
        // block(0), armor(0), block(1), armor(1), not all blocks then all armors.
        const bool a19 = kMonsterAscension >= 19;
        for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
            queue_monster_move_effect(s, mi, sts::registry::kDeca, move,
                                      kProtectTemplateStep, i);
            if (a19) {
                queue_monster_move_effect(s, mi, sts::registry::kDeca, move,
                                          kDecaPlatedArmorStep, i);
            }
        }
        m.flags |= kMonsterFlagShapeAttacking;  // `isAttacking = true;` (:128)
    } else {
        // BEAM (:113-120): two damage steps then two Dazed into the discard.
        queue_monster_move_effects(s, mi, sts::registry::kDeca, move);
        m.flags &= ~kMonsterFlagShapeAttacking;  // `isAttacking = false;` (:119)
    }
    queue_roll(s, mi);  // RollMoveAction (:131), OUTSIDE the switch
}

}  // namespace sts::engine
