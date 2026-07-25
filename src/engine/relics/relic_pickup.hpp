#pragma once

// Native relic PICKUP plumbing -- internal to src/engine (conventions §6: only
// `sts_engine`'s own .cpp files include this, so it lives beside them rather than
// in include/). This is the out-of-combat sibling of relic_native.hpp: where that
// one covers RelicHook bodies inside a combat, this covers the run-layer seams a
// relic sits on when it is DRAWN from a pool or PICKED UP.
//
// Two dispatch surfaces, not one. They are kept apart because they differ in
// every way that matters:
//
//   can_spawn  A PREDICATE. Pure: (RelicSpawnContext) -> bool, no RunState, no
//              RNG. Consulted inside the pool-draw loop (return_random_relic_key /
//              return_end_random_relic_key), where a `false` makes the draw pop
//              another id -- so this surface is RNG-VISIBLE and a wrong answer
//              moves the whole relicRng sequence. Default: true.
//   on_equip   An ACTION. Mutates RunState and may CONSUME miscRng (WarPaint /
//              Whetstone each burn one randomLong for a Collections.shuffle, even
//              when fewer than two cards qualify). Runs once, at acquisition,
//              after the relic's slot has been appended. Default: nothing.
//
// Both tables are GENERATED from registry/relics.yaml `pickup:` (the
// STS_REGISTRY_RELIC_CAN_SPAWN / STS_REGISTRY_RELIC_ON_EQUIP X-macros in the
// generated relic_table.hpp, expanded by relic_pools.cpp). A row that lists a
// surface but whose handler nobody wrote is a link error, exactly as for
// `native:` combat bodies. Bringing up a tier's relics is therefore: registry
// rows, one new .cpp under src/engine/relics/, one CMakeLists line -- and NO edit
// to relic_pools.cpp or to any other tier's file. Two people filling in different
// tiers (rare/shop, boss/special) never touch the same source line, which is what
// the generated table in relic_hooks.cpp already achieved for the combat domain.
//
// (The third surface, on_obtain_card, has its macro emitted but not yet consumed
// -- run_deck.hpp still hand-rolls that switch. No signature is fixed for it here
// until that wiring lands.)

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

// The generated dispatch tables (relic_pools.cpp): the handler for `id`, or
// nullptr when the row does not list that surface. A relic whose override is
// DEFERRED maps to its explicit empty body, not to nullptr.
[[nodiscard]] RelicCanSpawnFn relic_can_spawn_fn(RelicId id) noexcept;
[[nodiscard]] RelicOnEquipFn relic_on_equip_fn(RelicId id) noexcept;

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
inline void upgrade_random_cards(RunState& rs, RngStream& misc_rng,
                                 CardType wanted) noexcept {
    uint16_t candidates[kMasterDeckCap]{};
    uint16_t count = 0;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        const CardInstance& card = rs.master_deck[i];
        const CardDef* def = card_def(static_cast<CardId>(card.card_id));
        if (def != nullptr && def->type == wanted && card.upgrade == 0) {
            candidates[count++] = i;
        }
    }

    JdkRandom jdk(random_long(misc_rng));
    jdk_shuffle(std::span<uint16_t>(candidates, count), jdk);
    const uint16_t take = count < 2 ? count : 2;
    for (uint16_t i = 0; i < take; ++i) {
        rs.master_deck[candidates[i]].upgrade = 1;
    }
}

}  // namespace sts::engine
