// Invincible -- native power-hook body. One translation unit per power; see
// power_native.hpp for the dispatch plumbing and power_invincible.hpp for why
// only ONE of this power's two overrides is here and where the other one lives.

#include "power_invincible.hpp"

#include <cstdint>

#include "power_native.hpp"  // PowerNativeSig, actor_power_list
#include "sts/engine/combat_state.hpp"

namespace sts::engine {

void power_native_invincible(CombatState& s, Hook hook,
                             const HookContext& ctx) noexcept {
    if (hook != Hook::AT_START_OF_TURN) {
        return;  // the cap/drain is the apply_buffer site, not a hook
    }
    // InvinciblePower.atStartOfTurn (InvinciblePower.java:95-99):
    //     this.amount = this.maxAmt;
    // A FULL REFILL, not a decay, and unconditional -- a pool the player emptied
    // last turn is back at 300 (200 at A19+) when the Heart's own turn begins.
    // Synchronous, exactly as the Java's field write is; Flight's identical
    // restore at the same dispatch site is the precedent.
    //
    // Written through ctx.power_slot rather than a find-by-id: the dispatcher
    // names WHICH slot is speaking, and that is the contract even for a power
    // that is not instanced. `maxAmt` is that slot's `counter`.
    const PowerListView pv = actor_power_list(s, ctx.owner);
    if (ctx.power_slot < pv.count) {
        pv.slots[ctx.power_slot].amount =
            static_cast<int16_t>(ctx.power_counter);  // maxAmt
    }
}

}  // namespace sts::engine
