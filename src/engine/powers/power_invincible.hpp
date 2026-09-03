#pragma once

// Invincible -- native power-hook body (registry/powers.yaml id 139,
// PowerId::INVINCIBLE, `priority: 99`). The Corrupt Heart's per-turn damage cap
// (InvinciblePower.java, 55 lines, read in full).
//
// TWO OVERRIDES, AND THEY LIVE IN TWO DIFFERENT PLACES -- which is the whole of
// this header.
//
//   (a) atStartOfTurn (:95-99): `this.amount = this.maxAmt;` -- the pool REFILLS
//       at the start of the OWNER's turn, which is what makes the fight a
//       PER-TURN damage race rather than a total-damage one. That is THIS file:
//       the AT_START_OF_TURN hook, dispatched for a monster owner from
//       apply_pre_turn_logic (src/engine/action_queue.cpp), of which FlightPower
//       (id 94) is the other binder and the exact precedent.
//
//   (b) onAttackedToChangeDamage (:82-93):
//           if (damageAmount > this.amount) damageAmount = this.amount;
//           this.amount -= damageAmount;
//           if (this.amount < 0) this.amount = 0;
//           return damageAmount;
//       -- a CAP THAT ALSO DRAINS, so the pool is per-turn damage CAPACITY and
//       an over-cap hit is CLIPPED to whatever is left rather than refused.
//       That is NOT a hook: it is a bespoke integer stage of the receive
//       pipeline, and it lives at the `apply_buffer` site in
//       interp/interp_damage.cpp -- the BufferPower precedent (powers.yaml id
//       28, whose identical override has no registry hook either).
//
// THE SITE IS NOT atDamageFinalReceive. s3-design §5 trap 9 places
// onAttackedToChangeDamage BETWEEN decrementBlock and the onAttacked fan-out
// (AbstractMonster.damage:638-650; AbstractPlayer.damage:1412-1415), and the
// three atDamage* passes are a different pass entirely -- DamageInfo.applyPowers,
// which runs before the hit is even delivered. Putting the cap there would order
// it wrongly against block, Buffer, Torii and Tungsten Rod.
//
// THE ORDER AGAINST BUFFER IS UNOBSERVABLE, and that is a finding rather than an
// omission: the Java walks ONE loop over the victim's powers, so a creature
// holding both would apply them in slot order, but no creature can. Buffer is
// applied only by Fossilized Helix (relics.yaml FOSSILIZED_HELIX) and is
// therefore player-only; Invincible is applied only by
// CorruptHeart.usePreBattleAction (:101) and is therefore Heart-only. The
// interp_damage.cpp site runs Buffer then Invincible, and no state can reach it
// with both.
//
// maxAmt HAS NO POD HOME OF ITS OWN and rides PowerSlot.counter -- the slot's
// declared second number, the Flight / Panache / Malleable / Constricted
// precedent. It is written through the APPLY_POWER COUNTER OPERAND at the one
// call site that knows the value (monster_corrupt_heart.cpp), not by a
// PowerId-keyed special case in op_apply_power's new-slot path, because unlike
// Flight's `storedAmount` the number is available to the caller. It is also the
// SECOND member of the translator's five-way untagged power `misc` union
// (PROTOCOL §3.14) -- the union member S3.21's `power.misc_field` exists for and
// that no capture has yet witnessed; S3.62's Heart capture is the witness.
//
// `priority = 99` (:79) is in the registry row, not here: op_apply_power's
// sort_powers_like_the_game reads it, and slot order IS the walk order for every
// fan-out.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_invincible(CombatState& state, Hook hook,
                             const HookContext& ctx) noexcept;

}  // namespace sts::engine
