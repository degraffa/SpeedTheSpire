// Spore Cloud -- native power-hook body. One translation unit per power; see
// power_native.hpp for the dispatch plumbing and power_spore_cloud.hpp for what
// this power does and why it is native.

#include "power_spore_cloud.hpp"

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // add_to_top, ActionQueueItem, kActorPlayer
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

// AbstractRoom.isBattleEnding (AbstractRoom.java:628-635) reduced to what this
// engine models: `isBattleOver || monsters.areMonstersBasicallyDead()`, where
// areMonstersBasicallyDead (MonsterGroup.java:90-95) is "every monster is
// isDying || isEscaping" -- exactly monster_dead_or_escaped (combat_state.hpp),
// the same predicate action_queue.cpp's victory check and power_hooks.cpp's
// end-of-round walk use. (No Act-1 group pairs a Fungi Beast with a Looter, so
// the escaped term cannot fire here today; it is written anyway because the
// predicate IS areMonstersBasicallyDead, not a per-site approximation of it.)
// isBattleOver is not a separate latch here: the pump sets COMBAT_OVER from
// exactly this predicate, so the two collapse into one walk.
[[nodiscard]] bool battle_is_ending(const CombatState& s) noexcept {
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        if (!monster_dead_or_escaped(s.monsters[i])) {
            return false;
        }
    }
    return true;
}

}  // namespace

void power_native_spore_cloud(CombatState& s, Hook hook,
                              const HookContext& ctx) noexcept {
    // SporeCloudPower.onDeath (SporeCloudPower.java:35-42).
    if (hook != Hook::ON_DEATH) {
        return;
    }
    // The dying beast's own HP is already 0 at the dispatch site, and die() sets
    // isDying before the power walk (AbstractMonster.java:927), so it counts
    // itself here exactly as the Java does. The LAST monster's Spore Cloud
    // therefore releases nothing.
    if (battle_is_ending(s)) {
        return;  // (:36-38)
    }
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = ctx.owner;      // the dying beast (the Java passes a null source)
    apply.tgt = kActorPlayer;   // new VulnerablePower(AbstractDungeon.player, ...)
    apply.amount = ctx.power_amount;  // this.amount -- the stack it was built with
    apply.flags = make_apply_power_flags(PowerId::VULNERABLE);
    add_to_top(s, apply);       // addToTop, not addToBot (:41)
}

}  // namespace sts::engine
