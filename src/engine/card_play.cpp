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
#include "sts/engine/power_hooks.hpp"   // B3.2: onPlayCard/onUseCard fan-out + onExhaust
#include "sts/engine/rng_stream.hpp"    // random (card_random_rng roll)
#include "sts/engine/types.hpp"         // CardId

namespace sts::engine {

namespace {

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
    // Where the step lands. SELF -> the player; CARD_TARGET -> the card's
    // resolved monster (declared or the card-level random roll); ALL_ENEMY /
    // RANDOM_ENEMY -> the execute-time fan-out / per-hit-roll sentinels (Stage B
    // B3.1; execute_opcode resolves them against the then-live monsters).
    switch (step.target) {
        case StepTarget::SELF:
            item.tgt = kActorPlayer;
            break;
        case StepTarget::CARD_TARGET:
            item.tgt = resolved_target;
            break;
        case StepTarget::ALL_ENEMY:
            item.tgt = kActorAllEnemies;
            break;
        case StepTarget::RANDOM_ENEMY:
            item.tgt = kActorRandomEnemy;
            break;
        default:
            item.tgt = kActorPlayer;
            break;
    }
    item.amount = step.amount;
    item.flags = step.extra;  // APPLY_POWER: PowerId flags; else 0
    add_to_bottom(s, item);
}

// Move the played card (pool index) from hand to its destination pile. A card
// with the EXHAUST flag goes to the exhaust pile (AbstractCard.exhaust /
// UseCardAction), otherwise to discard (AbstractPlayer.useCard ->
// moveToDiscardPile). Locating by pool index is unambiguous (each pool row is a
// distinct instance). Stage B B3.1 adds the exhaust destination; the skeleton's
// five cards are all non-exhaust, so the discard path is unchanged for them.
void move_card_hand_to_pile(CombatState& s, CardPoolIndex pool_index,
                            bool to_exhaust) noexcept {
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        if (s.hand[i] == pool_index) {
            for (uint8_t j = static_cast<uint8_t>(i + 1); j < s.hand_count; ++j) {
                s.hand[j - 1] = s.hand[j];
            }
            --s.hand_count;
            if (to_exhaust) {
                assert(s.exhaust_count < kExhaustCap &&
                       "exhaust overflow (design doc §4.1: hard assert)");
                s.exhaust[s.exhaust_count] = pool_index;
                ++s.exhaust_count;
                // §5.5 onExhaust (CardGroup.moveToExhaustPile): fires as the card
                // lands in the exhaust pile. No-op without an on-exhaust power.
                dispatch_on_exhaust(s, s.card_pool[pool_index].card_id);
            } else {
                assert(s.discard_count < kDiscardCap &&
                       "discard overflow (design doc §4.1: hard assert)");
                s.discard[s.discard_count] = pool_index;
                ++s.discard_count;
            }
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

    // 1. onPlayCard hook fan-out (§5.3: player powers -> monster powers ->
    //    relics -> stance -> blights -> hand/discard/draw cards). Queues each
    //    responding hook's effects; no-op without a hook-bearing power.
    dispatch_on_play_card(s, s.card_pool[pool_index].card_id, item.target);

    // 2. ++cardsPlayedThisTurn (AbstractPlayer.useCard:1373).
    ++s.cards_played_this_turn;

    // 3. Resolve the effect target (trap-10 random roll at dequeue, else declared).
    const uint8_t resolved_target = resolve_play_target(s, *def, item.target);

    // Per-instance runtime data: the upgrade level selects which of the two
    // effect programs runs (Stage B B3.1 two-row lookup); the instance flags
    // (seeded from the registry at combat_begin / card creation) drive X-cost
    // and the exhaust destination.
    const uint8_t upgrade = s.card_pool[pool_index].upgrade;
    const uint16_t inst_flags = s.card_pool[pool_index].flags;
    const uint8_t cost_now = s.card_pool[pool_index].cost_now;
    const CardEffectView eff = card_effect_steps(*def, upgrade);
    const bool is_xcost = has_card_flag(inst_flags, CardFlag::XCOST);

    // 4. c.use(): QUEUE the card's effect actions via add_to_bottom, in the
    //    card's addToBot order. Effects resolve later through the pump priority
    //    loop -- they are NOT applied inline here (matches AbstractCard.use()).
    //    X-cost cards repeat their program energyOnUse times (WhirlwindAction:
    //    for i in [0, energyOnUse) queue the effect), then spend ALL energy.
    if (is_xcost) {
        int energy_on_use = s.player_energy;
        if (energy_on_use < 0) {
            energy_on_use = 0;
        }
        for (int rep = 0; rep < energy_on_use; ++rep) {
            for (uint8_t k = 0; k < eff.count; ++k) {
                queue_effect_step(s, eff.steps[k], resolved_target);
            }
        }
    } else {
        for (uint8_t k = 0; k < eff.count; ++k) {
            queue_effect_step(s, eff.steps[k], resolved_target);
        }
    }

    // 5. UseCardAction onUseCard fan-out (UseCardAction.java:41-64 order: player
    //    powers -> relics -> cards -> monster powers), THEN move the card out of
    //    hand. Corruption's onUseCard may redirect a SKILL to exhaust here, so the
    //    exhaust destination is re-read from the instance flags AFTER the fan-out
    //    (not the pre-fan-out snapshot). EXHAUST -> exhaust pile, else discard.
    dispatch_on_use_card(s, pool_index, s.card_pool[pool_index].card_id);
    move_card_hand_to_pile(
        s, pool_index,
        has_card_flag(s.card_pool[pool_index].flags, CardFlag::EXHAUST));

    // 6. energy.use(cost): deducted AFTER the effects are queued (useCard order).
    //    X-cost already consumed all energy above; otherwise deduct the
    //    per-instance runtime cost (card_pool[...].cost_now), so a cost modifier
    //    (SET_COST) is honored.
    if (is_xcost) {
        s.player_energy = 0;
    } else {
        s.player_energy =
            static_cast<int16_t>(s.player_energy - static_cast<int16_t>(cost_now));
    }
}

}  // namespace sts::engine
