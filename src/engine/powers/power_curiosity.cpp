// Curiosity -- native power-hook body. One translation unit per power; see
// power_native.hpp for the dispatch plumbing and power_curiosity.hpp for what
// this power does.

#include "power_curiosity.hpp"

#include <cstdint>

#include "power_native.hpp"             // PowerNativeSig (the shared handler signature)
#include "sts/engine/action_queue.hpp"  // add_to_bottom
#include "sts/engine/cards.hpp"         // card_def, CardType (the POWER guard)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_curiosity(CombatState& s, Hook hook,
                            const HookContext& ctx) noexcept {
    if (hook != Hook::ON_USE_CARD) {
        return;
    }
    // CuriosityPower.onUseCard (CuriosityPower.java:34-39):
    //
    //     if (card.type == AbstractCard.CardType.POWER) {
    //         this.flash();
    //         this.addToBot(new ApplyPowerAction(this.owner, this.owner,
    //             new StrengthPower(this.owner, this.amount), this.amount));
    //     }
    //
    // An EQUALITY test on POWER, which is the mirror image of Hex's bare
    // `!= ATTACK`: a STATUS or CURSE play triggers Hex and does NOT trigger this.
    const CardDef* cd = card_def(static_cast<CardId>(ctx.card_id));
    if (cd == nullptr || cd->type != CardType::POWER) {
        return;  // the card-type guard (:36) -- the ONE thing that makes this native
    }
    // ApplyPowerAction(owner, owner, StrengthPower(owner, amount), amount): both
    // the source and the target are the power's OWNER, which for this power is
    // always a monster (the Awakened One applies it to itself). Routing it
    // through APPLY_POWER rather than writing the slot keeps the priority re-sort
    // and the Artifact/onApplyPower interception -- Strength is a BUFF, so
    // nothing intercepts it, but the door is the door.
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = ctx.owner;
    apply.tgt = ctx.owner;
    apply.amount = ctx.power_amount;  // `this.amount` -- the Curiosity stack (:38)
    apply.flags = make_apply_power_flags(PowerId::STRENGTH);
    add_to_bottom(s, apply);  // addToBot (:37)
}

}  // namespace sts::engine
