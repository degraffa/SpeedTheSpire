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

#include <cassert>

#include "sts/engine/card_pools.hpp"      // transform_card (the ONE authority)
#include "sts/engine/combat_rewards.hpp"  // RewardScreen assembly doors
#include "sts/engine/potions.hpp"         // get_random_potion (flat draw)
#include "sts/engine/rest_sites.hpp"      // rest_card_upgradeable / _purgeable
#include "sts/engine/run_deck.hpp"        // master-deck add/remove doors

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

// --- onEquip: the five screen-opening bodies (Wave-C track 2) ----------------
//
// All five are `pickup: on_equip_screen` rows: their bodies need pieces the
// plain RelicOnEquipSig cannot carry (cardRandomRng, a RewardScreen, a grid
// request) and receive them through RelicEquipContext -- the design is
// documented at that struct (include/sts/engine/relic_pools.hpp). In S1 the
// ONLY producer is Neow's boss swap (the Act-1 boss chest is S2 content;
// Scrap Ooze rolls COMMON/UNCOMMON/RARE; shops sell no BOSS tier), so the one
// live call site is neow.cpp's spawn_relic_and_obtain, which translates the
// ctx screen request onto NeowState's GRID / ITEM_REWARD sub-screens.
//
// TIMING NOTE, recorded rather than "fixed": at Neow the Java onEquip is
// ASYNCHRONOUS -- spawnRelicAndObtain's obtain() does NOT call onEquip
// (AbstractRelic.java:277-292); it fires from AbstractRelic.update (:339-348)
// when the relic's fly-in animation lands, several frames later. Nothing else
// in the boss-swap payout runs in between (BOSS_RELIC is category 3's only
// member and carries no drawback, NeowReward.java:122-125), so the sim's
// inline dispatch is observably identical HERE -- but a future producer that
// does work after spawnRelicAndObtain in the same frame would see a different
// interleaving than an inline dispatch. Do not generalize this call site
// without re-deriving that ordering.

namespace {

// PandorasBox's removal predicate. The Java condition is a per-INSTANCE tag
// check -- hasTag(STARTER_STRIKE) || hasTag(STARTER_DEFEND) -- but both tags
// are set in the card CONSTRUCTORS (Strike_Red.java:36, Defend_Red.java:30),
// so EVERY instance of those two ids carries its tag: obtained later, upgraded
// (tags survive upgrade), bottled, anything. No other RED card sets either
// tag, which makes id-level membership Java-exact for the Ironclad. (The
// registry carries no STARTER_* column; adding one would duplicate this
// two-id fact per the dossier's spelling (a).)
[[nodiscard]] bool pandoras_starter_tagged(CardId id) noexcept {
    return id == CardId::STRIKE || id == CardId::DEFEND;
}

// AbstractCreature.increaseMaxHp(amount, true) (AbstractCreature.java:199-209):
// maxHealth += amount AND heal(amount). The player-heal extra body is the
// onNotBloodied relic fan-out, which no S1 relic reaches out of combat (Red
// Skull acts on a combat power), so the run-layer effect is the clamp alone --
// the same reading neow.cpp's file-local increase_max_hp documents.
void increase_max_hp_with_heal(RunState& rs, int amount) noexcept {
    rs.max_hp = static_cast<int16_t>(rs.max_hp + amount);
    int hp = static_cast<int>(rs.hp) + amount;
    if (hp > rs.max_hp) {
        hp = rs.max_hp;
    }
    rs.hp = static_cast<int16_t>(hp);
}

// The canSpawn context for Calling Bell's three pool pops, mirroring
// neow_spawn_context (neow.cpp): every gate filled from the live run. The
// draws are COMMON/UNCOMMON/RARE so the boss gates cannot fire, but the
// context is built whole rather than partially so the body stays correct at
// any future call site.
[[nodiscard]] RelicSpawnContext equip_spawn_context(const RunState& rs) noexcept {
    RelicSpawnContext ctx{};
    ctx.floor = rs.floor;
    ctx.act = rs.act;
    fill_deck_spawn_gates(rs, ctx);
    fill_campfire_relic_count(rs, ctx);
    fill_boss_spawn_gates(rs, ctx);
    return ctx;
}

}  // namespace

// Astrolabe.giveCards (Astrolabe.java:65-79), for picks already made: per card,
// in PICK order, masterDeck.removeCard(card) then transformCard(card, true,
// miscRng) -- ONE miscRng draw each through the shared transform_card list --
// then `if canUpgrade() upgrade()` (AbstractDungeon.java:873-876, no RNG), the
// obtain queued as a ShowCardAndObtainEffect that appends only when its
// animation completes. EQUIVALENT SYNCHRONOUS ORDER: the draws read the CARD
// POOLS, never the deck (returnTrulyRandomCardFromAvailable,
// AbstractDungeon.java:1016-1045), so removing all picks first (descending
// index, to keep the recorded indices valid) and then drawing+appending in
// pick order produces the same miscRng sequence, the same identities and the
// same final deck as the Java's remove/draw interleaving with deferred
// appends. The deferred-append half is the b14_accept2 RACE shape: a capture
// can show the deck shrunk-but-not-yet-regrown mid-animation, which the replay
// harness already classifies narrowly.
void relic_astrolabe_transform_cards(RunState& rs, RngStream& misc_rng,
                                     const uint16_t* deck_indices,
                                     int count) noexcept {
    // Astrolabe's set is never larger than the grid's 3 picks (the screenless
    // branch runs only when the whole purgeable list fits in 3).
    constexpr int kAstrolabePickCap = 3;
    assert(count >= 0 && count <= kAstrolabePickCap);
    CardId src[kAstrolabePickCap] = {};
    uint16_t sorted[kAstrolabePickCap] = {};
    for (int i = 0; i < count; ++i) {
        src[i] = static_cast<CardId>(rs.master_deck[deck_indices[i]].card_id);
        sorted[i] = deck_indices[i];
    }
    // Remove in descending master-deck index order (insertion sort; count <= 3).
    for (int i = 1; i < count; ++i) {
        for (int j = i; j > 0 && sorted[j] > sorted[j - 1]; --j) {
            const uint16_t t = sorted[j];
            sorted[j] = sorted[j - 1];
            sorted[j - 1] = t;
        }
    }
    for (int i = 0; i < count; ++i) {
        (void)remove_master_deck_card(rs, sorted[i]);
    }
    for (int i = 0; i < count; ++i) {
        const CardId got = transform_card(misc_rng, src[i]);
        // autoUpgrade: transformedCard.canUpgrade() is `type != CURSE && type
        // != STATUS && !upgraded` (AbstractCard.java:672-679); the transformed
        // copy is fresh (upgrade 0), so only the type test bites. Searing Blow
        // overrides it to always-true, which agrees with this test at 0.
        const CardDef* def = card_def(got);
        const uint8_t up = def != nullptr && def->type != CardType::CURSE &&
                                   def->type != CardType::STATUS
                               ? 1
                               : 0;
        // The obtain door: ShowCardAndObtainEffect -> souls.obtain ->
        // masterDeck.addToTop == APPEND (CardGroup.java:455-457), with the
        // Omamori curse branch ahead of it -- exactly add_card_to_master_deck.
        (void)add_card_to_master_deck(rs, got, up);
    }
}

void relic_on_equip_screen_pandoras_box(RunState& rs, RngStream& /*misc_rng*/,
                                        RelicSlot& /*slot*/,
                                        RelicEquipContext& ctx) noexcept {
    // PandorasBox.onEquip (PandorasBox.java:54-77). Step 1: iterator-remove
    // every starter-tagged card from the master deck, SYNCHRONOUSLY, counting
    // them. The Java's i.remove() edits the raw group with no removeCard()
    // call; remove_master_deck_card differs only in the onMasterDeckChange
    // recompute it runs per removal (idempotent -- Du-Vu Doll recounts from
    // scratch) and Parasite's on-remove loss (never starter-tagged), so the
    // resulting state is identical.
    int count = 0;
    for (uint16_t i = 0; i < rs.master_deck_count;) {
        if (pandoras_starter_tagged(
                static_cast<CardId>(rs.master_deck[i].card_id))) {
            (void)remove_master_deck_card(rs, i);
            ++count;
        } else {
            ++i;
        }
    }
    if (count == 0) {
        return;  // no draws, no screen (:64) -- fully inert.
    }

    // Step 2: `count` returnTrulyRandomCard().makeCopy() draws (:67) -- ONE
    // cardRandomRng random(size - 1) each over the UNFILTERED 72-row src-pool
    // concatenation (AbstractDungeon.java:936-942; kIroncladTrulyRandomPool is
    // that list -- NOT the HEALING-filtered kIroncladCombatPool). No miscRng,
    // no relicRng, no cardRng.
    CardId drawn[kMasterDeckCap] = {};
    for (int i = 0; i < count; ++i) {
        const int32_t roll = random(
            ctx.card_random_rng,
            sts::registry::kIroncladTrulyRandomPoolCount - 1);
        drawn[i] = sts::registry::kIroncladTrulyRandomPool[
            static_cast<std::size_t>(roll)];
    }

    // Step 3: the deck receives them in REVERSE draw order. addToBottom
    // PREPENDS each draw into the display group (CardGroup.java:459-461), the
    // confirmation grid's confirm iterates that group FORWARD and each
    // FastCardObtainEffect appends (masterDeck.addToTop, :455-457) -- so with
    // draws d0..dn-1 the deck gains dn-1 .. d0. Getting this backwards is the
    // STS00051 bug class: right multiset, wrong master_deck[i]. Each obtain
    // goes through the Omamori-aware door (FastCardObtainEffect.java:24-28 ==
    // add_card_to_master_deck's curse branch; unreachable here -- no red pool
    // row is a curse -- but the door is the door). The relic onObtainCard
    // fan-out runs inside it; the Java additionally ran onPreviewObtainCard at
    // draw time (:70-72), which only matters to the eggs, and an egg upgraded
    // at preview is a no-op at obtain -- one upgrade either way. At a Neow
    // boss swap the relic list holds only Pandora's Box itself (Burning Blood
    // was removed before the pool pop), so the fan-outs see no listener.
    //
    // The confirmation grid itself (openConfirmationGrid, :75) carries NO
    // choice -- one Confirm button over a display-only group -- so the run
    // layer skips it: ctx.screen stays NONE and the deck edit is synchronous.
    // A capture WILL contain that grid and its confirm command; the replay
    // harness's GRID classification is the named seam for that (command_map
    // triage note), not this body.
    for (int i = count - 1; i >= 0; --i) {
        (void)add_card_to_master_deck(rs, drawn[i]);
    }
}

void relic_on_equip_screen_tiny_house(RunState& rs, RngStream& misc_rng,
                                      RelicSlot& /*slot*/,
                                      RelicEquipContext& ctx) noexcept {
    // TinyHouse.onEquip (TinyHouse.java:36-63), draws in Java order:
    //
    // 1. Collections.shuffle(upgradableCards, new Random(miscRng.randomLong()))
    //    (:43) -- the seed draw is UNCONDITIONAL, taken even when the list is
    //    empty (it sits ahead of the isEmpty() guard at :44), the War Paint /
    //    Whetstone shape. The candidate list is built in MASTER-DECK order
    //    over canUpgrade() (:38-42) -- rest_card_upgradeable is that predicate,
    //    including Searing Blow's always-true override.
    uint16_t candidates[kMasterDeckCap] = {};
    uint16_t count = 0;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        if (rest_card_upgradeable(rs.master_deck[i])) {
            candidates[count++] = i;
        }
    }
    JdkRandom jdk(random_long(misc_rng));
    jdk_shuffle(std::span<uint16_t>(candidates, count), jdk);
    if (count > 0) {
        // Only get(0) is upgraded on BOTH arms (:45-56): the else arm's
        // get(1) line is bottledCardUpgradeCheck, a bottled-relic description
        // refresh with no simulator consequence. ++ (not = 1) is the
        // rest_upgrade_card precedent -- Searing Blow stacks.
        ++rs.master_deck[candidates[0]].upgrade;
    }

    // 2. increaseMaxHp(5, true) (:58) -- current HP rises too.
    increase_max_hp_with_heal(rs, 5);

    // 3-5. The reward screen: addGoldToRewards(50) (:59), then
    // addPotionToRewards(PotionHelper.getRandomPotion(miscRng)) (:60) -- the
    // FLAT one-draw getRandomPotion(rng) (PotionHelper.java:164-167), NOT
    // returnRandomPotion's d100 tier gate + rejection loop, and off miscRng,
    // not potionRng -- then combatRewardScreen.open(DESCRIPTIONS[3]) (:61),
    // whose setupItemReward copies the room rows and appends the unconditional
    // getRewardCards() row (CombatRewardScreen.java:241-262, :72-99). At Neow
    // that row is rolled AND KEPT -- NeowRoom's event leaves noCardsInRewards
    // false -- which is where Tiny House's "1 card" comes from (contrast
    // Calling Bell, which rolls the same row and throws it away). Screen row
    // order is therefore GOLD, POTION, CARDS.
    ctx.rewards = RewardScreen{};
    ctx.rewards.open_card_item = kNoOpenCardReward;
    (void)add_event_combat_gold_reward(rs, ctx.rewards, 50);
    RunRewardItem& potion = ctx.rewards.items[ctx.rewards.count++];
    potion = RunRewardItem{};
    potion.kind = static_cast<uint8_t>(RewardItemKind::POTION);
    potion.id = static_cast<uint16_t>(get_random_potion(misc_rng));
    roll_setup_item_card_reward(rs, ctx.reward_room, ctx.rewards);
    ctx.screen = RelicEquipScreen::ITEM_REWARD;
}

void relic_on_equip_screen_astrolabe(RunState& rs, RngStream& misc_rng,
                                     RelicSlot& /*slot*/,
                                     RelicEquipContext& ctx) noexcept {
    // Astrolabe.onEquip (Astrolabe.java:35-55). The candidate list is
    // getPurgeableCards (CardGroup.java:978-985 -- master-deck order minus
    // Necronomicurse / Curse of the Bell / Ascender's Bane), which
    // rest_card_purgeable renders per row.
    uint16_t purgeable[kMasterDeckCap] = {};
    uint16_t count = 0;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        if (rest_card_purgeable(rs.master_deck[i])) {
            purgeable[count++] = i;
        }
    }
    if (count == 0) {
        return;  // (:42-45) no RNG, no screen.
    }
    if (count <= 3) {
        // (:46-47) screenless: transform ALL of them, in master-deck order.
        relic_astrolabe_transform_cards(rs, misc_rng, purgeable,
                                        static_cast<int>(count));
        return;
    }
    // (:48-54) the 3-pick grid: open(tmp, 3, msg, false, false, false, false)
    // -- no confirm step (the !anyNumber arm closes on the third click,
    // GridCardSelectScreen.java:215-234) and no cancel. The picks apply in
    // click order when the set completes; the call site owns the screen and
    // routes the completed set back through relic_astrolabe_transform_cards.
    ctx.screen = RelicEquipScreen::GRID_TRANSFORM_UPGRADE;
    ctx.grid_picks = 3;
}

void relic_on_equip_screen_empty_cage(RunState& rs, RngStream& /*misc_rng*/,
                                      RelicSlot& /*slot*/,
                                      RelicEquipContext& ctx) noexcept {
    // EmptyCage.onEquip (EmptyCage.java:35-57). Same purgeable list as
    // Astrolabe; NO RNG on any path. The RoomPhase INCOMPLETE -> COMPLETE
    // bracket (:43, :77) exists so the room cannot be left mid-grid and is
    // presentation-layer here (the run layer's phase machine already cannot
    // leave Neow with a grid up).
    uint16_t purgeable[kMasterDeckCap] = {};
    uint16_t count = 0;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        if (rest_card_purgeable(rs.master_deck[i])) {
            purgeable[count++] = i;
        }
    }
    if (count == 0) {
        return;  // (:48-51)
    }
    if (count <= 2) {
        // (:52-53) deleteCards on the whole list, synchronously (unlike
        // Astrolabe's animation-deferred obtains, the removals land at once,
        // :67-79). Descending index keeps the recorded indices valid; the
        // final deck is order-identical to the Java's forward object removal.
        for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
            (void)remove_master_deck_card(rs, purgeable[i]);
        }
        return;
    }
    // (:55) the 2-pick purge grid: forPurge=true at numCards != 1 closes on
    // the second click with no confirmation popup (GridCardSelectScreen.java:
    // 189-209). Removal per pick is the call site's REMOVE grid arm.
    ctx.screen = RelicEquipScreen::GRID_REMOVE;
    ctx.grid_picks = 2;
}

void relic_on_equip_screen_calling_bell(RunState& rs, RngStream& /*misc_rng*/,
                                        RelicSlot& /*slot*/,
                                        RelicEquipContext& ctx) noexcept {
    // CallingBell.onEquip (CallingBell.java:36-45) + update (:47-65). The
    // onEquip half presents the Bell Curse on a choice-free confirmation grid;
    // the update half fires the frame after it closes. Both are synchronous
    // here; the confirmation grid is skipped for the same reason as Pandora's
    // (zero choice content -- the capture-side confirm is the harness's seam).
    //
    // 1. The curse reaches the deck through FastCardObtainEffect -> souls
    //    .obtain -> masterDeck.addToTop == APPEND (CardGroup.java:455-457),
    //    with the constructor's Omamori branch ahead of the add
    //    (FastCardObtainEffect.java:24-28): a held charge consumes the curse
    //    instead. That is add_card_to_master_deck exactly -- Omamori door,
    //    append position, onObtainCard/onMasterDeckChange fan-outs. (In S1
    //    the Omamori branch is unreachable -- the boss swap is the only
    //    Calling Bell path and no relic can precede it -- but the door is the
    //    door.)
    (void)add_card_to_master_deck(rs, CardId::CURSE_OF_THE_BELL);

    // 2. combatRewardScreen.open() (:51) rolls the unconditional
    //    setupItemReward card row -- a FULL cardRng assembly, pity counter
    //    included -- and rewards.clear() (:52) throws the whole list away with
    //    the draws already spent. Skipping the roll would desync cardRng and
    //    cardBlizzRandomizer for the rest of the run; this is the single
    //    easiest thing to get wrong in this relic. (Contrast Tiny House: same
    //    roll, KEPT.)
    ctx.rewards = RewardScreen{};
    ctx.rewards.open_card_item = kNoOpenCardReward;
    roll_setup_item_card_reward(rs, ctx.reward_room, ctx.rewards);
    (void)remove_first_card_reward_item(ctx.rewards);

    // 3. Three fixed-tier returnRandomScreenlessRelic pops (:53-55), strictly
    //    COMMON then UNCOMMON then RARE: pool front-pops off the lists
    //    shuffled once at initializeRelicList -- pool-consuming, relicRng-
    //    counter-NEUTRAL. Each lands as a REWARD ROW, claimed through the
    //    ordinary screen (its own onEquip fires at claim time), never
    //    auto-equipped.
    const RelicSpawnContext sctx = equip_spawn_context(rs);
    const RelicTier tiers[3] = {RelicTier::COMMON, RelicTier::UNCOMMON,
                                RelicTier::RARE};
    for (const RelicTier tier : tiers) {
        const RelicId id = return_random_screenless_relic(rs, tier, sctx);
        RunRewardItem& item = ctx.rewards.items[ctx.rewards.count++];
        item = RunRewardItem{};
        item.kind = static_cast<uint8_t>(RewardItemKind::RELIC);
        item.id = static_cast<uint16_t>(id);
    }
    ctx.screen = RelicEquipScreen::ITEM_REWARD;
}

}  // namespace sts::engine
