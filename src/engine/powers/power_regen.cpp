// Regen -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_regen.hpp for what this power does.

#include "power_regen.hpp"

#include <cstdint>
#include "../interp/interp_powers.hpp"  // op_remove_power (the removal choke point)
#include "power_native.hpp"             // find_power
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/relic_hooks.hpp"   // heal_player_with_relics
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_regen(CombatState& s, Hook hook,
                        const HookContext& ctx) noexcept {
    // RegenPower.atEndOfTurn -> RegenAction(owner, amount)
    // (RegenPower.java:35-38, RegenAction.java:34-47): if currentHealth > 0,
    // `target.heal(amount, true)`; then, for a PLAYER owner, decrement the
    // stack by 1 (remove at 0). The heal is applied synchronously -- no HEAL
    // opcode (the Blood Potion / Burning Blood precedent) -- but it is the
    // FULL AbstractCreature.heal (AbstractCreature.java:386-417), not a bare
    // HP write, and for the player that method carries two things a raw
    // `+=` skips:
    //
    //  * the onPlayerHeal relic fold (:393-395) -- Magic Flower's x1.5, Mark
    //    of the Bloom's 0;
    //  * the NOT-BLOODIED cross (:404-408): a heal that lifts the player above
    //    maxHealth / 2.0f fires every relic's onNotBloodied, which is Red
    //    Skull's -3 Strength (RedSkull.java:55-63).
    //
    // heal_player_with_relics IS that method (relic_hooks.cpp); every other
    // in-combat player heal already routes through it. This one wrote the HP
    // directly, so a Red Skull player whose Regeneration carried them back
    // over half kept the +3 the game had taken away. The witness is capture
    // s2v3_wave2_STS205404_ps296 (run_STS205404_a20_ironclad.jsonl), floor 41
    // "3 Darklings", seq 710 -> 711: 37/75 HP + Regeneration 4 = 41 > 37.5,
    // the game's Strength 4 -> 1, the sim's stayed 4. Every player attack
    // from turn 3 on then hit for +3 (Carnage 21 vs 24, Reckless Charge 8 vs
    // 11, Sever Soul 17 vs 20), Darkling#1 half-died a turn early, reincarnated
    // a turn early, and the sim's turn-6 legal targets were the game's
    // half-dead records -- the follower stop at script step 718.
    //
    // WHICH heal() -- the overload matters, and it is why the monster arm below
    // is NOT AbstractMonster's body. RegenAction calls the TWO-ARG
    // `heal(amount, true)` (:38), and neither AbstractPlayer nor AbstractMonster
    // overrides that overload: both override only `heal(int)`
    // (AbstractPlayer.java:1545-1553, AbstractMonster.java:384-399). So a
    // Regeneration tick lands in AbstractCreature.heal(int, boolean) for EITHER
    // owner. For a monster that body's extra machinery is provably inert -- the
    // relic fold is `if (!this.isPlayer) continue` (:393-395), and the
    // not-bloodied cross reads `this.isBloodied`, a field the whole game sets
    // only on the player (AbstractPlayer.java:1477,1575) -- so the monster arm
    // reduces to the isDying early-out, the +=, and the clamp, which is
    // AbstractMonster.heal's body as well. No landed content grants REGEN to a
    // monster (RegenerateMonsterPower is its own power), but the arm is written
    // to the Java rather than left as a hole.
    //
    // WHICH DOOR, since the HEAL opcode has both bodies too (op_heal,
    // interp/interp_damage.cpp). The player arm shares op_heal's door exactly:
    // op_heal's player branch IS the single call `heal_player_with_relics(s,
    // amount)`, so the onPlayerHeal fold and the not-bloodied cross have one
    // implementation between them and this power is a caller of it, not a
    // second copy. The monster arm is deliberately not routed through op_heal:
    // that branch models AbstractMonster.heal(int) -- a different Java method
    // from the one RegenAction reaches -- and it carries a halfDead-clearing
    // invariant maintainer for the Darkling/Awakened One revival heals that
    // RegenAction's own `currentHealth > 0` guard makes unreachable here. The
    // sibling power_regenerate_monster.cpp keeps its clamp inline for the same
    // reason.
    if (hook != Hook::AT_END_OF_TURN) {
        return;
    }
    if (ctx.owner == kActorPlayer) {
        if (s.player_hp > 0) {  // RegenAction.java:38 `currentHealth > 0`
            heal_player_with_relics(s, ctx.power_amount);
        }
        // RegenAction's decrement is isPlayer-gated (:41-47):
        //
        //     if (this.target.isPlayer && (p = getPower("Regeneration")) != null) {
        //         --p.amount;
        //         if (p.amount == 0) this.target.powers.remove(p);
        //         else p.updateDescription();
        //     }
        //
        // SYNCHRONOUS, inside RegenAction.update -- not a queued
        // RemoveSpecificPowerAction, which is why this stays a direct write
        // rather than following the Plated Armor / Draw Reduction precedent of
        // queueing a REDUCE_POWER.
        //
        // THE REMOVAL GOES THROUGH THE CHOKE POINT. This used to zero
        // `power_id` in place, which leaves a NONE hole INSIDE
        // player_power_count -- the list is a packed array whose count is its
        // length, so a hole is not "an empty slot", it is a corrupt list that
        // every consumer walks: PublicView, public_hash, the hidden twin, and
        // the `--vitals` compare that caught it. op_remove_power is
        // find-then-remove_slot_at (interp/interp_powers.cpp), and
        // remove_slot_at is the one place that compacts the tail down and zeroes
        // the vacated row. Same bug class, same fix as power_plated_armor.cpp.
        //
        // The one delta from the Java, stated because it is real: `powers.remove(p)`
        // is a bare ArrayList removal and fires NO onRemove, while remove_slot_at
        // dispatches Hook::ON_POWER_REMOVED. It is inert here -- REGEN's registry
        // row binds only at_end_of_turn (powers.yaml id 18) and this native body
        // early-returns on every other hook -- so the compaction is all that
        // happens. A power that both self-decays like this AND bound an onRemove
        // would need the Awakened One's hand-compaction instead
        // (monster_awakened_one.cpp:81-83), which exists for exactly that reason.
        PowerSlot* rp = find_power(s, ctx.owner, PowerId::REGEN);
        if (rp != nullptr) {
            rp->amount = static_cast<int16_t>(rp->amount - 1);
            if (rp->amount <= 0) {  // `== 0` in the Java; a stack never goes under
                op_remove_power(s, ctx.owner, PowerId::REGEN);
            }
        }
        return;
    }
    if (ctx.owner < kMonsterCap) {
        MonsterState& m = s.monsters[ctx.owner];
        // `currentHealth > 0` (RegenAction:38) and AbstractCreature.heal's
        // isDying early-out (:390-392) collapse here: a 0-HP record is dying or
        // half-dead, and neither takes a Regeneration tick in the game (a
        // half-dead Darkling has had its powers cleared, so no REGEN survives
        // on it to fire). The stricter of the two is RegenAction's, which is
        // the one written.
        if (m.hp > 0) {
            int32_t v = static_cast<int32_t>(m.hp) + ctx.power_amount;
            if (v > m.max_hp) {
                v = m.max_hp;
            }
            m.hp = static_cast<int16_t>(v);
        }
    }
}

}  // namespace sts::engine
