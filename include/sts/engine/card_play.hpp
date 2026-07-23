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
//   * AbstractPlayer.useCard (AbstractPlayer.java:1358-1384) -- the onPlayCard
//     hook fan-out (player powers -> monster powers -> relics -> stance ->
//     blights -> hand/discard/draw cards), ++cardsPlayedThisTurn, then
//     c.use(player, target), then move the card to its destination pile, then
//     energy.use(cost).
//   * UseCardAction (UseCardAction.java, whole file) -- the separate queued
//     action that runs onUseCard/onAfterUseCard and moves the card to discard.
//   * getRandomMonster(cardRandomRng) (AbstractDungeon / MonsterGroup) -- the
//     TRAP 10 random-target roll, evaluated at DEQUEUE time.
//
// HOOK-ORDER COLLAPSE (documented decision). The real game splits card
// resolution across TWO pump cycles purely for
// animation pacing: useCard() runs the onPlayCard fan-out + ++cardsPlayedThisTurn
// + c.use() (which QUEUES the card's effect actions via addToBot) + energy.use();
// then a SEPARATELY queued UseCardAction later runs onUseCard/onAfterUseCard and
// the duration-timed hand->discard move. Our headless engine has no animation,
// and the skeleton has ZERO relics/powers/blights/stances with a real
// onPlayCard/onUseCard/onAfterUseCard body -- every hook in the fan-out is a
// no-op. So resolve_card_play() collapses both cycles into ONE synchronous step
// when the cardQueue head is dequeued. This yields the identical observable end
// state and the identical RNG draw sequence as the multi-cycle version (verified
// against the source: the only cross-cycle interaction would be a listener with
// a real body, of which the skeleton has none). The hook sites are present as
// documented no-op stubs so Stage B attaches real listeners without moving the
// call sites (the §5.5-style extension-point convention).
//
// KEY TIMING FACTS preserved (matching the Java, not simplified away):
//   * A card's use() does NOT apply its effects inline -- it QUEUES them via
//     add_to_bottom onto the action_queue; they resolve later through the
//     normal pump priority order, exactly like AbstractCard.use()'s addToBot.
//   * Energy is deducted AFTER the effects are queued (useCard order), not before.
//   * The random-target roll (trap 10) happens at DEQUEUE (resolve), never at
//     enqueue (queue_card_play) -- see resolve_play_target / roll_random_target.

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

// Resolve one dequeued card play (design doc §5.3, collapsed per the note above).
// Called from pump_step()'s step 3 when the cardQueue head is a real card (not
// the end-turn sentinel). Runs the no-op hook stubs, ++cards_played_this_turn,
// the trap-10 target resolution, then queues the card's effect ActionQueueItems
// via add_to_bottom. The upgrade-selected effect program (Stage B B3.1 two-row
// lookup) runs once, or -- for an X-cost card -- energyOnUse times with energy
// then zeroed. The card moves hand->exhaust (EXHAUST flag) or hand->discard, and
// non-X cost is deducted from player_energy. `item.card_index` is the card-pool
// index of the played card.
void resolve_card_play(CombatState& state, const CardQueueItem& item) noexcept;

}  // namespace sts::engine
