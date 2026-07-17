// The batch API implementation -- combat_begin, legal_actions, advance.
// See advance.hpp for the design/provenance notes and the type-shape rationale.
//
// Provenance (combat construction): AbstractPlayer.preBattlePrep
// (AbstractPlayer.java:1564-1595) / drawPile.initializeDeck(masterDeck)
// (AbstractPlayer.java:1584) / CardGroup.initializeDeck (CardGroup.java:928-955)
// / copy.shuffle(shuffleRng) == Collections.shuffle(group,
// new java.util.Random(shuffleRng.randomLong())) (CardGroup.java:561-567). The
// combat-start shuffle is byte-for-byte the same mechanism as A4.2's in-combat
// reshuffle (piles.cpp shuffle_discard_into_draw): one shuffle_rng.random_long()
// seeds a JdkRandom whose LCG drives jdk_shuffle's Fisher-Yates. Verified by
// reading the cited Java, not assumed.

#include "sts/engine/advance.hpp"

#include <cassert>
#include <cstdint>

#include "sts/engine/action_queue.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/monster_jaw_worm.hpp"
#include "sts/engine/observation.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

CombatState combat_begin(int64_t run_seed, int32_t floor,
                         std::span<const CardId> deck) noexcept {
    assert(deck.size() <= static_cast<std::size_t>(kCardPoolCap) &&
           "deck exceeds card-pool capacity (design doc §4.2)");
    assert(deck.size() <= static_cast<std::size_t>(kDrawCap) &&
           "deck exceeds draw-pile capacity");

    // Value-initialize: zero-fills every field AND padding, so a freshly-begun
    // combat is byte-hash-stable (design doc §4.1). Everything below only writes
    // live fields; drained scratch stays zero.
    CombatState state{};

    // -- Card pool: one CardInstance per deck entry, base cost from the registry
    //    (cards.hpp). upgrade/flags/misc stay 0 (the skeleton deck is all base
    //    cards, design doc §9). --
    const int n = static_cast<int>(deck.size());
    for (int i = 0; i < n; ++i) {
        const CardDef* def = card_def(deck[i]);
        assert(def != nullptr && "deck holds an unknown CardId");
        state.card_pool[i].card_id = static_cast<uint16_t>(deck[i]);
        state.card_pool[i].upgrade = 0;
        state.card_pool[i].cost_now = def->base_cost;
        state.card_pool[i].flags = 0;
        state.card_pool[i].misc = 0;
    }

    // -- The five floor-scoped RNG streams (design doc §3.4 / §3.6). All five
    //    share the identical seed formula run_seed + floor at floor entry
    //    (floor_stream), so they start IDENTICAL -- this is correct and
    //    golden-vector-verified in A1.3, not a bug. --
    state.monster_hp_rng = floor_stream(run_seed, floor);
    state.ai_rng = floor_stream(run_seed, floor);
    state.shuffle_rng = floor_stream(run_seed, floor);
    state.card_random_rng = floor_stream(run_seed, floor);
    state.misc_rng = floor_stream(run_seed, floor);

    // -- Shuffle the deck into the draw pile (provenance in the file header).
    //    initializeDeck copies the master deck (in deck order), shuffles the copy
    //    with shuffleRng, then addToTop's each card front-to-back onto the empty
    //    draw pile -- so the draw pile list becomes the shuffled copy in the SAME
    //    order, getTopCard (== last element) drawn first. Our draw[] convention
    //    (draw[draw_count-1] == top) mirrors that list, identical to A4.2's
    //    reshuffle: build the pool-index order in deck order, draw ONE
    //    shuffle_rng.random_long(), seed a JdkRandom, jdk_shuffle in place, then
    //    the shuffled order IS the draw[] order. --
    for (int i = 0; i < n; ++i) {
        state.draw[i] = static_cast<CardPoolIndex>(i);
    }
    if (n > 1) {
        const int64_t seed = random_long(state.shuffle_rng);  // exactly one draw
        JdkRandom jrng(seed);
        jdk_shuffle(std::span<CardPoolIndex>(state.draw, static_cast<std::size_t>(n)),
                    jrng);
    }
    state.draw_count = static_cast<uint8_t>(n);
    // hand / discard / exhaust / limbo start empty (value-init zeroed).

    // -- Player (placeholder A20 stats -- see advance.hpp's PLACEHOLDER STATS
    //    note; exact Ironclad starting HP is deferred to Stage B per design doc
    //    §11). player_energy is intentionally left 0 here: the start-of-turn
    //    sequence below SETS it to kIroncladBaseEnergy when the first pump()
    //    drains through turn 1 (energy-recharge, action_queue.cpp). --
    state.player_hp = 80;
    state.player_max_hp = 80;
    state.player_block = 0;

    // -- Monster: one Jaw Worm (design doc §9). jaw_worm_init rolls HP from
    //    monster_hp_rng and performs decision #1 (forced-first-move Chomp,
    //    consuming one ai_rng draw), A3.2. --
    state.monster_count = 1;
    jaw_worm_init(state, 0);

    // -- Prime the pump's turn-1 invariants so the FIRST pump() call takes the
    //    start-of-turn branch (step 6) WITHOUT first misfiring the monster-turn
    //    branch (step 4/5). Study of pump_step's priority order (action_queue.cpp)
    //    shows the branch it lands on given empty queues depends on two flags:
    //      * monster_attacks_queued == 1  -> step 4 is skipped, so no monster
    //        turn is queued before the player has had turn 1 (this is exactly the
    //        A3.1 invariant "monster_attacks_queued stays true through the
    //        player's turn"; the flag is cleared only at the end-turn sentinel).
    //      * turn_has_ended == 1          -> step 6 (start_of_turn) fires,
    //        running the opening-hand draw, energy refill, and ++turn (0 -> 1).
    //    With both set and every queue empty, one pump() call runs: step 6
    //    start_of_turn -> step 1 executes the queued DrawCard(5) -> step 7
    //    WAITING_ON_USER. This REUSES the exact turn-N machinery instead of
    //    hand-rolling turn-1 setup (design intent: reuse, don't duplicate). --
    state.turn = 0;                   // start_of_turn's ++turn lands on 1
    state.monster_attacks_queued = 1;
    state.turn_has_ended = 1;
    pump(state, jaw_worm_take_turn);
    // Post: phase == WAITING_ON_USER, turn == 1, energy == kIroncladBaseEnergy,
    // hand_count == kStartOfTurnDrawCount (5), draw_count == n - 5.

    return state;
}

void legal_actions(const CombatState& state, ActionMask& out) noexcept {
    const bool waiting =
        state.phase == static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    for (int i = 0; i < kHandCap; ++i) {
        if (waiting && i < state.hand_count) {
            const CardInstance& c = state.card_pool[state.hand[i]];
            out.can_play[i] = state.player_energy >= c.cost_now;
        } else {
            out.can_play[i] = false;
        }
    }
    out.can_end_turn = waiting;
}

namespace {

// Fill result[i] from the post-pump state: terminal flag, placeholder reward,
// and the observation projection. See advance.hpp's REWARD note.
void fill_result(const CombatState& s, StepResult& r) noexcept {
    const bool player_dead = s.player_hp <= 0;
    bool any_monster_alive = false;
    for (uint8_t m = 0; m < s.monster_count; ++m) {
        if (s.monsters[m].hp > 0) {
            any_monster_alive = true;
            break;
        }
    }
    const bool all_monsters_dead = !any_monster_alive;

    r.terminal = player_dead || all_monsters_dead;
    if (player_dead) {
        r.reward = -1.0f;                       // loss takes precedence
    } else if (all_monsters_dead) {
        r.reward = 1.0f;                        // win
    } else {
        r.reward = 0.0f;                        // combat ongoing
    }
    encode_observation(s, r.obs);
}

}  // namespace

void advance(std::span<CombatState> states, std::span<const Action> actions,
             std::span<StepResult> results) noexcept {
    assert(states.size() == actions.size() &&
           actions.size() == results.size() &&
           "advance(): states/actions/results must be equal-length spans");

    for (std::size_t i = 0; i < states.size(); ++i) {
        CombatState& s = states[i];
        const Action a = actions[i];
        switch (action_verb(a)) {
            case ActionVerb::PLAY_CARD:
                // arg0 = hand index, arg1 = target monster slot (types.hpp).
                // queue_card_play enqueues; pump resolves the play + any monster
                // turn triggered by an end-of-turn (none here, mid-turn play).
                queue_card_play(s, action_arg0(a), action_arg1(a));
                pump(s, jaw_worm_take_turn);
                break;
            case ActionVerb::END_TURN:
                add_card_to_queue_bottom(s, make_end_turn_sentinel());
                pump(s, jaw_worm_take_turn);
                break;
            case ActionVerb::USE_POTION:
            case ActionVerb::CHOOSE:
            default:
                // Out of skeleton scope (design doc §9: no potions, no choice
                // prompts in the 5-card micro-game). Documented no-op -- NOT
                // silently wrong, just not yet built. Stage B wires these.
                break;
        }
        fill_result(s, results[i]);
    }
}

}  // namespace sts::engine
