#pragma once

// Regen -- native power-hook body (registry/powers.yaml id 18,
// PowerId::REGEN).
//
// Heals `amount` at end of turn -- for the player through the full
// AbstractCreature.heal seam (heal_player_with_relics: the onPlayerHeal relic
// fold and the Red Skull not-bloodied cross), for a monster a bare clamped
// heal -- and a PLAYER-owned stack then decays by 1.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_regen(CombatState& s, Hook hook,
                        const HookContext& ctx) noexcept;

}  // namespace sts::engine
