#pragma once

// DAMAGE-domain opcode bodies: the DamageInfo float pipeline (compute_damage,
// declared publicly in sts/engine/interp.hpp), the DAMAGE / LOSE_HP landing
// paths, and DROPKICK (a DAMAGE plus a conditional follow-up, so it lives with
// the op_damage it calls rather than with the card helpers). See interp_ops.hpp
// for the split's rationale.

#include <cstdint>

#include "sts/engine/combat_state.hpp"  // CombatState, ActionQueueItem
#include "sts/engine/interp.hpp"        // DamageType

namespace sts::engine {

// DAMAGE (see the definition for the full DamageInfo provenance).
void op_damage(CombatState& s, uint8_t src, uint8_t tgt, int base,
               int strength_mult = 1,
               DamageType type = DamageType::NORMAL) noexcept;

// LOSE_HP / LOSE_HP_PER_HAND.
void op_lose_hp(CombatState& s, uint8_t tgt, int amount) noexcept;

// DROPKICK.
void op_dropkick(CombatState& s, const ActionQueueItem& item) noexcept;

// DAMAGE_FEED (Feed): damage, then the max-HP gain when the hit left tgt dead.
void op_damage_feed(CombatState& s, uint8_t src, uint8_t tgt, int base,
                    int max_hp_gain) noexcept;

// VAMPIRE_DAMAGE_ALL (Reaper): hit every live monster, then queue the summed heal.
void op_vampire_damage_all(CombatState& s, int base) noexcept;

// HEAL: the queued HealAction, routed through the onPlayerHeal relic seam.
void op_heal(CombatState& s, uint8_t tgt, int amount) noexcept;

}  // namespace sts::engine
