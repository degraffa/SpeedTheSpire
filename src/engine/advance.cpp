// The batch API implementation -- combat_begin, legal_actions, advance.
// See advance.hpp for the design/provenance notes and the type-shape rationale.
//
// Provenance (combat construction): AbstractPlayer.preBattlePrep
// (AbstractPlayer.java:1564-1595) / drawPile.initializeDeck(masterDeck)
// (AbstractPlayer.java:1584) / CardGroup.initializeDeck (CardGroup.java:928-955)
// / copy.shuffle(shuffleRng) == Collections.shuffle(group,
// new java.util.Random(shuffleRng.randomLong())) (CardGroup.java:561-567). The
// combat-start shuffle is byte-for-byte the same mechanism as the in-combat
// reshuffle (piles.cpp shuffle_discard_into_draw): one shuffle_rng.random_long()
// seeds a JdkRandom whose LCG drives jdk_shuffle's Fisher-Yates.

#include "sts/engine/advance.hpp"

#include <cassert>
#include <cstdint>

#include "sts/engine/action_queue.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"  // spawn_group, dispatch_monster_turn
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
        state.card_pool[i].cost_now = card_cost(*def, 0);
        // Seed per-instance flags from the registry (Stage B B3.1: exhaust/
        // ethereal/innate/unplayable/retain/xcost). The skeleton deck is all
        // base cards with no flags, so this is 0 for every skeleton card.
        state.card_pool[i].flags = card_flags(*def, 0);
        state.card_pool[i].misc = 0;
    }

    // -- The five floor-scoped RNG streams (design doc §3.4 / §3.6). All five
    //    share the identical seed formula run_seed + floor at floor entry
    //    (floor_stream), so they start IDENTICAL -- this is correct
    //    (golden-vector-verified), not a bug. --
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
    //    (draw[draw_count-1] == top) mirrors that list, identical to the in-combat
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
    // CardGroup.initializeDeck (AbstractCard.isInnate): after the one shuffle,
    // normal cards are added to the draw pile first and the collected innate
    // cards are then placed on top. This keeps the shuffled relative order in
    // both groups and guarantees Writhe is among the first opening draws.
    CardPoolIndex innate[kDrawCap]{};
    uint8_t innate_count = 0;
    uint8_t normal_count = 0;
    for (int i = 0; i < n; ++i) {
        const CardPoolIndex pi = state.draw[i];
        if (has_card_flag(state.card_pool[pi].flags, CardFlag::INNATE)) {
            innate[innate_count++] = pi;
        } else {
            state.draw[normal_count++] = pi;
        }
    }
    for (uint8_t i = 0; i < innate_count; ++i) {
        state.draw[static_cast<uint8_t>(normal_count + i)] = innate[i];
    }
    state.draw_count = static_cast<uint8_t>(normal_count + innate_count);
    // hand / discard / exhaust / limbo start empty (value-init zeroed).

    // -- Player (placeholder A20 stats -- see advance.hpp's PLACEHOLDER STATS
    //    note; exact Ironclad starting HP is deferred to Stage B per design doc
    //    §11). player_energy is intentionally left 0 here: the start-of-turn
    //    sequence below SETS it to kIroncladBaseEnergy when the first pump()
    //    drains through turn 1 (energy-recharge, action_queue.cpp). --
    state.player_hp = 80;
    state.player_max_hp = 80;
    state.player_block = 0;

    // -- Monster group (B3.12): the skeleton's fixed encounter is a single Jaw
    //    Worm (design doc §9), spawned through the generalized spawn_group /
    //    monster-dispatch path rather than a hard-wired jaw_worm_init call. For a
    //    single Jaw Worm this is byte-identical to the old path (spawn_group sets
    //    monster_count and calls jaw_worm_init(state, 0)); the real
    //    encounter-driven group derivation (resolve_composition -> game_ids ->
    //    MonsterIds) threads through here once the run layer supplies the
    //    encounter. --
    static constexpr MonsterId kSkeletonGroup[] = {MonsterId::JAW_WORM};
    spawn_group(state, kSkeletonGroup);
    // usePreBattleAction phase (the player's preBattlePrep, AFTER all ctors+init;
    // B3.13's monster_hp_rng curl-up seam). Jaw Worm has none, so this is a no-op
    // for the skeleton group and the 20 fixtures stay byte-identical.
    use_pre_battle_actions(state);

    // -- Prime the pump's turn-1 invariants so the FIRST pump() call takes the
    //    start-of-turn branch (step 6) WITHOUT first misfiring the monster-turn
    //    branch (step 4/5). Study of pump_step's priority order (action_queue.cpp)
    //    shows the branch it lands on given empty queues depends on two flags:
    //      * monster_attacks_queued == 1  -> step 4 is skipped, so no monster
    //        turn is queued before the player has had turn 1 (this is exactly the
    //        invariant "monster_attacks_queued stays true through the player's
    //        turn"; the flag is cleared only at the end-turn sentinel).
    //      * turn_has_ended == 1          -> step 6 (start_of_turn) fires,
    //        running the opening-hand draw, energy refill, and ++turn (0 -> 1).
    //    With both set and every queue empty, one pump() call runs: step 6
    //    start_of_turn -> step 1 executes the queued DrawCard(5) -> step 7
    //    WAITING_ON_USER. This REUSES the exact turn-N machinery instead of
    //    hand-rolling turn-1 setup (design intent: reuse, don't duplicate). --
    state.turn = 0;                   // start_of_turn's ++turn lands on 1
    state.monster_attacks_queued = 1;
    state.turn_has_ended = 1;
    pump(state, dispatch_monster_turn);
    // Post: phase == WAITING_ON_USER, turn == 1, energy == kIroncladBaseEnergy,
    // hand_count == kStartOfTurnDrawCount (5), draw_count == n - 5.

    return state;
}

void legal_actions(const CombatState& state, ActionMask& out) noexcept {
    const bool waiting =
        state.phase == static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);

    // Zero the (hand_slot x target) grid up front so every early-return path
    // (CHOOSE-pending, not-waiting) leaves it well-defined; the main loop fills
    // the rows for enemy-target cards (B3.12).
    for (int i = 0; i < kHandCap; ++i) {
        for (int t = 0; t < kMonsterCap; ++t) {
            out.can_play_target[i][t] = false;
        }
    }

    // CHOOSE-in-combat (Stage B B3.4): if the head of the action queue is an open
    // CHOOSE_CARD that needs a selection, the player is choosing a hand card. The
    // ONLY legal actions are CHOOSE(hand_slot) over the eligible slots -- no
    // play/end-turn while the hand-select screen is open (mandatory single-selects
    // in scope: Armaments+/True Grit+/Warcry, canPickZero == false).
    if (waiting && state.action_count > 0) {
        const ActionQueueItem& front = state.action_queue[state.action_head];
        if (static_cast<Opcode>(front.opcode) == Opcode::CHOOSE_CARD &&
            choice_requires_user(state, front)) {
            const ChoiceKind kind = choose_kind_from_flags(front.flags);
            const uint8_t excluded = choice_excluded_index(front);
            out.choice_pending = true;
            out.choice_from_discard = choice_source_is_discard(kind);
            out.can_end_turn = false;
            // can_choose[i] over the kind's SOURCE pile: hand slots for the hand
            // kinds, discard slots for discard-to-draw-top (Headbutt). For a large
            // discard pile only the first kHandCap slots are reflected here; advance
            // validates any arg0 against the real source-pile count.
            for (int i = 0; i < kHandCap; ++i) {
                out.can_play[i] = false;
                out.can_choose[i] = choice_slot_eligible(
                    state, static_cast<uint8_t>(i), kind, excluded);
            }
            return;
        }
    }

    out.choice_pending = false;
    out.choice_from_discard = false;

    // Clash (canUse): playable only when EVERY card in hand is an Attack
    // (Clash.java:184-194). Computed once and applied to any hand card whose
    // CardDef.requires_all_attacks is set.
    bool all_hand_attacks = true;
    for (uint8_t i = 0; i < state.hand_count; ++i) {
        const CardDef* d =
            card_def(static_cast<CardId>(state.card_pool[state.hand[i]].card_id));
        if (d == nullptr || d->type != CardType::ATTACK) {
            all_hand_attacks = false;
            break;
        }
    }

    // Normality (curse): while a Normality is in hand, no card may be played once
    // 3 cards have been played this turn (Normality.canPlay:29-35 -- a canPlay veto
    // on EVERY other card when cardsPlayedThisTurn >= 3). PLAY_LIMIT is the fixed 3
    // (Normality.java:26). Computed once; forces every hand card unplayable below.
    bool normality_locked = false;
    if (state.cards_played_this_turn >= 3) {
        for (uint8_t i = 0; i < state.hand_count; ++i) {
            if (state.card_pool[state.hand[i]].card_id ==
                static_cast<uint16_t>(CardId::NORMALITY)) {
                normality_locked = true;
                break;
            }
        }
    }

    for (int i = 0; i < kHandCap; ++i) {
        out.can_choose[i] = false;
        if (waiting && i < state.hand_count) {
            const CardInstance& c = state.card_pool[state.hand[i]];
            // UNPLAYABLE (statuses/curses) is never a legal play regardless of
            // energy (Stage B B3.1). Otherwise affordability: energy >= cost_now
            // (X-cost cards carry cost_now 0, so they are always affordable,
            // matching costForTurn == -1).
            const bool unplayable = has_card_flag(c.flags, CardFlag::UNPLAYABLE);
            bool playable = !unplayable && state.player_energy >= c.cost_now &&
                            !normality_locked;
            // Clash's all-attacks canUse predicate (Stage B B3.3).
            if (playable) {
                const CardDef* d = card_def(static_cast<CardId>(c.card_id));
                if (d != nullptr && d->requires_all_attacks && !all_hand_attacks) {
                    playable = false;
                }
            }
            out.can_play[i] = playable;
            // Per-target legality (B3.12): an enemy-target (needs_target) card is
            // legal only against a LIVE monster slot. Self/all/none/random cards
            // ignore the declared target, so their grid row stays all-false and
            // can_play[i] alone carries their legality.
            if (out.can_play[i]) {
                const CardDef* def = card_def(static_cast<CardId>(c.card_id));
                if (def != nullptr && def->needs_target) {
                    for (int t = 0; t < kMonsterCap; ++t) {
                        out.can_play_target[i][t] =
                            t < static_cast<int>(state.monster_count) &&
                            state.monsters[t].hp > 0;
                    }
                }
            }
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
                pump(s, dispatch_monster_turn);
                break;
            case ActionVerb::END_TURN:
                add_card_to_queue_bottom(s, make_end_turn_sentinel());
                pump(s, dispatch_monster_turn);
                break;
            case ActionVerb::CHOOSE: {
                // Stage B B3.4: resolve one selection on the open CHOOSE_CARD at
                // the head of the action queue. arg0 = the chosen hand slot. Ignored
                // (documented no-op) unless a choice is actually pending and the slot
                // is a legal selection -- an illegal CHOOSE cannot corrupt state.
                if (s.phase != static_cast<uint8_t>(CombatPhase::WAITING_ON_USER) ||
                    s.action_count == 0) {
                    break;
                }
                ActionQueueItem& front = s.action_queue[s.action_head];
                if (static_cast<Opcode>(front.opcode) != Opcode::CHOOSE_CARD ||
                    !choice_requires_user(s, front)) {
                    break;
                }
                const ChoiceKind kind = choose_kind_from_flags(front.flags);
                const uint8_t slot = action_arg0(a);
                // arg0 indexes the kind's SOURCE pile (hand, or discard for
                // discard-to-draw-top). choice_slot_eligible checks the bound and
                // the discard source-card exclusion.
                if (!choice_slot_eligible(s, slot, kind,
                                          choice_excluded_index(front))) {
                    break;  // illegal selection -- no-op
                }
                apply_choice_selection(s, slot, kind);
                // One card selected: decrement the remaining count. When it hits 0
                // (or no eligible cards remain), the next pump pops the now-satisfied
                // CHOOSE_CARD; otherwise the pump re-blocks for the next selection.
                front.amount -= 1;
                pump(s, dispatch_monster_turn);
                break;
            }
            case ActionVerb::USE_POTION:
            default:
                // Out of scope (no potions in S1 combat yet). Documented no-op.
                break;
        }
        fill_result(s, results[i]);
    }
}

}  // namespace sts::engine
