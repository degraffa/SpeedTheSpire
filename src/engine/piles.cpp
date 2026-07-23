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
#include <cstdint>
#include <span>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // B3.2: onExhaust dispatch (EXHAUST opcode path)
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

void shuffle_discard_into_draw(CombatState& s) noexcept {
    if (s.discard_count == 0) {
        return;  // nothing to reshuffle -- no random_long drawn (matches the game)
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
            // §5.5 onExhaust (CardGroup.moveToExhaustPile): fires as the card lands
            // in the exhaust pile. No-op without an on-exhaust power, so the
            // skeleton EXHAUST-opcode path is unchanged.
            dispatch_on_exhaust(s, s.card_pool[idx].card_id);
            return;
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
