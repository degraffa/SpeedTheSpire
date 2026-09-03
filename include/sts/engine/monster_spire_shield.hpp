#pragma once

// SpireShield (registry/monsters.yaml id 67, MonsterId::SPIRE_SHIELD) -- the
// left-hand half of the Act-4 elite `Shield and Spear` (SpireShield.java, 177
// lines, read in full). Its twin is monster_spire_spear.hpp; the two share one
// `Surrounded`/`BackAttack` lifetime and one die() body, and s3-design §5 trap 7
// is the record that their KILL ORDER is observable.
//
// SIX READINGS THIS MODULE LEANS ON:
//
// (1) THE ONE monsterHpRng DRAW OVER A ONE-WIDE RANGE. The ctor's `super(...)`
//     HP argument is the literal 110 (:49) -- no draw -- and then
//         if (ascensionLevel >= 8) setHp(125); else setHp(110);
//     (:55-59). `setHp(int)` is literally `this.setHp(hp, hp)`
//     (AbstractMonster.java:777-779) and the two-arg body's first statement is
//     `currentHealth = monsterHpRng.random(minHp, maxHp)` (:765-766),
//     UNCONDITIONAL -- and `Random.random(int, int)` increments `counter` and
//     calls `nextInt(end - start + 1)`, which consumes an XS128 `nextLong` even
//     when the range is 1 (Random.java:58-61). So the fixed HP is a fixed
//     OUTCOME, not a skipped draw: init spends EXACTLY ONE monster_hp_rng draw
//     over the row's `min == max` column, and the Shield-and-Spear spawn spends
//     exactly TWO. The Nemesis (monsters.yaml 56) is the landed precedent for
//     the same overload; the Maw (55) is the contrasting shape (no setHp call at
//     all, hence zero draws). Optimising the draw away because the range is one
//     wide desynchronises monsterHpRng for the whole rest of the run
//     (s3-design §5 trap 4).
//
// (2) THE BASH ORB BRANCH IS SHORT-CIRCUITED, AND THE SHORT CIRCUIT IS THE
//     MODEL. takeTurn case BASH (:82-92) ends with
//         if (!AbstractDungeon.player.orbs.isEmpty()
//             && AbstractDungeon.aiRng.randomBoolean()) {
//             ApplyPowerAction(player, this, new FocusPower(player, -1), -1);
//             break;
//         }
//         ApplyPowerAction(player, this, new StrengthPower(player, -1), -1);
//     Java's `&&` evaluates left to right and STOPS ON THE FIRST FALSE. An
//     Ironclad has no orb slots at all -- orbs are the Defect's -- so
//     `player.orbs.isEmpty()` is true, the negation is false, and
//     `aiRng.randomBoolean()` IS NEVER EVALUATED: no draw is spent, and the
//     Strength arm is taken every single time. The registry row authors the
//     Strength step for exactly that reason and FocusPower stays deliberately
//     unregistered (powers.yaml, S4 with the Defect).
//     WHAT IS MODELLED IS THE SHORT CIRCUIT, NOT ITS OUTCOME: the branch is
//     written out in spire_shield_take_turn with the orb test spelled as the
//     engine's `kPlayerHasNoOrbs` constant, so the day a character with orbs is
//     simulated the missing half is a visible edit at the right site rather than
//     a silently absent aiRng draw. See the note at that constant.
//
// (3) THE `moveCount % 3` CYCLE, AND WHERE ITS EXTRA aiRng DRAW LANDS. getMove
//     (:113-137) IGNORES `num` entirely and switches on `this.moveCount % 3`:
//         case 0: aiRng.randomBoolean() ? FORTIFY(DEFEND) : BASH(ATTACK_DEBUFF)
//         case 1: !lastMove(BASH) ? BASH(ATTACK_DEBUFF) : FORTIFY(DEFEND)
//         default: SMASH(ATTACK_DEFEND)
//     then `++this.moveCount` (:136). So the per-turn ai_rng cost is TWO draws on
//     the case-0 arm (rollMove's `random(99)` plus the coin) and ONE on the other
//     two -- the `num` it ignores is still spent, because
//     AbstractMonster.rollMove draws it before calling getMove
//     (AbstractMonster.java:465-467). Its twin's coin sits on case 2 instead,
//     which is why the pair's telegraphs interleave the way they do.
//     `moveCount` lives in MonsterState.flags bits 17-18, stored mod 3
//     (combat_state.hpp, kMonsterFlagSpireGuardMoveCount*); the field is a plain
//     `int` in the Java that is only ever read `% 3`.
//
// (4) usePreBattleAction QUEUES THREE ITEMS, AND THE MIDDLE ONE IS THE
//     FRAMEWORK'S. The Java body (:69-77) is two addToBottom items:
//         ApplyPowerAction(PLAYER, this, new SurroundedPower(player))     (:71)
//         ApplyPowerAction(this, this, new ArtifactPower(this, 2 | 1))    (:72-76)
//     -- the Shield is the ONLY source of Surrounded in the fight (the Spear's
//     pre-battle is Artifact only, SpireSpear.java:73-80). The THIRD item is not
//     in this class at all: when the Surrounded application RESOLVES,
//     AbstractDungeon.onModifyPower (:2653-2667) re-runs applyPowers over the
//     whole group, and AbstractMonster.applyPowers (:999-1002) addToTop's a
//     BackAttackPower onto whichever guard the player is not facing. addToTop at
//     that moment puts it AHEAD of the two Artifacts and BEHIND Surrounded, which
//     is EXACTLY the position an addToBottom between them occupies -- so this
//     body queues [Surrounded, BackAttack?, Artifact] and the resolve order is
//     identical. The `BackAttack?` item is derived from the live predicate
//     (back_attack.hpp), not hard-coded to this monster.
//
// (5) damage() IS PRESENTATION AND die() IS POST-SUPER. The damage(DamageInfo)
//     override (:155-162) is `super.damage(info)` and then, behind
//     `info.owner != null && info.type != THORNS && info.output > 0`, the "Hit"
//     spine animation -- the Sentry precedent, so NO on_monster_damaged entry.
//     die() (:164-176) calls `super.die()` FIRST and carries all its content
//     after it, so it registers in monster_die_after_fn and not monster_die_fn;
//     the walk skips the dying guard purely because super.die() has already set
//     isDying (there is no `m == this` term), which is the Reptomancer ordering.
//
// (6) THE die() BODY IS BYTE-IDENTICAL TO THE SPEAR'S, AND KILL ORDER IS
//     OBSERVABLE THROUGH IT. Both bodies are
//         for (m : getCurrRoom().monsters.monsters) {
//             if (m.isDead || m.isDying) continue;
//             if (player.hasPower("Surrounded")) {
//                 player.flipHorizontal = m.drawX < player.drawX;
//                 addToBottom(RemoveSpecificPowerAction(player, player, "Surrounded"));
//             }
//             if (!m.hasPower("BackAttack")) continue;
//             addToBottom(RemoveSpecificPowerAction(m, m, "BackAttack"));
//         }
//     (SpireShield.java:164-176 == SpireSpear.java:171-183). Two consequences:
//     whichever guard dies first ENDS the back-attack mechanic for the rest of
//     the fight; and the number of queued items differs by kill order, because
//     the second `if` fires only when the SURVIVOR is the one carrying the
//     marker. Kill the Shield first (with the player still facing right, the
//     opening state) and the Shield itself held the marker, so the surviving
//     Spear has none and ONE item is queued; kill the Spear first while the
//     Shield holds the marker and TWO are. Both bodies live in
//     spire_guard_die_after, which both ids dispatch to.
//     s3-design §5 trap 7's witness is two Act-4 elite captures, one per kill
//     order, replayed `--combat` zero-diff. **UNVERIFIED-until-captured: S3.62
//     owes exactly that pair.**

#include <cstdint>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// The Java ctor's `offsetX` (SpireShield.java:49), stored as MonsterState::draw_x
// -- the ordering key `drawX = WIDTH*0.75 + offsetX*xScale` is a strictly
// monotone affine function of (back_attack.hpp). -1000 puts the Shield far to the
// player's LEFT in the centred Shield-and-Spear room; the Spear's +70 puts it to
// the RIGHT.
inline constexpr int16_t kSpireShieldDrawX = -1000;

// `FORTIFY_BLOCK` (SpireShield.java:46). Never ascension-branched, so it is a
// constant here and carries no tier column in the row.
inline constexpr int32_t kSpireShieldFortifyBlock = 30;

// `!AbstractDungeon.player.orbs.isEmpty()` (SpireShield.java:86), for the
// Ironclad this engine simulates: FALSE, because orbs are the Defect's and no
// Ironclad run can ever hold one -- there is no relic, potion, card or event in
// the game that gives the Ironclad an orb slot. It is a named constant rather
// than a deleted branch so that reading spire_shield_take_turn shows the whole
// Java expression, `aiRng` draw included, and so that S4's Defect work has a
// compile-time-visible hook instead of an absent one. See header note (2).
inline constexpr bool kPlayerHasOrbs = false;

void spire_shield_init(CombatState& state, uint8_t monster_index) noexcept;
void spire_shield_use_pre_battle_action(CombatState& state,
                                        uint8_t monster_index) noexcept;
void spire_shield_take_turn(CombatState& state, uint8_t monster_index) noexcept;
void spire_shield_roll_move(CombatState& state, uint8_t monster_index) noexcept;

// The die() body BOTH guards share (header note 6) -- registered by both ids in
// monster_die_after_fn. It is one function and not two because the two Java
// bodies are byte-identical, and a copy would be the thing that drifts.
void spire_guard_die_after(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
