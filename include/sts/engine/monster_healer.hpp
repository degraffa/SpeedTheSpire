#pragma once

// The Healer (the Act-2 STRONG group "Centurion and Healer",
// MonsterHelper.java:498-500, where it spawns SECOND). Stats and move effects are
// generated registry data (monsters.yaml id 36); move SELECTION is native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   Healer.java:32-199; HealAction.java:13-36 -> AbstractMonster.heal
//   (AbstractMonster.java:383-399); StrengthPower / FrailPower (registered);
//   AbstractMonster.java:705-715 (init/rollMove), 765-775 (setHp), 431-491;
//   MonsterGroup.java:35-40,62-66; RollMoveAction.java:17-21.
//
// THE MOVE TREE (getMove, Healer.java:151-180) reads a GROUP-WIDE HP SUM and is
// ascension-switched at BOTH of its first two gates -- the only getMove in this
// batch of which both are true:
//
//   needToHeal = SUM over { m in group : !isDying && !isEscaping } of
//                (m.maxHealth - m.currentHealth)          -- INCLUDING SELF
//
//   1. needToHeal > (A17 ? 20 : 15) && !lastTwoMoves(HEAL) -> HEAL   (Intent.BUFF)
//   2. num >= 40 && (A17 ? !lastMove(ATTACK) : !lastTwoMoves(ATTACK))
//                                                         -> ATTACK (ATTACK_DEBUFF)
//   3. !lastTwoMoves(BUFF)                                -> BUFF   (Intent.BUFF)
//   4. otherwise                                          -> ATTACK (ATTACK_DEBUFF)
//
// SIX THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) MOVES 2 AND 3 TELEGRAPH THE SAME INTENT. HEAL is `setMove((byte)2,
//     Intent.BUFF)` (:159,163) -- BUFF, not DEFEND and not a heal-specific icon
//     -- and BUFF is `setMove((byte)3, Intent.BUFF)` (:176). So the intent alone
//     does NOT identify the move, and any consumer that maps intent -> move (a
//     policy, an observation layer) must read move_history[0] instead. This is
//     the first such collision in the roster.
//
// (2) THE A17 GATES MOVE IN OPPOSITE DIRECTIONS. The heal threshold RISES (>20
//     from >15), making healing rarer; the ATTACK guard TIGHTENS from
//     !lastTwoMoves(ATTACK) to !lastMove(ATTACK), which fires LESS often (a
//     single preceding ATTACK now blocks arm 2, where below A17 it took two).
//     Both push toward arm 3/4. They are separate `if (asc >= 17) ... else ...`
//     blocks (:157-165 and :166-174), not one branch, so a reading that assumes
//     one ascension test governs the whole method is wrong.
//
// (3) THE A17 STAT BLOCK DOES NOT INHERIT A2. The ctor is a FULL else-if CHAIN
//     (:64-76) that RESTATES every value on every arm:
//         A17+ : magicDmg 9, strAmt 4, healAmt 20
//         A2+  : magicDmg 9, strAmt 3, healAmt 16
//         base : magicDmg 8, strAmt 2, healAmt 16
//     so healAmt is UNCHANGED at A2 and magicDmg is UNCHANGED at A17. Both
//     non-changes are spelled out as explicit tier columns in the registry row
//     and are deliberate negative tests.
//
// (4) HEAL AND BUFF FAN OUT OVER THE LIVE GROUP, INCLUDING THEMSELVES. Each
//     queues ONE action per member of `AbstractDungeon.getMonsters().monsters`
//     that is neither isDying nor isEscaping (:99-102 / :109-112), in GROUP
//     (slot) order, with the liveness read at QUEUE TIME -- so a member that dies
//     to something queued in between still receives its already-queued action
//     (and, for HEAL, is then rejected by AbstractMonster.heal's own isDying
//     guard at resolve). An effect list cannot express a per-combat action COUNT,
//     so the registry row authors ONE self-targeted template step per move and
//     this module retargets it per member through queue_monster_move_effect.
//
// (5) AI-RNG ACCOUNTING. One ai_rng.random(99) at init() -- READ, not discarded,
//     though arm 1 can preempt it -- then one per turn through the trailing
//     RollMoveAction (:116), which sits OUTSIDE takeTurn's switch so all three
//     cases reach it. No branch spends a second draw. One monster_hp_rng draw in
//     the ctor, over the A7 column (50, 58). BOTH sound helpers are UNSEEDED
//     libGDX MathUtils -- playSfx a randomBoolean (:120) and playDeathSfx a
//     random(2) (:128) -- so neither the per-turn sounds nor the death cost a
//     seeded draw, and the Healer registers NO MonsterDieFn (contrast the
//     Mugger's, whose playDeathSfx is seeded aiRng).
//
// (6) ENC_NAME "HealerTank" IS DEAD CONTENT. The constant exists (:40) but no
//     MonsterHelper.getEncounter case and no encounter key anywhere in the tree
//     names it (searched, not assumed), so it is deliberately not registered --
//     the same call S2.01 made for the "Mysterious Sphere" ENCOUNTER key.
//
// NO PRE-BATTLE ACTION, NO damage() CONSEQUENCE, NO SPAWN PATH. Healer.java
// declares takeTurn, playSfx, playDeathSfx, changeState, getMove, damage and die.
// changeState's only key is "STAFF_RAISE" (:139-148), spine calls; damage()
// (:183-190) is super.damage plus the standard non-THORNS, output > 0 hit
// animation; die() (:193-198) is the unseeded sound plus animation. Nothing
// splits or summons a Healer.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// getMove's decision (Healer.java:151-180) as a pure function of the GROUP's HP,
// the mover's history, the rolled `num`, and the ASCENSION -- a parameter rather
// than the fixed kMonsterAscension precisely so the tier-2 tests can exercise the
// sub-A17 arms of BOTH gates, which move in opposite directions. The engine
// always calls it at kMonsterAscension.
void healer_decide_move(CombatState& state, uint8_t monster_index, int32_t num,
                        int32_t ascension) noexcept;

void healer_init(CombatState& state, uint8_t monster_index) noexcept;

// The trailing RollMoveAction (Healer.java:124), reached by all three takeTurn
// cases.
void healer_roll_move(CombatState& state, uint8_t monster_index) noexcept;

void healer_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
