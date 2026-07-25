// Plated Armor -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_plated_armor.hpp for what this power does.

#include "power_plated_armor.hpp"

#include <cstdint>
#include "power_native.hpp"             // find_power
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_plated_armor(CombatState& s, Hook hook,
                               const HookContext& ctx) noexcept {
    if (hook == Hook::AT_END_OF_TURN_PRE_CARD) {
        // GainBlockAction(owner, amount) at the §5.4 pre-card phase (the same
        // slot as Metallicize). A direct GainBlockAction -> kBlockNoPowers, so
        // Plated Armor block does NOT get Dexterity (PlatedArmorPower.java:72).
        ActionQueueItem blk{};
        blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
        blk.src = ctx.owner;
        blk.tgt = ctx.owner;
        blk.amount = ctx.power_amount;
        blk.flags = kBlockNoPowers;
        add_to_bottom(s, blk);  // addToBot (PlatedArmorPower.java:72)
        return;
    }
    if (hook == Hook::WAS_HP_LOST) {
        // Lose 1 stack on a real attack from a distinct creature; NOT on a
        // THORNS / HP_LOSS / self loss (PlatedArmorPower.java:54-58). The
        // ReducePowerAction removes the power at 0.
        if (ctx.damage_type == static_cast<uint8_t>(DamageType::THORNS) ||
            ctx.damage_type == static_cast<uint8_t>(DamageType::HP_LOSS) ||
            ctx.source == ctx.owner || ctx.amount <= 0) {
            return;
        }
        PowerSlot* pa = find_power(s, ctx.owner, PowerId::PLATED_ARMOR);
        if (pa != nullptr) {
            pa->amount = static_cast<int16_t>(pa->amount - 1);
            if (pa->amount <= 0) {
                pa->power_id = static_cast<uint16_t>(PowerId::NONE);
            }
        }
        return;
    }
}

}  // namespace sts::engine
