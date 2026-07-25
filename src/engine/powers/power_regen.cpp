// Regen -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_regen.hpp for what this power does.

#include "power_regen.hpp"

#include <cstdint>
#include "power_native.hpp"             // find_power
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_regen(CombatState& s, Hook hook,
                        const HookContext& ctx) noexcept {
    // RegenPower.atEndOfTurn -> RegenAction(owner, amount)
    // (RegenPower.java:35-38, RegenAction.java:34-47): heal `amount` (clamped
    // to max, only if currentHealth>0) then, for a PLAYER owner, decrement the
    // stack by 1 (remove at 0). The heal is applied directly -- no HEAL opcode
    // (the Blood Potion / Burning Blood precedent) and a heal has no queue
    // interplay with other end-of-turn effects.
    if (hook != Hook::AT_END_OF_TURN) {
        return;
    }
    int16_t* hp = nullptr;
    int16_t max_hp = 0;
    if (ctx.owner == kActorPlayer) {
        hp = &s.player_hp;
        max_hp = s.player_max_hp;
    } else if (ctx.owner < kMonsterCap) {
        hp = &s.monsters[ctx.owner].hp;
        max_hp = s.monsters[ctx.owner].max_hp;
    }
    if (hp != nullptr && *hp > 0) {
        int32_t v = static_cast<int32_t>(*hp) + ctx.power_amount;
        if (v > max_hp) {
            v = max_hp;
        }
        *hp = static_cast<int16_t>(v);
    }
    if (ctx.owner == kActorPlayer) {  // RegenAction decrement is isPlayer-gated
        PowerSlot* rp = find_power(s, ctx.owner, PowerId::REGEN);
        if (rp != nullptr) {
            rp->amount = static_cast<int16_t>(rp->amount - 1);
            if (rp->amount <= 0) {
                rp->power_id = static_cast<uint16_t>(PowerId::NONE);
            }
        }
    }
}

}  // namespace sts::engine
