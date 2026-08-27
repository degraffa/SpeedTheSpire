#pragma once

// Pile operations on CombatState -- draw / reshuffle / exhaust (design doc §3.3,
// §9). This module implements the composite draw/reshuffle/exhaust behaviors;
// interp.cpp's dispatch delegates the DRAW/SHUFFLE_IN/EXHAUST opcodes here.
//
// Provenance (read from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * AbstractPlayer.draw(int) (AbstractPlayer.java:1632-1655) -- move the top
//     card of drawPile to hand, `numCards` times; the empty-drawPile branch only
//     logs (the caller guarantees non-empty). AbstractPlayer.draw()
//     (:1657-1665) refuses entirely when hand.size() == 10.
//   * DrawCardAction.update() (DrawCardAction.java:63-127) -- the composite that
//     drives draw(): the ONE-TIME up-front hand-cap and the deck-exhaustion
//     reshuffle re-queue. See the hand-cap note below.
//   * EmptyDeckShuffleAction.update() (EmptyDeckShuffleAction.java:42-64) --
//     `discardPile.shuffle(shuffleRng)` then walk the shuffled discard
//     front-to-back moving each card to the draw pile via Soul.shuffle.
//   * CardGroup.shuffle(Random) (CardGroup.java:565-567) --
//     `Collections.shuffle(group, new java.util.Random(rng.randomLong()))`:
//     EXACTLY one shuffleRng.randomLong() draw, then a JDK-LCG-seeded Fisher-Yates
//     over the discard list. Reuses the already-golden-tested JdkRandom +
//     jdk_shuffle (rng_jdk.hpp), not a reimplementation. Trap 2 (§10): the
//     shuffle routes through java.util.Random's LCG, NOT xorshift128+.
//   * Soul.shuffle(card, isInvisible) (Soul.java:90-102) -- `this.group =
//     drawPile; this.group.addToTop(card)`, i.e. append to the END of the draw
//     pile list (CardGroup.addToTop == group.add, :455-457).
//
// -------------------------------------------------------------------------
// HAND-SIZE CAP (non-obvious rule). There is no draw-then-discard on hand
// overflow. DrawCardAction.update() computes, ONCE up front (the `!shuffleCheck`
// branch, DrawCardAction.java:92-97), before any card is drawn:
//     if (amount + hand.size() > 10) { amount += 10 - (amount + hand.size()); }
// which is algebraically `amount = 10 - hand.size()` -- i.e. the draw amount is
// CAPPED so overflowing cards are simply never drawn. (And AbstractPlayer.draw()
// refuses outright when hand.size()==10, a degenerate case of the same formula.)
// So the real rule is:
//     amount = min(amount, kHandCap - hand_count), applied once, before drawing.
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
// convention), then clears the discard pile. This public entry point represents
// the authored sites that predicate construction on a non-empty discard, so an
// empty discard is a no-op. DrawCardAction's distinct recursive-overdraw case
// can construct an EmptyDeckShuffleAction over an empty discard anyway; that
// action still fires onShuffle and consumes one random_long, and draw_cards
// models it internally. Overflowing kDrawCap is a defensive silent clamp (never
// reached in the skeleton: hand+draw+discard <= the master deck size).
void shuffle_discard_into_draw(CombatState& state) noexcept;

// The RESHUFFLE_ALL opcode's body (Deep Breath). DeepBreath.use (DeepBreath.java:
// 34-38) guards BOTH of its actions on the SAME `discardPile.size() > 0` test, so
// the pair consumes TWO shuffle_rng draws or none:
//   (1) EmptyDeckShuffleAction -- as shuffle_discard_into_draw above (one
//       shuffle_rng draw, plus the relics' onShuffle its ctor fires);
//   (2) ShuffleAction(drawPile, false) -- one MORE shuffle_rng draw seeding a
//       fresh JdkRandom over the WHOLE draw pile (CardGroup.shuffle():561-563).
//       Its triggerRelics argument is false, so there is NO second onShuffle pass.
// It is one function rather than two authored steps because (1) empties the
// discard: once it has run, nothing can still observe the guard's input. An empty
// discard pile draws nothing at all and leaves the draw pile untouched -- the game
// skips both actions together, never just the second.
//
// The played Deep Breath itself needs no special handling here: throughout this
// action it sits in the `limbo` pile (AbstractPlayer.useCard:1369-1375 queues
// the card's own actions FIRST and the UseCardAction that files it LAST), so it
// is simply not in the discard this shuffles. The former `exclude` parameter --
// a local compensation for the early discard move -- is folded into that
// general model (interp.hpp USE_CARD).
void reshuffle_all(CombatState& state) noexcept;

// Draw `amount` cards from the top of the draw pile into the hand, reshuffling
// the discard pile in when the draw pile empties (AbstractPlayer.draw() +
// DrawCardAction). Semantics, in order:
//   (1) if hand is already full (hand_count == kHandCap) draw nothing, return 0
//       (AbstractPlayer.draw() early-out);
//   (2) cap amount = min(amount, kHandCap - hand_count) ONCE, up front (the
//       hand-size rule above -- overflow cards are never drawn);
//   (3) draw one card at a time; if the draw pile is empty, reshuffle the discard
//       in and continue, but if BOTH piles are empty stop early (the game's
//       deckSize+discardSize==0 guard);
//   (4) when the capped request began with at least one card but exceeds the
//       total draw+discard population, reproduce the recursive DrawCardAction's
//       final EmptyDeckShuffleAction over an empty discard: fire onShuffle and
//       consume one shuffle_rng draw even though no card moves.
// Returns the number of cards actually drawn (<= the capped amount). A non-
// positive `amount` draws nothing.
int draw_cards(CombatState& state, int amount) noexcept;

// Move the card whose pool index == `pool_index` from hand to the exhaust pile
// (all pile mutations live in this one module). Not-in-hand is a documented
// no-op. Out-of-range index
// (< 0 or > 0xFF) is a no-op. None of the five skeleton cards exhausts by
// default, so this is exercised directly by tests rather than by card play at M1.
void exhaust_card(CombatState& state, int pool_index) noexcept;

// Exhaust ETHEREAL hand cards, then discard all non-RETAIN hand cards. This
// models the state result of DiscardAtEndOfTurnAction after the sentinel-path
// card effects and atEndOfTurn powers have drained, so Regret sees the full hand
// before this sweep. The animation-only ClearCardQueue/DiscardAction chain is
// intentionally collapsed without changing pile outcome or trigger ordering.
void discard_hand_at_end_of_turn(CombatState& state) noexcept;

// --- The limbo pile (cardInUse) ----------------------------------------------
// AbstractPlayer.useCard removes the played card from the hand and holds it as
// `cardInUse` (:1373-1375) until the queued UseCardAction files it away; an
// AUTOPLAYED card (Havoc / Double Tap) sits in the game's limbo CardGroup from
// the moment it leaves its source pile. Both are the CombatState `limbo` pile
// (design stage-a §4.2 budgets it, cap 8): membership there means "in no
// observable pile". Nesting (Havoc's target card queued while Havoc itself is
// still in limbo) stacks entries; kLimboCap overflow is a hard assert (§4.1).

// Append pool row `pool_index` to the limbo pile.
void limbo_add(CombatState& state, uint8_t pool_index) noexcept;

// Remove pool row `pool_index` from the limbo pile (by value -- nested plays
// resolve inner-first, but removal must not assume stack order). Returns true
// if it was present.
bool limbo_remove(CombatState& state, uint8_t pool_index) noexcept;

// File the card `pool_index` OUT of the limbo pile to its destination:
//   * remove_only (purgeOnUse poof, UseCardAction.java:89-94; POWER empower,
//     :95-108) -- the card leaves limbo and lands in NO pile (the pool row stays
//     as inert instance storage, exactly as the pre-limbo model kept it);
//   * to_exhaust -- exhaust pile append + resetAttributes cost revert
//     (ExhaustCardEffect.update:41-43); the caller decides whether onExhaust
//     hooks fire (they do in-combat; the COMBAT_OVER flush below does not);
//   * otherwise -- discard pile append (moveToDiscardPile, :127).
// A card not in limbo is a defensive no-op (returns false).
bool file_card_from_limbo(CombatState& state, uint8_t pool_index,
                          bool to_exhaust, bool remove_only) noexcept;

// COMBAT_OVER normalization: file every card still in limbo to its
// flag/type-derived destination, WITHOUT Strange Spoon rolls or onExhaust
// hooks. The pump halts the moment the fight is decided, abandoning whatever
// is still queued exactly as it already abandons a lethal card's trailing
// effects. Pending USE_CARD actions and terminal-cancelled queued autoplays are
// resolved exactly (including Spoon and onExhaust) by action_queue.cpp before
// this fallback runs. This helper handles only otherwise-stranded limbo rows
// and consumes their EXHAUST_ON_USE_ONCE state. The fixture corpus pins the
// ordinary lethal-Strike shape (discard, empty limbo).
void flush_limbo_at_combat_over(CombatState& state) noexcept;

// If pool row `pool_index` carries the COST_MODIFIED_FOR_TURN bit
// (setCostForTurn -- Infernal Blade's generated attack, Mummified Hand's cost-0
// pick), restore cost_now to the instance's combat BASE cost -- the
// SAVED_BASE_COST payload when a permanent writer has moved it, otherwise the
// registry cost for its upgrade level -- and clear the bit (AbstractCard.
// resetAttributes:2035-2045, `costForTurn = cost`). A row without the bit is
// untouched: Blood for Blood's and Corruption's combat-persistent reductions,
// and Confusion's roll, all write `cost` itself in the game and so SURVIVE
// resetAttributes. cost_now is the only field resetAttributes has to restore
// here -- block/damage/magicNumber are recomputed from the registry row at play
// time and damageTypeForTurn has no per-instance storage.
//
// WHERE IT FIRES. Three seams, not one:
//   1. the end-turn sweep, per card of draw/discard/hand
//      (AbstractRoom.endTurn:397-405);
//   2. exhaust (ExhaustCardEffect.update:41-43);
//   3. EVERY MOVE INTO THE DRAW OR DISCARD PILE, mid-turn included.
// (3) is `Soul.update` (Soul.java:193-231): a card handed to a Soul lands
// through the switch on its destination CardGroup's type, and the DRAW_PILE
// (case 2, :205-212) and DISCARD_PILE (case 3, :213-221) arms both call
// `clearPowers()` -> `resetAttributes()`. The switch-map class Soul$2 pins the
// labels: MASTER_DECK 1, DRAW_PILE 2, DISCARD_PILE 3, EXHAUST_PILE 4 -- so the
// master deck and the (Soul-less) exhaust pile get NO reset from this seam.
// The Soul-routed movers are `CardGroup.moveToDiscardPile` (:836-841),
// `moveToDeck` (:892-896), `moveToBottomOfDeck` (:898-902) and
// EmptyDeckShuffleAction's per-card `souls.shuffle`
// (EmptyDeckShuffleAction.java:55-58). `moveToHand` (:864-890) is NOT one of
// them: it calls applyPowers, never clearPowers.
//
// NOT every append to those piles goes through a Soul: the MakeTempCard*
// actions add through `ShowCardAndAddToDiscardEffect` / `ShowCardAndAddTo-
// DrawPileEffect`, which call `discardPile.addToTop` / `drawPile.addToTop|
// addToBottom|addToRandomSpot` directly (ShowCardAndAddToDiscardEffect.java:37,
// ShowCardAndAddToDrawPileEffect.java:46-50). Those sites therefore do NOT
// reset -- which is unobservable for the fresh copies they create but is the
// reason this is wired per-site rather than at the array append.
//
// The Java's reset is animation-deferred (the Soul reaches its pile a beat
// later), and the live capture shows exactly that one-record lag -- STS101166
// floor 20 has `Bash+(cost 0)` in the discard at seq 330 and `Bash+(cost 2)` at
// seq 331. The headless model collapses the animation, which is exact at every
// action boundary: `Soul.isCarryingCard` (:233-246) returning false only
// short-circuits to `isDone`, and the destination switch -- hence clearPowers --
// still runs, so the reset lands even when the card has already been drawn out
// again.
void reset_cost_for_turn(CombatState& state, uint8_t pool_index) noexcept;

}  // namespace sts::engine
