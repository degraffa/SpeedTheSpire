// Reactive ("Compulsive") -- native power-hook body. One translation unit per
// power; see power_native.hpp for the dispatch plumbing and power_reactive.hpp
// for the guard set, the game_id mismatch and the multi-hit consequence.

#include "power_reactive.hpp"

#include <cstdint>

#include "power_native.hpp"             // PowerNativeSig, actor_power_list
#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActorPlayer
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_reactive(CombatState& s, Hook hook,
                           const HookContext& ctx) noexcept {
    if (hook != Hook::ON_ATTACKED) {
        return;
    }
    // ReactivePower.onAttacked (ReactivePower.java:39-45):
    //
    //   if (info.owner != null && info.type != HP_LOSS && info.type != THORNS
    //       && damageAmount > 0 && damageAmount < this.owner.currentHealth) {
    //       this.flash();
    //       this.addToBot(new RollMoveAction((AbstractMonster) this.owner));
    //   }
    //   return damageAmount;
    //
    // The two type exclusions are already the op_damage ON_ATTACKED dispatch
    // gate (NORMAL only), which is why ctx.damage_type is not consulted -- and
    // unlike Shifting this power genuinely HAS the guard, so the narrow
    // type-tolerant walk deliberately does not include it.
    if (ctx.source_null || ctx.amount <= 0) {
        return;
    }
    // The owner is a monster: RollMoveAction takes an AbstractMonster and the
    // only applier is the Writhing Mass's usePreBattleAction. A player owner is
    // unconstructible in Acts 1-3 and has no move to roll.
    if (ctx.owner == kActorPlayer || ctx.owner >= kMonsterCap) {
        return;
    }
    // `damageAmount < owner.currentHealth`, STRICT and PRE-hit: op_damage
    // dispatches ON_ATTACKED before it writes HP, so this reads the health the
    // Java's AbstractCreature.damage reads at the same point. An exactly-lethal
    // hit (damage == hp) and any overkill fall out here and re-roll nothing.
    if (!(ctx.amount < s.monsters[ctx.owner].hp)) {
        return;
    }
    // addToBot (:42) -- so within a multi-hit attack every hit queues its own
    // re-roll and they resolve in order after the card finishes. Each is a full
    // rollMove through the target's MonsterRollMoveFn, which for the Writhing
    // Mass is its recursive getMove with its own extra ai_rng draws.
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = ctx.owner;
    roll.tgt = ctx.owner;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
