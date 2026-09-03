#pragma once

// SpireSpear (registry/monsters.yaml id 68, MonsterId::SPIRE_SPEAR) -- the
// right-hand half of the Act-4 elite `Shield and Spear` (SpireSpear.java, 184
// lines, read in full). Its twin is monster_spire_shield.hpp, which carries the
// six shared readings: the one-wide monsterHpRng draw, the mod-3 cycle and its
// extra aiRng draw, the pre-battle ordering, the presentation-only damage()
// override, and the byte-identical die() body (spire_guard_die_after, defined in
// the Shield's translation unit and registered by BOTH ids).
//
// FOUR READINGS THIS MODULE ADDS:
//
// (1) THE CYCLE IS THE SHIELD'S, MIRRORED. getMove (:116-140):
//         case 0: !lastMove(BURN_STRIKE) ? BURN_STRIKE(ATTACK_DEBUFF)
//                                        : PIERCER(BUFF)
//         case 1: SKEWER(ATTACK), unconditional
//         default: aiRng.randomBoolean() ? PIERCER(BUFF) : BURN_STRIKE(...)
//     then `++this.moveCount` (:139). The coin flip sits on case 2 where the
//     Shield's sits on case 0, and the history read sits on case 0 where the
//     Shield's sits on case 1 -- which is exactly why the pair's telegraphs
//     interleave rather than march in step. Per-turn ai_rng cost is 2 draws on
//     the case-2 arm and 1 on the others; `num` is ignored on every arm and is
//     still spent by rollMove (AbstractMonster.java:465-467).
//     `moveCount` shares the Shield's type-scoped MonsterState.flags range
//     (kMonsterFlagSpireGuardMoveCount*, combat_state.hpp), stored mod 3.
//
// (2) usePreBattleAction IS ARTIFACT ONLY, AND THAT IS A READING, NOT AN
//     OMISSION. (:73-80) is one `ApplyPowerAction(this, this, new ArtifactPower(
//     this, 2))` at ascension >= 18, else `(this, 1)`. It applies NO Surrounded:
//     SpireShield.java:71 is the game's only SurroundedPower construction, so the
//     Shield alone arms the back-attack mechanic and the Spear merely benefits
//     from it when the player turns away.
//
// (3) THE THREE THINGS THE ROW COULD NOT AUTHOR, ALL IN THIS BODY.
//     (a) BURN_STRIKE's Burn PILE is ASCENSION-BRANCHED and the pile rides in a
//         step's `extra`, which has no tier column. At A18+ it is
//         `MakeTempCardInDrawPileAction(new Burn(), 2, false, true)` (:92) -- the
//         4-arg overload, so `randomSpot = false` and `toBottom` defaults false
//         (MakeTempCardInDrawPileAction.java:44-46): two copies at the TOP of the
//         DRAW pile with NO cardRandomRng draw. Below A18 it is
//         `MakeTempCardInDiscardAction(new Burn(), 2)` (:95) -- the DISCARD pile.
//         The row authors the A18+ arm, which is the live one at
//         kMonsterAscension 20; the branch is written out HERE so the sub-A18 arm
//         is a visible edit rather than an absent one.
//     (b) PIERCER is an ALL-ALLIES fan-out, ITSELF INCLUDED: `for (m :
//         getMonsters().monsters) addToBottom(new ApplyPowerAction(m, this, new
//         StrengthPower(m, 2), 2));` (:99-101), with NO liveness filter. A step's
//         target vocabulary is SELF or PLAYER, so the row authors one
//         SELF-targeted +2 template and this body retargets it per member -- the
//         Shield's FORTIFY and the Healer's HEAL/BUFF precedent. The Spear's own
//         copy is why a lone surviving Spear still ramps.
//     (c) SKEWER's hit COUNT is `skewerCount` -- 3, and 4 from A3 (:63,:67) -- a
//         per-tier STEP COUNT, and an effect list expresses per-tier AMOUNTS
//         only. The row authors ONE 10-damage template and this body emits it
//         `skewerCount` times; kSpireSpearSkewerCount below is where that number
//         lives, and the row's comment points here.
//     BURN_STRIKE's own count is NOT in this list: `BURN_STRIKE_COUNT` is the
//     field constant 2 (:46), flat at every ascension, so the row authors two
//     literal damage steps (the Gremlin Leader STAB precedent).
//
// (4) THE TWO HITS AND THE skewerCount HITS ARE SEPARATE DamageActions.
//     BURN_STRIKE loops `for (i = 0; i < 2; ++i)` queueing ChangeState / Wait /
//     DamageAction each pass (:86-90), and SKEWER loops skewerCount times the same
//     way (:105-109), so block, a halving power and the lethal clamp all apply PER
//     HIT rather than once to a doubled number. SKEWER's is the 4-arg
//     `DamageAction(player, info, SLASH_DIAGONAL, true)` -- the trailing `true` is
//     `isFast`, a duration, and carries no combat effect.
//
// UNVERIFIED-until-captured: S3.62 owes the Shield-and-Spear fight capture, and
// the KILL-ORDER pair that s3-design section 5 trap 7 requires.

#include <cstdint>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// The Java ctor's `offsetX` (SpireSpear.java:50), stored as MonsterState::draw_x.
// +70 puts the Spear to the RIGHT of the centred player in the Shield-and-Spear
// room, where the Shield's -1000 puts it to the LEFT -- back_attack.hpp note (2)
// derives why those two numbers straddle the player at every resolution.
inline constexpr int16_t kSpireSpearDrawX = 70;

// `skewerCount` (SpireSpear.java:47,:63,:67) -- 3 below A3, 4 from A3. THE ONE
// NUMBER OF THIS MONSTER THAT IS NOT IN THE REGISTRY TABLE, because it is a
// per-tier step COUNT and an effect list carries per-tier AMOUNTS; header note
// (3c). Resolved at kMonsterAscension 20, so the A3 arm is live.
[[nodiscard]] inline constexpr int spire_spear_skewer_count(int ascension) noexcept {
    return ascension >= 3 ? 4 : 3;
}

// `BURN_STRIKE_COUNT` (SpireSpear.java:46). Flat at every ascension, which is why
// the row authors two literal damage steps rather than a counted template. It is
// named here anyway so the loop bound in the body cites the field.
inline constexpr int kSpireSpearBurnStrikeCount = 2;

void spire_spear_init(CombatState& state, uint8_t monster_index) noexcept;
void spire_spear_use_pre_battle_action(CombatState& state,
                                       uint8_t monster_index) noexcept;
void spire_spear_take_turn(CombatState& state, uint8_t monster_index) noexcept;
void spire_spear_roll_move(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
