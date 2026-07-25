// Sharp Hide -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_sharp_hide.hpp for what this power does.

#include "power_sharp_hide.hpp"

#include <cstdint>
#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActorPlayer
#include "sts/engine/cards.hpp"         // card_def, CardType (the ATTACK guard)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_damage_flags, DamageType

namespace sts::engine {

void power_native_sharp_hide(CombatState& s, Hook hook,
                             const HookContext& ctx) noexcept {
    // SharpHidePower.onUseCard (SharpHidePower.java:43-49): if card.type ==
    // ATTACK, addToBot DamageAction(player, DamageInfo(owner, amount, THORNS)).
    // Dispatched at ON_USE_CARD, where power_hooks.cpp fans out to the monsters'
    // powers LAST (UseCardAction.java:41-64) -- so the retaliation is queued
    // behind the attack's own effects and lands after them.
    if (hook != Hook::ON_USE_CARD) {
        return;
    }
    const CardDef* cd = card_def(static_cast<CardId>(ctx.card_id));
    if (cd == nullptr || cd->type != CardType::ATTACK) {
        return;  // the card-type guard (SharpHidePower.java:45)
    }
    ActionQueueItem dmg{};
    dmg.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    dmg.src = ctx.owner;        // DamageInfo(this.owner, ...) -- the Guardian
    dmg.tgt = kActorPlayer;     // ... aimed at AbstractDungeon.player (:47)
    dmg.amount = ctx.power_amount;
    dmg.flags = make_damage_flags(DamageType::THORNS);
    add_to_bottom(s, dmg);      // addToBot (SharpHidePower.java:47)
}

}  // namespace sts::engine
