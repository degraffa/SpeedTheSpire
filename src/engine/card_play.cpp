// Card-play flow -- PLAY_CARD -> cardQueue -> dequeue resolution. See
// card_play.hpp for the full provenance, the hook-order collapse decision, and
// the preserved timing facts (effects are QUEUED not applied inline; energy
// deducted after; random-target roll at dequeue).
//
// Provenance: GameActionManager.getNextAction cardQueue branch
// (GameActionManager.java:193-280), AbstractPlayer.useCard (:1358-1384),
// UseCardAction (whole file), getRandomMonster(cardRandomRng). Design doc §5.3,
// §6, §9, §10 trap 10.

#include "sts/engine/card_play.hpp"

#include <cassert>
#include <cstdint>

#include "sts/engine/action_queue.hpp"  // add_to_bottom, add_card_to_queue_bottom, kActorPlayer
#include "sts/engine/cards.hpp"         // CardDef, card_def
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode
#include "sts/engine/rng_stream.hpp"    // random (card_random_rng roll)
#include "sts/engine/types.hpp"         // CardId

namespace sts::engine {

namespace {

// The card-play hook fan-out (design doc §5.3; AbstractPlayer.useCard:1358-1372).
// Every listener is a no-op in the M1 skeleton (zero relics/powers/blights/
// stances with an onPlayCard body). Present as a documented stub so a future
// stage attaches real listeners here without moving the call site -- and in the
// exact acquisition/pile order the game triggers them.
void trigger_on_play_card(CombatState& /*s*/, const CardDef& /*def*/,
                          uint8_t /*target*/) noexcept {
    // player powers onPlayCard -> each monster's powers onPlayCard -> relics
    // onPlayCard (acquisition order) -> stance -> blights -> hand cards
    // onPlayCard -> discard pile cards -> draw pile cards. All no-ops (skeleton).
}

// UseCardAction's onUseCard/onAfterUseCard hook stage (UseCardAction.java). Also
// entirely no-op in the skeleton; kept as a named site for the same reason.
void trigger_on_use_card(CombatState& /*s*/, const CardDef& /*def*/) noexcept {
    // onUseCard -> onAfterUseCard listeners. All no-ops (skeleton).
}

// Instantiate one registry effect step into a concrete ActionQueueItem and queue
// it via add_to_bottom (exactly how AbstractCard.use() calls addToBot(...)). The
// registry stores src-independent templates; src is always the player for the
// skeleton's five cards (all player-owned effects), and tgt is substituted here:
// kActorPlayer for SELF steps, the resolved monster slot for CARD_TARGET steps.
void queue_effect_step(CombatState& s, const CardEffectStep& step,
                       uint8_t resolved_target) noexcept {
    ActionQueueItem item{};
    item.opcode = static_cast<uint16_t>(step.op);
    item.src = kActorPlayer;
    item.tgt = (step.target == StepTarget::SELF) ? kActorPlayer : resolved_target;
    item.amount = step.amount;
    item.flags = step.extra;  // APPLY_POWER: PowerId flags; else 0
    add_to_bottom(s, item);
}

// Move the played card (pool index) from hand to the discard pile. None of the
// five skeleton cards exhausts/reshuffles/rebounds, so the destination is always
// discard (AbstractPlayer.useCard -> moveToDiscardPile equivalent). Locating by
// pool index is unambiguous (each pool row is a distinct instance).
void move_card_hand_to_discard(CombatState& s, CardPoolIndex pool_index) noexcept {
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        if (s.hand[i] == pool_index) {
            for (uint8_t j = static_cast<uint8_t>(i + 1); j < s.hand_count; ++j) {
                s.hand[j - 1] = s.hand[j];
            }
            --s.hand_count;
            assert(s.discard_count < kDiscardCap &&
                   "discard overflow (design doc §4.1: hard assert)");
            s.discard[s.discard_count] = pool_index;
            ++s.discard_count;
            return;
        }
    }
    // Not found in hand: a malformed play. Leave state untouched (defensive; the
    // caller/queue_card_play only ever queues a card that was in hand).
}

}  // namespace

// --- Public ------------------------------------------------------------------

bool queue_card_play(CombatState& s, uint8_t hand_index, uint8_t target) noexcept {
    if (hand_index >= s.hand_count) {
        return false;
    }
    CardQueueItem item{};
    item.card_index = s.hand[hand_index];  // the played card's pool index
    item.target = target;
    add_card_to_queue_bottom(s, item);     // normal (non-priority) enqueue
    // NOTE (trap 10): no card_random_rng draw happens here -- the random-target
    // roll is a DEQUEUE-time operation (resolve_play_target), not enqueue-time.
    return true;
}

uint8_t roll_random_target(CombatState& s) noexcept {
    // getRandomMonster(null, true, cardRandomRng): pick uniformly among the LIVE
    // monsters via one cardRandomRng draw over [0, aliveCount-1] (inclusive).
    uint8_t alive[kMonsterCap];
    int n = 0;
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        if (s.monsters[i].hp > 0) {
            alive[n++] = i;
        }
    }
    if (n == 0) {
        return kActorPlayer;  // no live target (pump ends combat before this)
    }
    const int32_t pick = random(s.card_random_rng, n - 1);  // one draw, inclusive
    return alive[pick];
}

uint8_t resolve_play_target(CombatState& s, const CardDef& def,
                            uint8_t declared_target) noexcept {
    // TRAP 10 dispatch point: random-target cards roll at DEQUEUE. None of the
    // five skeleton cards sets random_target, so the else-branch is what M1
    // exercises; the roll path is covered by the trap-10 test.
    if (def.random_target) {
        return roll_random_target(s);
    }
    return declared_target;
}

void resolve_card_play(CombatState& s, const CardQueueItem& item) noexcept {
    const CardPoolIndex pool_index = item.card_index;
    const CardId card_id = static_cast<CardId>(s.card_pool[pool_index].card_id);
    const CardDef* def = card_def(card_id);
    assert(def != nullptr && "resolve_card_play on a non-registry card");
    if (def == nullptr) {
        return;
    }

    // 1. onPlayCard hook fan-out (no-op stubs; declared target for the hooks).
    trigger_on_play_card(s, *def, item.target);

    // 2. ++cardsPlayedThisTurn (AbstractPlayer.useCard:1373).
    ++s.cards_played_this_turn;

    // 3. Resolve the effect target (trap-10 random roll at dequeue, else declared).
    const uint8_t resolved_target = resolve_play_target(s, *def, item.target);

    // 4. c.use(): QUEUE the card's effect actions via add_to_bottom, in the
    //    card's addToBot order. Effects resolve later through the pump priority
    //    loop -- they are NOT applied inline here (matches AbstractCard.use()).
    for (uint8_t k = 0; k < def->step_count; ++k) {
        queue_effect_step(s, def->steps[k], resolved_target);
    }

    // 5. UseCardAction hook stage (no-op) + move the card hand->discard.
    trigger_on_use_card(s, *def);
    move_card_hand_to_discard(s, pool_index);

    // 6. energy.use(cost): deducted AFTER the effects are queued (useCard order).
    //    The per-instance runtime cost is card_pool[...].cost_now (not the
    //    registry base cost), so a future cost-reduction effect is honored.
    s.player_energy = static_cast<int16_t>(
        s.player_energy - static_cast<int16_t>(s.card_pool[pool_index].cost_now));
}

}  // namespace sts::engine
