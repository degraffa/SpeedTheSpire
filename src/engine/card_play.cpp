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
#include "sts/engine/relic_hooks.hpp"   // B3.25: player_has_relic (Strike Dummy bake)
#include "sts/engine/rng_stream.hpp"    // random (card_random_rng roll)
#include "sts/engine/types.hpp"         // CardId

namespace sts::engine {

namespace {

// Count "Strike"-named cards (CardTags.STRIKE == CardDef.is_strike) in the
// hand + draw + discard piles, excluding pool index `exclude` (the just-played
// source card). Perfected Strike's countCards() scans exactly those three piles
// (PerfectedStrike.java:565-580); at queue time the source is still in hand but
// is in limbo (cardInUse) in the game, so it is excluded. Exhaust pile is NOT
// counted (matching the Java).
[[nodiscard]] int count_strikes_excluding(const CombatState& s,
                                          CardPoolIndex exclude) noexcept {
    int n = 0;
    auto scan = [&](const CardPoolIndex* pile, uint8_t cnt) noexcept {
        for (uint8_t i = 0; i < cnt; ++i) {
            const CardPoolIndex pi = pile[i];
            if (pi == exclude) {
                continue;
            }
            const CardDef* d =
                card_def(static_cast<CardId>(s.card_pool[pi].card_id));
            if (d != nullptr && d->is_strike) {
                ++n;
            }
        }
    };
    scan(s.hand, s.hand_count);
    scan(s.draw, s.draw_count);
    scan(s.discard, s.discard_count);
    return n;
}

// Instantiate one registry effect step into a concrete ActionQueueItem and queue
// it via add_to_bottom (exactly how AbstractCard.use() calls addToBot(...)). The
// registry stores src-independent templates; src is always the player for the
// skeleton's five cards (all player-owned effects), and tgt is substituted here:
// kActorPlayer for SELF steps, the resolved monster slot for CARD_TARGET steps.
void queue_effect_step(CombatState& s, const CardEffectStep& step,
                       uint8_t resolved_target,
                       CardPoolIndex source_index) noexcept {
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
    // PLAY_TOP_DRAW (Havoc): stamp the SOURCE card's pool index into `amount` so
    // the opcode can exclude it from the deck it plays from -- the source is
    // cardInUse (limbo) in the game, not a replay candidate (op_play_top_draw).
    if (step.op == static_cast<decltype(step.op)>(Opcode::PLAY_TOP_DRAW)) {
        item.amount = source_index;
    } else if (step.op == static_cast<decltype(step.op)>(Opcode::MAKE_CARD)) {
        // MAKE_CARD (Stage B B3.3 authoring): the step's `extra` packs the created
        // CardId (bits 0-15), the destination CardPile (bits 16-23), and an
        // upgraded-copy flag (bit 24). The interpreter reads the CardId + upgrade
        // bit from the item's `flags` and the CardPile from `src`, so split them
        // out here (tgt stays kActorPlayer -- SELF -- so it is not mistaken for an
        // enemy-fan-out sentinel).
        item.src = static_cast<uint8_t>((step.extra >> 16) & 0xFFu);
        item.flags = step.extra;  // CardId(low16) + upgraded bit(24)
    } else if (step.op == static_cast<decltype(step.op)>(Opcode::DAMAGE_PER_STRIKE)) {
        // Perfected Strike: BAKE the per-"Strike" bonus into a plain DAMAGE at
        // queue time -- applyPowers-at-use timing, with the just-played source card
        // excluded from the count (it is in limbo, PerfectedStrike.java:592-607).
        // `extra` is the per-Strike magicNumber; `amount` the flat base.
        const int strikes = count_strikes_excluding(s, source_index);
        item.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
        item.amount = step.amount + static_cast<int32_t>(step.extra) * strikes;
        item.flags = 0;
    } else if (step.op ==
               static_cast<decltype(step.op)>(Opcode::DAMAGE_UPGRADE_SCALE)) {
        // Searing Blow: its stored upgrade COUNT, not a boolean, determines the
        // permanent base damage. Upgrade k adds (first increment + k), so n
        // upgrades total amount + n*increment + n*(n-1)/2. Bake at queue time,
        // the same applyPowers-at-use timing as every ordinary card damage row.
        const int n = s.card_pool[source_index].upgrade;
        item.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
        item.amount = step.amount + n * static_cast<int32_t>(step.extra) +
                      (n * (n - 1)) / 2;
        item.flags = 0;
    } else if (step.op ==
               static_cast<decltype(step.op)>(Opcode::DAMAGE_RAMPAGE)) {
        // Rampage's ModifyDamageAction follows its DamageAction. Preserve that
        // execute-time order by carrying the source pool index and per-play
        // increment in flags; the interpreter deals current base+misc first,
        // then increments only this card instance's misc counter.
        item.flags = static_cast<uint32_t>(source_index) |
                     (step.extra << 8u);
    } else if (step.op == static_cast<decltype(step.op)>(Opcode::CHOOSE_CARD) &&
               choice_source_is_discard(choose_kind_from_flags(step.extra))) {
        // Headbutt (discard-source choice): stamp the just-played source card's
        // pool index into `tgt` so the choice excludes it -- resolve_card_play
        // moves the source to the discard early, but the game keeps it in limbo
        // (AbstractPlayer.useCard:1369-1375; choice_excluded_index).
        item.tgt = source_index;
    }
    // Strike Dummy (B3.25): AbstractCard.applyPowers runs relic atDamageModify on
    // float(baseDamage) BEFORE the player-power atDamageGive loop
    // (AbstractCard.java:2229-2237); StrikeDummy.atDamageModify (StrikeDummy.java:
    // 28-33) adds +3.0f for STRIKE-tagged cards. Baked into the queued base at
    // queue time -- the same applyPowers-at-use timing as DAMAGE_PER_STRIKE above
    // -- and float-exact (int base+3 == float(base)+3.0f for all card bases). The
    // gate covers the plain DAMAGE steps of Strike/Pommel/Twin/Wild Strike AND
    // Perfected Strike's DAMAGE_PER_STRIKE (already rewritten to a DAMAGE whose
    // `amount` carries the per-Strike bonus -- the game likewise counts first,
    // then applies atDamageModify). No relic -> byte-identical to pre-B3.25.
    if (static_cast<Opcode>(item.opcode) == Opcode::DAMAGE) {
        const CardDef* sd =
            card_def(static_cast<CardId>(s.card_pool[source_index].card_id));
        if (sd != nullptr && sd->is_strike &&
            player_has_relic(s, RelicId::STRIKE_DUMMY)) {
            item.amount += 3;
        }
    }
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

// --- B3.9 card-level trigger programs ---------------------------------------
// A passive status/curse (trigger != ON_PLAY) runs its `effects`/`upgraded`
// program at a hook site instead of on play. Every in-scope trigger step is
// SELF/player-targeted (Burn/Decay self-DAMAGE, Doubt/Shame self-APPLY_POWER,
// Regret self-LOSE_HP_PER_HAND, Void GAIN_ENERGY, Pain self-LOSE_HP), so the
// queued item is always player-owned/player-targeted -- no enemy fan-out. Since
// these cards are UNPLAYABLE, reusing their effect program for the trigger is
// unambiguous (it is never run on play). add_top mirrors the Java's
// addToTop/addToBot for the specific hook (Pain's LoseHP is addToTop).
void queue_card_trigger_program(CombatState& s, CardEffectView eff,
                                bool add_top) noexcept {
    for (uint8_t k = 0; k < eff.count; ++k) {
        const CardEffectStep& step = eff.steps[k];
        ActionQueueItem item{};
        item.opcode = static_cast<uint16_t>(step.op);
        item.src = kActorPlayer;
        item.tgt = kActorPlayer;   // all trigger steps are SELF (player-owned)
        item.amount = step.amount;
        item.flags = step.extra;   // DAMAGE: DamageType; APPLY_POWER: PowerId
        if (add_top) {
            add_to_top(s, item);
        } else {
            add_to_bottom(s, item);
        }
    }
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
    //    ON_PLAY guard (B3.25): a passive status/curse (trigger != ON_PLAY)
    //    stores its TRIGGER program in `effects` (B3.9); its use() is empty in
    //    the game (Regret.use is a no-op, Regret.java:28-31 etc.), so playing
    //    one -- reachable only via Blue Candle's curse playability -- must NOT
    //    run that program. Previously unreachable (unplayable cards were never
    //    played), so no behavior change for any existing path.
    if (def->trigger != CardTrigger::ON_PLAY) {
        // no queued effects: the card's program belongs to its passive trigger
    } else if (is_xcost) {
        int energy_on_use = s.player_energy;
        if (energy_on_use < 0) {
            energy_on_use = 0;
        }
        for (int rep = 0; rep < energy_on_use; ++rep) {
            for (uint8_t k = 0; k < eff.count; ++k) {
                queue_effect_step(s, eff.steps[k], resolved_target, pool_index);
            }
        }
    } else {
        for (uint8_t k = 0; k < eff.count; ++k) {
            queue_effect_step(s, eff.steps[k], resolved_target, pool_index);
        }
    }

    // 5. UseCardAction onUseCard fan-out (UseCardAction.java:41-64 order: player
    //    powers -> relics -> cards -> monster powers), THEN move the card out of
    //    hand. Corruption's onUseCard may redirect a SKILL to exhaust here, so the
    //    exhaust destination is re-read from the instance flags AFTER the fan-out
    //    (not the pre-fan-out snapshot). EXHAUST -> exhaust pile, else discard.
    dispatch_on_use_card(s, pool_index, s.card_pool[pool_index].card_id);
    // hand.triggerOnOtherCardPlayed(c) (AbstractPlayer.useCard:1371-1373): fired
    // AFTER c.use() + the UseCardAction fan-out, BEFORE the card leaves the hand,
    // on every OTHER hand card. Pain (trigger ON_OTHER_CARD_PLAYED) addToTop's a
    // 1-HP loss per other card played. No-op unless a Pain-style curse is in hand.
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        const CardPoolIndex pi = s.hand[i];
        if (pi == pool_index) {
            continue;  // the just-played card does not trigger on itself
        }
        const CardDef* od = card_def(static_cast<CardId>(s.card_pool[pi].card_id));
        if (od == nullptr || od->trigger != CardTrigger::ON_OTHER_CARD_PLAYED) {
            continue;
        }
        queue_card_trigger_program(
            s, card_effect_steps(*od, s.card_pool[pi].upgrade), /*add_top=*/true);
    }
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

// --- B3.9 card-level trigger dispatch (public) -------------------------------

void dispatch_card_on_draw(CombatState& s, uint8_t pool_index) noexcept {
    // AbstractPlayer.draw():1642 c.triggerWhenDrawn() -- fired per drawn card,
    // BEFORE the power/relic onCardDraw fan-out. Void (trigger ON_DRAW) addToBot's
    // a LoseEnergy(1) (GAIN_ENERGY -1). No-op unless the drawn card is a trigger
    // card, so a normal draw is unchanged.
    if (pool_index >= kCardPoolCap) {
        return;
    }
    const CardDef* def = card_def(static_cast<CardId>(s.card_pool[pool_index].card_id));
    if (def == nullptr || def->trigger != CardTrigger::ON_DRAW) {
        return;
    }
    queue_card_trigger_program(
        s, card_effect_steps(*def, s.card_pool[pool_index].upgrade),
        /*add_top=*/false);
}

void dispatch_card_end_of_turn(CombatState& s) noexcept {
    // §5.4 hand-card triggerOnEndOfTurnForPlayingCard (GameActionManager:373-375):
    // Burn/Decay/Doubt/Regret/Shame each queue their self-effect (self DAMAGE /
    // debuff / HP loss). The game routes these through the cardQueue (the card
    // plays itself); the observable result is the self-effect queued at the §5.4
    // hand-trigger stage, which is what we queue here. add_to_bottom preserves
    // hand order; all resolve on later pump iterations.
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        const CardPoolIndex pi = s.hand[i];
        const CardDef* def =
            card_def(static_cast<CardId>(s.card_pool[pi].card_id));
        if (def == nullptr || def->trigger != CardTrigger::END_OF_TURN) {
            continue;
        }
        queue_card_trigger_program(
            s, card_effect_steps(*def, s.card_pool[pi].upgrade), /*add_top=*/false);
    }
}

}  // namespace sts::engine
