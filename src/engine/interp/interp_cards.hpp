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

// MAKE_CARD. `self_copy` + `source_pi` (meaningful only when `self_copy` is
// true) implement AbstractCard.makeStatEquivalentCopy's cost/misc/
// freeToPlayOnce carry-over for a genuine self-copy (Anger cloning itself,
// registry `self_copy: true`, S3.53) -- see the definition.
void op_make_card(CombatState& s, uint16_t card_id_raw, CardPile pile,
                  int count, bool upgraded, bool self_copy,
                  CardPoolIndex source_pi) noexcept;

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

// --- the three cost primitives, shared with the sites outside this TU --------

// AbstractCard.cost for pool row `pi` -- the combat-PERMANENT cost, as distinct
// from costForTurn (which IS cost_now). Confusion's `card.cost = newCost`
// (ConfusionPower.java:43) and every modifyCostForCombat writer move it off the
// registry row; a COST_MODIFIED_FOR_TURN instance reports the value the
// end-of-turn sweep will restore (the SAVED_BASE_COST payload when one was
// saved, else the registry cost).
//
// EVERY Java `c.cost > 0` / `c.cost >= 0` / `costForTurn != cost` test is THIS,
// never card_cost(registry). Two landed defects came from reading the registry
// row instead, both witnessed live: MummifiedHand's candidate filter
// (MummifiedHand.java:44) and CorruptionPower.onCardDraw's
// isCostModifiedForTurn decision (via setCostForTurn, AbstractCard.java:2008).
[[nodiscard]] int instance_base_cost(const CombatState& s,
                                     CardPoolIndex pi) noexcept;

// AbstractCard.setCostForTurn (AbstractCard.java:2001-2011) on pool row `pi`:
// clamp `amount` at 0, write cost_now, and set COST_MODIFIED_FOR_TURN -- saving
// the combat-permanent cost in the SAVED_BASE_COST payload when it differs from
// the registry row -- only when the new value differs from instance_base_cost.
void set_cost_for_turn(CombatState& s, CardPoolIndex pi, int amount) noexcept;

// AbstractPlayer.onCardDrawOrDiscard's Corruption branch (AbstractPlayer.java:
// 1348-1352): every SKILL in hand whose costForTurn != 0 takes
// modifyCostForCombat(-9), i.e. cost AND costForTurn both to 0, PERMANENTLY for
// the combat. A no-op without Corruption, and idempotent (it only touches a
// cost_now > 0 SKILL).
//
// `visible_hand` bounds the sweep to the first N hand slots. It is not a Java
// concept: it exists for the DRAW opcode, whose batch draw runs ahead of the
// game's one-card-at-a-time draw()/onCardDrawOrDiscard interleave. Leave it at
// its default at every other site. See the definition for the full derivation
// and for the enumeration of onCardDrawOrDiscard's callers.
void corruption_hand_cost_sweep(CombatState& s,
                                uint8_t visible_hand = 0xFFu) noexcept;

// ApplyPowerAction's Corruption-specific constructor side effect: permanently
// reduce every SKILL in hand, draw, discard, and exhaust to zero cost for this
// combat before the power action itself resolves.
void apply_corruption_cost_modifier(CombatState& s) noexcept;

// EXHAUST_NON_ATTACKS (Sever Soul).
void op_exhaust_non_attacks(CombatState& s) noexcept;

// RANDOM_ATTACK_TO_HAND (Infernal Blade, and -- via the S2.34 pool selector in
// `flags`, interp.hpp kRandomToHandPool* -- Enchiridion's POWER draw).
void op_random_attack_to_hand(CombatState& s, uint32_t flags) noexcept;

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
