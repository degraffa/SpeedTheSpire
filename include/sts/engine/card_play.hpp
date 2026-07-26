#pragma once

// Card-play flow -- the PLAY_CARD action -> cardQueue -> dequeue-resolution path
// (design doc §5.3). Ties the effect interpreter (interp.hpp), the pile ops
// (piles.hpp), and the action-queue pump (action_queue.hpp) together into an
// actually-playable card game.
//
// Provenance (read from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * GameActionManager.getNextAction, cardQueue branch (GameActionManager.java
//     :193-280) -- a queued card is DEQUEUED and resolved here, not when the
//     player chose it.
//   * GameActionManager.getNextAction (:214-249) -- the onPlayCard hook fan-out
//     (player powers -> monster powers -> relics -> stance -> blights ->
//     hand/discard/draw cards) and ++cardsPlayedThisTurn before useCard.
//   * AbstractPlayer.useCard (AbstractPlayer.java:1358-1384) -- calculate/use,
//     queue UseCardAction, notify the other hand cards, then remove the played
//     card into cardInUse limbo and finally energy.use(cost).
//   * UseCardAction (UseCardAction.java, whole file) -- the separate queued
//     action that runs onUseCard/onAfterUseCard and moves the card to discard.
//   * getRandomMonster(cardRandomRng) (AbstractDungeon / MonsterGroup) -- the
//     TRAP 10 random-target roll, evaluated at DEQUEUE time.
//
// THE LIMBO WINDOW (the collapse that used to live here is REVERSED). The game
// splits card resolution across two pump cycles, and that split is OBSERVABLE,
// not animation pacing: useCard() runs the onPlayCard fan-out +
// ++cardsPlayedThisTurn + c.use() (which QUEUES the card's effect actions via
// addToBot) + the UseCardAction ctor's onUseCard fan-out + queues the
// UseCardAction LAST + hand.removeCard + cardInUse = c + energy.use(). From
// removeCard until UseCardAction.update resolves, the played card is in NO
// pile, while every action the card queued resolves inside that window. An
// early collapse (file the card at resolve time) was correct for the original
// hookless skeleton but wrong the moment anything in the window observed a
// pile: the reshuffle a card's own draw triggers (Shrug It Off), the discard a
// card's own program appends to (Anger), a discard/exhaust grid select
// (Headbutt/Exhume), the Strange Spoon roll's cardRandomRng position, the
// played card's own onExhaust timing (Dark Embrace). Three per-site
// compensations had accumulated; the model now reproduces the window itself:
// resolve_card_play moves the card to the LIMBO pile (CombatState.limbo -- the
// game's cardInUse / limbo CardGroup) and queues a USE_CARD action
// (UseCardAction.update: spoon roll + filing) at UseCardAction's exact queue
// position (interp_cards.cpp op_use_card).
//
// KEY TIMING FACTS preserved (matching the Java, not simplified away):
//   * A card's use() does NOT apply its effects inline -- it QUEUES them via
//     add_to_bottom onto the action_queue; they resolve later through the
//     normal pump priority order, exactly like AbstractCard.use()'s addToBot.
//   * The onPlayCard fan-out and the UseCardAction CTOR's onUseCard fan-out run
//     synchronously at resolve time (both run inside useCard in the game, with
//     the card still in the hand); only UseCardAction.UPDATE -- spoon + filing
//     -- is deferred, as the queued USE_CARD.
//   * Energy is deducted AFTER the effects are queued (useCard order), not before.
//   * The random-target roll (trap 10) happens at DEQUEUE (resolve), never at
//     enqueue (queue_card_play) -- see resolve_play_target / roll_random_target.
//   * The dequeue path re-runs Java's canUse gate BEFORE every hook. If a
//     selected enemy died while a targeted autoplay waited in the queue, the
//     play is not retargeted and runs no hooks/effects/counters/energy spend;
//     only a no-trigger USE_CARD files the already-limbo autoplay instance
//     (GameActionManager.java:209-214,285-301; AbstractCard.java:854-859).

#include <cstdint>

#include "sts/engine/cards.hpp"         // CardDef
#include "sts/engine/combat_state.hpp"  // CombatState, CardQueueItem

namespace sts::engine {

// PLAY_CARD entry point: the player chooses to play hand[hand_index] at `target`
// (a monster slot; ignored for self-target Skills). This ONLY enqueues the play
// onto the cardQueue (via add_card_to_queue_bottom, the normal non-priority
// path) -- it resolves nothing, matching the game's enqueue-now / dequeue-later
// timing. Returns false (and enqueues nothing) if hand_index is out of range.
//
// The legality/affordability gate lives in legal_actions (advance.hpp); this
// entry point assumes the caller only requests legal plays and the actual cost
// check lives at resolution time (the real canPlayCard gate is evaluated at
// dequeue).
bool queue_card_play(CombatState& state, uint8_t hand_index, uint8_t target) noexcept;

// The dequeue-time random-target roll (TRAP 10): getRandomMonster(cardRandomRng).
// Rolls one card_random_rng draw to pick uniformly among the LIVE monsters
// (hp > 0), excluding dead ones, and returns that monster's slot. Returns
// kActorPlayer if no monster is alive (degenerate; the pump ends combat first).
// Public so the trap-10 test can prove the roll consumes exactly one draw and
// excludes dead monsters. Consumes a card_random_rng draw; call ONLY at dequeue.
[[nodiscard]] uint8_t roll_random_target(CombatState& state) noexcept;

// Resolve a card's play target at DEQUEUE time (trap 10 dispatch point): for a
// random-target card this rolls roll_random_target(); otherwise it returns the
// player-declared target unchanged. Public so the trap-10 test can exercise the
// dequeue-time roll directly and contrast it with queue_card_play (which never
// touches card_random_rng).
[[nodiscard]] uint8_t resolve_play_target(CombatState& state, const CardDef& def,
                                          uint8_t declared_target) noexcept;

// Resolve one dequeued card play (design doc §5.3; the limbo model above).
// Called from pump_step()'s step 3 when the cardQueue head is a real card (not
// the end-turn sentinel). Resolves trap-10 targeting, applies the dead-selected-
// target canUse cancellation above, then runs the hook fan-outs,
// ++cards_played_this_turn, and queues the card's effect ActionQueueItems via
// add_to_bottom -- the upgrade-selected effect program (the two-row lookup)
// once, or, for an X-cost card, energyOnUse times with energy then zeroed --
// then queues the USE_CARD filing action, moves the card hand -> LIMBO, and
// deducts the non-X cost from player_energy. The card reaches its destination
// pile (exhaust / discard / nowhere for POWER + purge) only when the queued
// USE_CARD executes. `item.card_index` is the card-pool index of the played
// card; an AUTOPLAYED card (Havoc / Double Tap) is already in limbo when this
// runs.
void resolve_card_play(CombatState& state, const CardQueueItem& item) noexcept;

// --- Card-level passive triggers (statuses / curses) -------------------------
// Status/curse cards whose CardDef.trigger != ON_PLAY run their effect program at
// a passive hook rather than on play (they are unplayable). Both are no-ops when
// no such card is at the relevant site, so the skeleton fixtures are unchanged.

// c.triggerWhenDrawn() for the card just drawn into hand at pool index
// `pool_index` (AbstractPlayer.draw:1642). Void (ON_DRAW) queues GAIN_ENERGY -1.
// Called by the DRAW opcode per newly-drawn card, before the power onCardDraw
// fan-out (interp.cpp).
void dispatch_card_on_draw(CombatState& state, uint8_t pool_index) noexcept;

// The §5.4 hand-card end-of-turn stage: each hand card with trigger END_OF_TURN
// (Burn/Decay/Doubt/Regret/Shame) queues its self-effect via
// triggerOnEndOfTurnForPlayingCard. The DiscardAtEndOfTurnAction pile sweep is
// queued separately after at-end-of-turn powers, so every trigger sees the full
// hand before ethereal cards exhaust and normal cards discard.
void dispatch_card_end_of_turn(CombatState& state) noexcept;

}  // namespace sts::engine
