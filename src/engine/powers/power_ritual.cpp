// Ritual -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_ritual.hpp for what this power does.

#include "power_ritual.hpp"

#include <cstdint>
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_ritual(CombatState& s, Hook hook,
                         const HookContext& ctx) noexcept {
    // RitualPower has two branches on `onPlayer`; we key it on the OWNER
    // actor (the potion applies to the PLAYER; the Cultist to ITSELF):
    //   * atEndOfTurn (RitualPower.java:38-43, isPlayer guard): the
    //     player/potion Ritual gains `amount` Strength each end of turn.
    //     Only the player is dispatched AT_END_OF_TURN (dispatch_at_end_of_
    //     turn), and a monster owner is guarded out here for safety.
    //   * atEndOfRound (:46-55, !onPlayer + skipFirst): the monster Cultist
    //     Ritual gains `amount` Strength each round AFTER the first. The
    //     skipFirst state is the owner monster's kMonsterFlagRitualSkip bit,
    //     set by the Cultist when it casts Incantation.
    if (hook == Hook::AT_END_OF_TURN) {
        if (ctx.owner != kActorPlayer) {
            return;  // RitualPower.atEndOfTurn's isPlayer guard
        }
        ActionQueueItem up{};
        up.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        up.src = ctx.owner;
        up.tgt = ctx.owner;
        up.amount = ctx.power_amount;  // +amount Strength
        up.flags = make_apply_power_flags(PowerId::STRENGTH);
        add_to_bottom(s, up);          // addToBot (RitualPower.java:41)
        return;
    }
    if (hook == Hook::AT_END_OF_ROUND) {
        if (ctx.owner == kActorPlayer || ctx.owner >= kMonsterCap) {
            return;  // player Ritual is onPlayer -> atEndOfRound no-op
        }
        uint32_t& mf = s.monsters[ctx.owner].flags;
        if ((mf & kMonsterFlagRitualSkip) != 0u) {
            mf &= ~kMonsterFlagRitualSkip;
            return;  // skipFirst: consume, no Strength this round
        }
        ActionQueueItem up{};
        up.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        up.src = ctx.owner;
        up.tgt = ctx.owner;
        up.amount = ctx.power_amount;  // +amount Strength
        up.flags = make_apply_power_flags(PowerId::STRENGTH);
        add_to_bottom(s, up);          // addToBot (RitualPower.java:50)
        return;
    }
}

}  // namespace sts::engine
