// Card-play flow -- PLAY_CARD -> cardQueue -> dequeue resolution. See
// card_play.hpp for the full provenance, the played-card limbo window, and
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
#include "sts/engine/piles.hpp"         // limbo_add (cardInUse model)
#include "sts/engine/power_hooks.hpp"   // onPlayCard/onUseCard fan-out + onExhaust
#include "interp/interp_cards.hpp"      // Corruption ApplyPowerAction constructor
#include "relics/relics_boss.hpp"       // kVelvetChokerPlayLimit
#include "sts/engine/relic_hooks.hpp"   // player_has_relic (Strike Dummy bake)
#include "sts/engine/rng_stream.hpp"    // random (card_random_rng roll)
#include "sts/engine/types.hpp"         // CardId

namespace sts::engine {

namespace {

// Count "Strike"-named cards (CardTags.STRIKE == CardDef.is_strike) in the
// hand + draw + discard piles, the source card INCLUDED when it is in one of
// them. Perfected Strike's countCards() scans exactly those three piles
// (PerfectedStrike.java:37-52), and it runs from calculateCardDamage at
// useCard entry (AbstractPlayer.java:1361) -- BEFORE hand.removeCard (:1373).
// A hand-played Perfected Strike is therefore still in the HAND and counts
// itself (playCard, :1285-1302, queues the hovered card without removing it);
// an AUTOPLAYED one sits in the limbo CardGroup and does not. Both fall out of
// pile membership here: at queue time a hand play is still in s.hand, an
// autoplay is in s.limbo, and limbo is not scanned. Exhaust pile is NOT
// counted (matching the Java). The former unconditional source exclusion
// contradicted the Java for hand plays (every hand play was `magic` low).
[[nodiscard]] int count_strikes(const CombatState& s) noexcept {
    int n = 0;
    auto scan = [&](const CardPoolIndex* pile, uint8_t cnt) noexcept {
        for (uint8_t i = 0; i < cnt; ++i) {
            const CardDef* d =
                card_def(static_cast<CardId>(s.card_pool[pile[i]].card_id));
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

// Presence, not magnitude: Corruption's live slot carries amount -1. Its card
// checks this synchronously in use() before it constructs an ApplyPowerAction.
[[nodiscard]] bool player_has_power_slot(const CombatState& s,
                                         PowerId id) noexcept {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return true;
        }
    }
    return false;
}

// Instantiate one registry effect step into a concrete ActionQueueItem and queue
// it via add_to_bottom (exactly how AbstractCard.use() calls addToBot(...)). The
// registry stores src-independent templates; src is always the player for the
// skeleton's five cards (all player-owned effects), and tgt is substituted here:
// kActorPlayer for SELF steps, the resolved monster slot for CARD_TARGET steps.
void queue_effect_step(CombatState& s, const CardEffectStep& step,
                       uint8_t resolved_target,
                       CardPoolIndex source_index) noexcept {
    // Thinking Ahead's queue-time guard (ThinkingAhead.java:32-37): a
    // CHOOSE_CARD step authored `guard: hand_nonempty` is queued only when the
    // hand is non-empty AT THIS INSTANT -- the same synchronous moment
    // use()/resolve_card_play's queueing loop reads it, before any of this
    // card's own already-queued-but-not-yet-executed steps (its DRAW) have
    // run and BEFORE move_played_card_to_limbo (below) removes the played
    // card from hand.group -- exactly where AbstractPlayer.useCard's
    // hand.removeCard(c) sits, AFTER c.use() (:1369) at :1374. For an
    // ordinary HAND play the played card is therefore STILL in `s.hand` here
    // and counts itself (hand_count >= 1 always): the guard is a structural
    // no-op there. It only blocks an AUTOPLAY (op_play_card /
    // op_play_top_draw put the instance straight into limbo before its
    // effects are queued, so it is genuinely absent from `s.hand`). A gated
    // step that fails the guard is simply never queued -- no ActionQueueItem,
    // no CHOOSE_CARD prompt, nothing to auto-resolve.
    if (step.op == static_cast<decltype(step.op)>(Opcode::CHOOSE_CARD) &&
        choose_queue_guard_hand_nonempty(step.extra) && s.hand_count == 0) {
        return;
    }
    ActionQueueItem item{};
    item.opcode = static_cast<uint16_t>(step.op);
    item.src = kActorPlayer;
    // Where the step lands. SELF -> the player; CARD_TARGET -> the card's
    // resolved monster (declared or the card-level random roll); ALL_ENEMY /
    // RANDOM_ENEMY -> the execute-time fan-out / per-hit-roll sentinels
    // (execute_opcode resolves them against the then-live monsters).
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
    // NOTE: PLAY_TOP_DRAW, RESHUFFLE_ALL and the discard-source CHOOSE_CARD no
    // longer carry a stamped source-card exclusion. The played card is in the
    // LIMBO pile while its own actions resolve (resolve_card_play below), so
    // none of those opcodes can see it in a pile it should not be in -- the
    // three per-item compensations are folded into that general model.
    if (step.op == static_cast<decltype(step.op)>(Opcode::MAKE_CARD)) {
        // MAKE_CARD authoring: the step's `extra` packs the created
        // CardId (bits 0-15), the destination CardPile (bits 16-23), and an
        // upgraded-copy flag (bit 24). The interpreter reads the CardId + upgrade
        // bit from the item's `flags` and the CardPile from `src`, so split them
        // out here (tgt stays kActorPlayer -- SELF -- so it is not mistaken for an
        // enemy-fan-out sentinel).
        item.src = static_cast<uint8_t>((step.extra >> 16) & 0xFFu);
        item.flags = step.extra;  // CardId(low16) + upgraded bit(24)
    } else if (step.op == static_cast<decltype(step.op)>(Opcode::DAMAGE_PER_STRIKE)) {
        // Perfected Strike: BAKE the per-"Strike" bonus into a plain DAMAGE at
        // queue time -- calculateCardDamage-at-useCard timing
        // (AbstractPlayer.java:1361, before hand.removeCard at :1373), so a
        // hand-played Perfected Strike counts ITSELF while an autoplayed one
        // (in limbo) does not -- both by plain pile membership (count_strikes).
        // `extra` is the per-Strike magicNumber; `amount` the flat base.
        const int strikes = count_strikes(s);
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
    } else if (step.op ==
               static_cast<decltype(step.op)>(Opcode::RITUAL_DAGGER)) {
        // Ritual Dagger: the base damage AND the kill-conditional growth are
        // both the played instance's misc, so stamp the source pool index into
        // flags (the DAMAGE_RAMPAGE shape); `amount` already carries the
        // authored increase (magicNumber 3 / 5).
        item.flags = static_cast<uint32_t>(source_index);
    }
    // Strike Dummy: AbstractCard.applyPowers runs relic atDamageModify on
    // float(baseDamage) BEFORE the player-power atDamageGive loop
    // (AbstractCard.java:2229-2237); StrikeDummy.atDamageModify (StrikeDummy.java:
    // 28-33) adds +3.0f for STRIKE-tagged cards. Baked into the queued base at
    // queue time -- the same applyPowers-at-use timing as DAMAGE_PER_STRIKE above
    // -- and float-exact (int base+3 == float(base)+3.0f for all card bases). The
    // gate covers the plain DAMAGE steps of Strike/Pommel/Twin/Wild Strike AND
    // Perfected Strike's DAMAGE_PER_STRIKE (already rewritten to a DAMAGE whose
    // `amount` carries the per-Strike bonus -- the game likewise counts first,
    // then applies atDamageModify). Without the relic this is a no-op, so the
    // queued base is byte-identical to a relic-free play.
    if (static_cast<Opcode>(item.opcode) == Opcode::DAMAGE) {
        const CardDef* sd =
            card_def(static_cast<CardId>(s.card_pool[source_index].card_id));
        if (sd != nullptr && sd->is_strike &&
            player_has_relic(s, RelicId::STRIKE_DUMMY)) {
            item.amount += 3;
        }
    }
    if (static_cast<Opcode>(item.opcode) == Opcode::APPLY_POWER &&
        item.tgt == kActorPlayer &&
        apply_power_id_from_flags(item.flags) == PowerId::CORRUPTION) {
        if (player_has_power_slot(s, PowerId::CORRUPTION)) {
            // Corruption.use scans the live power list before constructing the
            // action (Corruption.java:43-51). A second copy therefore neither
            // queues an ApplyPowerAction nor re-runs its constructor cost walk.
            return;
        }
        // ApplyPowerAction performs this walk in its constructor, synchronously
        // at c.use()/queue time (ApplyPowerAction.java:49-67), not when the
        // queued action later resolves. It therefore also runs if the action
        // will become a no-op or combat ends before that resolution.
        apply_corruption_cost_modifier(s);
    }
    add_to_bottom(s, item);
}

// AbstractPlayer.useCard's energy skip (AbstractPlayer.java:1378): the deduction
// is skipped entirely when the player has Corruption and the played card is a
// SKILL. This is separate from both cost rewrites above: the power constructor
// permanently zeroes every SKILL already in all four piles, and onCardDraw
// zeroes later draws for the turn. A SKILL can still arrive directly in hand or
// acquire a nonzero displayed cost; it must be affordable because
// AbstractCard.hasEnoughEnergy:862-893 has no Corruption branch, but useCard
// spends no energy once it is played.
[[nodiscard]] bool corruption_makes_free(const CombatState& s,
                                         const CardDef& def) noexcept {
    if (def.type != CardType::SKILL) {
        return false;
    }
    return player_has_power_slot(s, PowerId::CORRUPTION);
}

// hand.removeCard(c); cardInUse = c (AbstractPlayer.useCard:1373-1375): the
// played card leaves the hand and enters LIMBO -- no observable pile -- until
// the queued USE_CARD action files it away. An AUTOPLAYED card (Havoc /
// Double Tap) never was in the hand: op_play_top_draw / op_play_card put it in
// limbo directly (the game's limbo CardGroup), so finding it absent from the
// hand but already in limbo is the normal autoplay path, not an error.
void move_played_card_to_limbo(CombatState& s, CardPoolIndex pool_index) noexcept {
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        if (s.hand[i] == pool_index) {
            for (uint8_t j = static_cast<uint8_t>(i + 1); j < s.hand_count; ++j) {
                s.hand[j - 1] = s.hand[j];
            }
            --s.hand_count;
            limbo_add(s, pool_index);
            return;
        }
    }
    // Not in hand: an autoplay already sitting in limbo is expected; anything
    // else is a malformed play, left untouched (defensive).
}

// CardQueueItem.autoplayCard is not a stored bit in the compact two-byte queue
// row. Its live producers (PlayTopCardAction / PLAY_CARD copies) put the
// instance in limbo before queueing it, while an ordinary hand play stays in
// hand until AbstractPlayer.useCard succeeds. Pile membership is therefore the
// exact distinction at dequeue time.
[[nodiscard]] bool card_is_autoplaying(const CombatState& s,
                                       CardPoolIndex pool_index) noexcept {
    for (uint8_t i = 0; i < s.limbo_count; ++i) {
        if (s.limbo[i] == pool_index) {
            return true;
        }
    }
    return false;
}

// A failed autoplay canUse check does not run the normal play sequence, but
// GameActionManager still queues `new UseCardAction(c)` with
// dontTriggerOnUseCard set (GameActionManager.java:285-301). Its constructor
// fires no hooks; its update performs only the normal purge/exhaust/discard
// decision (including Strange Spoon and onExhaust). Leaving the compact-model
// card in limbo until this queued USE_CARD resolves is gameplay-equivalent to
// Java's presentation-only ExhaustCardEffect removal and gives the filing
// action the source membership file_card_from_limbo requires.
void queue_cancelled_autoplay_filing(CombatState& s,
                                     CardPoolIndex pool_index) noexcept {
    ActionQueueItem use{};
    use.opcode = static_cast<uint16_t>(Opcode::USE_CARD);
    use.src = kActorPlayer;
    use.tgt = kActorPlayer;
    use.amount = pool_index;
    add_to_bottom(s, use);
}

[[nodiscard]] bool has_player_power(const CombatState& s,
                                    PowerId id) noexcept {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool all_hand_cards_are_attacks(const CombatState& s) noexcept {
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        const CardDef* def =
            card_def(static_cast<CardId>(s.card_pool[s.hand[i]].card_id));
        if (def == nullptr || def->type != CardType::ATTACK) {
            return false;
        }
    }
    return true;
}

// SecretTechnique.canUse (SecretTechnique.java:35-50) and SecretWeapon.canUse
// (SecretWeapon.java:35-50) are the same shape as Clash's: super.canUse() first,
// then a walk of p.drawPile.group setting a flag when any card has the wanted
// CardType, and canUse = false (with a cantUseMessage) when none does. So the
// card is UNPLAYABLE while the draw pile holds no card of that type. The scan
// covers the whole draw pile, and the played card is in the hand at this point
// (or in limbo for an autoplay), never in the draw pile, so it cannot satisfy
// its own predicate.
[[nodiscard]] bool draw_pile_has_type(const CombatState& s,
                                      uint8_t card_type) noexcept {
    for (uint8_t i = 0; i < s.draw_count; ++i) {
        const CardDef* def =
            card_def(static_cast<CardId>(s.card_pool[s.draw[i]].card_id));
        if (def != nullptr && static_cast<uint8_t>(def->type) == card_type) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool normality_vetoes_play(const CombatState& s) noexcept {
    if (s.cards_played_this_turn < 3) {
        return false;
    }
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        if (s.card_pool[s.hand[i]].card_id ==
            static_cast<uint16_t>(CardId::NORMALITY)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool velvet_choker_vetoes_play(const CombatState& s) noexcept {
    for (uint8_t i = 0; i < s.relic_count; ++i) {
        if (s.relics[i].relic_id ==
                static_cast<uint16_t>(RelicId::VELVET_CHOKER) &&
            s.relics[i].counter >= kVelvetChokerPlayLimit) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool monsters_basically_dead(const CombatState& s) noexcept {
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        if (!monster_basically_dead(s.monsters[i])) {
            return false;
        }
    }
    return true;
}

// --- Card-level trigger programs (statuses / curses) -------------------------
// A passive status/curse (trigger != ON_PLAY) runs its `effects`/`upgraded`
// program at a hook site instead of on play. Every in-scope trigger step is
// SELF/player-targeted (Burn/Decay self-DAMAGE, Doubt/Shame self-APPLY_POWER,
// Regret self-LOSE_HP_PER_HAND, Void GAIN_ENERGY, Pain self-LOSE_HP), so the
// queued item is always player-owned/player-targeted -- no enemy fan-out. Since
// these cards are UNPLAYABLE, reusing their effect program for the trigger is
// unambiguous (it is never run on play). add_top mirrors the Java's
// addToTop/addToBot for the specific hook (Pain's LoseHP is addToTop).
//
// `hand_size_lock` stamps LOSE_HP_PER_HAND's amount. Regret is the only author
// of that opcode, and the game LOCKS its magicNumber at TRIGGER time
// (Regret.java:36 `magicNumber = baseMagicNumber = player.hand.size()`, inside
// triggerOnEndOfTurnForPlayingCard), not when the LoseHPAction resolves. The
// distinction is load-bearing: the end-of-turn autoplay below takes every
// triggering card OUT of the hand before its own action runs, so an
// execute-time read would be short by the number of curses that triggered.
void queue_card_trigger_program(CombatState& s, CardEffectView eff,
                                bool add_top, uint8_t hand_size_lock) noexcept {
    for (uint8_t k = 0; k < eff.count; ++k) {
        const CardEffectStep& step = eff.steps[k];
        ActionQueueItem item{};
        item.opcode = static_cast<uint16_t>(step.op);
        item.src = kActorPlayer;
        item.tgt = kActorPlayer;   // all trigger steps are SELF (player-owned)
        item.amount = step.amount;
        if (step.op == static_cast<decltype(step.op)>(Opcode::LOSE_HP_PER_HAND)) {
            item.amount = static_cast<int16_t>(hand_size_lock);
        }
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
    // getRandomMonster(null, true, cardRandomRng) (MonsterGroup.java:156-171):
    // pick uniformly among the monsters still IN the fight via one cardRandomRng
    // draw over [0, aliveCount-1] (inclusive). The Java's aliveOnly filter skips
    // `halfDead || isDying || isEscaping` (:164), so an escaped monster is not
    // in the candidate list -- monster_dead_or_escaped is that exact test here.
    // It KEEPS the targeting predicate now that halfDead is live: halfDead
    // implies hp == 0, so a half-dead monster is already excluded, which is
    // right -- it cannot be picked as a random target even though it is still
    // in the fight.
    uint8_t alive[kMonsterCap];
    int n = 0;
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        if (!monster_dead_or_escaped(s.monsters[i])) {
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

bool card_can_use_without_target(const CombatState& s,
                                 CardPoolIndex pool_index,
                                 bool autoplay) noexcept {
    if (pool_index >= kCardPoolCap) {
        return false;
    }
    const CardInstance& card = s.card_pool[pool_index];
    const CardDef* def = card_def(static_cast<CardId>(card.card_id));
    if (def == nullptr) {
        return false;
    }

    // AbstractCard.canUse's two cost<-1 escape hatches happen before the
    // ordinary cardPlayable && hasEnoughEnergy tail.
    if (has_card_flag(card.flags, CardFlag::UNPLAYABLE)) {
        const bool relic_unlock =
            (def->type == CardType::STATUS &&
             player_has_relic(s, RelicId::MEDICAL_KIT)) ||
            (def->type == CardType::CURSE &&
             player_has_relic(s, RelicId::BLUE_CANDLE));
        if (!relic_unlock) {
            return false;
        }
    }

    // AbstractCard.hasEnoughEnergy, including its power/relic/card fan-outs and
    // the in-scope card override (Clash.canUse).
    if (s.turn_has_ended != 0u ||
        (def->type == CardType::ATTACK &&
         has_player_power(s, PowerId::ENTANGLE)) ||
        velvet_choker_vetoes_play(s) || normality_vetoes_play(s) ||
        (def->requires_all_attacks && !all_hand_cards_are_attacks(s)) ||
        (def->requires_draw_pile_type != kNoDrawPileType &&
         !draw_pile_has_type(s, def->requires_draw_pile_type))) {
        return false;
    }
    // AbstractCard.hasEnoughEnergy (:888):
    //     EnergyPanel.totalCount >= costForTurn || freeToPlay() || isInAutoplay
    // -- so a freeToPlayOnce card is legal at ANY energy, exactly as an autoplay
    // is. freeToPlay() is also true while FreeAttackPower is up (Corruption's
    // ATTACK cousin, :2057-2062), which has no registry row, so the instance bit
    // is the whole predicate here.
    if (!autoplay &&
        !has_card_flag(card.flags, CardFlag::FREE_TO_PLAY_ONCE) &&
        s.player_energy < static_cast<int16_t>(card.cost_now)) {
        return false;
    }
    return true;
}

bool card_can_use(const CombatState& s, CardPoolIndex pool_index,
                  uint8_t target, bool autoplay) noexcept {
    if (pool_index >= kCardPoolCap) {
        return false;
    }
    const CardDef* def = card_def(
        static_cast<CardId>(s.card_pool[pool_index].card_id));
    if (def == nullptr || monsters_basically_dead(s)) {
        return false;
    }
    // UPGRADE-AWARE kind: the game's canUse reads the live `card.target`
    // field, which upgrade() may have reassigned (Blind+ / Trip+ ->
    // ALL_ENEMY, Blind.java:48 / Trip.java:53) -- an upgraded Blind takes no
    // target, so no selected-monster test applies to it.
    const CardTargetKind kind =
        card_target_kind(*def, s.card_pool[pool_index].upgrade);
    const bool selected_enemy_target =
        kind == CardTargetKind::ENEMY ||
        kind == CardTargetKind::SELF_AND_ENEMY;
    if (selected_enemy_target && target < s.monster_count &&
        s.monsters[target].hp <= 0) {
        return false;
    }
    return card_can_use_without_target(s, pool_index, autoplay);
}

void resolve_card_play(CombatState& s, const CardQueueItem& item) noexcept {
    const CardPoolIndex pool_index = item.card_index;
    const CardId card_id = static_cast<CardId>(s.card_pool[pool_index].card_id);
    const CardDef* def = card_def(card_id);
    assert(def != nullptr && "resolve_card_play on a non-registry card");
    if (def == nullptr) {
        return;
    }

    // GameActionManager.getNextAction resolves a random target first, then
    // re-runs canUse before ANY hook/counter/use work (:209-249).
    const uint8_t resolved_target = resolve_play_target(s, *def, item.target);

    const bool autoplay = card_is_autoplaying(s, pool_index);
    // A rejected ordinary queued hand play simply stays in hand. A rejected
    // autoplay already in limbo gets the no-trigger UseCardAction filing
    // GameActionManager adds at :285-301. Nothing before this point except the
    // random-target draw is observable.
    if (!card_can_use(s, pool_index, resolved_target, autoplay)) {
        if (autoplay) {
            queue_cancelled_autoplay_filing(s, pool_index);
        }
        return;
    }

    // 1. onPlayCard hook fan-out (§5.3: player powers -> monster powers ->
    //    relics -> stance -> blights -> hand/discard/draw cards). Queues each
    //    responding hook's effects; no-op without a hook-bearing power.
    dispatch_on_play_card(s, s.card_pool[pool_index].card_id, resolved_target);

    // 2. ++cardsPlayedThisTurn (AbstractPlayer.useCard:1373).
    ++s.cards_played_this_turn;

    // GameActionManager.java:264-283 is AFTER the successful gate and hook /
    // counter sequence, but BEFORE AbstractPlayer.useCard. It applies only to
    // exact CardTarget.ENEMY -- read UPGRADE-AWARE, because the Java reads the
    // live `card.target` field: an upgraded Blind/Trip is ALL_ENEMY
    // (Blind.java:48 / Trip.java:53) and plays through a dead baked target
    // (the Distilled Chaos interleaving the tier-2 test reproduces). A null,
    // dead, or escaping selected monster suppresses useCard; a limbo autoplay
    // is removed with no filing, while an ordinary queued card remains in
    // hand. SELF_AND_ENEMY deliberately proceeds (Spot Weakness).
    if (card_target_kind(*def, s.card_pool[pool_index].upgrade) ==
            CardTargetKind::ENEMY &&
        (resolved_target >= s.monster_count ||
         monster_dead_or_escaped(s.monsters[resolved_target]))) {
        if (autoplay) {
            (void)limbo_remove(s, pool_index);
        }
        return;
    }

    // Per-instance runtime data: the upgrade level selects which of the two
    // effect programs runs (the base/upgraded two-row lookup); the instance flags
    // (seeded from the registry at combat_begin / card creation) drive X-cost
    // and the exhaust destination.
    const uint8_t upgrade = s.card_pool[pool_index].upgrade;
    const uint16_t inst_flags = s.card_pool[pool_index].flags;
    const uint8_t cost_now = s.card_pool[pool_index].cost_now;
    const CardEffectView eff = card_effect_steps(*def, upgrade);
    const bool is_xcost = has_card_flag(inst_flags, CardFlag::XCOST);

    // The card's energyOnUse -- AbstractCard.energyOnUse as the UseCardAction
    // fan-out and the X-cost repetition both read it: the queue item's
    // snapshot for an ignoreEnergyTotal autoplay replay (GameActionManager
    // hands the item's energyOnUse to the card; the AUTOPLAY_X_ENERGY misc
    // snapshot is this engine's carrier), EnergyPanel.totalCount otherwise
    // (AbstractPlayer.useCard:1363-1365 clamps down to it), never negative.
    // Chemical X's +2 is deliberately NOT in this number: the relic boosts the
    // X-action's REPETITION count (energyOnUse + 2 inside WhirlwindAction.
    // update), not the field itself, so Necronomicon's `energyOnUse >= 2` arm
    // (the ON_USE_CARD fan-out below) reads the unboosted value. Hoisted above
    // step 4 so the fan-out and the repetition loop read one derivation.
    int energy_on_use = 0;
    if (is_xcost) {
        energy_on_use =
            autoplay &&
                    has_card_flag(inst_flags, CardFlag::AUTOPLAY_X_ENERGY)
                ? static_cast<int>(s.card_pool[pool_index].misc)
                : static_cast<int>(s.player_energy);
        if (energy_on_use < 0) {
            energy_on_use = 0;
        }
    }

    // 4. c.use(): QUEUE the card's effect actions via add_to_bottom, in the
    //    card's addToBot order. Effects resolve later through the pump priority
    //    loop -- they are NOT applied inline here (matches AbstractCard.use()).
    //    X-cost cards repeat their program energyOnUse times (WhirlwindAction:
    //    for i in [0, energyOnUse) queue the effect), then spend ALL energy.
    //    ON_PLAY guard: a passive status/curse (trigger != ON_PLAY)
    //    stores its TRIGGER program in `effects`; its use() is empty in
    //    the game (Regret.use is a no-op, Regret.java:28-31 etc.), so playing
    //    one -- reachable only via Blue Candle's curse playability -- must NOT
    //    run that program. Previously unreachable (unplayable cards were never
    //    played), so no behavior change for any existing path.
    if (def->trigger != CardTrigger::ON_PLAY) {
        // no queued effects: the card's program belongs to its passive trigger
    } else if (is_xcost) {
        // Chemical X (ChemicalX.java:12-30, BOOST = 2): every X-cost action --
        // WhirlwindAction.update, SkewerAction.update -- computes its repetition
        // count as energyOnUse + 2 while the relic is owned, while the energy
        // actually SPENT in step 6 below is still the un-boosted energyOnUse.
        // The boost is applied AFTER the negative clamp, exactly as the Java
        // adds it to an already-clamped energyOnUse.
        int reps = energy_on_use;
        if (player_has_relic(s, RelicId::CHEMICAL_X)) {
            reps += 2;
        }
        for (int rep = 0; rep < reps; ++rep) {
            for (uint8_t k = 0; k < eff.count; ++k) {
                queue_effect_step(s, eff.steps[k], resolved_target, pool_index);
            }
        }
    } else {
        for (uint8_t k = 0; k < eff.count; ++k) {
            queue_effect_step(s, eff.steps[k], resolved_target, pool_index);
        }
    }

    // 5. UseCardAction: its CONSTRUCTOR runs synchronously at useCard time
    //    (AbstractPlayer.java:1370) -- that is the onUseCard fan-out
    //    (UseCardAction.java:41-64 order: player powers -> relics -> cards ->
    //    monster powers), dispatched here with the played card still in the
    //    hand, exactly as the ctor sees it. Its UPDATE -- the Strange Spoon
    //    roll and the filing -- is the queued USE_CARD action below, which the
    //    useCard body addToBottom's AFTER the card's own actions (:1370), so
    //    the card stays in limbo while they resolve. Corruption's onUseCard may
    //    redirect a SKILL to exhaust; the destination is derived from the
    //    instance flags when USE_CARD executes, which is after the fan-out.
    dispatch_on_use_card(s, pool_index, s.card_pool[pool_index].card_id,
                         resolved_target, energy_on_use);
    {
        ActionQueueItem use{};
        use.opcode = static_cast<uint16_t>(Opcode::USE_CARD);
        use.src = kActorPlayer;
        use.tgt = kActorPlayer;
        use.amount = pool_index;
        add_to_bottom(s, use);
    }
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
            s, card_effect_steps(*od, s.card_pool[pi].upgrade), /*add_top=*/true,
            s.hand_count);
    }
    // hand.removeCard(c); cardInUse = c (:1373-1375): into limbo. The Strange
    // Spoon roll and the exhaust/discard/poof/empower decision all belong to
    // UseCardAction.update and happen when the queued USE_CARD executes
    // (op_use_card) -- AFTER the card's own actions have resolved, which is
    // both pile-visible (discard/exhaust ORDER) and RNG-visible (the spoon
    // boolean's position in the cardRandomRng stream).
    move_played_card_to_limbo(s, pool_index);

    // 6. energy.use(cost): deducted AFTER the effects are queued (useCard order).
    //    X-cost already consumed all energy above; otherwise deduct the
    //    per-instance runtime cost (card_pool[...].cost_now), so a cost modifier
    //    (SET_COST) is honored.
    //    The freeToPlayOnce clause of the same skip (AbstractPlayer.java:1378's
    //    `c.freeToPlay()`): the play costs nothing. It is read off the flags
    //    SNAPSHOT taken above, because the card's own program may already have
    //    granted or spent the bit by now, while the Java reads it at useCard
    //    time. The bit itself is consumed later, when the queued USE_CARD files
    //    the card (UseCardAction.java:87).
    const bool free_once = has_card_flag(inst_flags, CardFlag::FREE_TO_PLAY_ONCE);
    if (!autoplay && is_xcost && !free_once) {
        s.player_energy = 0;
    } else if (!autoplay && !free_once && !corruption_makes_free(s, *def)) {
        s.player_energy =
            static_cast<int16_t>(s.player_energy - static_cast<int16_t>(cost_now));
    }
}

void normalize_terminal_card_queue(CombatState& s) noexcept {
    const uint8_t queued = s.card_queue_count;
    for (uint8_t i = 0; i < queued; ++i) {
        const CardQueueItem item = s.card_queue[i];
        if (is_end_turn_sentinel(item) ||
            !card_is_autoplaying(s, item.card_index)) {
            continue;
        }
        ActionQueueItem use{};
        use.opcode = static_cast<uint16_t>(Opcode::USE_CARD);
        use.src = kActorPlayer;
        use.tgt = kActorPlayer;
        use.amount = item.card_index;
        execute_opcode(s, use);
    }
    s.card_queue_count = 0;
}

// --- Card-level trigger dispatch (public) ------------------------------------

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
        /*add_top=*/false, s.hand_count);
}

void dispatch_card_end_of_turn(CombatState& s) noexcept {
    // §5.4 hand-card triggerOnEndOfTurnForPlayingCard (GameActionManager.java:
    // 373-375). Burn/Decay/Doubt/Regret/Shame do NOT merely queue a self-effect:
    // each one sets `dontTriggerOnUseCard` and APPENDS ITSELF TO THE cardQueue
    // (Burn.java:53-56, Decay.java:49-52, Doubt.java:49-52, Regret.java:35-39,
    // Shame.java:39-42). GameActionManager then dequeues each and PLAYS it --
    // canUse is bypassed by the dontTriggerOnUseCard clause (:214), every
    // onPlayCard / onUseCard / triggerOnCardPlayed fan-out and the
    // cardsPlayedThisTurn increment are skipped (:220-249,
    // UseCardAction.java:41-64), AbstractPlayer.useCard runs `c.use()` (which is
    // where the self-effect actually comes from -- each body is guarded by
    // `if (this.dontTriggerOnUseCard)`), queues UseCardAction, and REMOVES THE
    // CARD FROM THE HAND (:1373-1375). So the card lands in the DISCARD PILE
    // here, at the trigger stage, and the later DiscardAtEndOfTurnAction sweep
    // never sees it.
    //
    // That ordering is observable and was the STS00048 floor-1 divergence: a
    // Shame in hand alongside a Strike went into the discard BEFORE the
    // getTopCard sweep filed the rest of the hand, so the pre-reshuffle discard
    // read [.., Shame, Strike] and not [.., Strike, Shame]. One transposition in
    // the shuffle input is one transposition in the shuffled draw pile, which
    // dealt the player a different card on the following turn and desynced the
    // whole fight. Queueing only the self-effect (what this function used to do)
    // left the card in the hand for the sweep and produced the wrong order.
    //
    // The Java serializes this through the cardQueue -- card k is fully resolved
    // before card k+1 is dequeued -- and the cardQueue is only serviced with an
    // EMPTY action queue, so per card the resolution order is
    // [its own effects, its UseCardAction]. Appending both to the action queue
    // bottom, per card, in hand order, reproduces exactly that sequence; it is
    // also why every step goes to the BOTTOM even though four of the five bodies
    // spell addToTop (with an empty queue the two are the same insertion).
    //
    // Pass 1 mirrors callEndOfTurnActions' walk of hand.group with the hand still
    // whole: that is where Regret locks magicNumber = hand.size().
    CardPoolIndex triggered[kHandCap]{};
    uint8_t triggered_count = 0;
    const uint8_t hand_size_lock = s.hand_count;
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        const CardPoolIndex pi = s.hand[i];
        const CardDef* def =
            card_def(static_cast<CardId>(s.card_pool[pi].card_id));
        if (def == nullptr || def->trigger != CardTrigger::END_OF_TURN) {
            continue;
        }
        triggered[triggered_count] = pi;
        ++triggered_count;
    }

    // Pass 2 is the cardQueue drain: play each card, in queue (== hand) order.
    for (uint8_t k = 0; k < triggered_count; ++k) {
        const CardPoolIndex pi = triggered[k];
        const CardDef* def =
            card_def(static_cast<CardId>(s.card_pool[pi].card_id));
        if (def == nullptr) {
            continue;
        }
        queue_card_trigger_program(
            s, card_effect_steps(*def, s.card_pool[pi].upgrade),
            /*add_top=*/false, hand_size_lock);
        // AbstractPlayer.useCard:1373-1375 -- hand.removeCard(c); cardInUse = c.
        // The queued USE_CARD below is UseCardAction.update: it files the card
        // out of limbo AFTER its own effects have resolved, so the discard order
        // is [effects of card k, card k, effects of card k+1, card k+1, ...].
        // None of the five is `exhaust`, so all five file to the discard pile;
        // op_use_card derives that from the instance flags rather than assuming.
        move_played_card_to_limbo(s, pi);
        ActionQueueItem use{};
        use.opcode = static_cast<uint16_t>(Opcode::USE_CARD);
        use.src = kActorPlayer;
        use.tgt = kActorPlayer;
        use.amount = pi;
        add_to_bottom(s, use);
    }
}

}  // namespace sts::engine
