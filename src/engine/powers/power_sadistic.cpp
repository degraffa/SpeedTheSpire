// Sadistic -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_sadistic.hpp for what this power does.

#include "power_sadistic.hpp"

#include <cstdint>
#include "power_native.hpp"             // find_power
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags, CardPile
#include "sts/engine/powers.hpp"        // power_def, PowerDef, PowerType
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_sadistic(CombatState& s, Hook hook,
                           const HookContext& ctx) noexcept {
    // SadisticPower.onApplyPower (SadisticPower.java:41-45), source side: on
    // applying a DEBUFF -- other than Shackled -- to a DIFFERENT creature that
    // has no Artifact, deal `amount` THORNS damage to that target.
    // `source == this.owner` (:42) needs no separate check here:
    // dispatch_on_apply_power_source (power_hooks.cpp) is the ONLY caller of
    // Hook::ON_APPLY_POWER and always dispatches on the SOURCE actor's own
    // power list (`dispatch_actor_powers(s, source, ...)`), so a Sadistic on
    // any actor OTHER than the source never receives this hook -- the Java
    // condition holds structurally, by construction of the dispatch.
    if (hook != Hook::ON_APPLY_POWER) {
        return;
    }
    const PowerId applied =
        static_cast<PowerId>(ctx.applied_power_id);
    if (applied == PowerId::SHACKLED) {
        // !power.ID.equals("Shackled") (SadisticPower.java:42). This branch
        // was dead code guarding against a power the registry did not yet
        // have until Dark Shackles' PowerId::SHACKLED (Dark Shackles' own
        // debuff) landed and made it reachable.
        return;
    }
    const PowerDef* ap = power_def(applied);
    const bool is_debuff = ap != nullptr && ap->type == PowerType::DEBUFF;
    if (!is_debuff || ctx.target == ctx.owner) {
        return;
    }
    if (find_power(s, ctx.target, PowerId::ARTIFACT) != nullptr) {
        return;  // Artifact target -> Sadistic skips (its own guard)
    }
    ActionQueueItem dmg{};
    dmg.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    dmg.src = ctx.owner;         // owner-owned THORNS damage
    dmg.tgt = ctx.target;
    dmg.amount = ctx.power_amount;
    dmg.flags = make_damage_flags(DamageType::THORNS);
    add_to_bottom(s, dmg);       // addToBot (SadisticPower.java:43)
}

}  // namespace sts::engine
