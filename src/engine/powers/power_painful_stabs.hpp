#pragma once

// Painful Stabs -- native power-hook body (registry/powers.yaml id 97,
// PowerId::PAINFUL_STABS).
//
// The Book of Stabbing's opener (BookOfStabbing.java:78-81), self-applied at
// amount -1. Every non-THORNS hit it lands on the player puts ONE Wound in the
// discard pile -- per HIT, so a stabCount of 5 makes five Wounds.
//
// The FIRST and only binder of Hook::ON_INFLICT_DAMAGE (17), which S2.2F landed
// with its dispatch site and no producer. That hook is the ATTACKER's power
// list: AbstractPlayer.damage:1449-1453 walks `info.owner.powers`, not the
// victim's, which is why this cannot be folded into WAS_HP_LOST.
//
// Native ONLY for the DamageType guard -- the Rage/Hex precedent. The effect it
// queues is an ordinary MAKE_CARD item into CardPile::DISCARD, which
// op_make_card already implements; no opcode was added for it.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_painful_stabs(CombatState& s, Hook hook,
                                const HookContext& ctx) noexcept;

}  // namespace sts::engine
