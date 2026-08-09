#pragma once

// The Bronze Automaton (BronzeAutomaton.java, read in full; registry/
// monsters.yaml id 40) -- the Act-2 boss encounter "Automaton" (encounters.yaml
// id 38, a single EMIT). Stats and move effects are generated registry data;
// move SELECTION, the orb summon and the death sweep are native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), all read in full:
//   BronzeAutomaton.java:41-191; BronzeOrb.java:29-102 (the summon's ctor
//   draws); SpawnMonsterAction.java:28-73; SuicideAction.java:12-38;
//   AbstractMonster.java:431-491 (move history), :765-779 (setHp),
//   :925-951 (die).
//
// FIVE THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) firstTurn IS CONSUMED ON THE INIT ROLL (the Snecko precedent -- no
//     storage). getMove's firstTurn arm (:150-154) forces SPAWN_ORBS/UNKNOWN
//     and clears the latch; the only call that can see it set is init()'s
//     rollMove, so bronze_automaton_init decides it directly and
//     bronze_automaton_decide_move models every later decision. The init draw
//     is still taken and discarded -- getMove reads `num` on NO arm at all,
//     but rollMove drew it (AbstractMonster.java:465-467) and the stream moved.
//
// (2) numTurns COUNTS ONLY TWO OF THE FIVE ARMS. `++this.numTurns` (:173) sits
//     BELOW three returns: the firstTurn arm, the numTurns==4 HYPER_BEAM arm
//     (which also resets it to 0) and the post-beam recovery arm all return
//     without incrementing. Only a FLAIL or BOOST decision counts, so the
//     cycle is beam-recovery, then four counted turns, then beam again.
//     Stored in MonsterState.pad0 (this type's whole use of it), saturating --
//     the ==4 test cannot be re-reached past 4 anyway (the reset is the only
//     way back down, and it happens AT 4).
//
// (3) THE A19 BRANCH SWAPS THE RECOVERY TURN. lastMove(HYPER_BEAM) telegraphs
//     BOOST/DEFEND_BUFF at ascension >= 19 and STUNNED/STUN below (:160-167).
//     Both arms are spelled at the fixed A20 with the tier-2 tests driving the
//     other; STUNNED's takeTurn case is presentation ONLY plus the trailing
//     roll -- a stunned turn still costs one ai_rng draw.
//
// (4) A SPAWN_ORBS TURN'S STREAM ORDER IS THE BATCH'S MOST FRAGILE FACT.
//     BOTH BronzeOrb ctors run in the SpawnMonsterAction ARGUMENT LIST at
//     addToBottom time (:116,:122), so the queue-time reads are
//         monsterHpRng orb-1 super-arg (52,58 flat), orb-1 setHp (tiered),
//         monsterHpRng orb-2 super-arg,              orb-2 setHp
//     and the resolve-time reads are ai_rng orb-1 init roll, orb-2 init roll,
//     then the boss's own trailing RollMoveAction THIRD -- it was queued
//     behind both spawns. The two ctor ranges are read from the registry row
//     (hp columns + the SUPER_ARG_HP roll row), so live play and
//     burn_unspawned_ctor_rolls cannot drift. The SFXAction coins around the
//     spawns (:111-121) are UNSEEDED MathUtils.
//
// (5) THE MINION APPLY IS addToTop AT EACH SPAWN'S RESOLVE, NOT addToBot.
//     SpawnMonsterAction with isMinion=true (:67-69) addToTop's the
//     ApplyPowerAction the moment the spawn resolves -- so orb 1 already
//     carries Minion before orb 2's spawn resolves. That is the OPPOSITE
//     order from SummonGremlinAction's bit (kSpawnApplyMinion), which is why
//     this module leaves that bit CLEAR and queues an explicit APPLY_POWER
//     item immediately AFTER each spawn item: the spawn's resolution queues
//     nothing ahead of it, so "next item" IS the addToTop position. Amount is
//     MinionPower's unassigned -1 (kMinionAppliedAmount).
//
// The die() override (:177-190) is presentation then super.die() then
// onBossVictoryLogic (achievements -- not sim-visible) then the addToTop
// suicide sweep over every !isDead && !isDying record (:182-187): STRICTLY
// POST-super (the Reptomancer shape -- run pre-super it would sweep the boss
// itself), so it registers in monster_die_after_fn and monster_die_fn is an
// explicit nullptr. Each SuicideAction is the 1-arg ctor -> die(TRUE), so a
// swept orb's Stasis onDeath fires and the stolen card returns while the
// victory queue drains. The forward walk pushing one add_to_top item per
// survivor reproduces the Java's net LIFO order (survivors die in reverse
// slot order); the VFX/HideHealthBar items around each are presentation.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// The boss's own `offsetX` (BronzeAutomaton.java:71, the ctor's 9th argument):
// -50. Between the two orb positions (-300 and +200), so orb 1 inserts BEFORE
// the boss and orb 2 AFTER it -- the on-screen [orb, boss, orb] layout is also
// the record order, pinned by the spawn-order test.
inline constexpr int16_t kBronzeAutomatonDrawX = -50;

// The two orb spawn positions, indexed by spawn order: BronzeOrb(-300, 200, 0)
// then BronzeOrb(200, 130, 1) (BronzeAutomaton.java:116,122). The y and the
// `count` argument are presentation.
inline constexpr int16_t kBronzeOrbSlotX[2] = {-300, 200};

void bronze_automaton_init(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (:99-105): ONE queued ApplyPowerAction(this, this,
// ArtifactPower(this, 3)). The 3 is a flat literal at every ascension (:103).
void bronze_automaton_use_pre_battle_action(CombatState& state,
                                            uint8_t monster_index) noexcept;

void bronze_automaton_take_turn(CombatState& state,
                                uint8_t monster_index) noexcept;

// The trailing RollMoveAction (:145) is QUEUED: on a SPAWN_ORBS turn it must
// resolve after both orbs' init rolls (note (4)).
void bronze_automaton_roll_move(CombatState& state,
                                uint8_t monster_index) noexcept;

// getMove (:149-174) at an arbitrary ascension, MINUS the firstTurn arm (note
// (1): that arm is init-only). `num` is the aiRng.random(99) the caller
// already drew -- read by NO arm, spent by every roll.
void bronze_automaton_decide_move(CombatState& state, uint8_t monster_index,
                                  int32_t num) noexcept;

// die() AFTER super.die() (:181-187): the suicide sweep. Registered in
// monster_die_after_fn.
void bronze_automaton_die_after(CombatState& state,
                                uint8_t monster_index) noexcept;

}  // namespace sts::engine
