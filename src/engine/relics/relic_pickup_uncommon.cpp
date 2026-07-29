// UNCOMMON-tier relic pickup bodies -- the out-of-combat overrides declared by
// `pickup:` on the RelicTier.UNCOMMON rows of registry/relics.yaml. Companion to
// relic_pickup_common.cpp; see relics/relic_pickup.hpp for the two surfaces and
// the generated dispatch.
//
// This file also holds the tier's DEFERRED onEquip bodies. Under the generated
// dispatch a listed surface with no definition is a link error, so "registered
// but not implemented yet" cannot be expressed by omission: it is an explicit
// empty function carrying the deferral reason and the Java citation, right where
// the implementation will go. That is a decision on the record instead of a hole.

#include "relic_pickup.hpp"

namespace sts::engine {

// --- canSpawn: the "Settings.isEndless || floorNum <= 48" family --------------
//
// DarkstonePeriapt.canSpawn (DarkstonePeriapt.java:44-46), FrozenEgg2.canSpawn
// (FrozenEgg2.java:53-55), MeatOnTheBone.canSpawn (MeatOnTheBone.java:53-55),
// MoltenEgg2.canSpawn (MoltenEgg2.java:53-55), QuestionCard.canSpawn
// (QuestionCard.java:29-31), SingingBowl.canSpawn (SingingBowl.java:41-43),
// ToxicEgg2.canSpawn (ToxicEgg2.java:53-55).

bool relic_can_spawn_darkstone_periapt(const RelicSpawnContext& ctx) noexcept {
    return ctx.endless || ctx.floor <= 48;  // DarkstonePeriapt.java:44-46
}

bool relic_can_spawn_frozen_egg(const RelicSpawnContext& ctx) noexcept {
    return ctx.endless || ctx.floor <= 48;  // FrozenEgg2.java:53-55
}

bool relic_can_spawn_meat_on_the_bone(const RelicSpawnContext& ctx) noexcept {
    return ctx.endless || ctx.floor <= 48;  // MeatOnTheBone.java:53-55
}

bool relic_can_spawn_molten_egg(const RelicSpawnContext& ctx) noexcept {
    return ctx.endless || ctx.floor <= 48;  // MoltenEgg2.java:53-55
}

bool relic_can_spawn_question_card(const RelicSpawnContext& ctx) noexcept {
    return ctx.endless || ctx.floor <= 48;  // QuestionCard.java:29-31
}

bool relic_can_spawn_singing_bowl(const RelicSpawnContext& ctx) noexcept {
    return ctx.endless || ctx.floor <= 48;  // SingingBowl.java:41-43
}

bool relic_can_spawn_toxic_egg(const RelicSpawnContext& ctx) noexcept {
    return ctx.endless || ctx.floor <= 48;  // ToxicEgg2.java:53-55
}

// --- canSpawn: floor gate AND "not in a shop" --------------------------------

bool relic_can_spawn_the_courier(const RelicSpawnContext& ctx) noexcept {
    // Courier.canSpawn (Courier.java:41-43):
    //   (Settings.isEndless || floorNum <= 48) && !(getCurrRoom() instanceof ShopRoom)
    // Endless bypasses the floor clause only, not the shop clause -- see
    // relic_can_spawn_maw_bank in relic_pickup_common.cpp.
    return (ctx.endless || ctx.floor <= 48) && !ctx.in_shop;
}

// --- canSpawn: other floor constants -----------------------------------------

bool relic_can_spawn_matryoshka(const RelicSpawnContext& ctx) noexcept {
    // Matryoshka.canSpawn (Matryoshka.java:59-61): floorNum <= 40.
    return ctx.endless || ctx.floor <= 40;
}

// --- canSpawn: the Bottled trio's deck-content gates -------------------------
//
// These three have NO Settings.isEndless clause at all (BottledFlame.java:93-99,
// BottledLightning.java:93-99, BottledTornado.java:93-95), so the deck gate
// applies even in Endless. Under the old shared `if (endless) return true;`
// framework step they had to be special-cased BEFORE it; now each body simply
// says what its Java says and the ordering question disappears.
//
// The master-deck facts themselves are precomputed into the context by
// fill_deck_spawn_gates (relic_pools.cpp), which is where the BASIC-rarity vs.
// CardHelper.hasCardType asymmetry between Flame/Lightning and Tornado lives.

bool relic_can_spawn_bottled_flame(const RelicSpawnContext& ctx) noexcept {
    // BottledFlame.canSpawn (BottledFlame.java:93-99): any masterDeck card with
    // type == ATTACK && rarity != BASIC.
    return ctx.deck_has_nonbasic_attack;
}

bool relic_can_spawn_bottled_lightning(const RelicSpawnContext& ctx) noexcept {
    // BottledLightning.canSpawn (BottledLightning.java:93-99): same, SKILL.
    return ctx.deck_has_nonbasic_skill;
}

bool relic_can_spawn_bottled_tornado(const RelicSpawnContext& ctx) noexcept {
    // BottledTornado.canSpawn (BottledTornado.java:93-95) delegates to
    // CardHelper.hasCardType(POWER) (CardHelper.java:80-86) -- a plain type scan
    // with NO rarity clause, unlike its two siblings.
    return ctx.deck_has_power;
}

// --- onEquip -----------------------------------------------------------------

void relic_on_equip_pear(RunState& rs, RngStream& /*misc_rng*/,
                         RelicSlot& /*slot*/) noexcept {
    // Pear.onEquip (Pear.java:29-31): increaseMaxHp(10, true) -- +10 max AND +10
    // current (increaseMaxHp heals the gained amount).
    rs.max_hp = static_cast<int16_t>(rs.max_hp + 10);
    rs.hp = static_cast<int16_t>(rs.hp + 10);
}

// --- onEquip: the Bottled trio (`pickup: on_equip_screen`) --------------------
//
// BottledFlame.onEquip (BottledFlame.java:41-53), read in full; Lightning and
// Tornado are textually identical except for the type filter and the flag.
// When masterDeck.getPurgeableCards().getX() is non-empty the game opens a
// MANDATORY 1-pick grid over it (`gridSelectScreen.open(group, 1, msg, false,
// false, false, false)` -- no confirm, no cancel) and parks the room at
// RoomPhase.INCOMPLETE (:49-51); the async update (:63-77) then sets the
// chosen instance's inBottle* flag. When the filtered list is EMPTY, no
// screen opens and `cardSelected` stays true: the relic is kept, permanently
// unbottled (:41 guard).
//
// The bodies therefore only ask the call site for the screen: the run layer's
// pending-bottle overlay (run_advance.cpp) is the modal grid, and the pick
// lands as the master-deck instance's bottle bit (run_deck.hpp -- the
// encoding decision is documented there). Zero RNG on every path.

namespace {
void bottle_on_equip(const RunState& rs, MasterBottleKind kind,
                     RelicEquipContext& ctx) noexcept {
    if (!bottle_has_eligible_card(rs, kind)) {
        return;  // no screen; the relic stays, permanently unbottled (:41)
    }
    ctx.screen = RelicEquipScreen::GRID_BOTTLE;
    ctx.grid_picks = 1;
    ctx.bottle = kind;
}
}  // namespace

void relic_on_equip_screen_bottled_flame(RunState& rs, RngStream& /*misc_rng*/,
                                         RelicSlot& /*slot*/,
                                         RelicEquipContext& ctx) noexcept {
    // BottledFlame.java:41-53 -- purgeable ATTACKs (any rarity; a Strike is
    // offerable -- only canSpawn reads rarity).
    bottle_on_equip(rs, MasterBottleKind::FLAME, ctx);
}

void relic_on_equip_screen_bottled_lightning(
    RunState& rs, RngStream& /*misc_rng*/, RelicSlot& /*slot*/,
    RelicEquipContext& ctx) noexcept {
    // BottledLightning.java:41-53 -- purgeable SKILLs.
    bottle_on_equip(rs, MasterBottleKind::LIGHTNING, ctx);
}

void relic_on_equip_screen_bottled_tornado(
    RunState& rs, RngStream& /*misc_rng*/, RelicSlot& /*slot*/,
    RelicEquipContext& ctx) noexcept {
    // BottledTornado.java:41-53 -- purgeable POWERs.
    bottle_on_equip(rs, MasterBottleKind::TORNADO, ctx);
}

// FrozenEgg2.onEquip (FrozenEgg2.java:31-38) walks the cards ALREADY OFFERED on
// the combat-reward screen and re-runs onPreviewObtainCard over them, so a card
// on the current reward screen shows its upgraded form the moment the egg is
// picked up.
//
// STILL A DOCUMENTED NO-OP, but the reason CHANGED (wave2-engine stage 3d,
// 2026-07-28). The old justification -- "neither the screen nor its RewardItem
// list exists yet" -- EXPIRED: RewardScreen and RunRewardItem.card_upgrades
// exist (combat_rewards.hpp) and an elite reward screen can hold a RELIC and a
// CARDS item at once, so claiming an egg while an offer is still open is the
// game's preview case. What is missing is PLUMBING, not the screen: this plain
// on_equip surface receives (RunState, miscRng, slot) only -- the reward
// screen travels in RelicEquipContext, which only on_equip_screen bodies get,
// and the eggs cannot move to that surface because the PLAIN acquire_relic
// door (event grants, exordium_events_*; the ctx-less shop fallback) refuses
// on_equip_screen ids by design. The divergence this leaves is NARROW: the
// still-open OFFER's upgrade bits (a reward-screen observation the run differ
// compares); the MASTER DECK converges regardless, because the pick lands
// through onObtainCard below with the egg then owned. Carried as its own
// Deferred-obligations row ("Egg trio onEquip reward-screen preview pass");
// do not fix it here without deciding how every equip site sees the screen.
void relic_on_equip_frozen_egg(RunState& /*rs*/, RngStream& /*misc_rng*/,
                               RelicSlot& /*slot*/) noexcept {}

// MoltenEgg2.onEquip (MoltenEgg2.java:31-38) -- identical preview pass, ATTACKs;
// same expired premise, same open row as Frozen Egg above.
void relic_on_equip_molten_egg(RunState& /*rs*/, RngStream& /*misc_rng*/,
                               RelicSlot& /*slot*/) noexcept {}

// ToxicEgg2.onEquip (ToxicEgg2.java:31-38) -- identical preview pass, SKILLs;
// same expired premise, same open row as Frozen Egg above.
void relic_on_equip_toxic_egg(RunState& /*rs*/, RngStream& /*misc_rng*/,
                              RelicSlot& /*slot*/) noexcept {}

// --- onObtainCard ------------------------------------------------------------
//
// The three eggs share one body but for the CardType. `canUpgrade() && !upgraded`
// is modelled as `card.upgrade == 0`: the registry's cards are single-upgrade, so
// "not yet upgraded" and "can still be upgraded" are the same test. A card handed
// in already upgraded is left alone, which is what the Java's !upgraded gives.

void relic_on_obtain_card_molten_egg(RunState& /*rs*/, CardInstance& card,
                                     const CardDef& def) noexcept {
    // MoltenEgg2.onObtainCard (MoltenEgg2.java:46-50): ATTACK -> upgrade().
    if (def.type == CardType::ATTACK && card.upgrade == 0) {
        card.upgrade = 1;
    }
}

void relic_on_obtain_card_toxic_egg(RunState& /*rs*/, CardInstance& card,
                                    const CardDef& def) noexcept {
    // ToxicEgg2.onObtainCard (ToxicEgg2.java:46-50): SKILL -> upgrade().
    if (def.type == CardType::SKILL && card.upgrade == 0) {
        card.upgrade = 1;
    }
}

void relic_on_obtain_card_frozen_egg(RunState& /*rs*/, CardInstance& card,
                                     const CardDef& def) noexcept {
    // FrozenEgg2.onObtainCard (FrozenEgg2.java:46-50): POWER -> upgrade().
    if (def.type == CardType::POWER && card.upgrade == 0) {
        card.upgrade = 1;
    }
}

void relic_on_obtain_card_darkstone_periapt(RunState& rs, CardInstance& /*card*/,
                                            const CardDef& def) noexcept {
    // DarkstonePeriapt.onObtainCard (DarkstonePeriapt.java:28-36): a CURSE grants
    // increaseMaxHp(6, true) -- +6 max AND +6 current (heals the gained amount,
    // DarkstonePeriapt.java:34). The ModHelper "Hoarder" daily-mod branch
    // (:30-33) triples the gain; daily mods are out of scope, and ModHelper state
    // does not exist, so only the unmodded :34 call is modelled.
    //
    // FAITHFULNESS NOTE: the Java keys on `card.color == CardColor.CURSE`, not on
    // the card TYPE. This tests the type instead, because CardDef has no color
    // column -- `color` exists in registry/cards.yaml but is consumed at
    // generation time (card-pool membership) and never emitted into the struct.
    // The two are not merely equal in our data: every curse in the game is
    // constructed with `CardType.CURSE, CardColor.CURSE` together in one super()
    // call (Regret.java:29, AscendersBane.java:24, Parasite.java:25 -- all eleven
    // curse rows), so no card can satisfy one and not the other. Keying on color
    // literally would mean adding a color column to CardDef and regenerating the
    // whole card table for zero behavioural difference, so the type test stays
    // and this note records why.
    if (def.type == CardType::CURSE) {
        rs.max_hp = static_cast<int16_t>(rs.max_hp + 6);
        rs.hp = static_cast<int16_t>(rs.hp + 6);
    }
}

}  // namespace sts::engine
