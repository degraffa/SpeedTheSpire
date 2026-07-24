#pragma once

// Large slime monster modules + the split framework's native bodies (B3.17).
// HP and non-split move effects are generated registry data; history-dependent
// getMove selection, the split turn's queued action sequence, and the damage()
// interrupt are native. The fixed S1 difficulty is A20, so move selection
// follows the cited A17 normal-monster AI branches while HP/damage resolve
// from the base/A2/A7/A17 table columns.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled; every file read in full):
//   AcidSlime_L.java:73-102 (ctor: setHp branches, damage table, SplitPower),
//     :104-140 (takeTurn -- cases 1/2/4 each queue their OWN RollMoveAction;
//     case 3 SPLIT queues CannotLose -> Suicide(this,false) -> 2x
//     SpawnMonsterAction(new AcidSlime_M(saveX -/+ 134, ..., 0, currentHealth))
//     -> CanLose and NO RollMoveAction, then setMove(SPLIT)),
//     :142-152 (damage() interrupt), :154-215 (getMove; A17 branch :156-185);
//   SpikeSlime_L.java:68-94 (ctor), :96-128 (takeTurn -- switch cases queue no
//     RollMoveAction; ONE unconditional RollMoveAction after the switch, :127,
//     INCLUDING the split case -- it resolves on the then-dead parent),
//     :130-140 (damage() interrupt), :142-167 (getMove; A17 branch :144-155);
//   SlimeBoss.java:149-157,171-179 (shared split machinery reading: identical
//     CannotLose/Suicide/Spawn x2/CanLose shape, children at the boss's
//     CURRENT hp; :175 the same <= maxHealth/2 interrupt sans splitTriggered);
//   SpawnMonsterAction.java:28-59 (smart positioning: insertion index == count
//     of existing records -- dead included -- with drawX < the child's), :48
//     (m.init() -> the child's first aiRng rollMove at RESOLVE time);
//   SuicideAction.java:21-36 (hp=0 + die(triggerRelics=false); block untouched);
//   CannotLoseAction.java / CanLoseAction.java:12-15 (room cannotLose latch);
//   RollMoveAction.java:17-21 (no liveness check -- a dead parent still rolls);
//   SetMoveAction.java:52-56; SplitPower.java:19-26 (marker power, amount -1);
//   AbstractMonster.java:431-437 (setMove pushes moveHistory), :465-467
//     (rollMove = getMove(aiRng.random(99))), :139,150 (4-arg child ctor:
//     maxHealth = currentHealth = newHealth -- NO monsterHpRng draw), :712-715
//     (init = rollMove), :925-951 (die), :869 (endBattle gated on !cannotLose);
//   MonsterGroup.java:35-40 (addMonster inserts at index; records never removed).
//
// SPLIT SEMANTICS (the framework contract):
//   * Trigger: after ANY damage() call that leaves the slime alive with
//     (float)currentHealth <= (float)maxHealth / 2.0f (exact for int16 HP:
//     2*hp <= max_hp), while nextMove != SPLIT and splitTriggered is unset:
//     synchronously setMove(SPLIT, UNKNOWN) + push history, queue a SET_MOVE
//     re-assert at the queue BOTTOM, latch kMonsterFlagSplitTriggered. The
//     queued SET_MOVE lands AFTER any already-queued RollMoveAction (own-turn
//     thorns interrupt), which still rolls -- and draws aiRng -- first.
//   * Split turn: children are CONSTRUCTED at takeTurn time (hp captured from
//     the parent's CURRENT hp before the suicide) but their init() aiRng roll
//     runs when each SPAWN_MONSTER resolves, in queue order (left child, then
//     right). The parent suicides first (no relic triggers), stays in its slot
//     as a dead record, and combat cannot end inside the CannotLose window.
//   * Slots: SpawnMonsterAction smart positioning counts records with drawX <
//     child.drawX. Children spawn at saveX-134 / saveX+134; in every S1 layout
//     that fields a large slime pre-B3.20 (the solo "Large Slime" encounter,
//     MonsterHelper.java:409-414) all other records are the parent itself, so
//     the left child inserts AT the parent's slot p (parent shifts to p+1) and
//     the right child at p+2. B3.20's boss fight must recompute indices from
//     its explicit coordinates (-385/+120 vs the boss's 0) instead of reusing
//     p/p+2 blindly. In the boss descendant layout, Acid L's left medium
//     (-14) inserts before the dead boss (0), while its right medium (254)
//     appends after the Acid parent (120); monster_slime_large.cpp derives
//     those slots from the retained SLIME_BOSS record.
//   * Actions queued after the spawns must PRE-compute post-insertion slots
//     (the Java holds object references; this engine holds indices): Spike L's
//     unconditional trailing RollMoveAction targets the parent at p+1.
//
// Draw accounting at A20 (independent fixtures pin these):
//   * ctor: exactly 1 monster_hp_rng inclusive HP draw; SplitPower has none;
//   * init(): exactly 1 ai_rng.random(99) draw (empty history -> no tiebreak);
//   * Spike L: 1 random(99) per queued ROLL_MOVE, no boolean draws ever --
//     including one POSTHUMOUS roll on the dead parent after its split;
//   * Acid L: 1 random(99) per queued ROLL_MOVE plus a conditional
//     randomBoolean(0.6f/0.6f/0.4f) tiebreak (nextFloat-based); the split turn
//     itself queues NO roll for the parent;
//   * each spawned child: exactly 1 random(99) at SPAWN_MONSTER resolve time.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Encounter-spawn init: HP roll (monster_hp_rng) + SplitPower + first rollMove.
void spike_slime_large_init(CombatState& state, uint8_t monster_index) noexcept;
void acid_slime_large_init(CombatState& state, uint8_t monster_index) noexcept;

// takeTurn bodies (dispatched via monster_turn_fn).
void spike_slime_large_take_turn(CombatState& state,
                                 uint8_t monster_index) noexcept;
void acid_slime_large_take_turn(CombatState& state,
                                uint8_t monster_index) noexcept;

// Queued-roll bodies for the ROLL_MOVE opcode (RollMoveAction.update -> rollMove;
// no liveness check). Also used for the initial init() roll.
void spike_slime_large_roll_move(CombatState& state,
                                 uint8_t monster_index) noexcept;
void acid_slime_large_roll_move(CombatState& state,
                                uint8_t monster_index) noexcept;

// Split-spawn init at a fixed HP (the 4-arg ctor + init(): NO monster_hp_rng
// draw, SplitPower added, one ai_rng rollMove). B3.20's Slime Boss split spawns
// large slimes through these.
void spike_slime_large_spawn_at_hp(CombatState& state, uint8_t monster_index,
                                   int16_t hp) noexcept;
void acid_slime_large_spawn_at_hp(CombatState& state, uint8_t monster_index,
                                  int16_t hp) noexcept;

// The shared damage() interrupt (both classes; SlimeBoss differs only in
// lacking the splitTriggered latch, which its own B3.20 module will handle).
// Called by op_damage / op_lose_hp AFTER the hit fully lands (the Java override
// runs after super.damage()).
void large_slime_on_damaged(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
