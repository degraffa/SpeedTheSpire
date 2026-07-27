// The Act-1-reachable one-time special events (registry/events.yaml ids 18, 19,
// 22, 23, 25, 27, 30, 31; src/engine/events/one_time_specials.cpp) AND the
// gate evidence for the six that the getShrine filter excludes from Act 1.
//
// Coverage:
//   * tier-2 per implemented row: generated EventDef metadata + linked body.
//   * GATE EVIDENCE for every excluded special: build_shrine_pool refuses to
//     offer it from an Act-1 state that satisfies EVERY non-act condition, so
//     the exclusion is provably the act gate and nothing else; and it DOES
//     offer it once the state moves to the gating act. This is what makes the
//     "reachable set" claim testable rather than a comment.
//   * directed dialog scripts through the public API for every option.
//   * RNG attribution named per event: FaceTrader's one miscRng randomLong,
//     Lab / The Woman in Blue's flat potionRng draws, We Meet Again's THREE
//     conditional miscRng draws, and the zero-draw events proved zero-draw.
//   * A15 variants: FaceTrader 75 -> 50 gold, Lab 3 -> 2 potions, The Woman in
//     Blue's 5%-max-HP refusal penalty, and NoteForYourself leaving the pool.

#include "sts/engine/event_framework.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>

#include <gtest/gtest.h>

#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/potions.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"
#include "sts/registry/event_table.hpp"

namespace {

using namespace sts::engine;
namespace reg = sts::registry;

constexpr int64_t kSeed = 6070809;

RunController raw_event_controller(EventId id, int ascension = 0) {
    RunController rc = run_begin(kSeed, static_cast<uint8_t>(ascension));
    rc.run.floor = 9;
    rc.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
    rc.room_type = static_cast<uint8_t>(RoomType::Event);
    rc.event = EventDialogState{};
    rc.event.event_id = static_cast<uint16_t>(id);
    rc.rewards = RewardScreen{};
    rc.rewards.open_card_item = kNoOpenCardReward;
    return rc;
}

void enter(RunController& rc) {
    const EventDialogImpl* impl = event_dialog_impl(rc.event.event_id);
    ASSERT_NE(impl, nullptr);
    impl->on_enter(rc, rc.event);
}

RunController event_controller(EventId id, int ascension = 0) {
    RunController rc = raw_event_controller(id, ascension);
    const EventDialogImpl* impl = event_dialog_impl(rc.event.event_id);
    EXPECT_NE(impl, nullptr);
    impl->on_enter(rc, rc.event);
    return rc;
}

void choose(RunController& rc, uint8_t option) {
    const Action action = make_action(ActionVerb::CHOOSE, option);
    StepResult result{};
    advance(std::span<RunController>(&rc, 1),
            std::span<const Action>(&action, 1),
            std::span<StepResult>(&result, 1));
}

EventDialogMenu menu(const RunController& rc) {
    EventDialogMenu out{};
    const EventDialogImpl* impl = event_dialog_impl(rc.event.event_id);
    EXPECT_NE(impl, nullptr);
    impl->build_menu(rc, rc.event, out);
    return out;
}

void set_deck(RunState& rs,
              std::initializer_list<std::pair<CardId, uint8_t>> cards) {
    rs.master_deck_count = 0;
    for (const auto& [id, upgrade] : cards) {
        ASSERT_TRUE(add_card_to_master_deck(rs, id, upgrade));
    }
}

void give_relic(RunState& rs, RelicId id, int16_t counter = -1) {
    ASSERT_LT(rs.relic_count, kRelicCap);
    rs.relics[rs.relic_count].relic_id = static_cast<uint16_t>(id);
    rs.relics[rs.relic_count].counter = counter;
    ++rs.relic_count;
}

bool pool_offers(const RunState& rs, EventId id) {
    uint16_t out[kShrineListCount + kSpecialListCount]{};
    const int n = build_shrine_pool(rs, out, kShrineListCount + kSpecialListCount);
    return std::find(out, out + n, static_cast<uint16_t>(id)) != out + n;
}

// =============================================================================
// Reachability: the getShrine filter is the authority for the whole batch
// =============================================================================

TEST(OneTimeSpecials, ActOneOffersExactlyTheEightImplementedSpecials) {
    RunController rc = run_begin(kSeed, 0);
    rc.run.act = 1;
    rc.run.gold = 500;   // clears Designer's 75, The Joust's 50, Woman's 50
    rc.run.hp = 70;      // clears Knowing Skull's > 12
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::DOUBT, 0}});  // isCursed
    give_relic(rc.run, RelicId::BURNING_BLOOD);
    give_relic(rc.run, RelicId::CIRCLET);  // clears N'loth's >= 2 relics

    // Every non-act gate is satisfied, so anything still absent is absent
    // BECAUSE OF ITS ACT GATE and nothing else.
    for (const EventId id :
         {EventId::ACCURSED_BLACKSMITH, EventId::BONFIRE_ELEMENTALS,
          EventId::FACE_TRADER, EventId::FOUNTAIN_OF_CLEANSING, EventId::LAB,
          EventId::NOTE_FOR_YOURSELF, EventId::WE_MEET_AGAIN,
          EventId::THE_WOMAN_IN_BLUE}) {
        EXPECT_TRUE(pool_offers(rc.run, id))
            << "EventId " << static_cast<int>(id) << " should be Act-1 offered";
        EXPECT_NE(event_dialog_impl(static_cast<uint16_t>(id)), nullptr);
    }
    for (const EventId id : {EventId::DESIGNER, EventId::DUPLICATOR,
                             EventId::KNOWING_SKULL, EventId::NLOTH,
                             EventId::SECRET_PORTAL, EventId::THE_JOUST}) {
        EXPECT_FALSE(pool_offers(rc.run, id))
            << "EventId " << static_cast<int>(id) << " must be act-gated out";
        EXPECT_EQ(event_dialog_impl(static_cast<uint16_t>(id)), nullptr);
    }
}

TEST(OneTimeSpecials, EachExcludedSpecialAppearsOnceItsGatingActArrives) {
    RunController rc = run_begin(kSeed, 0);
    rc.run.gold = 500;
    rc.run.hp = 70;
    give_relic(rc.run, RelicId::BURNING_BLOOD);
    give_relic(rc.run, RelicId::CIRCLET);

    rc.run.act = 2;
    // TheCity turns on Designer, Duplicator, Knowing Skull, N'loth, The Joust.
    for (const EventId id : {EventId::DESIGNER, EventId::DUPLICATOR,
                             EventId::KNOWING_SKULL, EventId::NLOTH,
                             EventId::THE_JOUST}) {
        EXPECT_TRUE(pool_offers(rc.run, id)) << static_cast<int>(id);
    }
    // FaceTrader spans Exordium and TheCity, so act 2 keeps it and act 3 drops
    // it -- the asymmetry that makes it Act-1 reachable in the first place.
    EXPECT_TRUE(pool_offers(rc.run, EventId::FACE_TRADER));
    rc.run.act = 3;
    EXPECT_FALSE(pool_offers(rc.run, EventId::FACE_TRADER));
    EXPECT_TRUE(pool_offers(rc.run, EventId::DUPLICATOR));
    // SecretPortal additionally needs an unmodelled 800s playtime, so it stays
    // out even in TheBeyond -- recorded, not accidental.
    EXPECT_FALSE(pool_offers(rc.run, EventId::SECRET_PORTAL));

    // The two NON-act gates on Act-1-reachable rows, proved by their absence.
    RunController poor = run_begin(kSeed, 0);
    poor.run.act = 1;
    poor.run.gold = 49;
    set_deck(poor.run, {{CardId::STRIKE, 0}});
    EXPECT_FALSE(pool_offers(poor.run, EventId::THE_WOMAN_IN_BLUE));
    EXPECT_FALSE(pool_offers(poor.run, EventId::FOUNTAIN_OF_CLEANSING));
    poor.run.gold = 50;
    EXPECT_TRUE(pool_offers(poor.run, EventId::THE_WOMAN_IN_BLUE));
    ASSERT_TRUE(add_card_to_master_deck(poor.run, CardId::DOUBT));
    EXPECT_TRUE(pool_offers(poor.run, EventId::FOUNTAIN_OF_CLEANSING));
}

TEST(OneTimeSpecials, EveryImplementedRowCarriesAuditedRegistryMetadata) {
    struct Row {
        EventId id;
        uint8_t screens;
        uint8_t a15_changes;
    };
    constexpr Row kRows[] = {
        {EventId::ACCURSED_BLACKSMITH, 3, 0},
        {EventId::BONFIRE_ELEMENTALS, 4, 0},
        {EventId::FACE_TRADER, 3, 1},
        {EventId::FOUNTAIN_OF_CLEANSING, 2, 0},
        {EventId::LAB, 1, 1},
        {EventId::NOTE_FOR_YOURSELF, 4, 1},
        {EventId::WE_MEET_AGAIN, 2, 0},
        {EventId::THE_WOMAN_IN_BLUE, 2, 1},
    };
    for (const Row& row : kRows) {
        const reg::EventDef* def = reg::event_def(row.id);
        ASSERT_NE(def, nullptr);
        EXPECT_TRUE(def->native);
        EXPECT_TRUE(def->implemented);
        EXPECT_EQ(def->screen_count, row.screens);
        EXPECT_EQ(def->a15_change_count, row.a15_changes);
    }
}

// =============================================================================
// Accursed Blacksmith
// =============================================================================

TEST(AccursedBlacksmith, ForgeIsGatedRummagePaysPainThenWarpedTongs) {
    {
        RunController rc = event_controller(EventId::ACCURSED_BLACKSMITH);
        set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::DEFEND, 1}});
        const EventDialogMenu m = menu(rc);
        ASSERT_EQ(m.count, 3);
        EXPECT_TRUE(m.enabled[0]);
        choose(rc, 0);
        EXPECT_EQ(rc.event.grid_kind,
                  static_cast<uint8_t>(EventGridKind::UPGRADE));
        choose(rc, 0);
        EXPECT_EQ(rc.run.master_deck[0].upgrade, 1);
        choose(rc, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
    {
        RunController rc = event_controller(EventId::ACCURSED_BLACKSMITH, 20);
        set_deck(rc.run, {{CardId::STRIKE, 1}, {CardId::DOUBT, 0}});
        const EventDialogMenu m = menu(rc);
        EXPECT_FALSE(m.enabled[0]);
        EXPECT_TRUE(m.enabled[1]);
    }
    {
        RunController rc = event_controller(EventId::ACCURSED_BLACKSMITH);
        set_deck(rc.run, {{CardId::STRIKE, 0}});
        const uint8_t relics = rc.run.relic_count;
        choose(rc, 1);
        ASSERT_EQ(rc.run.master_deck_count, 2);
        EXPECT_EQ(rc.run.master_deck[1].card_id,
                  static_cast<uint16_t>(CardId::PAIN));
        ASSERT_EQ(rc.run.relic_count, relics + 1);
        EXPECT_EQ(rc.run.relics[relics].relic_id,
                  static_cast<uint16_t>(RelicId::WARPED_TONGS));
        choose(rc, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
    {
        // Rummage through Omamori: the relic still lands, the curse does not.
        RunController rc = event_controller(EventId::ACCURSED_BLACKSMITH);
        set_deck(rc.run, {{CardId::STRIKE, 0}});
        give_relic(rc.run, RelicId::OMAMORI, 1);
        choose(rc, 1);
        EXPECT_EQ(rc.run.master_deck_count, 1);
        EXPECT_EQ(rc.run.relics[1].counter, 0);
        EXPECT_EQ(rc.run.relics[rc.run.relic_count - 1].relic_id,
                  static_cast<uint16_t>(RelicId::WARPED_TONGS));
    }
    {
        RunController rc = event_controller(EventId::ACCURSED_BLACKSMITH);
        const uint8_t relics = rc.run.relic_count;
        const uint16_t deck = rc.run.master_deck_count;
        choose(rc, 2);
        EXPECT_EQ(rc.run.relic_count, relics);
        EXPECT_EQ(rc.run.master_deck_count, deck);
        choose(rc, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
}

// =============================================================================
// Bonfire Elementals
// =============================================================================

TEST(BonfireElementals, PayoutFollowsTheOfferedCardsRarityAndPrecedesRemoval) {
    struct Case {
        CardId offered;
        int start_hp;
        int expect_hp;
        int expect_max_hp;
        bool expect_relic;
    };
    // BASIC pays nothing; COMMON heals 5; UNCOMMON full-heals; RARE adds 10
    // max HP (which itself heals 10) and then full-heals; CURSE pays a relic.
    const Case cases[] = {
        {CardId::STRIKE, 40, 40, 80, false},    // BASIC
        {CardId::ANGER, 40, 45, 80, false},     // COMMON
        {CardId::DROPKICK, 40, 80, 80, false},  // UNCOMMON
        {CardId::OFFERING, 40, 90, 90, false},  // RARE
        {CardId::DOUBT, 40, 40, 80, true},      // CURSE
    };
    for (const Case& c : cases) {
        RunController rc = event_controller(EventId::BONFIRE_ELEMENTALS);
        set_deck(rc.run, {{c.offered, 0}});
        rc.run.hp = static_cast<int16_t>(c.start_hp);
        rc.run.max_hp = 80;
        const uint8_t relics = rc.run.relic_count;
        choose(rc, 0);
        EXPECT_EQ(rc.event.screen, 1);
        choose(rc, 0);
        ASSERT_EQ(rc.event.grid_kind,
                  static_cast<uint8_t>(EventGridKind::PURGE));
        choose(rc, 0);
        EXPECT_EQ(rc.run.master_deck_count, 0);
        EXPECT_EQ(rc.run.hp, c.expect_hp)
            << "offered " << static_cast<int>(c.offered);
        EXPECT_EQ(rc.run.max_hp, c.expect_max_hp);
        if (c.expect_relic) {
            ASSERT_EQ(rc.run.relic_count, relics + 1);
            EXPECT_EQ(rc.run.relics[relics].relic_id,
                      static_cast<uint16_t>(RelicId::SPIRIT_POOP));
        } else {
            EXPECT_EQ(rc.run.relic_count, relics);
        }
        choose(rc, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
}

TEST(BonfireElementals, SecondSpiritPoopBecomesACircletAndParasiteLosesMaxHp) {
    {
        RunController rc = event_controller(EventId::BONFIRE_ELEMENTALS);
        set_deck(rc.run, {{CardId::DOUBT, 0}});
        give_relic(rc.run, RelicId::SPIRIT_POOP);
        const uint8_t relics = rc.run.relic_count;
        choose(rc, 0);
        choose(rc, 0);
        choose(rc, 0);
        ASSERT_EQ(rc.run.relic_count, relics + 1);
        EXPECT_EQ(rc.run.relics[relics].relic_id,
                  static_cast<uint16_t>(RelicId::CIRCLET));
    }
    {
        // Parasite is a CURSE: the relic payout runs first, then the removal
        // takes the 3 max HP.
        RunController rc = event_controller(EventId::BONFIRE_ELEMENTALS);
        set_deck(rc.run, {{CardId::PARASITE, 0}});
        rc.run.hp = 80;
        rc.run.max_hp = 80;
        choose(rc, 0);
        choose(rc, 0);
        choose(rc, 0);
        EXPECT_EQ(rc.run.max_hp, 77);
        EXPECT_EQ(rc.run.hp, 77);
        EXPECT_EQ(rc.run.relics[rc.run.relic_count - 1].relic_id,
                  static_cast<uint16_t>(RelicId::SPIRIT_POOP));
    }
    {
        // No purgeable card at all: the dialog reaches its final page anyway.
        RunController rc = event_controller(EventId::BONFIRE_ELEMENTALS, 20);
        set_deck(rc.run, {{CardId::ASCENDERS_BANE, 0}});
        choose(rc, 0);
        choose(rc, 0);
        EXPECT_EQ(rc.event.grid_kind,
                  static_cast<uint8_t>(EventGridKind::NONE));
        EXPECT_EQ(rc.run.master_deck_count, 1);
        choose(rc, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
}

// =============================================================================
// FaceTrader
// =============================================================================

TEST(FaceTrader, TouchIsTieredGoldThenTenthMaxHpDamage) {
    {
        RunController rc = event_controller(EventId::FACE_TRADER);
        rc.run.max_hp = 80;
        rc.run.hp = 80;
        enter(rc);  // re-freeze the constructor values against this max HP
        const int gold = rc.run.gold;
        EXPECT_EQ(menu(rc).count, 1);
        choose(rc, 0);
        ASSERT_EQ(menu(rc).count, 3);
        choose(rc, 0);
        EXPECT_EQ(rc.run.gold, gold + 75);
        EXPECT_EQ(rc.run.hp, 72);  // 80 / 10
        choose(rc, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
    {
        RunController rc = event_controller(EventId::FACE_TRADER, 15);
        rc.run.max_hp = 5;
        rc.run.hp = 5;
        enter(rc);
        const int gold = rc.run.gold;
        choose(rc, 0);
        choose(rc, 0);
        EXPECT_EQ(rc.run.gold, gold + 50);  // A15 goldReward
        EXPECT_EQ(rc.run.hp, 4);            // max(1, 5 / 10)
    }
}

TEST(FaceTrader, TradeShufflesTheUnownedFacesOnOneMiscRngRandomLong) {
    RunController rc = event_controller(EventId::FACE_TRADER);
    rc.combat.misc_rng = from_seed(24680);
    RngStream expected_rng = rc.combat.misc_rng;
    std::array<RelicId, 5> faces = {
        RelicId::CULTIST_MASK, RelicId::FACE_OF_CLERIC, RelicId::GREMLIN_MASK,
        RelicId::NLOTHS_MASK,  RelicId::SSSERPENT_HEAD,
    };
    JdkRandom jdk(random_long(expected_rng));
    jdk_shuffle(std::span<RelicId>(faces), jdk);
    const uint8_t relics = rc.run.relic_count;

    choose(rc, 0);
    choose(rc, 1);
    EXPECT_EQ(rc.combat.misc_rng.counter, expected_rng.counter);
    ASSERT_EQ(rc.run.relic_count, relics + 1);
    EXPECT_EQ(rc.run.relics[relics].relic_id,
              static_cast<uint16_t>(faces[0]));

    // All five owned: the Circlet fallback, and the randomLong is STILL drawn
    // because the Random is constructed before the (no-op) shuffle.
    RunController full = event_controller(EventId::FACE_TRADER);
    for (const RelicId id :
         {RelicId::CULTIST_MASK, RelicId::FACE_OF_CLERIC,
          RelicId::GREMLIN_MASK, RelicId::NLOTHS_MASK,
          RelicId::SSSERPENT_HEAD}) {
        give_relic(full.run, id);
    }
    const uint8_t owned = full.run.relic_count;
    const int before = full.combat.misc_rng.counter;
    choose(full, 0);
    choose(full, 1);
    EXPECT_GT(full.combat.misc_rng.counter, before);
    ASSERT_EQ(full.run.relic_count, owned + 1);
    EXPECT_EQ(full.run.relics[owned].relic_id,
              static_cast<uint16_t>(RelicId::CIRCLET));
}

TEST(FaceTrader, LeaveTouchesNothingAndDrawsNothing) {
    RunController rc = event_controller(EventId::FACE_TRADER);
    const int gold = rc.run.gold;
    const int hp = rc.run.hp;
    const int before = rc.combat.misc_rng.counter;
    choose(rc, 0);
    choose(rc, 2);
    EXPECT_EQ(rc.run.gold, gold);
    EXPECT_EQ(rc.run.hp, hp);
    EXPECT_EQ(rc.combat.misc_rng.counter, before);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

// =============================================================================
// Fountain of Cleansing
// =============================================================================

TEST(FountainOfCleansing, DrinkRemovesEveryCurseButAscendersBane) {
    RunController rc = event_controller(EventId::FOUNTAIN_OF_CLEANSING, 20);
    set_deck(rc.run, {{CardId::ASCENDERS_BANE, 0},
                      {CardId::STRIKE, 0},
                      {CardId::DOUBT, 0},
                      {CardId::PARASITE, 0},
                      {CardId::REGRET, 0},
                      {CardId::DEFEND, 0}});
    rc.run.max_hp = 80;
    rc.run.hp = 80;
    const int before = rc.combat.misc_rng.counter;

    choose(rc, 0);
    ASSERT_EQ(rc.run.master_deck_count, 3);
    EXPECT_EQ(rc.run.master_deck[0].card_id,
              static_cast<uint16_t>(CardId::ASCENDERS_BANE));
    EXPECT_EQ(rc.run.master_deck[1].card_id,
              static_cast<uint16_t>(CardId::STRIKE));
    EXPECT_EQ(rc.run.master_deck[2].card_id,
              static_cast<uint16_t>(CardId::DEFEND));
    EXPECT_EQ(rc.run.max_hp, 77);  // Parasite's removal hook fired exactly once
    EXPECT_EQ(rc.combat.misc_rng.counter, before);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(FountainOfCleansing, RefusingKeepsEveryCurse) {
    RunController rc = event_controller(EventId::FOUNTAIN_OF_CLEANSING);
    set_deck(rc.run, {{CardId::DOUBT, 0}, {CardId::REGRET, 0}});
    choose(rc, 1);
    EXPECT_EQ(rc.run.master_deck_count, 2);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

// =============================================================================
// Lab
// =============================================================================

TEST(Lab, PotionsAreFlatPotionRngDrawsAndTheCountIsTiered) {
    for (const int ascension : {0, 15}) {
        const int expected_count = ascension >= 15 ? 2 : 3;
        RunController rc = event_controller(EventId::LAB, ascension);
        rc.run.potion_rng = from_seed(13571);
        RngStream expected_rng = rc.run.potion_rng;
        std::array<PotionId, 3> expected{};
        for (int i = 0; i < expected_count; ++i) {
            expected[static_cast<std::size_t>(i)] = static_cast<PotionId>(
                random(expected_rng, kPotionPoolSize - 1) + 1);
        }

        EXPECT_EQ(menu(rc).count, 1);
        choose(rc, 0);
        ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
        EXPECT_EQ(rc.event.event_id, 0);
        // One draw per potion -- the flat PotionHelper.getRandomPotion, not
        // returnRandomPotion's variable-length rejection sampling.
        EXPECT_EQ(rc.run.potion_rng.counter, expected_rng.counter);
        EXPECT_EQ(rc.run.potion_rng.counter,
                  from_seed(13571).counter + expected_count);
        ASSERT_EQ(rc.rewards.count, expected_count);
        for (int i = 0; i < expected_count; ++i) {
            EXPECT_EQ(rc.rewards.items[i].kind,
                      static_cast<uint8_t>(RewardItemKind::POTION));
            EXPECT_EQ(rc.rewards.items[i].id,
                      static_cast<uint16_t>(expected[static_cast<std::size_t>(i)]));
        }
        choose(rc, 0);  // claim the first potion into a slot
        EXPECT_EQ(rc.run.potions[0],
                  static_cast<uint16_t>(expected[0]));
        choose(rc, kChooseProceed);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
}

// =============================================================================
// NoteForYourself
// =============================================================================

TEST(NoteForYourself, TakingTheCardInsertsAtTheTopThenOpensTheGiveAwayGrid) {
    RunController rc = event_controller(EventId::NOTE_FOR_YOURSELF);
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::DEFEND, 0}});
    EXPECT_EQ(menu(rc).count, 1);
    choose(rc, 0);
    ASSERT_EQ(menu(rc).count, 2);

    choose(rc, 0);
    ASSERT_EQ(rc.run.master_deck_count, 3);
    // addToTop, not addToBottom: the granted card is master-deck index 0.
    EXPECT_EQ(rc.run.master_deck[0].card_id,
              static_cast<uint16_t>(CardId::IRON_WAVE));
    EXPECT_EQ(rc.run.master_deck[0].upgrade, 0);
    EXPECT_EQ(rc.run.master_deck[1].card_id,
              static_cast<uint16_t>(CardId::STRIKE));
    ASSERT_EQ(rc.event.grid_kind, static_cast<uint8_t>(EventGridKind::PURGE));

    // The grid includes the card just taken (the game opens it over the whole
    // purgeable deck, after the insert).
    choose(rc, 0);
    ASSERT_EQ(rc.run.master_deck_count, 2);
    EXPECT_EQ(rc.run.master_deck[0].card_id,
              static_cast<uint16_t>(CardId::STRIKE));
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(NoteForYourself, RefusingChangesNothingAndTheRowLeavesThePoolAtA15) {
    RunController rc = event_controller(EventId::NOTE_FOR_YOURSELF);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    choose(rc, 0);
    choose(rc, 1);
    EXPECT_EQ(rc.run.master_deck_count, 1);
    EXPECT_EQ(rc.event.grid_kind, static_cast<uint8_t>(EventGridKind::NONE));
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));

    // isNoteForYourselfAvailable's ascension >= 15 branch keeps the key out of
    // specialOneTimeEventList entirely, so the row is unreachable at A15+.
    RunController low = run_begin(kSeed, 14);
    EXPECT_TRUE(pool_offers(low.run, EventId::NOTE_FOR_YOURSELF));
    RunController high = run_begin(kSeed, 15);
    EXPECT_FALSE(pool_offers(high.run, EventId::NOTE_FOR_YOURSELF));
}

// =============================================================================
// WeMeetAgain
// =============================================================================

TEST(WeMeetAgain, TheThreeOffersDrawMiscRngOnlyWhenTheyExist) {
    // Nothing to give: no potion held, under 50 gold, an all-BASIC deck.
    RunController none = raw_event_controller(EventId::WE_MEET_AGAIN);
    none.run.gold = 49;
    set_deck(none.run, {{CardId::STRIKE, 0}, {CardId::BASH, 0}});
    none.combat.misc_rng = from_seed(99001);
    const int untouched = none.combat.misc_rng.counter;
    enter(none);
    EXPECT_EQ(none.combat.misc_rng.counter, untouched);
    const EventDialogMenu m = menu(none);
    ASSERT_EQ(m.count, 4);
    EXPECT_FALSE(m.enabled[0]);
    EXPECT_FALSE(m.enabled[1]);
    EXPECT_FALSE(m.enabled[2]);
    EXPECT_TRUE(m.enabled[3]);

    // Everything available: three draws, in constructor order.
    RunController rc = raw_event_controller(EventId::WE_MEET_AGAIN);
    rc.run.gold = 200;
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::NONE);
    rc.run.potions[1] = 3;
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::ANGER, 0}});
    rc.combat.misc_rng = from_seed(99001);
    RngStream expected_rng = rc.combat.misc_rng;
    std::array<uint8_t, 1> held = {1};
    JdkRandom potion_rng(random_long(expected_rng));
    jdk_shuffle(std::span<uint8_t>(held), potion_rng);
    const int expected_gold = random(expected_rng, 50, 150);
    std::array<uint16_t, 1> offerable = {1};  // ANGER; STRIKE is BASIC
    JdkRandom card_rng(random_long(expected_rng));
    jdk_shuffle(std::span<uint16_t>(offerable), card_rng);

    enter(rc);
    EXPECT_EQ(rc.combat.misc_rng.counter, expected_rng.counter);
    EXPECT_EQ(rc.event.scratch0, 1);
    EXPECT_EQ(rc.event.scratch1, expected_gold);
    EXPECT_EQ(rc.event.scratch2, 1);
    const EventDialogMenu all = menu(rc);
    EXPECT_TRUE(all.enabled[0] && all.enabled[1] && all.enabled[2] &&
                all.enabled[3]);
}

TEST(WeMeetAgain, EveryPaidOfferBuysExactlyOneScreenlessRelic) {
    {
        RunController rc = raw_event_controller(EventId::WE_MEET_AGAIN);
        rc.run.gold = 200;
        rc.run.potions[1] = 3;
        enter(rc);
        const uint8_t relics = rc.run.relic_count;
        choose(rc, 0);
        EXPECT_EQ(rc.run.potions[1], static_cast<uint16_t>(PotionId::NONE));
        EXPECT_EQ(rc.run.relic_count, relics + 1);
        choose(rc, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
    {
        RunController rc = raw_event_controller(EventId::WE_MEET_AGAIN);
        rc.run.gold = 120;
        enter(rc);
        const int offer = rc.event.scratch1;
        ASSERT_GE(offer, 50);
        ASSERT_LE(offer, 120);
        const uint8_t relics = rc.run.relic_count;
        choose(rc, 1);
        EXPECT_EQ(rc.run.gold, 120 - offer);
        EXPECT_EQ(rc.run.relic_count, relics + 1);
    }
    {
        RunController rc = raw_event_controller(EventId::WE_MEET_AGAIN);
        rc.run.gold = 200;
        set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::ANGER, 0}});
        enter(rc);
        ASSERT_EQ(rc.event.scratch2, 1);
        const uint8_t relics = rc.run.relic_count;
        choose(rc, 2);
        ASSERT_EQ(rc.run.master_deck_count, 1);
        EXPECT_EQ(rc.run.master_deck[0].card_id,
                  static_cast<uint16_t>(CardId::STRIKE));
        EXPECT_EQ(rc.run.relic_count, relics + 1);
    }
    {
        // Refuse: no relic, no cost, no relicRng movement.
        RunController rc = raw_event_controller(EventId::WE_MEET_AGAIN);
        rc.run.gold = 200;
        enter(rc);
        const uint8_t relics = rc.run.relic_count;
        const int relic_rng = rc.run.relic_rng.counter;
        choose(rc, 3);
        EXPECT_EQ(rc.run.gold, 200);
        EXPECT_EQ(rc.run.relic_count, relics);
        EXPECT_EQ(rc.run.relic_rng.counter, relic_rng);
        choose(rc, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
}

// =============================================================================
// The Woman in Blue
// =============================================================================

TEST(TheWomanInBlue, EachPurchaseCostsItsPriceAndRollsThatManyPotions) {
    for (int option = 0; option <= 2; ++option) {
        const int cost = 20 + 10 * option;
        RunController rc = event_controller(EventId::THE_WOMAN_IN_BLUE);
        rc.run.gold = 90;
        rc.run.potion_rng = from_seed(2020);
        RngStream expected_rng = rc.run.potion_rng;
        std::array<PotionId, 3> expected{};
        for (int i = 0; i <= option; ++i) {
            expected[static_cast<std::size_t>(i)] = static_cast<PotionId>(
                random(expected_rng, kPotionPoolSize - 1) + 1);
        }
        ASSERT_EQ(menu(rc).count, 4);

        choose(rc, static_cast<uint8_t>(option));
        EXPECT_EQ(rc.run.gold, 90 - cost);
        ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
        ASSERT_EQ(rc.rewards.count, option + 1);
        EXPECT_EQ(rc.run.potion_rng.counter, expected_rng.counter);
        for (int i = 0; i <= option; ++i) {
            EXPECT_EQ(rc.rewards.items[i].kind,
                      static_cast<uint8_t>(RewardItemKind::POTION));
            EXPECT_EQ(rc.rewards.items[i].id,
                      static_cast<uint16_t>(expected[static_cast<std::size_t>(i)]));
        }
        choose(rc, kChooseProceed);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
}

TEST(TheWomanInBlue, RefusalIsFreeBelowA15AndCostsFivePercentAtA15) {
    {
        RunController rc = event_controller(EventId::THE_WOMAN_IN_BLUE);
        rc.run.max_hp = 80;
        rc.run.hp = 80;
        const int gold = rc.run.gold;
        const int potions = rc.run.potion_rng.counter;
        choose(rc, 3);
        EXPECT_EQ(rc.run.hp, 80);
        EXPECT_EQ(rc.run.gold, gold);
        EXPECT_EQ(rc.run.potion_rng.counter, potions);
        choose(rc, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
    {
        RunController rc = event_controller(EventId::THE_WOMAN_IN_BLUE, 15);
        rc.run.max_hp = 80;
        rc.run.hp = 80;
        choose(rc, 3);
        EXPECT_EQ(rc.run.hp, 76);  // MathUtils.ceil(80 * 0.05f) == 4
    }
    {
        // ceil, not truncation: 71 * 0.05f == 3.55 -> 4.
        RunController rc = event_controller(EventId::THE_WOMAN_IN_BLUE, 15);
        rc.run.max_hp = 71;
        rc.run.hp = 71;
        choose(rc, 3);
        EXPECT_EQ(rc.run.hp, 67);
    }
    {
        // Lethal refusal ends the run: the HP_LOSS still goes through the
        // null-owner door, so Torii does not soften it.
        RunController rc = event_controller(EventId::THE_WOMAN_IN_BLUE, 15);
        rc.run.max_hp = 80;
        rc.run.hp = 3;
        give_relic(rc.run, RelicId::TORII);
        choose(rc, 3);
        EXPECT_EQ(rc.run.hp, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER));
    }
}

}  // namespace
