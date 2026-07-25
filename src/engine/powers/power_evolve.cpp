// Evolve -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_evolve.hpp for what this power does.

#include "power_evolve.hpp"

#include <cstdint>
#include "power_native.hpp"             // find_power
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/cards.hpp"         // card_def, CardType
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_damage_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_evolve(CombatState& s, Hook hook,
                         const HookContext& ctx) noexcept {
    // EvolvePower.onCardDraw: only a STATUS draw triggers, and No Draw
    // suppresses the response even if the draw was otherwise forced.
    if (hook != Hook::ON_CARD_DRAW ||
        find_power(s, ctx.owner, PowerId::NO_DRAW) != nullptr) {
        return;
    }
    const CardDef* cd = card_def(static_cast<CardId>(ctx.card_id));
    if (cd == nullptr || cd->type != CardType::STATUS) {
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
