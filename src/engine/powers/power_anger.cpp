// Anger -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_anger.hpp for what this power does.

#include "power_anger.hpp"

#include <cstdint>
#include "sts/engine/action_queue.hpp"  // add_to_top / kActor*
#include "sts/engine/cards.hpp"         // card_def, CardType (the SKILL guard)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_anger(CombatState& s, Hook hook,
                        const HookContext& ctx) noexcept {
    // AngerPower.onUseCard (AngerPower.java:39-45): if the played card's type is
    // SKILL, addToTop ApplyPowerAction(owner, owner, StrengthPower(owner,
    // amount), amount). ATTACK and POWER cards do nothing -- that type guard is
    // the whole reason this power is native rather than a data binding.
    //
    // The owner is the monster that cast Bellow, so `src`/`tgt` are both
    // ctx.owner: the Strength lands on the Nob, not on the player who played the
    // card. Queued addToTop (AngerPower.java:41), so it resolves BEFORE anything
    // the card itself queued afterwards.
    if (hook != Hook::ON_USE_CARD) {
        return;
    }
    const CardDef* cd = card_def(static_cast<CardId>(ctx.card_id));
    if (cd == nullptr || cd->type != CardType::SKILL) {
        return;  // the SKILL-type guard (AngerPower.java:40)
    }
    ActionQueueItem up{};
    up.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    up.src = ctx.owner;
    up.tgt = ctx.owner;
    up.amount = ctx.power_amount;  // +amount Strength
    up.flags = make_apply_power_flags(PowerId::STRENGTH);
    add_to_top(s, up);  // addToTop (AngerPower.java:41)
}

}  // namespace sts::engine
