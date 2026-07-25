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
// (1) NOBODY IN THIS GANG ESCAPES. Every gremlin has a move 99 whose takeTurn
//     case queues EscapeAction, reachable only once `escapeNext` is latched or
//     `deathReact` re-telegraphs. `escapeNext()` (AbstractMonster.java:908-910)
//     has NO caller anywhere in the decompiled tree, and the only `deathReact()`
//     call is BanditBear.java:131 -- the Act-2 "Bandits" group. Act 1's gremlin
//     encounter therefore never sets either, so the `if (this.escapeNext)` guard
//     in every takeTurn below always takes the else branch and move 99 is
//     unreachable. It is not modelled here at all (registry/monsters.yaml records
//     the same, with the two halves an Act-2 task must add together).
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

}  // namespace sts::engine
