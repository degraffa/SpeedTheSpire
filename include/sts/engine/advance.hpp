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
// STARTING HP (sourced; the earlier "PLACEHOLDER STATS / provenance deferred per
// design doc §11" note here is OBSOLETE). combat_begin uses hp == max_hp == 80,
// and that number now has provenance: it is the Ironclad's CharSelectInfo sheet
// (Ironclad.java:114) -- not a convention borrowed from the
// tests awaiting a source read.
//
// combat_begin is the STANDALONE entry point: it takes no RunState, so it
// applies that base sheet as a DEFAULT. The run layer does not go through the
// default -- run_advance.cpp's enter_combat mirrors combat_begin step for step
// but seeds player_hp / player_max_hp from rc.run.hp / rc.run.max_hp. Those match
// the CharSelectInfo sheet only up to ascension 5: the run-setup ascension
// modifiers (the max-HP loss and the 90 %-of-max current HP -- run_advance.hpp)
// move them, so an ascension-20 run enters its first combat at 68/75. This
// default is deliberately NOT ascension-aware, because combat_begin has no
// ascension to be aware of; a caller that wants a run's real sheet builds the
// combat through the run layer.
[[nodiscard]] CombatState combat_begin(int64_t run_seed, int32_t floor,
                                       std::span<const CardId> deck) noexcept;

// --- ActionMask -------------------------------------------------------------

// Which actions are legal in the current state (design doc §7:
// legal_actions(const CombatState&, ActionMask&)). The design doc names the
// call but leaves the type's shape to the implementation; this is that shape.
//
// can_play[i] is per-hand-slot playability (affordability + not-unplayable +
// phase gate); can_play_target[i][t] is the multi-monster (hand_slot x
// target) grid for enemy-targeted cards. Together with can_end_turn / the CHOOSE
// fields they enumerate every legal action.
//
// TARGETING: a card with `needs_target` (an enemy-target Attack) is legal
// only against a LIVE monster slot; can_play_target[i][t] is true iff hand slot i
// is playable AND card_def(hand[i]).needs_target AND t < monster_count AND
// monsters[t].hp > 0. Self / all-enemy / no-target / random-target cards ignore
// the declared target, so their can_play_target row is all-false and their
// legality is carried by can_play[i] alone. This is the real target enumeration
// the earlier single-monster mask deferred.
//
// Fixed-size, trivially copyable, no allocation.
struct ActionMask {
    // can_play[i] is true iff hand slot i holds a card the player can legally
    // play right now: phase == WAITING_ON_USER AND i < hand_count AND not
    // UNPLAYABLE AND player_energy >= card_pool[hand[i]].cost_now. Slots >=
    // hand_count and all slots when not WAITING_ON_USER are false. (A live-target
    // check for enemy-target cards lives in can_play_target below; can_play[i]
    // keeps its affordability meaning so a policy can read either view.)
    bool can_play[kHandCap];
    // can_play_target[i][t]: hand slot i is a legal play against monster slot t
    // (enemy-target cards only; see TARGETING above). All-false for non-target
    // cards, dead/absent monster slots, and unplayable/unaffordable/non-waiting
    // states. The PLAY_CARD action's arg1 is the chosen target slot t.
    bool can_play_target[kHandCap][kMonsterCap];
    // END_TURN is always legal while WAITING_ON_USER (the player may always
    // choose to end the turn), and illegal otherwise.
    bool can_end_turn;

    // --- CHOOSE-in-combat ---
    // When a CHOOSE_CARD is open at the head of the action queue and needs a real
    // selection (choice_requires_user), the player is choosing a hand card, NOT
    // playing/ending: `choice_pending` is true, `can_play`/`can_end_turn` are all
    // false, and `can_choose[i]` is true for each hand slot that is a legal
    // selection (the eligible cards on the hand-select screen). The CHOOSE action
    // arg0 is the chosen hand slot. When no choice is pending, `choice_pending` is
    // false and `can_choose` is all false.
    bool choice_pending;
    bool can_choose[kHandCap];

    // --- Discard-source CHOOSE (Headbutt) ---
    // A DISCARD_TO_DRAW_TOP choice selects from the DISCARD pile, not the hand.
    // When `choice_from_discard` is true, the CHOOSE action arg0 is a DISCARD slot
    // (every discard card is eligible -- DiscardPileToTopOfDeckAction has no
    // filter), and `can_choose[i]` reflects the first min(discard_count, kHandCap)
    // discard slots (a convenience for the common small-discard case; discard slots
    // >= kHandCap are still legal and validated by advance against discard_count).
    // `choice_from_discard` is false for hand-source choices and when idle.
    bool choice_from_discard;

    // --- Exhaust-source CHOOSE (Exhume) ---
    // An EXHAUST_TO_HAND choice selects from the EXHAUST pile. When
    // `choice_from_exhaust` is true, the CHOOSE action arg0 is an EXHAUST slot,
    // and `can_choose[i]` reflects the first min(exhaust_count, kHandCap) exhaust
    // slots on the same convenience terms as `choice_from_discard` above. The two
    // flags are mutually exclusive, and both are false for a hand-source choice
    // and when idle.
    bool choice_from_exhaust;
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
//   +1.0f  win  -- EVERY monster's hp reached 0 (and the player is alive; the
//                  single-monster wording here predates the multi-monster
//                  groups -- fill_result in advance.cpp scans all of them)
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
// EVERY action is checked against legal_actions() before it is dispatched, for
// every verb: an action the mask does not report as legal is a no-op that cannot
// mutate the state (see the gate's comment in advance.cpp for why that is a
// memory-safety property and not a tidiness rule). This overload builds that
// mask itself, once per state per step.
//
// NO heap allocation anywhere in the loop -- the spans are iterated directly, no
// std::vector / new. (encode_observation is itself allocation-free.)
void advance(std::span<CombatState> states, std::span<const Action> actions,
             std::span<StepResult> results) noexcept;

// The same step, with the legality mask supplied by the caller.
//
// WHY IT EXISTS. A search loop that picks its action by calling legal_actions()
// already holds the mask advance() is about to rebuild. Rebuilding it is the
// dominant cost of the legality gate: the gate exactly DOUBLES the mask work for
// such a caller, and that is what it costs. Handing the mask in removes the
// second build. It is a pure throughput option -- the guard is NOT relaxed, and
// the three-span overload above keeps working and keeps building its own mask.
//
// MEASURED (tools/bench_ab.sh, interleaved, 7 pairs, Release+LTO, bench_advance
// over a 10k-state batch; bench_advance_mask is the same benchmark driving this
// overload). In the all-states-live regime the guard's cost was originally
// quoted in (25 fixed iterations):
//     pre-guard advance()          4.35 M steps/s
//     guarded, mask rebuilt        3.13 M steps/s   (the cost)
//     guarded, mask supplied       4.32-4.46 M steps/s
// so the overload is +38.0% over the rebuilding form (95% band +33.2%..+42.9%)
// and back level with the pre-guard baseline, +2.4% (95% band +1.6%..+3.3%).
// The guard's throughput cost is fully recovered without touching the guard.
// Over a default 3-second run, where most states have gone terminal and a
// rejected action skips the pump entirely, the same swap is +84.3% (9.52 ->
// 17.53 M steps/s): the cheaper the step, the more the redundant mask dominates.
//
// EXACT EQUIVALENCE. Both overloads dispatch through one shared step function,
// so this is the same code with the same guard, differing only in where the mask
// came from. For any state s and action a,
//     ActionMask m; legal_actions(s, m);
//     advance({&s,1}, {&a,1}, {&r,1}, {&m,1})
// leaves s and r byte-identical to advance({&s,1}, {&a,1}, {&r,1}).
//
// THE CALLER'S CONTRACT -- masks[i] MUST equal what legal_actions(states[i], m)
// would produce for states[i] AS IT IS ON ENTRY, i.e. before this call mutates
// it. Practically: build the mask from the state you are about to step, use it
// to choose the action, pass both, and do not reuse it for a later step.
//
// WHAT HAPPENS IF IT DOES NOT. The guard believes the mask. A mask that is
// stale, belongs to another state, or is default-constructed is not detected in
// a Release build, and the consequences are asymmetric:
//   * A mask that is too PERMISSIVE (reports an action the state does not
//     actually allow) admits an illegal action into the pump. That is precisely
//     the failure the guard was added to prevent -- e.g. an END_TURN on a
//     terminal or choice-blocked state appends an end-turn sentinel to a card
//     queue nothing will drain, and past kCardQueueCap appends it writes off the
//     end of the array into its neighbours (combat_state.hpp). Silent memory
//     corruption, not an exception.
//   * A mask that is too RESTRICTIVE silently drops legal actions: the step
//     becomes a no-op, the state stalls, and a search loop can spin without
//     progressing.
// Debug and asan builds do NOT accept this on trust: they assert that the
// supplied mask matches a freshly computed one, so a violated contract fails
// loudly in the presets tests run under. Release builds skip the check -- that
// is the entire point of the overload -- so a caller that only ever runs Release
// gets no diagnosis. Develop against debug.
//
// masks is parallel to states/actions/results and must have the same length
// (asserted, as with the other three).
void advance(std::span<CombatState> states, std::span<const Action> actions,
             std::span<StepResult> results,
             std::span<const ActionMask> masks) noexcept;

}  // namespace sts::engine
