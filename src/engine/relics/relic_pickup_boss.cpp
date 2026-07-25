// BOSS-tier relic pickup bodies -- the out-of-combat overrides declared by
// `pickup:` on the RelicTier.BOSS rows of registry/relics.yaml. See
// relics/relic_pickup.hpp for the three surfaces and the generated dispatch.
//
// Exactly ONE boss relic overrides canSpawn in a way S1 can answer: Ectoplasm's
// act gate. Black Blood's gate reads the owned relic list. Every other boss file
// takes AbstractRelic's `return true` default -- recorded here rather than left
// to be inferred from an absence, because a spurious gate would change the
// relicRng draw order.

#include "relic_pickup.hpp"

namespace sts::engine {

// --- canSpawn ----------------------------------------------------------------

bool relic_can_spawn_ectoplasm(const RelicSpawnContext& ctx) noexcept {
    // Ectoplasm.canSpawn (Ectoplasm.java:50-53): `return AbstractDungeon.actNum
    // <= 1;`. The ONLY act-gated canSpawn in the S1 relic set, and the only
    // consumer of RelicSpawnContext::act. There is no Settings.isEndless disjunct
    // in this body, so endless does NOT bypass it.
    return ctx.act <= 1;
}

bool relic_can_spawn_black_blood(const RelicSpawnContext& ctx) noexcept {
    // BlackBlood.canSpawn (BlackBlood.java:33-36): `return
    // AbstractDungeon.player.hasRelic("Burning Blood");` -- the Ironclad's
    // starter. No floor clause, no endless clause. Filled by
    // fill_boss_spawn_gates (relic_pools.cpp); the context default is `true`,
    // which is the fresh-Ironclad answer (Ironclad.getStartingRelics,
    // Ironclad.java:113-115).
    return ctx.has_burning_blood;
}

// --- onEquip: the DEFERRED run-layer bodies ----------------------------------
//
// Four boss relics override onEquip with a run-layer flow that does not exist
// yet. Under the generated dispatch a listed `pickup:` surface with no definition
// is a LINK ERROR, so "registered but not implemented" cannot be expressed by
// omission: each is an explicit empty body carrying the reason and the citation,
// right where the implementation goes.
//
// All four are also RNG-relevant in the same direction: each consumes relicRng,
// miscRng or neither in a way that a PARTIAL implementation would get wrong, so
// they are deferred WHOLE rather than half-done.

void relic_on_equip_pandoras_box(RunState& /*rs*/, RngStream& /*misc_rng*/,
                                 RelicSlot& /*slot*/) noexcept {
    // PandorasBox.onEquip (PandorasBox.java:47-70): remove every card tagged
    // STARTER_STRIKE / STARTER_DEFEND from the master deck, then create that many
    // AbstractDungeon.returnTrulyRandomCard() replacements.
    //
    // DEFERRED: returnTrulyRandomCard draws over the WHOLE red pool (commons,
    // uncommons AND rares) -- the same generated all-colour combat pool Dead
    // Branch waits on (relics_rare.cpp) -- and the registry carries no
    // CardTags.STARTER_* column, so even the REMOVAL half cannot be written
    // faithfully today (the starting deck's Strikes and Defends are the tagged
    // ones; a card named Strike obtained later is not).
}

void relic_on_equip_tiny_house(RunState& /*rs*/, RngStream& /*misc_rng*/,
                               RelicSlot& /*slot*/) noexcept {
    // TinyHouse.onEquip (TinyHouse.java:24-60): shuffle the upgradable master-deck
    // cards with `new Random(miscRng.randomLong())` and upgrade the first;
    // increaseMaxHp(5, true); addGoldToRewards(50);
    // addPotionToRewards(PotionHelper.getRandomPotion(miscRng)); open the
    // combat-reward screen.
    //
    // DEFERRED WHOLE, not in part. The body consumes miscRng TWICE -- the shuffle
    // seed and the potion identity roll, itself a rejection-sampling loop
    // (potions.cpp, trap 14) -- and both draws sit inside a reward-screen flow
    // that does not exist yet. Taking only the upgrade and the +5 max HP
    // would leave miscRng short by two draws and desync every later miscRng
    // consumer, which is strictly worse than an inert relic.
}

void relic_on_equip_astrolabe(RunState& /*rs*/, RngStream& /*misc_rng*/,
                              RelicSlot& /*slot*/) noexcept {
    // Astrolabe.onEquip (Astrolabe.java:29-51) -> giveCards (:57-71): open a
    // 3-card grid select over the purgeable master deck, then for each pick
    // removeCard + AbstractDungeon.transformCard(card, true, miscRng).
    //
    // DEFERRED: needs the run-layer grid-select screen AND transformCard, which is
    // miscRng-consuming, so a partial would desync the stream. When the deck fits
    // in three cards the Java skips the screen and transforms them all (:38-40) --
    // that branch is part of the same deferral.
}

void relic_on_equip_empty_cage(RunState& /*rs*/, RngStream& /*misc_rng*/,
                               RelicSlot& /*slot*/) noexcept {
    // EmptyCage.onEquip (EmptyCage.java:28-52) -> deleteCards (:62-75): open a
    // 2-card grid select over the purgeable master deck and remove both.
    //
    // DEFERRED: needs the run-layer grid-select screen. Unlike the other three
    // this body consumes NO RNG at all, so it is the cheapest of the four to
    // un-defer -- it is a `remove_master_deck_card` per pick (run_deck.hpp) once a
    // choice surface exists.
}

void relic_on_equip_calling_bell(RunState& /*rs*/, RngStream& /*misc_rng*/,
                                 RelicSlot& /*slot*/) noexcept {
    // CallingBell.onEquip (CallingBell.java:27-36) + update (:38-54): add a Curse
    // of the Bell to the master deck, then offer one COMMON + one UNCOMMON + one
    // RARE relic through returnRandomScreenlessRelic.
    //
    // DEFERRED: the three pool draws are relicRng-consuming and land on a
    // combat-reward screen that does not exist yet, and Curse of the Bell has
    // no registry/cards.yaml row. return_random_relic_key (relic_pools.cpp) is
    // already the right callee for the three draws; only the screen is missing.
}

}  // namespace sts::engine
