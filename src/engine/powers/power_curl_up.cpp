// Curl Up -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_curl_up.hpp for what this power does.

#include "power_curl_up.hpp"

#include <cstdint>
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_curl_up(CombatState& s, Hook hook,
                          const HookContext& ctx) noexcept {
    // CurlUpPower.onAttacked (CurlUpPower.java:36-46): the FIRST NORMAL,
    // non-lethal (damageAmount < owner.currentHealth), >0 attack makes the
    // louse gain `amount` Block, then removes Curl Up (one-shot -- modelled
    // by the self-removal). op_damage already gated dispatch_on_attacked to
    // NORMAL src != tgt AFTER decrementBlock, with the post-block damage and
    // the owner's pre-hit HP, so `ctx.amount` == damageAmount and the owner's
    // hp is still currentHealth here.
    if (hook != Hook::ON_ATTACKED) {
        return;
    }
    if (ctx.source == ctx.owner || ctx.amount <= 0 ||
        ctx.owner >= kMonsterCap) {
        return;
    }
    MonsterState& owner = s.monsters[ctx.owner];
    if ((owner.flags & kMonsterFlagCurlUpTriggered) != 0u) {
        return;  // triggered latch flips before queued actions resolve
    }
    if (ctx.amount >= owner.hp) {
        return;  // damageAmount < owner.currentHealth guard (lethal skips)
    }
    // CurlUpPower.java:40 sets triggered=true synchronously, before the
    // GainBlock/RemoveSpecificPower actions it adds to the bottom. This is
    // observable for queued multi-hit attacks: later hits run while Curl Up
    // is still present but must not queue another block gain.
    owner.flags |= kMonsterFlagCurlUpTriggered;
    ActionQueueItem blk{};
    blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
    blk.src = ctx.owner;
    blk.tgt = ctx.owner;
    blk.amount = ctx.power_amount;  // Block = Curl Up amount
    blk.flags = kBlockNoPowers;     // GainBlockAction (direct, no Dexterity)
    add_to_bottom(s, blk);          // addToBot (CurlUpPower.java:42)
    ActionQueueItem rem{};
    rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
    rem.src = ctx.owner;
    rem.tgt = ctx.owner;
    rem.flags = make_apply_power_flags(PowerId::CURL_UP);
    add_to_bottom(s, rem);          // addToBot (CurlUpPower.java:43)
}

}  // namespace sts::engine
