#pragma once

// Hex -- native power-hook body (registry/powers.yaml id 93, PowerId::HEX).
//
// The Chosen's signature debuff, applied to the PLAYER (Chosen.java:134). Every
// NON-ATTACK card the player uses shuffles `amount` Dazed into the draw pile at
// random positions.
//
// Native ONLY for the card-type condition -- the Rage precedent (power_rage.cpp).
// The effect it queues is an ordinary MAKE_CARD item into CardPile::DRAW_RANDOM,
// which op_make_card already implements; no opcode was added for it.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_hex(CombatState& s, Hook hook,
                      const HookContext& ctx) noexcept;

}  // namespace sts::engine
