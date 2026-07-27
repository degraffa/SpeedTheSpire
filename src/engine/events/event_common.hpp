#pragma once

// event_common.hpp -- helpers shared by every event-body translation unit.
//
// INTERNAL header (conventions.md "Where a new header goes"): its only
// consumers are the `src/engine/events/*.cpp` bodies, so it lives beside them
// rather than in the published `include/sts/engine/` surface. It exists because
// the same AbstractCreature / AbstractDungeon shims kept being copied into
// every new event translation unit; conventions.md §7 makes the recurrence the
// point at which the footgun is removed rather than documented again.
//
// Provenance (each read in full from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * AbstractCreature.heal              AbstractCreature.java:386-421
//   * AbstractCreature.increaseMaxHp     AbstractCreature.java:199-209
//     (the `showEffect` flag is display-only: the body ALWAYS heals `amount`)
//   * AbstractCreature.decreaseMaxHealth AbstractCreature.java:211-223
//   * AbstractPlayer.loseGold            AbstractPlayer.java:697-712
//   * AbstractDungeon.returnRandomScreenlessRelic
//                                        AbstractDungeon.java:681-688
//   * MathUtils.ceil                     libGDX MathUtils (BIG_ENOUGH_INT form)

#include <algorithm>
#include <cstdint>

#include "sts/engine/event_framework.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_state.hpp"

namespace sts::engine::events {

// AbstractCreature.heal: clamped at maxHealth, and a dying creature is skipped.
// Magic Flower's onPlayerHeal is COMBAT-phase gated (MagicFlower.java:31-38),
// so no relic modifies an event-room heal in S1.
inline void heal(RunState& rs, int amount) noexcept {
    if (amount <= 0 || rs.hp <= 0) {
        return;
    }
    rs.hp = static_cast<int16_t>(std::min(static_cast<int>(rs.max_hp),
                                          static_cast<int>(rs.hp) + amount));
}

// increaseMaxHp(amount, showEffect) raises maxHealth and then calls
// heal(amount, true) UNCONDITIONALLY -- `showEffect` only picks the VFX. A
// caller passing `false` (Bonfire's rare payout) still gains the HP.
inline void increase_max_hp(RunState& rs, int amount) noexcept {
    if (amount <= 0) {
        return;
    }
    rs.max_hp = static_cast<int16_t>(static_cast<int>(rs.max_hp) + amount);
    heal(rs, amount);
}

inline void decrease_max_hp(RunState& rs, int amount) noexcept {
    if (amount <= 0) {
        return;
    }
    rs.max_hp =
        static_cast<int16_t>(std::max(1, static_cast<int>(rs.max_hp) - amount));
    if (rs.hp > rs.max_hp) {
        rs.hp = rs.max_hp;
    }
}

// AbstractPlayer.loseGold is expressed by sts::engine::lose_gold
// (src/engine/relics/relic_pickup.hpp), the run layer's single lose-gold door
// with the named onSpendGold/onLoseGold fan-outs. A byte-equivalent duplicate
// briefly lived here (two concurrent branches each added one and the union was
// ambiguous at the one_time_specials call sites); it was removed at
// integration in favor of the door.

// libGDX MathUtils.ceil(float) = BIG_ENOUGH_INT - (int)(BIG_ENOUGH_FLOOR - v),
// with BIG_ENOUGH_INT = 16384 and BIG_ENOUGH_FLOOR = 16384.0. Reproduced in the
// same float discipline as interp.hpp's mathutils_round rather than via
// std::ceil, because the two disagree on values that are not exactly
// representable and the game's form is the spec.
[[nodiscard]] inline constexpr int mathutils_ceil(float value) noexcept {
    constexpr int kBigEnoughInt = 16384;
    constexpr float kBigEnoughFloor = 16384.0f;
    return kBigEnoughInt - static_cast<int>(kBigEnoughFloor - value);
}

[[nodiscard]] inline RelicSpawnContext event_relic_context(
    const RunState& rs) noexcept {
    RelicSpawnContext ctx{};
    ctx.floor = rs.floor;
    fill_deck_spawn_gates(rs, ctx);
    fill_campfire_relic_count(rs, ctx);
    fill_boss_spawn_gates(rs, ctx);
    return ctx;
}

// returnRandomScreenlessRelic(returnRandomRelicTier()): one relicRng tier roll,
// then pool pops that are RE-ROLLED (never put back) while the key is one of
// the four relics with no screenless presentation.
[[nodiscard]] inline RelicId draw_event_relic(RunState& rs,
                                              bool screenless) noexcept {
    const RelicTier tier = return_random_relic_tier(rs);
    const RelicSpawnContext ctx = event_relic_context(rs);
    RelicId id = return_random_relic_key(rs, tier, ctx);
    while (screenless &&
           (id == RelicId::BOTTLED_FLAME || id == RelicId::BOTTLED_LIGHTNING ||
            id == RelicId::BOTTLED_TORNADO || id == RelicId::WHETSTONE)) {
        id = return_random_relic_key(rs, tier, ctx);
    }
    return id;
}

inline void acquire_event_relic(RunController& rc, bool screenless) noexcept {
    const RelicId id = draw_event_relic(rc.run, screenless);
    (void)acquire_relic(rc.run, rc.combat.misc_rng, id);
}

// The single-Proceed page every completed dialog ends on.
inline void one_proceed_menu(EventDialogMenu& out) noexcept {
    out = EventDialogMenu{};
    out.count = 1;
    out.enabled[0] = true;
}

[[nodiscard]] inline bool grid_has_card(const RunState& rs,
                                        EventGridKind kind) noexcept {
    EventDialogState probe{};
    open_event_grid(probe, kind);
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        if (event_grid_card_legal(rs, probe, i)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool has_purgeable_card(const RunState& rs) noexcept {
    return grid_has_card(rs, EventGridKind::PURGE);
}

[[nodiscard]] inline bool has_upgradable_card(const RunState& rs) noexcept {
    return grid_has_card(rs, EventGridKind::UPGRADE);
}

}  // namespace sts::engine::events
