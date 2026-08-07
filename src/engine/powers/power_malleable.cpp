// Malleable -- native power-hook body. One translation unit per power; see
// power_native.hpp for the dispatch plumbing and power_malleable.hpp for what
// this power does and why basePower lives in PowerSlot.counter.

#include "power_malleable.hpp"

#include <cstdint>

#include "power_native.hpp"             // PowerNativeSig, actor_power_list
#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, kBlockNoPowers
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_malleable(CombatState& s, Hook hook,
                            const HookContext& ctx) noexcept {
    if (hook == Hook::ON_ATTACKED) {
        // MalleablePower.onAttacked (MalleablePower.java:62-75):
        //
        //   if (damageAmount < this.owner.currentHealth && damageAmount > 0
        //       && info.owner != null && info.type == DamageInfo.DamageType.NORMAL
        //       && info.type != DamageInfo.DamageType.HP_LOSS) {
        //       this.flash();
        //       this.owner.isPlayer ? addToTop(...) : addToBot(...)
        //           new GainBlockAction(this.owner, this.owner, this.amount);
        //       ++this.amount;
        //       this.updateDescription();
        //   }
        //   return damageAmount;
        //
        // FIVE CONDITIONS, and each maps somewhere here:
        //
        //  * `info.type == NORMAL` (and the redundant `!= HP_LOSS` behind it) is
        //    already the dispatch gate: op_damage fires ON_ATTACKED only for
        //    NORMAL damage (interp_damage.cpp), which is also why ctx.damage_type
        //    is not consulted. THORNS and HP_LOSS therefore never reach here --
        //    a Flame Barrier reflect and an Offering do not feed the plant.
        //  * `info.owner != null` is ctx.source_null: a null-source pure-matrix
        //    hit triggers nothing.
        //  * `damageAmount > 0` is ctx.amount -- a hit the owner's BLOCK fully
        //    absorbed does NOT trigger, because op_damage passes the post-block
        //    number.
        //  * `damageAmount < owner.currentHealth` is STRICT, and the strictness
        //    is the interesting half: an EXACTLY-lethal hit (damage == hp) and
        //    any overkill trigger NOTHING -- no block, no escalation. The health
        //    read is PRE-HIT: op_damage dispatches ON_ATTACKED before it writes
        //    HP, which is where AbstractCreature.damage puts it too.
        if (ctx.source_null || ctx.amount <= 0) {
            return;
        }
        int32_t owner_hp = 0;
        if (ctx.owner == kActorPlayer) {
            owner_hp = s.player_hp;
        } else if (ctx.owner < kMonsterCap) {
            owner_hp = s.monsters[ctx.owner].hp;
        } else {
            return;
        }
        if (!(ctx.amount < owner_hp)) {
            return;  // exactly-lethal and overkill both fall out here
        }
        // The block is QUEUED for a monster owner (addToBot) and would be
        // addToTop for a player one; no S1/S2 effect gives the player Malleable,
        // so only the addToBot arm is reachable and it is the one written. It is
        // a DIRECT GainBlockAction, so kBlockNoPowers -- the owner's own
        // Dexterity/Frail do not scale it, exactly as for every other
        // GainBlockAction in the engine.
        //
        // The AMOUNT queued is the value BEFORE this hit's escalation
        // (`this.amount` is read at :70, incremented at :72), which is what makes
        // a triple-hit attack pay 3 + 4 + 5 rather than 4 + 5 + 6.
        ActionQueueItem blk{};
        blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
        blk.src = ctx.owner;
        blk.tgt = ctx.owner;
        blk.amount = ctx.power_amount;
        blk.flags = kBlockNoPowers;
        add_to_bottom(s, blk);
        // ++this.amount -- SYNCHRONOUS, so the next hit of the same multi-hit
        // attack sees the escalated value even though the blocks are still
        // queued. Written through ctx.power_slot rather than a find-by-id, so it
        // is the slot that is speaking; the dispatcher guarantees the index.
        const PowerListView pv = actor_power_list(s, ctx.owner);
        if (ctx.power_slot < pv.count) {
            PowerSlot& slot = pv.slots[ctx.power_slot];
            if (slot.amount < INT16_MAX) {
                slot.amount = static_cast<int16_t>(slot.amount + 1);
            }
        }
        return;
    }
    if (hook == Hook::AT_END_OF_TURN) {
        // MalleablePower.atEndOfTurn(isPlayer) (MalleablePower.java:44-51):
        //     if (this.owner.isPlayer) return;
        //     this.amount = this.basePower;
        // A MONSTER-owner reset back to the starting 3 -- so every stack the
        // plant earned from the player's attacks is given up at the end of the
        // turn, and the escalation is a within-turn mechanic, not a ramp.
        // Dispatched from the monster walk in dispatch_at_end_of_round
        // (power_hooks.cpp).
        if (ctx.owner == kActorPlayer) {
            return;  // the isPlayer early return
        }
        const PowerListView pv = actor_power_list(s, ctx.owner);
        if (ctx.power_slot < pv.count) {
            pv.slots[ctx.power_slot].amount =
                static_cast<int16_t>(ctx.power_counter);  // basePower
        }
        return;
    }
    if (hook == Hook::AT_END_OF_ROUND) {
        // MalleablePower.atEndOfRound (MalleablePower.java:53-60) is the exact
        // MIRROR: `if (!this.owner.isPlayer) return; this.amount = basePower;`.
        //
        // DEAD IN S1 AND S2 -- nothing gives the player Malleable (the Snake
        // Plant's usePreBattleAction applies it to itself, SnakePlant.java:69-72,
        // and no card, relic or potion grants it) -- and bound anyway. It is real
        // Java, it costs one branch, and the alternative is an unbound hook whose
        // absence is invisible. If a player-side granter ever lands, this is
        // already correct rather than a hole.
        if (ctx.owner != kActorPlayer) {
            return;  // the !isPlayer early return
        }
        const PowerListView pv = actor_power_list(s, ctx.owner);
        if (ctx.power_slot < pv.count) {
            pv.slots[ctx.power_slot].amount =
                static_cast<int16_t>(ctx.power_counter);
        }
        return;
    }
}

}  // namespace sts::engine
