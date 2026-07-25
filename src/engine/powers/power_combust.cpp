// Combust -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_combust.hpp for what this power does.

#include "power_combust.hpp"

#include <cstdint>
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags, CardPile

namespace sts::engine {

void power_native_combust(CombatState& s, Hook hook,
                          const HookContext& ctx) noexcept {
    // CombustPower.atEndOfTurn (CombustPower.java:39-47): enqueue its
    // private hpLoss first, then THORNS damage to every enemy. stackPower
    // increments hpLoss once per reapplication; the player-only card path
    // persists that hidden counter in CombatState.flags.
    if (hook != Hook::AT_END_OF_TURN) {
        return;
    }
    bool any_live = false;
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        any_live = any_live || s.monsters[i].hp > 0;
    }
    if (!any_live) {
        return;
    }
    uint32_t hp_loss =
        (s.flags & kCombatFlagCombustHpLossMask) >> kCombatFlagCombustHpLossShift;
    if (hp_loss == 0u) {
        hp_loss = 1u;  // constructed fixture/direct power application
    }
    ActionQueueItem lose{};
    lose.opcode = static_cast<uint16_t>(Opcode::LOSE_HP);
    lose.src = ctx.owner;
    lose.tgt = ctx.owner;
    lose.amount = static_cast<int32_t>(hp_loss);
    add_to_bottom(s, lose);
    ActionQueueItem damage{};
    damage.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    damage.src = ctx.owner;
    damage.tgt = (ctx.owner == kActorPlayer) ? kActorAllEnemies : kActorPlayer;
    damage.amount = ctx.power_amount;
    damage.flags = make_damage_flags(DamageType::THORNS);
    add_to_bottom(s, damage);
}

}  // namespace sts::engine
