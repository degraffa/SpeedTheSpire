// Pile operations -- draw / reshuffle / exhaust. See piles.hpp for the full
// provenance, the hand-size-cap correction (decompiled Java > ledger prose), and
// the headless timing simplification rationale.
//
// Provenance: AbstractPlayer.draw(int/void) (AbstractPlayer.java:1632-1665),
// DrawCardAction.update (DrawCardAction.java:63-127), EmptyDeckShuffleAction.
// update (EmptyDeckShuffleAction.java:42-64), CardGroup.shuffle(Random)
// (CardGroup.java:565-567), Soul.shuffle (Soul.java:90-102). Reuses A1.2's
// golden-tested JdkRandom + jdk_shuffle and A1.3's random_long. Design doc §3.3,
// §9; §10 trap 2.

#include "sts/engine/piles.hpp"

#include <algorithm>
#include <cstdint>
#include <span>

#include "sts/engine/combat_state.hpp"
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

    // Hand-size cap, applied ONCE up front (DrawCardAction.java:92-97, corrected):
    // amount = min(amount, kHandCap - hand_count). Overflowing cards are simply
    // never drawn -- there is NO draw-then-discard. See piles.hpp's correction.
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
    // Relocated verbatim from A4.1's interp.cpp op_exhaust (behavior unchanged).
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
            return;
        }
    }
}

}  // namespace sts::engine
