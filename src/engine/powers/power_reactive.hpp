#pragma once

// Reactive -- native power-hook body (registry/powers.yaml id 105,
// PowerId::REACTIVE, game_id "Compulsive").
//
// THE CLASS NAME IS A LIE AND THE REGISTRY KNOWS IT. The Java class is
// ReactivePower but its POWER_ID literal is "Compulsive"
// (ReactivePower.java:19), and the oracle joins on the literal -- so the row's
// `game_id` is "Compulsive" while the enumerator keeps the class's name. The
// NoBlock row (id 77) is the same shape of mismatch.
//
// The Writhing Mass's re-telegraph (ReactivePower.java, read in full). ONE live
// hook:
//
//   * ON_ATTACKED (:39-45)  a real, NON-LETHAL, NORMAL hit queues a ROLL_MOVE
//                           on the owner -- the monster picks a NEW next move
//                           in the middle of the player's turn.
//
// It modifies no damage number and no block number (onAttacked returns
// `damageAmount` unchanged, :44), so none of the count-guarded switches in
// interp_damage.cpp / interp_block.cpp gains a case.
//
// THIS IS MALLEABLE'S GUARD SET VERBATIM WITH A RollMoveAction WHERE THE
// GainBlockAction WAS. Compare MalleablePower.java:62-75 line for line:
//
//   * `info.owner != null`                 -> ctx.source_null
//   * `info.type != HP_LOSS && != THORNS`  -> already the dispatch gate
//                                             (op_damage fires ON_ATTACKED only
//                                             for NORMAL src != tgt damage), and
//                                             UNLIKE Shifting this power really
//                                             does have the guard, so the
//                                             narrow type-tolerant walk must NOT
//                                             reach it
//   * `damageAmount > 0`                   -> ctx.amount, the POST-block number,
//                                             so a fully-blocked hit re-rolls
//                                             nothing
//   * `damageAmount < owner.currentHealth` -> STRICT, and the strictness is the
//                                             point: an EXACTLY-lethal hit and
//                                             any overkill re-roll NOTHING. The
//                                             health read is PRE-hit, which is
//                                             where AbstractCreature.damage puts
//                                             it too.
//
// NO NEW OPCODE. Opcode::ROLL_MOVE (8) already exists and roll_monster_move
// dispatches through monster_roll_move_fn (monster_dispatch.hpp), which until
// this batch answered only for the large slimes. The whole framework cost of
// this power is that the Writhing Mass registers a MonsterRollMoveFn.
//
// TWO CONSEQUENCES WORTH SPELLING OUT.
//
// (1) addToBOT (:42), so the re-roll resolves after the rest of the player's
//     card. A THREE-HIT attack therefore queues THREE re-rolls and the Writhing
//     Mass runs its (recursive, randomBoolean-spending) getMove three times --
//     the sharpest ai_rng consumer in Acts 1-3.
//
// (2) The re-roll is a FULL rollMove with `firstMove` already spent, so it can
//     select MEGA_DEBUFF. A Writhing Mass can therefore be made to grant its
//     Parasite DURING THE PLAYER'S TURN rather than on its own, which is a
//     directed test rather than a curiosity.
//
// PRIORITY 50 (:30) is a registry column, not a fact this file acts on, but it
// is why the slot sorts where it does -- and slot order is the atDamage* walk
// order and every fan-out's order, so it is a number rather than a decoration
// (the Flight precedent).
//
// APPLIED BEFORE MALLEABLE. WrithingMass.usePreBattleAction (:80-83) queues
// Reactive first and Malleable second, which is the power-list order every
// fan-out walks -- so on a single hit the re-roll is queued before the block.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_reactive(CombatState& state, Hook hook,
                           const HookContext& ctx) noexcept;

}  // namespace sts::engine
