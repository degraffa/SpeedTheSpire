// Beat of Death -- native power-hook body. One translation unit per power; see
// power_native.hpp for the dispatch plumbing and power_beat_of_death.hpp for
// what this power does and why the type and the hook are both load-bearing.

#include "power_beat_of_death.hpp"

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActorPlayer
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_damage_flags, DamageType

namespace sts::engine {

void power_native_beat_of_death(CombatState& s, Hook hook,
                                const HookContext& ctx) noexcept {
    if (hook != Hook::ON_AFTER_USE_CARD) {
        return;
    }
    // BeatOfDeathPower.onAfterUseCard (BeatOfDeathPower.java:39-44). NO card
    // filter: the Java body inspects neither `card` nor `action`, so a POWER, a
    // status, a curse and an exhausted attack all pulse alike. The one play this
    // hook never sees is filtered UPSTREAM, in the caller: UseCardAction.update
    // skips the whole fan-out for a `dontTriggerOnUseCard` card (:83) -- an
    // end-of-turn Burn/Decay/Doubt/Regret/Shame self-play -- and op_use_card
    // carries that guard (kUseCardDontTriggerOnUseCard), exactly as it does for
    // Time Warp.
    ActionQueueItem dmg{};
    dmg.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    dmg.src = ctx.owner;      // DamageInfo(this.owner, ...) -- the Heart
    dmg.tgt = kActorPlayer;   // ... aimed at AbstractDungeon.player (:42)
    dmg.amount = ctx.power_amount;  // `this.amount`, the live stack
    dmg.flags = make_damage_flags(DamageType::THORNS);
    add_to_bottom(s, dmg);    // addToBot (:42)
}

}  // namespace sts::engine
