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
#include <cstddef>
#include <cstdint>

#include "sts/engine/action_queue.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/knowledge.hpp"         // combat-start knowledge reset/reveal
#include "sts/engine/monster_dispatch.hpp"  // spawn_group, dispatch_monster_turn
#include "sts/engine/omniscient_observation.hpp"
#include "sts/engine/relic_hooks.hpp"       // atPreBattle dispatch
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
        // `deck` is a std::span, so subscripting it converts the loop index to
        // size_type -- the only two -Wsign-conversion sites in src/engine. The
        // index is bounded by deck.size() and cannot be negative, so an explicit
        // widening cast (done ONCE, into a named card id, rather than twice at the
        // two subscripts) is the honest fix; the plain-array subscripts below need
        // no cast.
        const CardId id = deck[static_cast<std::size_t>(i)];
        const CardDef* def = card_def(id);
        assert(def != nullptr && "deck holds an unknown CardId");
        state.card_pool[i].card_id = static_cast<uint16_t>(id);
        state.card_pool[i].upgrade = 0;
        state.card_pool[i].cost_now = card_cost(*def, 0);
        // Seed per-instance flags from the registry (exhaust/
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
    //    sequence below SETS it to energy_master(state) when the first pump()
    //    drains through turn 1 (energy-recharge, action_queue.cpp). This entry
    //    point never fills the relic mirror, so that is kIroncladBaseEnergy
    //    here and the 20 fixtures are unmoved. --
    state.player_hp = 80;
    state.player_max_hp = 80;
    state.player_block = 0;

    // -- Monster group: this entry point's fixed encounter is a single Jaw
    //    Worm (design doc §9), spawned through the generalized spawn_group /
    //    monster-dispatch path rather than a hard-wired jaw_worm_init call. For a
    //    single Jaw Worm this is byte-identical to the old path (spawn_group sets
    //    monster_count and calls jaw_worm_init(state, 0)); the real
    //    encounter-driven group derivation (resolve_composition -> game_ids ->
    //    MonsterIds) threads through here once the run layer supplies the
    //    encounter. --
    // Knowledge observers (knowledge.hpp): fresh combat -> fresh knowledge,
    // BEFORE the spawns below can telegraph a reveal. Pure observation, no
    // state or RNG touch -- the 20 fixtures replay byte-identically.
    knowledge_reset();

    static constexpr MonsterId kSkeletonGroup[] = {MonsterId::JAW_WORM};
    spawn_group(state, kSkeletonGroup);
    // usePreBattleAction phase (the player's preBattlePrep, AFTER all ctors+init;
    // the monster_hp_rng curl-up seam). Jaw Worm has none, so this is a no-op
    // for the skeleton group and the 20 fixtures stay byte-identical.
    use_pre_battle_actions(state);

    // -- applyPreCombatLogic (AbstractPlayer.java:1885-1890), the LAST line of
    //    preBattlePrep (:1607). It runs BEFORE the turn-1 block below, which is
    //    the entire reason atPreBattle is a distinct hook from atBattleStart:
    //    Snecko Eye's Confusion has to be on the player before the opening
    //    DrawCardAction, or the first hand escapes the cost roll. Nothing is
    //    drained here -- what this queues sits at the front of the queue that
    //    begin_first_turn's own pump() drains, exactly as in the game, where
    //    preBattlePrep's actions resolve before AbstractRoom's waitTimer ticks
    //    (AbstractRoom.java:229-235).
    //
    //    A no-op (and byte-identical) without a responding relic: the combat
    //    relic mirror is empty for this entry point, so the 20 fixtures are
    //    unchanged. The RUN entry point (run_advance.cpp enter_combat) carries
    //    the identical call at its step (10), between the emerald entry roll
    //    and begin_first_turn, so the two combat-construction paths cannot
    //    drift. --
    {
        const RelicView rv = player_relics(state);
        dispatch_relics_at_pre_battle(state, rv.relics, rv.count);
    }

    // Knowledge observer: construction is final (pile built, mirror final --
    // empty for this standalone entry point). Arms the REVEAL_DRAW_ORDER
    // record and retro-gates telegraph reveals; the opening draws inside the
    // turn-1 pump below then pop through the ordinary draw hook.
    knowledge_on_combat_ready(state);

    // -- The game's turn-1 block (AbstractRoom.java:236-258). begin_first_turn
    //    (action_queue.cpp) owns it for BOTH combat-construction paths -- this one
    //    and enter_combat (run_advance.cpp) -- so the two cannot drift. It still
    //    REUSES the exact turn-N start-of-turn machinery (design intent: reuse,
    //    don't duplicate); what it does not reuse is getNextAction's step-6
    //    end-of-round pass, which the game cannot reach on turn 1. See the
    //    declaration in action_queue.hpp for the full derivation.
    //
    //    The relic AT_BATTLE_START dispatch (applyStartOfCombatLogic,
    //    AbstractRoom.java:245) also lives inside that shared block, so this
    //    entry point carries it structurally -- it used to carry it not at all
    //    (G6 campaign 2 spot-diff §8.0). A no-op here today because this entry
    //    point's relic mirror is always empty, which is why the 20 fixtures do
    //    not move. --
    begin_first_turn(state, dispatch_monster_turn);
    // Post: phase == WAITING_ON_USER, turn == 1, energy == energy_master(state),
    // hand_count == game_hand_size(state), draw_count == n - that. This entry
    // point has an empty relic mirror, so both are their base constants.

    // initializeDeck's overflow draw for a >5-card placeOnTop collection --
    // see the declaration (advance.hpp) for the preTurnActions derivation of
    // why it runs after the turn-1 pump. This entry point has no relic mirror,
    // so there is no atBattleStart drain between the pump and this call.
    queue_innate_overflow_draw(state, innate_count);

    return state;
}

void queue_innate_overflow_draw(CombatState& state,
                                uint8_t innate_count) noexcept {
    // CardGroup.initializeDeck:951-953. The threshold and the queued amount
    // both read AbstractDungeon.player.masterHandSize -- the SAME field Snecko
    // Eye's onEquip enlarges (SneckoEye.java:31, masterHandSize += 2) and that
    // preBattlePrep snapshots into gameHandSize (AbstractPlayer.java:1579) --
    // NOT the constant 5. game_hand_size(state) is this engine's derivation of
    // that field (Snecko Eye is its only S1 registry writer), so the overflow
    // threshold moves together with the start-of-turn draw: Snecko Eye + 6
    // top-placed cards is NO overflow in the game (6 <= 7), pinned by
    // RunCombatBottle.SneckoEyeRaisesTheInnateOverflowThresholdWithTheDraw.
    // The ordering contract is on the declaration in advance.hpp. A no-op
    // leaves the state byte-untouched, so every existing combat whose
    // top-placed collection fits the hand (all 20 fixtures included -- empty
    // relic mirror, hand size 5) is unchanged.
    const int32_t hand = game_hand_size(state);
    if (static_cast<int32_t>(innate_count) <= hand) {
        return;
    }
    ActionQueueItem draw{};
    draw.opcode = static_cast<uint16_t>(Opcode::DRAW);
    draw.src = kActorPlayer;
    draw.tgt = kActorPlayer;
    draw.amount = static_cast<int32_t>(innate_count) - hand;
    add_to_bottom(state, draw);
    pump(state, dispatch_monster_turn);
}

void legal_actions(const CombatState& state, ActionMask& out) noexcept {
    const bool waiting =
        state.phase == static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);

    // Zero the (hand_slot x target) grid up front so every early-return path
    // (CHOOSE-pending, not-waiting) leaves it well-defined; the main loop fills
    // the rows for enemy-target cards.
    for (int i = 0; i < kHandCap; ++i) {
        for (int t = 0; t < kMonsterCap; ++t) {
            out.can_play_target[i][t] = false;
        }
    }

    // CHOOSE-in-combat: if the head of the action queue is an open
    // CHOOSE_CARD that needs a selection, the player is choosing a hand card. The
    // ONLY legal actions are CHOOSE(hand_slot) over the eligible slots -- no
    // play/end-turn while the hand-select screen is open (mandatory single-selects
    // in scope: Armaments+/True Grit+/Warcry, canPickZero == false).
    if (waiting && state.action_count > 0) {
        const ActionQueueItem& front = state.action_queue[state.action_head];
        if (static_cast<Opcode>(front.opcode) == Opcode::DISCOVERY &&
            discovery_choice_prepared(front)) {
            out.choice_pending = true;
            out.choice_from_discard = false;
            out.choice_from_exhaust = false;
            out.choice_from_generated = true;
            out.choice_from_draw = false;
            out.choice_optional = false;
            out.can_confirm_choice = false;
            out.choice_selected_count = 0;
            out.can_end_turn = false;
            // Skip is a legal close for a TYPED discovery only (Attack/Skill/
            // Power Potion): customCombatOpen's `skippable` is
            // `this.cardType != null` (DiscoveryAction.java:49,
            // CardRewardScreen.java:485-500). The Discovery card and the
            // Colorless Potion open the same screen with the button hidden.
            out.can_skip_choice = discovery_skippable(front);
            for (int i = 0; i < kHandCap; ++i) {
                out.can_play[i] = false;
                out.can_choose[i] = i < kDiscoveryChoiceCount;
            }
            return;
        }
        if (static_cast<Opcode>(front.opcode) == Opcode::CHOOSE_CARD &&
            choice_requires_user(state, front)) {
            const ChoiceKind kind = choose_kind_from_flags(front.flags);
            const uint8_t type_filter =
                choose_type_filter_from_flags(front.flags);
            out.choice_pending = true;
            out.choice_from_discard = choice_source(kind) == ChoiceSource::DISCARD;
            out.choice_from_exhaust = choice_source(kind) == ChoiceSource::EXHAUST;
            out.choice_from_draw = choice_source(kind) == ChoiceSource::DRAW;
            out.choice_from_generated = false;
            out.can_skip_choice = false;
            out.can_end_turn = false;
            const bool optional = choose_is_optional(front.flags);
            out.choice_optional = optional;
            // The confirm button. Both in-scope optional screens open with
            // canPickZero, which enables it immediately and never disables it
            // again, so "the screen is open" IS "confirm is legal" -- an empty
            // confirm included. A mandatory screen has no confirm move at all:
            // it ends when its count is met.
            out.can_confirm_choice = optional;
            out.choice_selected_count =
                optional ? choose_selected_count(front.flags) : uint8_t{0};
            // can_choose[i] over the kind's SOURCE pile: hand slots for the hand
            // kinds, discard slots for discard-to-draw-top (Headbutt), draw-pile
            // slots for the type-filtered deck-to-hand choices. For a large
            // source pile only the first kHandCap slots are reflected here;
            // advance validates any arg0 against the real source-pile count.
            //
            // An OPTIONAL choice reads its own predicate instead: the hand there
            // is [unpicked] ++ [picked], every picked slot is toggleable back
            // out, and an unpicked one is only offered while the selection has
            // room (advance.hpp).
            for (int i = 0; i < kHandCap; ++i) {
                out.can_play[i] = false;
                out.can_choose[i] =
                    optional ? optional_choice_slot_legal(
                                   state, front, static_cast<uint8_t>(i))
                             : choice_slot_eligible(
                                   state, static_cast<uint8_t>(i), kind,
                                   type_filter);
            }
            return;
        }
    }

    out.choice_pending = false;
    out.choice_from_discard = false;
    out.choice_from_exhaust = false;
    out.choice_from_generated = false;
    out.can_skip_choice = false;
    out.choice_from_draw = false;
    out.choice_optional = false;
    out.can_confirm_choice = false;
    out.choice_selected_count = 0;

    for (int i = 0; i < kHandCap; ++i) {
        out.can_choose[i] = false;
        if (waiting && i < state.hand_count) {
            const CardPoolIndex pi = state.hand[i];
            const CardDef* def = card_def(
                static_cast<CardId>(state.card_pool[pi].card_id));
            // Shared authority with dequeue-time revalidation. Targeted cards
            // expose the target-independent canUse portion here; the exact
            // cardPlayable/reticle result is carried by can_play_target.
            out.can_play[i] =
                card_can_use_without_target(state, pi, /*autoplay=*/false);
            // Per-target legality: an enemy-target (needs_target) card is
            // legal only against a monster slot that is IN the fight -- the
            // game's target reticle skips isDeadOrEscaped monsters, so an
            // escaped Looter is not a legal target even though its hp is
            // positive. Self/all/none/random cards ignore the declared target,
            // so their grid row stays all-false and can_play[i] alone carries
            // their legality. UPGRADE-AWARE: Blind+/Trip+ reassign the card
            // target to ALL_ENEMY in upgrade() (Blind.java:48 / Trip.java:53),
            // so their upgraded instances take no target row.
            if (out.can_play[i] && def != nullptr &&
                card_needs_target(*def, state.card_pool[pi].upgrade)) {
                for (int t = 0; t < kMonsterCap; ++t) {
                    out.can_play_target[i][t] =
                        t < static_cast<int>(state.monster_count) &&
                        !monster_dead_or_escaped(state.monsters[t]) &&
                        card_can_use(state, pi, static_cast<uint8_t>(t),
                                     /*autoplay=*/false);
                }
            } else if (out.can_play[i]) {
                out.can_play[i] =
                    card_can_use(state, pi, kActorPlayer, /*autoplay=*/false);
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
    const bool player_escaped = (s.flags & kCombatFlagPlayerEscaped) != 0u;
    // "In the fight" is the pump's own predicate (monster_dead_or_escaped): an
    // ESCAPED monster keeps positive hp but ends the battle like a dead one, so
    // a mugged combat terminates here too. The terminal STATE stays distinct --
    // the escaped record reads hp > 0 with kMonsterFlagEscaped, a killed one
    // reads hp <= 0, and kCombatFlagMugged marks the room.
    bool any_monster_in_fight = false;
    for (uint8_t m = 0; m < s.monster_count; ++m) {
        if (!monster_dead_or_escaped(s.monsters[m])) {
            any_monster_in_fight = true;
            break;
        }
    }

    r.terminal = player_dead || player_escaped || !any_monster_in_fight;
    if (player_dead) {
        r.reward = -1.0f;                       // loss takes precedence
    } else if (player_escaped) {
        r.reward = 0.0f;                        // Smoke Bomb: survived, no kill
    } else if (!any_monster_in_fight) {
        r.reward = 1.0f;                        // win (kills and/or escapes)
    } else {
        r.reward = 0.0f;                        // combat ongoing
    }
    omniscient_encode_observation(s, r.omniscient_obs);
}

// PLAY_CARD legality, read ENTIRELY out of an ActionMask that legal_actions()
// just filled -- no predicate of its own.
//
// can_play[slot] already folds every play gate (phase, hand bound,
// affordability, UNPLAYABLE + Blue Candle, Normality's play limit, Clash's
// all-attacks rule). Targeting is the one part that lives in the grid, and the
// mask's documented shape (advance.hpp TARGETING) is that an enemy-target card
// gets a row with a true for each LIVE monster slot while a self/all/none/random
// card's row stays all-false. So "does this card take a target?" is answered by
// asking whether the row has any true at all, rather than by re-reading
// CardDef::needs_target here -- which would be a second copy of the decision
// legal_actions already made, i.e. exactly the kind of duplicate that drifts.
[[nodiscard]] bool play_is_legal(const ActionMask& mask, uint8_t slot,
                                 uint8_t target) noexcept {
    if (slot >= kHandCap || !mask.can_play[slot]) {
        return false;
    }
    bool takes_target = false;
    for (int t = 0; t < kMonsterCap; ++t) {
        takes_target = takes_target || mask.can_play_target[slot][t];
    }
    if (!takes_target) {
        return true;  // non-target card: can_play[slot] alone carries legality
    }
    return target < kMonsterCap && mask.can_play_target[slot][target];
}

#ifndef NDEBUG
// Does `m` say what legal_actions() would say about `s` right now?
//
// Only compiled into Debug/ASan builds, where it backs the mask-accepting
// overload's contract assert. Field-wise rather than std::memcmp: ActionMask is
// all-bool today and so has no padding, but that is a property of the current
// field list, not a guarantee, and a memcmp would start reporting phantom
// mismatches the day a wider field is added.
[[nodiscard]] bool mask_matches_state(const CombatState& s,
                                      const ActionMask& m) noexcept {
    ActionMask fresh{};
    legal_actions(s, fresh);
    for (int i = 0; i < kHandCap; ++i) {
        if (m.can_play[i] != fresh.can_play[i]) return false;
        if (m.can_choose[i] != fresh.can_choose[i]) return false;
        for (int t = 0; t < kMonsterCap; ++t) {
            if (m.can_play_target[i][t] != fresh.can_play_target[i][t]) return false;
        }
    }
    return m.can_end_turn == fresh.can_end_turn &&
           m.choice_pending == fresh.choice_pending &&
           m.choice_from_discard == fresh.choice_from_discard &&
           m.choice_from_exhaust == fresh.choice_from_exhaust &&
           m.choice_from_generated == fresh.choice_from_generated &&
           m.can_skip_choice == fresh.can_skip_choice &&
           m.choice_from_draw == fresh.choice_from_draw &&
           m.choice_optional == fresh.choice_optional &&
           m.can_confirm_choice == fresh.can_confirm_choice &&
           m.choice_selected_count == fresh.choice_selected_count;
}
#endif

// ONE state, ONE action, ONE already-built mask -- the whole of a step.
//
// Both advance() overloads route through this, which is what makes "the
// mask-accepting overload has identical guard semantics" a structural fact
// rather than a claim to re-verify: there is a single dispatch and a single
// gate, and the overloads differ only in who built the mask they hand it.
void step_one(CombatState& s, Action a, const ActionMask& mask,
              StepResult& result) noexcept {
    // THE LEGALITY GATE -- one for EVERY verb, not just CHOOSE.
    //
    // advance()'s contract is that an action which is not legal for the
    // state's current phase is a no-op that cannot mutate state. CHOOSE used
    // to be the only verb that honoured it; PLAY_CARD and END_TURN did not,
    // and that was a memory-corruption bug, not a cosmetic one. A batch API
    // is normally driven by keeping the batch uniform and stepping finished
    // combats rather than compacting them, so a terminal state gets fed
    // END_TURN over and over. pump_step (action_queue.cpp) short-circuits to
    // COMBAT_OVER before it ever reaches the card-queue step, so every one of
    // those sentinels was appended and NONE was ever drained: 16 steps filled
    // card_queue and the 17th ran off the end of it, into card_queue_count /
    // pad_cardq / monster_queue / monster_attacks_queued / the relic mirror
    // (combat_state.hpp). The same holds with a hand-select screen open --
    // pump blocks on the CHOOSE_CARD at the action-queue head, so the card
    // queue is not drained there either and a live (non-terminal) state
    // overflows just as readily.
    //
    // WHY THE GUARD DELEGATES TO legal_actions() instead of testing
    // `phase == WAITING_ON_USER` itself: legal_actions() is the function that
    // decides what a caller may send, and a guard that re-derives its own
    // version of that decision is free to disagree with it. It would here,
    // immediately -- while a hand-select screen is open the phase IS
    // WAITING_ON_USER, yet legal_actions() reports can_end_turn == false and
    // can_play all-false, so a phase-only guard would still admit the
    // END_TURN that overflows the card queue. "Not terminal" is weaker still.
    // Reading the mask is the only formulation with no second copy of the
    // rule to drift: any future gate added to legal_actions() (a new curse, a
    // new phase, a new relic veto) is inherited here for free, and the mask
    // and the dispatch cannot disagree because there is only one of them.
    //
    // COST, measured, not guessed (bench_advance, Release+LTO, 10k-state
    // batch, 25 fixed iterations so every state is still LIVE): 2.09 ms ->
    // 2.86 ms per batch step, 4.37M -> 3.21M steps/s. That is the worst case
    // by construction -- the benchmark's policy already calls
    // legal_actions() to pick its action, so this gate exactly DOUBLES the
    // mask work on the hot path, and nothing else in the step is expensive
    // enough to dilute it. (Over a full 3s run the same binary reports
    // 10.5M steps/s, because most states are terminal by then and a rejected
    // action skips the pump entirely.)
    //
    // It is paid deliberately: a guard that cannot drift is worth more than a
    // mask rebuild, and the alternative on offer was silent memory
    // corruption. The cost is also RECOVERABLE now, without weakening
    // anything: the four-span advance() overload takes the caller's
    // already-computed mask (a policy has one in hand by construction) and
    // feeds it to this very function, so a search loop pays for the mask
    // once. In the same all-live regime that measured 3.21M above, the
    // overload runs at 4.32-4.46M -- level with the 4.35M pre-guard baseline
    // (advance.hpp has the interleaved A/B). What is NOT on offer is a
    // hand-rolled phase test in here: that would trade the cost back for
    // exactly the drift this replaced.

    switch (action_verb(a)) {
        case ActionVerb::PLAY_CARD:
            // arg0 = hand index, arg1 = target monster slot (types.hpp).
            // queue_card_play enqueues; pump resolves the play + any monster
            // turn triggered by an end-of-turn (none here, mid-turn play).
            if (!play_is_legal(mask, action_arg0(a), action_arg1(a))) {
                break;  // illegal play -- documented no-op
            }
            queue_card_play(s, action_arg0(a), action_arg1(a));
            pump(s, dispatch_monster_turn);
            break;
        case ActionVerb::END_TURN:
            if (!mask.can_end_turn) {
                break;  // illegal end-turn -- documented no-op
            }
            add_card_to_queue_bottom(s, make_end_turn_sentinel());
            pump(s, dispatch_monster_turn);
            break;
        case ActionVerb::CHOOSE: {
            // Resolve one selection on the open CHOOSE_CARD at
            // the head of the action queue. arg0 = the chosen hand slot. Ignored
            // (documented no-op) unless a choice is actually pending and the slot
            // is a legal selection -- an illegal CHOOSE cannot corrupt state.
            //
            // choice_pending IS the "a real prompt is open" chain this case
            // used to spell out for itself (waiting AND action_count > 0 AND
            // the head is a CHOOSE_CARD AND choice_requires_user); reading it
            // off the mask keeps that chain in one place.
            if (!mask.choice_pending) {
                break;
            }
            ActionQueueItem& front = s.action_queue[s.action_head];
            const uint8_t slot = action_arg0(a);
            if (mask.choice_from_generated) {
                // Closing the screen -- pick OR skip -- resumes the frozen
                // DiscoveryAction, whose remaining ticks each regenerate and
                // discard a full offer (kDiscoveryWastedRegens, interp.hpp: the
                // derivation and the seven-capture table). Java order within
                // those ticks: tick 2 regenerates and THEN retrieves the
                // chosen card (DiscoveryAction.java:47 before :53-85), ticks
                // 3..6 only regenerate. The skip path is the same ticks with
                // discoveryCard still null (SkipCardButton closes the screen
                // without writing it, SkipCardButton.java:64-66;
                // CardRewardScreen sets it only on a card pick, :234), so it
                // consumes the item, creates nothing, and refunds nothing.
                if (slot == kChooseSkipCard) {
                    if (!mask.can_skip_choice) {
                        break;  // non-skippable screen -- documented no-op
                    }
                    discard_discovery_regens(s, front, kDiscoveryWastedRegens);
                    ActionQueueItem consumed{};
                    (void)pop_action_front(s, consumed);
                    pump(s, dispatch_monster_turn);
                    break;
                }
                if (slot >= kDiscoveryChoiceCount || !mask.can_choose[slot]) {
                    break;
                }
                discard_discovery_regens(s, front, 1);  // tick 2's regen...
                resolve_discovery_choice(s, front, slot);  // ...then retrieve
                discard_discovery_regens(s, front,
                                         kDiscoveryWastedRegens - 1);
                ActionQueueItem consumed{};
                (void)pop_action_front(s, consumed);
                pump(s, dispatch_monster_turn);
                break;
            }
            if (mask.choice_optional) {
                // TOGGLE, not commit: the card moves between the hand and the
                // pending selection and nothing is applied until CONFIRM. The
                // legality predicate is the same function legal_actions() built
                // can_choose[] from, for the reason spelled out below -- except
                // that here can_choose[] is not narrower than the rule (an
                // optional choice always sources the ten-slot hand), so the two
                // agree exactly.
                if (!optional_choice_slot_legal(s, front, slot)) {
                    break;  // illegal toggle -- no-op
                }
                toggle_optional_choice_slot(s, front, slot);
                // No pump: the item stays at the head, still blocking, and the
                // screen is still open. Only CONFIRM ends it.
                break;
            }
            const ChoiceKind kind = choose_kind_from_flags(front.flags);
            // arg0 indexes the kind's SOURCE pile (hand, or discard for
            // discard-to-draw-top). choice_slot_eligible checks the bound (the
            // just-played source card is in the limbo pile, so no discard
            // exclusion exists any more).
            //
            // This is the ONE place the guard reads a predicate rather than
            // the mask, and it is still the same single source of truth:
            // choice_slot_eligible is the very function legal_actions() calls
            // to fill can_choose[]. can_choose[] cannot be used directly here
            // because it is deliberately NARROWER than the rule -- it only
            // reflects the first kHandCap slots, while a discard-source choice
            // may legally name a discard slot beyond that (advance.hpp,
            // "Discard-source CHOOSE"). Gating on the array would reject legal
            // selections; gating on the function it is built from does not.
            if (!choice_slot_eligible(s, slot, kind,
                                      choose_type_filter_from_flags(front.flags))) {
                break;  // illegal selection -- no-op
            }
            // DUPLICATE carries its clone count in the packed flags
            // (Dual Wield magicNumber) and, being a REAL prompt here, takes
            // the prompted-resolution branch (the hand-select screen's
            // reorder bookkeeping, DualWieldAction.java:59-84).
            apply_choice_selection(s, slot, kind,
                                   choose_copies_from_flags(front.flags),
                                   /*prompted=*/true);
            // One card selected: decrement the remaining count. When it hits 0
            // (or no eligible cards remain), the next pump pops the now-satisfied
            // CHOOSE_CARD; otherwise the pump re-blocks for the next selection.
            front.amount -= 1;
            pump(s, dispatch_monster_turn);
            break;
        }
        case ActionVerb::CONFIRM: {
            // The hand-select screen's confirm button. Legal only while an
            // OPTIONAL choice is open (mask.can_confirm_choice, which is exactly
            // that condition) -- a mandatory screen has no button to press, and
            // outside a choice there is no screen at all.
            if (!mask.choice_pending || !mask.can_confirm_choice) {
                break;
            }
            // POP FIRST, then resolve off the popped copy. The Java action is
            // already off the queue while its update() runs -- and one kind,
            // HAND_TO_DISCARD_THEN_DRAW (Gambler's Brew / Gambling Chip),
            // add_to_TOPs a DrawCardAction from inside its resolution
            // (GamblingChipAction.java:53). Resolving before the pop would put
            // that draw at the head, and the pop would then throw the DRAW away
            // instead of the finished CHOOSE_CARD. The order is invisible to
            // every other kind: resolve_optional_choice reads only the item's
            // flags and the hand suffix, and the pop touches neither.
            ActionQueueItem consumed{};
            (void)pop_action_front(s, consumed);
            resolve_optional_choice(s, consumed);
            pump(s, dispatch_monster_turn);
            break;
        }
        case ActionVerb::USE_POTION:
        default:
            // Out of scope for the combat-only entry point: potions belong to
            // the run (the belt lives in RunState), so the run overload of
            // advance() handles USE_POTION before it ever delegates a combat
            // step down here -- and it gates it on its own RunActionMask the
            // same way this switch gates the combat verbs (run_advance.cpp
            // step_potion). Here it is an UNCONDITIONAL no-op, which honours
            // the contract trivially: no state is touched on any path.
            break;
    }
    fill_result(s, result);
}

}  // namespace

void advance(std::span<CombatState> states, std::span<const Action> actions,
             std::span<StepResult> results) noexcept {
    assert(states.size() == actions.size() &&
           actions.size() == results.size() &&
           "advance(): states/actions/results must be equal-length spans");

    for (std::size_t i = 0; i < states.size(); ++i) {
        // No caller mask on this path, so build one per state. See step_one's
        // gate comment for what it costs and the overload below for how a caller
        // that already has the mask avoids paying it twice.
        ActionMask mask{};
        legal_actions(states[i], mask);
        step_one(states[i], actions[i], mask, results[i]);
    }
}

void advance(std::span<CombatState> states, std::span<const Action> actions,
             std::span<StepResult> results,
             std::span<const ActionMask> masks) noexcept {
    assert(states.size() == actions.size() &&
           actions.size() == results.size() &&
           actions.size() == masks.size() &&
           "advance(): states/actions/results/masks must be equal-length spans");

    for (std::size_t i = 0; i < states.size(); ++i) {
        // THE CALLER'S CONTRACT, CHECKED WHERE CHECKING IS FREE. masks[i] must
        // be what legal_actions() would produce for states[i] as it stands right
        // now (advance.hpp spells out what a violation costs -- a too-permissive
        // mask walks an illegal action into the pump, which is the memory
        // corruption the guard exists to stop). Debug and ASan builds recompute
        // and compare, so a stale or mismatched mask aborts in the presets the
        // suite actually runs under; Release trusts the caller, which is the
        // whole reason this overload exists. The recomputation is inside the
        // assert's own expression, so NDEBUG removes it entirely rather than
        // leaving a computed-and-discarded mask behind.
        assert(mask_matches_state(states[i], masks[i]) &&
               "advance(): supplied ActionMask does not match its CombatState -- "
               "rebuild it from the state you are about to step");
        step_one(states[i], actions[i], masks[i], results[i]);
    }
}

}  // namespace sts::engine
