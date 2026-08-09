// Constricted -- native power-hook body. One translation unit per power; see
// power_native.hpp for the dispatch plumbing and power_constricted.hpp for what
// this power does and why the SOURCE lives in PowerSlot.counter.

#include "power_constricted.hpp"

#include <cstdint>

#include "power_native.hpp"             // PowerNativeSig
#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActorPlayer
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, DamageType, make_damage_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_constricted(CombatState& s, Hook hook,
                              const HookContext& ctx) noexcept {
    if (hook != Hook::AT_END_OF_TURN) {
        return;
    }
    // ConstrictedPower.atEndOfTurn(isPlayer) (ConstrictedPower.java:48-52):
    //
    //   this.flashWithoutSound();
    //   this.playApplyPowerSfx();
    //   this.addToBot(new DamageAction(this.owner,
    //                                  new DamageInfo(this.source, this.amount,
    //                                                 DamageType.THORNS)));
    //
    // NO isPlayer GATE -- unlike Ritual and Malleable, this body runs for a
    // monster owner too. Nothing in Acts 1-3 puts Constricted on a monster (the
    // Spire Growth is its only applier and it targets the player,
    // SpireGrowth.java:82-89), but the absence of the gate is the Java's, so the
    // absence is reproduced rather than a gate invented. Both dispatch walks
    // reach here: dispatch_at_end_of_turn for the player, the monster pass of
    // dispatch_at_end_of_round for a monster.
    //
    // The two sound/flash calls are presentation and draw no seeded RNG.
    ActionQueueItem dmg{};
    dmg.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    // THE SOURCE, and the one place this power is not a two-line data program.
    // PowerSlot.counter holds the applying monster's slot index (see the header);
    // it is deliberately NOT ctx.owner, because a self-sourced tick would trip
    // dispatch_was_hp_lost's Rupture guard.
    //
    // A slot whose counter is out of range can only come from a corrupt record
    // -- op_apply_power writes it from the item the Spire Growth queued -- so the
    // fallback is chosen to preserve the property that actually matters: keep the
    // source DISTINCT from the victim rather than silently becoming self-damage.
    // Monster slot 0 is the honest choice: the Spire Growth is always solo
    // (encounters.yaml id 47 emits one "Serpent" and nothing else), so slot 0 IS
    // where it sits.
    const int32_t src = ctx.power_counter;
    dmg.src = (src >= 0 && src < static_cast<int32_t>(kMonsterCap))
                  ? static_cast<uint8_t>(src)
                  : static_cast<uint8_t>(0);
    dmg.tgt = ctx.owner;
    dmg.amount = ctx.power_amount;
    // DamageType.THORNS: no atDamageGive/atDamageReceive passes, so neither the
    // source's Strength nor the victim's Vulnerable touches the number, and the
    // victim's own ON_ATTACKED binders do not fire from it (interp_damage.cpp's
    // NORMAL-only gate) -- which is correct for every landed binder and is
    // separately handled for Shifting, the one power with no type guard of its
    // own (power_shifting.hpp).
    dmg.flags = make_damage_flags(DamageType::THORNS);
    add_to_bottom(s, dmg);  // addToBot (:51)
}

}  // namespace sts::engine
