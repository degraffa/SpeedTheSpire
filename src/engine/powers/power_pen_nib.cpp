// Pen Nib -- native power-hook body. One translation unit per power; see
// power_native.hpp for the dispatch plumbing and power_pen_nib.hpp for what
// this power does.

#include "power_pen_nib.hpp"

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActor*
#include "sts/engine/cards.hpp"         // card_def, CardType (the ATTACK guard)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_pen_nib(CombatState& s, Hook hook,
                          const HookContext& ctx) noexcept {
    // PenNibPower.onUseCard (PenNibPower.java:39-44):
    //     if (card.type == ATTACK)
    //         addToBot(new RemoveSpecificPowerAction(owner, owner, "Pen Nib"));
    //
    // addToBot, exactly as for Vigor and for the same reason: the removal
    // resolves after the played attack's own actions, so EVERY hit of a
    // multi-hit attack is doubled and only then is the power spent. A SKILL or
    // POWER play does not consume it.
    //
    // The doubling itself is NOT here: atDamageGive (:51-57) is a slot-ordered
    // pass in compute_damage and lives as a case in interp_damage.cpp's
    // at_damage_give, where PowerSlot ORDER (priority 6) is what places it after
    // Strength/Vigor and before Frail/Intangible/Weak.
    if (hook != Hook::ON_USE_CARD) {
        return;
    }
    const CardDef* cd = card_def(static_cast<CardId>(ctx.card_id));
    if (cd == nullptr || cd->type != CardType::ATTACK) {
        return;  // the attack-type guard (PenNibPower.java:41)
    }
    ActionQueueItem rem{};
    rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
    rem.src = ctx.owner;
    rem.tgt = ctx.owner;
    rem.flags = make_apply_power_flags(PowerId::PEN_NIB);
    add_to_bottom(s, rem);  // addToBot (PenNibPower.java:42)
}

}  // namespace sts::engine
