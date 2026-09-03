#pragma once

// The Awakened One (the Act-3 boss encounter "Awakened One": two Cultists then
// the boss, MonsterHelper.java:588-590, which encounters.yaml row 58 already
// has). Stats and move effects are generated registry data (monsters.yaml id 62);
// move SELECTION, the phase transition and the death edge are native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   AwakenedOne.java:67-377; AbstractMonster.java:621-707 (damage -> die),
//   :765-779 (setHp), :915-919 (escape), :921-951 (die/die(bool)),
//   :431-491 (move history); MonsterGroup.java:90-95; ClearCardQueueAction.java;
//   CanLoseAction.java:12-15; HealAction.java:13-36; CardGroup.java:463-469
//   (addToRandomSpot); CuriosityPower / UnawakenedPower / RegenerateMonsterPower.
//
// ============================================================================
// THE SHAPE: ONE MONSTER, TWO FORMS, AND A DEATH THAT IS NOT A DEATH
// ============================================================================
//
// Phase 1 dies at 0 HP into `halfDead` -- the framework flag S2.2F landed
// (kMonsterFlagHalfDead, combat_state.hpp) -- which is DEAD to targeting and
// ALIVE to the fight, so the corpse is queued, takes a turn, and that turn is
// REBIRTH: a full heal back to 300/320 and a second, harder move set. The two
// Java liveness predicates disagree on exactly that flag, and the disagreement
// IS the mechanic:
//
//   isDeadOrEscaped        = isDying || halfDead || isEscaping   -> untargetable
//   areMonstersBasicallyDead = isDying ||             isEscaping -> still fighting
//
// FIVE THINGS THAT ARE EASY TO GET WRONG HERE
//
// (1) THE PHASE TRANSITION LIVES IN damage(), NOT IN die(). AwakenedOne.damage
//     (:281-320) calls super.damage first -- whose tail calls die(), which the
//     `!cannotLose` guard SUPPRESSES ENTIRELY (:357) -- and then re-fires by hand
//     the two fan-outs super.die() would have done: the boss's own powers'
//     onDeath, then the player's relics' onMonsterDeath. That hand-firing is the
//     whole reason MonsterDieFn carries a bool VETO: without it a phase-1
//     half-death would fire both fan-outs twice.
//
// (2) ...AND IN PHASE 2 THEY DO FIRE TWICE, ON PURPOSE. Once cannotLose has been
//     cleared by Rebirth's CanLoseAction, the real death runs super.die() (fan-out
//     #1) and then falls back into the SAME `if (currentHealth <= 0 && !halfDead)`
//     block, which fan-outs again (#2). halfDead is false by then, so nothing
//     stops it. This is not a misreading and it is not harmless: Gremlin Horn
//     binds onMonsterDeath, so killing the Awakened One pays it TWICE. Modelled
//     literally -- see awakened_one_on_damaged.
//
// (3) THE REBIRTH PURGE KEEPS STRENGTH AND REGENERATE. The filter (:302-308) is
//     `type == DEBUFF` UNION {Curiosity, Unawakened, Shackled} -- everything else
//     STAYS. So an A4+ Awakened One carries its +2 Strength (plus every stack
//     Curiosity bought it off the player's POWER plays) into phase 2, and its
//     Regenerate keeps ticking. It is also an in-place Iterator.remove(), NOT a
//     RemoveSpecificPowerAction, so NO onRemove fires and Hook::ON_POWER_REMOVED
//     is deliberately not dispatched (unobservable today -- neither of that
//     hook's two binders, Plated Armor and Flight, can be on this boss -- but
//     wrong to fire regardless).
//
// (4) `firstTurn` IS READ BY BOTH TREES AND CLEARED BY ONLY ONE. Phase 1's arm
//     clears it inside getMove (:248); phase 2's arm returns WITHOUT clearing
//     (:262-265) and takeTurn case 5 clears it instead (:183). The transition
//     re-SETS it (:314). Net sequence after the transition:
//         REBIRTH -> (roll, firstTurn still set) -> DARK_ECHO -> (roll) -> normal
//     and every one of those rolls is a real ai_rng draw.
//
// (5) THE ROLL IS QUEUED, NOT INLINE, AND THE DOUBLE setMove NEEDS IT. The
//     transition sets move 3 SYNCHRONOUSLY (:309) and ALSO queues a
//     SetMoveAction(3) at the bottom (:312). That looks redundant and is not: if
//     the boss is downed by Thorns DURING ITS OWN TURN, its takeTurn already put
//     a RollMoveAction at the bottom of the queue, so the synchronous set would
//     be overwritten by that roll -- and the queued SetMoveAction, landing behind
//     it, wins. Reproducing that requires the trailing roll to be a QUEUED
//     ROLL_MOVE item (the large-slime precedent), so this monster registers a
//     MonsterRollMoveFn.
//
// ----------------------------------------------------------------------------
// ONE RECORDED DEVIATION -- WHEN halfDead CLEARS
// ----------------------------------------------------------------------------
// changeState("REBIRTH") clears halfDead at :223, i.e. one action BEFORE the
// HealAction it then queues. This engine clears it inside the heal instead
// (op_heal, interp/interp_damage.cpp). The Java can clear early because it tracks
// isDying in a separate field; here the flag IS the only carrier of "at 0 HP and
// not dying", so an early clear would make the boss read as basically-dead for
// the one action in between. That action is the trailing RollMoveAction, whose
// getMove reads neither HP nor the flag, and the combat-over gate is held open by
// the room's cannotLose latch on either reading -- so nothing observes the
// difference. Recorded at both sites rather than in one.
//
// ----------------------------------------------------------------------------
// RNG ACCOUNTING
// ----------------------------------------------------------------------------
// ONE monster_hp_rng draw in the ctor. setHp(320)/setHp(300) is the SINGLE-ARG
// overload, which is literally setHp(hp, hp) (AbstractMonster.java:777-779), and
// the two-arg body draws unconditionally even for a degenerate range -- the
// Hexaghost's row already carries this reading, and the S2.28 scout dossier
// concluded the opposite from the same precedent. The two Cultists roll their own
// (real) ranges FIRST, in spawn order.
// ONE ai_rng draw at init, then ONE per turn through the trailing RollMoveAction
// (:207), which sits OUTSIDE takeTurn's switch so every case reaches it --
// INCLUDING case 3, the Rebirth turn. No branch spends a second draw; getMove
// does not recurse.
// The Sludge move spends ONE card_random_rng draw per copy inside op_make_card's
// DRAW_RANDOM branch, and NONE when the draw pile is empty.
//
// ----------------------------------------------------------------------------
// DEAD CONTENT, recorded rather than modelled
// ----------------------------------------------------------------------------
// `saidPower` (:77, :361-365) is UNREACHABLE. It is initialised false and the
// ONLY assignment to it sits INSIDE `if (this.saidPower)`, so the branch can
// never run and the speech never happens. Not modelled; recorded here so the next
// reader does not go looking for a missing sound.
// The `Settings.isEndless` / `ModHelper` arms of changeState (:215-221) are
// pinned false, as everywhere else in this engine.
// onBossVictoryLogic / onFinalBossVictoryLogic (:370,:373) carry no sim-visible
// content -- see monster_dispatch.cpp's die-after body for the check.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// getMove (AwakenedOne.java:243-278) as a decision over the mover's own state:
// TWO DISJOINT TREES on the `form1` flag, both gated by `firstTurn`, neither
// reading HP and neither branching on ascension. `num` is the aiRng.random(99)
// the caller already drew. Exposed for the tier-2 tests, which drive both trees
// directly.
void awakened_one_decide_move(CombatState& state, uint8_t monster_index,
                              int32_t num) noexcept;

void awakened_one_init(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (:139-156): the room's cannotLose latch (a DIRECT field
// write, not a queued action) plus four addToBottom ApplyPowerActions.
void awakened_one_use_pre_battle_action(CombatState& state,
                                        uint8_t monster_index) noexcept;

// The QUEUED trailing RollMoveAction (:207) -- see note (5) above for why this
// monster must not roll inline.
void awakened_one_roll_move(CombatState& state, uint8_t monster_index) noexcept;

void awakened_one_take_turn(CombatState& state, uint8_t monster_index) noexcept;

// The damage() override's tail (:291-319): the phase transition, and in phase 2
// the deliberate second death fan-out. Runs from on_monster_damaged, i.e. after
// the hit and after the death edge -- which is where `super.damage(info)` puts it
// (:282).
void awakened_one_on_damaged(CombatState& state, uint8_t monster_index,
                             int32_t hp_lost) noexcept;

// die() (:356-375). The WHOLE body, super.die() included, sits inside
// `if (!getCurrRoom().cannotLose)`, so this has no pre-super content of its own
// and exists only to return the VETO.
[[nodiscard]] bool awakened_one_die(CombatState& state,
                                    uint8_t monster_index) noexcept;

// The post-`super.die()` half (:359-373): every surviving Cultist flees.
void awakened_one_die_after(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
