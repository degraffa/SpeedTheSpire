#pragma once

// The shared atEndOfRound body of the three DURATION debuffs -- Vulnerable
// (id 2), Weak (id 3) and Frail (id 21). Internal to src/engine; not public API.
//
// WHY ONE BODY. VulnerablePower.atEndOfRound (VulnerablePower.java:44-53),
// WeakPower.atEndOfRound (WeakPower.java:44-53) and FrailPower.atEndOfRound
// (FrailPower.java:40-49) are the same six lines in the Java, differing only in
// POWER_ID:
//
//     if (this.justApplied) { this.justApplied = false; return; }
//     if (this.amount == 0) addToBot(new RemoveSpecificPowerAction(owner, owner, POWER_ID));
//     else                  addToBot(new ReducePowerAction(owner, owner, POWER_ID, 1));
//
// Writing it once is what makes the three UNIFORM. They diverge only in when the
// ctor sets justApplied, and that half lives at the application site
// (op_apply_power, interp/interp_powers.cpp), not here.
//
// WHERE THE LATCH LIVES. `PowerSlot.counter` (types.hpp), per instance, so it
// works for a monster-owned instance as well as the player's. Frail used to keep
// its latch in a single CombatState.flags bit, which by construction could only
// ever describe the player's copy; Vulnerable and Weak can sit on the player and
// all five monsters at once, so that shape does not generalize.
//
// The latch is ALWAYS 0 at a WAITING_ON_USER boundary: it is set during the
// monster phase and consumed by dispatch_at_end_of_round in the same pump, before
// control returns. So it never appears in a snapshot, a state hash, a committed
// fixture or an oracle diff -- which is also why the translator can go on
// deferring the oracle's `just_applied` field.

#include <cstdint>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext
#include "sts/engine/types.hpp"        // PowerId

namespace sts::engine {

// The AT_END_OF_ROUND body for one duration debuff owned by `ctx.owner`.
// Answers only that hook; every other hook returns immediately.
void duration_debuff_at_end_of_round(CombatState& s, Hook hook,
                                     const HookContext& ctx,
                                     PowerId id) noexcept;

// Does a NEW slot of `id` on `tgt` start latched? The three ctors' justApplied
// conditions, read out of the Java and mapped onto what the engine can see at the
// application site. Called from op_apply_power (the new-slot path only -- stacking
// preserves the existing instance's latch, as ApplyPowerAction does by handing the
// amount to the live object and discarding the freshly built one).
[[nodiscard]] bool duration_debuff_starts_just_applied(const CombatState& s,
                                                       uint8_t tgt,
                                                       PowerId id,
                                                       bool is_source_monster)
    noexcept;

}  // namespace sts::engine
