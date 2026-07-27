// The shared atEndOfRound body of Vulnerable / Weak / Frail, plus the
// application-time justApplied predicate. See the header for why one body.

#include "power_duration_debuff.hpp"

#include <cstdint>

#include "power_native.hpp"             // find_power
#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActorPlayer
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"  // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void duration_debuff_at_end_of_round(CombatState& s, Hook hook,
                                     const HookContext& ctx,
                                     PowerId id) noexcept {
    if (hook != Hook::AT_END_OF_ROUND) {
        return;
    }
    PowerSlot* slot = find_power(s, ctx.owner, id);
    if (slot == nullptr) {
        return;
    }
    // `if (this.justApplied) { this.justApplied = false; return; }` -- the round
    // the debuff arrived is spent clearing the latch, not decrementing.
    if (slot->counter != 0) {
        slot->counter = 0;
        return;
    }
    ActionQueueItem item{};
    item.src = ctx.owner;
    item.tgt = ctx.owner;
    item.flags = make_apply_power_flags(id);
    if (slot->amount == 0) {
        // The Java's `if (this.amount == 0)` branch. Unreachable through this
        // path -- ReducePowerAction removes rather than reduces once the request
        // meets the stack (ReducePowerAction.java:45-51), so a slot never rests
        // at 0 -- but it is what the Java writes, and a power parked at 0 by some
        // other route must leave rather than tick forever.
        item.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
        item.amount = 0;
    } else {
        item.opcode = static_cast<uint16_t>(Opcode::REDUCE_POWER);
        item.amount = 1;
    }
    add_to_bottom(s, item);
}

bool duration_debuff_starts_just_applied(const CombatState& s, uint8_t tgt,
                                         PowerId id) noexcept {
    switch (id) {
        case PowerId::VULNERABLE:
            // `if (actionManager.turnHasEnded && isSourceMonster)`
            // (VulnerablePower.java:36-38) -- BOTH clauses. turn_has_ended is read
            // directly; the isSourceMonster clause needs no separate test in S1
            // scope, because the enemy phase is the only window in which
            // turn_has_ended is set and nothing but a monster applies Vulnerable
            // there. The two player-side appliers that pass false while targeting
            // the player -- Berserk (Berserk.java:33) and a Fungi Beast's Spore
            // Cloud on death (SporeCloudPower) -- both resolve during the player's
            // own turn, so both fall out unlatched exactly as the Java has them.
            // The `tgt == kActorPlayer` half is belt-and-braces: no S1 effect
            // applies Vulnerable to a monster during the enemy phase.
            return tgt == kActorPlayer && s.turn_has_ended != 0;
        case PowerId::WEAK:
        case PowerId::FRAIL:
            // `if (isSourceMonster)` alone (WeakPower.java:35-37,
            // FrailPower.java:32-34). In S1 scope that predicate is exactly
            // "the owner is the player": every application to the player passes
            // true (Act-1 monster moves, plus the Doubt and Shame curses' own
            // end-of-turn ApplyPowerAction -- Doubt.java:35, Shame.java:34), and
            // every application to a monster passes false (player cards, Weak
            // Potion, Champion's Belt). Recorded as an in-scope reading rather
            // than a universal one: a future card that weakens the PLAYER with
            // isSourceMonster=false would need the flag carried on the item.
            return tgt == kActorPlayer;
        default:
            return false;
    }
}

}  // namespace sts::engine
