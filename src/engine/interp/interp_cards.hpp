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

// ApplyPowerAction's Corruption-specific constructor side effect: permanently
// reduce every SKILL in hand, draw, discard, and exhaust to zero cost for this
// combat before the power action itself resolves.
void apply_corruption_cost_modifier(CombatState& s) noexcept;

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

// DARK_SHACKLES / ENLIGHTENMENT / RANDOM_COLORLESS_TO_HAND.
void op_dark_shackles(CombatState& s, uint8_t target, int amount) noexcept;
void op_enlightenment(CombatState& s, bool for_rest_of_combat) noexcept;
// RANDOM_COLORLESS_TO_HAND -- `count` independent colorless-combat-pool draws
// into the hand. `flags` is the kColorlessToHand* set (interp.hpp): 0 for Jack
// of All Trades, cost-zero-for-turn [| upgraded-copy] for Transmutation's
// per-X-repetition body.
void op_random_colorless_to_hand(CombatState& s, int count,
                                 uint32_t flags) noexcept;

// RANDOM_CARD_TO_DRAW (Chrysalis / Metamorphosis) -- `count` picks over the RED
// combat pool filtered to CardType `type` (the raw CardType byte, interp.hpp
// random_card_to_draw_type_from_flags), ALL rolled before ANY of the `count`
// random-spot draw-pile insertions. Generated base copies with a registry cost
// > 0 are zeroed permanently for the combat.
void op_random_card_to_draw(CombatState& s, int count, uint8_t type) noexcept;

// UPGRADE_ALL (Apotheosis) -- upgrade every eligible card in hand, drawPile,
// discardPile, exhaustPile, in that order.
void op_upgrade_all(CombatState& s) noexcept;

// UPGRADE_RANDOM_CARD (Warped Tongs / UpgradeRandomCardAction) -- upgrade ONE
// random upgradeable hand card, in place, for the combat. Spends exactly ONE
// shuffle_rng.random_long() and ONLY when the eligible subset is non-empty.
void op_upgrade_random_card(CombatState& s) noexcept;

// DRAW_PILE_FETCH (Violence) -- pull up to `amount` cards of CardType `type`
// (the raw CardType byte, interp.hpp draw_pile_fetch_type_from_flags) out of the
// draw pile into the hand. Dual-stream: k-1 card_random_rng draws for the temp
// browse group, then ONE shuffle_rng draw per non-empty pick.
void op_draw_pile_fetch(CombatState& s, int amount, uint8_t type) noexcept;

// The shared per-card cost roll: `if (card.cost >= 0) { newCost =
// cardRandomRng.random(3); if (card.cost != newCost) card.costForTurn =
// card.cost = newCost; }`. ONE card_random_rng draw for any instance whose base
// cost is non-negative -- even when the roll changes nothing -- and none at all
// for an XCOST/UNPLAYABLE one. The comparison is against the BASE cost
// (instance_base_cost), so a live this-turn modification survives an equal roll
// untouched, and the write is permanent (COST_MODIFIED_FOR_TURN is cleared, not
// set).
//
// `clear_free_to_play_once` is the ONLY difference between the two Java bodies
// that reach here: ConfusionPower.onCardDraw (ConfusionPower.java:38-48) ends
// its `cost >= 0` branch with `card.freeToPlayOnce = false`, OUTSIDE the
// equality `if`; RandomizeHandCostAction (RandomizeHandCostAction.java:26-38)
// never touches the field. One body with one boolean is deliberate: the two were
// separately hand-written once and diverged in precisely this area.
void randomize_card_cost(CombatState& s, CardPoolIndex pi,
                         bool clear_free_to_play_once) noexcept;

// RANDOMIZE_HAND_COST (Snecko Oil / RandomizeHandCostAction) -- randomize_card_cost
// over every hand slot in hand order, freeToPlayOnce untouched. No operand.
void op_randomize_hand_cost(CombatState& s) noexcept;

// APPLY_STASIS (the Bronze Orb's card theft, ApplyStasisAction.update:32-80):
// steal one card from the draw pile (or the discard when the draw is empty)
// through the RARE -> UNCOMMON -> COMMON -> unfiltered card_random_rng cascade,
// park it in the limbo pile, and add_to_top the APPLY_POWER that gives `owner`
// the Stasis power holding it (counter = pool index + 1). Both piles empty ==
// zero draws, no power. See interp.hpp Opcode::APPLY_STASIS.
void op_apply_stasis(CombatState& s, uint8_t owner) noexcept;

// STASIS_RETURN (StasisPower.onDeath:38-44): move the stolen card (pool index
// in item.amount) OUT of limbo into the queue-time destination in item.flags
// (kStasisReturnToHandBit), spilling HAND -> DISCARD at resolve when the hand
// has filled in between (MakeTempCardInHandAction.update:71-77). A card no
// longer in limbo is a defensive no-op.
void op_stasis_return(CombatState& s, const ActionQueueItem& item) noexcept;

}  // namespace sts::engine
