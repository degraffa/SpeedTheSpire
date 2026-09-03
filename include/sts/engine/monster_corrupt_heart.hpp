#pragma once

// The Corrupt Heart (registry/monsters.yaml id 69, MonsterId::CORRUPT_HEART) --
// the game's final boss and the only member of the Act-4 `The Heart` encounter
// (encounters.yaml 63, MonsterHelper.java:596-598). CorruptHeart.java, 212
// lines, read in full.
//
// SEVEN READINGS THIS MODULE LEANS ON:
//
// (1) THE OPENING DEBILITATE DOES NOT ADVANCE THE CYCLE. getMove (:171-200)
//     opens with
//         if (this.isFirstMove) { this.setMove(DEBILITATE, STRONG_DEBUFF);
//                                 this.isFirstMove = false; return; }
//     -- an EARLY RETURN, so `++this.moveCount` at :199 is skipped. It is the
//     only selection in the roster shaped that way, and a body that increments
//     unconditionally puts the whole `moveCount % 3` cycle one step out for the
//     rest of the fight (every ECHO becomes a BLOOD_SHOTS and every buff turn
//     arrives one turn early). The opener therefore costs the init rollMove's
//     `aiRng.random(99)` and NOTHING more -- no randomBoolean.
//
// (2) THE CYCLE'S PER-TURN ai_rng COST IS 1 OR 2 DRAWS AND DEPENDS ON THE ARM.
//     Every roll spends AbstractMonster.rollMove's `aiRng.random(99)`
//     (AbstractMonster.java:465-467) even though getMove ignores `num`; on top
//     of that, `moveCount % 3 == 0` spends ONE MORE -- `aiRng.randomBoolean()`
//     (:180) -- choosing between BLOOD_SHOTS and ECHO_ATTACK. Case 1 reads
//     `!lastMove(ECHO_ATTACK)` (:188) and case 2 is unconditional, so neither
//     draws. Losing or adding that conditional draw desynchronises the shared ai
//     stream for the rest of the combat.
//
// (3) THE BUFF MOVE NEGATES A NEGATIVE STRENGTH BEFORE ADDING ITS +2.
//     takeTurn case GAIN_ONE_STRENGTH (:120-151) computes
//         additionalAmount = hasPower("Strength") && getPower("Strength").amount < 0
//                            ? -getPower("Strength").amount : 0
//     and queues `StrengthPower(this, additionalAmount + 2)` (:127). So a
//     Shockwave / Piercing Wail is UNDONE rather than merely out-paced: the
//     Heart comes back to +2 net, not to (debuff + 2). The read is of the
//     acting monster's OWN live power at QUEUE time -- before any of the items
//     this turn queues resolve -- which is why it cannot be a registry column
//     and why this module reads the slot itself.
//
// (4) THE buffCount LADDER IS A SECOND QUEUED ITEM, NOT A REPLACEMENT.
//     The switch at :128-148 queues ONE MORE ApplyPowerAction after the Strength
//     one, then `++buffCount` (:149):
//         0 -> ArtifactPower(this, 2)        1 -> BeatOfDeathPower(this, 1)
//         2 -> PainfulStabsPower(this)       3 -> StrengthPower(this, 10)
//         default -> StrengthPower(this, 50), FOREVER
//     Rung 1 is why Beat of Death's additive stacking matters (powers.yaml 138):
//     the pre-battle 1 -- 2 at A19+ -- climbs. Rung 2 makes the Heart a SECOND
//     producer of Painful Stabs (powers.yaml 97, the Book of Stabbing's), at the
//     1-arg ctor's amount -1.
//
// (5) BLOOD_SHOTS IS bloodHitCount SEPARATE DamageActions, and the count is
//     ASCENSION-VARYING: 12, 15 from A4 (:80,:84). Separate hits are the whole
//     point at 2 damage each -- block, Vulnerable, the Heart's Strength ramp and
//     (once rung 2 has landed) Painful Stabs' one Wound PER LANDED HIT all apply
//     twelve or fifteen times. An effect list carries per-tier AMOUNTS, not
//     per-tier COUNTS, so monsters.yaml authors ONE 2-damage template and this
//     module emits it N times -- the Book of Stabbing's shape.
//
// (6) NO ARTIFACT AT BATTLE START, and the A19 branch SUBTRACTS.
//     usePreBattleAction (:88-103) queues exactly two items:
//     `InvinciblePower(this, invincibleAmt)` then
//     `BeatOfDeathPower(this, beatAmount)`, where `invincibleAmt = 300` and
//     `invincibleAmt -= 100` at A19+ (:93-96) while `beatAmount = 1` and
//     `++beatAmount` at A19+ (:97-100). So the A19 column of Invincible is the
//     SMALLER number, which is the one that reads like a typo and is not. The
//     Artifact the Heart eventually holds comes from the buff ladder's rung 0,
//     not from here. The first three lines of the method (:90-92) are music.
//
// (7) die() IS POST-super AND IS THE RUN'S EDGE, NOT A COMBAT EFFECT.
//     `if (!getCurrRoom().cannotLose) { super.die(); removeListener(...);
//      onBossVictoryLogic(); onFinalBossVictoryLogic(); stopClock = true; }`
//     (:202-211). Everything after super.die() is achievements, the StatsScreen
//     and the wall clock; the sim-visible true-victory edge is the RUN layer's
//     and landed with S3.33 (the Act-4 TrueVictoryRoom terminal), together with
//     the Heart kill's surviving `miscRng.random(-5,5)` gold draw (s3-design §5
//     trap 5). So this type registers an explicit nullptr in
//     monster_die_after_fn rather than a body -- a recorded reading, not an
//     omission.

#include <cstdint>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// --- usePreBattleAction constants (CorruptHeart.java:93-100) -----------------
// Neither number has a monsters.yaml column: they are pre-battle POWER amounts,
// not move-effect amounts, and the row records that they live here.
inline constexpr int32_t kCorruptHeartInvincibleAmount = 300;      // (:93)
inline constexpr int32_t kCorruptHeartInvincibleAmountA19 = 200;   // (:94-96)
inline constexpr int32_t kCorruptHeartBeatOfDeathAmount = 1;       // (:97)
inline constexpr int32_t kCorruptHeartBeatOfDeathAmountA19 = 2;    // (:98-100)

// --- bloodHitCount (CorruptHeart.java:80,84) --------------------------------
// A per-tier step COUNT, which an effect list cannot express -- header note (5).
inline constexpr int32_t kCorruptHeartBloodHitCount = 12;
inline constexpr int32_t kCorruptHeartBloodHitCountA4 = 15;

// --- the buff ladder (CorruptHeart.java:121-148) ----------------------------
// The always-added Strength is the registry's authored step; these are the
// ladder's own numbers, which are not authored anywhere else.
inline constexpr int32_t kCorruptHeartArtifactAmount = 2;        // (:130)
inline constexpr int32_t kCorruptHeartLadderBeatAmount = 1;      // (:134)
inline constexpr int32_t kCorruptHeartLadderStrength3 = 10;      // (:142)
inline constexpr int32_t kCorruptHeartLadderStrengthMax = 50;    // (:146)
// The rung at which the ladder becomes permanent -- see the saturation
// argument on kMonsterFlagCorruptHeartBuffCountMask (combat_state.hpp).
inline constexpr uint32_t kCorruptHeartBuffCountSaturation = 4u;

// --- per-instance state accessors (MonsterState.flags, combat_state.hpp) ----

[[nodiscard]] inline bool corrupt_heart_first_move(
    const MonsterState& m) noexcept {
    return (m.flags & kMonsterFlagCorruptHeartFirstMove) != 0u;
}
inline void corrupt_heart_set_first_move(MonsterState& m, bool v) noexcept {
    if (v) {
        m.flags |= kMonsterFlagCorruptHeartFirstMove;
    } else {
        m.flags &= ~kMonsterFlagCorruptHeartFirstMove;
    }
}

// `moveCount % 3`, stored mod 3 -- header note on the mask in combat_state.hpp.
[[nodiscard]] inline uint32_t corrupt_heart_move_count(
    const MonsterState& m) noexcept {
    return (m.flags & kMonsterFlagCorruptHeartMoveCountMask) >>
           kMonsterFlagCorruptHeartMoveCountShift;
}
inline void corrupt_heart_set_move_count(MonsterState& m, uint32_t v) noexcept {
    m.flags = (m.flags & ~kMonsterFlagCorruptHeartMoveCountMask) |
              ((v % 3u) << kMonsterFlagCorruptHeartMoveCountShift);
}

// `buffCount`, saturating at kCorruptHeartBuffCountSaturation.
[[nodiscard]] inline uint32_t corrupt_heart_buff_count(
    const MonsterState& m) noexcept {
    return (m.flags & kMonsterFlagCorruptHeartBuffCountMask) >>
           kMonsterFlagCorruptHeartBuffCountShift;
}
inline void corrupt_heart_set_buff_count(MonsterState& m, uint32_t v) noexcept {
    const uint32_t clamped =
        v > kCorruptHeartBuffCountSaturation ? kCorruptHeartBuffCountSaturation : v;
    m.flags = (m.flags & ~kMonsterFlagCorruptHeartBuffCountMask) |
              (clamped << kMonsterFlagCorruptHeartBuffCountShift);
}

void corrupt_heart_init(CombatState& state, uint8_t monster_index) noexcept;
void corrupt_heart_use_pre_battle_action(CombatState& state,
                                         uint8_t monster_index) noexcept;
void corrupt_heart_take_turn(CombatState& state,
                             uint8_t monster_index) noexcept;
void corrupt_heart_roll_move(CombatState& state,
                             uint8_t monster_index) noexcept;

// getMove (:171-200). `num` is discarded by the Java (all four arms ignore it),
// so it is not a parameter here -- the draw itself is spent by the callers, one
// per rollMove, exactly where AbstractMonster.rollMove spends it. The extra
// randomBoolean of the `moveCount % 3 == 0` arm comes off `state.ai_rng` inside.
void corrupt_heart_decide_move(CombatState& state,
                               uint8_t monster_index) noexcept;

}  // namespace sts::engine
