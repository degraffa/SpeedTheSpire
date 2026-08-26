// S2.31 -- the eight NON-COMBAT Act-2 event bodies (Addict, Back to Basics,
// Beggar, Cursed Tome, Drug Dealer, Forgotten Altar, Ghosts, Nest).
//
// What is pinned here, per the B4.11-B4.13 event pattern:
//   * the OPTION TREE of each event: screen count, per-screen button count, and
//     every greyed-out (isDisabled) button's live predicate;
//   * every A15 branch EXPLICITLY, including the four events that HAVE NONE --
//     an absent branch is an audit result, so it is asserted rather than
//     omitted (a silently-added ascension test would otherwise pass);
//   * ACQUISITION of every payout row this batch pays out: relics
//     (Necronomicon / Enchiridion / Nilry's Codex / Circlet / Bloody Idol /
//     Mutagenic Strength / the rolled screenless relic), cards (J.A.X.,
//     Apparition, Ritual Dagger) and curses (Shame, Decay) -- each landing in
//     the master deck or the relic array, through the door the Java uses;
//   * the RNG accounting: which stream, how many draws, and on which branch.
//
// Provenance for every expectation is the Java cited in
// src/engine/events/city_events_i.cpp's header, each class read in full.

#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>

#include <gtest/gtest.h>

#include "sts/engine/card_pools.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"
#include "sts/registry/event_table.hpp"
#include "sts/registry/game_ids.hpp"

namespace {

using namespace sts::engine;
namespace r = sts::registry;

constexpr int64_t kSeed = 918273645;

// The Act-2 shape: floor 26 is the first ?-room-capable City floor
// (act_floor_base(2) == 17), and every event here is drawn from TheCity's list.
RunController event_controller(EventId id, int ascension = 0) {
    RunController rc = run_begin(kSeed, static_cast<uint8_t>(ascension));
    rc.run.act = 2;
    rc.run.floor = 26;
    rc.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
    rc.room_type = static_cast<uint8_t>(RoomType::Event);
    rc.event = EventDialogState{};
    rc.event.event_id = static_cast<uint16_t>(id);
    rc.rewards = RewardScreen{};
    rc.rewards.open_card_item = kNoOpenCardReward;
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

[[nodiscard]] int count_card(const RunState& rs, CardId id) {
    int n = 0;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        if (rs.master_deck[i].card_id == static_cast<uint16_t>(id)) {
            ++n;
        }
    }
    return n;
}

[[nodiscard]] int relic_index(const RunState& rs, RelicId id) {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (rs.relics[i].relic_id == static_cast<uint16_t>(id)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// =============================================================================
// Registry metadata -- the by-name pin act_event_lists_test deliberately does
// not carry (it asserts the invariant; this asserts the roster).
// =============================================================================

TEST(CityEventsI, RegistryMarksAllThirteenCityRowsImplemented) {
    struct Row {
        r::EventId id;
        bool implemented;
        int screens;
        int a15_changes;
    };
    // TheCity.initializeEventList order (TheCity.java:185-198). All thirteen
    // are implemented as of S2.31 (the eight non-combat bodies this file
    // pins) + S2.32 (the five combat-embed / deck-surgery bodies, pinned in
    // city_events_ii_test.cpp); the roster stays by-name so flipping one
    // without updating it is a failing test rather than a silent scope creep.
    // The S2.32 rows' screens/a15 values are that batch's, carried here at
    // integration (2026-08-09).
    constexpr Row kRows[] = {
        {r::EventId::ADDICT, true, 2, 0},
        {r::EventId::BACK_TO_BASICS, true, 3, 0},
        {r::EventId::BEGGAR, true, 4, 0},
        {r::EventId::COLOSSEUM, true, 3, 0},
        {r::EventId::CURSED_TOME, true, 6, 1},
        {r::EventId::DRUG_DEALER, true, 3, 0},
        {r::EventId::FORGOTTEN_ALTAR, true, 2, 1},
        {r::EventId::GHOSTS, true, 2, 1},
        {r::EventId::MASKED_BANDITS, true, 4, 0},
        {r::EventId::NEST, true, 3, 1},
        {r::EventId::THE_LIBRARY, true, 3, 1},
        {r::EventId::THE_MAUSOLEUM, true, 2, 1},
        {r::EventId::VAMPIRES, true, 2, 0},
    };
    for (const Row& row : kRows) {
        const r::EventDef* def = r::event_def(row.id);
        ASSERT_NE(def, nullptr) << r::event_game_id(row.id);
        EXPECT_TRUE(def->native) << r::event_game_id(row.id);
        EXPECT_EQ(def->implemented, row.implemented)
            << r::event_game_id(row.id);
        EXPECT_EQ(def->screen_count, row.screens) << r::event_game_id(row.id);
        EXPECT_EQ(def->a15_change_count, row.a15_changes)
            << r::event_game_id(row.id);
        // The generated dispatch table is keyed off `implemented`; a row that
        // claims a body must have one linked, and one that does not must not.
        EXPECT_EQ(event_dialog_impl(static_cast<uint16_t>(row.id)) != nullptr,
                  row.implemented)
            << r::event_game_id(row.id);
    }
}

// =============================================================================
// Addict
// =============================================================================

TEST(Addict, BuyRollsTheRelicBeforeSpendingAndTheButtonGreysOutUnderThePrice) {
    RunController rc = event_controller(EventId::ADDICT);
    rc.run.gold = 85;
    const uint8_t relics_before = rc.run.relic_count;

    // The screenless relic the event will roll, derived on a copy of the run so
    // the expectation is independent of the body.
    RunState probe = rc.run;
    const RelicTier tier = return_random_relic_tier(probe);
    RelicSpawnContext ctx{};
    ctx.floor = probe.floor;
    fill_deck_spawn_gates(probe, ctx);
    fill_campfire_relic_count(probe, ctx);
    fill_boss_spawn_gates(probe, ctx);
    const RelicId expected = return_random_screenless_relic(probe, tier, ctx);

    EventDialogMenu m = menu(rc);
    ASSERT_EQ(m.count, 3);
    EXPECT_TRUE(m.enabled[0]);
    choose(rc, 0);
    EXPECT_EQ(rc.run.gold, 0);
    EXPECT_EQ(rc.run.relic_count, relics_before + 1);
    EXPECT_EQ(rc.run.relics[relics_before].relic_id,
              static_cast<uint16_t>(expected));
    EXPECT_EQ(rc.run.relic_rng.counter, probe.relic_rng.counter);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));

    // gold < 85 greys the SAME button out; the two ctor arms differ only in
    // text (Addict.java:29-33).
    RunController poor = event_controller(EventId::ADDICT);
    poor.run.gold = 84;
    m = menu(poor);
    ASSERT_EQ(m.count, 3);
    EXPECT_FALSE(m.enabled[0]);
    EXPECT_TRUE(m.enabled[1]);
    EXPECT_TRUE(m.enabled[2]);
    choose(poor, 0);  // refused by run_advance: nothing moves
    EXPECT_EQ(poor.run.gold, 84);
    EXPECT_EQ(poor.event.screen, 0);
}

TEST(Addict, StealPaysRelicAndShameFreeAndTheOmamoriDoorEatsTheCurseOnly) {
    RunController rc = event_controller(EventId::ADDICT);
    rc.run.gold = 500;
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    const uint8_t relics_before = rc.run.relic_count;
    choose(rc, 1);
    EXPECT_EQ(rc.run.gold, 500);  // the steal costs nothing
    EXPECT_EQ(rc.run.relic_count, relics_before + 1);
    EXPECT_EQ(count_card(rc.run, CardId::SHAME), 1);

    // An owned Omamori eats the Shame and still lets the relic through: the
    // charge is spent by the ShowCardAndObtainEffect ctor (:59), the relic is
    // obtained at :60.
    RunController blocked = event_controller(EventId::ADDICT, 20);
    set_deck(blocked.run, {{CardId::STRIKE, 0}});
    give_relic(blocked.run, RelicId::OMAMORI, 2);
    const uint8_t before = blocked.run.relic_count;
    choose(blocked, 1);
    EXPECT_EQ(count_card(blocked.run, CardId::SHAME), 0);
    EXPECT_EQ(blocked.run.relics[1].counter, 1);
    EXPECT_EQ(blocked.run.relic_count, before + 1);
}

TEST(Addict, LeaveIsStateNeutralAndTheEventHasNoA15Branch) {
    RunController base = event_controller(EventId::ADDICT, 14);
    RunController a15 = event_controller(EventId::ADDICT, 15);
    base.run.gold = 200;
    a15.run.gold = 200;
    // The 85-gold price is a constant (Addict.java:24): the same menu at A14
    // and A20, and the leave option changes nothing.
    EXPECT_EQ(menu(base).count, menu(a15).count);
    EXPECT_EQ(menu(base).enabled[0], menu(a15).enabled[0]);
    const int gold = a15.run.gold;
    const uint8_t relics = a15.run.relic_count;
    choose(a15, 2);
    EXPECT_EQ(a15.run.gold, gold);
    EXPECT_EQ(a15.run.relic_count, relics);
    // ONE click: the Java's default arm calls openMap() at the press itself
    // (Addict.java:65-69), so the rewritten single-button page is installed
    // behind an already-open map and is never clicked. This test's earlier
    // form spent a second choose here, which is exactly the extra page that
    // put the sim a floor behind both Act-2 event captures of the
    // s243_breadth campaigns -- pay and rob DO keep their second click (the
    // two flows above), leave does not.
    EXPECT_EQ(a15.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

// =============================================================================
// Back to Basics
// =============================================================================

TEST(BackToBasics, SimplicityUpgradesOnlyUnupgradedStarterStrikesAndDefends) {
    RunController rc = event_controller(EventId::BACK_TO_BASICS);
    set_deck(rc.run, {{CardId::STRIKE, 0},
                      {CardId::STRIKE, 1},
                      {CardId::DEFEND, 0},
                      {CardId::BASH, 0},
                      {CardId::PERFECTED_STRIKE, 0},
                      {CardId::ASCENDERS_BANE, 0}});
    EventDialogMenu m = menu(rc);
    ASSERT_EQ(m.count, 2);
    EXPECT_TRUE(m.enabled[0]);
    EXPECT_TRUE(m.enabled[1]);

    choose(rc, 1);
    EXPECT_EQ(rc.run.master_deck[0].upgrade, 1);
    EXPECT_EQ(rc.run.master_deck[1].upgrade, 1);  // already +1, canUpgrade false
    EXPECT_EQ(rc.run.master_deck[2].upgrade, 1);
    // Bash is not a starter; Perfected Strike carries CardTags.STRIKE but NOT
    // STARTER_STRIKE; Ascender's Bane is a curse and canUpgrade() refuses it.
    EXPECT_EQ(rc.run.master_deck[3].upgrade, 0);
    EXPECT_EQ(rc.run.master_deck[4].upgrade, 0);
    EXPECT_EQ(rc.run.master_deck[5].upgrade, 0);
    EXPECT_EQ(rc.run.master_deck_count, 6);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(BackToBasics, EleganceOpensAnUnbottledPurgeGridAndSkipsItWhenEmpty) {
    RunController rc = event_controller(EventId::BACK_TO_BASICS);
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::BASH, 0}});
    choose(rc, 0);
    ASSERT_EQ(rc.event.grid_kind, static_cast<uint8_t>(EventGridKind::PURGE));
    choose(rc, 1);
    ASSERT_EQ(rc.run.master_deck_count, 1);
    EXPECT_EQ(rc.run.master_deck[0].card_id,
              static_cast<uint16_t>(CardId::STRIKE));
    EXPECT_EQ(rc.event.grid_kind, static_cast<uint8_t>(EventGridKind::NONE));
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));

    // getGroupWithoutBottledCards(getPurgeableCards()).size() == 0 (:70): the
    // Java advances to COMPLETE with no grid and no removal.
    RunController empty = event_controller(EventId::BACK_TO_BASICS, 20);
    set_deck(empty.run, {{CardId::ASCENDERS_BANE, 0}});
    choose(empty, 0);
    EXPECT_EQ(empty.event.grid_kind, static_cast<uint8_t>(EventGridKind::NONE));
    EXPECT_EQ(empty.run.master_deck_count, 1);
    choose(empty, 0);
    EXPECT_EQ(empty.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(BackToBasics, HasNoAscensionBranchOnEitherOption) {
    for (int ascension : {0, 14, 15, 20}) {
        RunController rc = event_controller(EventId::BACK_TO_BASICS,
                                            ascension);
        set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::DEFEND, 0}});
        const EventDialogMenu m = menu(rc);
        EXPECT_EQ(m.count, 2) << ascension;
        choose(rc, 1);
        EXPECT_EQ(rc.run.master_deck[0].upgrade, 1) << ascension;
        EXPECT_EQ(rc.run.master_deck[1].upgrade, 1) << ascension;
    }
}

// =============================================================================
// Beggar
// =============================================================================

TEST(Beggar, PayingOpensASeparatePageWhoseButtonOpensTheGridAndEndsTheEvent) {
    RunController rc = event_controller(EventId::BEGGAR);
    rc.run.gold = 200;
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::BASH, 0}});

    EventDialogMenu m = menu(rc);
    ASSERT_EQ(m.count, 2);
    EXPECT_TRUE(m.enabled[0]);
    choose(rc, 0);
    EXPECT_EQ(rc.run.gold, 125);
    EXPECT_EQ(rc.event.screen, 1);
    // The pay screen is NOT the grid: one button stands between them
    // (Beggar.java:68, :79-85).
    EXPECT_EQ(rc.event.grid_kind, static_cast<uint8_t>(EventGridKind::NONE));
    m = menu(rc);
    EXPECT_EQ(m.count, 1);

    choose(rc, 0);
    ASSERT_EQ(rc.event.grid_kind, static_cast<uint8_t>(EventGridKind::PURGE));
    choose(rc, 0);
    // update() removes and calls openMap() in the same tick: no proceed page.
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(rc.run.master_deck_count, 1);
    EXPECT_EQ(rc.run.master_deck[0].card_id,
              static_cast<uint16_t>(CardId::BASH));
}

TEST(Beggar, RefusingKeepsTheGoldAndTheGreyOutIsGoldSeventyFive) {
    RunController rc = event_controller(EventId::BEGGAR);
    rc.run.gold = 200;
    choose(rc, 1);
    EXPECT_EQ(rc.run.gold, 200);
    EXPECT_EQ(rc.event.screen, 2);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));

    // Unreachable through getEvent (its own gate is gold >= 75), but the button
    // predicate is a second, independent test and is written as the Java writes
    // it.
    RunController poor = event_controller(EventId::BEGGAR);
    poor.run.gold = 74;
    const EventDialogMenu m = menu(poor);
    EXPECT_FALSE(m.enabled[0]);
    EXPECT_TRUE(m.enabled[1]);
}

TEST(Beggar, HasNoAscensionBranch) {
    for (int ascension : {0, 15}) {
        RunController rc = event_controller(EventId::BEGGAR, ascension);
        rc.run.gold = 100;
        choose(rc, 0);
        EXPECT_EQ(rc.run.gold, 25) << ascension;  // GOLD_COST is 75 always
    }
}

// =============================================================================
// Cursed Tome
// =============================================================================

TEST(CursedTome, PageDamagesAreOneTwoThreeAndTheFinishCostIsTenOrFifteen) {
    for (int ascension : {0, 15}) {
        RunController rc = event_controller(EventId::CURSED_TOME, ascension);
        rc.run.hp = 70;
        EventDialogMenu m = menu(rc);
        ASSERT_EQ(m.count, 2);  // read / leave

        choose(rc, 0);
        EXPECT_EQ(rc.run.hp, 70) << ascension;  // opening the book is free
        EXPECT_EQ(menu(rc).count, 1) << ascension;
        choose(rc, 0);
        EXPECT_EQ(rc.run.hp, 69) << ascension;
        choose(rc, 0);
        EXPECT_EQ(rc.run.hp, 67) << ascension;
        choose(rc, 0);
        EXPECT_EQ(rc.run.hp, 64) << ascension;
        m = menu(rc);
        ASSERT_EQ(m.count, 2) << ascension;  // finish / stop

        choose(rc, 0);
        EXPECT_EQ(rc.run.hp, ascension >= 15 ? 49 : 54) << ascension;
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD))
            << ascension;
    }
}

TEST(CursedTome, StoppingOnTheLastPageCostsThreeAndPaysNothing) {
    RunController rc = event_controller(EventId::CURSED_TOME, 20);
    rc.run.hp = 70;
    const uint8_t relics = rc.run.relic_count;
    const int rng = rc.combat.misc_rng.counter;
    choose(rc, 0);
    choose(rc, 0);
    choose(rc, 0);
    choose(rc, 0);
    choose(rc, 1);  // stop reading
    EXPECT_EQ(rc.run.hp, 61);  // 1 + 2 + 3 + 3
    EXPECT_EQ(rc.run.relic_count, relics);
    EXPECT_EQ(rc.combat.misc_rng.counter, rng);  // no book -> no draw
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::EVENT_DIALOG));
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));

    // Ignoring the tome outright is free and takes no draw.
    RunController ignore = event_controller(EventId::CURSED_TOME);
    const int hp = ignore.run.hp;
    choose(ignore, 1);
    EXPECT_EQ(ignore.run.hp, hp);
    choose(ignore, 0);
    EXPECT_EQ(ignore.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

// randomBook (CursedTome.java:142-157) with each of the four possible list
// shapes, and the reward-screen claim that actually equips the book.
TEST(CursedTome, TheBookIsAMiscRngPickOverTheUnownedThreeWithACircletFallback) {
    struct Case {
        RelicId owned_a;
        RelicId owned_b;
        RelicId expected;  // the single survivor, so the draw is degenerate
    };
    const Case kCases[] = {
        {RelicId::ENCHIRIDION, RelicId::NILRYS_CODEX, RelicId::NECRONOMICON},
        {RelicId::NECRONOMICON, RelicId::NILRYS_CODEX, RelicId::ENCHIRIDION},
        {RelicId::NECRONOMICON, RelicId::ENCHIRIDION, RelicId::NILRYS_CODEX},
    };
    for (const Case& c : kCases) {
        RunController rc = event_controller(EventId::CURSED_TOME);
        rc.run.hp = 70;
        give_relic(rc.run, c.owned_a);
        give_relic(rc.run, c.owned_b);
        // run_begin leaves the floor stream mid-flight, so every draw count
        // below is a DELTA rather than an absolute counter.
        const int rng_before = rc.combat.misc_rng.counter;
        for (int i = 0; i < 4; ++i) {
            choose(rc, 0);
        }
        choose(rc, 0);
        ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
        ASSERT_EQ(rc.rewards.count, 1);
        EXPECT_EQ(rc.rewards.items[0].kind,
                  static_cast<uint8_t>(RewardItemKind::RELIC));
        EXPECT_EQ(rc.rewards.items[0].id, static_cast<uint16_t>(c.expected));
        // A one-entry list still spends the random(0) draw.
        EXPECT_EQ(rc.combat.misc_rng.counter, rng_before + 1);
        // The reward screen is the exit, and claiming the row equips the book.
        choose(rc, 0);
        EXPECT_GE(relic_index(rc.run, c.expected), 0);
    }

    // All three owned -> Circlet, and NO draw (the list is never indexed).
    RunController full = event_controller(EventId::CURSED_TOME, 15);
    full.run.hp = 70;
    give_relic(full.run, RelicId::NECRONOMICON);
    give_relic(full.run, RelicId::ENCHIRIDION);
    give_relic(full.run, RelicId::NILRYS_CODEX);
    const int full_rng_before = full.combat.misc_rng.counter;
    for (int i = 0; i < 4; ++i) {
        choose(full, 0);
    }
    choose(full, 0);
    ASSERT_EQ(full.rewards.count, 1);
    EXPECT_EQ(full.rewards.items[0].id, static_cast<uint16_t>(RelicId::CIRCLET));
    EXPECT_EQ(full.combat.misc_rng.counter, full_rng_before);
}

TEST(CursedTome, LethalPageDamageEndsTheRunAndNeverOpensTheRewardScreen) {
    RunController rc = event_controller(EventId::CURSED_TOME, 15);
    rc.run.hp = 12;
    choose(rc, 0);
    choose(rc, 0);
    choose(rc, 0);
    choose(rc, 0);
    const int rng = rc.combat.misc_rng.counter;
    choose(rc, 0);  // 15 damage into 6 HP
    EXPECT_EQ(rc.run.hp, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER));
    // The Java's damage() returns and randomBook still runs -- so the draw is
    // spent even though no screen opens (the Scrap Ooze precedent).
    EXPECT_EQ(rc.combat.misc_rng.counter, rng + 1);
    EXPECT_EQ(rc.rewards.count, 0);
}

// =============================================================================
// Drug Dealer
// =============================================================================

TEST(DrugDealer, JaxAndMutagenicStrengthAreTheTwoDrawFreePayouts) {
    RunController jax = event_controller(EventId::DRUG_DEALER);
    set_deck(jax.run, {{CardId::STRIKE, 0}});
    const int rng = jax.combat.misc_rng.counter;
    choose(jax, 0);
    EXPECT_EQ(count_card(jax.run, CardId::JAX), 1);
    EXPECT_EQ(jax.combat.misc_rng.counter, rng);
    choose(jax, 0);
    EXPECT_EQ(jax.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));

    RunController mut = event_controller(EventId::DRUG_DEALER);
    choose(mut, 2);
    EXPECT_GE(relic_index(mut.run, RelicId::MUTAGENIC_STRENGTH), 0);

    // hasRelic("MutagenicStrength") -> Circlet (DrugDealer.java:73-79).
    RunController dupe = event_controller(EventId::DRUG_DEALER, 20);
    give_relic(dupe.run, RelicId::MUTAGENIC_STRENGTH);
    choose(dupe, 2);
    EXPECT_GE(relic_index(dupe.run, RelicId::CIRCLET), 0);
}

TEST(DrugDealer, TransformTakesTwoPicksOffARawPurgeableGridThatKeepsBottles) {
    RunController rc = event_controller(EventId::DRUG_DEALER);
    set_deck(rc.run, {{CardId::ANGER, 0}, {CardId::DEFEND, 0},
                      {CardId::BASH, 0}});
    rc.combat.misc_rng = from_seed(4321);
    RngStream expected_rng = rc.combat.misc_rng;
    const CardId first = transform_card(expected_rng, CardId::ANGER);
    const CardId second = transform_card(expected_rng, CardId::BASH);

    EventDialogMenu m = menu(rc);
    ASSERT_EQ(m.count, 3);
    EXPECT_TRUE(m.enabled[1]);
    choose(rc, 1);
    ASSERT_EQ(rc.event.grid_kind,
              static_cast<uint8_t>(EventGridKind::TRANSFORMABLE_ANY));

    choose(rc, 0);  // Anger
    // One pick is not two: nothing has moved yet (DrugDealer.update fires only
    // at selectedCards.size() == 2).
    EXPECT_EQ(rc.run.master_deck_count, 3);
    EXPECT_EQ(rc.combat.misc_rng.counter, 0);
    choose(rc, 0);  // the SAME card again -- not a pair
    EXPECT_EQ(rc.run.master_deck_count, 3);

    choose(rc, 2);  // Bash
    ASSERT_EQ(rc.run.master_deck_count, 3);
    EXPECT_EQ(rc.run.master_deck[0].card_id,
              static_cast<uint16_t>(CardId::DEFEND));
    EXPECT_EQ(rc.run.master_deck[1].card_id, static_cast<uint16_t>(first));
    EXPECT_EQ(rc.run.master_deck[2].card_id, static_cast<uint16_t>(second));
    EXPECT_EQ(rc.combat.misc_rng.counter, expected_rng.counter);
    EXPECT_EQ(rc.event.grid_kind, static_cast<uint8_t>(EventGridKind::NONE));
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(DrugDealer, TheGridIsTheOneEventGridThatDoesNotExcludeBottledCards) {
    RunController rc = event_controller(EventId::DRUG_DEALER);
    set_deck(rc.run, {{CardId::ANGER, 0}, {CardId::DEFEND, 0}});
    rc.run.master_deck[0].flags = static_cast<uint16_t>(
        rc.run.master_deck[0].flags |
        master_bottle_bit(MasterBottleKind::FLAME));
    choose(rc, 1);
    ASSERT_EQ(rc.event.grid_kind,
              static_cast<uint8_t>(EventGridKind::TRANSFORMABLE_ANY));
    EXPECT_TRUE(event_grid_card_legal(rc.run, rc.event, 0));

    // The same card under the ordinary event grids is NOT selectable -- the
    // clause DrugDealer.java:128 is missing.
    EventDialogState probe{};
    open_event_grid(probe, EventGridKind::TRANSFORMABLE);
    EXPECT_FALSE(event_grid_card_legal(rc.run, probe, 0));
    open_event_grid(probe, EventGridKind::PURGE);
    EXPECT_FALSE(event_grid_card_legal(rc.run, probe, 0));
}

TEST(DrugDealer, TransformIsGreyedOutUnderTwoPurgeableCardsAndHasNoA15) {
    RunController rc = event_controller(EventId::DRUG_DEALER, 20);
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::ASCENDERS_BANE, 0}});
    EventDialogMenu m = menu(rc);
    ASSERT_EQ(m.count, 3);
    EXPECT_TRUE(m.enabled[0]);
    EXPECT_FALSE(m.enabled[1]);  // one purgeable card only
    EXPECT_TRUE(m.enabled[2]);
    choose(rc, 1);
    EXPECT_EQ(rc.event.grid_kind, static_cast<uint8_t>(EventGridKind::NONE));
    EXPECT_EQ(rc.event.screen, 0);

    // No ascension read exists in the file: same menu, same payouts.
    for (int ascension : {0, 15}) {
        RunController a = event_controller(EventId::DRUG_DEALER, ascension);
        set_deck(a.run, {{CardId::STRIKE, 0}, {CardId::DEFEND, 0}});
        const EventDialogMenu am = menu(a);
        EXPECT_EQ(am.count, 3) << ascension;
        EXPECT_TRUE(am.enabled[1]) << ascension;
        choose(a, 0);
        EXPECT_EQ(count_card(a.run, CardId::JAX), 1) << ascension;
    }
}

// =============================================================================
// Forgotten Altar
// =============================================================================

TEST(ForgottenAltar, TheIdolSwapKeepsTheRelicSlotAndSkipsOnEquip) {
    RunController rc = event_controller(EventId::FORGOTTEN_ALTAR);
    give_relic(rc.run, RelicId::GOLDEN_IDOL);
    give_relic(rc.run, RelicId::TINY_CHEST, 3);
    const uint8_t count = rc.run.relic_count;
    const int slot = relic_index(rc.run, RelicId::GOLDEN_IDOL);
    ASSERT_EQ(slot, 1);

    EventDialogMenu m = menu(rc);
    ASSERT_EQ(m.count, 3);
    EXPECT_TRUE(m.enabled[0]);
    choose(rc, 0);
    EXPECT_EQ(rc.run.relic_count, count);  // a REPLACEMENT, not an append
    EXPECT_EQ(relic_index(rc.run, RelicId::GOLDEN_IDOL), -1);
    EXPECT_EQ(relic_index(rc.run, RelicId::BLOODY_IDOL), slot);
    // Trap 8: everything after the swapped slot keeps its acquisition index.
    EXPECT_EQ(relic_index(rc.run, RelicId::TINY_CHEST), 2);
    EXPECT_EQ(rc.run.relics[2].counter, 3);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(ForgottenAltar, HoldingABloodyIdolPaysACircletAndKEEPSTheGoldenIdol) {
    RunController rc = event_controller(EventId::FORGOTTEN_ALTAR);
    give_relic(rc.run, RelicId::GOLDEN_IDOL);
    give_relic(rc.run, RelicId::BLOODY_IDOL);
    choose(rc, 0);
    // ForgottenAltar.java:106-109 -- the surprising arm: the Golden Idol stays.
    EXPECT_GE(relic_index(rc.run, RelicId::GOLDEN_IDOL), 0);
    EXPECT_GE(relic_index(rc.run, RelicId::BLOODY_IDOL), 0);
    EXPECT_GE(relic_index(rc.run, RelicId::CIRCLET), 0);
}

TEST(ForgottenAltar, GiveIsGreyedOutWithoutAGoldenIdol) {
    RunController rc = event_controller(EventId::FORGOTTEN_ALTAR, 20);
    const EventDialogMenu m = menu(rc);
    ASSERT_EQ(m.count, 3);
    EXPECT_FALSE(m.enabled[0]);
    EXPECT_TRUE(m.enabled[1]);
    EXPECT_TRUE(m.enabled[2]);
    const uint8_t relics = rc.run.relic_count;
    choose(rc, 0);
    EXPECT_EQ(rc.run.relic_count, relics);
    EXPECT_EQ(rc.event.screen, 0);
}

TEST(ForgottenAltar, BloodOfferRaisesMaxHpFirstAndA15MovesThePercentOnly) {
    // Below A15 the cost is round(entryMaxHp * 0.25). increaseMaxHp(5) heals 5
    // FIRST, so a full-HP player is at entry+5 before the hit. The entry max HP
    // is re-derived rather than hard-coded because run_begin's own A6 and A14
    // max-HP modifiers are not this event's business.
    RunController base = event_controller(EventId::FORGOTTEN_ALTAR, 14);
    const int base_max = base.run.max_hp;
    base.run.hp = static_cast<int16_t>(base_max);
    const int base_loss = static_cast<int>(
        mathutils_round(static_cast<float>(base_max) * 0.25f));
    choose(base, 1);
    EXPECT_EQ(base.run.max_hp, base_max + 5);
    EXPECT_EQ(base.run.hp, base_max + 5 - base_loss);

    // A15 moves the percent to 0.35 and nothing else.
    RunController a15 = event_controller(EventId::FORGOTTEN_ALTAR, 15);
    const int entry_max = a15.run.max_hp;
    a15.run.hp = static_cast<int16_t>(entry_max);
    const int loss = static_cast<int>(
        mathutils_round(static_cast<float>(entry_max) * 0.35f));
    choose(a15, 1);
    EXPECT_EQ(a15.run.max_hp, entry_max + 5);
    EXPECT_EQ(a15.run.hp, entry_max + 5 - loss);

    // The hpLoss is frozen from the ENTRY max HP (ForgottenAltar.java:50 runs
    // in the ctor), so the +5 does not enlarge it -- 0.35 * (entry_max + 5) is
    // strictly larger, which is what makes the ordering observable at all.
    EXPECT_LT(loss, static_cast<int>(mathutils_round(
                        static_cast<float>(entry_max + 5) * 0.35f)));
}

TEST(ForgottenAltar, SmashingTheAltarTakesTheOrdinaryCurseDoor) {
    RunController rc = event_controller(EventId::FORGOTTEN_ALTAR);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    choose(rc, 2);
    EXPECT_EQ(count_card(rc.run, CardId::DECAY), 1);

    RunController blocked = event_controller(EventId::FORGOTTEN_ALTAR, 20);
    set_deck(blocked.run, {{CardId::STRIKE, 0}});
    give_relic(blocked.run, RelicId::OMAMORI, 2);
    choose(blocked, 2);
    EXPECT_EQ(count_card(blocked.run, CardId::DECAY), 0);
    EXPECT_EQ(blocked.run.relics[1].counter, 1);
}

// =============================================================================
// Ghosts
// =============================================================================

TEST(Ghosts, A15MovesTheApparitionCountAndNotTheMaxHpPrice) {
    // The trap: the ctor's ascension read only picks the option STRING; the
    // price is ceil(maxHealth * 0.5f) at every ascension and the count is what
    // drops, 5 -> 3 (Ghosts.java:86-89).
    RunController base = event_controller(EventId::GHOSTS, 14);
    set_deck(base.run, {{CardId::STRIKE, 0}});
    const int base_max = base.run.max_hp;
    base.run.hp = static_cast<int16_t>(base_max);
    choose(base, 0);
    EXPECT_EQ(base.run.max_hp, base_max - (base_max + 1) / 2);
    EXPECT_EQ(count_card(base.run, CardId::APPARITION), 5);

    RunController a15 = event_controller(EventId::GHOSTS, 15);
    set_deck(a15.run, {{CardId::STRIKE, 0}});
    const int a15_max = a15.run.max_hp;
    a15.run.hp = static_cast<int16_t>(a15_max);
    choose(a15, 0);
    EXPECT_EQ(a15.run.max_hp, a15_max - (a15_max + 1) / 2);
    EXPECT_EQ(count_card(a15.run, CardId::APPARITION), 3);
    choose(a15, 0);
    EXPECT_EQ(a15.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(Ghosts, TheCostIsClampedToMaxHpMinusOneAndTheOfferIsNeverGreyedOut) {
    RunController rc = event_controller(EventId::GHOSTS);
    rc.run.max_hp = 1;
    rc.run.hp = 1;
    const EventDialogImpl* impl = event_dialog_impl(rc.event.event_id);
    ASSERT_NE(impl, nullptr);
    impl->on_enter(rc, rc.event);  // re-freeze the cost at the new max HP
    // ceil(1 * 0.5f) == 1 >= maxHealth, so the clamp makes it 0.
    EXPECT_EQ(rc.event.scratch0, 0);
    const EventDialogMenu m = menu(rc);
    ASSERT_EQ(m.count, 2);
    EXPECT_TRUE(m.enabled[0]);
    EXPECT_TRUE(m.enabled[1]);
    choose(rc, 0);
    EXPECT_EQ(rc.run.max_hp, 1);
    EXPECT_EQ(rc.run.hp, 1);  // never lethal
    EXPECT_EQ(count_card(rc.run, CardId::APPARITION), 5);
}

TEST(Ghosts, LeavingCostsNothing) {
    RunController rc = event_controller(EventId::GHOSTS, 20);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    const int max_hp = rc.run.max_hp;
    choose(rc, 1);
    EXPECT_EQ(rc.run.max_hp, max_hp);
    EXPECT_EQ(rc.run.master_deck_count, 1);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

// =============================================================================
// Nest
// =============================================================================

TEST(Nest, TheEntryScreenHasOneButtonAndTheOfferIsGoldThenDagger) {
    RunController rc = event_controller(EventId::NEST);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    EventDialogMenu m = menu(rc);
    ASSERT_EQ(m.count, 1);  // Nest.java:34 -- exactly one ctor option
    choose(rc, 0);
    m = menu(rc);
    ASSERT_EQ(m.count, 2);
    EXPECT_TRUE(m.enabled[0]);
    EXPECT_TRUE(m.enabled[1]);

    const int gold = rc.run.gold;
    choose(rc, 0);  // index 0 is the GOLD offer (updateDialogOption, :45)
    EXPECT_EQ(rc.run.gold, gold + 99);
    EXPECT_EQ(count_card(rc.run, CardId::RITUAL_DAGGER), 0);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(Nest, A15HalvesTheGoldAndTheDaggerCostsASixDamageNullOwnerHit) {
    RunController a15 = event_controller(EventId::NEST, 15);
    const int gold = a15.run.gold;
    choose(a15, 0);
    choose(a15, 0);
    EXPECT_EQ(a15.run.gold, gold + 50);

    RunController dagger = event_controller(EventId::NEST);
    set_deck(dagger.run, {{CardId::STRIKE, 0}});
    dagger.run.hp = 40;
    choose(dagger, 0);
    choose(dagger, 1);
    EXPECT_EQ(dagger.run.hp, 34);
    EXPECT_EQ(count_card(dagger.run, CardId::RITUAL_DAGGER), 1);
    choose(dagger, 0);
    EXPECT_EQ(dagger.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));

    // DamageInfo(null, 6) is a NORMAL hit with a NULL owner, so Torii's
    // onAttacked -- which the Java only reaches when info.owner != null -- does
    // NOT clamp it to 1, while Tungsten Rod's owner-independent onLoseHpLast
    // does take one off.
    RunController torii = event_controller(EventId::NEST, 20);
    give_relic(torii.run, RelicId::TORII);
    torii.run.hp = 40;
    choose(torii, 0);
    choose(torii, 1);
    EXPECT_EQ(torii.run.hp, 34);

    RunController rod = event_controller(EventId::NEST);
    give_relic(rod.run, RelicId::TUNGSTEN_ROD);
    rod.run.hp = 40;
    choose(rod, 0);
    choose(rod, 1);
    EXPECT_EQ(rod.run.hp, 35);
}

TEST(Nest, TheDaggerStillLandsWhenTheSixDamageIsLethal) {
    RunController rc = event_controller(EventId::NEST);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    rc.run.hp = 4;
    choose(rc, 0);
    choose(rc, 1);
    EXPECT_EQ(rc.run.hp, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER));
    // The queued ShowCardAndObtainEffect is appended by the same statement
    // block the Java runs after damage() returns.
    EXPECT_EQ(count_card(rc.run, CardId::RITUAL_DAGGER), 1);
}

}  // namespace
