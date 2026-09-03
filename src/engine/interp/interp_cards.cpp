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
#include "sts/engine/knowledge.hpp"     // draw-pile knowledge hooks (observers)
#include "sts/engine/piles.hpp"         // exhaust_card / shuffle_discard_into_draw / limbo
#include "sts/engine/power_hooks.hpp"   // dispatch_on_exhaust (USE_CARD filing)
#include "sts/engine/relic_hooks.hpp"   // player_has_relic (Strange Spoon)
#include "sts/engine/rng_jdk.hpp"       // JdkRandom / jdk_shuffle (temp-list shuffle)
#include "sts/engine/rng_stream.hpp"    // random / random_boolean (spoon roll)
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"    // card_game_id (APPLY_STASIS's sort key)

#include <span>
#include <string_view>

namespace sts::engine {

namespace {

// A replay copy dequeues after the original X card has spent its energy, so
// NewQueueCardAction must snapshot the original energyOnUse. This storage is
// deliberately restricted to a fresh purge-only copy: no persistent card's
// misc semantics (Rampage, etc.) are overwritten, and the transient row cannot
// reach a later ordinary play. Draw-top autoplay is front-queued and can read
// current energy at dequeue, so it needs no storage.
void capture_purge_copy_x_energy(CombatState& s, CardPoolIndex pi,
                                 uint32_t play_flags) noexcept {
    if ((play_flags & (kPlayCardCopy | kPlayCardPurge)) !=
            (kPlayCardCopy | kPlayCardPurge) ||
        !has_card_flag(s.card_pool[pi].flags, CardFlag::XCOST)) {
        return;
    }
    int energy = s.player_energy;
    if (energy < 0) {
        energy = 0;
    }
    s.card_pool[pi].misc = static_cast<uint16_t>(energy);
    // The two misc overloads are mutually exclusive by construction (no X-cost
    // card grows a uuid-shared counter), and this one wins the word if a future
    // row ever managed both -- clearing the link keeps misc_group_row honest
    // rather than letting it read an energy value as a pool index.
    s.card_pool[pi].flags = static_cast<uint16_t>(
        (s.card_pool[pi].flags &
         ~card_flag_bit(CardFlag::REPLAY_MISC_LINK)) |
        card_flag_bit(CardFlag::AUTOPLAY_X_ENERGY));
}

// Does this card's `misc` hold a mid-combat counter that the game grows through
// GetAllInBattleInstances.get(uuid) -- i.e. a number a makeSameInstanceOf replay
// copy SHARES with its original rather than snapshots? Exactly two rows qualify:
// Rampage (DAMAGE_RAMPAGE, ModifyDamageAction.java:26-33) and Ritual Dagger
// (RITUAL_DAGGER / initial_misc, RitualDaggerAction.java:39-46). Everything else
// either leaves misc at zero or uses it as the transient AUTOPLAY_X_ENERGY
// snapshot, which is per-copy by construction -- so the link is stamped only
// here and every other replay copy's row stays byte-identical to before.
[[nodiscard]] bool card_misc_is_uuid_shared(const CardDef& def,
                                            uint8_t upgrade) noexcept {
    if (def.initial_misc != 0) {
        return true;  // Ritual Dagger: the ctor seeds it, the kill grows it
    }
    const CardEffectView eff = card_effect_steps(def, upgrade);
    for (uint8_t k = 0; k < eff.count; ++k) {
        const auto op = static_cast<Opcode>(eff.steps[k].op);
        if (op == Opcode::DAMAGE_RAMPAGE || op == Opcode::RITUAL_DAGGER) {
            return true;
        }
    }
    return false;
}

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
        case ChoiceSource::DRAW:
            return s.draw_count;
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
        case ChoiceSource::DRAW:
            return s.draw;
        case ChoiceSource::HAND:
        default:
            return s.hand;
    }
}

// Does pool instance `pi` have CardType `type` (a raw CardType byte)?
[[nodiscard]] bool instance_has_type(const CombatState& s, CardPoolIndex pi,
                                     uint8_t type) noexcept {
    const CardDef* def = card_def(static_cast<CardId>(s.card_pool[pi].card_id));
    return def != nullptr && static_cast<uint8_t>(def->type) == type;
}

// CardGroup.addToRandomSpot into a FRESH `new CardGroup(UNSPECIFIED)` -- the
// browse list SkillFromDeckToHandAction (:35-39),
// AttackFromDeckToHandAction (:35-39) and DrawPileToHandAction (:37-41) all
// build the same way, walking the REAL draw pile front to back
// (`for (AbstractCard c : this.p.drawPile.group)`) and inserting every
// type-matching card.
//
// CardGroup.addToRandomSpot (CardGroup.java:463-469) in full:
//
//     if (this.group.size() == 0) { this.group.add(c); }
//     else { this.group.add(AbstractDungeon.cardRandomRng.random(
//                               this.group.size() - 1), c); }
//
// so the EMPTY-group branch is a plain append that draws NOTHING, and every
// later insert spends exactly ONE cardRandomRng draw over the inclusive range
// [0, size-1]. k matching cards therefore cost exactly k-1 draws. (Note the
// insert index can never be `size`, so a card is never appended past the end
// once the group is non-empty -- that skew is part of the browse order and is
// reproduced here rather than approximated by a uniform shuffle.)
//
// Returns the temp group's size; `tmp` must have room for kDrawCap entries.
// s.draw[i] is the same list index as drawPile.group.get(i) (index 0 == bottom,
// draw_count-1 == top), so the walk order matches the Java's iteration order.
int build_draw_temp_group(CombatState& s, uint8_t type,
                          CardPoolIndex* tmp) noexcept {
    int n = 0;
    for (uint8_t i = 0; i < s.draw_count; ++i) {
        const CardPoolIndex pi = s.draw[i];
        if (!instance_has_type(s, pi, type)) {
            continue;
        }
        if (n == 0) {
            tmp[n++] = pi;  // group.size() == 0 -> plain add, NO rng
            continue;
        }
        const int32_t pos = random(s.card_random_rng, n - 1);  // ONE draw
        for (int j = n; j > pos; --j) {
            tmp[j] = tmp[j - 1];
        }
        tmp[pos] = pi;
        ++n;
    }
    return n;
}

// Remove pool instance `pi` from the draw pile (shifting the tail down). Returns
// false when it is not there (defensive).
bool remove_from_draw(CombatState& s, CardPoolIndex pi) noexcept {
    for (uint8_t i = 0; i < s.draw_count; ++i) {
        if (s.draw[i] != pi) {
            continue;
        }
        for (uint8_t j = static_cast<uint8_t>(i + 1); j < s.draw_count; ++j) {
            s.draw[j - 1] = s.draw[j];
        }
        --s.draw_count;
        return true;
    }
    return false;
}

// drawPile.removeCard(card) + hand.addToTop(card), with the "hand is full"
// redirect the deck-to-hand actions share: at hand.size() == 10 the card is
// moveToDiscardPile'd out of the draw pile instead and a hand-is-full dialog is
// shown (SkillFromDeckToHandAction.java:46-48 / :72-74,
// DrawPileToHandAction.java:51-55). The card is CONSUMED either way -- it always
// leaves the draw pile.
void draw_card_to_hand_or_discard(CombatState& s, CardPoolIndex pi) noexcept {
    if (!remove_from_draw(s, pi)) {
        return;
    }
    // Knowledge observer: a player-visible browse pick removed `pi` from a
    // known pile position (knowledge.hpp).
    knowledge_on_remove_known(s, pi);
    if (s.hand_count < kHandCap) {
        // hand.addToTop (SkillFromDeckToHandAction.java:44 and its siblings) --
        // NOT hand.addToHand through ShowCardAndAddToHandEffect, so there is no
        // onCardDrawOrDiscard step on this arm.
        s.hand[s.hand_count++] = pi;  // hand.addToTop == append
    } else if (s.discard_count < kDiscardCap) {
        s.discard[s.discard_count++] = pi;
        // moveToDiscardPile -> Soul.update's DISCARD_PILE arm (piles.hpp).
        reset_cost_for_turn(s, pi);
        // ... and CardGroup.moveToDiscardPile:841's onCardDrawOrDiscard, which
        // is what makes this hand-full redirect a Corruption sweep point.
        corruption_hand_cost_sweep(s);
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

}  // namespace

// The three cost primitives below are declared in interp_cards.hpp rather than
// kept TU-private: every Java `c.cost` / `setCostForTurn` / onCardDrawOrDiscard
// site outside this file needs them (CorruptionPower.onCardDraw in
// powers/power_corruption.cpp, MummifiedHand.onUseCard in
// relics/relics_uncommon.cpp, and the pile moves in piles.cpp / interp.cpp).
// Re-deriving "which of the two cost fields does this read" at each of those
// sites is exactly how they drifted apart in the first place.

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
    const int base = instance_base_cost(s, pi);
    const int registry_base = static_cast<int>(card_cost(*def, c.upgrade));
    if (base != registry_base &&
        !has_card_flag(c.flags, CardFlag::SAVED_BASE_COST)) {
        const uint16_t encoded = static_cast<uint16_t>(
            static_cast<uint16_t>(base > 7 ? 7 : base) <<
            kSavedBaseCostShift);
        c.flags = static_cast<uint16_t>(
            (c.flags & ~kSavedBaseCostMask) |
            card_flag_bit(CardFlag::SAVED_BASE_COST) | encoded);
    }
    c.cost_now = static_cast<uint8_t>(amount);
    if (amount != base) {
        c.flags = static_cast<uint16_t>(
            c.flags | card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN));
    }
}

// Reconstruct AbstractCard.cost (as distinct from costForTurn/cost_now).
// A COST_MODIFIED_FOR_TURN instance still has its registry base cost; every
// other in-combat permanent writer changes both fields, so cost_now is cost.
[[nodiscard]] int instance_base_cost(const CombatState& s,
                                     CardPoolIndex pi) noexcept {
    const CardInstance& c = s.card_pool[pi];
    if (has_card_flag(c.flags, CardFlag::SAVED_BASE_COST)) {
        return static_cast<int>(saved_base_cost(c.flags));
    }
    if (has_card_flag(c.flags, CardFlag::COST_MODIFIED_FOR_TURN)) {
        const CardDef* def = card_def(static_cast<CardId>(c.card_id));
        return def == nullptr ? 0 : static_cast<int>(card_cost(*def, c.upgrade));
    }
    return static_cast<int>(c.cost_now);
}

// AbstractPlayer.onCardDrawOrDiscard's Corruption branch (AbstractPlayer.java:
// 1341-1356, the branch at :1348-1352):
//
//     if (this.hasPower("Corruption")) {
//         for (AbstractCard c : this.hand.group) {
//             if (c.type != CardType.SKILL || c.costForTurn == 0) continue;
//             c.modifyCostForCombat(-9);
//         }
//     }
//
// ShowCardAndAddToHandEffect's two constructors run it (:47 / :69) right after
// `hand.addToHand(card)` (:43 / :65), so a SKILL created into the hand while
// Corruption is up is swept HERE, before the constructor's own trailing
// `setCostForTurn(-9)` (:48-50 / :70-72) -- which then finds costForTurn already
// 0 and, per setCostForTurn's body, changes nothing.
//
// The distinction that matters is which of the two cost fields moves.
// modifyCostForCombat(-9) on a costForTurn > 0 card takes the FIRST arm
// (AbstractCard.java:2013-2022): costForTurn += -9, clamped to 0, and then
// `this.cost = this.costForTurn`. BOTH fields land on 0 and the card is 0-cost
// for the whole COMBAT, not for this turn -- so no end-of-turn sweep restores
// it. CorruptionPower.onCardDraw (CorruptionPower.java:37-42) is the other,
// weaker shape (setCostForTurn(-9), this turn only) and does NOT reach a created
// card: onCardDraw fires only on the draw path (AbstractPlayer.java:1645-1650).
//
// In the engine's encoding, AbstractCard.cost is reconstructed by
// instance_base_cost, so "cost = costForTurn = 0" is cost_now 0 with NEITHER
// COST_MODIFIED_FOR_TURN nor SAVED_BASE_COST set -- clearing them is what makes
// the write permanent rather than a this-turn discount.
//
// The sweep is over the WHOLE hand, as in the Java (:1349 walks hand.group), not
// just the newcomer -- and the live witness is a case where the newcomer is not
// the card that moves at all. Capture s2v3_wave1_STS239327_ps13
// (run_STS239327_a20_ironclad.jsonl), floor 50, the Act-3 double boss: the
// player holds Corruption and a Necronomicurse, so every skill played is
// exhausted and every exhaust of the curse re-creates it in hand through
// MakeTempCardInHandAction (Necronomicurse.java:43-49). The newcomer is a CURSE
// and is never swept; the resident skills are. Pre-fix,
// `replay_run_diff --replay --combat` reported at seq 598
//
//     hand capture: Bite@1 Blood for Blood+@0 Juggernaut@2 ... Armaments@0 ...
//     hand sim:     Bite@1 Blood for Blood+@0 Juggernaut@2 ... Armaments@1 ...
//
// the same for Ghostly Armor at seq 600, and the by-slot
// `card_pool[29].cost_now: 0 -> 1` / `card_pool[30].cost_now: 0 -> 1` field diffs
// on every record from seq 598 to 635 -- the wrong cost then rode into the
// exhaust pile, because exhaust/end-of-turn resetAttributes restores
// AbstractCard.cost (:2043) and the engine's was still the registry 1.
//
// XCOST rows carry cost_now 0 and are skipped here, which is what
// `costForTurn == 0` does for the Java's -1 sentinel -- and correctly so:
// modifyCostForCombat's second arm would need cost >= 0, and an X-cost card's is
// -1.
//
// EVERY ENTRY POINT. onCardDrawOrDiscard has exactly five callers in the game
// (grep: the method is called nowhere else):
//
//   ShowCardAndAddToHandEffect.java:47 / :69  -- a card created INTO the hand
//   CardGroup.moveToDiscardPile        :841   -- ANY move into the discard pile
//   CardGroup.moveToExhaustPile        :861   -- ANY move into the exhaust pile
//   AbstractPlayer.draw()              :1664  -- after EACH single card drawn
//   UseCardAction.java                 :124   -- the returnToHand branch, which
//                                                this engine deliberately does
//                                                not model (op_use_card's
//                                                comment at the filing switch)
//
// and each of the first four is called from the engine's mirror of that Java
// site. The sweep is idempotent by construction -- it only touches a SKILL whose
// cost_now is still > 0 -- so a Java path the engine collapses into one step
// (an exhaust that is also a hand move) cannot double-apply.
//
// `visible_hand` is NOT a Java concept: it is the number of leading `s.hand`
// slots that correspond to the game's hand.group AT THIS INSTANT, and it exists
// for the one engine site whose hand is ahead of the game's. AbstractPlayer.draw
// interleaves (draw one card -> its onCardDraw -> this sweep -> draw the next),
// while the engine's DRAW opcode draws the whole batch first and then walks the
// new cards firing hooks; without the bound, the first card's sweep would see
// cards 2..n that the game has not drawn yet and permanently zero a SKILL whose
// own onCardDraw was about to give it a THIS-TURN zero instead. Passing
// `before + i + 1` reproduces the game's hand.group exactly at each step.
void corruption_hand_cost_sweep(CombatState& s, uint8_t visible_hand) noexcept {
    if (!player_carries_power(s, PowerId::CORRUPTION)) {
        return;
    }
    const uint8_t n = s.hand_count < visible_hand ? s.hand_count : visible_hand;
    for (uint8_t i = 0; i < n; ++i) {
        CardInstance& c = s.card_pool[s.hand[i]];
        const CardDef* def = card_def(static_cast<CardId>(c.card_id));
        if (def == nullptr || def->type != CardType::SKILL || c.cost_now == 0) {
            continue;
        }
        // modifyCostForCombat(-9), the costForTurn > 0 arm (:2014-2022): both
        // fields to 0, permanently -- so every this-turn marker is cleared.
        c.cost_now = 0;
        c.flags = static_cast<uint16_t>(
            c.flags & ~card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN) &
            ~card_flag_bit(CardFlag::SAVED_BASE_COST) & ~kSavedBaseCostMask);
    }
}

namespace {

// Allocate a fresh library copy and add it to the hand, spilling to discard at
// the hand cap. Discovery optionally applies setCostForTurn(0); Jack of All
// Trades leaves the registry cost unchanged.
//
// `upgraded` is TransmutationAction's `if (this.upgraded) c.upgrade()`
// (TransmutationAction.java:46-48): the copy is generated at upgrade level 1, so
// its registry cost/flags come from the UPGRADED row -- and, because the Java
// upgrades BEFORE setCostForTurn(0) (:49), that is the cost the this-turn zero
// replaces and the cost the end-of-turn sweep restores.
//
// The `free_this_turn && cost != 0` guard is also the X-cost story:
// setCostForTurn is a no-op while costForTurn < 0 (AbstractCard.java:2002), and
// an X-cost row carries CardFlag::XCOST with a registry cost of 0, so a
// generated X-cost card (Transmutation can generate another Transmutation) keeps
// its X-cost play semantics and gains no this-turn marker.
void add_library_copy_to_hand(CombatState& s, CardId id, bool free_this_turn,
                              bool upgraded = false) noexcept {
    const CardDef* def = card_def(id);
    if (def == nullptr) {
        return;
    }
    int slot = -1;
    for (int i = 0; i < kCardPoolCap; ++i) {
        if (s.card_pool[i].card_id == static_cast<uint16_t>(CardId::NONE)) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return;
    }
    const uint8_t upg = upgraded ? 1 : 0;
    CardInstance& c = s.card_pool[slot];
    c.card_id = static_cast<uint16_t>(id);
    c.upgrade = upg;
    c.cost_now = free_this_turn ? 0 : card_cost(*def, upg);
    c.flags = card_flags(*def, upg);
    if (free_this_turn && card_cost(*def, upg) != 0) {
        c.flags = static_cast<uint16_t>(
            c.flags | card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN));
    }
    c.misc = 0;
    const CardPoolIndex pi = static_cast<CardPoolIndex>(slot);
    if (s.hand_count < kHandCap) {
        s.hand[s.hand_count++] = pi;
        // ShowCardAndAddToHandEffect:47 / :69 -- the Corruption sweep. A
        // free_this_turn copy is already at cost_now 0 and the sweep skips it
        // (DiscoveryAction's own setCostForTurn(0) at :61-62 runs BEFORE the
        // effect, exactly as here); a Jack of All Trades colorless SKILL added
        // at its registry cost is the arm that actually moves.
        corruption_hand_cost_sweep(s);
    } else if (s.discard_count < kDiscardCap) {
        // ShowCardAndAddToDiscardEffect (DiscoveryAction.java:69 / :77 / :79-80)
        // has no onCardDrawOrDiscard step at all.
        s.discard[s.discard_count++] = pi;
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

// AbstractCard.canUpgrade (AbstractCard.java:672-680) in order:
//   type == CURSE  -> false
//   type == STATUS -> false
//   otherwise      -> !upgraded
// SearingBlow.canUpgrade (SearingBlow.java:58-60) OVERRIDES the whole base
// method with `return true`, so it is tested FIRST here -- an override runs
// INSTEAD OF, not after, the base body. Shared by choice_slot_eligible's
// UPGRADE kind (Armaments, a single hand card) and op_upgrade_all (Apotheosis,
// all four piles) so the two never drift apart.
[[nodiscard]] bool can_upgrade_instance(const CombatState& s,
                                        CardPoolIndex pi) noexcept {
    const CardInstance& c = s.card_pool[pi];
    if (c.card_id == static_cast<uint16_t>(CardId::SEARING_BLOW)) {
        return c.upgrade != UINT8_MAX;
    }
    const CardDef* def = card_def(static_cast<CardId>(c.card_id));
    if (def == nullptr || def->type == CardType::CURSE ||
        def->type == CardType::STATUS) {
        return false;
    }
    return c.upgrade == 0;
}

// Upgrade the pool instance in place (in-combat upgrade: ArmamentsAction,
// Warped Tongs' UpgradeRandomCardAction, Apotheosis -- c.upgrade() +
// applyPowers()). `upgrade` is a count.
//
// COST. `AbstractCard.upgrade()` is per-card, and the ONLY way it can touch the
// cost is `upgradeBaseCost` (AbstractCard.java:725-735) -- which the great
// majority of cards never call. PowerThrough.upgrade (PowerThrough.java:40-45)
// is upgradeName + upgradeBlock and nothing else, so an upgrade of a Power
// Through leaves BOTH `cost` and `costForTurn` exactly where they were.
// Re-seeding cost_now from the registry row unconditionally clobbered that:
// STS128113 ps27 floor 27 is the live witness -- Snecko's Confusion rolled the
// drawn Power Through to 2 (a PERMANENT write, `costForTurn = cost = newCost`,
// ConfusionPower.java:43), Warped Tongs then upgraded it, and the engine reset
// it to the registry 1 while the game held 2. STS101166 ps0 floor 20 is the
// same defect from the other side: Armaments upgraded a Bash that Mummified
// Hand had set to costForTurn 0, and the live capture shows `Bash+(cost 0)`
// where the engine showed 2.
//
// So the base cost moves only when the card really calls upgradeBaseCost, which
// is exactly when the registry cost differs across the two upgrade levels (a
// call with an unchanged argument is a behavioural no-op: diff is preserved and
// re-applied to the same base). Blood for Blood is the one card whose argument
// is RELATIVE to its current, combat-reduced cost rather than a literal
// (BloodForBlood.java:45-57), so it keeps its own arm.
//
// FLAGS. Only the AUTHORED half is re-read from the upgraded row
// (kAuthoredCardFlagMask, types.hpp): Apparition+ drops ETHEREAL, and nothing
// in upgrade() can reach freeToPlayOnce / purgeOnUse / exhaustOnUseOnce or the
// cost bookkeeping bits, which a blind re-seed silently wiped.
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
    const int old_base = instance_base_cost(s, pi);
    const int old_cost_for_turn = static_cast<int>(c.cost_now);
    const uint8_t old_upgrade = c.upgrade;
    ++c.upgrade;

    // Authored bits from the new row; every per-instance runtime bit survives.
    c.flags = static_cast<uint16_t>(
        (card_flags(*def, c.upgrade) & kAuthoredCardFlagMask) |
        (c.flags & static_cast<uint16_t>(~kAuthoredCardFlagMask)));

    bool calls_upgrade_base_cost = false;
    int new_base = old_base;
    if (id == CardId::BLOOD_FOR_BLOOD) {
        calls_upgrade_base_cost = true;
        new_base = old_base < 4 ? old_base - 1 : 3;
    } else {
        const int reg_old = static_cast<int>(card_cost(*def, old_upgrade));
        const int reg_new = static_cast<int>(card_cost(*def, c.upgrade));
        if (reg_old != reg_new) {
            calls_upgrade_base_cost = true;
            new_base = reg_new;
        }
    }
    if (!calls_upgrade_base_cost) {
        return;  // cost and costForTurn are both untouched
    }

    // upgradeBaseCost (:726-734), verbatim: hold the costForTurn-minus-cost
    // difference across the move, re-apply it only to a POSITIVE costForTurn,
    // then clamp at zero.
    const int diff = old_cost_for_turn - old_base;
    int new_cost_for_turn = old_cost_for_turn;
    if (old_cost_for_turn > 0) {
        new_cost_for_turn = new_base + diff;
    }
    if (new_cost_for_turn < 0) {
        new_cost_for_turn = 0;
    }
    if (new_base < 0) {
        new_base = 0;  // BloodForBlood.upgrade's own post-clamp on cost (:50-52)
    }
    c.cost_now = static_cast<uint8_t>(new_cost_for_turn);

    // Re-state the base-cost bookkeeping for the NEW base (types.hpp): cost_now
    // alone carries it when the two are equal, otherwise the this-turn marker
    // does, plus the saved base whenever it is not the registry row's.
    c.flags = static_cast<uint16_t>(
        c.flags & ~card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN) &
        ~card_flag_bit(CardFlag::SAVED_BASE_COST) & ~kSavedBaseCostMask);
    if (new_cost_for_turn != new_base) {
        c.flags = static_cast<uint16_t>(
            c.flags | card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN));
        if (new_base != static_cast<int>(card_cost(*def, c.upgrade))) {
            const uint16_t encoded = static_cast<uint16_t>(
                static_cast<uint16_t>(new_base > 7 ? 7 : new_base)
                << kSavedBaseCostShift);
            c.flags = static_cast<uint16_t>(
                c.flags | card_flag_bit(CardFlag::SAVED_BASE_COST) | encoded);
        }
    }
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
        // moveToDeck -> Soul.update's DRAW_PILE arm (piles.hpp).
        reset_cost_for_turn(s, pi);
        knowledge_on_place_top(s, pi);  // observer: Headbutt-style known top
    }
}

// Liquid Memories / BetterDiscardPileToHandAction's per-card body, shared by its
// forced branch (:62-72) and its post-select resolution (:91-102) -- the two are
// character-for-character the same three lines:
//
//     if (this.player.hand.size() < 10) {
//         this.player.hand.addToHand(c);
//         if (this.setCost) c.setCostForTurn(this.newCost);   // newCost = 0
//         this.player.discardPile.removeCard(c);
//     }
//     c.lighten(false); c.applyPowers();                      // presentation
//
// THE GUARD WRAPS THE REMOVAL. A card that does not fit is not moved, not
// re-costed, and NOT taken out of the discard pile -- it simply stays there.
// There is no spill-to-discard (that is MakeTempCardInHandAction's rule, and
// reaching for it here would be wrong) and no partial application.
//
// setCostForTurn is THIS-TURN, not permanent (contrast Confusion / Snecko Oil,
// which write card.cost). set_cost_for_turn already implements
// AbstractCard.setCostForTurn (AbstractCard.java:2001-2011) exactly, including
// the X-cost no-op while costForTurn < 0 and the COST_MODIFIED_FOR_TURN /
// SAVED_BASE_COST bookkeeping that lets the end-of-turn sweep restore the real
// base -- so it is called rather than re-derived.
void discard_slot_to_hand_free(CombatState& s, uint8_t slot) noexcept {
    if (slot >= s.discard_count || s.hand_count >= kHandCap) {
        return;  // `hand.size() < 10` -- no move, no re-cost, no removal
    }
    const CardPoolIndex pi = s.discard[slot];
    for (uint8_t j = static_cast<uint8_t>(slot + 1); j < s.discard_count; ++j) {
        s.discard[j - 1] = s.discard[j];
    }
    --s.discard_count;
    s.hand[s.hand_count++] = pi;
    set_cost_for_turn(s, pi, 0);
}

// AbstractCard.makeStatEquivalentCopy (AbstractCard.java:825-848), factored so
// every in-scope stat-equivalent-copy site shares ONE implementation rather
// than each hand-rolling its own (Dual Wield's clone_card_to_hand below and
// Anger's self-copy in op_make_card, S3.53). makeStatEquivalentCopy is `this.
// makeCopy()` (a FRESH instance, i.e. the registry's authored row at the
// SOURCE's own upgrade level -- makeCopy() + the ctor's upgrade() loop
// together are exactly card_flags(def, src.upgrade)) with a named field list
// (:830-847) layered on top: upgrade/timesUpgraded (implied by re-deriving at
// src.upgrade), cost/costForTurn/isCostModified/isCostModifiedForTurn (cost_now
// plus the COST_MODIFIED_FOR_TURN / SAVED_BASE_COST bits, sts/engine/types.hpp),
// misc, and freeToPlayOnce.
//
// A VERBATIM `card_pool[dst] = card_pool[src]` was tried first here and is
// WRONG: it also carries EXHAUST_ON_USE_ONCE, PURGE_ON_USE and
// AUTOPLAY_X_ENERGY -- none of which makeStatEquivalentCopy's field list
// names, and every one of which is exactly the kind of ONE-PLAY transient
// state that corrupts a pool row now sitting inertly in a pile. Witnessed
// live on this fix's first pass: Anger autoplayed off the draw pile by Havoc
// carries EXHAUST_ON_USE_ONCE until UseCardAction's post-fan-out clear
// (UseCardAction.java:132, op_use_card below) -- which is AFTER Anger's own
// MAKE_CARD step already ran, since the played card's effect program resolves
// before the USE_CARD item queued behind it -- so a verbatim copy stamped the
// freshly discarded clone with a bit that made ITS OWN LATER PLAY silently
// exhaust instead of discard (s2v3_wave2_STS227212_ps88 floor 33: discard
// [Anger] short one, exhaust[Anger] over one, from turn 9 on -- caught by this
// task's own before/after replay, not by a live capture).
void make_stat_equivalent_copy(CombatState& s, int dst_slot,
                               CardPoolIndex src_pi) noexcept {
    const CardInstance& src = s.card_pool[src_pi];
    const CardDef* def = card_def(static_cast<CardId>(src.card_id));
    CardInstance& dst = s.card_pool[dst_slot];
    dst.card_id = src.card_id;
    dst.upgrade = src.upgrade;
    dst.cost_now = src.cost_now;
    dst.flags = static_cast<uint16_t>(
        (def == nullptr ? uint16_t{0} : card_flags(*def, src.upgrade)) |
        (src.flags &
         static_cast<uint16_t>(
             card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN) |
             card_flag_bit(CardFlag::SAVED_BASE_COST) | kSavedBaseCostMask |
             card_flag_bit(CardFlag::FREE_TO_PLAY_ONCE))));
    // makeStatEquivalentCopy, unlike makeSameInstanceOf, does NOT carry the uuid
    // (AbstractCard.java:825-848 vs :819-823): the clone gets a fresh identity and
    // a SNAPSHOT of the source's misc, then diverges. So if the source row is
    // itself a replay copy whose misc is a group link, materialise the real
    // value rather than handing the copy a link to a counter it does not share
    // (REPLAY_MISC_LINK is deliberately not in the carried-flags mask above, so
    // the copy never inherits the link bit itself).
    dst.misc = has_card_flag(src.flags, CardFlag::REPLAY_MISC_LINK)
                   ? s.card_pool[misc_group_row(s, src_pi)].misc
                   : src.misc;
}

// Dual Wield: add one stat-equivalent clone of pool instance `src_pi` to
// the hand, spilling to the discard when the hand is full (MakeTempCardInHand-
// Action.update:71-77).
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
    make_stat_equivalent_copy(s, slot, src_pi);
    const CardPoolIndex idx = static_cast<CardPoolIndex>(slot);
    if (s.hand_count < kHandCap) {
        s.hand[s.hand_count++] = idx;
        // ShowCardAndAddToHandEffect:47 / :69 (MakeTempCardInHandAction.addToHand
        // :84-130). Dual Wield only ever clones an ATTACK or a POWER, so the
        // newcomer itself is never swept; the sweep is over the whole hand.
        corruption_hand_cost_sweep(s);
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
                // Soul.update's DRAW_PILE arm (piles.hpp).
                reset_cost_for_turn(s, pi);
                knowledge_on_place_top(s, pi);  // observer: known top
            }
            break;
        }
        case ChoiceKind::PUT_ON_DRAW_BOTTOM: {
            // ForethoughtAction (:39-42, :57-60), in the Java's order: the
            // freeToPlayOnce grant is read and applied BEFORE the move, off
            // `c.cost` -- AbstractCard.cost, the card's combat BASE cost, not
            // costForTurn. instance_base_cost is that reconstruction, and it is
            // what makes an Enlightenment'd or Confusion'd card carry its real
            // current base here rather than the registry row. A cost of exactly
            // 0 is left alone: `if (c.cost > 0)` is strict.
            const CardPoolIndex pi = s.hand[slot];
            if (instance_base_cost(s, pi) > 0) {
                s.card_pool[pi].flags = static_cast<uint16_t>(
                    s.card_pool[pi].flags |
                    card_flag_bit(CardFlag::FREE_TO_PLAY_ONCE));
            }
            (void)remove_from_hand(s, slot);
            // moveToBottomOfDeck -> CardGroup.addToBottom == group.add(0, c)
            // (CardGroup.java:459-461): the BOTTOM of the draw pile is index 0
            // here (PUT_ON_DRAW_TOP appends at draw_count), so everything shifts
            // up one. Moving several cards in sequence therefore leaves the LAST
            // one deepest and the FIRST one nearest the top -- the first card
            // picked is the first of them drawn again.
            if (s.draw_count < kDrawCap) {
                for (uint8_t j = s.draw_count; j > 0; --j) {
                    s.draw[j] = s.draw[j - 1];
                }
                s.draw[0] = pi;
                ++s.draw_count;
                // Soul.update's DRAW_PILE arm (piles.hpp). It does NOT clear
                // the freeToPlayOnce grant made above: resetAttributes
                // (:2035-2045) never mentions the field.
                reset_cost_for_turn(s, pi);
                knowledge_on_place_bottom(s, pi);  // observer: known bottom
            }
            break;
        }
        case ChoiceKind::UPGRADE:
            upgrade_instance(s, s.hand[slot]);
            break;
        case ChoiceKind::DISCARD_TO_DRAW_TOP:
            discard_slot_to_draw_top(s, slot);
            break;
        case ChoiceKind::DISCARD_TO_HAND_FREE:
            discard_slot_to_hand_free(s, slot);
            break;
        case ChoiceKind::HAND_TO_DISCARD_THEN_DRAW: {
            // `AbstractDungeon.player.hand.moveToDiscardPile(c)`
            // (GamblingChipAction.java:55). The draw-back is NOT here -- it is
            // one add_to_top for the WHOLE selection at confirm time, in
            // resolve_optional_choice, because the Java queues a single
            // DrawCardAction sized by selectedCards.size() rather than one per
            // card.
            //
            // GameActionManager.incrementDiscard(false) (:56) and
            // c.triggerOnManualDiscard() (:57) are named at the ChoiceKind's
            // definition: neither has an S1 counterpart to fire.
            const CardPoolIndex pi = remove_from_hand(s, slot);
            if (s.discard_count < kDiscardCap) {
                s.discard[s.discard_count++] = pi;
                // Soul.update's DISCARD_PILE arm (piles.hpp).
                reset_cost_for_turn(s, pi);
                // CardGroup.moveToDiscardPile:841 -- onCardDrawOrDiscard, per
                // discarded card. KNOWN IMPRECISION, stated rather than hidden:
                // the selection's not-yet-discarded picks sit at the tail of
                // s.hand here, where the game has them in
                // handCardSelectScreen.selectedCards -- i.e. OUT of hand.group
                // and out of the game's sweep. It can only be read by a still-
                // selected SKILL carrying a nonzero cost under Corruption, and
                // Gambling Chip (the only in-scope caller, GamblingChipAction.
                // java:55) resolves at battle start, before any Corruption.
                corruption_hand_cost_sweep(s);
            }
            break;
        }
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
        case ChoiceKind::DRAW_TO_HAND:
            // The selected DRAW-pile card leaves the real draw pile for the
            // hand (or the discard, at a full hand). The unchosen cards keep
            // their draw-pile positions -- only the temp browse list was ever
            // randomized, and it is discarded.
            draw_card_to_hand_or_discard(s, s.draw[slot]);
            break;
    }
}

// The eligible source-pile-card count for a CHOOSE_CARD of `kind` (hand slots for
// the hand kinds, discard slots for discard-to-draw-top). The just-played source
// card needs no exclusion here: it sits in the LIMBO pile while its own choice
// is open (AbstractPlayer.useCard:1369-1375), so no source pile contains it.
[[nodiscard]] int count_eligible(const CombatState& s, ChoiceKind kind,
                                 uint8_t type_filter) noexcept {
    const uint8_t src_count = choice_pile_count(s, kind);
    int n = 0;
    for (uint8_t i = 0; i < src_count; ++i) {
        if (choice_slot_eligible(s, i, kind, type_filter)) {
            ++n;
        }
    }
    return n;
}

}  // namespace

// --- Opcode bodies ----------------------------------------------------------

void apply_corruption_cost_modifier(CombatState& s) noexcept {
    // ApplyPowerAction's CONSTRUCTOR special-case (ApplyPowerAction.java:
    // 49-67), not CorruptionPower.onCardDraw: constructing a Corruption
    // application walks hand, drawPile, discardPile, then exhaustPile and
    // calls modifyCostForCombat(-9) on every SKILL. That writes BOTH `cost`
    // and `costForTurn`, clamped at zero, so this is permanent for the combat
    // and supersedes any previous this-turn cost.
    //
    // STS300219 is the observable trap: Corruption+ consumes the player's last
    // two energy while Exhume is already in hand. The constructor makes that
    // Exhume cost zero, so it remains playable and opens its exhaust grid.
    auto reduce = [&](CardPoolIndex pi) {
        if (pi >= kCardPoolCap) {
            return;
        }
        CardInstance& c = s.card_pool[pi];
        const CardDef* def = card_def(static_cast<CardId>(c.card_id));
        if (def == nullptr || def->type != CardType::SKILL ||
            has_card_flag(c.flags, CardFlag::XCOST)) {
            return;
        }
        c.cost_now = 0;
        c.flags = static_cast<uint16_t>(
            c.flags & ~card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN) &
            ~card_flag_bit(CardFlag::SAVED_BASE_COST) &
            ~kSavedBaseCostMask);
    };
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        reduce(s.hand[i]);
    }
    for (uint8_t i = 0; i < s.draw_count; ++i) {
        reduce(s.draw[i]);
    }
    for (uint8_t i = 0; i < s.discard_count; ++i) {
        reduce(s.discard[i]);
    }
    for (uint8_t i = 0; i < s.exhaust_count; ++i) {
        reduce(s.exhaust[i]);
    }
}

// MAKE_CARD: create `count` copies of `id` into `pile`. An ORDINARY copy
// (self_copy == false: Wound/Dazed/Burn/Necronomicurse -- a literal `new
// Wound()`, never the played card itself) takes a free card_pool row and its
// cost_now/flags come from the registry (base, upgrade 0). A SELF
// stat-equivalent copy (self_copy == true: Anger cloning itself, registry
// `self_copy: true`) instead copies the SOURCE INSTANCE `source_pi` whole --
// cost_now, upgrade, flags (COST_MODIFIED_FOR_TURN / SAVED_BASE_COST payload /
// FREE_TO_PLAY_ONCE included) and misc -- exactly like Dual Wield's
// clone_card_to_hand, generalized to whichever pile MAKE_CARD targets, per
// AbstractCard.makeStatEquivalentCopy (AbstractCard.java:825-848: `card.cost =
// this.cost; card.costForTurn = this.costForTurn; card.isCostModified =
// this.isCostModified; card.isCostModifiedForTurn = this.isCostModifiedForTurn;
// ...; card.misc = this.misc; card.freeToPlayOnce = this.freeToPlayOnce;`).
// `upgraded` (the registry's `upgraded_copy` bit) is then redundant on the
// self-copy path -- the copied source's own `upgrade` count already carries
// it -- and is ignored there.
//
// Before this fix EVERY MAKE_CARD, including a self-copy, reseeded the
// registry base row: a Confusion-rolled Anger (ConfusionPower.onCardDraw,
// ConfusionPower.java:38-48 -- card.cost = card.costForTurn = a random 0..3)
// cloned itself back to cost 0 instead of the rolled cost. Witnessed by the
// S3.53 sweep (s2v3_wave1_STS206243_ps5, s2v3_wave1_STS216263_ps95,
// s2v3_wave2_STS200527_ps9 -- `_oracle_data/s3/s353_sweep.tsv`).
//
// Provenance: MakeTempCardInHandAction (:64-82, incl. the hand-full -> discard
// spill), MakeTempCardInDiscardAction, ShuffleIntoDrawPileAction +
// CardGroup.addToRandomSpot (:463-468).
void op_make_card(CombatState& s, uint16_t card_id_raw, CardPile pile,
                  int count, bool upgraded, bool self_copy,
                  CardPoolIndex source_pi) noexcept {
    const CardId id = static_cast<CardId>(card_id_raw);
    const CardDef* def = card_def(id);
    if (def == nullptr || count <= 0) {
        return;
    }
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
        if (self_copy) {
            // make_stat_equivalent_copy (above, beside clone_card_to_hand):
            // carries cost_now, the cost-runtime flag bits, misc and
            // freeToPlayOnce from the SOURCE instance -- exactly
            // AbstractCard.makeStatEquivalentCopy's field list
            // (AbstractCard.java:830-847), no more -- rather than the
            // registry's fresh-base row this branch used before S3.53, or a
            // verbatim struct copy (which also drags EXHAUST_ON_USE_ONCE /
            // PURGE_ON_USE / AUTOPLAY_X_ENERGY along; see that function's
            // comment for the live witness).
            make_stat_equivalent_copy(s, slot, source_pi);
        } else {
            s.card_pool[slot].card_id = card_id_raw;
            s.card_pool[slot].upgrade = upg;
            s.card_pool[slot].cost_now = card_cost(*def, upg);
            s.card_pool[slot].flags = card_flags(*def, upg);
            s.card_pool[slot].misc = 0;
        }
        const CardPoolIndex idx = static_cast<CardPoolIndex>(slot);
        switch (pile) {
            case CardPile::HAND:
                // MakeTempCardInHandAction: overflow past kHandCap spills to the
                // discard pile (the "hand is full" branch, :71-77).
                if (s.hand_count < kHandCap) {
                    s.hand[s.hand_count++] = idx;
                    // ShowCardAndAddToHandEffect (:43-50 / :65-72): addToHand,
                    // then onCardDrawOrDiscard's Corruption sweep -- a created
                    // SKILL is 0-cost for the combat. The discard spill goes
                    // through ShowCardAndAddToDiscardEffect, which has no such
                    // step, hence inside this branch only.
                    corruption_hand_cost_sweep(s);
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
                    knowledge_on_place_top(s, idx);  // observer: known top
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
                // Observer: random-position insertion weakens order knowledge
                // to the relative-order constraint (knowledge.hpp contract).
                knowledge_on_insert_random(s, idx);
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
    const uint8_t type_filter = choose_type_filter_from_flags(item.flags);
    const int need = item.amount;
    if (need <= 0) {
        return;
    }
    if (choose_is_optional(item.flags)) {
        // An optional screen is ended by the confirm button, so the pump blocks
        // on it whenever there was anything to show and it is resolved through
        // resolve_optional_choice, not here. Reaching execute at all therefore
        // means choice_requires_user said no -- the source pile was empty --
        // which is the branch where ExhaustAction (:76-79) and ForethoughtAction
        // (:33-36) set isDone and do nothing. Note this is NOT the forced-all
        // branch below: ExhaustAction's hand.size() <= amount auto-exhaust is
        // guarded by `!this.anyNumber` (:80), and every optional caller passes
        // anyNumber true, so a 2-card hand under a Purity that may take 3 still
        // opens the screen rather than silently exhausting both.
        return;
    }
    if (is_random && choose_random_any_number(item.flags)) {
        // FiendFireAction queues `new ExhaustAction(1, true, true)` once per
        // card.  `anyNumber=true` skips ExhaustAction's otherwise-forced
        // hand.size() <= amount branch, so even its final one-card action calls
        // hand.getRandomCard(cardRandomRng) and consumes random(0).  The action
        // is still auto-resolved (isRandom), never a user prompt.
        for (int k = 0; k < need; ++k) {
            if (s.hand_count == 0) {
                break;
            }
            const int32_t ridx = random(s.card_random_rng,
                                        static_cast<int32_t>(s.hand_count) - 1);
            apply_choice_to_slot(s, static_cast<uint8_t>(ridx), kind);
        }
        return;
    }
    const int eligible = count_eligible(s, kind, type_filter);
    if (eligible <= need && kind == ChoiceKind::PUT_ON_DRAW_TOP && !is_random) {
        // PutOnDeckAction.update's NO-SCREEN branch, in full
        // (PutOnDeckAction.java:33-54). The action first clamps
        // `amount = min(amount, hand.size())` (:37-39); the screen then opens
        // only while `hand.group.size() > amount` (:45-49), so this branch is
        // reached exactly when the caller asked for at least the whole hand --
        // the outcome is FORCED, nothing is chosen. It is nevertheless:
        //
        //     for (i = 0; i < this.p.hand.size(); ++i) {
        //         this.p.hand.moveToDeck(
        //             this.p.hand.getRandomCard(AbstractDungeon.cardRandomRng),
        //             this.isRandom);
        //     }
        //
        // getRandomCard(rng) is `group.get(rng.random(size - 1))`
        // (CardGroup.java:498-500) -- ONE cardRandomRng draw per moved card,
        // spent even though the result cannot change what ends up on the deck.
        // An EMPTY hand runs zero iterations and draws nothing.
        //
        // The loop bound is re-read each iteration off a list moveToDeck is
        // SHRINKING, so with h >= 2 cards the Java moves only ceil(h/2) of
        // them. That is reproduced here rather than "corrected", by writing the
        // same shrinking loop -- but it is unreachable today: every authored
        // put-on-draw-top step selects amount 1 (Warcry, Thinking Ahead), and
        // amount 1 reaches this branch only at h <= 1, where the two readings
        // coincide.
        //
        // THIS BILLING IS PER-KIND, NOT BLANKET. Each ChoiceKind is backed by a
        // different Java action with its own forced-path behaviour --
        // ArmamentsAction upgrades its single candidate directly with no rng at
        // all, ExhaustAction's non-random branch exhausts the hand in order --
        // so the draws belong to this kind only.
        for (int i = 0; i < static_cast<int>(s.hand_count); ++i) {
            const int32_t pick =
                random(s.card_random_rng, static_cast<int32_t>(s.hand_count) - 1);
            apply_choice_to_slot(s, static_cast<uint8_t>(pick),
                                 ChoiceKind::PUT_ON_DRAW_TOP);
        }
        return;
    }
    if (eligible <= need) {
        // Forced: apply to ALL eligible cards. Snapshot pool indices first (the
        // apply mutates the source pile). (ExhaustAction: hand.size() <= amount ->
        // exhaust whole hand; ArmamentsAction: exactly one upgradeable -> upgrade
        // it; DiscardPileToTopOfDeckAction: <= 1 discard card -> auto-move it;
        // Skill/AttackFromDeckToHandAction: exactly one matching draw-pile card
        // -> take it with no screen, :44-64, and zero matches -> a silent
        // no-op, :40-43.) PutOnDeckAction took its own branch above -- its
        // forced path is the one that spends rng. The eligible-<=-need bound
        // keeps the snapshot within kHandCap (every in-scope discard- and
        // draw-source choice has need == 1).
        CardPoolIndex picked[kHandCap];
        int m = 0;
        const uint8_t sc = choice_pile_count(s, kind);
        const CardPoolIndex* src_pile = choice_pile_cards(s, kind);
        for (uint8_t i = 0; i < sc && m < kHandCap; ++i) {
            if (choice_slot_eligible(s, i, kind, type_filter)) {
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
    knowledge_on_draw_top(s, pi);  // observer: Havoc reveals+consumes the top
    // exhaustOnUseOnce = true (:48) -> force the played card to exhaust; play it
    // free (autoplay does not pay energy). The game moves the card into the
    // player's limbo CardGroup (PlayTopCardAction.update:
    // `AbstractDungeon.player.limbo.group.add(card)`) -- ours goes to the limbo
    // pile, where resolve_card_play finds it -- then its play is queued at the
    // front of the cardQueue (NewQueueCardAction -> the normal §5.3 resolve).
    s.card_pool[pi].flags |=
        card_flag_bit(CardFlag::EXHAUST_ON_USE_ONCE);
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
    if ((flags & kPlayCardDeferRoll) != 0u) {
        // MayhemPower$1.update (recovered class; see kPlayCardDeferRoll): roll
        // the target NOW -- the getRandomMonster is the PlayTopCardAction's
        // CONSTRUCTOR argument, so the draw precedes everything, including the
        // turn's DrawCardAction still sitting in the queue -- and addToBot the
        // real play with the target baked, exactly one level down. No pile is
        // touched here; the pop happens when the re-queued item executes.
        ActionQueueItem play{};
        play.opcode = static_cast<uint16_t>(Opcode::PLAY_CARD);
        play.src = kActorPlayer;
        play.tgt = roll_random_target(s);
        play.amount = source_index;
        play.flags = flags & ~kPlayCardDeferRoll;
        add_to_bottom(s, play);
        return;
    }
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
        knowledge_on_draw_top(s, pi);  // observer: Mayhem-style top autoplay
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
        const CardPoolIndex root = misc_group_row(s, pi);
        s.card_pool[slot] = s.card_pool[pi];
        pi = static_cast<CardPoolIndex>(slot);
        // makeSameInstanceOf copies the uuid too (AbstractCard.java:819-823), so
        // any counter this card grows mid-combat is SHARED with the original for
        // as long as the copy is alive: ModifyDamageAction / RitualDaggerAction
        // both write every instance GetAllInBattleInstances.get(uuid) finds, and
        // that walk includes LIMBO, where this copy is about to sit
        // (GetAllInBattleInstances.java:29-32). The plain row copy above cannot
        // model that -- it snapshots the counter as it stands NOW, which is
        // BEFORE the original's own play has grown it (resolve_card_play queues
        // the card's program at step 4 and fires this ON_USE_CARD hook at step 5,
        // exactly as AbstractPlayer.useCard:1369-1370 does), so a double-tapped
        // Rampage would deal 8 then 8 instead of 8 then 13. Redirect the copy's
        // counter reads and writes to the group's owning row instead; `root` is
        // resolved BEFORE the copy so a copy of a copy (Necronomicon re-firing
        // on a Double Tap copy) points at the same original.
        const CardDef* copy_def =
            card_def(static_cast<CardId>(s.card_pool[pi].card_id));
        if (copy_def != nullptr &&
            card_misc_is_uuid_shared(*copy_def, s.card_pool[pi].upgrade)) {
            s.card_pool[pi].misc = root;
            s.card_pool[pi].flags = static_cast<uint16_t>(
                s.card_pool[pi].flags |
                card_flag_bit(CardFlag::REPLAY_MISC_LINK));
        }
    }
    if ((flags & kPlayCardPurge) != 0u) {
        s.card_pool[pi].flags = static_cast<uint16_t>(
            s.card_pool[pi].flags | card_flag_bit(CardFlag::PURGE_ON_USE));
    }
    if ((flags & kPlayCardExhaust) != 0u) {
        s.card_pool[pi].flags = static_cast<uint16_t>(
            s.card_pool[pi].flags |
            card_flag_bit(CardFlag::EXHAUST_ON_USE_ONCE));
    }
    capture_purge_copy_x_energy(s, pi, flags);
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
//   :79-88   onAfterUseCard fan-out -- dispatch_on_after_use_card, between the
//            program's actions and the filing. PLAYER powers then MONSTER
//            powers, no relics (that participant list is what makes it a
//            different hook from ON_USE_CARD, not just a different moment).
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
    // UseCardAction.update:87 -- `this.targetCard.freeToPlayOnce = false;` runs
    // at the TOP of the update, before the purge/POWER/spoon/filing branches, so
    // the one free play is spent no matter where the card ends up (and even for
    // a POWER, which lands in no pile at all). That is the whole lifetime: the
    // energy check and the spend both happened back at play time
    // (AbstractCard.java:888, AbstractPlayer.java:1378), and this is the seam
    // that stops the next play being free too. Cleared unconditionally, unlike
    // exhaustOnUseOnce below, which :132 clears only on the filing path.
    s.card_pool[pi].flags = static_cast<uint16_t>(
        s.card_pool[pi].flags & ~card_flag_bit(CardFlag::FREE_TO_PLAY_ONCE));
    // :77-86 onAfterUseCard -- the seam the comment above reserved. The "NO S1
    // binder exists" paragraph is DELETED rather than amended: the prerequisite
    // arrived (conventions section 8), and Slow and Time Warp are the binders it
    // named. It dispatches HERE, between the program's actions (already resolved
    // -- useCard addToBottom'd this item last) and the purge / POWER / Strange
    // Spoon / filing decisions below, which is exactly the Java's order.
    //
    // UNLESS the play is a `dontTriggerOnUseCard` one: BOTH loops carry
    // `if (this.targetCard.dontTriggerOnUseCard) continue;` (:78, :83), so an
    // end-of-turn Burn/curse self-play and a cancelled autoplay's filing reach
    // this update and fire NOTHING -- the Time Eater's clock does not tick for
    // a Shame that plays itself. The producers stamp the bit on the item
    // (kUseCardDontTriggerOnUseCard, interp.hpp); the filing below is the same
    // either way, exactly as :87-134 run unconditionally after the two loops.
    if ((item.flags & kUseCardDontTriggerOnUseCard) == 0u) {
        dispatch_on_after_use_card(s, pi, s.card_pool[pi].card_id);
    }
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
    if (!remove_only) {
        // BOTH filing branches end in onCardDrawOrDiscard: `hand.moveTo
        // DiscardPile` (UseCardAction.java:126 -> CardGroup.java:841) and
        // `hand.moveToExhaustPile` (:129 -> CardGroup.java:861). It runs LAST in
        // moveToExhaustPile, after the onExhaust fan-out above, which is why
        // this sits below the dispatch rather than inside file_card_from_limbo.
        //
        // The `remove_only` branches have no such step: purgeOnUse goes through
        // ShowCardAndPoofAction (:91) and a POWER through hand.empower
        // (CardGroup.java:844-848), and neither touches onCardDrawOrDiscard.
        // Corruption's own redirect makes this the sweep every played SKILL
        // runs (CorruptionPower.onUseCard sets action.exhaustCard).
        corruption_hand_cost_sweep(s);
    }
}

// FIEND_FIRE (FiendFireAction.update, FiendFireAction.java:32-46): count =
// hand.size() at EXECUTE time; addToTop `count` DamageActions, then addToTop
// `count` ExhaustAction(1, isRandom = true) -- the SECOND addToTop loop lands in
// front, so the random exhausts all resolve before the first hit.  Its third
// constructor argument is `anyNumber=true`, which means the final singleton is
// STILL chosen by `getRandomCard(cardRandomRng)`, rather than taking
// ExhaustAction's forced-all shortcut.  Both loops preserve their internal order
// because each pushes onto the front in turn, which reverses an already-uniform
// list into itself.
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
        ex.flags = make_choose_flags(ChoiceKind::EXHAUST, /*random=*/true) |
                   kChoiceRandomAnyNumberBit;
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

// RANDOM_ATTACK_TO_HAND (Infernal Blade / InfernalBlade.use:31-35, and -- via
// the S2.34 flags pool selector -- Enchiridion.atPreBattle / Enchiridion.
// java:30-39, which is the SAME returnTrulyRandomCardInCombat(type) body with
// CardType.POWER): pick ONE uniformly-random member of the selected combat
// pool (AbstractDungeon.java:964-979 -- ONE cardRandomRng random(size-1) draw
// over srcCommon+srcUncommon+srcRare filtered to non-HEALING cards of the
// type), makeCopy() a BASE instance, setCostForTurn(0), and add it to the hand
// (MakeTempCardInHandAction: hand-cap spill to discard). The cost-0 is
// this-turn-only: COST_MODIFIED_FOR_TURN is reset by the end-turn sweep
// (AbstractRoom.endTurn:397-405) / on exhaust (ExhaustCardEffect:41-43).
// Enchiridion's explicit `if (c.cost != -1)` guard and Infernal Blade's bare
// setCostForTurn coincide: setCostForTurn is a no-op while costForTurn < 0
// (AbstractCard.java:2002) -- and neither pool holds an X-cost RED card the
// distinction could bite on today (Whirlwind is the ATTACK pool's one X-cost,
// whose XCOST cost_now is 0 either way). Pool membership/order provenance:
// generated kIroncladAttackPool / kIroncladPowerPool (cards.hpp).
void op_random_attack_to_hand(CombatState& s, uint32_t flags) noexcept {
    static_assert(kIroncladAttackPoolCount > 0,
                  "Infernal Blade needs a non-empty attack pool");
    static_assert(kIroncladPowerPoolCount > 0,
                  "Enchiridion needs a non-empty power pool");
    const bool power_pool = flags == kRandomToHandPoolPower;
    const int32_t pool_count = power_pool
                                   ? static_cast<int32_t>(kIroncladPowerPoolCount)
                                   : static_cast<int32_t>(kIroncladAttackPoolCount);
    const int32_t pick = random(s.card_random_rng, pool_count - 1);
    const CardId id = power_pool
                          ? kIroncladPowerPool[static_cast<unsigned>(pick)]
                          : kIroncladAttackPool[static_cast<unsigned>(pick)];
    const CardDef* def = card_def(id);
    if (def == nullptr) {
        return;  // defensive; the pools only hold registry rows
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
        // ShowCardAndAddToHandEffect:47 / :69. Both pools here are ATTACK /
        // POWER, so the newcomer is never the card the sweep moves.
        corruption_hand_cost_sweep(s);
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
            ~card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN) &
            ~card_flag_bit(CardFlag::SAVED_BASE_COST) &
            ~kSavedBaseCostMask);
        return;
    }
}

// DARK_SHACKLES (DarkShackles.use, :32-38). The Artifact test happens while
// use() queues its actions, BEFORE Strength(-amount) has a chance to spend the
// stack. This fused opcode performs that read once, then prepends the child
// actions in reverse so they resolve Strength -> Shackled -> UseCard filing.
void op_dark_shackles(CombatState& s, uint8_t target, int amount) noexcept {
    if (target >= s.monster_count || monster_dead_or_escaped(s.monsters[target])) {
        return;
    }
    bool has_artifact = false;
    for (uint8_t i = 0; i < s.monsters[target].power_count; ++i) {
        const PowerSlot& p = s.monsters[target].powers[i];
        if (p.power_id == static_cast<uint16_t>(PowerId::ARTIFACT) &&
            p.amount > 0) {
            has_artifact = true;
            break;
        }
    }
    if (!has_artifact) {
        ActionQueueItem shackled{};
        shackled.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        shackled.src = kActorPlayer;
        shackled.tgt = target;
        shackled.amount = amount;
        shackled.flags = make_apply_power_flags(PowerId::SHACKLED);
        add_to_top(s, shackled);
    }
    ActionQueueItem strength{};
    strength.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    strength.src = kActorPlayer;
    strength.tgt = target;
    strength.amount = -amount;
    strength.flags = make_apply_power_flags(PowerId::STRENGTH);
    add_to_top(s, strength);
}

// ENLIGHTENMENT (EnlightenmentAction.update, :30-42). Base changes only
// costForTurn; upgraded additionally changes `cost` for cards whose reconstructed
// base cost exceeded 1. The source card is in limbo while this scans the hand.
void op_enlightenment(CombatState& s, bool for_rest_of_combat) noexcept {
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        const CardPoolIndex pi = s.hand[i];
        CardInstance& c = s.card_pool[pi];
        const int base_before = instance_base_cost(s, pi);
        if (c.cost_now > 1) {
            const CardDef* def = card_def(static_cast<CardId>(c.card_id));
            const int registry_base =
                def == nullptr ? base_before
                               : static_cast<int>(card_cost(*def, c.upgrade));
            if (base_before != registry_base &&
                !has_card_flag(c.flags, CardFlag::SAVED_BASE_COST)) {
                const uint16_t encoded = static_cast<uint16_t>(
                    static_cast<uint16_t>(base_before > 7 ? 7 : base_before)
                    << kSavedBaseCostShift);
                c.flags = static_cast<uint16_t>(
                    (c.flags & ~kSavedBaseCostMask) |
                    card_flag_bit(CardFlag::SAVED_BASE_COST) | encoded);
            }
            c.cost_now = 1;
            c.flags = static_cast<uint16_t>(
                c.flags | card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN));
        }
        if (for_rest_of_combat && base_before > 1) {
            // Java changes `cost` to 1 without overwriting a cheaper
            // costForTurn. Keep that temporary current cost and remember the
            // new permanent base (1); otherwise both fields are now 1 and the
            // temporary marker can disappear.
            if (c.cost_now < 1) {
                const uint16_t encoded = static_cast<uint16_t>(
                    1u << kSavedBaseCostShift);
                c.flags = static_cast<uint16_t>(
                    (c.flags & ~kSavedBaseCostMask) |
                    card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN) |
                    card_flag_bit(CardFlag::SAVED_BASE_COST) | encoded);
            } else {
                c.cost_now = 1;
                c.flags = static_cast<uint16_t>(
                    c.flags &
                    ~card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN) &
                    ~card_flag_bit(CardFlag::SAVED_BASE_COST) &
                    ~kSavedBaseCostMask);
            }
        }
    }
}

// RANDOM_COLORLESS_TO_HAND. Two consumers, distinguished only by `flags`:
//   * Jack of All Trades (JackOfAllTrades.use:31-35, flags == 0): `amount`
//     independent returnTrulyRandomColorlessCardInCombat() draws
//     (AbstractDungeon.java:981-996 -- ONE cardRandomRng random(size-1) each
//     over srcColorlessCardPool minus HEALING), each a BASE copy at its
//     registry cost, into the hand with the MakeTempCardInHandAction hand-cap
//     spill to discard. Duplicates are allowed: each draw is independent.
//   * Transmutation, ONE repetition per X energy (TransmutationAction.update
//     :44-51). Same draw, then `if (upgraded) c.upgrade()` followed by
//     `c.setCostForTurn(0)` -- kColorlessToHandUpgradedCopy and
//     kColorlessToHandCostZeroForTurn. The order matters and is preserved: the
//     this-turn zero replaces the UPGRADED row's cost.
// The rolls are sequential with nothing between them (the queued
// MakeTempCardInHandActions consume no rng), so N calls cost exactly N draws.
void op_random_colorless_to_hand(CombatState& s, int count,
                                 uint32_t flags) noexcept {
    static_assert(kColorlessCombatPoolCount > 0,
                  "Jack of All Trades needs a non-empty colorless pool");
    const bool free_this_turn = (flags & kColorlessToHandCostZeroForTurn) != 0u;
    const bool upgraded = (flags & kColorlessToHandUpgradedCopy) != 0u;
    for (int i = 0; i < count; ++i) {
        const int32_t pick = random(
            s.card_random_rng,
            static_cast<int32_t>(kColorlessCombatPoolCount) - 1);
        add_library_copy_to_hand(
            s, kColorlessCombatPool[static_cast<unsigned>(pick)],
            free_this_turn, upgraded);
    }
}

// RANDOM_CARD_TO_DRAW (Chrysalis, type SKILL / Metamorphosis, type ATTACK --
// Chrysalis.java:31-42 and Metamorphosis.java:31-42, the same loop). The two
// halves below are the whole point of fusing the loop into one opcode: the Java
// performs ALL `count` pool rolls inside use() and only then resolves the
// `count` queued MakeTempCardInDrawPileActions, so the cardRandomRng stream is
// [roll x count][insert x count] -- never interleaved. See the Opcode enum
// comment in interp.hpp for why nothing can slip between the two halves.
//
// Each pick is one random(size-1) over the type-filtered RED combat pool
// (returnTrulyRandomCardInCombat(type), AbstractDungeon.java:964-979), and the
// generated instance is a BASE library copy whose cost, when the registry base
// is > 0, is zeroed PERMANENTLY for the combat: Chrysalis.java:35-38 writes BOTH
// card.cost and card.costForTurn, so this is op_madness's model (cost_now = 0
// with no COST_MODIFIED_FOR_TURN marker), NOT setCostForTurn's. An X-cost card
// (Java cost -1, our CardFlag::XCOST with base cost 0) and an already-0 card
// both fail the `cost > 0` guard and keep their registry cost -- an X-cost
// generated card is still an X-cost card.
//
// The insertion is CardGroup.addToRandomSpot (:463-469) via
// ShowCardAndAddToDrawPileEffect's ctor (:47-48; randomSpot true, toBottom
// false -- MakeTempCardInDrawPileAction's 4-arg ctor, :44-46): ONE
// card_random_rng draw over [0, size-1] per insert, or a free plain append when
// the draw pile is EMPTY at that moment. The pile grows as the inserts proceed,
// so insert k sees the k-1 cards already added.
void op_random_card_to_draw(CombatState& s, int count, uint8_t type) noexcept {
    static_assert(kIroncladSkillPoolCount > 0 && kIroncladAttackPoolCount > 0,
                  "Chrysalis / Metamorphosis need non-empty type pools");
    if (count <= 0) {
        return;
    }
    const bool want_skill = type == static_cast<uint8_t>(CardType::SKILL);
    if (!want_skill && type != static_cast<uint8_t>(CardType::ATTACK)) {
        return;  // defensive: only the two authored filters have an emitted pool
    }
    const CardId* pool = want_skill ? kIroncladSkillPool.data()
                                    : kIroncladAttackPool.data();
    const int pool_count =
        want_skill ? kIroncladSkillPoolCount : kIroncladAttackPoolCount;
    // Half 1 -- every pool roll, in order, before any insertion. The roll loop
    // runs the FULL `count` regardless of what the piles can hold, because that
    // is what the Java's use() does; only the storage is bounded (the generated
    // cards are all headed for the draw pile, so its cap is the bound).
    CardId picked[kDrawCap];
    int n = 0;
    for (int i = 0; i < count; ++i) {
        const int32_t pick =
            random(s.card_random_rng, static_cast<int32_t>(pool_count) - 1);
        if (n < kDrawCap) {
            picked[n++] = pool[static_cast<unsigned>(pick)];
        }
    }
    // Half 2 -- the queued MakeTempCardInDrawPileActions, in queue order.
    for (int i = 0; i < n; ++i) {
        const CardDef* def = card_def(picked[i]);
        if (def == nullptr) {
            continue;  // defensive; the pool only holds registry rows
        }
        if (s.draw_count >= kDrawCap) {
            return;  // defensive; nothing left to insert into
        }
        int slot = -1;
        for (int k = 0; k < kCardPoolCap; ++k) {
            if (s.card_pool[k].card_id == static_cast<uint16_t>(CardId::NONE)) {
                slot = k;
                break;
            }
        }
        if (slot < 0) {
            return;  // pool exhausted (defensive; 160-row cap, design §4.2)
        }
        CardInstance& c = s.card_pool[slot];
        c.card_id = static_cast<uint16_t>(picked[i]);
        c.upgrade = 0;  // makeCopy() -- a base library card
        c.flags = card_flags(*def, 0);
        c.misc = 0;
        const uint8_t base = card_cost(*def, 0);
        // `if (card.cost > 0)` -- an X-cost card has Java cost -1 (CardFlag::
        // XCOST here, base cost 0) and so never enters this branch.
        c.cost_now = (base > 0) ? 0 : base;
        const CardPoolIndex idx = static_cast<CardPoolIndex>(slot);
        int pos = 0;
        if (s.draw_count > 0) {
            pos = random(s.card_random_rng, s.draw_count - 1);
        }
        for (int j = s.draw_count; j > pos; --j) {
            s.draw[j] = s.draw[j - 1];
        }
        s.draw[pos] = idx;
        ++s.draw_count;
    }
}

// UPGRADE_ALL (Apotheosis / ApotheosisAction.update, :25-34): upgradeAllCards-
// InGroup runs over p.hand, p.drawPile, p.discardPile, p.exhaustPile IN THAT
// ORDER (:28-31), and canUpgrade() (:38) gates each member (shared with
// Armaments' single-card choice via can_upgrade_instance). c.upgrade() +
// applyPowers() is upgrade_instance, the same in-place mutation Armaments
// uses. The played Apotheosis itself is in the LIMBO pile for the whole of
// this resolution (resolve_card_play), so it sits in none of the four piles
// scanned and is never upgraded -- matching the Java, where the card mid-use()
// is in no CardGroup either.
void op_upgrade_all(CombatState& s) noexcept {
    auto upgrade_pile = [&s](const CardPoolIndex* pile, uint8_t count) noexcept {
        for (uint8_t i = 0; i < count; ++i) {
            const CardPoolIndex pi = pile[i];
            if (can_upgrade_instance(s, pi)) {
                upgrade_instance(s, pi);
            }
        }
    };
    upgrade_pile(s.hand, s.hand_count);
    upgrade_pile(s.draw, s.draw_count);
    upgrade_pile(s.discard, s.discard_count);
    upgrade_pile(s.exhaust, s.exhaust_count);
}

// UPGRADE_RANDOM_CARD (Warped Tongs / UpgradeRandomCardAction.update,
// UpgradeRandomCardAction.java:28-50), read in full and reproduced in ORDER,
// because every early-out sits at a different point in the shuffleRng stream:
//
//   :31-34  an EMPTY hand ends the action -- ZERO draws.
//   :35-39  build `upgradeable` from the hand, skipping `!c.canUpgrade() ||
//           c.type == STATUS`. CardGroup.addToTop is `group.add(c)` -- an APPEND
//           (CardGroup.java:455-457) -- so the temp group is in HAND ORDER.
//   :40-45  IF the subset is non-empty: `upgradeable.shuffle()` then
//           `group.get(0).upgrade()`. Otherwise the action ends having drawn
//           NOTHING.
//
// THE STREAM FACT: the no-arg CardGroup.shuffle() is `Collections.shuffle(group,
// new java.util.Random(AbstractDungeon.shuffleRng.randomLong()))`
// (CardGroup.java:561-563) -- ONE shuffle_rng draw seeding a JDK Fisher-Yates
// over the PRE-FILTERED group, and only when that group is non-empty. A hand
// with nothing upgradeable therefore costs zero. This is why the action needed
// its own opcode instead of CHOOSE_CARD{RANDOM, UPGRADE}, which draws
// card_random_rng once over the WHOLE hand: different stream, different set.
//
// The filter is canUpgrade(), NOT `upgrade == 0`: SearingBlow.canUpgrade
// (SearingBlow.java:58-60) overrides the base with `return true`, so an already
// upgraded Searing Blow stays eligible. can_upgrade_instance is that predicate,
// shared with Armaments and Apotheosis so the three cannot drift.
// The explicit `type == STATUS` test at :37 is redundant -- canUpgrade already
// rejects CURSE and STATUS (AbstractCard.java:672-680) -- and is kept in this
// note so the filter is visibly derived rather than invented.
//
// `superFlash()` and `applyPowers()` (:43-44) are presentation: this engine has
// no preview layer, and upgrade_instance already re-seeds cost_now/flags from the
// upgraded registry row, which is the only state applyPowers would touch.
void op_upgrade_random_card(CombatState& s) noexcept {
    if (s.hand_count == 0) {
        return;  // (:31-34) -- no draw
    }
    CardPoolIndex eligible[kHandCap]{};
    uint8_t n = 0;
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        const CardPoolIndex pi = s.hand[i];
        if (can_upgrade_instance(s, pi)) {
            eligible[n++] = pi;  // addToTop == append, so HAND ORDER (:38)
        }
    }
    if (n == 0) {
        return;  // (:40) -- the shuffle is INSIDE the non-empty test: no draw
    }
    JdkRandom jr(random_long(s.shuffle_rng));
    jdk_shuffle(std::span<CardPoolIndex>(eligible, static_cast<std::size_t>(n)),
                jr);
    upgrade_instance(s, eligible[0]);  // group.get(0).upgrade() (:42-43)
}

// DRAW_PILE_FETCH (Violence / DrawPileToHandAction.update, :31-71). Read in
// order, because each early-out sits at a different point in the rng stream:
//
//   :33-36  an EMPTY draw pile ends the action BEFORE the temp list exists --
//           zero draws of either stream.
//   :37-41  build the temp browse group of every matching draw-pile card via
//           addToRandomSpot -- k matches, k-1 card_random_rng draws.
//   :42-45  ZERO matches ends the action here, having spent exactly what the
//           build spent (for k == 0 that is nothing).
//   :46-67  `amount` iterations. An empty temp list `continue`s with NO rng at
//           all (:47) -- so asking for more than the draw pile holds is free
//           past the last real pick. Otherwise tmp.shuffle() -- the NO-ARG
//           CardGroup.shuffle (:561-563), i.e.
//           Collections.shuffle(group, new java.util.Random(
//               AbstractDungeon.shuffleRng.randomLong())) -- ONE shuffle_rng
//           draw seeding a JDK LCG that drives the same Fisher-Yates the deck
//           reshuffle uses (jdk_shuffle). getBottomCard() (:49) is
//           group.get(0), removed from the temp list (:50) and then moved
//           draw -> hand, or draw -> DISCARD at a full hand (:51-55) -- either
//           way it is consumed out of the draw pile.
//
// There is NO canUse gate on Violence (Violence.java has no canUse override),
// so all three whiff paths above are genuinely reachable from a legal play.
void op_draw_pile_fetch(CombatState& s, int amount, uint8_t type) noexcept {
    if (s.draw_count == 0) {
        return;  // :33-36 -- before the build, so ZERO draws
    }
    CardPoolIndex tmp[kDrawCap];
    int n = build_draw_temp_group(s, type, tmp);
    if (n == 0) {
        return;  // :42-45 -- after the build (which cost nothing for k == 0)
    }
    for (int i = 0; i < amount; ++i) {
        if (n == 0) {
            continue;  // :47 -- no shuffle, no draw
        }
        JdkRandom rng(random_long(s.shuffle_rng));
        jdk_shuffle(std::span<CardPoolIndex>(tmp, static_cast<std::size_t>(n)),
                    rng);
        const CardPoolIndex pi = tmp[0];  // getBottomCard == group.get(0)
        for (int j = 1; j < n; ++j) {
            tmp[j - 1] = tmp[j];
        }
        --n;
        draw_card_to_hand_or_discard(s, pi);
    }
}

// The shared per-card cost roll behind ConfusionPower.onCardDraw and
// RandomizeHandCostAction. The two Java bodies are near-twins that differ in
// EXACTLY one line, so they are one helper with one boolean rather than two
// hand copies that drift:
//
//   ConfusionPower.onCardDraw (ConfusionPower.java:38-48)
//     if (card.cost >= 0) {
//         int newCost = cardRandomRng.random(3);
//         if (card.cost != newCost) { card.costForTurn = card.cost = newCost;
//                                     card.isCostModified = true; }
//         card.freeToPlayOnce = false;              <-- OUTSIDE the inner if
//     }
//
//   RandomizeHandCostAction.update (RandomizeHandCostAction.java:26-38)
//     for (AbstractCard card : p.hand.group) {
//         int newCost;
//         if (card.cost < 0 || card.cost == (newCost = cardRandomRng.random(3)))
//             continue;                             <-- no freeToPlayOnce line
//         card.costForTurn = card.cost = newCost;
//         card.isCostModified = true;
//     }
//
// Four things are load-bearing and shared:
//
// (1) THE DRAW IS UNCONDITIONAL once cost >= 0. It happens BEFORE the equality
//     test (`||` is short-circuit and the assignment sits in its RIGHT operand),
//     so a card that rolls its own current cost still consumes exactly one
//     card_random_rng draw.
// (2) `card.cost < 0` cards consume NO draw. In the Java those are the X-cost
//     rows (cost -1) and the unplayable status/curse rows (cost < -1); here that
//     is exactly the XCOST / UNPLAYABLE instance flags, which the generator sets
//     from those same negative sentinels (stsgen/emit/cards.py parse_card_flags)
//     and which nothing ever clears.
// (3) THE COMPARISON IS AGAINST card.cost -- the BASE cost, not costForTurn --
//     so it is instance_base_cost, not cost_now. On equality the Java writes
//     NOTHING, so a live this-turn cost modification survives intact.
// (4) The write is `costForTurn = cost = newCost`, i.e. PERMANENT for the
//     instance. cost_now is therefore written WITHOUT setting
//     COST_MODIFIED_FOR_TURN, and any such bit already on the instance is
//     cleared -- after the assignment the Java's cost and costForTurn are equal,
//     so leaving it set would make the end-of-turn sweep (reset_cost_for_turn)
//     restore the REGISTRY cost and silently undo the randomization.
//
// `clear_free_to_play_once` is the one difference: true for Confusion (:46),
// false for RandomizeHandCostAction, which never touches the field. Note that
// Confusion clears it on the EQUALITY path too -- that line is outside the
// inner `if`, and only the whole `cost >= 0` branch gates it.
void randomize_card_cost(CombatState& s, CardPoolIndex pi,
                         bool clear_free_to_play_once) noexcept {
    if (pi >= kCardPoolCap) {
        return;
    }
    CardInstance& c = s.card_pool[pi];
    if (has_card_flag(c.flags, CardFlag::XCOST) ||
        has_card_flag(c.flags, CardFlag::UNPLAYABLE)) {
        return;  // (2) card.cost < 0: no draw, no change
    }
    const int32_t new_cost = random(s.card_random_rng, 3);  // (1) random(3) == 0..3
    if (new_cost != instance_base_cost(s, pi)) {            // (3)
        c.cost_now = static_cast<uint8_t>(new_cost);        // (4)
        c.flags = static_cast<uint16_t>(
            c.flags & ~card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN) &
            ~card_flag_bit(CardFlag::SAVED_BASE_COST) & ~kSavedBaseCostMask);
    }
    if (clear_free_to_play_once) {
        c.flags = static_cast<uint16_t>(
            c.flags & ~card_flag_bit(CardFlag::FREE_TO_PLAY_ONCE));
    }
}

// RANDOMIZE_HAND_COST (Snecko Oil / RandomizeHandCostAction.update,
// RandomizeHandCostAction.java:26-38). The whole body is the per-card helper
// above run over `p.hand.group` in iteration order, i.e. hand-slot order, with
// freeToPlayOnce left alone -- the action never mentions the field.
//
// The hand is read HERE, at resolve, not at queue time. SneckoOil.use
// (SneckoOil.java:42-45) queues DrawCardAction(potency) and then this, both
// addToBot, so the hand this walks already contains the drawn cards. With
// Confusion also up, each of those draws has ALREADY spent its own
// card_random_rng draw at onCardDraw before this pass spends one per hand card;
// the two are consecutive stretches of one stream, in that order.
//
// No bound check on hand_count is needed beyond the array's own: hand_count is
// <= kHandCap by construction everywhere that writes it.
void op_randomize_hand_cost(CombatState& s) noexcept {
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        randomize_card_cost(s, s.hand[i], /*clear_free_to_play_once=*/false);
    }
}

// The pool a DISCOVERY item draws its three offers from. All five are the same
// rejection sampler over a different list -- DiscoveryAction.generateCardChoices
// (DiscoveryAction.java:105-120) calls returnTrulyRandomCardInCombat(type)
// (AbstractDungeon.java:964-979) and generateColorlessCardChoices (:89-103)
// calls returnTrulyRandomColorlessCardInCombat (:981-995), and each of those is
// exactly ONE cardRandomRng.random(size - 1) over its concatenated,
// HEALING-filtered list.
struct DiscoveryPoolView {
    const CardId* cards;
    int count;
};

[[nodiscard]] DiscoveryPoolView discovery_pool_view(DiscoveryPool pool) noexcept {
    switch (pool) {
        case DiscoveryPool::ATTACK:
            return {kIroncladAttackPool.data(), kIroncladAttackPoolCount};
        case DiscoveryPool::SKILL:
            return {kIroncladSkillPool.data(), kIroncladSkillPoolCount};
        case DiscoveryPool::POWER:
            return {kIroncladPowerPool.data(), kIroncladPowerPoolCount};
        case DiscoveryPool::COLORLESS:
            return {kColorlessCombatPool.data(), kColorlessCombatPoolCount};
        case DiscoveryPool::COMBAT:
        default:
            return {kIroncladCombatPool.data(), kIroncladCombatPoolCount};
    }
}

// One generateCardChoices / generateColorlessCardChoices call
// (DiscoveryAction.java:105-120 / :89-103): the rejection sampler loops until
// it holds three DISTINCT cardIDs (`while (derp.size() != 3)`, `continue` on a
// duplicate cardID), one cardRandomRng draw per attempt. A pool with fewer
// than three members would spin forever in the game and here; every pool is
// generated, so that is a compile-time guarantee rather than a runtime hope.
void generate_discovery_offer(CombatState& s, const DiscoveryPoolView& pv,
                              CardId (&offered)[kDiscoveryChoiceCount]) noexcept {
    static_assert(kIroncladCombatPoolCount >= kDiscoveryChoiceCount &&
                      kIroncladAttackPoolCount >= kDiscoveryChoiceCount &&
                      kIroncladSkillPoolCount >= kDiscoveryChoiceCount &&
                      kIroncladPowerPoolCount >= kDiscoveryChoiceCount &&
                      kColorlessCombatPoolCount >= kDiscoveryChoiceCount,
                  "every Discovery pool needs at least three distinct cards, "
                  "or generateCardChoices' rejection loop cannot terminate");
    uint8_t count = 0;
    while (count < kDiscoveryChoiceCount) {
        const int32_t pick = random(
            s.card_random_rng, static_cast<int32_t>(pv.count) - 1);
        const CardId id = pv.cards[static_cast<unsigned>(pick)];
        bool duplicate = false;
        for (uint8_t i = 0; i < count; ++i) {
            if (offered[i] == id) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            offered[count++] = id;
        }
    }
}

// RESOLVED (was the ledger's "DiscoveryAction may regenerate its three-card
// offer on EVERY update tick" deferred obligation): it DOES, and the capture
// settled how often. DiscoveryAction.update computes `generatedCards` at
// DiscoveryAction.java:47 -- OUTSIDE the duration branch -- and only the FIRST
// tick's list reaches the screen (:49); under the oracle contract the action
// ticks 1 + kDiscoveryWastedRegens times in total (the full derivation and the
// seven-capture table live on that constant, interp.hpp). This latch is that
// first tick; the five wasted regenerations are burned by advance()'s CHOOSE
// dispatch when the pick or skip closes the screen, which is when the game
// resumes ticking -- so the counter a mid-screen observation sees is the
// open-tick cost alone, exactly as the captures record it.
void prepare_discovery_choice(CombatState& s,
                              ActionQueueItem& item) noexcept {
    if (discovery_choice_prepared(item)) {
        return;
    }
    const DiscoveryPoolView pv = discovery_pool_view(discovery_pool(item));
    CardId offered[kDiscoveryChoiceCount]{};
    generate_discovery_offer(s, pv, offered);
    item.flags =
        static_cast<uint32_t>(static_cast<uint16_t>(offered[0])) |
        (static_cast<uint32_t>(static_cast<uint16_t>(offered[1])) << 16u);
    item.amount =
        static_cast<int32_t>(static_cast<uint16_t>(offered[2]));
}

// The wasted-work half of the model above: `regens` complete offer
// regenerations whose results nothing reads. Each is the FULL rejection
// sampler -- a duplicate inside a wasted regeneration still costs its draw
// (STS01861 seq 90: a skip whose close spends 16, not 15). The pool is the
// item's own: line :47 calls the same generator the open tick called.
void discard_discovery_regens(CombatState& s, const ActionQueueItem& item,
                              int regens) noexcept {
    const DiscoveryPoolView pv = discovery_pool_view(discovery_pool(item));
    for (int r = 0; r < regens; ++r) {
        CardId scratch[kDiscoveryChoiceCount]{};
        generate_discovery_offer(s, pv, scratch);
    }
}

// DiscoveryAction.update's selection half (DiscoveryAction.java:53-85). It ALWAYS
// makes TWO makeStatEquivalentCopy()s of the chosen card (:55-56) and
// setCostForTurn(0)s both (:61-62), then fans them out by `amount`:
//
//   amount == 1 (:65-71)  one copy to hand, or to DISCARD if the hand is full;
//                         the second copy is dropped on the floor (disCard2 =
//                         null) and never reaches a pile.
//   amount == 2           hand + 2 <= 10 -> both to hand (:72-74);
//                         hand == 9      -> first to hand, second to discard
//                                           (:75-77);
//                         otherwise      -> both to discard (:78-80).
//
// Those four cases ARE sequential add-with-spill: at hand 8 both fit, at hand 9
// the first fills the hand and the second spills, at hand 10 both spill. So the
// fan-out is reproduced exactly by calling add_library_copy_to_hand once per
// copy -- its per-card spill (interp_cards.cpp add_library_copy_to_hand) is the
// same rule -- rather than by transcribing the branch table. Both copies are of
// the SAME card.
//
// MasterRealityPower's upgrade (:57-60) is Watcher-only and out of S1 scope.
void resolve_discovery_choice(CombatState& s,
                              const ActionQueueItem& item,
                              uint8_t slot) noexcept {
    if (!discovery_choice_prepared(item) || slot >= kDiscoveryChoiceCount) {
        return;
    }
    const CardId id = discovery_choice_card(item, slot);
    if (id == CardId::NONE) {
        return;
    }
    const int copies = discovery_copies(item);
    for (int i = 0; i < copies; ++i) {
        add_library_copy_to_hand(s, id, /*free_this_turn=*/true);
    }
}

// CODEX open tick (CodexAction.java:33-36): generateCardChoices() -- the SAME
// three-distinct rejection sampler as DISCOVERY, but over the fixed no-arg
// returnTrulyRandomCardInCombat() pool, i.e. the RED combat pool (:50-64 --
// NOT a colorless pool; the class imports nothing colorless and :54 calls the
// no-arg overload). One offer, prepared exactly once, in the shared item
// packing. The all-monsters-basically-dead zero-draw early-out (:29-32) lives
// at the pump's interception site, not here: it decides whether this is
// reached at all.
void prepare_codex_choice(CombatState& s, ActionQueueItem& item) noexcept {
    if (discovery_choice_prepared(item)) {
        return;
    }
    const DiscoveryPoolView pv = discovery_pool_view(DiscoveryPool::COMBAT);
    CardId offered[kDiscoveryChoiceCount]{};
    generate_discovery_offer(s, pv, offered);
    item.flags =
        static_cast<uint32_t>(static_cast<uint16_t>(offered[0])) |
        (static_cast<uint32_t>(static_cast<uint16_t>(offered[1])) << 16u);
    item.amount =
        static_cast<int32_t>(static_cast<uint16_t>(offered[2]));
}

// CODEX resolution (CodexAction.java:38-46): the picked card is
// makeStatEquivalentCopy'd -- of a FRESH base offer card, so a base library
// copy at its REGISTRY cost (no setCostForTurn anywhere in the action, unlike
// Discovery) -- and handed to ShowCardAndAddToDrawPileEffect(..., randomSpot =
// true), whose constructor runs drawPile.addToRandomSpot immediately
// (ShowCardAndAddToDrawPileEffect.java:47-48): ONE cardRandomRng.random(size-1)
// insert, or a free append when the pile is empty (CardGroup.java:463-468).
// op_make_card's DRAW_RANDOM arm is exactly that, so it is called rather than
// re-derived. A skip never reaches here (discoveryCard stays null, :39); there
// are NO wasted regenerations on either path (the generator sits inside the
// open tick's branch -- the one structural difference from DiscoveryAction).
void resolve_codex_choice(CombatState& s, const ActionQueueItem& item,
                          uint8_t slot) noexcept {
    if (!discovery_choice_prepared(item) || slot >= kDiscoveryChoiceCount) {
        return;
    }
    const CardId id = discovery_choice_card(item, slot);
    if (id == CardId::NONE) {
        return;
    }
    op_make_card(s, static_cast<uint16_t>(id), CardPile::DRAW_RANDOM,
                 /*count=*/1, /*upgraded=*/false, /*self_copy=*/false,
                 /*source_pi=*/CardPoolIndex{0});
}

// --- Public: CHOOSE_CARD queries ---------------------------------------------

bool choice_slot_eligible(const CombatState& s, uint8_t slot,
                          ChoiceKind kind, uint8_t type_filter) noexcept {
    if (kind == ChoiceKind::DRAW_TO_HAND) {
        // Secret Technique / Secret Weapon. The browse list is exactly the
        // draw-pile cards of one CardType (SkillFromDeckToHandAction.java:36-39
        // / AttackFromDeckToHandAction.java:36-39), so eligibility IS that type
        // test against a real draw-pile slot. A full hand is NOT an
        // ineligibility here, unlike Exhume: the action still runs and the
        // chosen card is consumed into the discard pile instead (:46-48).
        return slot < s.draw_count &&
               instance_has_type(s, s.draw[slot], type_filter);
    }
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
        // Any discard card is a legal pick, for both discard-source kinds:
        // DiscardPileToTopOfDeckAction has no eligibility filter, and
        // BetterDiscardPileToHandAction has none either. The just-played source
        // card is in the LIMBO pile, never in this one.
        //
        // A FULL HAND IS NOT AN INELIGIBILITY for DISCARD_TO_HAND_FREE, unlike
        // Exhume: BetterDiscardPileToHandAction still opens its screen and still
        // consumes the pick, and it is the PER-CARD body that declines to move a
        // card that does not fit (:63-69 / :92-98), leaving it in the discard.
        // Making it ineligible here would change the eligible COUNT, and that
        // count decides forced-vs-prompted.
        return slot < s.discard_count;
    }
    if (slot >= s.hand_count) {
        return false;
    }
    if (kind == ChoiceKind::UPGRADE) {
        // (Searing Blow is an ATTACK, so the type rejections would not have
        // fired for it either way; can_upgrade_instance's override-first
        // ordering is written to mirror Java dispatch, not to rely on that
        // coincidence.) Curses and statuses are never upgrade targets -- this
        // is reachable in combat: Writhe is an innate curse that opens in
        // hand, and Wound / Burn / Slimed enter hand mid-combat. Beyond
        // mis-targeting, the eligible COUNT feeds choice_requires_user /
        // op_choose_card's forced-vs-prompted branch (ArmamentsAction.java:
        // 46-60 counts exactly the canUpgrade() cards), so an inflated count
        // would change whether a hand-select screen opens at all.
        return can_upgrade_instance(s, s.hand[slot]);
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
    if (choose_is_optional(item.flags)) {
        // The screen stays open until the player presses confirm, so the ONLY
        // thing that can stop it opening is having nothing to show. Cards
        // already picked still count: they are the selected suffix of `hand`,
        // and a screen where everything has been picked is still an open screen
        // awaiting the button. Both optional callers' guards are exactly
        // "is the hand empty" (ExhaustAction.java:76-79,
        // ForethoughtAction.java:33-36), and both select from the whole hand, so
        // the hand count IS the eligible count for them.
        return choice_pile_count(s, kind) > 0;
    }
    return count_eligible(s, kind, choose_type_filter_from_flags(item.flags)) >
           item.amount;
}

// --- OPTIONAL (zero-to-N) selection ------------------------------------------
//
// The selected picks live as the trailing suffix of `hand`, their count in the
// item's flags nibble (interp.hpp). These three helpers are the only writers of
// that invariant, and each mirrors one HandCardSelectScreen interaction.

namespace {

// Where the selected suffix begins: hand[0, unselected) is what is left of the
// hand, hand[unselected, hand_count) is selectedCards in pick order. The stored
// count is clamped against the live hand for safety -- a hand-built or
// translated state cannot make the split run off the array.
[[nodiscard]] uint8_t optional_unselected_count(const CombatState& s,
                                                const ActionQueueItem& item) noexcept {
    const uint8_t picked = choose_selected_count(item.flags);
    return picked >= s.hand_count ? 0u
                                  : static_cast<uint8_t>(s.hand_count - picked);
}

}  // namespace

bool optional_choice_slot_legal(const CombatState& s, const ActionQueueItem& item,
                                uint8_t slot) noexcept {
    if (static_cast<Opcode>(item.opcode) != Opcode::CHOOSE_CARD ||
        !choose_is_optional(item.flags) || slot >= s.hand_count) {
        return false;
    }
    const uint8_t unselected = optional_unselected_count(s, item);
    if (slot >= unselected) {
        return true;  // deselect: never capped (updateSelectedCards, :441-447)
    }
    // selectHoveredCard only takes a card while there is room for it
    // (`numCardsToSelect > selectedCards.group.size()`, :375). Purity's cap is
    // its magicNumber; upgraded Forethought's is the Java's literal 99, which no
    // hand can reach.
    const ChoiceKind kind = choose_kind_from_flags(item.flags);
    if (!choice_slot_eligible(s, slot, kind,
                              choose_type_filter_from_flags(item.flags))) {
        return false;
    }
    return static_cast<int>(choose_selected_count(item.flags)) < item.amount;
}

void toggle_optional_choice_slot(CombatState& s, ActionQueueItem& item,
                                 uint8_t slot) noexcept {
    if (slot >= s.hand_count) {
        return;
    }
    const uint8_t unselected = optional_unselected_count(s, item);
    const CardPoolIndex pi = s.hand[slot];
    if (slot < unselected) {
        // SELECT: hand.removeCard(c) then selectedCards.addToTop(c) (:378-381).
        // Both are list operations on ONE combined array here -- lift the card
        // out (the rest closes up, preserving relative order) and append it at
        // the very end, which is where addToTop puts it.
        for (uint8_t j = static_cast<uint8_t>(slot + 1); j < s.hand_count; ++j) {
            s.hand[j - 1] = s.hand[j];
        }
        s.hand[s.hand_count - 1] = pi;
        item.flags = with_choose_selected_count(
            item.flags,
            static_cast<uint8_t>(choose_selected_count(item.flags) + 1));
        return;
    }
    // DESELECT: `AbstractDungeon.player.hand.addToTop(e); i.remove();`
    // (:441-443). The card goes to the END of the hand group -- NOT back to
    // where it was picked from -- and leaves selectedCards, so in the combined
    // array it lands at the split point and the picks before it shift up one.
    for (uint8_t j = slot; j > unselected; --j) {
        s.hand[j] = s.hand[j - 1];
    }
    s.hand[unselected] = pi;
    item.flags = with_choose_selected_count(
        item.flags, static_cast<uint8_t>(choose_selected_count(item.flags) - 1));
}

void resolve_optional_choice(CombatState& s, const ActionQueueItem& item) noexcept {
    const uint8_t picked = choose_selected_count(item.flags);
    if (picked == 0 || picked > s.hand_count) {
        return;  // an empty confirm walks an empty selectedCards.group
    }
    const ChoiceKind kind = choose_kind_from_flags(item.flags);
    const uint8_t unselected = static_cast<uint8_t>(s.hand_count - picked);
    // Snapshot the picks in PICK ORDER first: each apply mutates the hand.
    CardPoolIndex sel[kHandCap];
    for (uint8_t i = 0; i < picked; ++i) {
        sel[i] = s.hand[static_cast<uint8_t>(unselected + i)];
    }
    // `for (AbstractCard c : handCardSelectScreen.selectedCards.group)` --
    // ExhaustAction.java:102-105 and ForethoughtAction.java:55-61 both iterate
    // the selection in the order it was built, so the exhaust-pile order and the
    // draw-bottom order are the PICK order.
    for (uint8_t i = 0; i < picked; ++i) {
        for (uint8_t h = 0; h < s.hand_count; ++h) {
            if (s.hand[h] == sel[i]) {
                apply_choice_to_slot(s, h, kind,
                                     choose_copies_from_flags(item.flags));
                break;
            }
        }
    }
    if (kind == ChoiceKind::HAND_TO_DISCARD_THEN_DRAW) {
        // GamblingChipAction.java:53 -- `addToTop(new DrawCardAction(p,
        // selectedCards.group.size()))`. ONE DrawCardAction for the whole
        // selection, sized by the pick count read AT CONFIRM.
        //
        // Order: the Java's addToTop runs BEFORE the discard loop textually, but
        // it only queues, while the loop is synchronous inside the same
        // update(). So the observable sequence is discard-all-then-draw, which
        // is what this is. The `picked == 0` early return above is the
        // `if (!selectedCards.group.isEmpty())` guard at :52 -- an empty confirm
        // queues no draw at all.
        //
        // add_to_TOP, not bottom: the draw must resolve ahead of anything queued
        // behind this action (Gambling Chip's atTurnStartPostDraw queues a
        // RelicAboveCreatureAction alongside it, and other relics' turn-start
        // hooks queue after). The caller pops the finished CHOOSE_CARD item
        // BEFORE calling this, so the head this prepends to is the real one.
        ActionQueueItem draw{};
        draw.opcode = static_cast<uint16_t>(Opcode::DRAW);
        draw.src = kActorPlayer;
        draw.tgt = kActorPlayer;
        draw.amount = static_cast<int32_t>(picked);
        add_to_top(s, draw);
    }
}

void queue_gambling_chip_choice(CombatState& s) noexcept {
    // handCardSelectScreen.open(TEXT[..], 99, true, true)
    // (GamblingChipAction.java:43/:45): amount 99, anyNumber true, canPickZero
    // true -- the identical optional screen Elixir and upgraded Forethought
    // open. 99 is the action's literal and is the value the pick cap is compared
    // against, so it is authored as-is; the SELECTION is bounded by hand size,
    // which is <= kHandCap, so the 4-bit selected-count nibble is safe.
    ActionQueueItem item{};
    item.opcode = static_cast<uint16_t>(Opcode::CHOOSE_CARD);
    item.src = kActorPlayer;
    item.tgt = kActorPlayer;  // hand-source choice: no exclusion index
    item.amount = 99;
    item.flags = make_choose_flags(ChoiceKind::HAND_TO_DISCARD_THEN_DRAW,
                                   /*random=*/false, /*copies=*/1,
                                   kChoiceNoTypeFilter, /*optional=*/true);
    add_to_bottom(s, item);
}

void prepare_choice_draw_source(CombatState& s, ActionQueueItem& item) noexcept {
    if (static_cast<Opcode>(item.opcode) != Opcode::CHOOSE_CARD ||
        (item.flags & kChoiceDrawTempBuiltBit) != 0u) {
        return;
    }
    const ChoiceKind kind = choose_kind_from_flags(item.flags);
    if (choice_source(kind) != ChoiceSource::DRAW) {
        return;
    }
    item.flags |= kChoiceDrawTempBuiltBit;
    // SkillFromDeckToHandAction.update / AttackFromDeckToHandAction.update do
    // this on their FIRST update tick (:34-39), before any of the size-0 /
    // size-1 / grid branches -- so the cost lands whether or not a screen ever
    // opens. Only the DRAWS matter: a selection names a real draw-pile slot,
    // and the real draw pile is not touched by the browse, so the temp list's
    // ORDER is unobservable and the built list is discarded here.
    CardPoolIndex tmp[kDrawCap];
    (void)build_draw_temp_group(s, choose_type_filter_from_flags(item.flags),
                                tmp);
}

void apply_choice_selection(CombatState& s, uint8_t slot, ChoiceKind kind,
                            int copies, bool prompted) noexcept {
    if (kind == ChoiceKind::UPGRADE && prompted) {
        // ArmamentsAction removes every cannotUpgrade card before opening
        // HandCardSelectScreen, then retrieves the selected card and finally
        // returnCards() appends those ineligibles in their original order
        // (ArmamentsAction.java:45-91). The full hand visible after the prompt
        // is therefore:
        //   [other upgradeables] + [selected, now upgraded] + [ineligibles].
        // Keeping the ineligible cards in place is mechanically observable:
        // STS300091's next `play 2` must name the selected Defend+ and gain 8
        // block; the old order named Bash and lost 7 HP at end turn.
        if (slot >= s.hand_count ||
            !choice_slot_eligible(s, slot, ChoiceKind::UPGRADE)) {
            return;
        }
        const CardPoolIndex sel = s.hand[slot];
        CardPoolIndex reordered[kHandCap];
        uint8_t n = 0;
        for (uint8_t i = 0; i < s.hand_count; ++i) {
            if (i != slot &&
                choice_slot_eligible(s, i, ChoiceKind::UPGRADE)) {
                reordered[n++] = s.hand[i];
            }
        }
        reordered[n++] = sel;
        for (uint8_t i = 0; i < s.hand_count; ++i) {
            if (i != slot &&
                !choice_slot_eligible(s, i, ChoiceKind::UPGRADE)) {
                reordered[n++] = s.hand[i];
            }
        }
        for (uint8_t i = 0; i < n; ++i) {
            s.hand[i] = reordered[i];
        }
        s.hand_count = n;
        upgrade_instance(s, sel);
        return;
    }
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

// --- APPLY_STASIS / STASIS_RETURN (S2.24, the Bronze Orb's card theft) --------

void op_apply_stasis(CombatState& s, uint8_t owner) noexcept {
    // ApplyStasisAction.update (ApplyStasisAction.java:32-80). Both piles empty
    // ends the action before ANY draw and before any power is applied (:34-37).
    if (s.draw_count == 0 && s.discard_count == 0) {
        return;
    }
    // Source pile: the DRAW pile unless it is empty, then the DISCARD (:39,:51).
    const bool from_draw = s.draw_count != 0;
    CardPoolIndex* pile = from_draw ? s.draw : s.discard;
    const uint8_t count = from_draw ? s.draw_count : s.discard_count;

    // getRandomCard(cardRandomRng, rarity), RARE -> UNCOMMON -> COMMON
    // (CardGroup.java:526-538). Each pass builds the rarity-filtered view in
    // GROUP ORDER (index 0 == pile bottom; both engine pile arrays share that
    // convention -- draw[draw_count-1] is the top), Collections.sort()s it by
    // cardID (AbstractCard.compareTo == cardID.compareTo, :2583-2584 -- the
    // GAME_ID string, and the sort is STABLE, so two Strikes keep their pile
    // order), and spends ONE card_random_rng random(size-1). An EMPTY view
    // returns null WITHOUT drawing. The rarity read is the LIVE CardRarity
    // (registry card_rarity): a BASIC Strike matches no pass, a Wound is
    // COMMON, a poolable curse is CURSE and only the fallback can take it.
    static_assert(kDrawCap >= kDiscardCap,
                  "cand[] below is sized by the larger pile");
    struct Cand {
        std::string_view gid;
        uint8_t pos;  // position in the pile array (group order)
    };
    Cand cand[kDrawCap];
    int picked_pos = -1;
    constexpr sts::registry::CardRarity kCascade[3] = {
        sts::registry::CardRarity::RARE, sts::registry::CardRarity::UNCOMMON,
        sts::registry::CardRarity::COMMON};
    for (const sts::registry::CardRarity want : kCascade) {
        uint8_t n = 0;
        for (uint8_t i = 0; i < count; ++i) {
            const CardId cid = static_cast<CardId>(s.card_pool[pile[i]].card_id);
            if (sts::registry::card_rarity(cid) != want) {
                continue;
            }
            cand[n].gid = sts::registry::card_game_id(cid);
            cand[n].pos = i;
            ++n;
        }
        if (n == 0) {
            continue;  // null, no draw
        }
        // Stable insertion sort by game_id -- Collections.sort is stable and
        // ties (identical cardIDs) must keep group order. In-place, no heap
        // (std::stable_sort may allocate; N <= 128 makes this cheap).
        for (uint8_t i = 1; i < n; ++i) {
            const Cand key = cand[i];
            int j = static_cast<int>(i) - 1;
            while (j >= 0 && key.gid < cand[j].gid) {
                cand[j + 1] = cand[j];
                --j;
            }
            cand[j + 1] = key;
        }
        const int32_t pick =
            random(s.card_random_rng, static_cast<int32_t>(n) - 1);
        picked_pos = cand[pick].pos;
        break;
    }
    if (picked_pos < 0) {
        // The unfiltered fallback, getRandomCard(rng) (CardGroup.java:498-500):
        // ONE draw indexing the pile IN PILE ORDER -- no filter, no sort.
        picked_pos = static_cast<int>(
            random(s.card_random_rng, static_cast<int32_t>(count) - 1));
    }

    // removeCard + player.limbo.addToBottom(card) (:50,:62,:64): the ORIGINAL
    // instance moves -- everything the row carries (upgrade count, Rampage
    // misc, a permanent cost write) survives, which is exactly what the return
    // path's makeSameInstanceOf copy preserves in the game.
    const CardPoolIndex pi = pile[picked_pos];
    if (from_draw) {
        for (uint8_t j = static_cast<uint8_t>(picked_pos);
             j + 1 < s.draw_count; ++j) {
            s.draw[j] = s.draw[j + 1];
        }
        --s.draw_count;
        // Observer: the theft is player-visible (the addToTop'd ShowCardAction,
        // :78, displays the stolen card), so the pool index leaves the
        // knowledge chain; entries above a known-exact prefix keep their
        // positions -- an exact position excludes the stolen card having sat
        // between them (knowledge.cpp chain_remove_at).
        knowledge_on_remove_known(s, pi);
    } else {
        for (uint8_t j = static_cast<uint8_t>(picked_pos);
             j + 1 < s.discard_count; ++j) {
            s.discard[j] = s.discard[j + 1];
        }
        --s.discard_count;
    }
    limbo_add(s, pi);

    // addToTop(new ApplyPowerAction(owner, owner, new StasisPower(owner, card)))
    // (:77). The ShowCardAction addToTop'd after it (:78) resolves first and is
    // presentation. Amount is StasisPower's explicit -1 (StasisPower.java:27);
    // the stolen pool index rides the APPLY_POWER counter operand into the
    // slot's `counter` as index + 1 (powers.yaml id 98).
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = owner;
    apply.tgt = owner;
    apply.amount = -1;
    apply.flags = make_apply_power_flags(PowerId::STASIS,
                                         static_cast<int>(pi) + 1);
    add_to_top(s, apply);
}

void op_stasis_return(CombatState& s, const ActionQueueItem& item) noexcept {
    // StasisPower.onDeath queued this with the destination decided THEN
    // (`player.hand.size() != 10`, StasisPower.java:39); the HAND arm still
    // spills to the discard if the hand has filled by RESOLVE time
    // (MakeTempCardInHandAction.update:71-77) -- e.g. a boss-death sweep that
    // returns several stolen cards back-to-back.
    if (item.amount < 0 || item.amount >= kCardPoolCap) {
        return;
    }
    const CardPoolIndex pi = static_cast<CardPoolIndex>(item.amount);
    if (!limbo_remove(s, pi)) {
        return;  // defensive: the card is not parked (nothing stole it)
    }
    if (stasis_return_to_hand(item.flags) && s.hand_count < kHandCap) {
        s.hand[s.hand_count++] = pi;
        // ShowCardAndAddToHandEffect:47 / :69 -- the stolen card comes back
        // through MakeTempCardInHandAction (StasisPower.java:40), so a returned
        // SKILL with a live cost is zeroed for the combat under Corruption. The
        // MakeTempCardInDiscardAction arm (:42) and the resolve-time spill below
        // are ShowCardAndAddToDiscardEffect, which has no such step.
        corruption_hand_cost_sweep(s);
    } else if (s.discard_count < kDiscardCap) {
        s.discard[s.discard_count++] = pi;
    }
}

}  // namespace sts::engine
