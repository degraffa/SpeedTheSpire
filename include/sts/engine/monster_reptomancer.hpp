#pragma once

// The Reptomancer (registry/monsters.yaml id 60) and the SnakeDagger it summons
// (id 61) -- ONE module, because the summoner owns the dagger's positions, its
// spawn HP and its cap, and splitting them would put half of each fact in the
// other file. (Reptomancer.java 212 lines and SnakeDagger.java 115 lines, both
// read in full.)
//
// SEVEN READINGS THIS MODULE LEANS ON:
//
// (1) THE CONSTRUCTOR DRAWS monster_hp_rng TWICE. `super(NAME, ID,
//     AbstractDungeon.monsterHpRng.random(180, 190), ...)` (:64) is evaluated
//     before the constructor body, and setHp draws again (:68-72). The first
//     value is discarded and the STREAM OFFSET is the observable -- and the
//     Reptomancer sits at group index 1 of its own encounter, so BOTH daggers'
//     rolls move with it. Carried as registry DATA (row SUPER_ARG_HP, timing
//     CONSTRUCTOR_BEFORE_HP) so burn_unspawned_ctor_rolls orders it too; the
//     Taskmaster (id 38) is the only other double-drawer in the roster.
//
// (2) BOTH DAGGER SPAWNS ARE FULLY PLANNED AT QUEUE TIME, for the Gremlin
//     Leader's reasons and one extra. `new SnakeDagger(POSX[i], POSY[i])` (:123)
//     runs inside takeTurn, so its monster_hp_rng draw happens when the action is
//     QUEUED, not when it resolves; the child's init() rollMove happens at
//     RESOLVE (SpawnMonsterAction.java:48). The insertion slot is genuinely
//     resolve-time work in the Java and is planned here anyway because the
//     answer is already determined: the walk reads only `drawX`, no action
//     mutates it, dead records keep theirs, and the only records that can be
//     inserted in the window are these spawns themselves -- which the local
//     simulation models. That simulation is also what gives the Reptomancer's
//     own post-insertion index for the trailing RollMoveAction, since pending
//     action_queue items are NOT remapped across a spawn.
//
// (3) `daggers[4]` NEEDS NO STORAGE. The Java array is a slot map from
//     POSX index -> the dagger currently occupying it, and its only reader is
//     the spawn loop's `daggers[i] != null && !daggers[i].isDeadOrEscaped()`
//     test (:122). Every write puts the dagger built at POSX[i] into daggers[i]
//     (:124) or, in usePreBattleAction, the encounter dagger whose position IS
//     POSX[0]/POSX[1] (:96-100). So "slot i is occupied" is exactly "some record
//     with draw_x == POSX[i] is not dead-or-escaped", and MonsterState::draw_x
//     already stores that key -- the Gremlin Leader's identifySlot derivation,
//     verbatim. The derivation stays exact when a slot is RECYCLED: the dead
//     record keeps its draw_x and a new live one joins it, and the live one is
//     what both the Java pointer and this predicate see.
//
// (4) THE SMART POSITIONING IS SpawnMonsterAction's, WHICH IS NOT THE GREMLIN
//     LEADER'S. SpawnMonsterAction.java:50-56 `continue`s (a COUNT over the
//     whole list) where SummonGremlinAction.java:92-99 `break`s. This module
//     calls smart_position_for_spawn_action; see the correction note in
//     monster_dispatch.hpp.
//
// (5) THE MINION APPLICATION IS addToTOP HERE. SpawnMonsterAction.java:68 is
//     `addToTop(new ApplyPowerAction(m, m, new MinionPower(m)))`, where
//     SummonGremlinAction.java:114 is addToBot. Carried by the spawn item's
//     kSpawnMinionAtTop bit (interp.hpp). SpawnMonsterAction also does NOT run
//     the spawned monster's usePreBattleAction, so run_pre_battle stays false --
//     which is right, because a SnakeDagger has none.
//
// (6) canSpawn IS CONSULTED BY getMove, NOT BY takeTurn. `aliveCount <= 3` over
//     every record that is neither `this` nor isDying (:139-146) gates the
//     DECISION only; once SPAWN_DAGGER is telegraphed, takeTurn spawns whatever
//     the free-slot scan finds even if the group filled up in between. The two
//     caps are independent and both are reproduced: the group cap (canSpawn) and
//     the position cap (four POSX slots).
//
// (7) THE DEATH SWEEP IS POST-`super.die()` AND THAT IS WHAT MAKES IT TERMINATE.
//         public void die() {
//             super.die();
//             for (m : getCurrRoom().monsters.monsters) {
//                 if (m.isDead || m.isDying) continue;
//                 addToTop(new HideHealthBarAction(m));
//                 addToTop(new SuicideAction(m));
//             }
//         }                                            (Reptomancer.java:157-165)
//     There is no `m == this` term: the Reptomancer is skipped ONLY because
//     super.die() has already set its isDying. Run this body on the pre-super
//     side and it suicides itself in an infinite regress -- which is exactly the
//     case monster_dispatch.hpp's MonsterDieAfterFn comment names. The
//     one-argument SuicideAction defaults triggerRelics to TRUE
//     (SuicideAction.java:17-19), so each minion pays a full death. Both pushes
//     are addToTop inside the loop, so the SUICIDES RESOLVE IN REVERSE LIST
//     ORDER; HideHealthBarAction is presentation and is dropped, which does not
//     move that order.
//
// THE DAGGER ITSELF (SnakeDagger.java): its ONE monster_hp_rng draw is the
// super argument `monsterHpRng.random(20, 25)` (:46) and the class declares NO
// setHp, so the registry `hp` column IS that draw and the row needs no `rolls`
// entry -- the mirror image of reading (1). getMove (:90-98) is a firstMove
// one-shot (WOUND) and then EXPLODE forever, ignoring `num` -- but the rollMove
// that produced `num` still spends aiRng.random(99) every turn. EXPLODE's
// self-kill is `LoseHPAction(this, this, this.currentHealth)` (:73), an HP_LOSS
// through the ordinary damage path with a QUEUE-TIME amount -- not a
// SuicideAction, so no relic-trigger selector is involved.

#include <cstdint>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// Reptomancer.POSX (Reptomancer.java:57) -- the four dagger slots, in the INDEX
// ORDER the spawn loop scans them, which is NOT their left-to-right order.
// (POSY :58 is vertical only and has no combat meaning; it is not stored.)
inline constexpr int16_t kReptomancerDaggerX[4] = {210, -220, 180, -250};
// The Reptomancer's own ctor offsetX (:64), for the same positioning walk.
inline constexpr int16_t kReptomancerDrawX = -20;
// DAGGERS_PER_SPAWN / ASC_2_DAGGERS_PER_SPAWN (:52-53) and the branch (:67).
inline constexpr int32_t kReptomancerDaggersPerSpawn = 1;
inline constexpr int32_t kReptomancerDaggersPerSpawnA18 = 2;
inline constexpr int32_t kReptomancerDaggersAscension = 18;
// canSpawn's threshold (:145): `return aliveCount <= 3;`.
inline constexpr int32_t kReptomancerMaxOtherAlive = 3;

void reptomancer_init(CombatState& state, uint8_t monster_index) noexcept;
void reptomancer_use_pre_battle_action(CombatState& state,
                                       uint8_t monster_index) noexcept;
void reptomancer_take_turn(CombatState& state, uint8_t monster_index) noexcept;
void reptomancer_roll_move(CombatState& state, uint8_t monster_index) noexcept;
void reptomancer_die_after(CombatState& state, uint8_t monster_index) noexcept;

// getMove (:167-195), exposed for the directed tests. RE-DRAWS ai_rng on its two
// recursive arms, so it takes the whole state rather than just `num`.
void reptomancer_decide_move(CombatState& state, uint8_t monster_index,
                             int32_t num) noexcept;

// canSpawn (:139-146): the count of records that are neither the Reptomancer nor
// dying. Exposed because it is the one group-shaped predicate in the batch and
// the tests pin it directly.
[[nodiscard]] int32_t reptomancer_alive_count(const CombatState& state,
                                              uint8_t monster_index) noexcept;

// The SnakeDagger.
void snake_dagger_init(CombatState& state, uint8_t monster_index) noexcept;
void snake_dagger_spawn_at_hp(CombatState& state, uint8_t monster_index,
                              int16_t hp) noexcept;
void snake_dagger_take_turn(CombatState& state, uint8_t monster_index) noexcept;
void snake_dagger_roll_move(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
