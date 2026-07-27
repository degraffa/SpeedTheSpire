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
#include "sts/engine/power_hooks.hpp"  // onExhaust dispatch (EXHAUST opcode path)
#include "sts/engine/relic_hooks.hpp"  // onShuffle dispatch (Sundial)
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

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
        return;  // nothing to reshuffle -- no random_long drawn (matches the game)
    }

    // Relics onShuffle (EmptyDeckShuffleAction ctor, EmptyDeckShuffleAction.java:
    // 37-39): fires as the reshuffle action is created -- i.e. BEFORE the shuffle
    // draw -- and only when a reshuffle actually happens (DrawCardAction only
    // constructs the action when the discard is non-empty). Sundial counts these
    // (Sundial); its GainEnergy is QUEUED (addToBot), so the shuffle/draw math
    // below and shuffle_rng consumption are untouched. No-op without a
    // responding relic (fixtures byte-identical).
    {
        const RelicView rv = player_relics(s);
        dispatch_relics_on_shuffle(s, rv.relics, rv.count);
    }

    // CardGroup.shuffle(shuffleRng): exactly one shuffleRng.randomLong() seeds a
    // fresh java.util.Random, whose LCG drives Collections.shuffle's Fisher-Yates
    // over the discard list (trap 2: JDK LCG, NOT xorshift128+). This is the ONLY
    // shuffle_rng draw here -- the Fisher-Yates swaps consume JdkRandom-internal
    // draws, never the RngStream -- so shuffle_rng.counter advances by exactly 1.
    const int64_t seed = random_long(s.shuffle_rng);
    JdkRandom rng(seed);
    jdk_shuffle(std::span<CardPoolIndex>(s.discard, s.discard_count), rng);

    // EmptyDeckShuffleAction walks the shuffled discard front-to-back, moving each
    // card to the draw pile via Soul.shuffle -> drawPile.addToTop (append to the
    // END of the list). The draw pile is empty at this point, so its list becomes
    // the shuffled discard array in the SAME order; getTopCard (== last element)
    // is drawn first. Our array convention mirrors that list (draw[draw_count-1]
    // == top), so a straight std::copy onto the draw[] tail reproduces it exactly.
    int n = s.discard_count;
    if (s.draw_count + n > kDrawCap) {
        n = kDrawCap - s.draw_count;  // defensive clamp; unreachable in the skeleton
    }
    std::copy(s.discard, s.discard + n, s.draw + s.draw_count);
    s.draw_count = static_cast<uint8_t>(s.draw_count + n);
    s.discard_count = 0;
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
