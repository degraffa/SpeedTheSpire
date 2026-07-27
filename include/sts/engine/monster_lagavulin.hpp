#pragma once

// Lagavulin -- the sleeping Exordium elite. Native sleep/wake state machine.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled; class read in full):
//   Lagavulin.java:54-58  move bytes DEBUFF=1, STRONG_ATK=3, OPEN=4, IDLE=5,
//                         OPEN_NATURAL=6 (6 is never set by anything -- see
//                         registry/monsters.yaml),
//     :60-66              STRONG_ATK_DMG 18 / A_2_STRONG_ATK_DMG 20,
//                         DEBUFF_AMT -1 / A_18_DEBUFF_AMT -2, ARMOR_AMT 8,
//     :73-100             ctor: EnemyType.ELITE (:75), setHp(112,115) at A8+ else
//                         (109,111) (:77-81), attackDmg A3 (:82), debuff A18 (:83),
//                         `asleep = setAsleep` (:85) and the !asleep branch that
//                         pre-sets isOut and isOutTriggered (:88-91),
//     :102-114            usePreBattleAction: asleep -> GainBlockAction(self, 8) +
//                         ApplyPowerAction(Metallicize 8); awake -> setMove(DEBUFF),
//     :116-174            takeTurn: DEBUFF resets debuffTurnCount then applies
//                         Dexterity/Strength `debuff` to the player; STRONG_ATK
//                         increments it and attacks; IDLE counts to 3 then opens
//                         and SetMoveActions STRONG_ATK; OPEN is a stun text. Every
//                         case queues a RollMoveAction EXCEPT the third idle,
//     :176-195            changeState("OPEN"): guarded by !isDying; sets isOut and
//                         addToBottom ReducePowerAction(self, "Metallicize", 8),
//     :197-210            damage(): if super.damage() actually moved currentHealth
//                         and the elite has not opened yet -> setMove(OPEN, STUN),
//                         latch isOutTriggered, addToBottom ChangeStateAction(OPEN),
//     :212-227            getMove: `num` unused. isOut ? (debuffTurnCount < 2 ?
//                         (lastTwoMoves(STRONG_ATK) ? DEBUFF : STRONG_ATK) : DEBUFF)
//                         : IDLE;
//   AbstractMonster.java:431-437 (setMove pushes moveHistory), :465-491
//     (lastMove/lastTwoMoves), :712-715 (rollMove ALWAYS draws aiRng.random(99)),
//     :765-775 (setHp(min,max) -> one monsterHpRng draw);
//   AbstractCreature.java:548-553 + MonsterGroup.java:290-304 (a MONSTER's
//     Metallicize fires atEndOfTurnPreEndTurnCards at applyEndOfTurnPowers time,
//     i.e. as the player's next turn begins);
//   ReducePowerAction.java:35-53, SetMoveAction.java:52-56,
//   MonsterHelper.java:439-441 ("Lagavulin" -> new Lagavulin(true)) and :445-447
//     ("Lagavulin Event" -> new Lagavulin(false)).
//
// THE SLEEPING ARMOUR HOLDS AT 8 -- it does not accumulate.
//
// CORRECTION -- supersedes what this comment used to say. It read
// "monster block never decays in this build, so the armour stands at 8 / 16 / 24
// across the three sleeping turns", on the grounds that
// MonsterGroup.applyPreTurnLogic (MonsterGroup.java:98-105) is the only caller of
// a monster loseBlock() and its own only caller, MonsterStartTurnAction, is
// constructed by nothing. The second half of that was a decompiler artifact, not
// a fact about the game: `AbstractRoom.endTurn` queues MonsterStartTurnAction
// from an anonymous inner class that CFR dropped, leaving
// `new /* Unavailable Anonymous Inner Class!! */` at AbstractRoom.java:409.
// `AbstractRoom.endTurn (bytecode AbstractRoom$1, javap) -- CFR-dropped anonymous
// class` is the real call site; the javap read-out is quoted in full at
// action_queue.cpp's apply_pre_turn_logic, which now performs the walk.
//
// So a sleeping Lagavulin's block IS cleared at the start of its own turn, one
// full phase BEFORE its Metallicize re-grants 8 at applyEndOfTurnPowers time. The
// pre-battle 8 and every later tick therefore replace each other rather than
// stacking, and the armour the player sees is 8 on every sleeping turn.
//
// At the fixed S1 A20 difficulty (kMonsterAscension) the resolved columns are
// HP 112-115, Strong Attack 20, and the A18 debuff column: -2 Dexterity AND
// -2 Strength per Siphon Soul.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Constructor + init() for the ELITE encounter's `new Lagavulin(true)`
// (MonsterHelper.java:439-441): one monsterHpRng setHp draw, the asleep latch,
// then one aiRng.random(99) whose value getMove discards before telegraphing
// IDLE.
void lagavulin_init(CombatState& state, uint8_t monster_index) noexcept;

// Constructor + init() for `new Lagavulin(false)` -- the awake variant the
// "Lagavulin Event" encounter builds (MonsterHelper.java:445-447). Identical HP
// roll and aiRng draw, but the ctor's !asleep branch (Lagavulin.java:88-91)
// pre-sets isOut/isOutTriggered, so the init rollMove telegraphs STRONG_ATK
// instead of IDLE and usePreBattleAction overwrites it with DEBUFF (pushing a
// SECOND move-history entry). The registry carries ONE Lagavulin row and
// monster_init_fn binds the elite spawn; the event that would build the awake
// one is not implemented, so nothing calls this yet -- it exists so the variant
// flag is a tested code path rather than a comment, and so bringing up that
// event is a dispatch change and not a rewrite of the state machine.
void lagavulin_init_awake(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (Lagavulin.java:102-114). Branches on the asleep latch:
// asleep queues the 8 Block + Metallicize(8); awake re-telegraphs DEBUFF.
void lagavulin_use_pre_battle_action(CombatState& state,
                                     uint8_t monster_index) noexcept;

// takeTurn + the inline rollMove/getMove (Lagavulin.java:116-174, 212-227).
void lagavulin_take_turn(CombatState& state, uint8_t monster_index) noexcept;

// Lagavulin.damage's post-super wake check (:197-210). `hp_lost` is what
// `currentHealth != previousHealth` tests -- a hit fully eaten by the sleeping
// armour loses no HP and does NOT wake it.
void lagavulin_on_damaged(CombatState& state, uint8_t monster_index,
                          int32_t hp_lost) noexcept;

}  // namespace sts::engine
