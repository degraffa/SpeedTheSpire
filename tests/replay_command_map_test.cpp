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
using sts::engine::kChooseProceed;
using sts::engine::RunController;
using sts::engine::RunPhase;
using sts::replay::map_command;
using sts::replay::MapKind;
using sts::replay::MappedCommand;
using sts::replay::ScreenInfo;

// The mapping reads only `rc.phase` on every path exercised here (the GRID path
// additionally reads the master deck through legal_actions, and is not one of
// these cases), so a value-initialized controller with the phase set is the
// whole fixture.
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
