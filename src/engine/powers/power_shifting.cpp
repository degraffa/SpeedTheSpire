// Shifting -- native power-hook body. One translation unit per power; see
// power_native.hpp for the dispatch plumbing and power_shifting.hpp for the
// addToTop order flip, the dead Artifact arm and the type-tolerance divergence.

#include "power_shifting.hpp"

#include <cstdint>

#include "power_native.hpp"             // PowerNativeSig, find_power
#include "sts/engine/action_queue.hpp"  // add_to_top / kActorPlayer
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_shifting(CombatState& s, Hook hook,
                           const HookContext& ctx) noexcept {
    if (hook != Hook::ON_ATTACKED) {
        return;
    }
    // ShiftingPower.onAttacked (ShiftingPower.java:32-41):
    //
    //   if (damageAmount > 0) {
    //       addToTop(new ApplyPowerAction(owner, owner,
    //                                     new StrengthPower(owner, -damageAmount),
    //                                     -damageAmount));
    //       if (!this.owner.hasPower("Artifact"))
    //           addToTop(new ApplyPowerAction(owner, owner,
    //                                         new GainStrengthPower(owner, damageAmount),
    //                                         damageAmount));
    //       this.flash();
    //   }
    //   return damageAmount;
    //
    // ONE CONDITION, and no others -- no `info.owner != null`, no damage-type
    // test. `ctx.source_null` is therefore NOT consulted here, deliberately: a
    // null-source hit (the Explosive Potion matrix) swings the Transient's
    // Strength in the game and must here too. The type half of that same absence
    // is what dispatch_on_attacked_type_tolerant exists for (see the header).
    if (ctx.amount <= 0) {
        return;
    }
    // -damageAmount Strength on the OWNER. Queued with add_to_top, matching
    // addToTop, and pushed FIRST -- which is what puts it SECOND in execution
    // order behind the Shackled below.
    ActionQueueItem str{};
    str.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    str.src = ctx.owner;
    str.tgt = ctx.owner;
    str.amount = -ctx.amount;
    str.flags = make_apply_power_flags(PowerId::STRENGTH);
    add_to_top(s, str);
    // `!this.owner.hasPower("Artifact")` -- a PRESENCE test taken NOW, at queue
    // time, exactly where the Java takes it. Dead in Acts 1-3 (no effect gives a
    // Transient Artifact) and bound anyway; see the header.
    //
    // This is not the debuff-nullify path: the -Strength above is a DEBUFF and
    // op_apply_power will separately consume an Artifact stack for it when it
    // resolves. Both relationships exist in the Java too, and reproducing one
    // does not stand in for the other.
    if (find_power(s, ctx.owner, PowerId::ARTIFACT) == nullptr) {
        ActionQueueItem shackled{};
        shackled.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        shackled.src = ctx.owner;
        shackled.tgt = ctx.owner;
        shackled.amount = ctx.amount;
        // GainStrengthPower == PowerId::SHACKLED (powers.yaml id 78, POWER_ID
        // "Shackled"). Its own at_end_of_turn program applies `amount` Strength
        // to the owner and then removes itself, which is the give-back half of
        // the swap; nothing about that row changes for this producer.
        shackled.flags = make_apply_power_flags(PowerId::SHACKLED);
        // Pushed SECOND, so it resolves FIRST. That order is the Java's and it
        // is observable inside a multi-hit attack.
        add_to_top(s, shackled);
    }
}

}  // namespace sts::engine
