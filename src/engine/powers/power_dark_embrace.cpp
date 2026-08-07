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
    // This walk was `hp > 0`, which did NOT match the comment above it and was
    // a real divergence -- found while classifying the liveness call sites for
    // the halfDead split, not by a failing test. `areMonstersBasicallyDead` is
    // `isDying || isEscaping` per monster, so an ESCAPED monster (positive hp,
    // gone from the fight) made the old test answer "still fighting" and Dark
    // Embrace kept drawing after a Looter left. Two neighbouring sites --
    // relics_rare.cpp's Dead Branch and power_magnetism.cpp -- both cite Dark
    // Embrace as using "the same gate"; that was false until now.
    // monster_basically_dead is the shared predicate, and it gets the halfDead
    // term right for free.
    bool any_live = false;
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        any_live = any_live || !monster_basically_dead(s.monsters[i]);
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
