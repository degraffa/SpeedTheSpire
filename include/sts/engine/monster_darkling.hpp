#pragma once

// The Darkling -- the Act-3 encounter "3 Darklings", which sits in BOTH the weak
// and the strong pool (encounters.yaml ids 44 and 53; the s2-design section 5
// trap-8 row). Stats and move effects are generated registry data
// (monsters.yaml id 49); move SELECTION and the whole revival machine are native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   Darkling.java:42-244; RegrowPower.java (powers.yaml id 99, game_id
//   "Life Link"); AbstractMonster.java:921-950 (die), 383-399 (heal), 705-715
//   (init/rollMove), 431-491 (moveHistory); AbstractCreature.java:784-790
//   (isDeadOrEscaped); MonsterGroup.java:35-45, 90-95, 98-105, 117-122;
//   PhilosopherStone.java:50-54 (the only onSpawnMonster with content);
//   SetMoveAction.java:52-56.
//
// THIS IS THE ONE MONSTER IN THE BATCH WHOSE DEATH IS NOT A DEATH. Everything
// below exists to reproduce that, and the pieces are easier to hold if the shape
// is stated once up front:
//
//   * usePreBattleAction latches the ROOM's `cannotLose` (:96) -- by DIRECT
//     ASSIGNMENT, not through a CannotLoseAction -- and grants itself Regrow.
//   * die() (:239-243) calls `super.die()` ONLY when cannotLose is false. While
//     the latch is up, a Darkling at 0 HP is NOT dying: no isDying, no power
//     onDeath, no relic onMonsterDeath, no combat-over contribution.
//   * damage() (:200-236) picks that up: at hp <= 0 and not already halfDead it
//     sets halfDead, fires the two fan-outs BY HAND (which is precisely why the
//     veto exists -- otherwise they would fire twice), CLEARS the power list, and
//     then either telegraphs COUNT or, if every Darkling is down, drops the latch
//     and kills the whole group synchronously.
//   * a half-dead Darkling still takes turns (COUNT, then REINCARNATE), heals to
//     maxHealth/2 and comes back. It is OUT for targeting and IN for the fight --
//     the kMonsterFlagHalfDead / monster_basically_dead split (combat_state.hpp).
//
// FIVE DETAILS THAT ARE EASY TO GET WRONG, EACH PINNED BY A TEST IN THIS BATCH.
//
// (1) SLOT PARITY DECIDES WHO CAN CHOMP. getMove's low arm reads
//     `AbstractDungeon.getMonsters().monsters.lastIndexOf(this) % 2 == 0` (:163)
//     -- the monster's own index in the group list, which the game never compacts
//     (MonsterGroup.java:35-45) and which is exactly this engine's monster slot.
//     So slots 0 and 2 can CHOMP and SLOT 1 STRUCTURALLY NEVER CAN: the middle
//     Darkling of a group of three is a different monster from its siblings.
//
// (2) getMove RECURSES, WITH TWO DIFFERENT BOUNDS. The parity/lastMove failure
//     re-enters on aiRng.random(40, 99) (:166) and the lastTwoMoves(NIP) failure
//     on aiRng.random(0, 99) (:181). The draw COUNT is therefore input-dependent,
//     and the two bounds are not interchangeable -- a 40..99 re-entry cannot
//     reach the CHOMP arm again, a 0..99 one can. This must recurse; a loop that
//     re-rolls one bound is a different distribution AND a different stream.
//
// (3) THE HALF-DEATH PUSHES MOVE 4 ONTO THE HISTORY TWICE. `setMove` appends to
//     moveHistory on EVERY call (AbstractMonster.java:431-437), and the guarded
//     block at :219-224 does it twice: once synchronously (:221) and once when
//     the SetMoveAction it queued at :223 resolves. So a revived Darkling's
//     history reads [4, 4, <the move it died on>], which makes `!lastMove(CHOMP)`
//     and `!lastMove(HARDEN)` both true on its first decision back. That is
//     behaviour, not bookkeeping.
//
// (4) RELIC onMonsterDeath FIRES TWICE PER DARKLING, AND POWER onDeath ONCE.
//     Once at its half-death (:208-210), once again in the final group sweep,
//     where each member's die() now passes the veto and runs `super.die()`
//     (:228-230 -> AbstractMonster.java:933-937). The POWER walk does not double,
//     because `powers.clear()` (:211) emptied the list in between. Consequence: a
//     "3 Darklings" fight with Gremlin Horn yields SIX triggers, not three, and
//     with Spore Cloud three Vulnerable applications, not six. This falls out of
//     transcribing the Java and is flagged in the S2.25 Log for oracle
//     confirmation rather than asserted as certainly right.
//
// (5) `firstMove` NEEDS NO STORAGE AND IS NOT RESET BY REVIVAL. It is consumed on
//     init()'s rollMove -- the Snecko / Chosen / Red-Slaver precedent -- so the
//     init entry point IS the firstMove branch and every later decision runs the
//     general tree. The Java never re-sets it, so a revived Darkling does not get
//     a second opening move; reproducing that is a matter of not doing anything.
//
// WHERE THE PER-INSTANCE NIP DAMAGE LIVES: MonsterState.pad0, the Louse
// biteDamage precedent (a CONSTRUCTOR_AFTER_HP monster_hp_rng roll, registry
// row NIP_DAMAGE). NO MonsterState.flags BIT IS SPENT BY THIS MONSTER -- the
// half-dead bit is the framework's global bit 25, produced here and by the
// Awakened One (S2.28), and owned by neither.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// RegrowPower's stack amount, at both grant sites: the 3-arg pre-battle
// ApplyPowerAction (Darkling.java:97, amount defaults to powerToApply.amount = 1)
// and the explicit 1 at every REINCARNATE (:133).
inline constexpr int32_t kDarklingRegrowAmount = 1;

void darkling_init(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (Darkling.java:94-98): latch the room's cannotLose --
// SYNCHRONOUSLY, it is a bare field assignment at :96 and not a queued
// CannotLoseAction -- then queue ApplyPowerAction(this, this, new
// RegrowPower(this)). No RNG draw. Every Darkling in the group runs this and
// they all set the same combat-wide flag, which is idempotent.
void darkling_use_pre_battle_action(CombatState& state,
                                    uint8_t monster_index) noexcept;

// The trailing RollMoveAction (Darkling.java:140), reached by all five cases.
// Draws one ai_rng value, which getMove reads -- and may draw more (see note 2).
//
// It also carries the REINCARNATE turn's ChangeStateAction("REVIVE") (:132,
// :193-196), whose whole content is `halfDead = false`: see the body for why
// that clear can sit here and nowhere earlier.
void darkling_roll_move(CombatState& state, uint8_t monster_index) noexcept;

void darkling_take_turn(CombatState& state, uint8_t monster_index) noexcept;

// die() (Darkling.java:239-243): `if (!cannotLose) super.die();`. Returns the
// framework's VETO -- true == SUPPRESS super.die() -- so the caller skips the
// power onDeath and relic onMonsterDeath fan-outs entirely.
[[nodiscard]] bool darkling_die(CombatState& state, uint8_t monster_index) noexcept;

// damage() (Darkling.java:200-236), the post-`super.damage()` half: the
// half-death latch, the by-hand fan-outs, the COUNT telegraph, and the all-dead
// group kill. Dispatched from the on_monster_damaged seam, which the interpreter
// runs AFTER the death edge -- exactly where the Java's override body sits
// relative to the `super.damage()` that called die().
void darkling_on_damaged(CombatState& state, uint8_t monster_index,
                         int32_t hp_lost) noexcept;

}  // namespace sts::engine
