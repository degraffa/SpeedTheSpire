#pragma once

// Donu and Deca -- the Act-3 paired boss encounter "Donu and Deca"
// (MonsterHelper.java:591-593; encounters.yaml row 60). Stats and move effects
// are generated registry data (monsters.yaml ids 64 and 65); move SELECTION, the
// group fan-outs and Deca's ascension-switched telegraph are native.
//
// ONE TRANSLATION UNIT FOR BOTH, the monster_slaver.cpp precedent: the two
// classes are the same 146/158-line shape with four differences, and splitting
// them would duplicate the shape three times over while hiding the differences.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   Donu.java:33-146; Deca.java:36-158; MonsterHelper.java:591-593;
//   AbstractMonster.java:765-779 (setHp), :921-951 (die);
//   MonsterGroup.java:90-95 (areMonstersBasicallyDead);
//   GainBlockAction.java:51-55; ApplyPowerAction.java:97-100.
//
// ============================================================================
// SPAWN ORDER IS DECA FIRST
// ============================================================================
// `MonsterHelper.getEncounter("Donu and Deca")` builds `new Deca(), new Donu()`
// (:591-593) despite the key naming Donu first, so Deca is slot 0 and Donu slot 1.
// encounters.yaml row 60 already has it right; the fixture pins it because it is
// the sort of thing a reader "corrects".
//
// ============================================================================
// THE ALTERNATION -- deterministic, permanent, and driven by nothing random
// ============================================================================
// Both classes carry `isAttacking` and both getMoves read ONLY that:
//
//   Donu.getMove  (:125-131)  isAttacking ? BEAM(0) : CIRCLE(2)
//   Deca.getMove  (:135-143)  isAttacking ? BEAM(0)
//                                         : (asc >= 19 ? DEFEND_BUFF : DEFEND) SQUARE(2)
//
// and both takeTurns FLIP it at QUEUE TIME, before the trailing RollMoveAction
// resolves (Donu :110/:118, Deca :119/:128). The field INITIALISERS differ --
// Deca true (:74), Donu false (:70) -- so the pair opens exactly out of phase
// (Deca BEAM / Donu CIRCLE) and swaps every turn, forever. One flag bit serves
// both records (kMonsterFlagShapeAttacking); they co-occur in one group, which
// is fine, because the type-scoped rule is that no single RECORD is two types.
//
// THE aiRng DRAW IS STILL SPENT. Neither getMove reads `num`, but RollMoveAction
// calls rollMove -> getMove(aiRng.random(99)) regardless, so both bosses burn one
// draw per turn that changes nothing. Modelling the draw IS the point: it moves
// the shared stream, so every later monster decision in the fight shifts with it.
//
// ============================================================================
// MOVE ID 0 IS A REAL MOVE, AND IT COLLIDES WITH THE HISTORY SENTINEL
// ============================================================================
// `BEAM = 0` on both (Donu.java:42, Deca.java:46) -- the first move id 0 in the
// roster. The move-history ring uses 0 as its EMPTY-SLOT sentinel
// (monster_dispatch.hpp:29-33), so for these two `last_move_is(m, 0)` cannot tell
// "decided BEAM" from "nothing decided yet". NOTHING IS WRONG TODAY: neither
// getMove reads its history at all. It is a landmine for whoever later adds a
// history check to a move-0 monster, which is why the registry loader's bound was
// relaxed with that note attached, the row says it, and the fixture pins it.
//
// ============================================================================
// THE TWO GROUP LOOPS ARE UNGUARDED, AND THAT MUST NOT BE "FIXED"
// ============================================================================
//   Donu case 2 (:114-117)   for (m : getMonsters().monsters)
//                                ApplyPowerAction(m, this, StrengthPower(m,3), 3)
//   Deca case 2 (:122-128)   for (m : getMonsters().monsters) {
//                                GainBlockAction(m, this, 16);
//                                if (asc >= 19) ApplyPowerAction(m, this,
//                                                   PlatedArmorPower(m,3), 3); }
//
// No `isDying` / `isEscaping` filter anywhere -- unlike every S2.22 walk, which
// filters at queue time. Safety here is RESOLVE-TIME and belongs to the actions:
// ApplyPowerAction early-returns on `target.isDeadOrEscaped()`
// (ApplyPowerAction.java:97-100) and GainBlockAction on `!isDying && !isDead`
// (GainBlockAction.java:52). Both of those guards are engine surfaces this batch
// added (op_apply_power / op_block), precisely so these loops can stay literal.
// Queue-time and resolve-time liveness genuinely differ and the difference is
// observable the moment anything interleaves, so the walk stays over every RECORD
// -- dead ones included -- and each item no-ops on its own terms.
//
// DECA'S TWO STEPS INTERLEAVE PER MEMBER. The A19 Plated Armor is inside the same
// loop body as the block, so the queue reads block(Deca), armor(Deca),
// block(Donu), armor(Donu) -- not all four blocks then all four armors. Both
// bosses also include THEMSELVES in the walk (the Healer-row precedent).
//
// PLATED ARMOR ON ANOTHER MONSTER IS NEW. Plated Armor (PowerId 17) has been
// registered since S1 and gained an on_power_removed binding in S2.21, but Deca
// is the FIRST producer in the game that puts it on a DIFFERENT monster -- it can
// plate Donu -- which makes that power's run-out path reachable on a boss.
//
// ============================================================================
// THE PAIRED DEATH
// ============================================================================
// Both die() bodies (Donu :134-144, Deca :146-156) call `super.die()` FIRST and
// then gate their victory tail on `getMonsters().areMonstersBasicallyDead()`, so
// the tail fires ONCE, on the second death, and the first death is silent. The
// tail itself is achievements + onBossVictoryLogic / onFinalBossVictoryLogic,
// none of it sim-visible -- so NEITHER boss registers a MonsterDieFn or a
// MonsterDieAfterFn, and there is no cannotLose anywhere in this fight: the pair
// simply both have to die. Explicit nullptr cases, not a `default:`, because both
// classes DO declare die().
//
// ----------------------------------------------------------------------------
// RNG ACCOUNTING
// ----------------------------------------------------------------------------
// ONE monster_hp_rng draw EACH in the ctors -- setHp(265)/setHp(250) is the
// single-arg overload, which still draws over a degenerate range (see
// monster_awakened_one.hpp) -- in spawn order, Deca then Donu.
// ONE ai_rng draw each at init and one each per turn, all of them ignored by the
// decision.
// Deca's BEAM spends NO card_random_rng: MakeTempCardInDiscardAction appends.
//
// ----------------------------------------------------------------------------
// NEGATIVES
// ----------------------------------------------------------------------------
// * usePreBattleAction is Artifact 2, or 3 from A19, on ITSELF only -- no group
//   fan-out (Donu :93-99, Deca :97-108). Deca's additionally carries the BGM
//   lines Donu's does not; presentation, and the ONLY asymmetry in the pair's
//   pre-battle.
// * damage() (Donu :83-90, Deca :86-94) is super.damage plus a hit animation --
//   no override content, so no on_monster_damaged body for either.
// * There is no A4 branch on anything but beamDmg, no A9 branch on anything but
//   HP, and no A17 branch at all.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Donu.getMove (:125-131). Ignores `num` entirely; the caller still draws it.
void donu_decide_move(CombatState& state, uint8_t monster_index) noexcept;

// Deca.getMove (:135-143). ASCENSION-SWITCHED TELEGRAPH -- Intent.DEFEND_BUFF at
// A19+, Intent.DEFEND below -- the first per-tier intent in the roster, which the
// row schema cannot express. `ascension` is a parameter rather than the fixed
// kMonsterAscension exactly so the tier-2 tests can drive the sub-A19 arm.
void deca_decide_move(CombatState& state, uint8_t monster_index,
                      int32_t ascension) noexcept;

void donu_init(CombatState& state, uint8_t monster_index) noexcept;
void deca_init(CombatState& state, uint8_t monster_index) noexcept;

void donu_use_pre_battle_action(CombatState& state,
                                uint8_t monster_index) noexcept;
void deca_use_pre_battle_action(CombatState& state,
                                uint8_t monster_index) noexcept;

void donu_roll_move(CombatState& state, uint8_t monster_index) noexcept;
void deca_roll_move(CombatState& state, uint8_t monster_index) noexcept;

void donu_take_turn(CombatState& state, uint8_t monster_index) noexcept;
void deca_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
