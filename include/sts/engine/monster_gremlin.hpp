#pragma once

// The five Act-1 gremlins (MonsterHelper.spawnGremlins, MonsterHelper.java:
// 737-765): Warrior, Thief, Fat, Tsundere, Wizard. HP ranges and move effects
// are generated registry data; move SELECTION is native, per design doc B §4.2's
// budget ("move effects as data, selection native where the table shape doesn't
// fit"). The fixed S1 difficulty is A20, so each body follows the cited A17/A7/A2
// branch while still resolving the numbers from the base/A2/A7/A17 table columns.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), all read in full:
//   GremlinWarrior.java:45-141; GremlinThief.java:44-128;
//   GremlinFat.java:48-141; GremlinTsundere.java:49-134;
//   GremlinWizard.java:47-154; MonsterHelper.java:737-765;
//   GainBlockRandomMonsterAction.java:20-42; AngryPower.java:24-41;
//   AbstractMonster.java:431-491,705-715,765-775,908-913.
//
// FOUR THINGS WORTH KNOWING BEFORE READING THE BODIES
//
// (1) MOVE 99 IS UNREACHABLE IN EVERY ACT -- but these gremlins DO escape, by a
//     route that never touches it. AMENDED BY S2.23, which discharged the
//     stage-b "Gremlin move-99 escape" row; the previous wording said
//     "unreachable in Act 1" and left the Act-2 answer open, and the Act-2
//     answer turns out to be that nothing changes.
//
//     Every gremlin has a move 99 whose takeTurn case queues EscapeAction,
//     reachable only once `escapeNext` is latched or `deathReact` re-telegraphs.
//     Re-derived over the whole tree (`grep -rn "deathReact()\|escapeNext()\|new
//     EscapeAction" com/`):
//       * `escapeNext()` (AbstractMonster.java:908-910) has NO CALLER ANYWHERE;
//       * the only `deathReact()` call is BanditBear.java:131, and its group is
//         "Bandits" (BanditPointy/BanditLeader/BanditBear, MonsterHelper.java:
//         513-515) -- NO GREMLIN IS EVER IN IT, so deathReact is reachable in
//         Act 2 for the two bandits and for nobody else. That obligation is
//         re-pointed at the Bandits owner, not closed (docs/s2-tasks.md).
//     So `if (this.escapeNext)` always takes the else branch and move 99 stays
//     unmodelled, in Acts 1, 2 and 3 alike.
//
//     THE ESCAPE THESE GREMLINS ACTUALLY EXPERIENCE is GremlinLeader.die()
//     (GremlinLeader.java:237-240), which queues `new EscapeAction(m)` DIRECTLY
//     for every non-dying record. It bypasses getMove and setMove entirely, so
//     an escapee NEVER re-telegraphs Intent.ESCAPE -- unlike the Looter and the
//     Mugger, which do (Looter.java:131 / Mugger.java:132). That difference is
//     load-bearing for BLOCK_RANDOM_MONSTER (opcode 67), whose valid-list filter
//     reads the TELEGRAPHED intent and not the escaped flag
//     (GainBlockRandomMonsterAction.java:26-38, interp_block.cpp): a
//     leader-fan-out escapee is therefore still a legal block target in the
//     engine -- and in the game, for the same reason. Checked, and left exactly
//     as it is, rather than "fixed" into monster_dead_or_escaped.
//
//     `record_alive` below likewise tests `hp > 0` only and does NOT test
//     kMonsterFlagEscaped, which is exact for the same reason plus one more: an
//     escape here means the leader is already dead, and the leader's death is
//     what ends the fight.
//
// (2) DRAW ACCOUNTING at A20 (one monster_hp_rng draw per ctor, one ai_rng draw
//     per rollMove -- AbstractMonster.java:765-775 and :705-715):
//       * Warrior / Thief / Tsundere / Wizard: exactly ONE ai_rng.random(99), at
//         init(); its value is discarded because each getMove forces a fixed
//         opening move. Every later transition is a direct setMove or a queued
//         SetMoveAction, neither of which draws.
//       * Fat: one ai_rng.random(99) at init() PLUS one per turn, because
//         GremlinFat.takeTurn ends in a real RollMoveAction (GremlinFat.java:80).
//         Its value is discarded too (getMove is unconditional), but the draw
//         moves the stream, so it is a QUEUED ROLL_MOVE item resolved at dequeue
//         time -- gremlin_fat_roll_move, registered in monster_roll_move_fn.
//       * Tsundere additionally draws ONE ai_rng per Protect turn: the block
//         target (GainBlockRandomMonsterAction.java:35). See (4).
//
// (3) THE WIZARD'S CHARGE COUNTER is per-instance state (`currentCharge`,
//     GremlinWizard.java:43, initialised to 1), kept in MonsterState.pad0 -- the
//     per-monster aux byte the Louse already uses for its rolled bite damage.
//     Charge counts 1 -> 2 -> 3; hitting 3 telegraphs Ultimate Blast, and casting
//     resets it to 0 (:86). At A17+ the cast re-telegraphs ITSELF (:92-94), so
//     from the third turn on the wizard blasts every turn and the counter never
//     advances again.
//
// (4) THE TSUNDERE'S BLOCK TARGET IS ROLLED IN takeTurn, NOT AT DEQUEUE.
//     GainBlockRandomMonsterAction picks its target when the queued action
//     RESOLVES (GainBlockRandomMonsterAction.java:28-39); this module picks it
//     inside gremlin_tsundere_take_turn and queues an already-targeted BLOCK.
//     That is bit-exact here, and the reason is a pump invariant rather than
//     luck: pump_step reaches the monster-turn step ONLY with the action,
//     pre-turn and card queues all empty (action_queue.cpp step 5 is behind steps
//     1-3), and a Protect turn queues exactly one action. So between the pick and
//     the resolve nothing can run -- no monster can start or stop dying, and
//     nothing else draws ai_rng (the only other ai_rng consumers are monster move
//     rolls, and no monster rolls during another monster's turn). Rolling at
//     dequeue would need a new opcode and would produce an identical stream.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

void gremlin_warrior_init(CombatState& state, uint8_t monster_index) noexcept;
void gremlin_thief_init(CombatState& state, uint8_t monster_index) noexcept;
void gremlin_fat_init(CombatState& state, uint8_t monster_index) noexcept;
void gremlin_tsundere_init(CombatState& state, uint8_t monster_index) noexcept;
void gremlin_wizard_init(CombatState& state, uint8_t monster_index) noexcept;

void gremlin_warrior_take_turn(CombatState& state,
                               uint8_t monster_index) noexcept;
void gremlin_thief_take_turn(CombatState& state,
                             uint8_t monster_index) noexcept;
void gremlin_fat_take_turn(CombatState& state, uint8_t monster_index) noexcept;
void gremlin_tsundere_take_turn(CombatState& state,
                                uint8_t monster_index) noexcept;
void gremlin_wizard_take_turn(CombatState& state,
                              uint8_t monster_index) noexcept;

// GremlinWarrior.usePreBattleAction (GremlinWarrior.java:63-70): addToBottom
// ApplyPowerAction(self, AngryPower(self, ascension >= 17 ? 2 : 1)). Draws no
// RNG. The Warrior is the only gremlin that overrides usePreBattleAction.
void gremlin_warrior_use_pre_battle_action(CombatState& state,
                                           uint8_t monster_index) noexcept;

// GremlinFat's queued RollMoveAction body (GremlinFat.java:80): one
// ai_rng.random(99) whose value GremlinFat.getMove ignores, then setMove(BLUNT,
// ATTACK_DEBUFF). Registered in monster_roll_move_fn because the draw must land
// at DEQUEUE time -- the Fat's own attack can queue player Thorns/Flame Barrier
// damage ahead of it, and RollMoveAction has no liveness check
// (RollMoveAction.java:17-21).
void gremlin_fat_roll_move(CombatState& state, uint8_t monster_index) noexcept;

// --- Mid-combat spawn (S2.23) -------------------------------------------------
// MonsterSpawnAtHpFn for all five gremlins: the Gremlin Leader summons them
// (SummonGremlinAction, GremlinLeader.java:108-109), so from S2.23 on every one
// of them can arrive mid-combat.
//
// WHY THEY TAKE A PRE-DRAWN HP even though the Java runs the gremlin's FULL
// constructor -- setHp draw and all -- rather than a 4-arg newHealth ctor like
// the slimes'. The draw happens, but it happens at QUEUE time, inside
// SummonGremlinAction's own constructor (`MonsterHelper.getGremlin(...)` at
// SummonGremlinAction.java:42, called from the action's ctor, which the Java
// evaluates at addToBottom). The record itself is not inserted until update().
// So the summoner draws the HP and hands it over, exactly as the split does
// (monster_slime_large.cpp) -- the two arrive at the same signature from
// opposite directions, and getting it wrong would put a monster_hp_rng draw one
// action-queue drain too late.
//
// Each is spawn-time field init + the same discarded init() rollMove the
// encounter-time init does: all five getMove overrides force a fixed opening
// move and ignore the num, but the ai_rng draw still happens (note (2) above).
// `draw_x` is written by the spawn path from the SUMMONER's POSX table, not
// here (monster_dispatch.hpp).
void gremlin_warrior_spawn_at_hp(CombatState& state, uint8_t monster_index,
                                 int16_t hp) noexcept;
void gremlin_thief_spawn_at_hp(CombatState& state, uint8_t monster_index,
                               int16_t hp) noexcept;
void gremlin_fat_spawn_at_hp(CombatState& state, uint8_t monster_index,
                             int16_t hp) noexcept;
void gremlin_tsundere_spawn_at_hp(CombatState& state, uint8_t monster_index,
                                  int16_t hp) noexcept;
void gremlin_wizard_spawn_at_hp(CombatState& state, uint8_t monster_index,
                                int16_t hp) noexcept;

}  // namespace sts::engine
