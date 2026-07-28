#pragma once

// Native relic PICKUP plumbing -- internal to src/engine (conventions §6: only
// `sts_engine`'s own .cpp files include this, so it lives beside them rather than
// in include/). This is the out-of-combat sibling of relic_native.hpp: where that
// one covers RelicHook bodies inside a combat, this covers the run-layer seams a
// relic sits on when it is DRAWN from a pool or PICKED UP.
//
// Three dispatch surfaces, not one. They are kept apart because they differ in
// every way that matters:
//
//   can_spawn       A PREDICATE. Pure: (RelicSpawnContext) -> bool, no RunState,
//                   no RNG. Consulted inside the pool-draw loop
//                   (return_random_relic_key / return_end_random_relic_key),
//                   where a `false` makes the draw pop another id -- so this
//                   surface is RNG-VISIBLE and a wrong answer moves the whole
//                   relicRng sequence. Default: true.
//   on_equip        An ACTION. Mutates RunState and may CONSUME miscRng (WarPaint
//                   / Whetstone each burn one randomLong for a
//                   Collections.shuffle, even when fewer than two cards qualify).
//                   Runs once, at acquisition, after the relic's slot has been
//                   appended. Default: nothing.
//   on_obtain_card  An ACTION over the card being added to the master deck, run
//                   for every owned relic in ACQUISITION order. Mutates the card
//                   instance (the eggs' upgrade-on-obtain) and/or RunState
//                   (Darkstone Periapt's max HP, Ceramic Fish's gold). No RNG.
//                   Default: nothing.
//
// All three tables are GENERATED from registry/relics.yaml `pickup:` (the
// STS_REGISTRY_RELIC_CAN_SPAWN / _ON_EQUIP / _ON_OBTAIN_CARD X-macros in the
// generated relic_table.hpp; the first two are expanded by relic_pools.cpp, the
// third by run_deck.cpp). A row that lists a surface but whose handler nobody
// wrote is a link error, exactly as for `native:` combat bodies. Bringing up a
// tier's relics is therefore: registry rows, one new .cpp under
// src/engine/relics/, one CMakeLists line -- and NO edit to relic_pools.cpp,
// run_deck.cpp, or any other tier's file. Two people filling in different tiers
// (rare/shop, boss/special) never touch the same source line, which is what the
// generated table in relic_hooks.cpp already achieved for the combat domain.

#include <span>

#include "sts/engine/cards.hpp"       // card_def, CardDef, CardType
#include "sts/engine/relic_pools.hpp"  // RelicSpawnContext
#include "sts/engine/rng_jdk.hpp"     // JdkRandom, jdk_shuffle, random_long
#include "sts/engine/rng_stream.hpp"  // RngStream
#include "sts/engine/run_state.hpp"   // RunState, RelicSlot, kMasterDeckCap
#include "sts/engine/types.hpp"       // RelicId

namespace sts::engine {

// One relic's canSpawn override. Pure predicate over the spawn context; the
// framework treats "no handler" as AbstractRelic's default `return true`.
//
// Each body spells its Java clause IN FULL, including the `Settings.isEndless ||`
// disjunct where the Java has one -- the endless bypass is per-relic in the game,
// not a framework rule, and three relics (MawBank/SmilingMask/Courier) AND it
// with a shop check that endless does NOT bypass.
using RelicCanSpawnSig = bool(const RelicSpawnContext& ctx) noexcept;
using RelicCanSpawnFn = RelicCanSpawnSig*;

// One relic's onEquip/instantObtain body. `slot` is the relic's just-appended
// live slot (its counter is already seeded from the row's initial_counter);
// `misc_rng` is the run's miscRng, and a body that consumes it must consume it
// exactly as the Java does.
using RelicOnEquipSig = void(RunState& rs, RngStream& misc_rng,
                             RelicSlot& slot) noexcept;
using RelicOnEquipFn = RelicOnEquipSig*;

// One relic's onObtainCard body. `card` is the master-deck row just appended
// (mutable: the eggs upgrade it in place); `def` is its registry row, already
// resolved by the caller so each handler does not repeat the lookup. Handlers run
// in acquisition order and see each other's edits, exactly as the Java's
// sequential `r.onObtainCard(c)` fan-out does.
using RelicOnObtainCardSig = void(RunState& rs, CardInstance& card,
                                  const CardDef& def) noexcept;
using RelicOnObtainCardFn = RelicOnObtainCardSig*;

// The generated dispatch tables (relic_pools.cpp for the first two, run_deck.cpp
// for the third): the handler for `id`, or nullptr when the row does not list
// that surface. A relic whose override is DEFERRED maps to its explicit empty
// body, not to nullptr.
[[nodiscard]] RelicCanSpawnFn relic_can_spawn_fn(RelicId id) noexcept;
[[nodiscard]] RelicOnEquipFn relic_on_equip_fn(RelicId id) noexcept;
[[nodiscard]] RelicOnObtainCardFn relic_on_obtain_card_fn(RelicId id) noexcept;

// AbstractPlayer.gainGold (AbstractPlayer.java:719-737), the ONE door every
// run-layer gold gain goes through:
//
//     if (hasRelic("Ectoplasm")) { flash(); return; }        // NO gold at all
//     if (amount <= 0) { log } else { gold += amount; relics' onGainGold(); }
//
// Two things live here rather than at each producer. (1) ECTOPLASM SUPPRESSES THE
// GAIN ENTIRELY -- it returns before the `+=`, so a producer that writes
// `rs.gold +=` directly silently ignores a registered boss relic; that is why
// relic_pickup_rare.cpp's Old Coin note asked for this helper "when the boss tier
// lands". (2) the relics' onGainGold fan-out belongs beside the write; no
// registered S1 relic overrides onGainGold (the corpus override is Bloody
// Idol, which is not an S1 row), so the fan-out is an empty pass today and is
// named rather than written. Maw Bank overrides onEnterRoom/onSpendGold, not
// onGainGold.
//
// Non-positive amounts are NOT added, matching the Java's else-branch.
inline void gain_gold(RunState& rs, int32_t amount) noexcept {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (rs.relics[i].relic_id == static_cast<uint16_t>(RelicId::ECTOPLASM)) {
            return;
        }
    }
    if (amount > 0) {
        rs.gold += amount;
    }
}

// AbstractPlayer.loseGold (AbstractPlayer.java:697-717), the gain_gold twin for
// every run-layer gold LOSS outside a purchase flow:
//
//     if (room instanceof ShopRoom) relics' onSpendGold();   // NOT here
//     if (amount > 0) { gold -= amount; if (gold < 0) gold = 0;
//                       relics' onLoseGold(); }
//
// Two hooks live in that body. onSpendGold is gated on being IN a ShopRoom, so
// it cannot fire from an event or from Neow; Maw Bank is its only S1 override
// and the shop layer is its caller (`in_shop`). onLoseGold still has no S1
// override at all and is named rather than written, for the same reason
// gain_gold names its empty onGainGold fan-out.
//
// THE GATE IS THE ROOM, NOT THE AMOUNT. The Java runs the onSpendGold fan-out
// BEFORE the `amount > 0` test, so a shop transaction fires it even for a
// zero-cost item -- which is why the fan-out below sits ahead of the early
// return rather than inside it. Non-positive amounts still change no gold, and
// the clamp is the Java's, not a defensive extra.
//
// MawBank.onSpendGold (MawBank.java:38-44) is `if (!usedUp) { flash();
// setCounter(-2); }` and setCounter(-2) (:46-53) is what marks it used up, so
// counter == -2 is the used-up encoding -- the same one event_framework.cpp's
// onEnterRoom share already reads. This is a fan-out over every held copy, not
// a getRelic, matching the Java's relic iteration.
inline void dispatch_relics_on_spend_gold(RunState& rs) noexcept {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        RelicSlot& slot = rs.relics[i];
        if (slot.relic_id == static_cast<uint16_t>(RelicId::MAW_BANK) &&
            slot.counter != -2) {
            slot.counter = -2;
        }
    }
}

// AbstractRelic.onEnterRestRoom, fired from RestRoom.onPlayerEntry
// (RestRoom.java:33-43, specifically :38-42) at AbstractDungeon.java:1800 --
// i.e. AFTER the onEnterRoom and justEnteredRoom fan-outs, not instead of them.
//
// Ancient Tea Set is the only implementor in the whole game
// (AncientTeaSet.onEnterRestRoom, AncientTeaSet.java:76-81): `flash();
// this.counter = -2; this.pulse = true;`. counter == -2 is the ARMED encoding
// the game itself uses, so this is a plain RunState.relics write that
// fold_back_combat/enter_combat already carry into the next combat, where
// relic_native_ancient_tea_set consumes it to -1 on turn 1.
//
// A fan-out over every held copy rather than a getRelic, matching the Java's
// relic iteration, and inline here beside the other run-layer relic fan-outs so
// the rest-room entry site needs exactly one call.
inline void dispatch_relics_on_enter_rest_room(RunState& rs) noexcept {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        RelicSlot& slot = rs.relics[i];
        if (slot.relic_id == static_cast<uint16_t>(RelicId::ANCIENT_TEA_SET)) {
            slot.counter = -2;  // setCounter(-2) == armed (AncientTeaSet.java:78)
        }
    }
}

inline void lose_gold(RunState& rs, int32_t amount,
                      bool in_shop = false) noexcept {
    if (in_shop) {
        dispatch_relics_on_spend_gold(rs);
    }
    if (amount <= 0) {
        return;
    }
    rs.gold -= amount;
    if (rs.gold < 0) {
        rs.gold = 0;
    }
}

// Upgrade up to two random not-yet-upgraded master-deck cards of `wanted`
// (WarPaint.onEquip WarPaint.java:36-59 / Whetstone.onEquip Whetstone.java:36-59
// -- identical bodies but for the CardType). Shared by both handlers, and inline
// here rather than file-local in relic_pools.cpp because the handlers moved out
// of that TU.
//
// Collections.shuffle evaluates miscRng.randomLong() as the Random ctor argument
// even when the filtered list has fewer than two cards, so the draw happens
// unconditionally -- that consumption is part of the RNG contract, not an
// optimization to skip.
//
// The eligibility test is canUpgrade(), NOT `upgrade == 0`. Both Java bodies
// spell `if (!c.canUpgrade() || c.type != <TYPE>) continue;`
// (WarPaint.java:37-40 / Whetstone.java:37-40), and SearingBlow.canUpgrade
// (SearingBlow.java:58-60) OVERRIDES the base with `return true`, so an already
// upgraded Searing Blow stays eligible and can be upgraded again. `upgrade == 0`
// silently excluded it -- and, because the shuffle is over the FILTERED list, a
// wrongly-sized candidate list changes which cards are picked, not merely how
// many. The CURSE/STATUS half of canUpgrade is already implied by the
// `type == wanted` test above (wanted is ATTACK or SKILL), so Searing Blow is
// the whole of the difference.
// AbstractCard.canUpgrade (AbstractCard.java:672-680) for a master-deck
// instance: CURSE and STATUS are never upgradeable, everything else is
// upgradeable while `!upgraded`. SearingBlow.canUpgrade (SearingBlow.java:58-60)
// replaces the whole body with `return true`, so it is tested FIRST -- an
// override runs INSTEAD OF, not after, the base. The in-combat twin is
// can_upgrade_instance (interp_cards.cpp) and the rest-site twin is
// rest_card_upgradeable (rest_sites.cpp); all three answer the same Java method
// over a different carrier.
[[nodiscard]] inline bool can_upgrade(const CardInstance& card) noexcept {
    if (card.card_id == static_cast<uint16_t>(CardId::SEARING_BLOW)) {
        return card.upgrade != UINT8_MAX;
    }
    const CardDef* def = card_def(static_cast<CardId>(card.card_id));
    if (def == nullptr || def->type == CardType::CURSE ||
        def->type == CardType::STATUS) {
        return false;
    }
    return card.upgrade == 0;
}

inline void upgrade_random_cards(RunState& rs, RngStream& misc_rng,
                                 CardType wanted) noexcept {
    uint16_t candidates[kMasterDeckCap]{};
    uint16_t count = 0;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        const CardInstance& card = rs.master_deck[i];
        const CardDef* def = card_def(static_cast<CardId>(card.card_id));
        if (def != nullptr && def->type == wanted && can_upgrade(card)) {
            candidates[count++] = i;
        }
    }

    JdkRandom jdk(random_long(misc_rng));
    jdk_shuffle(std::span<uint16_t>(candidates, count), jdk);
    const uint16_t take = count < 2 ? count : 2;
    for (uint16_t i = 0; i < take; ++i) {
        // AbstractCard.upgrade() INCREMENTS timesUpgraded; SearingBlow.upgrade
        // (SearingBlow.java:48-54) does the same and is the only card for which
        // the distinction is visible, since every other card is filtered out by
        // canUpgrade() once its count reaches 1. A blind `= 1` silently reset a
        // Searing Blow+2 back to +1 -- the other half of the same defect the
        // canUpgrade() filter above fixes. can_upgrade already refuses UINT8_MAX,
        // so the increment cannot wrap.
        ++rs.master_deck[candidates[i]].upgrade;
    }
}

}  // namespace sts::engine
