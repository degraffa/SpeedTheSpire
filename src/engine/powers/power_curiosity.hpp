#pragma once

// Curiosity -- native power-hook body (registry/powers.yaml id 108,
// PowerId::CURIOSITY).
//
// The Awakened One's POWER-card tax (CuriosityPower.java, read in full). One
// hook:
//
//   * ON_USE_CARD (:34-39)  a POWER play grants the OWNER `amount` Strength.
//
// NATIVE FOR THE CARD-TYPE CONDITION ALONE -- the Rage (id 68) and Hex (id 93)
// precedent. The effect is an ordinary APPLY_POWER of STRENGTH at the power's own
// stack amount; an effect list simply cannot express "only when the played card
// is a POWER".
//
// THIS IS THE FIRST MONSTER-OWNED ON_USE_CARD BINDER. Every earlier binder
// (Rage, Hex, Corruption, Double Tap, Duplication, Mayhem, Panache...) is owned
// by the PLAYER; Curiosity is applied by the boss to ITSELF
// (AwakenedOne.java:146,149), so `ctx.owner` is a monster slot and the queued
// APPLY_POWER aims at that slot. No new dispatch site is needed: the
// UseCardAction CONSTRUCTOR fan-out already walks monster powers LAST
// (UseCardAction.java:41-64), which dispatch_on_use_card implements.
//
// IT DOES NOT SURVIVE THE REBIRTH. Curiosity is one of the three power ids the
// Awakened One's phase transition names explicitly (AwakenedOne.java:306), so
// however many Strength stacks phase 1 bought the player, phase 2 starts with no
// Curiosity at all. (The STRENGTH it already granted DOES survive -- Strength is
// a BUFF and is not in the purge list. See monster_awakened_one.hpp.)

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_curiosity(CombatState& state, Hook hook,
                            const HookContext& ctx) noexcept;

}  // namespace sts::engine
