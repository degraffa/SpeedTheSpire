// Regenerate Monster -- native power-hook body. One translation unit per
// power; see power_native.hpp for the dispatch plumbing and
// power_regenerate_monster.hpp for what this power does.

#include "power_regenerate_monster.hpp"

#include <cstdint>
#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_regenerate_monster(CombatState& s, Hook hook,
                                     const HookContext& ctx) noexcept {
    // RegenerateMonsterPower.atEndOfTurn -> HealAction(owner, owner, amount)
    // (RegenerateMonsterPower.java:37-43; HealAction.java:29-35): heal
    // `amount`, clamped to max HP, only if currentHealth>0. The owner is
    // always a monster (nothing constructs this power on the player) and its
    // halfDead/isDying/isDead guard is already covered by the AT_END_OF_TURN
    // dispatch walk skipping dead-or-escaped monsters before it ever reaches
    // this hook (power_hooks.cpp) -- halfDead has no S1 producer.
    //
    // DELIBERATELY NO DECREMENT: this is the entire distinction from
    // PowerId::REGEN (id 18) -- RegenPower.atEndOfTurn queues a RegenAction,
    // whose isPlayer-gated tail decays the stack (RegenAction.java:40-47);
    // RegenerateMonsterPower queues a bare HealAction, which has no such tail
    // at all, so this power's amount never decreases on its own.
    //
    // The heal is applied directly here -- no HEAL opcode (the Blood Potion /
    // Burning Blood / REGEN precedent) -- which is also why op_heal's
    // monster-target no-op comment (interp_damage.cpp) had to be updated to
    // name this power as its exception: the exception is resolved through
    // this native body, not through a case added to op_heal itself.
    //
    // DOCUMENTED DEVIATION: MonsterRoomElite.applyEmeraldEliteBuff addToBot's
    // one HealAction per group member (MonsterRoomElite.java:60-64), so the
    // Java's heals resolve from the action queue AFTER every member's
    // atEndOfTurn has been walked. This body instead writes each member's
    // heal synchronously, in-walk. Equivalent unless some other queued S1
    // effect needs to interleave between one monster's heal and the next's --
    // nothing in S1 does.
    if (hook != Hook::AT_END_OF_TURN) {
        return;
    }
    if (ctx.owner >= kMonsterCap) {
        return;
    }
    int16_t& hp = s.monsters[ctx.owner].hp;
    const int16_t max_hp = s.monsters[ctx.owner].max_hp;
    if (hp <= 0) {
        return;
    }
    int32_t v = static_cast<int32_t>(hp) + ctx.power_amount;
    if (v > max_hp) {
        v = max_hp;
    }
    hp = static_cast<int16_t>(v);
}

}  // namespace sts::engine
