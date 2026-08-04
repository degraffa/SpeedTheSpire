// Pile operations -- draw / reshuffle / exhaust. See piles.hpp for the full
// provenance, the hand-size-cap correction, and the headless timing
// simplification rationale.
//
// Provenance: AbstractPlayer.draw(int/void) (AbstractPlayer.java:1632-1665),
// DrawCardAction.update (DrawCardAction.java:63-127), EmptyDeckShuffleAction.
// update (EmptyDeckShuffleAction.java:42-64), CardGroup.shuffle(Random)
// (CardGroup.java:565-567), Soul.shuffle (Soul.java:90-102). Reuses the
// golden-tested JdkRandom + jdk_shuffle and random_long. Design doc §3.3,
// §9; §10 trap 2.

#include "sts/engine/piles.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <span>

#include "sts/engine/cards.hpp"        // card_cost (reset_cost_for_turn)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/knowledge.hpp"    // draw/shuffle knowledge hooks (observers)
#include "sts/engine/power_hooks.hpp"  // onExhaust dispatch (EXHAUST opcode path)
#include "sts/engine/relic_hooks.hpp"  // onShuffle dispatch (Sundial)
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

// Execute one EmptyDeckShuffleAction exactly as the game does.  The action is
// allowed to see an empty discard pile: CardGroup.shuffle(Random) still draws
// one shuffleRng.randomLong(), and the constructor has already fired every
// relic's onShuffle hook.  DrawCardAction can create precisely that empty
// action when a draw request exceeds every card left in draw + discard.
//
// Most authored shuffle sites guard on a non-empty discard before constructing
// the action.  Their public helper below preserves that guard; draw_cards calls
// this lower-level body for the one unguarded recursive-DrawCardAction case.
void execute_empty_deck_shuffle(CombatState& s) noexcept {
    const RelicView rv = player_relics(s);
    dispatch_relics_on_shuffle(s, rv.relics, rv.count);

    JdkRandom rng(random_long(s.shuffle_rng));
    jdk_shuffle(std::span<CardPoolIndex>(s.discard, s.discard_count), rng);

    const int moved = s.discard_count;
    int n = s.discard_count;
    if (s.draw_count + n > kDrawCap) {
        n = kDrawCap - s.draw_count;  // defensive clamp; unreachable in S1
    }
    std::copy(s.discard, s.discard + n, s.draw + s.draw_count);
    s.draw_count = static_cast<uint8_t>(s.draw_count + n);
    s.discard_count = 0;

    // Knowledge observer (knowledge.hpp): the pile was rewritten. The empty
    // action over an empty discard moves nothing and leaves the pile's order
    // untouched, so it is NOT a knowledge event -- the RNG draw it spends is
    // state evolution, not information.
    if (moved > 0) {
        knowledge_on_shuffle(s);
    }
}

}  // namespace

void reset_cost_for_turn(CombatState& s, uint8_t pool_index) noexcept {
    if (pool_index >= kCardPoolCap) {
        return;
    }
    CardInstance& c = s.card_pool[pool_index];
    if (!has_card_flag(c.flags, CardFlag::COST_MODIFIED_FOR_TURN)) {
        return;
    }
    const CardDef* def = card_def(static_cast<CardId>(c.card_id));
    if (has_card_flag(c.flags, CardFlag::SAVED_BASE_COST)) {
        c.cost_now = saved_base_cost(c.flags);
    } else if (def != nullptr) {
        c.cost_now = card_cost(*def, c.upgrade);  // costForTurn = cost
    }
    c.flags = static_cast<uint16_t>(
        c.flags & ~card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN) &
        ~card_flag_bit(CardFlag::SAVED_BASE_COST) & ~kSavedBaseCostMask);
}

void shuffle_discard_into_draw(CombatState& s) noexcept {
    if (s.discard_count == 0) {
        return;  // guarded authored sites never construct an empty action
    }

    // Relics onShuffle (EmptyDeckShuffleAction ctor, EmptyDeckShuffleAction.java:
    // 37-39): fires as the reshuffle action is created -- i.e. BEFORE the shuffle
    // draw. CardGroup.shuffle then draws exactly one shuffleRng.randomLong() to
    // seed its private JDK Fisher-Yates, and Soul.shuffle moves the result into
    // draw-pile order. The shared body also handles the empty action that only
    // DrawCardAction's recursive overdraw path can construct.
    execute_empty_deck_shuffle(s);
}

void reshuffle_all(CombatState& s) noexcept {
    // The card whose program queued this sits in the LIMBO pile (piles.hpp), so
    // the discard scanned here is exactly the game's discardPile at
    // DeepBreath.use time -- no exclusion needed.
    //
    // DeepBreath.use (DeepBreath.java:34-38) queues EmptyDeckShuffleAction AND
    // ShuffleAction(drawPile, false) under ONE `discardPile.size() > 0` guard.
    // Read that guard once, here: the first shuffle empties the discard, so a
    // second, separately-guarded step could never see the same answer.
    if (s.discard_count == 0) {
        return;  // both actions skipped -- ZERO shuffle_rng draws
    }

    // EmptyDeckShuffleAction. Its CTOR fires the relics' onShuffle
    // (EmptyDeckShuffleAction.java:37-39), before the shuffle draw; Sundial counts
    // these, and its GainEnergy is queued, so the RNG accounting below is
    // untouched.
    {
        const RelicView rv = player_relics(s);
        dispatch_relics_on_shuffle(s, rv.relics, rv.count);
    }
    // ONE shuffleRng.randomLong() seeds a fresh java.util.Random driving
    // Collections.shuffle over the discard list (CardGroup.shuffle(Random),
    // CardGroup.java:565-567). Then update() walks the shuffled list front-to-back
    // moving each card via Soul.shuffle -> drawPile.addToTop, i.e. appending to the
    // END of the draw list -- which is our draw[] tail, since draw[draw_count-1] is
    // the top card.
    {
        JdkRandom rng(random_long(s.shuffle_rng));
        jdk_shuffle(std::span<CardPoolIndex>(s.discard, s.discard_count), rng);
    }
    int moved = s.discard_count;
    if (s.draw_count + moved > kDrawCap) {
        moved = kDrawCap - s.draw_count;  // defensive clamp, as above
    }
    std::copy(s.discard, s.discard + moved, s.draw + s.draw_count);
    s.draw_count = static_cast<uint8_t>(s.draw_count + moved);
    s.discard_count = 0;

    // ShuffleAction.update (ShuffleAction.java:31-40) with triggerRelics == false:
    // no second onShuffle pass, just group.shuffle() -- CardGroup.shuffle()
    // (CardGroup.java:561-563) takes ONE MORE shuffleRng.randomLong() and
    // Collections.shuffle()s the WHOLE draw pile, which by now holds the
    // pre-existing draw cards plus everything the first half moved in.
    {
        JdkRandom rng(random_long(s.shuffle_rng));
        jdk_shuffle(std::span<CardPoolIndex>(s.draw, s.draw_count), rng);
    }

    // Knowledge observer: one hook for the whole composite -- the inlined
    // first half above has no hook of its own, and only the final full-pile
    // shuffle's outcome is observable, so firing once here is exact.
    knowledge_on_shuffle(s);
}

int draw_cards(CombatState& s, int amount) noexcept {
    // AbstractPlayer.draw(): a full hand refuses to draw anything at all.
    if (s.hand_count >= kHandCap) {
        return 0;
    }

    // Hand-size cap, applied ONCE up front (DrawCardAction.java:92-97):
    // amount = min(amount, kHandCap - hand_count). Overflowing cards are simply
    // never drawn -- there is NO draw-then-discard.
    const int capacity = kHandCap - s.hand_count;
    if (amount > capacity) {
        amount = capacity;
    }
    if (amount <= 0) {
        return 0;  // DrawCardAction: amount <= 0 draws nothing
    }

    // DrawCardAction does not merely loop until both piles are empty.  If this
    // request begins with at least one available card but asks for more than
    // draw + discard contain, its recursive tail sees a non-empty draw pile,
    // notices amount > deckSize, and constructs one final
    // EmptyDeckShuffleAction over an EMPTY discard.  That action still fires
    // onShuffle and consumes one shuffle_rng draw.  STS300219 turns 4-5 are the
    // live witness: four surviving cards requested by a five-card turn draw
    // advance shuffle_rng twice (the real reshuffle, then this empty one).
    const int available_at_entry =
        static_cast<int>(s.draw_count) + static_cast<int>(s.discard_count);
    const bool trailing_empty_shuffle =
        available_at_entry > 0 && amount > available_at_entry;

    int drawn = 0;
    for (int i = 0; i < amount; ++i) {
        if (s.draw_count == 0) {
            // Deck exhausted mid-draw: reshuffle the discard in and continue (the
            // headless collapse of DrawCardAction's EmptyDeckShuffle re-queue).
            shuffle_discard_into_draw(s);
            if (s.draw_count == 0) {
                break;  // both piles empty (deckSize+discardSize==0) -- stop
            }
        }
        const CardPoolIndex top = s.draw[s.draw_count - 1];  // top == end of array
        --s.draw_count;
        s.hand[s.hand_count] = top;
        ++s.hand_count;
        ++drawn;
        knowledge_on_draw_top(s, top);  // observer: the known top (if any) left
    }
    if (trailing_empty_shuffle) {
        execute_empty_deck_shuffle(s);
    }
    return drawn;
}

void exhaust_card(CombatState& s, int pool_index) noexcept {
    if (pool_index < 0 || pool_index > 0xFF) {
        return;
    }
    const CardPoolIndex idx = static_cast<CardPoolIndex>(pool_index);
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        if (s.hand[i] == idx) {
            for (uint8_t j = static_cast<uint8_t>(i + 1); j < s.hand_count; ++j) {
                s.hand[j - 1] = s.hand[j];
            }
            --s.hand_count;
            if (s.exhaust_count < kExhaustCap) {
                s.exhaust[s.exhaust_count] = idx;
                ++s.exhaust_count;
            }
            // An exhausted card's this-turn-only cost reverts
            // (ExhaustCardEffect.update:41-43 resetAttributes -> costForTurn =
            // cost). Restore cost_now from the registry row and clear the bit.
            reset_cost_for_turn(s, idx);
            // §5.5 onExhaust (CardGroup.moveToExhaustPile): fires as the card lands
            // in the exhaust pile. No-op without an on-exhaust power or card-level
            // on-exhaust program, so the skeleton EXHAUST-opcode path is unchanged.
            dispatch_on_exhaust(s, idx, s.card_pool[idx].card_id);
            return;
        }
    }
}

// --- The limbo pile (cardInUse) -- see piles.hpp for the model ---------------

void limbo_add(CombatState& s, uint8_t pool_index) noexcept {
    assert(s.limbo_count < kLimboCap &&
           "limbo overflow (design doc §4.1: hard assert)");
    s.limbo[s.limbo_count] = pool_index;
    ++s.limbo_count;
}

bool limbo_remove(CombatState& s, uint8_t pool_index) noexcept {
    for (uint8_t i = 0; i < s.limbo_count; ++i) {
        if (s.limbo[i] == pool_index) {
            for (uint8_t j = static_cast<uint8_t>(i + 1); j < s.limbo_count; ++j) {
                s.limbo[j - 1] = s.limbo[j];
            }
            --s.limbo_count;
            return true;
        }
    }
    return false;
}

bool file_card_from_limbo(CombatState& s, uint8_t pool_index, bool to_exhaust,
                          bool remove_only) noexcept {
    if (!limbo_remove(s, pool_index)) {
        return false;  // defensive: not a limbo card (malformed item)
    }
    if (remove_only) {
        // purgeOnUse poof / POWER empower (UseCardAction.java:89-108): the card
        // lands in NO pile. The pool row stays as inert instance storage (the
        // documented purged-copy row leak is unchanged by the limbo model).
        return true;
    }
    if (to_exhaust) {
        assert(s.exhaust_count < kExhaustCap &&
               "exhaust overflow (design doc §4.1: hard assert)");
        s.exhaust[s.exhaust_count] = pool_index;
        ++s.exhaust_count;
        // resetAttributes on exhaust (ExhaustCardEffect.update:41-43): a
        // this-turn-only cost reverts as the card lands in the pile.
        reset_cost_for_turn(s, pool_index);
        return true;
    }
    assert(s.discard_count < kDiscardCap &&
           "discard overflow (design doc §4.1: hard assert)");
    s.discard[s.discard_count] = pool_index;
    ++s.discard_count;
    return true;
}

void flush_limbo_at_combat_over(CombatState& s) noexcept {
    // Terminal normalization -- see piles.hpp. File front-to-back in limbo
    // insertion order. A synchronous replay may precede its source here
    // (Double Tap puts the copy into limbo during the source's UseCardAction
    // constructor), but purge copies land in no pile, so this preserves every
    // observable destination-pile order. Pending filing actions and queued
    // autoplay have already taken their exact path; this final fallback has no
    // RNG or hooks.
    while (s.limbo_count > 0) {
        const uint8_t pi = s.limbo[0];
        const CardDef* def = card_def(static_cast<CardId>(s.card_pool[pi].card_id));
        const bool remove_only =
            (def != nullptr && def->type == CardType::POWER) ||
            has_card_flag(s.card_pool[pi].flags, CardFlag::PURGE_ON_USE);
        const bool to_exhaust =
            has_card_flag(s.card_pool[pi].flags, CardFlag::EXHAUST) ||
            has_card_flag(s.card_pool[pi].flags,
                          CardFlag::EXHAUST_ON_USE_ONCE);
        file_card_from_limbo(s, pi, to_exhaust, remove_only);
        // The one-play bits die with the play. EXHAUST_ON_USE_ONCE follows
        // UseCardAction.java:132 and is consumed only on the filing path;
        // FREE_TO_PLAY_ONCE follows :87, which runs before every branch, so it
        // is consumed even for a card this normalization removes outright.
        s.card_pool[pi].flags = static_cast<uint16_t>(
            s.card_pool[pi].flags & ~card_flag_bit(CardFlag::FREE_TO_PLAY_ONCE));
        if (!remove_only) {
            s.card_pool[pi].flags = static_cast<uint16_t>(
                s.card_pool[pi].flags &
                ~card_flag_bit(CardFlag::EXHAUST_ON_USE_ONCE));
        }
    }
}

void discard_hand_at_end_of_turn(CombatState& s) noexcept {
    // DiscardAtEndOfTurnAction queues normal discards, then each ethereal hand
    // card prepends ExhaustSpecificCardAction. Exhaust therefore resolves first.
    // Scan right-to-left because exhaust_card removes from the compact hand.
    for (uint8_t i = s.hand_count; i > 0; --i) {
        const CardPoolIndex pi = s.hand[static_cast<uint8_t>(i - 1)];
        if (has_card_flag(s.card_pool[pi].flags, CardFlag::ETHEREAL)) {
            exhaust_card(s, pi);
        }
    }

    // Runic Pyramid (boss relic): the discard loop below is SKIPPED ENTIRELY.
    // DiscardAtEndOfTurnAction.java:35-40 guards the whole
    //     for (i < tempSize) addToTop(new DiscardAction(...))
    // block with `!player.hasRelic("Runic Pyramid") && !player.hasPower(
    // "Equilibrium")`. The Equilibrium half is Defect-only and has no registry
    // row, so only the relic clause is live.
    //
    // The ETHEREAL sweep above is deliberately OUTSIDE this guard, exactly as in
    // the Java: ethereal cards exhaust through triggerOnEndOfPlayerTurn (:41-45),
    // which runs unconditionally AFTER the guarded block. A Runic Pyramid
    // therefore keeps its hand but still loses its Ethereals; skipping both would
    // silently hand the player free Ethereals every turn.
    if (player_has_relic(s, RelicId::RUNIC_PYRAMID)) {
        return;
    }

    // DiscardAction repeatedly takes hand.getTopCard(), represented by the tail.
    // RETAIN cards are the cards DiscardAtEndOfTurnAction moves aside first.
    for (uint8_t i = s.hand_count; i > 0; --i) {
        const CardPoolIndex pi = s.hand[static_cast<uint8_t>(i - 1)];
        if (has_card_flag(s.card_pool[pi].flags, CardFlag::RETAIN)) {
            continue;
        }
        for (uint8_t j = i; j < s.hand_count; ++j) {
            s.hand[j - 1] = s.hand[j];
        }
        --s.hand_count;
        assert(s.discard_count < kDiscardCap &&
               "discard overflow (design doc pile capacity)");
        s.discard[s.discard_count] = pi;
        ++s.discard_count;
    }
}

}  // namespace sts::engine
