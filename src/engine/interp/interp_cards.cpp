// CARD-MANIPULATION-domain opcode bodies -- everything that creates, moves,
// re-costs or exhausts card instances, plus the public CHOOSE_CARD queries that
// share their eligibility rules (moved verbatim out of interp.cpp's anonymous
// namespace and its public section; see interp_ops.hpp for the split's
// rationale).

#include "interp_cards.hpp"

#include <cstdint>

#include "sts/engine/action_queue.hpp"
#include "sts/engine/card_play.hpp"     // roll_random_target (dequeue-time random enemy)
#include "sts/engine/cards.hpp"         // card_def / card_cost / card_flags (MAKE_CARD)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/piles.hpp"         // exhaust_card / shuffle_discard_into_draw
#include "sts/engine/rng_stream.hpp"    // random (DRAW_RANDOM insert position)
#include "sts/engine/types.hpp"

namespace sts::engine {

namespace {

// --- CHOOSE_CARD helpers (Stage B B3.4) -------------------------------------

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

// B3.6 (Dual Wield): add one stat-equivalent clone of pool instance `src_pi` to
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
    const uint8_t src_count =
        choice_source_is_discard(kind) ? s.discard_count : s.hand_count;
    if (slot >= src_count) {
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
    }
}

// The eligible source-pile-card count for a CHOOSE_CARD of `kind` (hand slots for
// the hand kinds, discard slots for discard-to-draw-top; `excluded` drops the
// just-played source card from a discard-source count).
[[nodiscard]] int count_eligible(const CombatState& s, ChoiceKind kind,
                                 uint8_t excluded) noexcept {
    const uint8_t src_count =
        choice_source_is_discard(kind) ? s.discard_count : s.hand_count;
    int n = 0;
    for (uint8_t i = 0; i < src_count; ++i) {
        if (choice_slot_eligible(s, i, kind, excluded)) {
            ++n;
        }
    }
    return n;
}

// Remove pool index `idx` from the discard pile if present; return true if it was
// removed (so the caller can restore it). Used to lift the just-played source card
// (Havoc) out of the deck during its own PLAY_TOP_DRAW (see op_play_top_draw).
bool discard_remove(CombatState& s, CardPoolIndex idx) noexcept {
    for (uint8_t i = 0; i < s.discard_count; ++i) {
        if (s.discard[i] == idx) {
            for (uint8_t j = static_cast<uint8_t>(i + 1); j < s.discard_count; ++j) {
                s.discard[j - 1] = s.discard[j];
            }
            --s.discard_count;
            return true;
        }
    }
    return false;
}

}  // namespace

// --- Opcode bodies ----------------------------------------------------------

// MAKE_CARD: create `count` copies of `id` into `pile` (Stage B B3.1). Each copy
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
    const bool from_discard = choice_source_is_discard(kind);
    const uint8_t excluded = choice_excluded_index(item);
    const int eligible = count_eligible(s, kind, excluded);
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
        const uint8_t sc = from_discard ? s.discard_count : s.hand_count;
        const CardPoolIndex* src_pile = from_discard ? s.discard : s.hand;
        for (uint8_t i = 0; i < sc && m < kHandCap; ++i) {
            if (choice_slot_eligible(s, i, kind, excluded)) {
                picked[m++] = src_pile[i];
            }
        }
        for (int k = 0; k < m; ++k) {
            // Re-find the slot each time (earlier applies may have shifted the pile).
            const uint8_t sc2 = from_discard ? s.discard_count : s.hand_count;
            const CardPoolIndex* sp2 = from_discard ? s.discard : s.hand;
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
// exhaust it. `exclude` is the pool index of the SOURCE card (Havoc): in the game
// the source is cardInUse (limbo) during PlayTopCardAction and is moved to discard
// only afterwards (AbstractPlayer.useCard: removeCard + cardInUse, then the queued
// UseCardAction discards), so it is NOT a replay candidate. Our synchronous
// resolve_card_play already moved it to discard, so lift it out for the duration
// and restore it after -- reproducing the limbo state exactly. The card_random_rng
// monster-target roll happens FIRST and UNCONDITIONALLY (getRandomMonster is
// evaluated as Havoc.use()'s argument), then the empty / reshuffle checks.
void op_play_top_draw(CombatState& s, int exclude) noexcept {
    const bool restore = (exclude >= 0 && exclude < kCardPoolCap)
                             ? discard_remove(s, static_cast<CardPoolIndex>(exclude))
                             : false;
    const CardPoolIndex excl = static_cast<CardPoolIndex>(exclude);
    auto restore_source = [&]() noexcept {
        if (restore && s.discard_count < kDiscardCap) {
            s.discard[s.discard_count++] = excl;
        }
    };

    // getRandomMonster(null, true, cardRandomRng): one card_random_rng draw over
    // the live monsters (no draw if none alive -- combat is over in that case).
    const uint8_t target = roll_random_target(s);
    if (s.draw_count == 0 && s.discard_count == 0) {
        restore_source();
        return;  // deckSize + discardSize == 0 -> nothing to play (:34-36)
    }
    if (s.draw_count == 0) {
        shuffle_discard_into_draw(s);  // EmptyDeckShuffleAction (one shuffle_rng draw)
        if (s.draw_count == 0) {
            restore_source();
            return;
        }
    }
    const CardPoolIndex pi = s.draw[s.draw_count - 1];  // getTopCard
    --s.draw_count;
    // exhaustOnUseOnce = true (:48) -> force the played card to exhaust; play it
    // free (autoplay does not pay energy). Put it in hand so resolve_card_play's
    // hand->pile move finds it, then queue its play at the front of the cardQueue
    // (NewQueueCardAction -> the normal §5.3 resolve).
    s.card_pool[pi].flags |= card_flag_bit(CardFlag::EXHAUST);
    s.card_pool[pi].cost_now = 0;
    if (s.hand_count < kHandCap) {
        s.hand[s.hand_count++] = pi;
    }
    CardQueueItem q{};
    q.card_index = pi;
    q.target = target;
    add_card_to_queue_top(s, q);
    restore_source();
}

// SET_COST: set card_pool[src].cost_now = amount (Stage B B3.1 cost-modifier
// write path). Clamped to the u8 cost_now range. The "temporary" (per-turn
// reset) and "which card / under what condition" logic belongs to the consumer
// (a power hook -- B3.2 -- or a CHOOSE selection); this is the write primitive.
//
// CURRENTLY UNREACHABLE (verified at the cleanup-interp refactor): nothing
// queues Opcode::SET_COST -- no site in src/ builds such an item, and no
// registry YAML authors `{op: SET_COST}`, because the operand is a card-POOL
// index (`src`) that a YAML author has no way to name. The opcode is retained
// (opcodes are append-only) and the body stays live for the deferred SET_COST
// authoring obligation. It becomes reachable as soon as a C++ consumer that
// already holds a pool index -- a power hook (the B3.2 cost-modifier path) or a
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

// --- Public: CHOOSE_CARD queries (Stage B B3.4) ------------------------------

bool choice_slot_eligible(const CombatState& s, uint8_t slot, ChoiceKind kind,
                          uint8_t excluded) noexcept {
    if (choice_source_is_discard(kind)) {
        // Discard-to-draw-top: any discard card is a legal pick
        // (DiscardPileToTopOfDeckAction has no eligibility filter) EXCEPT the
        // just-played source card (in limbo in the game, not the discard).
        return slot < s.discard_count && s.discard[slot] != excluded;
    }
    if (slot >= s.hand_count) {
        return false;
    }
    if (kind == ChoiceKind::UPGRADE) {
        // Most cards: canUpgrade() == !upgraded. SearingBlow.canUpgrade always
        // returns true, so its u8 upgrade count remains eligible until saturated.
        const CardInstance& c = s.card_pool[s.hand[slot]];
        if (c.card_id == static_cast<uint16_t>(CardId::SEARING_BLOW)) {
            return c.upgrade != UINT8_MAX;
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
    return count_eligible(s, kind, choice_excluded_index(item)) > item.amount;
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
