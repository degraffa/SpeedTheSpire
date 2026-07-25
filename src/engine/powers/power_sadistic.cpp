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
    // SadisticPower.onApplyPower (source side): on applying a DEBUFF to a
    // DIFFERENT creature that has no Artifact, deal `amount` THORNS damage
    // to that target. (Shackled excluded; no Shackled power in scope.)
    if (hook != Hook::ON_APPLY_POWER) {
        return;
    }
    const PowerId applied =
        static_cast<PowerId>(ctx.applied_power_id);
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
    add_to_bottom(s, dmg);       // addToBot (SadisticPower.java:43)
}

}  // namespace sts::engine
