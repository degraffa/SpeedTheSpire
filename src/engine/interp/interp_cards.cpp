// CARD-MANIPULATION-domain opcode bodies -- everything that creates, moves,
// re-costs or exhausts card instances, plus the public CHOOSE_CARD queries that
// share their eligibility rules (moved verbatim out of interp.cpp's anonymous
// namespace and its public section; see interp_ops.hpp for the split's
// rationale).

#include "interp_cards.hpp"

#include <cstdint>

#include "interp_ops.hpp"               // actor_powers (the Corruption presence test)
#include "sts/engine/action_queue.hpp"
#include "sts/engine/card_play.hpp"     // roll_random_target (dequeue-time random enemy)
#include "sts/engine/cards.hpp"         // card_def / card_cost / card_flags (MAKE_CARD)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/piles.hpp"         // exhaust_card / shuffle_discard_into_draw / limbo
#include "sts/engine/power_hooks.hpp"   // dispatch_on_exhaust (USE_CARD filing)
#include "sts/engine/relic_hooks.hpp"   // player_has_relic (Strange Spoon)
#include "sts/engine/rng_stream.hpp"    // random / random_boolean (spoon roll)
#include "sts/engine/types.hpp"

namespace sts::engine {

namespace {

// --- CHOOSE_CARD helpers -----------------------------------------------------

// The pile a CHOOSE_CARD of `kind` selects from. A choice's slot index -- the
// arg0 of a CHOOSE action, and the loop bound of every eligibility scan -- indexes
// THIS pile: the hand for most kinds, the discard pile for Headbutt, the exhaust
// pile for Exhume.
[[nodiscard]] uint8_t choice_pile_count(const CombatState& s,
                                        ChoiceKind k) noexcept {
    switch (choice_source(k)) {
        case ChoiceSource::DISCARD:
            return s.discard_count;
        case ChoiceSource::EXHAUST:
            return s.exhaust_count;
        case ChoiceSource::HAND:
        default:
            return s.hand_count;
    }
}
[[nodiscard]] const CardPoolIndex* choice_pile_cards(const CombatState& s,
                                                     ChoiceKind k) noexcept {
    switch (choice_source(k)) {
        case ChoiceSource::DISCARD:
            return s.discard;
        case ChoiceSource::EXHAUST:
            return s.exhaust;
        case ChoiceSource::HAND:
        default:
            return s.hand;
    }
}

// Does the player currently carry `id`? A PRESENCE test (AbstractCreature.
// hasPower), deliberately NOT amount-gated: Corruption lives in its slot with
// amount -1 (CorruptionPower.java:27).
[[nodiscard]] bool player_carries_power(const CombatState& s,
                                        PowerId id) noexcept {
    const PowerView pv = actor_powers(s, kActorPlayer);
    for (uint8_t i = 0; i < pv.count; ++i) {
        if (pv.slots[i].power_id == static_cast<uint16_t>(id)) {
            return true;
        }
    }
    return false;
}

// AbstractCard.setCostForTurn (AbstractCard.java:2001-2011): assign, clamp a
// negative sentinel to 0, and mark the instance cost-modified-for-turn whenever
// the new value differs from the card's own base cost (so the end-turn sweep
// restores it). ExhumeAction reaches a card this way.
void set_cost_for_turn(CombatState& s, CardPoolIndex pi, int amount) noexcept {
    CardInstance& c = s.card_pool[pi];
    const CardDef* def = card_def(static_cast<CardId>(c.card_id));
    if (def == nullptr) {
        return;
    }
    if (amount < 0) {
        amount = 0;  // setCostForTurn clamps at zero (:2004-2006)
    }
    c.cost_now = static_cast<uint8_t>(amount);
    if (static_cast<uint8_t>(amount) != card_cost(*def, c.upgrade)) {
        c.flags = static_cast<uint16_t>(
            c.flags | card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN));
    }
}

// ExhumeAction's "return the picked card to the hand" body (ExhumeAction.java:
// 54-66 forced / :92-102 prompted): addToHand, then -- while the player has
// Corruption and the card is a SKILL -- setCostForTurn(-9), i.e. free this turn;
// then removeCard from the exhaust pile.
void exhaust_slot_to_hand(CombatState& s, uint8_t slot) noexcept {
    if (slot >= s.exhaust_count || s.hand_count >= kHandCap) {
        return;
    }
    const CardPoolIndex pi = s.exhaust[slot];
    for (uint8_t j = static_cast<uint8_t>(slot + 1); j < s.exhaust_count; ++j) {
        s.exhaust[j - 1] = s.exhaust[j];
    }
    --s.exhaust_count;
    s.hand[s.hand_count++] = pi;
    const CardDef* def = card_def(static_cast<CardId>(s.card_pool[pi].card_id));
    if (def != nullptr && def->type == CardType::SKILL &&
        player_carries_power(s, PowerId::CORRUPTION)) {
        set_cost_for_turn(s, pi, 0);
    }
}

// Remove hand slot `slot` from the hand (shifting the tail down) and return the
// card's pool index. Precondition: slot < hand_count.
CardPoolIndex remove_from_hand(CombatState& s, uint8_t slot) noexcept {
    const CardPoolIndex pi = s.hand[slot];
    for (uint8_t j = static_cast<uint8_t>(slot + 1); j < s.hand_count; ++j) {
        s.hand[j - 1] = s.hand[j];
    }
    --s.hand_count;
    return pi;
}

// Upgrade the pool instance in place (in-combat upgrade, ArmamentsAction:
// c.upgrade() + applyPowers()). `upgrade` is a count; re-seed cost_now/flags from
// the registry's upgraded row so the new cost/flags take effect (upgradeBaseCost).
void upgrade_instance(CombatState& s, CardPoolIndex pi) noexcept {
    CardInstance& c = s.card_pool[pi];
    const CardDef* def = card_def(static_cast<CardId>(c.card_id));
    if (def == nullptr) {
        return;
    }
    if (c.upgrade == UINT8_MAX) {
        return;  // the POD encodes the count in u8; never wrap it to base
    }
    const CardId id = static_cast<CardId>(c.card_id);
    ++c.upgrade;
    if (id == CardId::BLOOD_FOR_BLOOD) {
        // BloodForBlood.upgrade uses its CURRENT combat-reduced cost: untouched
        // 4 -> 3, already-reduced n -> max(0,n-1), not a blind reset to 3.
        if (c.cost_now > 0) {
            --c.cost_now;
        }
    } else {
        c.cost_now = card_cost(*def, c.upgrade);
    }
    c.flags = card_flags(*def, c.upgrade);
}

// Move discard slot `slot` to the top of the draw pile (Headbutt /
// DiscardPileToTopOfDeckAction: removeCard from discard, moveToDeck == addToTop).
void discard_slot_to_draw_top(CombatState& s, uint8_t slot) noexcept {
    if (slot >= s.discard_count) {
        return;
    }
    const CardPoolIndex pi = s.discard[slot];
    for (uint8_t j = static_cast<uint8_t>(slot + 1); j < s.discard_count; ++j) {
        s.discard[j - 1] = s.discard[j];
    }
    --s.discard_count;
    if (s.draw_count < kDrawCap) {
        s.draw[s.draw_count++] = pi;  // top of draw (draw[draw_count-1])
    }
}

// Dual Wield: add one stat-equivalent clone of pool instance `src_pi` to
// the hand, spilling to the discard when the hand is full (MakeTempCardInHand-
// Action.update:71-77). makeStatEquivalentCopy (AbstractCard.java:826-848)
// preserves the upgrade count, cost/costForTurn (cost_now + the FOR_TURN bit in
// `flags`), and misc (Rampage's combat counter), so the clone copies the whole
// CardInstance into a fresh pool row.
void clone_card_to_hand(CombatState& s, CardPoolIndex src_pi) noexcept {
    int slot = -1;
    for (int i = 0; i < kCardPoolCap; ++i) {
        if (s.card_pool[i].card_id == static_cast<uint16_t>(CardId::NONE)) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return;  // pool exhausted (defensive; 160-row cap, design §4.2)
    }
    s.card_pool[slot] = s.card_pool[src_pi];
    const CardPoolIndex idx = static_cast<CardPoolIndex>(slot);
    if (s.hand_count < kHandCap) {
        s.hand[s.hand_count++] = idx;
    } else if (s.discard_count < kDiscardCap) {
        s.discard[s.discard_count++] = idx;
    }
}

// Apply one CHOOSE_CARD manipulation to slot `slot` of the kind's SOURCE pile
// (hand for exhaust/put-on-draw-top/upgrade/duplicate; discard for
// discard-to-draw-top). `copies` is the DUPLICATE clone count (Dual Wield
// magicNumber); the other kinds ignore it. This is the FORCED/auto shape for
// DUPLICATE (DualWieldAction.java:49-57: clones appended, NO hand reorder);
// the prompted screen bookkeeping lives in apply_choice_selection.
void apply_choice_to_slot(CombatState& s, uint8_t slot, ChoiceKind kind,
                          int copies = 1) noexcept {
    if (slot >= choice_pile_count(s, kind)) {
        return;
    }
    switch (kind) {
        case ChoiceKind::EXHAUST:
            // moveToExhaustPile (fires §5.5 onExhaust). exhaust_card locates by
            // pool index, so removing by index first would be equivalent; call it
            // directly on the slot's pool index.
            exhaust_card(s, s.hand[slot]);
            break;
        case ChoiceKind::PUT_ON_DRAW_TOP: {
            // moveToDeck -> addToTop == top of the draw pile (draw[draw_count-1]).
            const CardPoolIndex pi = remove_from_hand(s, slot);
            if (s.draw_count < kDrawCap) {
                s.draw[s.draw_count++] = pi;
            }
            break;
        }
        case ChoiceKind::UPGRADE:
            upgrade_instance(s, s.hand[slot]);
            break;
        case ChoiceKind::DISCARD_TO_DRAW_TOP:
            discard_slot_to_draw_top(s, slot);
            break;
        case ChoiceKind::DUPLICATE: {
            const CardPoolIndex pi = s.hand[slot];
            for (int k = 0; k < copies; ++k) {
                clone_card_to_hand(s, pi);
            }
            break;
        }
        case ChoiceKind::EXHAUST_TO_HAND:
            exhaust_slot_to_hand(s, slot);
            break;
    }
}

// The eligible source-pile-card count for a CHOOSE_CARD of `kind` (hand slots for
// the hand kinds, discard slots for discard-to-draw-top). The just-played source
// card needs no exclusion here: it sits in the LIMBO pile while its own choice
// is open (AbstractPlayer.useCard:1369-1375), so no source pile contains it.
[[nodiscard]] int count_eligible(const CombatState& s, ChoiceKind kind) noexcept {
    const uint8_t src_count = choice_pile_count(s, kind);
    int n = 0;
    for (uint8_t i = 0; i < src_count; ++i) {
        if (choice_slot_eligible(s, i, kind)) {
            ++n;
        }
    }
    return n;
}

}  // namespace

// --- Opcode bodies ----------------------------------------------------------

// MAKE_CARD: create `count` copies of `id` into `pile`. Each copy
// takes a free card_pool row (card_id == NONE); a new instance's cost_now/flags
// come from the registry (base, upgrade 0). Provenance: MakeTempCardInHandAction
// (:64-82, incl. the hand-full -> discard spill), MakeTempCardInDiscardAction,
// ShuffleIntoDrawPileAction + CardGroup.addToRandomSpot (:463-468).
void op_make_card(CombatState& s, uint16_t card_id_raw, CardPile pile,
                  int count, bool upgraded) noexcept {
    const CardId id = static_cast<CardId>(card_id_raw);
    const CardDef* def = card_def(id);
    if (def == nullptr || count <= 0) {
        return;
    }
    // makeStatEquivalentCopy preserves timesUpgraded (Anger clones an upgraded
    // Anger); every other in-scope MAKE_CARD source is a fresh base card.
    const uint8_t upg = upgraded ? 1 : 0;
    for (int k = 0; k < count; ++k) {
        int slot = -1;
        for (int i = 0; i < kCardPoolCap; ++i) {
            if (s.card_pool[i].card_id == static_cast<uint16_t>(CardId::NONE)) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            return;  // pool exhausted (defensive; the 160-row cap, design §4.2)
        }
        s.card_pool[slot].card_id = card_id_raw;
        s.card_pool[slot].upgrade = upg;
        s.card_pool[slot].cost_now = card_cost(*def, upg);
        s.card_pool[slot].flags = card_flags(*def, upg);
        s.card_pool[slot].misc = 0;
        const CardPoolIndex idx = static_cast<CardPoolIndex>(slot);
        switch (pile) {
            case CardPile::HAND:
                // MakeTempCardInHandAction: overflow past kHandCap spills to the
                // discard pile (the "hand is full" branch, :71-77).
                if (s.hand_count < kHandCap) {
                    s.hand[s.hand_count++] = idx;
                } else if (s.discard_count < kDiscardCap) {
                    s.discard[s.discard_count++] = idx;
                }
                break;
            case CardPile::DISCARD:
                if (s.discard_count < kDiscardCap) {
                    s.discard[s.discard_count++] = idx;
                }
                break;
            case CardPile::DRAW:
                // Onto the top of the draw pile (draw[draw_count-1] == top).
                if (s.draw_count < kDrawCap) {
                    s.draw[s.draw_count++] = idx;
                }
                break;
            case CardPile::DRAW_RANDOM: {
                // CardGroup.addToRandomSpot: insert at cardRandomRng.random(size-1)
                // (one draw), or append with NO draw when the pile is empty.
                if (s.draw_count >= kDrawCap) {
                    break;
                }
                int pos = 0;
                if (s.draw_count > 0) {
                    pos = random(s.card_random_rng, s.draw_count - 1);
                }
                for (int j = s.draw_count; j > pos; --j) {
                    s.draw[j] = s.draw[j - 1];
                }
                s.draw[pos] = idx;
                ++s.draw_count;
                break;
            }
        }
    }
}

// CHOOSE_CARD execute (auto path only -- the pump reaches here only when the
// choice does NOT require a user prompt: it is RANDOM, or forced because the
// eligible count is <= the amount to select). Faithful to ExhaustAction /
// PutOnDeckAction / ArmamentsAction's no-screen branches.
void op_choose_card(CombatState& s, const ActionQueueItem& item) noexcept {
    const ChoiceKind kind = choose_kind_from_flags(item.flags);
    const bool is_random = choose_is_random(item.flags);
    const int need = item.amount;
    if (need <= 0) {
        return;
    }
    const int eligible = count_eligible(s, kind);
    if (eligible <= need) {
        // Forced: apply to ALL eligible cards. Snapshot pool indices first (the
        // apply mutates the source pile). (ExhaustAction: hand.size() <= amount ->
        // exhaust whole hand; ArmamentsAction: exactly one upgradeable -> upgrade
        // it; PutOnDeckAction: hand.size() <= amount -> move all;
        // DiscardPileToTopOfDeckAction: <= 1 discard card -> auto-move it.) The
        // eligible-<=-need bound keeps the snapshot within kHandCap (every in-scope
        // discard-source choice has need == 1).
        CardPoolIndex picked[kHandCap];
        int m = 0;
        const uint8_t sc = choice_pile_count(s, kind);
        const CardPoolIndex* src_pile = choice_pile_cards(s, kind);
        for (uint8_t i = 0; i < sc && m < kHandCap; ++i) {
            if (choice_slot_eligible(s, i, kind)) {
                picked[m++] = src_pile[i];
            }
        }
        for (int k = 0; k < m; ++k) {
            // Re-find the slot each time (earlier applies may have shifted the pile).
            const uint8_t sc2 = choice_pile_count(s, kind);
            const CardPoolIndex* sp2 = choice_pile_cards(s, kind);
            for (uint8_t i = 0; i < sc2; ++i) {
                if (sp2[i] == picked[k]) {
                    apply_choice_to_slot(s, i, kind,
                                         choose_copies_from_flags(item.flags));
                    break;
                }
            }
        }
        return;
    }
    // eligible > need and RANDOM: roll card_random_rng once per pick over the
    // CURRENT hand (hand.getRandomCard(cardRandomRng), one draw per card;
    // ExhaustAction.isRandom). Only EXHAUST uses RANDOM in scope, whose eligible
    // set is the whole hand, so rolling over the hand matches getRandomCard.
    if (is_random) {
        for (int k = 0; k < need; ++k) {
            if (s.hand_count == 0) {
                break;
            }
            const int32_t ridx = random(s.card_random_rng,
                                        static_cast<int32_t>(s.hand_count) - 1);
            apply_choice_to_slot(s, static_cast<uint8_t>(ridx), kind);
        }
    }
    // Non-random with eligible > need never reaches here (the pump blocks first).
}

// PLAY_TOP_DRAW (Havoc / PlayTopCardAction.update): play the top draw card, then
// exhaust it. The SOURCE card (Havoc itself) needs no handling here any more:
// it is cardInUse during PlayTopCardAction in the game and sits in the LIMBO
// pile here (resolve_card_play), so it is in neither the draw pile nor the
// discard this may reshuffle -- the former lift-out/restore compensation is
// folded into the general model. The card_random_rng monster-target roll
// happens FIRST and UNCONDITIONALLY (getRandomMonster is evaluated as
// Havoc.use()'s argument), then the empty / reshuffle checks.
void op_play_top_draw(CombatState& s) noexcept {
    // getRandomMonster(null, true, cardRandomRng): one card_random_rng draw over
    // the live monsters (no draw if none alive -- combat is over in that case).
    const uint8_t target = roll_random_target(s);
    if (s.draw_count == 0 && s.discard_count == 0) {
        return;  // deckSize + discardSize == 0 -> nothing to play (:34-36)
    }
    if (s.draw_count == 0) {
        shuffle_discard_into_draw(s);  // EmptyDeckShuffleAction (one shuffle_rng draw)
        if (s.draw_count == 0) {
            return;
        }
    }
    const CardPoolIndex pi = s.draw[s.draw_count - 1];  // getTopCard
    --s.draw_count;
    // exhaustOnUseOnce = true (:48) -> force the played card to exhaust; play it
    // free (autoplay does not pay energy). The game moves the card into the
    // player's limbo CardGroup (PlayTopCardAction.update:
    // `AbstractDungeon.player.limbo.group.add(card)`) -- ours goes to the limbo
    // pile, where resolve_card_play finds it -- then its play is queued at the
    // front of the cardQueue (NewQueueCardAction -> the normal §5.3 resolve).
    s.card_pool[pi].flags |=
        card_flag_bit(CardFlag::EXHAUST_ON_USE_ONCE);
    s.card_pool[pi].cost_now = 0;
    limbo_add(s, pi);
    CardQueueItem q{};
    q.card_index = pi;
    q.target = target;
    add_card_to_queue_top(s, q);
}

// PLAY_CARD -- the general recursive-play verb. Three Java sites share this
// shape and differ only in the flags:
//   * DoubleTapPower.onUseCard (DoubleTapPower.java:43-66) -- makeSameInstanceOf
//     the just-played ATTACK into limbo, purgeOnUse = true, then
//     addCardQueueItem(..., inFrontOfQueue = true) with autoplay and
//     ignoreEnergyTotal: kPlayCardCopy | kPlayCardPurge | kPlayCardQueueFront.
//   * PlayTopCardAction.update (PlayTopCardAction.java:33-66) -- the top card of
//     the draw pile, reshuffling an empty draw pile first, optionally forced to
//     exhaust: kPlayCardFromDrawTop [| kPlayCardExhaust].
//   * a start-of-turn "play the top card of your draw pile" power: the same
//     draw-top form with no exhaust.
// `target` is the monster the play is aimed at. kActorRandomEnemy is accepted and
// rolls one card_random_rng getRandomMonster draw HERE, before the pile checks --
// matching Havoc, where getRandomMonster is evaluated as the action's argument.
// A QUEUED PLAY_CARD never arrives with that sentinel (execute_opcode resolves
// dynamic targets before the switch, taking the same single draw); the branch is
// for the direct callers, which pass a concrete target today.
// The played instance goes into the LIMBO pile -- the game's limbo CardGroup
// (DoubleTapPower.java:51 `player.limbo.addToBottom(tmp)`; PlayTopCardAction's
// `limbo.group.add(card)`) -- where resolve_card_play finds it; the queued
// USE_CARD then files it (a purge instance lands in no pile).
void op_play_card(CombatState& s, uint8_t target, int source_index,
                  uint32_t flags) noexcept {
    const uint8_t resolved =
        (target == kActorRandomEnemy) ? roll_random_target(s) : target;
    CardPoolIndex pi = 0;
    if ((flags & kPlayCardFromDrawTop) != 0u) {
        if (s.draw_count == 0 && s.discard_count == 0) {
            return;  // deckSize + discardSize == 0 -> nothing to play (:34-36)
        }
        if (s.draw_count == 0) {
            shuffle_discard_into_draw(s);  // EmptyDeckShuffleAction (:38-43)
            if (s.draw_count == 0) {
                return;
            }
        }
        pi = s.draw[s.draw_count - 1];  // getTopCard
        --s.draw_count;
    } else {
        if (source_index < 0 || source_index >= kCardPoolCap) {
            return;
        }
        pi = static_cast<CardPoolIndex>(source_index);
    }
    if ((flags & kPlayCardCopy) != 0u) {
        // makeSameInstanceOf: a fresh instance of the SAME card at the same
        // upgrade level, with the source's per-instance state (cost_now, flags,
        // misc) carried over -- the shape clone_card_to_hand already implements,
        // minus its hand insertion (this copy is placed below, and its cost and
        // disposition flags are rewritten first).
        int slot = -1;
        for (int i = 0; i < kCardPoolCap; ++i) {
            if (s.card_pool[i].card_id == static_cast<uint16_t>(CardId::NONE)) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            return;  // pool exhausted (defensive; 160-row cap, design §4.2)
        }
        s.card_pool[slot] = s.card_pool[pi];
        pi = static_cast<CardPoolIndex>(slot);
    }
    s.card_pool[pi].cost_now = 0;  // autoplay: useCard skips energy.use (:1378)
    if ((flags & kPlayCardPurge) != 0u) {
        s.card_pool[pi].flags = static_cast<uint16_t>(
            s.card_pool[pi].flags | card_flag_bit(CardFlag::PURGE_ON_USE));
    }
    if ((flags & kPlayCardExhaust) != 0u) {
        s.card_pool[pi].flags = static_cast<uint16_t>(
            s.card_pool[pi].flags |
            card_flag_bit(CardFlag::EXHAUST_ON_USE_ONCE));
    }
    limbo_add(s, pi);
    CardQueueItem q{};
    q.card_index = pi;
    q.target = resolved;
    if ((flags & kPlayCardQueueFront) != 0u) {
        add_card_to_queue_top(s, q);
    } else {
        add_card_to_queue_bottom(s, q);
    }
}

// USE_CARD -- UseCardAction.update (UseCardAction.java:77-137) as a queued
// action; `item.amount` is the played card's pool index, sitting in the LIMBO
// pile since resolve_card_play. Everything the card's own program queued has
// already resolved (useCard addToBottom'd this LAST, AbstractPlayer.java:1370),
// so the filing below lands AFTER any card the program added to the discard
// (Anger's copy) or the exhaust pile (Fiend Fire's hand exhausts), and the
// Strange Spoon boolean sits AFTER the program's cardRandomRng draws in the
// stream. In update() order:
//   :79-88   onAfterUseCard fan-out -- NO S1 binder exists (the only binders in
//            the game are BeatOfDeath / Rebound / Slow / TimeMaze / TimeWarp,
//            all out of S1 scope), so the seam is this comment; a future binder
//            dispatches here, between the program's actions and the filing.
//   :89-94   purgeOnUse: the instance poofs -- leaves limbo, lands in NO pile.
//   :95-108  POWER: hand.empower -- likewise lands in NO pile.
//   :109-113 Strange Spoon: an exhausting non-POWER play rolls ONE
//            cardRandomRng.randomBoolean(); true redirects to the discard. The
//            exhaustCard guard comes FIRST, so a non-exhausting play consumes
//            no RNG at all -- that guard order is RNG-visible.
//   :114-131 file: exhaust (moveToExhaustPile -> onExhaust hooks + the
//            resetAttributes cost revert) or discard (moveToDiscardPile). The
//            reboundCard / shuffleBackIntoDrawPile / returnToHand branches
//            (:118-126) have no S1 producer and are deliberately absent.
// The exhaust destination is read from the instance flags HERE. Intrinsic or
// permanent exhaust is CardFlag::EXHAUST; Havoc/Corruption's action-local
// exhaust is EXHAUST_ON_USE_ONCE and is cleared at UseCardAction.java:132 after
// the filing decision, even when Strange Spoon saved the card.
void op_use_card(CombatState& s, const ActionQueueItem& item) noexcept {
    if (item.amount < 0 || item.amount >= kCardPoolCap) {
        return;  // malformed (defensive)
    }
    const uint8_t pi = static_cast<uint8_t>(item.amount);
    const CardDef* def = card_def(static_cast<CardId>(s.card_pool[pi].card_id));
    const bool is_power = def != nullptr && def->type == CardType::POWER;
    const bool remove_only =
        is_power || has_card_flag(s.card_pool[pi].flags, CardFlag::PURGE_ON_USE);
    const bool exhaust_once = has_card_flag(
        s.card_pool[pi].flags, CardFlag::EXHAUST_ON_USE_ONCE);
    bool to_exhaust =
        has_card_flag(s.card_pool[pi].flags, CardFlag::EXHAUST) ||
        exhaust_once;
    if (!remove_only && to_exhaust &&
        player_has_relic(s, RelicId::STRANGE_SPOON) &&
        random_boolean(s.card_random_rng)) {
        to_exhaust = false;  // spoonProc (:109-118)
    }
    if (!file_card_from_limbo(s, pi, to_exhaust, remove_only)) {
        return;  // not in limbo (defensive; nothing to file)
    }
    if (!remove_only && exhaust_once) {
        // UseCardAction.update:132 -- exhaustOnUseOnce is consumed whether the
        // card exhausted or Strange Spoon redirected it to discard.
        s.card_pool[pi].flags = static_cast<uint16_t>(
            s.card_pool[pi].flags &
            ~card_flag_bit(CardFlag::EXHAUST_ON_USE_ONCE));
    }
    if (!remove_only && to_exhaust) {
        // §5.5 onExhaust (CardGroup.moveToExhaustPile): fires as the card lands
        // in the exhaust pile -- AFTER the card's own program resolved, so a
        // Dark Embrace draw for the played card is queued at THIS point, behind
        // anything the program's own exhausts already queued.
        dispatch_on_exhaust(s, pi, s.card_pool[pi].card_id);
    }
}

// FIEND_FIRE (FiendFireAction.update, FiendFireAction.java:32-46): count =
// hand.size() at EXECUTE time; addToTop `count` DamageActions, then addToTop
// `count` ExhaustAction(1, isRandom = true) -- the SECOND addToTop loop lands in
// front, so the random exhausts all resolve before the first hit. Both loops
// preserve their internal order because each pushes onto the front in turn, which
// reverses an already-uniform list into itself.
void op_fiend_fire(CombatState& s, uint8_t target, int base) noexcept {
    const int count = static_cast<int>(s.hand_count);
    for (int i = 0; i < count; ++i) {
        ActionQueueItem dmg{};
        dmg.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
        dmg.src = kActorPlayer;
        dmg.tgt = target;
        dmg.amount = base;
        add_to_top(s, dmg);
    }
    for (int i = 0; i < count; ++i) {
        ActionQueueItem ex{};
        ex.opcode = static_cast<uint16_t>(Opcode::CHOOSE_CARD);
        ex.src = kActorPlayer;
        ex.tgt = kActorPlayer;
        ex.amount = 1;
        ex.flags = make_choose_flags(ChoiceKind::EXHAUST, /*random=*/true);
        add_to_top(s, ex);
    }
}

// SET_COST: set card_pool[src].cost_now = amount (the cost-modifier
// write path). Clamped to the u8 cost_now range. The "temporary" (per-turn
// reset) and "which card / under what condition" logic belongs to the consumer
// (a power hook or a CHOOSE selection); this is the write primitive.
//
// CURRENTLY UNREACHABLE (verified at the cleanup-interp refactor): nothing
// queues Opcode::SET_COST -- no site in src/ builds such an item, and no
// registry YAML authors `{op: SET_COST}`, because the operand is a card-POOL
// index (`src`) that a YAML author has no way to name. The opcode is retained
// (opcodes are append-only) and the body stays live for the deferred SET_COST
// authoring obligation. It becomes reachable as soon as a C++ consumer that
// already holds a pool index -- a power hook (the cost-modifier path) or a
// CHOOSE selection -- queues the item.
void op_set_cost(CombatState& s, uint8_t pool_index, int new_cost) noexcept {
    if (pool_index >= kCardPoolCap) {
        return;
    }
    if (new_cost < 0) {
        new_cost = 0;
    } else if (new_cost > 255) {
        new_cost = 255;
    }
    s.card_pool[pool_index].cost_now = static_cast<uint8_t>(new_cost);
}

// Sever Soul: ExhaustAllNonAttackAction queues ExhaustSpecificCardAction for
// every non-Attack in hand; top insertion makes the topmost card exhaust first.
void op_exhaust_non_attacks(CombatState& s) noexcept {
    for (uint8_t i = s.hand_count; i > 0; --i) {
        const CardPoolIndex pi = s.hand[static_cast<uint8_t>(i - 1)];
        const CardDef* def = card_def(static_cast<CardId>(s.card_pool[pi].card_id));
        if (def != nullptr && def->type != CardType::ATTACK) {
            exhaust_card(s, pi);
        }
    }
}

// RANDOM_ATTACK_TO_HAND (Infernal Blade / InfernalBlade.use:31-35): pick ONE
// uniformly-random member of the combat ATTACK pool (returnTrulyRandomCardIn-
// Combat(ATTACK), AbstractDungeon.java:964-979 -- ONE cardRandomRng
// random(size-1) draw over srcCommon+srcUncommon+srcRare filtered to
// non-HEALING ATTACKs), makeCopy() a BASE instance, setCostForTurn(0), and add
// it to the hand (MakeTempCardInHandAction: hand-cap spill to discard). The
// cost-0 is this-turn-only: COST_MODIFIED_FOR_TURN is reset by the end-turn
// sweep (AbstractRoom.endTurn:397-405) / on exhaust (ExhaustCardEffect:41-43).
// Pool membership/order provenance: generated kIroncladAttackPool (cards.hpp).
void op_random_attack_to_hand(CombatState& s) noexcept {
    static_assert(kIroncladAttackPoolCount > 0,
                  "Infernal Blade needs a non-empty attack pool");
    const int32_t pick = random(
        s.card_random_rng, static_cast<int32_t>(kIroncladAttackPoolCount) - 1);
    const CardId id = kIroncladAttackPool[static_cast<unsigned>(pick)];
    const CardDef* def = card_def(id);
    if (def == nullptr) {
        return;  // defensive; the pool only holds registry rows
    }
    int slot = -1;
    for (int i = 0; i < kCardPoolCap; ++i) {
        if (s.card_pool[i].card_id == static_cast<uint16_t>(CardId::NONE)) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return;  // pool exhausted (defensive; 160-row cap, design §4.2)
    }
    s.card_pool[slot].card_id = static_cast<uint16_t>(id);
    s.card_pool[slot].upgrade = 0;                    // library copies are base
    s.card_pool[slot].cost_now = 0;                   // setCostForTurn(0)
    s.card_pool[slot].flags = static_cast<uint16_t>(
        card_flags(*def, 0) | card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN));
    s.card_pool[slot].misc = 0;
    const CardPoolIndex idx = static_cast<CardPoolIndex>(slot);
    if (s.hand_count < kHandCap) {
        s.hand[s.hand_count++] = idx;
    } else if (s.discard_count < kDiscardCap) {
        s.discard[s.discard_count++] = idx;
    }
}

// CONDITIONAL_DRAW (Impatience / ConditionalDrawAction.update, :29-45):
// checkCondition() (:39-45) returns false as soon as ONE hand card has the
// restricted type, and only a true answer addToTop's a DrawCardAction(source,
// amount). The draw is QUEUED rather than performed here, exactly as in the Java:
// that keeps it behind the DRAW opcode's No Draw gate and its per-card
// onCardDraw fan-out. addToTop, so it resolves ahead of anything already queued
// behind this item.
//
// The card that played this is already out of the hand when the item executes
// (resolve_card_play moves it before the queued effects run, mirroring
// AbstractPlayer.useCard removing it before the action resolves), so it cannot
// veto its own draw. Impatience is a SKILL and its restricted type is ATTACK, so
// that makes no difference for the only authored user -- it is stated because the
// scan is over whatever is in hand at EXECUTE time.
void op_conditional_draw(CombatState& s, int amount,
                         uint8_t restricted_type) noexcept {
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        const CardDef* def =
            card_def(static_cast<CardId>(s.card_pool[s.hand[i]].card_id));
        if (def != nullptr &&
            static_cast<uint8_t>(def->type) == restricted_type) {
            return;  // checkCondition() false -- NOTHING is queued
        }
    }
    ActionQueueItem draw{};
    draw.opcode = static_cast<uint16_t>(Opcode::DRAW);
    draw.src = kActorPlayer;
    draw.tgt = kActorPlayer;
    draw.amount = amount;
    add_to_top(s, draw);  // ConditionalDrawAction.java:33
}

namespace {

// The instance's `cost` field as the game keeps it, which is NOT the same thing
// as its `costForTurn` (our cost_now). The engine stores one cost per instance,
// and its two writers are distinguishable by the flag they leave behind:
//   * setCostForTurn moves costForTurn ONLY and marks the instance
//     COST_MODIFIED_FOR_TURN (the end-turn sweep restores costForTurn = cost from
//     the registry), so for such an instance `cost` is still the registry value;
//   * every other cost write here mirrors the game writing cost and costForTurn
//     TOGETHER (Confusion, and op_madness below), leaving no flag -- so cost_now
//     IS `cost`.
// Madness needs the two predicates separately, which is the only reason this
// distinction has to be reconstructed at all.
[[nodiscard]] int instance_base_cost(const CombatState& s,
                                     CardPoolIndex pi) noexcept {
    const CardInstance& c = s.card_pool[pi];
    if (has_card_flag(c.flags, CardFlag::COST_MODIFIED_FOR_TURN)) {
        const CardDef* def = card_def(static_cast<CardId>(c.card_id));
        return def == nullptr ? 0 : static_cast<int>(card_cost(*def, c.upgrade));
    }
    return static_cast<int>(c.cost_now);
}

}  // namespace

// MADNESS (MadnessAction.update, :26-65). Two halves:
//
//   (1) The GUARD (:29-42). Walk the hand once: a card with costForTurn > 0 sets
//       `betterPossible`; otherwise a card with cost > 0 sets `possible`. Note
//       the `continue` in the first branch -- a card only reaches the `cost > 0`
//       test when its costForTurn is already <= 0. If NEITHER flag ends up set
//       the action does nothing and draws NO cardRandomRng at all.
//
//   (2) findAndModifyCard(better) (:46-65). One
//       hand.getRandomCard(cardRandomRng) draw -- group.get(rng.random(size-1)),
//       CardGroup.java:498-500, i.e. exactly one card_random_rng draw over the
//       inclusive range [0, hand_count-1]. If the drawn card fails the branch's
//       predicate (costForTurn > 0 when `better`, else cost > 0) the method
//       RECURSES, which is another full draw. The draw count is therefore the
//       number of rejection-sampling attempts, not one.
//
// The loop terminates because the guard proved at least one hand card satisfies
// the very predicate the chosen branch tests: `better` is set only by a
// costForTurn > 0 card, and the !better branch is reached only when `possible`
// found a cost > 0 card.
//
// The zeroing writes BOTH c.cost and c.costForTurn (:50-52 / :58-60), i.e. it is
// PERMANENT for the combat and not this-turn-only. So it clears
// COST_MODIFIED_FOR_TURN as well as setting cost_now: leaving that bit set would
// let the end-of-turn sweep restore the registry cost, which is the one outcome
// the game rules out (isCostModified, which it does set, is display state with no
// reset path). `superFlash` is presentation.
void op_madness(CombatState& s) noexcept {
    bool better_possible = false;
    bool possible = false;
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        const CardPoolIndex pi = s.hand[i];
        if (s.card_pool[pi].cost_now > 0) {
            better_possible = true;
            continue;
        }
        if (instance_base_cost(s, pi) <= 0) {
            continue;
        }
        possible = true;
    }
    if (!better_possible && !possible) {
        return;  // MadnessAction.java:39 -- findAndModifyCard is never called
    }
    const bool better = better_possible;
    for (;;) {
        const int32_t pick =
            random(s.card_random_rng, static_cast<int32_t>(s.hand_count) - 1);
        const CardPoolIndex pi = s.hand[static_cast<unsigned>(pick)];
        const bool eligible = better ? (s.card_pool[pi].cost_now > 0)
                                     : (instance_base_cost(s, pi) > 0);
        if (!eligible) {
            continue;  // the recursive call -- another cardRandomRng draw
        }
        s.card_pool[pi].cost_now = 0;
        s.card_pool[pi].flags = static_cast<uint16_t>(
            s.card_pool[pi].flags &
            ~card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN));
        return;
    }
}

// --- Public: CHOOSE_CARD queries ---------------------------------------------

bool choice_slot_eligible(const CombatState& s, uint8_t slot,
                          ChoiceKind kind) noexcept {
    if (kind == ChoiceKind::EXHAUST_TO_HAND) {
        // Exhume. Three of ExhumeAction's early-outs collapse into eligibility:
        //   * a FULL hand kills the whole action (ExhumeAction.java:40-43), so no
        //     slot is eligible while hand_count == kHandCap;
        //   * an EMPTY exhaust pile ends it (:45-48) -- no slots to be eligible;
        //   * every Exhume copy is lifted out of the grid before the select opens
        //     (:74-80) and put back afterwards (:105), and the one-card branch
        //     likewise refuses when that one card is an Exhume (:50-53).
        // With no eligible slot the choice resolves to nothing, which is exactly
        // what all three early-outs do.
        if (s.hand_count >= kHandCap || slot >= s.exhaust_count) {
            return false;
        }
        return s.card_pool[s.exhaust[slot]].card_id !=
               static_cast<uint16_t>(CardId::EXHUME);
    }
    if (choice_source_is_discard(kind)) {
        // Discard-to-draw-top: any discard card is a legal pick
        // (DiscardPileToTopOfDeckAction has no eligibility filter). The
        // just-played source card is in the LIMBO pile, never in this one.
        return slot < s.discard_count;
    }
    if (slot >= s.hand_count) {
        return false;
    }
    if (kind == ChoiceKind::UPGRADE) {
        // AbstractCard.canUpgrade (AbstractCard.java:672-680) in order:
        //   type == CURSE  -> false
        //   type == STATUS -> false
        //   otherwise      -> !upgraded
        // SearingBlow.canUpgrade (SearingBlow.java:58-60) OVERRIDES the whole
        // base method with `return true`, so it is tested FIRST here -- an
        // override runs instead of, not after, the base body. (Searing Blow is
        // an ATTACK, so the type rejections would not have fired for it either
        // way; the ordering is written to mirror Java dispatch, not to rely on
        // that coincidence.) Its unbounded timesUpgraded is a u8 here, so the
        // only extra guard is saturation.
        const CardInstance& c = s.card_pool[s.hand[slot]];
        if (c.card_id == static_cast<uint16_t>(CardId::SEARING_BLOW)) {
            return c.upgrade != UINT8_MAX;
        }
        // Curses and statuses are never upgrade targets. This is reachable in
        // combat: Writhe is an innate curse that opens in hand, and Wound /
        // Burn / Slimed enter hand mid-combat. Beyond mis-targeting, the
        // eligible COUNT feeds choice_requires_user / op_choose_card's
        // forced-vs-prompted branch (ArmamentsAction.java:46-60 counts exactly
        // the canUpgrade() cards), so an inflated count would change whether a
        // hand-select screen opens at all.
        const CardDef* def = card_def(static_cast<CardId>(c.card_id));
        if (def == nullptr || def->type == CardType::CURSE ||
            def->type == CardType::STATUS) {
            return false;
        }
        return c.upgrade == 0;
    }
    if (kind == ChoiceKind::DUPLICATE) {
        // Dual Wield: only ATTACK or POWER cards can be duplicated
        // (DualWieldAction.isDualWieldable:95-97).
        const CardDef* def = card_def(
            static_cast<CardId>(s.card_pool[s.hand[slot]].card_id));
        return def != nullptr &&
               (def->type == CardType::ATTACK || def->type == CardType::POWER);
    }
    return true;  // EXHAUST / PUT_ON_DRAW_TOP accept any hand card
}

bool choice_requires_user(const CombatState& s,
                          const ActionQueueItem& item) noexcept {
    if (static_cast<Opcode>(item.opcode) != Opcode::CHOOSE_CARD) {
        return false;
    }
    if (item.amount <= 0 || choose_is_random(item.flags)) {
        return false;  // nothing left to pick / auto-rolled -- never blocks
    }
    const ChoiceKind kind = choose_kind_from_flags(item.flags);
    return count_eligible(s, kind) > item.amount;
}

void apply_choice_selection(CombatState& s, uint8_t slot, ChoiceKind kind,
                            int copies, bool prompted) noexcept {
    if (kind == ChoiceKind::DUPLICATE && prompted) {
        // DualWieldAction's PROMPTED branch (DualWieldAction.java:59-84): the
        // screen removed the ineligible cards from the hand up front and
        // returnCards() re-appends them at the END (hand.addToTop == list append,
        // :88-92) after retrieval; the SELECTED card left the hand into
        // selectedCards, is cleared, and 1 + dupeAmount stat-equivalent copies
        // are Make'd into the hand (:74-79). Net observable hand:
        //   [other eligibles, original relative order] +
        //   [ineligibles, original relative order] +
        //   [selected-equivalent + `copies` clones].
        // We keep the ORIGINAL instance as the "selected-equivalent" (a
        // makeStatEquivalentCopy is field-identical to it) and append `copies`
        // clones -- byte-identical piles, one fewer pool row consumed.
        if (slot >= s.hand_count) {
            return;
        }
        const CardPoolIndex sel = s.hand[slot];
        CardPoolIndex reordered[kHandCap];
        uint8_t n = 0;
        for (uint8_t i = 0; i < s.hand_count; ++i) {  // eligibles except selected
            if (i != slot && choice_slot_eligible(s, i, ChoiceKind::DUPLICATE)) {
                reordered[n++] = s.hand[i];
            }
        }
        for (uint8_t i = 0; i < s.hand_count; ++i) {  // then the ineligibles
            if (i != slot && !choice_slot_eligible(s, i, ChoiceKind::DUPLICATE)) {
                reordered[n++] = s.hand[i];
            }
        }
        reordered[n++] = sel;                          // selected at the end
        for (uint8_t i = 0; i < n; ++i) {
            s.hand[i] = reordered[i];
        }
        s.hand_count = n;
        for (int k = 0; k < copies; ++k) {
            clone_card_to_hand(s, sel);
        }
        return;
    }
    apply_choice_to_slot(s, slot, kind, copies);
}

}  // namespace sts::engine
