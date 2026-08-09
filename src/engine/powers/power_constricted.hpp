#pragma once

// Constricted -- native power-hook body (registry/powers.yaml id 102,
// PowerId::CONSTRICTED).
//
// The Spire Growth's unending bleed (ConstrictedPower.java, 53 lines, read in
// full). ONE live hook and no damage-pipeline case:
//
//   * AT_END_OF_TURN (:48-52)  queue THORNS damage `amount` at the owner,
//                              SOURCED FROM THE MONSTER THAT APPLIED IT.
//
// It modifies no damage number and no block number in any pass, so none of the
// count-guarded switches in interp_damage.cpp / interp_block.cpp gains a case.
//
// THREE THINGS THAT ARE NOT LIKE THE OTHER DEBUFFS.
//
// (1) IT NEVER DECREMENTS AND NEVER REMOVES ITSELF. Weak/Frail/Vulnerable are
//     duration debuffs with an at_end_of_round countdown; this one has no
//     countdown at all. Once applied it ticks every end of turn for the rest of
//     the combat, and the only thing that ends it is the combat.
//
// (2) PRIORITY 105 (:34) IS THE HIGHEST IN THE REGISTRY -- above Weak's 99. That
//     is a registry column, not a fact this file acts on, but it is why the slot
//     always sorts last and therefore walks last in every fan-out.
//
// (3) PowerSlot.counter IS THE SOURCE MONSTER'S SLOT INDEX, and that is the
//     whole reason this power is native rather than a two-step data program.
//
//     ConstrictedPower is the only power in the registry whose ctor takes a
//     SECOND creature (:25-35, `this.source = source`), and it is not decorative:
//     the tick is
//
//         addToBot(new DamageAction(this.owner,
//                                   new DamageInfo(this.SOURCE, this.amount,
//                                                  DamageType.THORNS)));
//
//     -- the DamageInfo's owner is the SOURCE, not the victim. A data
//     at_end_of_turn program cannot say that: queue_hook_step fills the item's
//     `src` from the power's OWNER, which here is the player, and a
//     player-sourced self-hit is observably different. dispatch_was_hp_lost's
//     RUPTURE guard fires only when `source == victim` (power_hooks.hpp), so a
//     player-sourced Constricted tick would give an Ironclad holding Rupture a
//     free Strength stack every single turn. THORNS itself reads nothing off the
//     source -- it skips the atDamageGive/atDamageReceive passes -- so the index
//     is needed for its IDENTITY, not for any number it carries.
//
//     A DEAD OR STALE INDEX IS FINE AND IS WHAT THE JAVA DOES. Nothing removes
//     Constricted when the Spire Growth dies, and the Java's `source` reference
//     dangles at a dead AbstractMonster in exactly the same way; the tick keeps
//     landing, still not sourced at the player, which is all that matters.
//
//     WHY counter AND NOT A BOOLEAN. A `source != victim` bit would be
//     sufficient for Acts 1-3 (one applier, at most one per combat) and would be
//     a smaller claim than the truth. counter is the slot's declared second
//     number, costs nothing, and is honest -- the Flight / Panache / Malleable
//     precedent. The index is written on op_apply_power's NEW-SLOT path from the
//     APPLY_POWER item's counter operand, which monster_spire_growth.cpp packs.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_constricted(CombatState& state, Hook hook,
                              const HookContext& ctx) noexcept;

}  // namespace sts::engine
