// Panache -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_panache.hpp for what this power does.

#include "power_panache.hpp"

#include <cstdint>

#include "power_native.hpp"             // PowerListView / actor_power_list
#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_damage_flags, kPanacheCardAmount

namespace sts::engine {

// PanachePower (PanachePower.java:21-68), read in full.
//
// TWO NUMBERS, and they move independently -- which is the whole reason the
// schema grew a `counter`:
//   * `amount` is the 5-card countdown. The ctor hard-codes it to CARD_AMT == 5
//     (:27,34) regardless of what the card passes, stackPower NEVER touches it
//     (:46-50), onUseCard decrements it and rolls it back to 5 on reaching zero
//     (:53-61), and atStartOfTurn resets it to 5 (:63-67).
//   * `counter` is the private `damage` (:28). The ctor takes it from the card
//     (10 base / 14 upgraded, Panache.java:27,32,39) and stackPower ADDS the
//     re-application's amount to it (:48) -- so two Panaches deal 20, from ONE
//     shared countdown, not two 10s from two powers. Both of those follow from
//     the single merged slot; the apply-side arithmetic lives in op_apply_power.
//
// NATIVE, not a data program, for three reasons no static step list can express:
// the decrement-and-test countdown, the reset-and-fire branch, and the damage
// coming from the slot's second number rather than its stack amount.
void power_native_panache(CombatState& s, Hook hook,
                          const HookContext& ctx) noexcept {
    const PowerListView pv = actor_power_list(s, ctx.owner);
    if (ctx.power_slot >= pv.count) {
        return;  // defensive: the dispatcher's index must be live
    }
    PowerSlot& slot = pv.slots[ctx.power_slot];

    if (hook == Hook::AT_START_OF_TURN) {
        // atStartOfTurn (:63-67): `this.amount = 5`. A FLAT RESET, not a
        // top-up -- partial progress toward the 5th card is LOST at the turn
        // boundary. The accumulated damage is untouched.
        slot.amount = kPanacheCardAmount;
        return;
    }
    if (hook != Hook::ON_USE_CARD) {
        return;
    }
    // onUseCard (:52-61). Fired from the UseCardAction fan-out, so it counts
    // exactly the plays the game counts: the ctor runs for every successful
    // play including a Mayhem/Havoc autoplay, and NOT for the no-trigger
    // (dontTriggerOnUseCard) filing a failed autoplay takes -- resolve_card_play
    // (card_play.cpp) queues that filing without dispatching this fan-out.
    //
    // The decrement is SYNCHRONOUS, exactly as `--this.amount` is; only the
    // damage is queued. Note also that a Panache being PLAYED does not count
    // itself: its ApplyPowerAction is still sitting in the queue when this
    // fan-out runs for that same play (AbstractPlayer.useCard:1369-1370), so the
    // power does not yet exist to respond.
    slot.amount = static_cast<int16_t>(slot.amount - 1);
    if (slot.amount != 0) {  // `if (this.amount == 0)` (:55), not <= 0
        return;
    }
    slot.amount = kPanacheCardAmount;  // :57
    // addToBot DamageAllEnemiesAction(player, createDamageMatrix(damage, true),
    // THORNS, SLASH_DIAGONAL) (:58). isPureDamage == true means the matrix is
    // built WITHOUT applyPowers (DamageInfo.createDamageMatrix, DamageInfo.java:
    // 126-136), and the THORNS type independently skips every NORMAL-only power
    // hook -- so player Strength and target Vulnerable/Weak all leave this
    // number alone. Block still absorbs it (it is damage(), not a LoseHP).
    // The AoE fan-out (execute_opcode) skips dead/escaped records, matching
    // DamageAllEnemiesAction's own isDeadOrEscaped filter (:74).
    ActionQueueItem dmg{};
    dmg.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    dmg.src = ctx.owner;
    dmg.tgt = (ctx.owner == kActorPlayer) ? kActorAllEnemies : kActorPlayer;
    dmg.amount = slot.counter;
    dmg.flags = make_damage_flags(DamageType::THORNS);
    add_to_bottom(s, dmg);
}

}  // namespace sts::engine
