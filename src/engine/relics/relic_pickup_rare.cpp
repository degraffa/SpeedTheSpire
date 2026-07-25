// RARE-tier relic pickup bodies -- the out-of-combat overrides declared by
// `pickup:` on the RelicTier.RARE rows of registry/relics.yaml. Companion to
// relic_pickup_common.cpp / relic_pickup_uncommon.cpp; see relics/relic_pickup.hpp
// for the three surfaces and the generated dispatch.

#include "relic_pickup.hpp"

namespace sts::engine {

// --- canSpawn: the plain "Settings.isEndless || floorNum <= N" family ---------

bool relic_can_spawn_prayer_wheel(const RelicSpawnContext& ctx) noexcept {
    return ctx.endless || ctx.floor <= 48;  // PrayerWheel.java:986-989
}

bool relic_can_spawn_wing_boots(const RelicSpawnContext& ctx) noexcept {
    return ctx.endless || ctx.floor <= 40;  // WingBoots.java:1383-1386
}

// --- canSpawn: floor gate AND "not in a shop" --------------------------------

bool relic_can_spawn_old_coin(const RelicSpawnContext& ctx) noexcept {
    // OldCoin.canSpawn (OldCoin.java:818-821):
    //   (Settings.isEndless || floorNum <= 48) && !(getCurrRoom() instanceof ShopRoom)
    // Endless bypasses the floor clause only, never the shop clause -- the same
    // shape as MawBank / SmilingMask / Courier.
    return (ctx.endless || ctx.floor <= 48) && !ctx.in_shop;
}

// --- canSpawn: the campfire trio ---------------------------------------------
//
// Girya.canSpawn (Girya.java:538-549), PeacePipe.canSpawn (PeacePipe.java:
// 862-873) and Shovel.canSpawn (Shovel.java:1024-1035) have byte-identical
// bodies:
//
//     if (floorNum >= 48 && !Settings.isEndless) return false;
//     count owned PeacePipe|Shovel|Girya;  return count < 2;
//
// Note the shape: the floor clause is a `>= 48` EARLY RETURN, not the `<= 48`
// the rest of the tree uses, so floor 48 itself is excluded here and included
// there. The owned-count is precomputed into the context by
// fill_campfire_relic_count (relic_pools.cpp), because canSpawn is a pure
// predicate over the context and must not re-scan RunState.

bool relic_can_spawn_girya(const RelicSpawnContext& ctx) noexcept {
    if (ctx.floor >= 48 && !ctx.endless) {
        return false;
    }
    return ctx.campfire_relic_count < 2;
}

bool relic_can_spawn_peace_pipe(const RelicSpawnContext& ctx) noexcept {
    if (ctx.floor >= 48 && !ctx.endless) {
        return false;
    }
    return ctx.campfire_relic_count < 2;
}

bool relic_can_spawn_shovel(const RelicSpawnContext& ctx) noexcept {
    if (ctx.floor >= 48 && !ctx.endless) {
        return false;
    }
    return ctx.campfire_relic_count < 2;
}

// --- onEquip -----------------------------------------------------------------

void relic_on_equip_du_vu_doll(RunState& rs, RngStream& /*misc_rng*/,
                               RelicSlot& slot) noexcept {
    // DuVuDoll.onEquip (DuVuDoll.java:334-345): counter = the number of
    // CURSE-type cards in the master deck. This is a real RunState-READING
    // override, not the `this.counter = 0` shape initial_counter already models,
    // which is why the row carries the pickup key. The identical recount on every
    // later deck change (onMasterDeckChange, :321-332) lives in run_deck.hpp.
    int16_t curses = 0;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        const CardDef* def =
            card_def(static_cast<CardId>(rs.master_deck[i].card_id));
        if (def != nullptr && def->type == CardType::CURSE) {
            ++curses;
        }
    }
    slot.counter = curses;
}

void relic_on_equip_mango(RunState& rs, RngStream& /*misc_rng*/,
                          RelicSlot& /*slot*/) noexcept {
    // Mango.onEquip (Mango.java:771-774): increaseMaxHp(14, true) -- +14 max AND
    // +14 current (increaseMaxHp heals the gained amount when the flag is set).
    rs.max_hp = static_cast<int16_t>(rs.max_hp + 14);
    rs.hp = static_cast<int16_t>(rs.hp + 14);
}

void relic_on_equip_old_coin(RunState& rs, RngStream& /*misc_rng*/,
                             RelicSlot& /*slot*/) noexcept {
    // OldCoin.onEquip (OldCoin.java:812-816): gainGold(300). Routed through the
    // shared gain_gold door (relics/relic_pickup.hpp) rather than a direct
    // `rs.gold +=`, which is what this comment asked for "when the boss tier
    // lands": Ectoplasm (registry id ECTOPLASM) makes gainGold return without
    // adding anything (AbstractPlayer.java:719-724), so a direct write would
    // silently ignore it.
    gain_gold(rs, 300);
}

}  // namespace sts::engine
