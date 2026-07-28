// Vigor -- native power-hook body. One translation unit per power; see
// power_native.hpp for the dispatch plumbing and power_vigor.hpp for what this
// power does.

#include "power_vigor.hpp"

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActor*
#include "sts/engine/cards.hpp"         // card_def, CardType (the ATTACK guard)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_vigor(CombatState& s, Hook hook,
                        const HookContext& ctx) noexcept {
    // VigorPower.onUseCard (VigorPower.java:49-55):
    //     if (card.type == ATTACK) { flash();
    //         addToBot(new RemoveSpecificPowerAction(owner, owner, "Vigor")); }
    //
    // addToBot is load-bearing, not incidental: the removal is queued BEHIND the
    // played card's own actions, so every hit of a multi-hit attack (Twin Strike,
    // Pummel) is boosted before Vigor leaves. A SKILL or POWER play leaves it
    // standing entirely -- unlike an end-of-turn power, Vigor is spent by attacks
    // and by nothing else.
    //
    // The damage side is NOT here: atDamageGive (:41-47) is a slot-ordered pass
    // in compute_damage, so it lives as a case in interp_damage.cpp's
    // at_damage_give.
    if (hook != Hook::ON_USE_CARD) {
        return;
    }
    const CardDef* cd = card_def(static_cast<CardId>(ctx.card_id));
    if (cd == nullptr || cd->type != CardType::ATTACK) {
        return;  // the attack-type guard (VigorPower.java:51)
    }
    ActionQueueItem rem{};
    rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
    rem.src = ctx.owner;
    rem.tgt = ctx.owner;
    rem.flags = make_apply_power_flags(PowerId::VIGOR);
    add_to_bottom(s, rem);  // addToBot (VigorPower.java:53)
}

}  // namespace sts::engine
