// COMMON-tier relic pickup bodies -- the out-of-combat overrides declared by
// `pickup:` on the RelicTier.COMMON rows of registry/relics.yaml. See
// relics/relic_pickup.hpp for the two surfaces and why they are separate tables;
// the dispatch tables themselves are generated from the registry and expanded in
// relic_pools.cpp, so this file is reached only through them, and bringing up a
// new tier never edits a shared switch.
//
// Provenance: every body below cites the Java method it mirrors, read in full.
// The canSpawn bodies spell their `Settings.isEndless ||` disjunct EXPLICITLY
// rather than relying on a framework-level endless bypass -- see the note on
// relic_can_spawn_maw_bank for why that distinction is load-bearing.

#include <algorithm>

#include "relic_pickup.hpp"

namespace sts::engine {

// --- canSpawn: the plain "Settings.isEndless || floorNum <= 48" family --------
//
// AncientTeaSet.canSpawn (AncientTeaSet.java:84-86), CeramicFish.canSpawn
// (CeramicFish.java:45-47), DreamCatcher.canSpawn (DreamCatcher.java:33-35),
// JuzuBracelet.canSpawn (JuzuBracelet.java:28-30), MealTicket.canSpawn
// (MealTicket.java:41-43), Omamori.canSpawn (Omamori.java:49-51),
// PotionBelt.canSpawn (PotionBelt.java:36-38), RegalPillow.canSpawn
// (RegalPillow.java:34-36) -- byte-identical bodies, one function each because
// the dispatch table is per-relic and a future rebalance can diverge them.

bool relic_can_spawn_ancient_tea_set(const RelicSpawnContext& ctx) noexcept {
    return ctx.endless || ctx.floor <= 48;  // AncientTeaSet.java:84-86
}

bool relic_can_spawn_ceramic_fish(const RelicSpawnContext& ctx) noexcept {
    return ctx.endless || ctx.floor <= 48;  // CeramicFish.java:45-47
}

bool relic_can_spawn_dream_catcher(const RelicSpawnContext& ctx) noexcept {
    return ctx.endless || ctx.floor <= 48;  // DreamCatcher.java:33-35
}

bool relic_can_spawn_juzu_bracelet(const RelicSpawnContext& ctx) noexcept {
    return ctx.endless || ctx.floor <= 48;  // JuzuBracelet.java:28-30
}

bool relic_can_spawn_meal_ticket(const RelicSpawnContext& ctx) noexcept {
    return ctx.endless || ctx.floor <= 48;  // MealTicket.java:41-43
}

bool relic_can_spawn_omamori(const RelicSpawnContext& ctx) noexcept {
    return ctx.endless || ctx.floor <= 48;  // Omamori.java:49-51
}

bool relic_can_spawn_potion_belt(const RelicSpawnContext& ctx) noexcept {
    return ctx.endless || ctx.floor <= 48;  // PotionBelt.java:36-38
}

bool relic_can_spawn_regal_pillow(const RelicSpawnContext& ctx) noexcept {
    return ctx.endless || ctx.floor <= 48;  // RegalPillow.java:34-36
}

// --- canSpawn: floor gate AND "not in a shop" --------------------------------

bool relic_can_spawn_maw_bank(const RelicSpawnContext& ctx) noexcept {
    // MawBank.canSpawn (MawBank.java:56-58):
    //   (Settings.isEndless || floorNum <= 48) && !(getCurrRoom() instanceof ShopRoom)
    // NOTE the bracketing: endless bypasses the FLOOR clause only, never the shop
    // clause. The hand-written switch this replaced hoisted `if (endless) return
    // true;` to the top as a shared framework step, which answered true for this
    // relic in a shop during Endless -- see the divergence note in the change
    // that generated this dispatch. Spelling each Java body in full removes the
    // whole class of bug.
    return (ctx.endless || ctx.floor <= 48) && !ctx.in_shop;
}

bool relic_can_spawn_smiling_mask(const RelicSpawnContext& ctx) noexcept {
    // SmilingMask.canSpawn (SmilingMask.java:41-43) -- same shape as MawBank.
    return (ctx.endless || ctx.floor <= 48) && !ctx.in_shop;
}

// --- canSpawn: the other floor constants -------------------------------------

bool relic_can_spawn_preserved_insect(const RelicSpawnContext& ctx) noexcept {
    // PreservedInsect.canSpawn (PreservedInsect.java:44-46): floorNum <= 52.
    return ctx.endless || ctx.floor <= 52;
}

bool relic_can_spawn_tiny_chest(const RelicSpawnContext& ctx) noexcept {
    // TinyChest.canSpawn (TinyChest.java:35-37): floorNum <= 35.
    return ctx.endless || ctx.floor <= 35;
}

// --- onEquip -----------------------------------------------------------------

void relic_on_equip_potion_belt(RunState& rs, RngStream& /*misc_rng*/,
                                RelicSlot& /*slot*/) noexcept {
    // PotionBelt.onEquip (PotionBelt.java:29-33): potionSlots += 2, then two
    // empty PotionSlot objects are appended so the belt's new slots exist. The
    // engine models slots as a count, so the two `potions.add(new PotionSlot(..))`
    // calls are the same fact as the += 2 and need no separate representation.
    // Clamped to kPotionCap, which the Java does not need (its list grows).
    rs.potion_slots = static_cast<uint8_t>(
        std::min<int>(kPotionCap, static_cast<int>(rs.potion_slots) + 2));
}

void relic_on_equip_strawberry(RunState& rs, RngStream& /*misc_rng*/,
                               RelicSlot& /*slot*/) noexcept {
    // Strawberry.onEquip (Strawberry.java:29-31): increaseMaxHp(7, true) -- +7
    // max AND +7 current (increaseMaxHp heals the gained amount when true).
    rs.max_hp = static_cast<int16_t>(rs.max_hp + 7);
    rs.hp = static_cast<int16_t>(rs.hp + 7);
}

void relic_on_equip_war_paint(RunState& rs, RngStream& misc_rng,
                              RelicSlot& /*slot*/) noexcept {
    // WarPaint.onEquip (WarPaint.java:36-59): collect upgradable SKILLs, shuffle
    // with `new Random(miscRng.randomLong())`, upgrade the first one or two. The
    // randomLong is drawn even when the list is empty (it is the ctor argument).
    upgrade_random_cards(rs, misc_rng, CardType::SKILL);
}

void relic_on_equip_whetstone(RunState& rs, RngStream& misc_rng,
                              RelicSlot& /*slot*/) noexcept {
    // Whetstone.onEquip (Whetstone.java:36-59): the WarPaint body with
    // CardType.ATTACK. Same unconditional miscRng.randomLong().
    upgrade_random_cards(rs, misc_rng, CardType::ATTACK);
}

}  // namespace sts::engine
