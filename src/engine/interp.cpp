// Effect interpreter -- opcode dispatch + the DAMAGE pipeline. See interp.hpp
// for the full design/provenance notes and the scope-boundary/field-encoding
// decisions (SHUFFLE_IN & ROLL_MOVE stubs, NOP == 0, APPLY_POWER flags
// encoding, EXHAUST via `amount` pool index, kActorPlayer actor sentinel).
//
// Provenance: DamageInfo.java:35-100, MathUtils.java:217, StrengthPower.java:
// 92-98, VulnerablePower.java:61-73, WeakPower.java:61-70. Design doc §5.5, §6,
// §10 trap 1.

#include "sts/engine/interp.hpp"

#include <cstdint>

#include "sts/engine/action_queue.hpp"
#include "sts/engine/card_play.hpp"     // roll_random_target (dequeue-time random enemy)
#include "sts/engine/cards.hpp"         // card_def / card_cost / card_flags (MAKE_CARD)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/piles.hpp"   // draw_cards / shuffle_discard_into_draw / exhaust_card
#include "sts/engine/power_hooks.hpp"   // B3.2 hook dispatch (onGainedBlock/onApplyPower/wasHPLost/onCardDraw)
#include "sts/engine/powers.hpp"        // power_def / PowerType (APPLY_POWER interception)
#include "sts/engine/rng_stream.hpp"    // random (DRAW_RANDOM insert position)
#include "sts/engine/types.hpp"

namespace sts::engine {

namespace {

// --- Actor power views ------------------------------------------------------

struct PowerView {
    const PowerSlot* slots;  // may be null when actor is out of range
    uint8_t count;
};

// Powers of an actor (kActorPlayer -> player_powers, 0..4 -> that monster).
// Out-of-range actors yield an empty view so callers iterate nothing and never
// index OOB (defensive against junk src/tgt in malformed items).
[[nodiscard]] PowerView actor_powers(const CombatState& s, uint8_t actor) noexcept {
    if (actor == kActorPlayer) {
        return PowerView{s.player_powers, s.player_power_count};
    }
    if (actor < kMonsterCap) {
        return PowerView{s.monsters[actor].powers, s.monsters[actor].power_count};
    }
    return PowerView{nullptr, 0};
}

// Mutable hp/block cells for an actor (null when out of range).
[[nodiscard]] int16_t* actor_hp(CombatState& s, uint8_t actor) noexcept {
    if (actor == kActorPlayer) {
        return &s.player_hp;
    }
    if (actor < kMonsterCap) {
        return &s.monsters[actor].hp;
    }
    return nullptr;
}

[[nodiscard]] int16_t* actor_block(CombatState& s, uint8_t actor) noexcept {
    if (actor == kActorPlayer) {
        return &s.player_block;
    }
    if (actor < kMonsterCap) {
        return &s.monsters[actor].block;
    }
    return nullptr;
}

// --- Damage hooks (NORMAL damage only; skeleton powers) ---------------------
// Iteration order == power-list order == application order (design doc §4.1).
// Every hook is a float op mirroring the cited Java exactly.

// atDamageGive: attacker-owned hooks. StrengthPower.atDamageGive (+amount),
// WeakPower.atDamageGive (*0.75f). Others pass through. `strength_mult` scales the
// Strength contribution (Heavy Blade counts Strength x magicNumber by temporarily
// multiplying strength.amount before applyPowers; HeavyBlade.java:426-435). The
// default 1 is bit-identical to the pre-B3.3 hook: float(amount) * 1.0f == float(
// amount), so every non-Heavy-Blade damage number is unchanged.
[[nodiscard]] float at_damage_give(float dmg, PowerSlot p,
                                   int strength_mult = 1) noexcept {
    switch (static_cast<PowerId>(p.power_id)) {
        case PowerId::STRENGTH:                        // StrengthPower.java:96
            return dmg + static_cast<float>(p.amount) *
                             static_cast<float>(strength_mult);
        case PowerId::WEAK:
            return dmg * 0.75f;                         // WeakPower.java:67
        default:
            return dmg;
    }
}

// atDamageReceive: target-owned hooks. VulnerablePower.atDamageReceive (*1.5f).
[[nodiscard]] float at_damage_receive(float dmg, PowerSlot p) noexcept {
    switch (static_cast<PowerId>(p.power_id)) {
        case PowerId::VULNERABLE:
            return dmg * 1.5f;                          // VulnerablePower.java:70
        default:
            return dmg;
    }
}

// modifyBlock (block-gain hook, distinct from the damage hooks above):
// DexterityPower.modifyBlock adds `amount` to the block gained, flooring the
// running total at 0 (DexterityPower.java:106-111). The only skeleton block
// modifier; iterated in power-list order over the block GAINER's powers. Applied
// by op_block for CARD block only (power/relic/potion block sets kBlockNoPowers).
[[nodiscard]] float modify_block(float blk, PowerSlot p) noexcept {
    switch (static_cast<PowerId>(p.power_id)) {
        case PowerId::DEXTERITY: {
            const float m = blk + static_cast<float>(p.amount);
            return m < 0.0f ? 0.0f : m;                 // modifyBlock floors at 0
        }
        default:
            return blk;
    }
}

// atDamageFinalGive / atDamageFinalReceive: no skeleton power overrides these
// (Strength/Vulnerable/Weak leave AbstractPower's identity default), so both
// are pass-throughs. Kept as explicit call sites so a future power that hooks
// the "final" pass slots in without moving the pipeline.
[[nodiscard]] float at_damage_final_give(float dmg, PowerSlot /*p*/) noexcept {
    return dmg;
}
[[nodiscard]] float at_damage_final_receive(float dmg, PowerSlot /*p*/) noexcept {
    return dmg;
}

// Player stance hooks. The skeleton is stanceless (design doc §4.2: stance
// field is a "0 = None" placeholder), so both are documented identity stubs --
// written as real call sites so a future stance system attaches here without
// touching the pipeline shape.
[[nodiscard]] float stance_at_damage_give(const CombatState& /*s*/, float dmg) noexcept {
    return dmg;  // AbstractStance.atDamageGive default (no stance active)
}
[[nodiscard]] float stance_at_damage_receive(const CombatState& /*s*/, float dmg) noexcept {
    return dmg;  // AbstractStance.atDamageReceive default (no stance active)
}

// --- Opcode bodies ----------------------------------------------------------

// DAMAGE: compute output via the pipeline, then land it on tgt -- block absorbs
// first (decrementBlock: block soaks up to its value), remainder hits hp,
// currentHealth clamped >= 0. (Death/onDeath handling is not yet modeled; the
// pump's hp<=0 check drives the COMBAT_OVER transition.)
void op_damage(CombatState& s, uint8_t src, uint8_t tgt, int base,
               int strength_mult = 1,
               DamageType type = DamageType::NORMAL) noexcept {
    if (tgt != kActorPlayer && tgt >= kMonsterCap) {
        return;
    }
    // THORNS / HP_LOSS damage skips the NORMAL-only power pipeline (every skeleton
    // applyPowers hook is `if (type == NORMAL)` in the Java): a Vulnerable attacker
    // does NOT amplify reflected Thorns, and player Strength/Weak do not scale it.
    // NORMAL damage runs the full DamageInfo.applyPowers pipeline, carrying the
    // B3.3 strength multiplier (Heavy-Blade-style attacks).
    const int out = (type == DamageType::NORMAL)
                        ? compute_damage(s, src, tgt, base, strength_mult)
                        : (base < 0 ? 0 : base);
    int16_t* hp = actor_hp(s, tgt);
    int16_t* blk = actor_block(s, tgt);
    if (hp == nullptr || blk == nullptr) {
        return;
    }
    int dmg = out;
    int block = *blk;
    if (dmg >= block) {
        dmg -= block;
        block = 0;
    } else {
        block -= dmg;
        dmg = 0;
    }
    *blk = static_cast<int16_t>(block);
    // onAttacked (AbstractPlayer.damage:1425-1426): the VICTIM's powers fire on a
    // NORMAL attack from a DISTINCT attacker -- AFTER decrementBlock and REGARDLESS
    // of whether damage penetrated (Thorns reflects even a fully-blocked hit). A
    // THORNS/HP_LOSS incoming does NOT trigger onAttacked (ThornsPower's own type
    // guard), so it is dispatched only for NORMAL damage with src != tgt. No-op
    // unless a power binds ON_ATTACKED, so skeleton/relic-free DAMAGE is unchanged.
    if (type == DamageType::NORMAL && src != tgt) {
        dispatch_on_attacked(s, tgt, src, dmg);
    }
    const int old_hp = *hp;
    int new_hp = old_hp - dmg;
    if (new_hp < 0) {
        new_hp = 0;
    }
    *hp = static_cast<int16_t>(new_hp);
    // wasHPLost (AbstractPlayer.damage:1445-1447): fires on the VICTIM's powers
    // for the HP actually lost, with the ATTACKER as source. Rupture's guard
    // (source == victim) means unblocked enemy damage does NOT grant Strength --
    // only self-inflicted (card) HP loss does; Plated Armor's guard also reads the
    // damage `type` (it does not decrement on THORNS/HP_LOSS). No-op without those,
    // so skeleton DAMAGE is unchanged.
    dispatch_was_hp_lost(s, tgt, src, old_hp - new_hp,
                         static_cast<uint8_t>(type));
}

// LOSE_HP: `tgt` loses `amount` HP directly, bypassing block (LoseHPAction /
// DamageInfo.HP_LOSS). Fires wasHPLost with source == tgt (SELF) -- the card /
// self HP-loss path that Rupture attributes to (Hemokinesis, Offering, Combust).
void op_lose_hp(CombatState& s, uint8_t tgt, int amount) noexcept {
    if (amount <= 0) {
        return;
    }
    int16_t* hp = actor_hp(s, tgt);
    if (hp == nullptr) {
        return;
    }
    const int old_hp = *hp;
    int new_hp = old_hp - amount;
    if (new_hp < 0) {
        new_hp = 0;
    }
    *hp = static_cast<int16_t>(new_hp);
    // source == victim (self), HP_LOSS type (bypasses block; not a THORNS/NORMAL
    // attack -- so it drives Rupture but not Thorns/Plated Armor's attack guards).
    dispatch_was_hp_lost(s, tgt, tgt, old_hp - new_hp,
                         static_cast<uint8_t>(DamageType::HP_LOSS));
}

// BLOCK: tgt gains `amount` block. CARD block (flags & kBlockNoPowers == 0) runs
// the gainer's modifyBlock hooks (Dexterity) with a single floor, mirroring
// AbstractCard.applyPowers + GainBlockAction; power/relic/potion block sets
// kBlockNoPowers (a direct GainBlockAction -- no modifyBlock) so it takes the
// straight add. With no Dexterity present the modifyBlock pass is the identity and
// gain == amount, so the 20 combat fixtures (no Dexterity) stay byte-identical.
void op_block(CombatState& s, uint8_t tgt, int amount, uint32_t flags) noexcept {
    int16_t* blk = actor_block(s, tgt);
    if (blk == nullptr) {
        return;
    }
    int gain = amount;
    if ((flags & kBlockNoPowers) == 0u) {
        float tmp = static_cast<float>(amount);
        const PowerView pv = actor_powers(s, tgt);
        for (uint8_t i = 0; i < pv.count; ++i) {
            tmp = modify_block(tmp, pv.slots[i]);   // Dexterity: + amount, floor 0
        }
        if (tmp < 0.0f) {
            tmp = 0.0f;                             // GainBlockAction post-clamp
        }
        gain = mathutils_floor(tmp);
    }
    int nb = *blk + gain;
    if (nb < 0) {
        nb = 0;
    }
    *blk = static_cast<int16_t>(nb);
    // onGainedBlock (Juggernaut): fires after the block gain, on the actor's own
    // powers, only for a positive gain (AbstractCreature.addBlock:426-433), with
    // the ACTUAL block gained (post-Dexterity). No-op unless a power binds
    // ON_GAINED_BLOCK -- so the skeleton BLOCK is unchanged.
    dispatch_on_gained_block(s, tgt, gain);
}

// APPLY_POWER: stack PowerId(flags) x amount onto tgt. Stacks onto an existing
// slot of the same id, else appends a new slot (hard cap kPowerCap -- overflow
// is a silent no-op here rather than an assert, since a malformed item must not
// crash; real card play never overflows 24 skeleton powers).
//
// B3.2 interception (ApplyPowerAction.java:106-138): (1) the SOURCE's powers'
// onApplyPower fire FIRST (Sadistic queues damage on a debuffed target); (2) if
// the TARGET has Artifact and the applied power is a DEBUFF, one Artifact stack is
// consumed and the power does NOT land. Both are no-ops without Sadistic/Artifact,
// so skeleton APPLY_POWER (Bash's Vulnerable, Bellow's Strength) is unchanged.
void op_apply_power(CombatState& s, uint8_t src, uint8_t tgt, PowerId id,
                    int amount) noexcept {
    if (id == PowerId::NONE) {
        return;
    }
    const PowerDef* applied_def = power_def(id);
    const bool is_debuff =
        applied_def != nullptr && applied_def->type == PowerType::DEBUFF;
    // (1) source-side onApplyPower (fires before the power lands).
    dispatch_on_apply_power_source(s, src, tgt, static_cast<uint16_t>(id),
                                   is_debuff);
    // (2) target-side Artifact nullify: a consumed Artifact stack blocks the debuff.
    if (apply_power_blocked_by_artifact(s, tgt, is_debuff)) {
        return;
    }
    PowerSlot* slots = nullptr;
    uint8_t* count = nullptr;
    if (tgt == kActorPlayer) {
        slots = s.player_powers;
        count = &s.player_power_count;
    } else if (tgt < kMonsterCap) {
        slots = s.monsters[tgt].powers;
        count = &s.monsters[tgt].power_count;
    } else {
        return;
    }
    const uint16_t pid = static_cast<uint16_t>(id);
    for (uint8_t i = 0; i < *count; ++i) {
        if (slots[i].power_id == pid) {
            slots[i].amount = static_cast<int16_t>(slots[i].amount + amount);
            return;
        }
    }
    if (*count >= kPowerCap) {
        return;
    }
    if (id == PowerId::CURL_UP && tgt < kMonsterCap) {
        // A newly-created CurlUpPower starts with triggered=false
        // (CurlUpPower.java:25,27-34). Existing instances preserve the latch when
        // stacked; only the new-slot path clears it.
        s.monsters[tgt].flags = static_cast<uint16_t>(
            s.monsters[tgt].flags & ~kMonsterFlagCurlUpTriggered);
    }
    slots[*count].power_id = pid;
    slots[*count].amount = static_cast<int16_t>(amount);
    ++*count;
}

// DRAW and EXHAUST (and SHUFFLE_IN) are implemented in piles.cpp
// (draw_cards / exhaust_card / shuffle_discard_into_draw); the dispatch below
// delegates to them.

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

// --- CHOOSE_CARD helpers + body (Stage B B3.4) ------------------------------

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
    c.upgrade = static_cast<uint8_t>(c.upgrade + 1);
    c.cost_now = card_cost(*def, c.upgrade);
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

// Apply one CHOOSE_CARD manipulation to slot `slot` of the kind's SOURCE pile
// (hand for exhaust/put-on-draw-top/upgrade; discard for discard-to-draw-top).
void apply_choice_to_slot(CombatState& s, uint8_t slot, ChoiceKind kind) noexcept {
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
                    apply_choice_to_slot(s, i, kind);
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

// REMOVE_POWER (RemoveSpecificPowerAction): drop PowerId(flags) from tgt's power
// list (shifting the tail down). No-op if the actor lacks the power.
void op_remove_power(CombatState& s, uint8_t tgt, PowerId id) noexcept {
    PowerSlot* slots = nullptr;
    uint8_t* count = nullptr;
    if (tgt == kActorPlayer) {
        slots = s.player_powers;
        count = &s.player_power_count;
    } else if (tgt < kMonsterCap) {
        slots = s.monsters[tgt].powers;
        count = &s.monsters[tgt].power_count;
    } else {
        return;
    }
    const uint16_t pid = static_cast<uint16_t>(id);
    for (uint8_t i = 0; i < *count; ++i) {
        if (slots[i].power_id == pid) {
            for (uint8_t j = static_cast<uint8_t>(i + 1); j < *count; ++j) {
                slots[j - 1] = slots[j];
            }
            --*count;
            slots[*count] = PowerSlot{};  // zero the vacated tail slot
            if (id == PowerId::CURL_UP && tgt < kMonsterCap) {
                // RemoveSpecificPowerAction destroys the CurlUpPower instance;
                // its private triggered latch leaves with it.
                s.monsters[tgt].flags = static_cast<uint16_t>(
                    s.monsters[tgt].flags & ~kMonsterFlagCurlUpTriggered);
            }
            return;
        }
    }
}

// SET_COST: set card_pool[src].cost_now = amount (Stage B B3.1 cost-modifier
// write path). Clamped to the u8 cost_now range. The "temporary" (per-turn
// reset) and "which card / under what condition" logic belongs to the consumer
// (a power hook -- B3.2 -- or a CHOOSE selection); this is the write primitive.
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

}  // namespace

// --- Public: DAMAGE pipeline -------------------------------------------------

int compute_damage(const CombatState& s, uint8_t src, uint8_t tgt,
                   int base) noexcept {
    return compute_damage(s, src, tgt, base, /*strength_mult=*/1);
}

int compute_damage(const CombatState& s, uint8_t src, uint8_t tgt, int base,
                   int strength_mult) noexcept {
    const PowerView owner = actor_powers(s, src);
    const PowerView target = actor_powers(s, tgt);
    float tmp = static_cast<float>(base);

    if (src != kActorPlayer) {
        // Monster-owned attack (DamageInfo.java:39-70). Target is (for the
        // skeleton) the player, so the stance hook is atDamageReceive and sits
        // AFTER the target's atDamageReceive loop. (strength_mult is only ever != 1
        // for the player's Heavy Blade, so it is a no-op on this branch.)
        for (uint8_t i = 0; i < owner.count; ++i) {
            tmp = at_damage_give(tmp, owner.slots[i], strength_mult);
        }
        for (uint8_t i = 0; i < target.count; ++i) {
            tmp = at_damage_receive(tmp, target.slots[i]);
        }
        tmp = stance_at_damage_receive(s, tmp);
        for (uint8_t i = 0; i < owner.count; ++i) {
            tmp = at_damage_final_give(tmp, owner.slots[i]);
        }
        for (uint8_t i = 0; i < target.count; ++i) {
            tmp = at_damage_final_receive(tmp, target.slots[i]);
        }
    } else {
        // Player-owned attack (DamageInfo.java:71-99). Owner is the player, so
        // the stance hook is atDamageGive and sits AFTER the owner's
        // atDamageGive loop, BEFORE the target's atDamageReceive loop.
        for (uint8_t i = 0; i < owner.count; ++i) {
            tmp = at_damage_give(tmp, owner.slots[i], strength_mult);
        }
        tmp = stance_at_damage_give(s, tmp);
        for (uint8_t i = 0; i < target.count; ++i) {
            tmp = at_damage_receive(tmp, target.slots[i]);
        }
        for (uint8_t i = 0; i < owner.count; ++i) {
            tmp = at_damage_final_give(tmp, owner.slots[i]);
        }
        for (uint8_t i = 0; i < target.count; ++i) {
            tmp = at_damage_final_receive(tmp, target.slots[i]);
        }
    }

    int out = mathutils_floor(tmp);  // one floor, at the very end (trap 1)
    if (out < 0) {
        out = 0;
    }
    return out;
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
        // canUpgrade(): !upgraded (and non-CURSE/STATUS -- every card in scope is
        // ATTACK/SKILL, so the type guard is vacuously satisfied). `upgrade` is a
        // count; 0 == not yet upgraded.
        return s.card_pool[s.hand[slot]].upgrade == 0;
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

void apply_choice_selection(CombatState& s, uint8_t slot,
                            ChoiceKind kind) noexcept {
    apply_choice_to_slot(s, slot, kind);
}

// --- Public: dispatch --------------------------------------------------------

void execute_opcode(CombatState& s, const ActionQueueItem& item) noexcept {
    // Dynamic-target resolution at EXECUTE time (interp.hpp decision (4)).
    if (item.tgt == kActorAllEnemies) {
        // AoE: a SEPARATE op (and, for DAMAGE, a separate DamageInfo) per LIVE
        // monster (DamageAllEnemiesAction skips isDeadOrEscaped). Snapshotting
        // the live set here matches the game resolving the AoE action in place.
        for (uint8_t i = 0; i < s.monster_count; ++i) {
            if (s.monsters[i].hp > 0) {
                ActionQueueItem one = item;
                one.tgt = i;
                execute_opcode(s, one);
            }
        }
        return;
    }
    if (item.tgt == kActorRandomEnemy) {
        // One uniformly-random LIVE monster, one card_random_rng draw per hit
        // (AttackDamageRandomEnemyAction re-rolls per resolution).
        const uint8_t t = roll_random_target(s);
        if (t != kActorPlayer) {
            ActionQueueItem one = item;
            one.tgt = t;
            execute_opcode(s, one);
        }
        return;
    }
    switch (static_cast<Opcode>(item.opcode)) {
        case Opcode::NOP:
            return;  // reserved safe no-op (value-init / unrecognized item)
        case Opcode::DAMAGE:
            // Plain DAMAGE: strength_mult 1; `flags` carries the DamageType
            // (0 == NORMAL for card attacks; THORNS for reflected damage).
            op_damage(s, item.src, item.tgt, item.amount, /*strength_mult=*/1,
                      damage_type_from_flags(item.flags));
            return;
        case Opcode::BLOCK:
            op_block(s, item.tgt, item.amount, item.flags);
            return;
        case Opcode::APPLY_POWER:
            op_apply_power(s, item.src, item.tgt,
                           apply_power_id_from_flags(item.flags), item.amount);
            return;
        case Opcode::DRAW: {
            // draw, then fire onCardDraw per newly-drawn card (in draw order) --
            // Corruption zeroes a drawn skill's cost, Evolve/FireBreathing (later)
            // react to statuses. No-op without such a power.
            const uint8_t before = s.hand_count;
            const int drawn = draw_cards(s, item.amount);  // piles.cpp: cap + reshuffle
            for (int i = 0; i < drawn; ++i) {
                const uint8_t hi = static_cast<uint8_t>(before + i);
                if (hi >= s.hand_count) {
                    break;
                }
                const CardPoolIndex pi = s.hand[hi];
                dispatch_on_card_draw(s, pi, s.card_pool[pi].card_id);
            }
            return;
        }
        case Opcode::GAIN_ENERGY:
            // player_energy += amount; no max-energy field to clamp against
            // (Ironclad base 3 is a constant, no relic/potion raises it here).
            s.player_energy = static_cast<int16_t>(s.player_energy + item.amount);
            return;
        case Opcode::SHUFFLE_IN:
            shuffle_discard_into_draw(s);  // piles.cpp: shuffle_rng + JDK LCG
            return;
        case Opcode::EXHAUST:
            exhaust_card(s, item.amount);  // piles.cpp
            return;
        case Opcode::ROLL_MOVE:
            return;  // stub: Jaw Worm rolls inside its MonsterTurnFn
        case Opcode::MAKE_CARD:
            op_make_card(s, make_card_id_from_flags(item.flags),
                         static_cast<CardPile>(item.src), item.amount,
                         make_card_upgraded_from_flags(item.flags));
            return;
        case Opcode::SET_COST:
            op_set_cost(s, item.src, item.amount);
            return;
        case Opcode::LOSE_HP:
            op_lose_hp(s, item.tgt, item.amount);
            return;
        case Opcode::CHOOSE_CARD:
            // Reached only on the auto path (RANDOM or forced-all); a real prompt
            // is intercepted by the pump (choice_requires_user) before execute.
            op_choose_card(s, item);
            return;
        case Opcode::PLAY_TOP_DRAW:
            // `amount` carries the source card's pool index to exclude from the
            // deck (stamped by resolve_card_play; see op_play_top_draw / Havoc).
            op_play_top_draw(s, item.amount);
            return;
        case Opcode::REMOVE_POWER:
            op_remove_power(s, item.tgt, apply_power_id_from_flags(item.flags));
            return;
        case Opcode::DAMAGE_BLOCK:
            // Body Slam: base == the player's CURRENT block (read here at execute
            // time), then the normal DamageInfo pipeline (Strength/Vulnerable still
            // apply). `src` is the player (queue_effect_step), `amount` is unused.
            op_damage(s, item.src, item.tgt, s.player_block);
            return;
        case Opcode::DAMAGE_STR_MULT:
            // Heavy Blade: `amount` base with Strength counted x `flags` (the
            // magicNumber multiplier), then the normal pipeline.
            op_damage(s, item.src, item.tgt, item.amount,
                      static_cast<int>(item.flags));
            return;
        case Opcode::DAMAGE_PER_STRIKE:
            // Baked into a plain DAMAGE at queue time (card_play.cpp); never
            // reaches execute in practice. Safe no-op if it somehow does.
            return;
        default:
            return;  // any unrecognized opcode is a safe no-op (decision (3))
    }
}

}  // namespace sts::engine
