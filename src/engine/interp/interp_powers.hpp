#pragma once

// POWER-domain opcode bodies: the power-slot list writers (APPLY_POWER,
// REMOVE_POWER, REDUCE_POWER) and SPOT_WEAKNESS, which queues an APPLY_POWER.
// See interp_ops.hpp for the split's rationale.

#include <cstdint>

#include "sts/engine/combat_state.hpp"  // CombatState
#include "sts/engine/types.hpp"         // PowerId

namespace sts::engine {

// APPLY_POWER (see the definition for the Sadistic/Artifact interception).
// `counter` is the applied instance's SECOND number (interp.hpp
// kApplyPowerCounterShift); 0 for every power that declares no meaning for it,
// which is every power but The Bomb.
void op_apply_power(CombatState& s, uint8_t src, uint8_t tgt, PowerId id,
                    int amount, int counter = 0) noexcept;

// REMOVE_POWER. `flags` is the queued item's whole flags word, so an INSTANCED
// power can name one specific slot (interp.hpp make_power_instance_flags); a
// key-free 0 keeps the historical first-match-by-id behaviour.
void op_remove_power(CombatState& s, uint8_t tgt, PowerId id,
                     uint32_t flags = 0) noexcept;

// REDUCE_POWER. Same instance-key convention as op_remove_power.
void op_reduce_power(CombatState& s, uint8_t tgt, PowerId id, int amount,
                     uint32_t flags = 0) noexcept;

// SPOT_WEAKNESS.
void op_spot_weakness(CombatState& s, uint8_t tgt, int amount) noexcept;

// DOUBLE_STRENGTH (Limit Break).
void op_double_strength(CombatState& s) noexcept;

}  // namespace sts::engine
