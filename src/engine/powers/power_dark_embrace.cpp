// Dark Embrace -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_dark_embrace.hpp for what this power does.

#include "power_dark_embrace.hpp"

#include <cstdint>
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags, CardPile

namespace sts::engine {

void power_native_dark_embrace(CombatState& s, Hook hook,
                               const HookContext& ctx) noexcept {
    // DarkEmbracePower.onExhaust (Java:36-41) first checks
    // areMonstersBasicallyDead(); an exhaust after combat cannot draw.
    if (hook != Hook::ON_EXHAUST) {
        return;
    }
    bool any_live = false;
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        any_live = any_live || s.monsters[i].hp > 0;
    }
    if (!any_live) {
        return;
    }
    ActionQueueItem draw{};
    draw.opcode = static_cast<uint16_t>(Opcode::DRAW);
    draw.src = ctx.owner;
    draw.tgt = ctx.owner;
    draw.amount = ctx.power_amount;
    add_to_bottom(s, draw);
}

}  // namespace sts::engine
