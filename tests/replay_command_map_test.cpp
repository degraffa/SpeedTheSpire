// replay_command_map_test -- the replay harness's screen-relative command
// mapping (tools/oracle_bridge/replay/src/command_map.hpp).
//
// WHY THIS SUITE EXISTS. `replay_run_diff --replay` re-drives the simulator
// through a captured command sequence, so a wrong ENTRY IN THE MAPPING TABLE
// looks exactly like an engine divergence: the sim silently does the wrong
// thing and every subsequent record disagrees. Until the table was separable
// the only way to see one was to re-run a whole campaign artifact by hand, and
// one such entry cost two runs of the b45_rewards_oracle2 campaign their whole
// post-floor-2 frontier (see `EVENT` below).
//
// The mapping is a pure function of (sim phase, captured screen, command), so
// these are stdlib-only unit tests over hand-built `ScreenInfo`s -- no
// artifact, no JSON, no campaign data root.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "command_map.hpp"
#include "sts/engine/run_advance.hpp"

namespace {

using sts::engine::action_arg0;
using sts::engine::action_verb;
using sts::engine::ActionVerb;
using sts::engine::EventGridKind;
using sts::engine::kChooseProceed;
using sts::engine::NeowScreen;
using sts::engine::RelicId;
using sts::engine::RelicSlot;
using sts::engine::RestScreen;
using sts::engine::RunController;
using sts::engine::RunPhase;
using sts::engine::ShopScreenKind;
using sts::replay::map_command;
using sts::replay::MapKind;
using sts::replay::MappedCommand;
using sts::replay::ScreenInfo;
using sts::replay::sim_grid_open;

// The mapping reads `rc.phase` and, on the GRID path, the sub-screen field that
// phase owns plus the relic list -- never the master deck, since the grid's
// index space belongs to the caller's GridSession. So a value-initialized
// controller with the phase set is the whole fixture, and the GRID cases just
// set one more field on it.
[[nodiscard]] RunController at_phase(RunPhase phase) {
    RunController rc{};
    rc.phase = static_cast<uint8_t>(phase);
    return rc;
}

[[nodiscard]] ScreenInfo event_page(std::vector<std::string> labels) {
    ScreenInfo s;
    s.screen_type = "EVENT";
    s.option_labels = std::move(labels);
    return s;
}

// --- the terminal [Leave] page ----------------------------------------------
//
// Every finished event dialog ends on a one-button page, and pressing that
// button calls AbstractEvent.openMap (AbstractEvent.java:120-123), which does
// two things and no more: it sets the CURRENT ROOM's phase to COMPLETE and
// opens the dungeon map with `doScrollingAnimation == false`, which is what
// makes the map DISMISSABLE (DungeonMapScreen.java:287). The event object, its
// room and its dialog panel all stay mounted -- DungeonMapScreen.close()
// (:316-320) hides the map and nothing else -- so a map `return` drops straight
// back onto the same page, and the capture's random-legal policy can press
// [Leave] again any number of times. Each repeat re-enters buttonEffect at the
// same screenNum and calls openMap again (FountainOfCurseRemoval.java:73-79;
// LivingWall.java:116-119), so every press after the first is state-free.
//
// The run layer has one door out of an event and no way back, exactly as with a
// reward screen's `proceed`. The sim's PHASE is what separates the exit from
// the bounces.

TEST(ReplayCommandMap, AnEventsFinalLeavePageIsTheRealExitWhileTheDialogIsLive) {
    const RunController rc = at_phase(RunPhase::EVENT_DIALOG);
    const MappedCommand m = map_command(rc, event_page({"Leave"}), "choose 0");
    ASSERT_EQ(m.kind, MapKind::ACTIONS) << m.reason;
    ASSERT_EQ(m.actions.size(), 1u);
    EXPECT_EQ(action_verb(m.actions[0]), ActionVerb::CHOOSE);
    EXPECT_EQ(action_arg0(m.actions[0]), 0);
}

TEST(ReplayCommandMap, ALeavePressedAfterTheSimAlreadyLeftTheEventIsAUiBounce) {
    const RunController rc = at_phase(RunPhase::MAP_CHOICE);
    const MappedCommand m = map_command(rc, event_page({"Leave"}), "choose 0");
    EXPECT_EQ(m.kind, MapKind::NOOP) << m.reason;
    EXPECT_TRUE(m.actions.empty());
}

// The bounce elision is deliberately narrow: it is the ONE-BUTTON page that
// openMap leaves behind. A multi-option event page while the sim is no longer
// in an event is a genuine desync, and stopping with a reason beats feeding a
// CHOOSE to whatever phase happens to be live -- in MAP_CHOICE that CHOOSE
// would pick a map node and move the run.
TEST(ReplayCommandMap, AMultiOptionEventPageOutsideAnEventStopsInsteadOfGuessing) {
    const RunController rc = at_phase(RunPhase::MAP_CHOICE);
    const MappedCommand m =
        map_command(rc, event_page({"Drink", "Leave"}), "choose 1");
    EXPECT_EQ(m.kind, MapKind::UNMAPPED);
    EXPECT_FALSE(m.reason.empty());
}

// --- Neow's two framing screens ---------------------------------------------
//
// Neow arrives as an EVENT screen but is not an events.yaml row; the run layer
// models the blessing menu as its own NEOW phase and does not model the opening
// [Talk] page (NeowEvent screenNum 0->3, no state change) or the closing
// [Leave], whose run-layer analogue is the proceed that opens the map.

TEST(ReplayCommandMap, NeowsOpeningTalkPageHasNoRunLayerEffect) {
    const RunController rc = at_phase(RunPhase::NEOW);
    const MappedCommand m = map_command(rc, event_page({"Talk"}), "choose 0");
    EXPECT_EQ(m.kind, MapKind::NOOP) << m.reason;
}

TEST(ReplayCommandMap, NeowsClosingLeaveIsTheRunLayersProceed) {
    const RunController rc = at_phase(RunPhase::NEOW);
    const MappedCommand m = map_command(rc, event_page({"Leave"}), "choose 0");
    ASSERT_EQ(m.kind, MapKind::ACTIONS) << m.reason;
    ASSERT_EQ(m.actions.size(), 1u);
    EXPECT_EQ(action_verb(m.actions[0]), ActionVerb::CHOOSE);
    EXPECT_EQ(action_arg0(m.actions[0]), kChooseProceed);
}

TEST(ReplayCommandMap, NeowsBlessingMenuChoiceIsIndexedStraightThrough) {
    const RunController rc = at_phase(RunPhase::NEOW);
    const MappedCommand m = map_command(
        rc, event_page({"Boss Swap", "Max HP", "Remove Card"}), "choose 2");
    ASSERT_EQ(m.kind, MapKind::ACTIONS) << m.reason;
    ASSERT_EQ(m.actions.size(), 1u);
    EXPECT_EQ(action_arg0(m.actions[0]), 2);
}

// A [Leave] pressed again after Neow's own map is already up is the same bounce
// as an ordinary event's: the run layer has already left NEOW for MAP_CHOICE.
TEST(ReplayCommandMap, NeowsLeaveRepeatedAfterItsMapIsUpIsAUiBounce) {
    const RunController rc = at_phase(RunPhase::MAP_CHOICE);
    const MappedCommand m = map_command(rc, event_page({"Leave"}), "choose 0");
    EXPECT_EQ(m.kind, MapKind::NOOP) << m.reason;
}

// --- the potion belt ---------------------------------------------------------
//
// `potion discard i` is the top panel's own button, and the top panel is drawn
// over whatever screen is up. CommandExecutor.executePotionCommand never looks
// at the screen: it bounds-checks the slot, asks canDiscard, and calls
// topPanel.destroyPotion, which is `potions.set(slot, new PotionSlot(slot))`
// and nothing else (TopPanel.java:529-531). The run layer's DISCARD_POTION is
// dispatched ahead of its own phase switch for the same reason, so ONE entry
// covers every screen -- and that is what these pin, because the entry used to
// exist nowhere and a MAP record carrying it ended the replay outright.

TEST(ReplayCommandMap, APotionDiscardIsTheSameMappingOnEveryScreen) {
    for (const char* screen : {"MAP", "SHOP_SCREEN", "COMBAT_REWARD", "NONE",
                               "EVENT", "GRID", "REST"}) {
        ScreenInfo s;
        s.screen_type = screen;
        const MappedCommand m =
            map_command(at_phase(RunPhase::MAP_CHOICE), s, "potion discard 1");
        ASSERT_EQ(m.kind, MapKind::ACTIONS) << screen << ": " << m.reason;
        ASSERT_EQ(m.actions.size(), 1u) << screen;
        EXPECT_EQ(action_verb(m.actions[0]), ActionVerb::DISCARD_POTION) << screen;
        EXPECT_EQ(action_arg0(m.actions[0]), 1) << screen;
    }
}

// The sibling verb must not be swept up with it: `potion use` needs a live
// target and stays the combat branch's business.
TEST(ReplayCommandMap, APotionUseIsStillTheCombatScreensTargetedVerb) {
    ScreenInfo s;
    s.screen_type = "NONE";
    const MappedCommand m =
        map_command(at_phase(RunPhase::COMBAT), s, "potion use 0 2");
    ASSERT_EQ(m.kind, MapKind::ACTIONS) << m.reason;
    ASSERT_EQ(m.actions.size(), 1u);
    EXPECT_EQ(action_verb(m.actions[0]), ActionVerb::USE_POTION);
    EXPECT_EQ(action_arg0(m.actions[0]), 0);
}

// --- grid screens ------------------------------------------------------------
//
// GridCardSelectScreen selects on click and commits on a button; the run layer
// does both at once and can undo neither. So a grid command is not a run-layer
// action at all -- it is an edit to a pending selection the CALLER holds
// (GridSession), and the table's only job is to say which of the three edits it
// is.

[[nodiscard]] RunController at_neow_grid() {
    RunController rc = at_phase(RunPhase::NEOW);
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::GRID);
    return rc;
}

[[nodiscard]] ScreenInfo grid_screen() {
    ScreenInfo s;
    s.screen_type = "GRID";
    return s;
}

TEST(ReplayCommandMap, AGridChooseIsBufferedRatherThanAppliedImmediately) {
    const MappedCommand m = map_command(at_neow_grid(), grid_screen(), "choose 3");
    ASSERT_EQ(m.kind, MapKind::GRID_PICK) << m.reason;
    EXPECT_EQ(m.grid_index, 3);
    EXPECT_TRUE(m.actions.empty())
        << "nothing may reach the run layer until the capture confirms";
}

// The gap this closes. STS00047's Neow removal grid reads `choose 2`, `cancel`,
// `choose 0`, `proceed`: the player eyed one Strike, changed their mind, and
// removed a different one. Because the pick was never applied, honouring the
// cancel costs nothing -- but the old table called `cancel` unmappable ("a grid
// pick cannot be undone") and ended that replay at seq 3, four records in.
TEST(ReplayCommandMap, AGridCancelDropsThePendingSelectionInsteadOfStopping) {
    const MappedCommand m = map_command(at_neow_grid(), grid_screen(), "cancel");
    EXPECT_EQ(m.kind, MapKind::GRID_CANCEL) << m.reason;
    EXPECT_TRUE(m.actions.empty());
}

TEST(ReplayCommandMap, AGridProceedIsTheCommitAndNotAScreenExit) {
    const MappedCommand m = map_command(at_neow_grid(), grid_screen(), "proceed");
    EXPECT_EQ(m.kind, MapKind::GRID_COMMIT) << m.reason;
    EXPECT_TRUE(m.actions.empty())
        << "the commit applies the buffered picks; it is not a kChooseProceed";
}

// The classification. STS00052 takes Neow's boss-relic blessing, is handed an
// Astrolabe, and the capture then drives Astrolabe's onEquip transform grid --
// which the engine never opens, because that onEquip is one of the five
// deferred BOSS bodies. The ACQUISITION is right (the relic is in the list, its
// pool is popped, every stream matches), so the honest outcome is a stop that
// names the body. The old table said "grid choose index has no legal
// master-deck slot", which reads as an index-mapping defect and is not one.
TEST(ReplayCommandMap, AGridTheSimNeverOpenedNamesTheDeferredRelicBody) {
    RunController rc = at_phase(RunPhase::NEOW);
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::BLESSING);
    rc.run.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::ASTROLABE), 0};
    rc.run.relic_count = 1;

    const MappedCommand m = map_command(rc, grid_screen(), "choose 3");
    EXPECT_EQ(m.kind, MapKind::UNMAPPED);
    EXPECT_NE(m.reason.find("Astrolabe"), std::string::npos) << m.reason;
    EXPECT_NE(m.reason.find("deferred"), std::string::npos) << m.reason;
}

// sim_grid_open is the discriminator that classification turns on, so it has to
// know every phase that owns a master-deck grid: a phase it did not know would
// misreport a perfectly live grid as a deferred body.
TEST(ReplayCommandMap, EveryPhaseWithAMasterDeckGridIsRecognised) {
    RunController rc{};
    EXPECT_FALSE(sim_grid_open(rc));

    rc.phase = static_cast<uint8_t>(RunPhase::NEOW);
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::GRID);
    EXPECT_TRUE(sim_grid_open(rc));
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::CARD_REWARD);
    EXPECT_FALSE(sim_grid_open(rc));

    rc = RunController{};
    rc.phase = static_cast<uint8_t>(RunPhase::REST_SITE);
    rc.rest.screen = static_cast<uint8_t>(RestScreen::SMITH);
    EXPECT_TRUE(sim_grid_open(rc));
    rc.rest.screen = static_cast<uint8_t>(RestScreen::TOKE);
    EXPECT_TRUE(sim_grid_open(rc));
    rc.rest.screen = static_cast<uint8_t>(RestScreen::MENU);
    EXPECT_FALSE(sim_grid_open(rc)) << "the campfire menu is not a grid";

    rc = RunController{};
    rc.phase = static_cast<uint8_t>(RunPhase::SHOP);
    rc.shop.screen = static_cast<uint8_t>(ShopScreenKind::PURGE_GRID);
    EXPECT_TRUE(sim_grid_open(rc));
    rc.shop.screen = static_cast<uint8_t>(ShopScreenKind::MENU);
    EXPECT_FALSE(sim_grid_open(rc));

    rc = RunController{};
    rc.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
    rc.event.grid_kind = static_cast<uint8_t>(EventGridKind::PURGE);
    EXPECT_TRUE(sim_grid_open(rc));
    rc.event.grid_kind = static_cast<uint8_t>(EventGridKind::NONE);
    EXPECT_FALSE(sim_grid_open(rc));
}

// --- the neighbouring elisions, pinned so the EVENT fix cannot disturb them --

TEST(ReplayCommandMap, AMapReturnIsAPureUiDismissal) {
    ScreenInfo s;
    s.screen_type = "MAP";
    const MappedCommand m = map_command(at_phase(RunPhase::MAP_CHOICE), s, "return");
    EXPECT_EQ(m.kind, MapKind::NOOP) << m.reason;
}

TEST(ReplayCommandMap, ARewardScreenProceedIsDeferredToTheMapChoiceThatMoves) {
    ScreenInfo s;
    s.screen_type = "COMBAT_REWARD";
    const MappedCommand proceed =
        map_command(at_phase(RunPhase::COMBAT_REWARD), s, "proceed");
    EXPECT_EQ(proceed.kind, MapKind::NOOP) << proceed.reason;

    ScreenInfo map;
    map.screen_type = "MAP";
    map.map_next_x = {3};
    const MappedCommand move =
        map_command(at_phase(RunPhase::COMBAT_REWARD), map, "choose 0");
    ASSERT_EQ(move.kind, MapKind::LEAVE_ROOM) << move.reason;
    ASSERT_EQ(move.actions.size(), 1u);
    EXPECT_EQ(action_arg0(move.actions[0]), 3);
}

}  // namespace
