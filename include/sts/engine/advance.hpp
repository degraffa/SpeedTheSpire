#pragma once

// The batch API -- advance(), legal_actions(), and combat construction
// (combat_begin). advance() is the only public API of the whole simulator
// (design doc §7), and every subsystem (RNG streams, the action-queue pump, the
// effect interpreter, the five cards, the Jaw Worm AI, the observation encoder)
// is exercised through it. This header's shapes are what every future phase
// (Stage B onward) builds on, so they are chosen with care and documented where
// the design doc left them implicit.
//
// SCOPE (design doc §9): the M1 walking skeleton is Ironclad vs. one Jaw Worm,
// the five skeleton cards. advance() therefore hard-wires the monster-turn seam
// to jaw_worm_take_turn -- Stage B generalizes the MonsterTurnFn per the monster
// registry. Only PLAY_CARD and END_TURN are implemented; USE_POTION and CHOOSE
// are documented no-ops (no potions or choice prompts exist in M1).
//
// Provenance (combat construction): AbstractPlayer.preBattlePrep
// (AbstractPlayer.java:1564-1595) -> drawPile.initializeDeck(masterDeck)
// (AbstractPlayer.java:1584) -> CardGroup.initializeDeck (CardGroup.java:928-955)
// -> copy.shuffle(shuffleRng) -> Collections.shuffle(group,
// new java.util.Random(shuffleRng.randomLong())) (CardGroup.java:565-567). The
// combat-start deck shuffle is the SAME one-randomLong()->JDK-LCG->Fisher-Yates
// mechanism as the in-combat reshuffle (piles.cpp shuffle_discard_into_draw);
// see combat_begin's implementation note in advance.cpp.

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/observation.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// --- combat_begin -----------------------------------------------------------

// Construct a fresh CombatState for the given run seed, floor, and deck
// (design doc §7's "combat-start construction of CombatState from (seed, floor,
// deck)"). The returned state is left at the start of the player's first turn:
// phase == WAITING_ON_USER, turn == 1, energy refilled, the opening hand drawn.
// This is achieved by priming the pump's turn-1 invariants and then draining one
// pump() call through the start-of-turn sequence -- combat_begin does NOT
// hand-roll turn-1 setup separately from the machinery pump() uses for every
// later turn (see the implementation note in advance.cpp).
//
// `deck` is any span of CardId (the function is general over deck size, not
// hard-coded to the skeleton's 12-card deck); each entry becomes one row of the
// card pool with cost_now = the registry base cost (cards.hpp). The deck is
// shuffled into the draw pile via one shuffle_rng draw + JDK Fisher-Yates.
//
// PLACEHOLDER STATS (design doc §11: the exact Ironclad A20 starting HP is
// "deferred to Stage B ... numbers not restated here without a source read").
// combat_begin uses hp == max_hp == 80, matching the convention the rest of the
// test suite already uses (cards_test.cpp / jaw_worm_test.cpp). Exact real-game
// provenance for the starting-HP number is deliberately deferred; it plugs in
// here (and in a future RunState->CombatState derivation) without an API change.
[[nodiscard]] CombatState combat_begin(int64_t run_seed, int32_t floor,
                                       std::span<const CardId> deck) noexcept;

// --- ActionMask -------------------------------------------------------------

// Which actions are legal in the current state (design doc §7:
// legal_actions(const CombatState&, ActionMask&)). The design doc names the
// call but leaves the type's shape to the implementation; this is that shape.
//
// The skeleton has exactly one monster, so target enumeration is trivial (an
// enemy-targeted card always targets monster slot 0; a self-targeted card
// ignores its target). We therefore expose per-hand-slot playability plus a
// single END_TURN flag rather than a full (hand_slot x target) cross-product.
//
// NOTE (Stage B): once combats can hold more than one monster, an Attack's
// legality becomes per-target and this mask must grow real target enumeration
// (a can_play[hand_slot][target] grid, or a packed legal-action list). The flat
// per-slot form here is honest about the skeleton's single-monster scope.
//
// Fixed-size, trivially copyable, no allocation.
struct ActionMask {
    // can_play[i] is true iff hand slot i holds a card the player can legally
    // play right now: phase == WAITING_ON_USER AND i < hand_count AND
    // player_energy >= card_pool[hand[i]].cost_now. Slots >= hand_count and all
    // slots when not WAITING_ON_USER are false.
    bool can_play[kHandCap];
    // END_TURN is always legal while WAITING_ON_USER (the player may always
    // choose to end the turn), and illegal otherwise.
    bool can_end_turn;

    // --- CHOOSE-in-combat (Stage B B3.4) ---
    // When a CHOOSE_CARD is open at the head of the action queue and needs a real
    // selection (choice_requires_user), the player is choosing a hand card, NOT
    // playing/ending: `choice_pending` is true, `can_play`/`can_end_turn` are all
    // false, and `can_choose[i]` is true for each hand slot that is a legal
    // selection (the eligible cards on the hand-select screen). The CHOOSE action
    // arg0 is the chosen hand slot. When no choice is pending, `choice_pending` is
    // false and `can_choose` is all false.
    bool choice_pending;
    bool can_choose[kHandCap];

    // --- Discard-source CHOOSE (Stage B B3.3: Headbutt) ---
    // A DISCARD_TO_DRAW_TOP choice selects from the DISCARD pile, not the hand.
    // When `choice_from_discard` is true, the CHOOSE action arg0 is a DISCARD slot
    // (every discard card is eligible -- DiscardPileToTopOfDeckAction has no
    // filter), and `can_choose[i]` reflects the first min(discard_count, kHandCap)
    // discard slots (a convenience for the common small-discard case; discard slots
    // >= kHandCap are still legal and validated by advance against discard_count).
    // `choice_from_discard` is false for hand-source choices and when idle.
    bool choice_from_discard;
};

static_assert(std::is_trivially_copyable_v<ActionMask>,
              "ActionMask must be trivially copyable (POD, no allocation)");

// Fill `out` with the current legal actions (affordability + phase gate above).
void legal_actions(const CombatState& state, ActionMask& out) noexcept;

// --- StepResult -------------------------------------------------------------

// The per-state result of one advance() step (design doc §7: "terminal flag,
// reward fields, and an observation view -- no allocation anywhere in the
// loop"). The engine's whole philosophy is POD / no pointers / no aliasing
// between distinct CombatStates, so the zero-allocation-compliant reading of
// "observation view" is to EMBED the ObsBuffer by value: encode_observation is
// the one intentional, controlled projection of the state (not a second raw
// state copy), and an embedded flat ObsBuffer holds no pointer back into the
// state. StepResult is thus a self-contained POD.
//
// REWARD (placeholder, NOT a frozen design). The real reward shaping is
// training-loop scope, wildly out of scope for M1. This is a minimal, honest,
// non-crashing scheme for the batch smoke test:
//   +1.0f  win  -- the sole monster's hp reached 0 (and the player is alive)
//   -1.0f  loss -- player_hp reached 0
//    0.0f  otherwise (combat ongoing)
// terminal == (all monsters dead) || (player dead).
struct StepResult {
    bool terminal;
    float reward;
    ObsBuffer obs;
};

static_assert(std::is_trivially_copyable_v<StepResult>,
              "StepResult must be trivially copyable (POD, embedded ObsBuffer)");

// --- advance ----------------------------------------------------------------

// Step a heterogeneous batch of combats by one action each (design doc §7's
// exact signature). states/actions/results are parallel spans of equal length
// (asserted). Each index is advanced INDEPENDENTLY: there is no shared or
// lockstep assumption between batch entries -- state i can be at a completely
// different point in its own fight and receive a completely different action
// from state j. This falls out naturally of a plain per-index loop; SIMD /
// parallelism is Stage C perf hardening, not needed for M1's contract.
//
// Per index i, actions[i] is dispatched by verb:
//   PLAY_CARD  -> queue_card_play(states[i], arg0=hand_index, arg1=target),
//                 then pump(states[i], jaw_worm_take_turn)
//   END_TURN   -> add the end-turn sentinel, then pump(states[i], jaw_worm_take_turn)
//   USE_POTION / CHOOSE -> no-op (out of skeleton scope; no potions/choices in M1)
// After pumping, results[i] is filled: terminal/reward from the post-pump state
// and encode_observation(states[i], results[i].obs).
//
// NO heap allocation anywhere in the loop -- the spans are iterated directly, no
// std::vector / new. (encode_observation is itself allocation-free.)
void advance(std::span<CombatState> states, std::span<const Action> actions,
             std::span<StepResult> results) noexcept;

}  // namespace sts::engine
