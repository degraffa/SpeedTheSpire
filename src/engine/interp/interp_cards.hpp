#pragma once

// CARD-MANIPULATION-domain opcode bodies: everything that creates, moves,
// re-costs or exhausts card instances (MAKE_CARD, CHOOSE_CARD, PLAY_TOP_DRAW,
// SET_COST, EXHAUST_NON_ATTACKS, RANDOM_ATTACK_TO_HAND) together with the pile
// helpers they share. The public CHOOSE_CARD queries (choice_slot_eligible /
// choice_requires_user / apply_choice_selection, declared in
// sts/engine/interp.hpp) are defined in this TU too: they read the same
// eligibility rules and call the same slot-application helper. See
// interp_ops.hpp for the split's rationale.

#include <cstdint>

#include "sts/engine/combat_state.hpp"  // CombatState, ActionQueueItem
#include "sts/engine/interp.hpp"        // CardPile, ChoiceKind

namespace sts::engine {

// MAKE_CARD.
void op_make_card(CombatState& s, uint16_t card_id_raw, CardPile pile,
                  int count, bool upgraded) noexcept;

// CHOOSE_CARD (auto path only -- see the definition).
void op_choose_card(CombatState& s, const ActionQueueItem& item) noexcept;

// PLAY_TOP_DRAW (Havoc).
void op_play_top_draw(CombatState& s, int exclude) noexcept;

// SET_COST.
void op_set_cost(CombatState& s, uint8_t pool_index, int new_cost) noexcept;

// EXHAUST_NON_ATTACKS (Sever Soul).
void op_exhaust_non_attacks(CombatState& s) noexcept;

// RANDOM_ATTACK_TO_HAND (Infernal Blade).
void op_random_attack_to_hand(CombatState& s) noexcept;

}  // namespace sts::engine
