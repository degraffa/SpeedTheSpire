// The Corrupt Heart: the isFirstMove early return, the moveCount % 3 cycle, the
// Strength negation and the buffCount ladder. See monster_corrupt_heart.hpp for
// provenance and the seven readings this body leans on.

#include "sts/engine/monster_corrupt_heart.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // move helpers, queue_monster_move_effect(s)
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kBloodShots = sts::registry::kCorruptHeartMoveBloodShots;  // 1
constexpr uint8_t kEchoAttack = sts::registry::kCorruptHeartMoveEchoAttack;  // 2
constexpr uint8_t kDebilitate = sts::registry::kCorruptHeartMoveDebilitate;  // 3
constexpr uint8_t kGainOneStrength =
    sts::registry::kCorruptHeartMoveGainOneStrength;  // 4

// The single authored 2-damage template of BLOOD_SHOTS, and the single authored
// `+2 Strength` template of GAIN_ONE_STRENGTH. Named so the coupling to
// monsters.yaml id 69 is visible from here.
constexpr uint8_t kBloodShotTemplateStep = 0;
constexpr uint8_t kStrengthTemplateStep = 0;

// PainfulStabsPower's 1-arg ctor sets `this.amount = -1`
// (PainfulStabsPower.java:29,131-138) and ApplyPowerAction's 3-arg form forwards
// it -- the Book of Stabbing's adjudication, restated because this is the second
// producer of the power (monster_book_of_stabbing.cpp:28).
constexpr int32_t kPainfulStabsAppliedAmount = -1;

void queue_apply_self(CombatState& s, uint8_t mi, PowerId id,
                      int32_t amount) noexcept {
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = mi;  // ApplyPowerAction(this, this, ...) -- its own source
    apply.tgt = mi;
    apply.amount = amount;
    apply.flags = make_apply_power_flags(id);
    add_to_bottom(s, apply);  // addToBottom, every site in this class
}

void queue_roll(CombatState& s, uint8_t mi) noexcept {
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

// `hasPower("Strength") && getPower("Strength").amount < 0
//      ? -getPower("Strength").amount : 0` (CorruptHeart.java:121-124) -- a live
// read of the ACTING MONSTER's own slot at QUEUE time. Header note (3).
[[nodiscard]] int32_t negated_strength_debuff(const CombatState& s,
                                              uint8_t mi) noexcept {
    const MonsterState& m = s.monsters[mi];
    for (uint8_t i = 0; i < m.power_count; ++i) {
        if (m.powers[i].power_id == static_cast<uint16_t>(PowerId::STRENGTH)) {
            const int32_t amt = m.powers[i].amount;
            return amt < 0 ? -amt : 0;
        }
    }
    return 0;
}

}  // namespace

void corrupt_heart_decide_move(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    if (corrupt_heart_first_move(m)) {
        // (:173-177). setMove FIRST, then clear the latch, then RETURN -- the
        // early return is what skips `++moveCount` at :199. Header note (1).
        set_monster_move(m, kDebilitate, MonsterIntent::STRONG_DEBUFF);
        corrupt_heart_set_first_move(m, false);
        return;
    }
    const uint32_t phase = corrupt_heart_move_count(m);
    if (phase == 0u) {
        // (:179-186). THE ONE ARM THAT SPENDS A SECOND ai_rng DRAW -- header
        // note (2). `true` takes BLOOD_SHOTS, `false` ECHO_ATTACK; the
        // setMove(..., base, count, isMultiDamage) overload at :181 differs from
        // :184 only in the intent NUMBERS it renders, which are presentation.
        if (random_boolean(s.ai_rng)) {
            set_monster_move(m, kBloodShots, MonsterIntent::ATTACK);
        } else {
            set_monster_move(m, kEchoAttack, MonsterIntent::ATTACK);
        }
    } else if (phase == 1u) {
        // (:187-194). `if (!this.lastMove(ECHO_ATTACK))` -> ECHO_ATTACK, else
        // BLOOD_SHOTS. No draw. Read BEFORE this decision's own setMove, so
        // move_history[0] is still the previous decision.
        if (!last_move_is(m, kEchoAttack)) {
            set_monster_move(m, kEchoAttack, MonsterIntent::ATTACK);
        } else {
            set_monster_move(m, kBloodShots, MonsterIntent::ATTACK);
        }
    } else {
        // (:195-197). Unconditional, no draw.
        set_monster_move(m, kGainOneStrength, MonsterIntent::BUFF);
    }
    corrupt_heart_set_move_count(m, phase + 1u);  // `++this.moveCount;` (:199)
}

void corrupt_heart_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m = MonsterState{};
    m.monster_id = static_cast<uint16_t>(MonsterId::CORRUPT_HEART);
    // The `super(...)` HP argument is the LITERAL 750 (:66) -- no draw -- and the
    // setHp chain (:72-76) is the ONE monster_hp_rng draw. Both arms are the
    // SINGLE-argument overload, which is literally setHp(hp, hp)
    // (AbstractMonster.java:777-779), so the range is degenerate and the draw
    // still happens (Random.java:58-61 counts and consumes at range 1). The
    // Nemesis / Act-3-boss shape; skipping it because min == max would
    // desynchronise monster_hp_rng for the rest of the run (s3-design §5 trap 4).
    const int32_t hp =
        random(s.monster_hp_rng,
               sts::registry::kCorruptHeart.hp_min(kMonsterAscension),
               sts::registry::kCorruptHeart.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    // The three field initialisers (:61-63), all written explicitly so a zeroed
    // record is not a plausible un-inited Heart (the Nemesis convention).
    corrupt_heart_set_first_move(m, true);
    corrupt_heart_set_move_count(m, 0u);
    corrupt_heart_set_buff_count(m, 0u);
    // init() -> rollMove -> getMove(aiRng.random(99)). The draw is real and the
    // `num` is DISCARDED by every arm, so the opener is DEBILITATE on every seed.
    (void)random(s.ai_rng, 99);
    corrupt_heart_decide_move(s, mi);
}

void corrupt_heart_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // (:88-103). TWO items, in this order, and NO ARTIFACT -- header note (6).
    // The A19 branch SUBTRACTS 100 from Invincible and pre-increments Beat of
    // Death, so A19+ is Invincible 200 / Beat of Death 2. No RNG.
    const int32_t invincible = kMonsterAscension >= 19
                                   ? kCorruptHeartInvincibleAmountA19
                                   : kCorruptHeartInvincibleAmount;
    const int32_t beat = kMonsterAscension >= 19
                             ? kCorruptHeartBeatOfDeathAmountA19
                             : kCorruptHeartBeatOfDeathAmount;
    // InvinciblePower's ctor sets BOTH `amount` and the private `maxAmt` to the
    // same argument (InvinciblePower.java:24-25). maxAmt has no POD home of its
    // own and rides PowerSlot.counter -- written here through the APPLY_POWER
    // COUNTER OPERAND (interp.hpp make_apply_power_flags), which op_apply_power's
    // new-slot path stores verbatim. That is the Bomb's operand, not Flight's
    // ctor-mirroring special case, because the value is known at the call site.
    ActionQueueItem inv{};
    inv.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    inv.src = mi;
    inv.tgt = mi;
    inv.amount = invincible;
    inv.flags = make_apply_power_flags(PowerId::INVINCIBLE, invincible);
    add_to_bottom(s, inv);
    queue_apply_self(s, mi, PowerId::BEAT_OF_DEATH, beat);
}

void corrupt_heart_take_turn(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];
    if (move == kDebilitate) {
        // (:108-119). EIGHT authored steps in addToBottom order: Vulnerable /
        // Weak / Frail 2 each, then the five status cards to RANDOM draw-pile
        // positions -- Dazed, Slimed, Wound, Burn, Void. Each MAKE_CARD
        // DRAW_RANDOM spends its own cardRandomRng draw against the pile size AS
        // IT THEN IS, which is why the five are five steps and not one step of
        // five. The whole program is data; nothing here is native.
        queue_monster_move_effects(s, mi, sts::registry::kCorruptHeart, move);
    } else if (move == kGainOneStrength) {
        // (:120-151). The two VFXActions (:125-126) are presentation.
        //
        // (a) The ALWAYS-+2 Strength, raised by any negative Strength the player
        //     has landed -- header note (3). The `+2` itself is the registry's
        //     authored step, read from the table rather than re-spelled here, so
        //     monsters.yaml stays the single source of the number and of the
        //     PowerId; only the runtime addend is native.
        const sts::registry::MonsterMove* mv =
            sts::registry::kCorruptHeart.move(move);
        if (mv != nullptr && mv->effect_count > kStrengthTemplateStep) {
            const sts::registry::MonsterMoveEffect& e =
                mv->effects[kStrengthTemplateStep];
            ActionQueueItem str{};
            str.opcode = static_cast<uint16_t>(e.op);
            str.src = mi;
            str.tgt = mi;  // MonsterMoveTarget::SELF
            str.amount = e.amount.at(kMonsterAscension) +
                         negated_strength_debuff(s, mi);
            str.flags = e.extra;  // APPLY_POWER: PowerId::STRENGTH
            add_to_bottom(s, str);
        }
        // (b) The buffCount ladder's SECOND item -- header note (4).
        const uint32_t rung = corrupt_heart_buff_count(m);
        if (rung == 0u) {
            queue_apply_self(s, mi, PowerId::ARTIFACT,
                             kCorruptHeartArtifactAmount);
        } else if (rung == 1u) {
            queue_apply_self(s, mi, PowerId::BEAT_OF_DEATH,
                             kCorruptHeartLadderBeatAmount);
        } else if (rung == 2u) {
            queue_apply_self(s, mi, PowerId::PAINFUL_STABS,
                             kPainfulStabsAppliedAmount);
        } else if (rung == 3u) {
            queue_apply_self(s, mi, PowerId::STRENGTH,
                             kCorruptHeartLadderStrength3);
        } else {
            queue_apply_self(s, mi, PowerId::STRENGTH,
                             kCorruptHeartLadderStrengthMax);
        }
        corrupt_heart_set_buff_count(m, rung + 1u);  // `++this.buffCount;` (:149)
    } else if (move == kBloodShots) {
        // (:152-162). The BloodShotEffect VFXAction (:154,:156) is presentation
        // and its Settings.FAST_MODE branch changes only the effect's duration.
        // Then `for (i = 0; i < bloodHitCount; ++i)` SEPARATE 4-arg DamageActions
        // on damage.get(1) -- header note (5).
        const int32_t hits = kMonsterAscension >= 4
                                 ? kCorruptHeartBloodHitCountA4
                                 : kCorruptHeartBloodHitCount;
        for (int32_t i = 0; i < hits; ++i) {
            queue_monster_move_effect(s, mi, sts::registry::kCorruptHeart, move,
                                      kBloodShotTemplateStep,
                                      kMoveTargetFromStep);
        }
    } else if (move == kEchoAttack) {
        // (:163-166). One DamageAction on damage.get(0); the ViceCrushEffect
        // VFXAction (:164) is presentation.
        queue_monster_move_effects(s, mi, sts::registry::kCorruptHeart, move);
    }
    // The RollMoveAction at :168 sits OUTSIDE the switch, so every case reaches
    // it -- including a move id no case matched, exactly as the Java's switch
    // with no default would.
    queue_roll(s, mi);
}

void corrupt_heart_roll_move(CombatState& s, uint8_t mi) noexcept {
    (void)random(s.ai_rng, 99);  // RollMoveAction -> rollMove: the draw is real
    corrupt_heart_decide_move(s, mi);
}

}  // namespace sts::engine
