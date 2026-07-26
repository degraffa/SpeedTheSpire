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

// PLAY_TOP_DRAW (Havoc). The played Havoc needs no exclusion parameter: it sits
// in the limbo pile throughout (see op_use_card).
void op_play_top_draw(CombatState& s) noexcept;

// USE_CARD -- UseCardAction.update as a queued action: Strange Spoon roll, then
// file the played card (item.amount = pool index) out of the limbo pile into
// discard / exhaust / nothing (purge, POWER). See the definition for the full
// UseCardAction.java mapping.
void op_use_card(CombatState& s, const ActionQueueItem& item) noexcept;

// SET_COST.
void op_set_cost(CombatState& s, uint8_t pool_index, int new_cost) noexcept;

// EXHAUST_NON_ATTACKS (Sever Soul).
void op_exhaust_non_attacks(CombatState& s) noexcept;

// RANDOM_ATTACK_TO_HAND (Infernal Blade).
void op_random_attack_to_hand(CombatState& s) noexcept;

// PLAY_CARD -- the general recursive-play verb (interp.hpp Opcode::PLAY_CARD and
// its kPlayCard* flag set). Shared by the queued opcode and by the SYNCHRONOUS
// call site a replay power needs: DoubleTapPower.onUseCard enqueues its copy
// inside the UseCardAction constructor (DoubleTapPower.java:60 ->
// AbstractPlayer.useCard:1370), not through a queued action, so its native body
// calls this directly rather than queuing an item.
void op_play_card(CombatState& s, uint8_t target, int source_index,
                  uint32_t flags) noexcept;

// FIEND_FIRE -- exhaust the hand one random card at a time, then hit `base` times.
void op_fiend_fire(CombatState& s, uint8_t target, int base) noexcept;

// CONDITIONAL_DRAW (Impatience) -- draw `amount` only if NO hand card has
// CardType `restricted_type` (the raw CardType value, interp.hpp
// conditional_draw_type_from_flags).
void op_conditional_draw(CombatState& s, int amount,
                         uint8_t restricted_type) noexcept;

// MADNESS -- rejection-sample the hand with card_random_rng and permanently zero
// the chosen card's cost.
void op_madness(CombatState& s) noexcept;

}  // namespace sts::engine
