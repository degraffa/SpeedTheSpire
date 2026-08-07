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
    // always a monster (nothing constructs this power on the player).
    //
    // Its halfDead/isDying/isDead guard (RegenerateMonsterPower.java:38-42) is
    // NOT covered by the dispatch walk, and used to be: the AT_END_OF_TURN walk
    // now skips only monster_basically_dead, so a HALF-DEAD monster does reach
    // this hook -- which is faithful, because the Java's applyEndOfTurnPowers
    // walk skips only isDying/isEscaping too, and is exactly why the power
    // carries its own halfDead guard. The `hp <= 0` early-out below IS that
    // guard: halfDead implies hp == 0 in this engine, so a half-dead Awakened
    // One does not regenerate, matching the Java.
    //
    // DELIBERATELY NO DECREMENT: this is the entire distinction from
    // PowerId::REGEN (id 18) -- RegenPower.atEndOfTurn queues a RegenAction,
    // whose isPlayer-gated tail decays the stack (RegenAction.java:40-47);
    // RegenerateMonsterPower queues a bare HealAction, which has no such tail
    // at all, so this power's amount never decreases on its own.
    //
    // The heal is applied directly here -- no HEAL opcode (the Blood Potion /
    // Burning Blood / REGEN precedent). That was once ALSO the reason op_heal's
    // monster target was a documented no-op; it is not any more. S2.22 gave
    // op_heal a real monster branch (AbstractMonster.heal, interp_damage.cpp) for
    // the Healer's per-member HealActions, so routing this power through the
    // opcode would now WORK -- it is simply not what the Java does here. This
    // power's deviation is the SYNCHRONOUS write (next paragraph), which is
    // unchanged and unrelated.
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
