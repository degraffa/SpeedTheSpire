// The six Exordium shrine bodies (registry/events.yaml ids 12-17,
// src/engine/events/shrines.cpp).
//
// Coverage:
//   * tier-2 per row: the generated EventDef metadata (native/implemented,
//     screen and A15 change counts) and a linked dispatch body.
//   * directed dialog scripts through the PUBLIC API only (legal_actions /
//     advance), one per option of every screen, including the leave paths and
//     the FINISHED -> MAP_CHOICE exit.
//   * RNG attribution, named per draw: Transmorgrifier's single miscRng
//     transform draw; the Wheel's single miscRng.random(0, 5) spin; Match and
//     Keep's THREE-stream deal (cardRng pool + curse draws, shuffleRng for the
//     colorless pick, miscRng for the board shuffle) against a bit-for-bit
//     independent hand-derivation.
//   * A15 variants: Golden Shrine's 100 -> 50 prayer, the Wheel's 0.10 -> 0.15
//     HP loss, Match and Keep's colorless -> second-curse deal.
//   * the shared obtain / gold doors: Omamori eats the Regret and the Decay,
//     Ectoplasm suppresses shrine gold.

#include "sts/engine/event_framework.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>

#include <gtest/gtest.h>

#include "sts/engine/card_pools.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"
#include "sts/registry/event_table.hpp"

namespace {

using namespace sts::engine;
namespace reg = sts::registry;

constexpr int64_t kSeed = 5150301;

// A controller parked in EVENT_DIALOG on `id`, with on_enter NOT yet run so a
// test can seed the exact streams the body will draw from.
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

RunController event_controller(EventId id, int ascension = 0) {
    RunController rc = raw_event_controller(id, ascension);
    const EventDialogImpl* impl = event_dialog_impl(rc.event.event_id);
    EXPECT_NE(impl, nullptr);
    impl->on_enter(rc, rc.event);
    return rc;
}

void enter(RunController& rc) {
    const EventDialogImpl* impl = event_dialog_impl(rc.event.event_id);
    ASSERT_NE(impl, nullptr);
    impl->on_enter(rc, rc.event);
}

void choose(RunController& rc, uint8_t option) {
    const Action action = make_action(ActionVerb::CHOOSE, option);
    StepResult result{};
    advance(std::span<RunController>(&rc, 1),
            std::span<const Action>(&action, 1),
            std::span<StepResult>(&result, 1));
}

RunActionMask mask_of(const RunController& rc) {
    RunActionMask out{};
    legal_actions(rc, out);
    return out;
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

// A seed whose first random(0, 5) is `wanted` -- the Wheel's spin.
int64_t seed_with_spin(int wanted) {
    for (int64_t seed = 1; seed < 200000; ++seed) {
        RngStream rng = from_seed(seed);
        if (random(rng, 0, 5) == wanted) {
            return seed;
        }
    }
    return 0;
}

// =============================================================================
// Tier-2: the generated registry metadata behind every shrine row
// =============================================================================

TEST(Shrines, EveryShrineRowIsImplementedNativeAndDispatched) {
    struct Row {
        EventId id;
        uint8_t screens;
        uint8_t a15_changes;
    };
    constexpr Row kRows[] = {
        {EventId::MATCH_AND_KEEP, 4, 1}, {EventId::GOLDEN_SHRINE, 2, 1},
        {EventId::TRANSMORGRIFIER, 3, 0}, {EventId::PURIFIER, 3, 0},
        {EventId::UPGRADE_SHRINE, 3, 0}, {EventId::WHEEL_OF_CHANGE, 3, 1},
    };
    for (const Row& row : kRows) {
        const reg::EventDef* def = reg::event_def(row.id);
        ASSERT_NE(def, nullptr);
        EXPECT_TRUE(def->native);
        EXPECT_TRUE(def->implemented);
        EXPECT_EQ(def->screen_count, row.screens);
        EXPECT_EQ(def->a15_change_count, row.a15_changes);
        EXPECT_NE(event_dialog_impl(static_cast<uint16_t>(row.id)), nullptr);
    }
}

// =============================================================================
// Golden Shrine
// =============================================================================

TEST(GoldenShrine, PrayIsTieredAndDesecrateGainsGoldBeforeTheCurse) {
    {
        RunController rc = event_controller(EventId::GOLDEN_SHRINE);
        const int gold = rc.run.gold;
        const EventDialogMenu m = menu(rc);
        ASSERT_EQ(m.count, 3);
        EXPECT_TRUE(m.enabled[0] && m.enabled[1] && m.enabled[2]);
        choose(rc, 0);
        EXPECT_EQ(rc.run.gold, gold + 100);
        EXPECT_EQ(rc.event.screen, 1);
        choose(rc, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
    {
        RunController rc = event_controller(EventId::GOLDEN_SHRINE, 15);
        const int gold = rc.run.gold;
        choose(rc, 0);
        EXPECT_EQ(rc.run.gold, gold + 50);  // A_2_GOLD_AMT
    }
    {
        RunController rc = event_controller(EventId::GOLDEN_SHRINE);
        set_deck(rc.run, {{CardId::STRIKE, 0}});
        const int gold = rc.run.gold;
        choose(rc, 1);
        EXPECT_EQ(rc.run.gold, gold + 275);
        ASSERT_EQ(rc.run.master_deck_count, 2);
        EXPECT_EQ(rc.run.master_deck[1].card_id,
                  static_cast<uint16_t>(CardId::REGRET));
    }
    {
        // Leave: state-neutral, then the proceed page.
        RunController rc = event_controller(EventId::GOLDEN_SHRINE, 20);
        const int gold = rc.run.gold;
        const uint16_t deck = rc.run.master_deck_count;
        choose(rc, 2);
        EXPECT_EQ(rc.run.gold, gold);
        EXPECT_EQ(rc.run.master_deck_count, deck);
        choose(rc, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
}

TEST(GoldenShrine, DesecrateRoutesThroughTheOmamoriAndEctoplasmDoors) {
    RunController rc = event_controller(EventId::GOLDEN_SHRINE);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    give_relic(rc.run, RelicId::OMAMORI, 2);
    give_relic(rc.run, RelicId::ECTOPLASM);
    const int gold = rc.run.gold;
    choose(rc, 1);
    EXPECT_EQ(rc.run.master_deck_count, 1);   // Regret absorbed
    EXPECT_EQ(rc.run.relics[1].counter, 1);   // one charge spent
    EXPECT_EQ(rc.run.gold, gold);             // Ectoplasm suppresses the payout
}

// =============================================================================
// Transmorgrifier
// =============================================================================

TEST(Transmorgrifier, TransformSpendsExactlyOneMiscRngDrawOnTheSameColorPool) {
    RunController rc = event_controller(EventId::TRANSMORGRIFIER);
    set_deck(rc.run, {{CardId::ANGER, 0}, {CardId::DEFEND, 0}});
    rc.combat.misc_rng = from_seed(31337);
    RngStream expected_rng = rc.combat.misc_rng;
    // Transmorgrifier.update (Transmogrifier.java:44-55) is Living
    // Wall's pair: removeCard, then transformCard(..., miscRng). The
    // expectation is therefore the shared `transform_card` (card_pools.hpp) off
    // the event's own stream; the LIST's order is pinned separately and without
    // a seed by
    // CardPoolLibraryOrder.TransformCardListIsCommonsThenBothSrcPoolsBackwards.
    const CardId expected = transform_card(expected_rng, CardId::ANGER);
    const int before = rc.combat.misc_rng.counter;

    choose(rc, 0);
    EXPECT_EQ(rc.event.grid_kind,
              static_cast<uint8_t>(EventGridKind::TRANSFORMABLE));
    // While a grid is open the legal actions are master-deck rows, never
    // dialog options.
    const RunActionMask grid_mask = mask_of(rc);
    EXPECT_TRUE(grid_mask.can_choose_master_deck[0]);
    EXPECT_FALSE(grid_mask.can_choose_event_option[0]);

    choose(rc, 0);
    EXPECT_EQ(rc.combat.misc_rng.counter, before + 1);
    EXPECT_EQ(rc.combat.misc_rng.counter, expected_rng.counter);
    ASSERT_EQ(rc.run.master_deck_count, 2);
    EXPECT_EQ(rc.run.master_deck[0].card_id,
              static_cast<uint16_t>(CardId::DEFEND));
    EXPECT_EQ(rc.run.master_deck[1].card_id, static_cast<uint16_t>(expected));
    EXPECT_EQ(rc.event.grid_kind, static_cast<uint8_t>(EventGridKind::NONE));
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(Transmorgrifier, LeaveAndAnUntransformableDeckAreBothStreamFree) {
    {
        RunController rc = event_controller(EventId::TRANSMORGRIFIER);
        const int before = rc.combat.misc_rng.counter;
        choose(rc, 1);
        EXPECT_EQ(rc.event.grid_kind,
                  static_cast<uint8_t>(EventGridKind::NONE));
        EXPECT_EQ(rc.combat.misc_rng.counter, before);
        choose(rc, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
    {
        // Ascender's Bane is the only unpurgeable Act-1 row, so a deck of just
        // it has an empty transform group: the dialog still advances.
        RunController rc = event_controller(EventId::TRANSMORGRIFIER, 20);
        set_deck(rc.run, {{CardId::ASCENDERS_BANE, 0}});
        const int before = rc.combat.misc_rng.counter;
        choose(rc, 0);
        EXPECT_EQ(rc.event.grid_kind,
                  static_cast<uint8_t>(EventGridKind::NONE));
        EXPECT_EQ(rc.combat.misc_rng.counter, before);
        EXPECT_EQ(rc.run.master_deck_count, 1);
        choose(rc, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
}

// =============================================================================
// Purifier / Upgrade Shrine
// =============================================================================

TEST(Purifier, PurgeUsesTheSharedGridAndConsumesNoStream) {
    RunController rc = event_controller(EventId::PURIFIER);
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::PARASITE, 0},
                      {CardId::DEFEND, 0}});
    const int before = rc.combat.misc_rng.counter;
    choose(rc, 0);
    EXPECT_EQ(rc.event.grid_kind, static_cast<uint8_t>(EventGridKind::PURGE));
    choose(rc, 1);
    ASSERT_EQ(rc.run.master_deck_count, 2);
    EXPECT_EQ(rc.run.master_deck[1].card_id,
              static_cast<uint16_t>(CardId::DEFEND));
    EXPECT_EQ(rc.run.max_hp, 77);  // Parasite.onRemoveFromMasterDeck
    EXPECT_EQ(rc.combat.misc_rng.counter, before);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));

    RunController leave = event_controller(EventId::PURIFIER);
    const uint16_t deck = leave.run.master_deck_count;
    choose(leave, 1);
    EXPECT_EQ(leave.run.master_deck_count, deck);
    choose(leave, 0);
    EXPECT_EQ(leave.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(UpgradeShrine, OptionZeroIsGreyedOutWithNothingLeftToUpgrade) {
    {
        RunController rc = event_controller(EventId::UPGRADE_SHRINE);
        set_deck(rc.run, {{CardId::STRIKE, 1}, {CardId::DEFEND, 0}});
        const EventDialogMenu m = menu(rc);
        ASSERT_EQ(m.count, 2);
        EXPECT_TRUE(m.enabled[0]);
        choose(rc, 0);
        EXPECT_EQ(rc.event.grid_kind,
                  static_cast<uint8_t>(EventGridKind::UPGRADE));
        const RunActionMask grid_mask = mask_of(rc);
        EXPECT_FALSE(grid_mask.can_choose_master_deck[0]);  // already upgraded
        EXPECT_TRUE(grid_mask.can_choose_master_deck[1]);
        choose(rc, 1);
        EXPECT_EQ(rc.run.master_deck[1].upgrade, 1);
        choose(rc, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
    {
        RunController rc = event_controller(EventId::UPGRADE_SHRINE, 20);
        set_deck(rc.run, {{CardId::STRIKE, 1}, {CardId::DOUBT, 0}});
        const EventDialogMenu m = menu(rc);
        ASSERT_EQ(m.count, 2);
        EXPECT_FALSE(m.enabled[0]);
        EXPECT_TRUE(m.enabled[1]);
        const RunActionMask mask = mask_of(rc);
        EXPECT_FALSE(mask.can_choose_event_option[0]);
        EXPECT_TRUE(mask.can_choose_event_option[1]);
    }
}

// =============================================================================
// Wheel of Change
// =============================================================================

TEST(WheelOfChange, SpinIsOneMiscRngDrawAndGoldPaysBeforeAcknowledgement) {
    const int64_t seed = seed_with_spin(0);
    ASSERT_NE(seed, 0);
    RunController rc = event_controller(EventId::WHEEL_OF_CHANGE);
    rc.combat.misc_rng = from_seed(seed);
    const int before = rc.combat.misc_rng.counter;
    const int gold = rc.run.gold;

    choose(rc, 0);
    EXPECT_EQ(rc.combat.misc_rng.counter, before + 1);
    EXPECT_EQ(rc.event.scratch0, 0);
    EXPECT_EQ(rc.run.gold, gold) << "[Play] only chooses the wheel result";
    choose(rc, 0);  // physical spin -> preApplyResult
    EXPECT_EQ(rc.run.gold, gold + 100);  // Exordium goldAmount
    choose(rc, 0);                       // [Prize!] -> applyResult only logs
    EXPECT_EQ(rc.run.gold, gold + 100);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(WheelOfChange, RelicResultOpensAScreenlessRelicRewardScreen) {
    const int64_t seed = seed_with_spin(1);
    ASSERT_NE(seed, 0);
    RunController rc = event_controller(EventId::WHEEL_OF_CHANGE);
    rc.combat.misc_rng = from_seed(seed);
    const uint8_t relics_before = rc.run.relic_count;

    choose(rc, 0);
    EXPECT_EQ(rc.event.scratch0, 1);
    const int relic_rng_before = rc.run.relic_rng.counter;
    const int common = static_cast<int>(RelicPool::COMMON);
    const uint8_t common_before = rc.run.relic_pool_count[common];
    choose(rc, 0);  // physical spin reveals the relic result
    EXPECT_EQ(rc.run.relic_rng.counter, relic_rng_before)
        << "the tier draw waits for [Prize!] acknowledgement";
    EXPECT_EQ(rc.run.relic_pool_count[common], common_before)
        << "no relic pool moves while the prize is merely visible";
    choose(rc, 0);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    ASSERT_EQ(rc.rewards.count, 1);
    EXPECT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::RELIC));
    // Nothing is owned until the row is claimed from the screen.
    EXPECT_EQ(rc.run.relic_count, relics_before);
    EXPECT_EQ(rc.event.event_id, 0);

    choose(rc, 0);
    EXPECT_EQ(rc.run.relic_count, relics_before + 1);
    choose(rc, kChooseProceed);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(WheelOfChange, HealCurseAndCardRemovalResults) {
    {
        const int64_t seed = seed_with_spin(2);
        ASSERT_NE(seed, 0);
        RunController rc = event_controller(EventId::WHEEL_OF_CHANGE);
        rc.combat.misc_rng = from_seed(seed);
        rc.run.hp = 11;
        choose(rc, 0);
        choose(rc, 0);
        EXPECT_EQ(rc.run.hp, 11) << "the spin reveals before applyResult";
        choose(rc, 0);
        EXPECT_EQ(rc.run.hp, rc.run.max_hp);
    }
    {
        const int64_t seed = seed_with_spin(3);
        ASSERT_NE(seed, 0);
        RunController rc = event_controller(EventId::WHEEL_OF_CHANGE);
        rc.combat.misc_rng = from_seed(seed);
        set_deck(rc.run, {{CardId::STRIKE, 0}});
        choose(rc, 0);
        choose(rc, 0);
        choose(rc, 0);
        ASSERT_EQ(rc.run.master_deck_count, 2);
        EXPECT_EQ(rc.run.master_deck[1].card_id,
                  static_cast<uint16_t>(CardId::DECAY));

        // The same result through Omamori: the Decay is absorbed.
        RunController blocked = event_controller(EventId::WHEEL_OF_CHANGE);
        blocked.combat.misc_rng = from_seed(seed);
        set_deck(blocked.run, {{CardId::STRIKE, 0}});
        give_relic(blocked.run, RelicId::OMAMORI, 1);
        choose(blocked, 0);
        choose(blocked, 0);
        choose(blocked, 0);
        EXPECT_EQ(blocked.run.master_deck_count, 1);
        EXPECT_EQ(blocked.run.relics[1].counter, 0);
    }
    {
        const int64_t seed = seed_with_spin(4);
        ASSERT_NE(seed, 0);
        RunController rc = event_controller(EventId::WHEEL_OF_CHANGE);
        rc.combat.misc_rng = from_seed(seed);
        set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::DEFEND, 0}});
        choose(rc, 0);
        choose(rc, 0);
        choose(rc, 0);
        EXPECT_EQ(rc.event.grid_kind,
                  static_cast<uint8_t>(EventGridKind::PURGE));
        choose(rc, 0);
        ASSERT_EQ(rc.run.master_deck_count, 1);
        EXPECT_EQ(rc.run.master_deck[0].card_id,
                  static_cast<uint16_t>(CardId::DEFEND));
        choose(rc, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));

        // No purgeable card: the result is a documented no-op, not a stall.
        RunController empty = event_controller(EventId::WHEEL_OF_CHANGE, 20);
        empty.combat.misc_rng = from_seed(seed);
        set_deck(empty.run, {{CardId::ASCENDERS_BANE, 0}});
        choose(empty, 0);
        choose(empty, 0);
        choose(empty, 0);
        EXPECT_EQ(empty.event.grid_kind,
                  static_cast<uint8_t>(EventGridKind::NONE));
        EXPECT_EQ(empty.run.master_deck_count, 1);
        choose(empty, 0);
        EXPECT_EQ(empty.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    }
}

TEST(WheelOfChange, DamageResultIsNullOwnerHpLossAndA15RaisesThePercent) {
    const int64_t seed = seed_with_spin(5);
    ASSERT_NE(seed, 0);
    {
        RunController rc = event_controller(EventId::WHEEL_OF_CHANGE);
        rc.combat.misc_rng = from_seed(seed);
        rc.run.max_hp = 80;
        rc.run.hp = 80;
        give_relic(rc.run, RelicId::TORII);
        choose(rc, 0);
        choose(rc, 0);
        choose(rc, 0);
        EXPECT_EQ(rc.run.hp, 72);  // (int)(80 * 0.10f) == 8
    }
    {
        RunController rc = event_controller(EventId::WHEEL_OF_CHANGE, 15);
        rc.combat.misc_rng = from_seed(seed);
        rc.run.max_hp = 80;
        rc.run.hp = 80;
        choose(rc, 0);
        choose(rc, 0);
        choose(rc, 0);
        EXPECT_EQ(rc.run.hp, 68);  // (int)(80 * 0.15f) == 12
    }
    {
        // Lethal: the run ends and the dialog is gone.
        RunController rc = event_controller(EventId::WHEEL_OF_CHANGE, 15);
        rc.combat.misc_rng = from_seed(seed);
        rc.run.max_hp = 80;
        rc.run.hp = 3;
        choose(rc, 0);
        choose(rc, 0);
        choose(rc, 0);
        EXPECT_EQ(rc.run.hp, 0);
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER));
    }
}

// =============================================================================
// Match and Keep!
// =============================================================================

// The independent hand-derivation of initializeCards + the board shuffle.
std::array<CardId, kMatchBoardSize> derive_match_board(RngStream& card_rng,
                                                      RngStream& shuffle_rng,
                                                      RngStream& misc_rng,
                                                      int ascension) {
    std::array<CardId, 6> ids{};
    ids[0] = kIroncladRarePool[static_cast<std::size_t>(
        random(card_rng, static_cast<int32_t>(kIroncladRarePoolCount - 1)))];
    ids[1] = kIroncladUncommonPool[static_cast<std::size_t>(random(
        card_rng, static_cast<int32_t>(kIroncladUncommonPoolCount - 1)))];
    ids[2] = kIroncladCommonPool[static_cast<std::size_t>(
        random(card_rng, static_cast<int32_t>(kIroncladCommonPoolCount - 1)))];
    if (ascension >= 15) {
        ids[3] = return_random_curse(card_rng);
        ids[4] = return_random_curse(card_rng);
    } else {
        // returnColorlessCard shuffles the LIVE `colorlessCardPool`, which
        // `addColorlessCards` fills with the APPENDING `addToTop`
        // (AbstractDungeon.java:1203-1210, CardGroup.java:455-457) and which is
        // therefore plain library order. `kColorlessPool` is the `src*` twin,
        // the same membership REVERSED, so this walks it backwards -- spelled
        // out here rather than shared with shrines.cpp, because an independent
        // transcription is the whole point of this derivation.
        std::array<CardId, static_cast<std::size_t>(kColorlessPoolCount)>
            colorless{};
        for (int i = 0; i < kColorlessPoolCount; ++i) {
            colorless[static_cast<std::size_t>(i)] = kColorlessPool[
                static_cast<std::size_t>(kColorlessPoolCount - 1 - i)];
        }
        JdkRandom colorless_rng(random_long(shuffle_rng));
        jdk_shuffle(std::span<CardId>(colorless), colorless_rng);
        ids[3] = CardId::SWIFT_STRIKE;
        for (CardId id : colorless) {
            if (reg::event_card_rarity(id) == reg::EventCardRarity::UNCOMMON) {
                ids[3] = id;
                break;
            }
        }
        ids[4] = return_random_curse(card_rng);
    }
    ids[5] = CardId::BASH;

    std::array<CardId, kMatchBoardSize> board{};
    for (int i = 0; i < 6; ++i) {
        board[static_cast<std::size_t>(i)] = ids[static_cast<std::size_t>(i)];
        board[static_cast<std::size_t>(i + 6)] =
            ids[static_cast<std::size_t>(i)];
    }
    JdkRandom board_rng(random_long(misc_rng));
    jdk_shuffle(std::span<CardId>(board), board_rng);
    return board;
}

TEST(MatchAndKeep, DealIsThreeStreamsAndMatchesTheHandDerivation) {
    RunController rc = raw_event_controller(EventId::MATCH_AND_KEEP);
    rc.run.card_rng = from_seed(778899);
    rc.combat.shuffle_rng = from_seed(112233);
    rc.combat.misc_rng = from_seed(445566);
    RngStream card = rc.run.card_rng;
    RngStream shuffle = rc.combat.shuffle_rng;
    RngStream misc = rc.combat.misc_rng;
    const std::array<CardId, kMatchBoardSize> expected =
        derive_match_board(card, shuffle, misc, 0);

    enter(rc);

    // Draw-count attribution, pinned per stream: FOUR cardRng draws below A15
    // (three getCard + one getCurse), ONE shuffleRng randomLong for the
    // colorless pick, ONE miscRng randomLong for the board shuffle.
    EXPECT_EQ(rc.run.card_rng.counter, card.counter);
    EXPECT_EQ(rc.run.card_rng.counter, from_seed(778899).counter + 4);
    EXPECT_EQ(rc.combat.shuffle_rng.counter, shuffle.counter);
    EXPECT_EQ(rc.combat.misc_rng.counter, misc.counter);

    for (int i = 0; i < kMatchBoardSize; ++i) {
        EXPECT_EQ(rc.event.board[i].card_id,
                  static_cast<uint16_t>(expected[static_cast<std::size_t>(i)]))
            << "board slot " << i;
        EXPECT_EQ(rc.event.board[i].taken, 0);
    }
    // Six identities, each dealt exactly twice.
    for (int i = 0; i < kMatchBoardSize; ++i) {
        int copies = 0;
        for (int j = 0; j < kMatchBoardSize; ++j) {
            copies += rc.event.board[i].card_id == rc.event.board[j].card_id;
        }
        EXPECT_GE(copies, 2);
    }
    EXPECT_EQ(rc.event.scratch0, 5);   // attemptCount
    EXPECT_EQ(rc.event.scratch1, -1);  // nothing flipped
}

TEST(MatchAndKeep, A15SwapsTheColorlessSlotForASecondCurseAndSkipsShuffleRng) {
    RunController rc = raw_event_controller(EventId::MATCH_AND_KEEP, 15);
    rc.run.card_rng = from_seed(778899);
    rc.combat.shuffle_rng = from_seed(112233);
    rc.combat.misc_rng = from_seed(445566);
    RngStream card = rc.run.card_rng;
    RngStream shuffle = rc.combat.shuffle_rng;
    RngStream misc = rc.combat.misc_rng;
    const std::array<CardId, kMatchBoardSize> expected =
        derive_match_board(card, shuffle, misc, 15);

    enter(rc);

    // FIVE cardRng draws at A15 (three getCard + two getCurse), and shuffleRng
    // is not touched at all because returnColorlessCard is never called.
    EXPECT_EQ(rc.run.card_rng.counter, from_seed(778899).counter + 5);
    EXPECT_EQ(rc.combat.shuffle_rng.counter, from_seed(112233).counter);
    EXPECT_EQ(rc.combat.misc_rng.counter, misc.counter);
    for (int i = 0; i < kMatchBoardSize; ++i) {
        EXPECT_EQ(rc.event.board[i].card_id,
                  static_cast<uint16_t>(expected[static_cast<std::size_t>(i)]));
    }
    int curses = 0;
    for (int i = 0; i < kMatchBoardSize; ++i) {
        const CardDef* def =
            card_def(static_cast<CardId>(rc.event.board[i].card_id));
        ASSERT_NE(def, nullptr);
        curses += def->type == CardType::CURSE;
    }
    EXPECT_EQ(curses, 4);  // two curse identities, dealt twice each
}

TEST(MatchAndKeep, FiveAttemptsMatchesObtainAndMissesOnlyFlipBack) {
    RunController rc = event_controller(EventId::MATCH_AND_KEEP);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    choose(rc, 0);  // acknowledge
    EXPECT_EQ(rc.event.screen, 1);
    choose(rc, 0);  // begin
    ASSERT_EQ(rc.event.screen, 2);

    const EventDialogMenu board_menu = menu(rc);
    ASSERT_EQ(board_menu.count, kMatchBoardSize);
    for (int i = 0; i < kMatchBoardSize; ++i) {
        EXPECT_TRUE(board_menu.enabled[i]);
    }

    // Locate a genuine pair and a mismatching slot from the dealt board.
    int a = 0;
    int b = -1;
    for (int j = 1; j < kMatchBoardSize && b < 0; ++j) {
        if (rc.event.board[j].card_id == rc.event.board[0].card_id) {
            b = j;
        }
    }
    ASSERT_GE(b, 0);
    int c = -1;
    for (int j = 1; j < kMatchBoardSize && c < 0; ++j) {
        if (j != b && rc.event.board[j].card_id != rc.event.board[0].card_id) {
            c = j;
        }
    }
    ASSERT_GE(c, 0);

    // A miss: both cards flip back, the deck is untouched, one attempt burns.
    const uint16_t deck = rc.run.master_deck_count;
    choose(rc, static_cast<uint8_t>(a));
    EXPECT_EQ(rc.event.scratch1, a);
    EXPECT_FALSE(menu(rc).enabled[a]);  // the flipped card cannot be re-picked
    choose(rc, static_cast<uint8_t>(c));
    EXPECT_EQ(rc.event.scratch1, -1);
    EXPECT_EQ(rc.event.scratch0, 4);
    EXPECT_EQ(rc.run.master_deck_count, deck);
    EXPECT_EQ(rc.event.board[a].taken, 0);
    EXPECT_TRUE(menu(rc).enabled[a]);

    // A match: both leave the board and the chosen copy joins the deck.
    choose(rc, static_cast<uint8_t>(a));
    choose(rc, static_cast<uint8_t>(b));
    EXPECT_EQ(rc.event.scratch0, 3);
    EXPECT_EQ(rc.event.board[a].taken, 1);
    EXPECT_EQ(rc.event.board[b].taken, 1);
    ASSERT_EQ(rc.run.master_deck_count, deck + 1);
    EXPECT_EQ(rc.run.master_deck[deck].card_id, rc.event.board[a].card_id);
    EXPECT_FALSE(menu(rc).enabled[a]);
    EXPECT_FALSE(menu(rc).enabled[b]);
    const RunActionMask mask = mask_of(rc);
    EXPECT_FALSE(mask.can_choose_event_option[a]);

    // Burn the remaining three attempts; the game ends on the fifth.
    int used = 2;
    while (rc.event.screen == 2) {
        int first = -1;
        int second = -1;
        for (int j = 0; j < kMatchBoardSize; ++j) {
            if (rc.event.board[j].taken != 0) {
                continue;
            }
            if (first < 0) {
                first = j;
            } else if (second < 0 &&
                       rc.event.board[j].card_id !=
                           rc.event.board[first].card_id) {
                second = j;
            }
        }
        ASSERT_GE(second, 0);
        choose(rc, static_cast<uint8_t>(first));
        choose(rc, static_cast<uint8_t>(second));
        ++used;
    }
    EXPECT_EQ(used, 5);
    EXPECT_EQ(rc.event.scratch0, 0);
    EXPECT_EQ(rc.event.screen, 3);
    EXPECT_EQ(menu(rc).count, 1);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(MatchAndKeep, MatchedCurseStillPassesThroughTheOmamoriDoor) {
    RunController rc = raw_event_controller(EventId::MATCH_AND_KEEP, 15);
    give_relic(rc.run, RelicId::OMAMORI, 1);
    enter(rc);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    choose(rc, 0);
    choose(rc, 0);

    int a = -1;
    int b = -1;
    for (int i = 0; i < kMatchBoardSize && a < 0; ++i) {
        const CardDef* def =
            card_def(static_cast<CardId>(rc.event.board[i].card_id));
        if (def == nullptr || def->type != CardType::CURSE) {
            continue;
        }
        for (int j = i + 1; j < kMatchBoardSize; ++j) {
            if (rc.event.board[j].card_id == rc.event.board[i].card_id) {
                a = i;
                b = j;
                break;
            }
        }
    }
    ASSERT_GE(b, 0);
    choose(rc, static_cast<uint8_t>(a));
    choose(rc, static_cast<uint8_t>(b));
    EXPECT_EQ(rc.event.board[a].taken, 1);
    EXPECT_EQ(rc.run.master_deck_count, 1);  // the curse was absorbed
    EXPECT_EQ(rc.run.relics[1].counter, 0);
}

}  // namespace
