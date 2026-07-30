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
// kApplyPowerCounterShift); 0 for every power that declares no meaning for it.
// `is_source_monster` reproduces DurationPower's source-sensitive justApplied
// constructor argument (normally true; Gremlin Visage is the known false path).
void op_apply_power(CombatState& s, uint8_t src, uint8_t tgt, PowerId id,
                    int amount, int counter = 0,
                    bool is_source_monster = true) noexcept;

// REMOVE_POWER. `flags` is the queued item's whole flags word, so an INSTANCED
// power can name one specific slot (interp.hpp make_power_instance_flags); a
// key-free 0 keeps the historical first-match-by-id behaviour.
void op_remove_power(CombatState& s, uint8_t tgt, PowerId id,
                     uint32_t flags = 0) noexcept;

// REDUCE_POWER. Same instance-key convention as op_remove_power.
void op_reduce_power(CombatState& s, uint8_t tgt, PowerId id, int amount,
                     uint32_t flags = 0) noexcept;

// REMOVE_DEBUFFS (Orange Pellets / RemoveDebuffsAction). Enumerate `tgt`'s power
// list AT RESOLVE TIME and queue one addToTop REMOVE_POWER per DEBUFF-typed
// slot -- see the opcode's note in interp.hpp for why the enumeration cannot
// happen at queue time and for the live-instance DEBUFF predicate.
void op_remove_debuffs(CombatState& s, uint8_t tgt) noexcept;

// SPOT_WEAKNESS.
void op_spot_weakness(CombatState& s, uint8_t tgt, int amount) noexcept;

// DOUBLE_STRENGTH (Limit Break).
void op_double_strength(CombatState& s) noexcept;

}  // namespace sts::engine
