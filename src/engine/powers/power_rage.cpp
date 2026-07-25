// Rage -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_rage.hpp for what this power does.

#include "power_rage.hpp"

#include <cstdint>
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/cards.hpp"         // card_def, CardType (Corruption skill check)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags, CardPile
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_rage(CombatState& s, Hook hook,
                       const HookContext& ctx) noexcept {
    // RagePower.onUseCard (RagePower.
    // java:41-47): if the played card is an ATTACK, GainBlockAction(
    // player, amount) -- dispatched at ON_USE_CARD (after the card's own
    // effects are queued, so the block lands after the attack's damage;
    // a direct GainBlockAction -> kBlockNoPowers, no Dexterity/Frail).
    // atEndOfTurn (:49-52): addToBot RemoveSpecificPowerAction(owner,
    // "Rage") -- Rage lasts one turn.
    if (hook == Hook::ON_USE_CARD) {
        const CardDef* cd = card_def(static_cast<CardId>(ctx.card_id));
        if (cd == nullptr || cd->type != CardType::ATTACK) {
            return;  // the attack-type guard (RagePower.java:42)
        }
        ActionQueueItem blk{};
        blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
        blk.src = ctx.owner;
        blk.tgt = ctx.owner;  // owner == the player in every S1 scope
        blk.amount = ctx.power_amount;
        blk.flags = kBlockNoPowers;
        add_to_bottom(s, blk);  // addToBot (RagePower.java:43)
        return;
    }
    if (hook == Hook::AT_END_OF_TURN) {
        ActionQueueItem rem{};
        rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
        rem.src = ctx.owner;
        rem.tgt = ctx.owner;
        rem.flags = make_apply_power_flags(PowerId::RAGE);
        add_to_bottom(s, rem);  // addToBot (RagePower.java:50)
        return;
    }
}

}  // namespace sts::engine
