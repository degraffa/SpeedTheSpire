// Fire Breathing -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_fire_breathing.hpp for what this power does.

#include "power_fire_breathing.hpp"

#include <cstdint>
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/cards.hpp"         // card_def, CardType
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_damage_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_fire_breathing(CombatState& s, Hook hook,
                                 const HookContext& ctx) noexcept {
    // FireBreathingPower.onCardDraw: a STATUS or CURSE draw deals its
    // amount as THORNS damage to every enemy (DamageAllEnemiesAction).
    if (hook != Hook::ON_CARD_DRAW) {
        return;
    }
    const CardDef* cd = card_def(static_cast<CardId>(ctx.card_id));
    if (cd == nullptr ||
        (cd->type != CardType::STATUS && cd->type != CardType::CURSE)) {
        return;
    }
    ActionQueueItem damage{};
    damage.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    damage.src = ctx.owner;
    damage.tgt = (ctx.owner == kActorPlayer) ? kActorAllEnemies : kActorPlayer;
    damage.amount = ctx.power_amount;
    damage.flags = make_damage_flags(DamageType::THORNS);
    add_to_bottom(s, damage);
}

}  // namespace sts::engine
