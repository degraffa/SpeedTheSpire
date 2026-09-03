#pragma once

// BACK ATTACK -- AbstractMonster's facing machinery, S3.42.
//
// This module is NOT a monster and NOT a power: every line of it is
// `AbstractMonster` / `CardGroup` framework code that happens to have exactly
// one live producer in the game (the Act-4 `Shield and Spear` elite, the only
// encounter that applies `SurroundedPower`). It lives on its own so that the two
// guard modules, the damage pipeline and the two facing-writing seams
// (queue_card_play, use_potion) all read ONE copy of the predicate.
//
// -----------------------------------------------------------------------------
// (1) THE PREDICATE, AND WHY IT IS GEOMETRY RATHER THAN A POWER
// -----------------------------------------------------------------------------
//
// AbstractMonster.applyBackAttack (AbstractMonster.java:1015-1017), read in full:
//
//     private boolean applyBackAttack() {
//         return AbstractDungeon.player.hasPower("Surrounded")
//             && (   AbstractDungeon.player.flipHorizontal
//                     && AbstractDungeon.player.drawX < this.drawX
//                 || !AbstractDungeon.player.flipHorizontal
//                     && AbstractDungeon.player.drawX > this.drawX);
//
// -- a hasPower test AND a facing/position test. `BackAttackPower` is NOT read
// here: the power is a MARKER the framework keeps in sync with this predicate
// (note 3), never the mechanism.
//
// -----------------------------------------------------------------------------
// (2) THE FACING QUESTION, RESOLVED (s3-design §2.3's
//     `UNVERIFIED -- needs decompile check`, and the S3 ledger's
//     "Back-attack facing: model it, or collapse it?" deferred row)
// -----------------------------------------------------------------------------
//
// The proposed collapse was: "with exactly two guards, the one the player is not
// facing takes 1.5x". THAT IS EXACT, and it is exact for a reason that has to be
// derived rather than assumed -- the geometry, read from three methods in full:
//
//   * AbstractMonster's ctor (AbstractMonster.java:152):
//         drawX = Settings.WIDTH * 0.75f + offsetX * Settings.xScale
//     with `offsetX` the ctor argument -- SpireShield -1000.0f
//     (SpireShield.java:49), SpireSpear +70.0f (SpireSpear.java:50).
//   * AbstractDungeon.java:1802-1806, the room-entry position:
//         if (getCurrRoom() instanceof MonsterRoom
//             && lastCombatMetricKey.equals("Shield and Spear"))
//             player.movePosition(Settings.WIDTH / 2.0f, floorY);
//         else { player.movePosition(Settings.WIDTH * 0.25f, floorY);
//                player.flipHorizontal = false; }
//     -- THE GUARDS' ROOM IS THE ONE ROOM IN THE GAME THAT CENTRES THE PLAYER,
//     and the one room that does not reset the facing.
//   * Settings.java:259,273,282: `xScale = WIDTH / 1920.0f`, always positive.
//
// Substituting, with k = xScale > 0 and WIDTH = 1920k:
//         Shield  drawX = k * (1440 - 1000) =  440k
//         PLAYER  drawX = k * 960                       <-- strictly between
//         Spear   drawX = k * (1440 +   70) = 1510k
// so the two guards are on STRICTLY OPPOSITE SIDES of the player at every
// resolution, and `applyBackAttack` reduces to "the player is not facing this
// one" -- true for exactly one guard while both live. The collapse is therefore
// sound; what it is NOT is free of state, because "which one" is decided by
// `flipHorizontal`, which the player MOVES by targeting a guard
// (AbstractPlayer.java:1291-1293). So the answer is: MODEL THE FACING (one bit,
// kCombatFlagPlayerFacingLeft), COLLAPSE THE COORDINATES (the stored
// MonsterState::draw_x ordering key already is the collapse -- drawX is a
// strictly monotone affine function of offsetX, so `player.drawX > m.drawX`
// is `kPlayerDrawXInGuardRoom > m.draw_x` on the offsetX scale).
//
// STILL UNVERIFIED-until-captured (S3.62): the derivation is from source, and no
// capture has yet shown a real player attacking each guard in turn.
//
// -----------------------------------------------------------------------------
// (3) THE 1.5x IS HARD-CODED IN AbstractMonster, AT TWO SITES WITH DIFFERENT
//     ARITHMETIC -- and only ONE of them has an engine consumer
// -----------------------------------------------------------------------------
//
//   (a) THE REAL HIT. AbstractMonster.applyPowers (:998-1013):
//           for (DamageInfo dmg : this.damage) {
//               dmg.applyPowers(this, AbstractDungeon.player);
//               if (!applyBackAttack) continue;
//               dmg.output = (int)((float)dmg.output * 1.5f);
//           }
//       DamageInfo.applyPowers (DamageInfo.java:39-70) has ALREADY done
//       `output = MathUtils.floor(tmp)` and the `< 0` clamp, so the multiply is
//       applied to an ALREADY-FLOORED, ALREADY-CLAMPED int and truncated again.
//       That is `compute_damage`'s monster-owned branch plus one trailing step,
//       which is exactly where back_attack_multiply() is applied.
//
//   (b) THE INTENT NUMBER. AbstractMonster.calculateDamage (:968-996) runs the
//       same float pipeline but multiplies MID-PIPELINE:
//           ... stance.atDamageReceive ...
//           if (applyBackAttack()) tmp = (int)(tmp * 1.5f);
//           ... atDamageFinalGive ... atDamageFinalReceive ...
//           dmg = MathUtils.floor(tmp);
//       THE TWO ORDERS ARE NOT EQUIVALENT. With the Shield Weakened, SMASH base
//       34: (a) floor(25.5) = 25 -> (int)37.5 = 37; (b) (int)38.25 = 38 ->
//       floor(38) = 38. They differ by one.
//       `intentDmg` HAS NO ENGINE CONSUMER: PublicView publishes the intent ID
//       and nothing else (public_view.hpp:190, `intent` is a move id, suppressed
//       to 0 under Runic Dome), and no engine site computes a displayed monster
//       damage number. So (b) is deliberately NOT implemented, and this comment
//       is the record of where it goes and what its arithmetic is THE DAY a
//       consumer appears -- it is not the same call as (a).
//
//   (c) THE ONE PLACE (b) LEAKS INTO STATE, and why it does not bite: the
//       Shield's sub-A18 SMASH block is `damage.get(1).output` (SpireShield.java
//       :107), the POST-power output -- which the back attack has multiplied.
//       The engine resolves every tier at kMonsterAscension 20, where SMASH's
//       block is the flat 99 of the A18+ arm (:103-105) and no runtime read
//       happens. registry/monsters.yaml records the same limitation at the step.
//
// -----------------------------------------------------------------------------
// (4) THE MARKER, AND THE ONE DECLARED DEVIATION
// -----------------------------------------------------------------------------
//
// `BackAttackPower` (powers.yaml 137) is applied by the FRAMEWORK, not by either
// guard: AbstractMonster.applyPowers (:999-1002) does
//     if (applyBackAttack && !hasPower("BackAttack"))
//         addToTop(new ApplyPowerAction(this, null, new BackAttackPower(this)));
// and removeSurroundedPower (:1019-1023) does the mirror-image addToTop removal.
// The two are driven from
//     * AbstractDungeon.onModifyPower (:2653-2667) -- EVERY power add/reduce/
//       remove re-runs applyPowers over the whole group; and
//     * CardGroup.refreshHandLayout (CardGroup.java:204-223) -- which is the ONLY
//       caller of removeSurroundedPower, and the only site that can take the
//       marker AWAY.
//
// Because the predicate's inputs are just {Surrounded present, facing bit,
// draw_x}, re-evaluating at EVERY onModifyPower is identical to re-evaluating
// only where one of those three changes: every other evaluation finds the marker
// already in its correct place and queues nothing. This module therefore
// evaluates at three sites -- Surrounded landing (the Shield's pre-battle), the
// facing moving (queue_card_play / use_potion), and a guard dying -- and each one
// reproduces the Java's queue placement:
//
//   * PRE-BATTLE. The Shield queues [Surrounded->player, Artifact->self]
//     addToBottom; the game's marker is addToTop'd when Surrounded RESOLVES,
//     i.e. ahead of the two Artifacts but behind Surrounded. Queueing it
//     addToBottom BETWEEN them gives the identical resolve order, and the guard
//     body does exactly that (monster_spire_shield.cpp). Exact, not approximate.
//
//   * DEATH. Both die() bodies are explicit RemoveSpecificPowerActions and are
//     reproduced literally, addToBottom, in list order.
//
//   * THE FACING MOVE -- **THE DECLARED DEVIATION**. The game's re-evaluation
//     rides `refreshHandLayout`, which fires from ~30 sites during a card's
//     resolution; WHICH of them fires first (and therefore where in the queue the
//     marker's ApplyPowerAction / RemoveSpecificPowerAction lands relative to the
//     played card's own actions) is not decidable from source alone. This module
//     queues both, addToTop, in group order -- the shape and the relative order
//     of the two are the Java's; the queue POSITION is pinned to the play seam
//     (AbstractPlayer.playCard, which is where the Java writes the facing) rather
//     than to a hand-layout callback. NOTHING IN THE ENGINE READS THE MARKER --
//     the multiplier reads the predicate (note 1) -- so this can only move the
//     marker's arrival by a few queue slots WITHIN one card's resolution, never
//     the damage. **S3.62's Shield-and-Spear capture is the named witness that
//     must settle it**; until then this behaviour is UNVERIFIED-until-captured.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// The player's `drawX` inside the Shield-and-Spear room, expressed on
// MonsterState::draw_x's scale (the Java ctor's `offsetX`), so it can be compared
// against a stored key with no float anywhere.
//
// DERIVATION (note 2): a monster's `drawX` is `WIDTH*0.75 + offsetX*xScale` and
// the player's is `WIDTH/2` (AbstractDungeon.java:1803). With WIDTH = 1920*xScale
// and xScale > 0, dividing through by xScale puts the player at
// 960 - 1440 = -480 on the offsetX scale, and the comparison
// `player.drawX > m.drawX` becomes `-480 > m.draw_x` at every resolution.
//
// It is a CONSTANT and not a stored player field because the room-entry branch
// that produces it names one encounter by string ("Shield and Spear"), and the
// predicate below is dead for every other room anyway -- no other encounter
// applies Surrounded, so no other room can reach a reader of this number. If a
// second Surrounded producer ever exists, this constant becomes a field; the
// centring branch is the thing to re-read then.
inline constexpr int16_t kPlayerDrawXInGuardRoom = -480;

// Both markers are applied through the 3-arg `ApplyPowerAction(target, source,
// power)`, which forwards the POWER'S OWN `amount` (ApplyPowerAction.java:79-81),
// and both ctors set `this.amount = -1` (SurroundedPower.java:20,
// BackAttackPower.java:27). That -1 is the game's spelling of "this power shows
// no number" -- `canGoNegative` is false (AbstractPower.java:70, never
// overridden), so renderAmount draws nothing -- NOT a stack count. It is carried
// faithfully into PowerSlot.amount because it is oracle-visible, exactly as
// kMinionAppliedAmount is (monster_dispatch.hpp), and op_apply_power's
// `amount == -1` non-stacking path is what makes a re-application a no-op.
inline constexpr int32_t kSurroundedAppliedAmount = -1;
inline constexpr int32_t kBackAttackAppliedAmount = -1;

// AbstractMonster.applyBackAttack (AbstractMonster.java:1015-1017), for the
// monster in slot `mi`. False for an out-of-range slot.
//
// The `hasPower("Surrounded")` conjunct is FIRST and short-circuits, which is
// what keeps this a two-instruction no-op in every combat that is not the Act-4
// elite: no other encounter in the game applies SurroundedPower.
[[nodiscard]] bool monster_applies_back_attack(const CombatState& s,
                                               uint8_t mi) noexcept;

// The POSITIONAL half of that predicate, without the `hasPower("Surrounded")`
// conjunct. Exposed because CardGroup.refreshHandLayout tests exactly this half
// per member (it establishes the hasPower conjunct once, at :204), and because
// the Shield's pre-battle needs it BEFORE the Surrounded it is queueing has
// landed.
[[nodiscard]] bool monster_back_attacked_by_position(const CombatState& s,
                                                     uint8_t mi) noexcept;

// The BackAttack marker AbstractMonster.applyPowers (:999-1002) addToTop's when
// the Shield's pre-battle SurroundedPower RESOLVES, queued addToBottom instead --
// see monster_spire_shield.hpp note (4) for why the two produce the identical
// resolve order, and why the marker is derived from the live geometry here rather
// than hard-coded to one guard. Must be called between the Surrounded item and
// the Shield's own Artifact item.
void queue_pre_battle_back_attack_markers(CombatState& s) noexcept;

// `dmg.output = (int)((float)dmg.output * 1.5f)` -- AbstractMonster.applyPowers
// (:1006), applied to the already-floored, already-clamped int output of
// DamageInfo.applyPowers. Note (3a); a free function so the exact float
// arithmetic is written once and cited once.
[[nodiscard]] int back_attack_multiply(int output) noexcept;

// `player.flipHorizontal = m.drawX < player.drawX` (AbstractPlayer.java:1292 ==
// PotionPopUp:200 == SpireShield.java:170 == SpireSpear.java:177). All four sites
// are this one line; the callers carry their own guards.
void set_player_facing_toward(CombatState& s, uint8_t mi) noexcept;

// CardGroup.refreshHandLayout's Surrounded block (CardGroup.java:204-223),
// queue-for-queue:
//
//     if (player.hasPower("Surrounded") && monsters != null)
//         for (AbstractMonster m : monsters.monsters)
//             if (<m satisfies applyBackAttack's positional half>) m.applyPowers();
//             else { m.applyPowers(); m.removeSurroundedPower(); }
//
// i.e. per group member, in list order: ensure the BACK_ATTACK marker is present
// when the predicate holds and absent when it does not, each as one addToTop
// item. The leading `if (monsters.areMonstersBasicallyDead()) return;` (:201-203)
// is reproduced; the per-member walk is NOT liveness-filtered, exactly as the
// Java's is not -- the queued items carry their own resolve-time guards.
//
// Called at the facing-move seams only; see note (4) for why that is equivalent
// to the game's every-onModifyPower re-evaluation, and for the deviation it
// declares.
void refresh_back_attack_markers(CombatState& s) noexcept;

}  // namespace sts::engine
