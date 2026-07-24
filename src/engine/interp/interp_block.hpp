#pragma once

// BLOCK-domain opcode bodies: the modifyBlock gain path plus the two derived
// block verbs (DOUBLE_BLOCK, BLOCK_PER_NON_ATTACK), both of which resolve
// through op_block and so stay in its translation unit. See interp_ops.hpp for
// the split's rationale.

#include <cstdint>

#include "sts/engine/combat_state.hpp"  // CombatState

namespace sts::engine {

// BLOCK (see the definition for the CARD-vs-direct kBlockNoPowers distinction).
void op_block(CombatState& s, uint8_t tgt, int amount, uint32_t flags) noexcept;

// DOUBLE_BLOCK (Entrench).
void op_double_block(CombatState& s, uint8_t tgt) noexcept;

// BLOCK_PER_NON_ATTACK (Second Wind).
void op_block_per_non_attack(CombatState& s, int block_per_card) noexcept;

}  // namespace sts::engine
