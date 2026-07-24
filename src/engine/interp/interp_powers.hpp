#pragma once

// POWER-domain opcode bodies: the power-slot list writers (APPLY_POWER,
// REMOVE_POWER, REDUCE_POWER) and SPOT_WEAKNESS, which queues an APPLY_POWER.
// See interp_ops.hpp for the split's rationale.

#include <cstdint>

#include "sts/engine/combat_state.hpp"  // CombatState
#include "sts/engine/types.hpp"         // PowerId

namespace sts::engine {

// APPLY_POWER (see the definition for the B3.2 Sadistic/Artifact interception).
void op_apply_power(CombatState& s, uint8_t src, uint8_t tgt, PowerId id,
                    int amount) noexcept;

// REMOVE_POWER.
void op_remove_power(CombatState& s, uint8_t tgt, PowerId id) noexcept;

// REDUCE_POWER.
void op_reduce_power(CombatState& s, uint8_t tgt, PowerId id,
                     int amount) noexcept;

// SPOT_WEAKNESS.
void op_spot_weakness(CombatState& s, uint8_t tgt, int amount) noexcept;

}  // namespace sts::engine
