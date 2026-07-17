#pragma once

// Pile operations on CombatState -- draw / reshuffle / exhaust (design doc §3.3,
// §9). This is A4.2: the layer A4.1 deferred. A4.1's DRAW opcode was a minimal
// non-empty/non-overflow slice and its SHUFFLE_IN was a bare stub; this module
// implements the real composite behaviors and interp.cpp's dispatch now delegates
// DRAW/SHUFFLE_IN/EXHAUST here.
//
// Provenance (read from D:\STS_BG_Mod\SlayTheSpireDecompiled, verified for A4.2):
//   * AbstractPlayer.draw(int) (AbstractPlayer.java:1632-1655) -- move the top
//     card of drawPile to hand, `numCards` times; the empty-drawPile branch only
//     logs (the caller guarantees non-empty). AbstractPlayer.draw()
//     (:1657-1665) refuses entirely when hand.size() == 10.
//   * DrawCardAction.update() (DrawCardAction.java:63-127) -- the composite that
//     drives draw(): the ONE-TIME up-front hand-cap and the deck-exhaustion
//     reshuffle re-queue. See the hand-cap correction note below.
//   * EmptyDeckShuffleAction.update() (EmptyDeckShuffleAction.java:42-64) --
//     `discardPile.shuffle(shuffleRng)` then walk the shuffled discard
//     front-to-back moving each card to the draw pile via Soul.shuffle.
//   * CardGroup.shuffle(Random) (CardGroup.java:565-567) --
//     `Collections.shuffle(group, new java.util.Random(rng.randomLong()))`:
//     EXACTLY one shuffleRng.randomLong() draw, then a JDK-LCG-seeded Fisher-Yates
//     over the discard list. Reuses A1.2's already-golden-tested JdkRandom +
//     jdk_shuffle (rng_jdk.hpp), not a reimplementation. Trap 2 (§10): the
//     shuffle routes through java.util.Random's LCG, NOT xorshift128+.
//   * Soul.shuffle(card, isInvisible) (Soul.java:90-102) -- `this.group =
//     drawPile; this.group.addToTop(card)`, i.e. append to the END of the draw
//     pile list (CardGroup.addToTop == group.add, :455-457).
//
// -------------------------------------------------------------------------
// HAND-SIZE-CAP CORRECTION (stop-the-line; decompiled Java > the ledger's A4.2
// deliverable prose, per the working-agreements precedence rule). The ledger's
// A4.2 line reads "hand-size-10 overflow rule (drawn card goes to discard)".
// That is NOT what the game does. DrawCardAction.update() computes, ONCE up front
// (the `!shuffleCheck` branch, :92-97), before any card is drawn:
//     if (amount + hand.size() > 10) { amount += 10 - (amount + hand.size()); }
// which is algebraically `amount = 10 - hand.size()` -- i.e. the draw amount is
// CAPPED so overflowing cards are simply never drawn. There is no draw-then-
// discard. (And AbstractPlayer.draw() refuses outright when hand.size()==10, a
// degenerate case of the same formula.) So the real rule is:
//     amount = min(amount, kHandCap - hand_count), applied once, before drawing.
// Recorded in the ledger Log and the design-doc change log.
//
// TIMING/ARCHITECTURE SIMPLIFICATION (justified). The real game spreads the
// reshuffle-then-continue-drawing across several animated AbstractGameActions:
// DrawCardAction draws what is left of the deck, then addToTop(new
// EmptyDeckShuffleAction()), then addToTop(new DrawCardAction(remaining)) -- the
// splitting exists only for frame-by-frame visual pacing. A headless engine has
// no animation, so draw_cards() collapses this into ONE synchronous call (draw a
// card; if the draw pile empties with cards still owed, reshuffle in place and
// keep going). The observable result is identical -- same final pile contents,
// same RNG draws consumed in the same order and count -- without re-entering the
// action queue.

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Reshuffle the discard pile into the draw pile (EmptyDeckShuffleAction +
// CardGroup.shuffle + Soul.shuffle). Draws EXACTLY one random_long from
// shuffle_rng (advancing its counter by 1) to seed a JdkRandom, Fisher-Yates
// shuffles the discard pool-index array, appends the shuffled array onto the tail
// of draw[] (so the last shuffled element becomes the top / drawn-first card, per
// the game's addToTop-to-END convention paired with our draw[draw_count-1]==top
// convention), then clears the discard pile. Empty discard pile is a no-op (no
// random_long is drawn, matching the game where an empty discard shuffles to
// nothing -- though in practice draw_cards only calls this with a non-empty
// discard). Overflowing kDrawCap is a defensive silent clamp (never reached in
// the skeleton: hand+draw+discard <= the master deck size).
void shuffle_discard_into_draw(CombatState& state) noexcept;

// Draw `amount` cards from the top of the draw pile into the hand, reshuffling
// the discard pile in when the draw pile empties (AbstractPlayer.draw() +
// DrawCardAction). Semantics, in order:
//   (1) if hand is already full (hand_count == kHandCap) draw nothing, return 0
//       (AbstractPlayer.draw() early-out);
//   (2) cap amount = min(amount, kHandCap - hand_count) ONCE, up front (the
//       corrected hand-size rule above -- overflow cards are never drawn);
//   (3) draw one card at a time; if the draw pile is empty, reshuffle the discard
//       in and continue, but if BOTH piles are empty stop early (the game's
//       deckSize+discardSize==0 guard).
// Returns the number of cards actually drawn (<= the capped amount). A non-
// positive `amount` draws nothing.
int draw_cards(CombatState& state, int amount) noexcept;

// Move the card whose pool index == `pool_index` from hand to the exhaust pile
// (relocated verbatim from A4.1's interp.cpp op_exhaust so all pile mutations
// live in one module). Not-in-hand is a documented no-op. Out-of-range index
// (< 0 or > 0xFF) is a no-op. None of the five skeleton cards exhausts by
// default, so this is exercised directly by tests rather than by card play at M1.
void exhaust_card(CombatState& state, int pool_index) noexcept;

}  // namespace sts::engine
