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

#include <algorithm>
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

// The same page WITH the fork's per-button `choice_index`, which is what a real
// capture carries. -1 stands for the field's ABSENCE, which is how a DISABLED
// button appears.
[[nodiscard]] ScreenInfo event_page_indexed(std::vector<std::string> labels,
                                            std::vector<int> choice_index) {
    ScreenInfo s = event_page(std::move(labels));
    s.option_choice_index = std::move(choice_index);
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

// A STOP REASON IS READ BY A HUMAN, SO IT SPELLS THE PHASE. STS00042 of
// `b45_rewards_oracle_20260727T204809Z_claude01` stopped with "the sim is in 3,
// not an event dialog", and the obligation row filed off that message had to
// gloss the integer itself ("3 [COMBAT]") before it could even ask its question
// -- then asked the wrong one, because a bare enum ordinal says nothing about
// what the sim was actually doing. `phase_name` already existed for the
// per-record `DIFF` line; the reasons now use the same spelling.
TEST(ReplayCommandMap, AnEventDesyncStopNamesTheSimsPhaseRatherThanItsOrdinal) {
    const RunController rc = at_phase(RunPhase::COMBAT);
    const MappedCommand m =
        map_command(rc, event_page({"Touch", "Trade", "Leave"}), "choose 0");
    ASSERT_EQ(m.kind, MapKind::UNMAPPED);
    EXPECT_NE(m.reason.find("COMBAT"), std::string::npos) << m.reason;
    EXPECT_EQ(m.reason.find(" in 3,"), std::string::npos)
        << "a bare ordinal is what made this stop unreadable: " << m.reason;
}

TEST(ReplayCommandMap, EveryRunPhaseHasAName) {
    // A phase whose name is missing would print "?" into a stop reason -- the
    // same unreadable outcome the ordinal produced, arriving silently.
    for (const RunPhase p :
         {RunPhase::NONE, RunPhase::NEOW, RunPhase::MAP_CHOICE, RunPhase::COMBAT,
          RunPhase::COMBAT_REWARD, RunPhase::ROOM_UNIMPLEMENTED, RunPhase::RUN_OVER,
          RunPhase::REST_SITE, RunPhase::TREASURE_ROOM, RunPhase::EVENT_DIALOG,
          RunPhase::SHOP}) {
        const std::string name = sts::replay::phase_name(static_cast<uint8_t>(p));
        EXPECT_FALSE(name.empty());
        EXPECT_STRNE(name.c_str(), "?")
            << "unnamed RunPhase ordinal " << static_cast<int>(p);
    }
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

// The classification, RE-POINTED after Wave-C track 2 and reconciled with the
// G6 classifier at integration. Until the five BOSS onEquip bodies landed,
// STS00045/46/52's captures drove a boss relic's grid the engine never opened
// and this stop could assert the deferral as FACT. Astrolabe's grid is now
// live (the sim opens NeowScreen::GRID itself, and the GRID_PICK path below is
// what a replay takes), and the classifier knows the build's own
// deferred-whole set (`relic_on_equip_deferred`), so for a relic whose body is
// LIVE the reason must rule the deferral out by name and point at the
// divergence instead -- naming the relic as inspected, not as the verdict. The
// state here is synthetic (BLESSING with a grid command) precisely because a
// live tree can only reach it by diverging.
TEST(ReplayCommandMap, AGridTheSimNeverOpenedNamesTheLastRelicAsTheSuspect) {
    RunController rc = at_phase(RunPhase::NEOW);
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::BLESSING);
    rc.run.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::ASTROLABE), 0};
    rc.run.relic_count = 1;

    const MappedCommand m = map_command(rc, grid_screen(), "choose 3");
    EXPECT_EQ(m.kind, MapKind::UNMAPPED);
    EXPECT_NE(m.reason.find("Astrolabe"), std::string::npos) << m.reason;
    EXPECT_NE(m.reason.find("deferred"), std::string::npos) << m.reason;
    EXPECT_NE(m.reason.find("diverged"), std::string::npos)
        << "the reason must no longer assert the deferral as fact: " << m.reason;
}

// The LIVE half of the same screen: with the sim's Astrolabe grid actually
// open (NeowScreen::GRID), the identical capture command is an ordinary
// buffered grid pick, not a stop. This is the pairing that keeps the fallback
// above honest -- the classification fires only when the sim really has no
// grid.
TEST(ReplayCommandMap, ALiveNeowGridTakesTheBufferedPickPathInstead) {
    RunController rc = at_phase(RunPhase::NEOW);
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::GRID);
    rc.run.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::ASTROLABE), 0};
    rc.run.relic_count = 1;

    const MappedCommand m = map_command(rc, grid_screen(), "choose 3");
    EXPECT_EQ(m.kind, MapKind::GRID_PICK) << m.reason;
    EXPECT_EQ(m.grid_index, 3);
}

// Same readability rule as the EVENT stop above: this reason also carried a
// bare `rc.phase` ordinal.
TEST(ReplayCommandMap, AnUnsimulatedGridStopAlsoNamesThePhaseRatherThanItsOrdinal) {
    RunController rc = at_phase(RunPhase::NEOW);
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::BLESSING);
    rc.run.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::ASTROLABE), 0};
    rc.run.relic_count = 1;

    const MappedCommand m = map_command(rc, grid_screen(), "choose 3");
    ASSERT_EQ(m.kind, MapKind::UNMAPPED);
    EXPECT_NE(m.reason.find("NEOW"), std::string::npos) << m.reason;
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

// --- the bottle grid's REVERSE row order, proven live -------------------------
//
// getCardsOfType builds the bottle grid with addToBottom, which is a PREPEND
// (CardGroup.java:1052-1058 -> :459-461), so the game's grid rows run in
// REVERSE master-deck order -- unlike every other master-deck grid, whose
// getPurgeableCards keeps deck order. open_grid_session therefore snapshots
// the legal indices DESCENDING for a pending-bottle grid, and this was the one
// mapping claim only a live capture could prove.
//
// It was proven 2026-07-28: campaign wave2cap_roundA2_20260728T224858Z_claude01
// run STS04925, seq 58-59 -- a floor-5 chest offers Bottled Flame, the claim
// opens the game's grid reading [Blood for Blood, Sword Boomerang, Bash,
// Strike, Strike+, Strike, Strike, Strike] over a deck whose attacks sit in
// exactly the opposite order, and the capture's `choose 1` bottles SWORD
// BOOMERANG (the deck's second-from-last attack). replay_run_diff --replay
// re-drove the run through this session and diffed zero on every record
// through and past the bottling -- master-deck `flags` included, which is the
// bit the fork's in_bottle_flame key populates on the capture side. A forward
// (ascending) snapshot would have bottled an opening Strike instead and
// diverged on every record from seq 59 on. This test is that capture's shape,
// promoted; the campaign artifact is the evidence, this is the guard.
TEST(ReplayCommandMap, ABottleGridSessionSnapshotsTheLegalIndicesDescending) {
    namespace eng = sts::engine;
    RunController rc = at_phase(RunPhase::COMBAT_REWARD);
    rc.pending_bottle = static_cast<uint8_t>(eng::MasterBottleKind::FLAME);
    auto add = [&rc](eng::CardId id, uint8_t up = 0) {
        eng::CardInstance c{};
        c.card_id = static_cast<uint16_t>(id);
        c.upgrade = up;
        rc.run.master_deck[rc.run.master_deck_count++] = c;
    };
    // STS04925's deck shape at the claim: starter Strikes (one smithed),
    // Defends, Bash, then the two acquired attacks in acquisition order.
    add(eng::CardId::STRIKE);        // 0
    add(eng::CardId::STRIKE);        // 1
    add(eng::CardId::STRIKE, 1);     // 2
    add(eng::CardId::STRIKE);       // 3
    add(eng::CardId::STRIKE);       // 4
    add(eng::CardId::DEFEND);       // 5 -- a SKILL: not on the flame grid
    add(eng::CardId::DEFEND);       // 6
    add(eng::CardId::BASH);         // 7
    add(eng::CardId::SWORD_BOOMERANG);  // 8
    add(eng::CardId::BLOOD_FOR_BLOOD);  // 9

    ASSERT_TRUE(sim_grid_open(rc)) << "pending_bottle overlays any phase";

    sts::replay::GridSession g;
    sts::replay::open_grid_session(rc, g);
    // The legal rows are the eight attacks; the session must hold their
    // master-deck indices DESCENDING, so the game's top row (the LAST
    // acquired attack) maps positionally.
    const std::vector<int> expected = {9, 8, 7, 4, 3, 2, 1, 0};
    EXPECT_EQ(g.filtered, expected);
    // The capture's `choose 1` names the game's second row == Sword Boomerang
    // (deck index 8), which is where the live run's in_bottle_flame landed.
    ASSERT_GT(g.filtered.size(), 1u);
    EXPECT_EQ(g.filtered[1], 8);

    // And the non-bottle contrast: the same deck's PURGE-style grid (no
    // pending bottle) snapshots ASCENDING -- the ordering exception is the
    // bottle grid alone.
    rc.pending_bottle = static_cast<uint8_t>(eng::MasterBottleKind::NONE);
    rc.phase = static_cast<uint8_t>(RunPhase::REST_SITE);
    rc.rest.screen = static_cast<uint8_t>(RestScreen::SMITH);
    sts::replay::GridSession g2;
    sts::replay::open_grid_session(rc, g2);
    ASSERT_FALSE(g2.filtered.empty());
    EXPECT_TRUE(std::is_sorted(g2.filtered.begin(), g2.filtered.end()));
}

// --- the neighbouring elisions, pinned so the EVENT fix cannot disturb them --

TEST(ReplayCommandMap, AMapReturnIsAPureUiDismissal) {
    ScreenInfo s;
    s.screen_type = "MAP";
    const MappedCommand m = map_command(at_phase(RunPhase::MAP_CHOICE), s, "return");
    EXPECT_EQ(m.kind, MapKind::NOOP) << m.reason;
}

// --- an event's two index spaces --------------------------------------------
//
// `screen_state.options[]` lists every dialog button, disabled ones included,
// and the run layer's option ordinal is that same full-list position (an event
// body publishes `count` buttons plus an `enabled[]` mask). A `choose N`
// command indexes `choice_list`, which is the ENABLED buttons only -- and each
// option's `choice_index` is precisely its position in THAT space.
//
// STS00856 of the G6 campaign is the live cost of conflating them. Golden Wing
// offers [Pray, Locked(disabled), Leave] with choice_index [0, -, 1]; greedy
// pressed `choose 1` = Leave, the untranslated 1 named the LOCKED gold branch,
// the sim's own enabled[] mask refused it, and the sim stayed on the intro
// page. The next record's exit press (`choose 0` on a one-button page) was then
// applied to the intro page's option 0 -- Pray -- costing the sim 7 HP the run
// never lost (`hp: 60 -> 53` at seq 25) and leaving it a floor behind for the
// rest of the artifact.
TEST(ReplayCommandMap, AnEventChooseIsTranslatedOutOfTheChoiceListIndexSpace) {
    const ScreenInfo golden_wing =
        event_page_indexed({"Pray", "Locked", "Leave"}, {0, -1, 1});
    const MappedCommand m =
        map_command(at_phase(RunPhase::EVENT_DIALOG), golden_wing, "choose 1");
    ASSERT_EQ(m.kind, MapKind::ACTIONS) << m.reason;
    ASSERT_EQ(m.actions.size(), 1u);
    EXPECT_EQ(action_verb(m.actions[0]), ActionVerb::CHOOSE);
    EXPECT_EQ(action_arg0(m.actions[0]), 2)
        << "`choose 1` is the second ENABLED button, which is option 2";
}

TEST(ReplayCommandMap, APageWithNoDisabledButtonIsUnchangedByTheTranslation) {
    const ScreenInfo page =
        event_page_indexed({"Dig", "Pray", "Leave"}, {0, 1, 2});
    const MappedCommand m =
        map_command(at_phase(RunPhase::EVENT_DIALOG), page, "choose 2");
    ASSERT_EQ(m.kind, MapKind::ACTIONS) << m.reason;
    ASSERT_EQ(m.actions.size(), 1u);
    EXPECT_EQ(action_arg0(m.actions[0]), 2);
}

// An artifact written before the fork emitted `choice_index` carries no such
// field, and every page in it is one the identity mapping was right about.
TEST(ReplayCommandMap, ACaptureWithoutChoiceIndexFallsBackToTheIdentity) {
    const MappedCommand m = map_command(at_phase(RunPhase::EVENT_DIALOG),
                                        event_page({"Dig", "Pray", "Leave"}),
                                        "choose 1");
    ASSERT_EQ(m.kind, MapKind::ACTIONS) << m.reason;
    ASSERT_EQ(m.actions.size(), 1u);
    EXPECT_EQ(action_arg0(m.actions[0]), 1);
}

// The translation fails LOUD rather than falling back when the index space says
// the two sides disagree: guessing here is what produced the STS00856 shape.
TEST(ReplayCommandMap, AnEventChooseNamingNoEnabledButtonStopsInsteadOfGuessing) {
    const ScreenInfo golden_wing =
        event_page_indexed({"Pray", "Locked", "Leave"}, {0, -1, 1});
    const MappedCommand m =
        map_command(at_phase(RunPhase::EVENT_DIALOG), golden_wing, "choose 2");
    EXPECT_EQ(m.kind, MapKind::UNMAPPED);
    EXPECT_NE(m.reason.find("choice_index"), std::string::npos) << m.reason;
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

// --- Neow's three-potion payout is a COMBAT_REWARD screen that MUST be left --
//
// The lazy-leave elision above is right for a combat-reward ROOM and wrong for
// the NeowRoom. `NeowRewardType::THREE_SMALL_POTIONS` delivers through
// `combatRewardScreen.open()` (NeowReward.java:268-283), so the capture's
// screen label is `COMBAT_REWARD` and says nothing about which of the two it
// is; the SIM's phase does, exactly as it does for an event's exit page.
//
// Both G6 runs whose blessing was "Obtain 3 random Potions" -- STS00283 and
// STS00700 -- went `COMBAT_REWARD` `proceed` (seq 5) straight to a `MAP` (seq
// 6) with no page between, and both diverged at seq 7 on one field, `floor: 1
// -> 0`: the NOOP left the sim in NEOW, the map `choose` became LEAVE_ROOM, and
// its CHOOSE(dst) was executed by `ITEM_REWARD`'s else branch as
// `claim_reward(dst)`. Seq 0-6 were zero-diff on both, and `--neow` reads both
// out fully clean, so the engine half was never at fault.
TEST(ReplayCommandMap, NeowsPotionRewardScreenIsLeftRatherThanNoOpped) {
    RunController rc = at_phase(RunPhase::NEOW);
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::ITEM_REWARD);
    ScreenInfo s;
    s.screen_type = "COMBAT_REWARD";

    const MappedCommand m = map_command(rc, s, "proceed");
    ASSERT_EQ(m.kind, MapKind::ACTIONS) << m.reason;
    // Two presses, because the run layer spells in two states what the game
    // passed through in one frame: ITEM_REWARD -> DONE (neow_finish_payout),
    // then DONE -> RunPhase::MAP_CHOICE (run_advance.cpp:1478-1494).
    ASSERT_EQ(m.actions.size(), 2u);
    for (const auto& a : m.actions) {
        EXPECT_EQ(action_verb(a), ActionVerb::CHOOSE);
        EXPECT_EQ(action_arg0(a), kChooseProceed);
    }
}

TEST(ReplayCommandMap, ClaimingAPotionOnNeowsRewardScreenIsStillAPlainChoose) {
    RunController rc = at_phase(RunPhase::NEOW);
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::ITEM_REWARD);
    ScreenInfo s;
    s.screen_type = "COMBAT_REWARD";

    const MappedCommand m = map_command(rc, s, "choose 1");
    ASSERT_EQ(m.kind, MapKind::ACTIONS) << m.reason;
    ASSERT_EQ(m.actions.size(), 1u);
    EXPECT_EQ(action_arg0(m.actions[0]), 1);
}

// The discriminator is the Neow SUB-SCREEN, not merely the NEOW phase: only
// ITEM_REWARD is the screen a `proceed` closes.
TEST(ReplayCommandMap, AProceedElsewhereInNeowKeepsTheLazyLeaveElision) {
    RunController rc = at_phase(RunPhase::NEOW);
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::CARD_REWARD);
    ScreenInfo s;
    s.screen_type = "COMBAT_REWARD";
    EXPECT_EQ(map_command(rc, s, "proceed").kind, MapKind::NOOP);
}

// --- the grid stop must not blame a relic that is not the cause --------------
//
// STS02009 of the G6 campaign stopped with "the most recently acquired relic is
// Burning Blood, whose onEquip body is deferred". Burning Blood is the Ironclad
// STARTING relic and its onEquip is modelled; nothing had just been acquired.
// The reason read `rc.run.relics`' last entry unconditionally, so on a run that
// never took a relic it named the one the player started with -- and asserted a
// deferral that does not exist. The real cause was a desync: the sim had been
// in COMBAT since seq 61 while the capture opened a master-deck grid.
TEST(ReplayCommandMap, AGridStopInCombatBlamesTheDesyncNotTheStartingRelic) {
    RunController rc = at_phase(RunPhase::COMBAT);
    rc.run.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::BURNING_BLOOD), 0};
    rc.run.relic_count = 1;

    const MappedCommand m = map_command(rc, grid_screen(), "choose 1");
    ASSERT_EQ(m.kind, MapKind::UNMAPPED);
    EXPECT_NE(m.reason.find("COMBAT"), std::string::npos) << m.reason;
    EXPECT_EQ(m.reason.find("Burning Blood"), std::string::npos)
        << "the relic list has nothing to do with a grid opened mid-combat: "
        << m.reason;
    EXPECT_EQ(m.reason.find("deferred"), std::string::npos) << m.reason;
    EXPECT_NE(m.reason.find("first divergence"), std::string::npos)
        << "the stop must point the reader at the frontier: " << m.reason;
}

// Even on a phase where a pickup COULD have just happened, a relic whose
// onEquip is modelled is not an explanation -- and saying so is not the same as
// saying nothing, because it rules the relic out by name.
TEST(ReplayCommandMap, AGridStopDoesNotClaimAModelledOnEquipIsDeferred) {
    RunController rc = at_phase(RunPhase::MAP_CHOICE);
    rc.run.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::BURNING_BLOOD), 0};
    rc.run.relic_count = 1;

    const MappedCommand m = map_command(rc, grid_screen(), "choose 1");
    ASSERT_EQ(m.kind, MapKind::UNMAPPED);
    EXPECT_NE(m.reason.find("Burning Blood"), std::string::npos) << m.reason;
    EXPECT_NE(m.reason.find("modelled"), std::string::npos) << m.reason;
    EXPECT_EQ(m.reason.find("body is deferred"), std::string::npos) << m.reason;
}

// --- Match and Keep's board is indexed by screen position, not by option -----
//
// The fork's `getOrderedCards()` offers the cards still ON THE BOARD and still
// FACE DOWN, sorted by SCREEN POSITION, so `choose N` names the N-th smallest
// still-offered position. The run layer's option index is the BOARD SLOT: its
// `match_menu` publishes twelve options and enables `board[i].taken == 0 &&
// scratch1 != i`. Same set of cards, two index spaces, related by
// `match_screen_position` -- and passing N through addresses an unrelated card
// while still DOING something, which is why the desync compounds instead of
// stopping. (STS00683 lost the Double Tap it matched; STS00856 a Shame.)
[[nodiscard]] ScreenInfo match_play_page(std::vector<std::string> labels) {
    ScreenInfo s;
    s.screen_type = "EVENT";
    s.event_id = "Match and Keep!";
    s.option_labels = std::move(labels);
    for (std::size_t i = 0; i < s.option_labels.size(); ++i)
        s.option_choice_index.push_back(static_cast<int>(i));
    return s;
}

// A controller parked on a live Match and Keep board with every card face down
// and nothing taken. `event.screen == 2` is the body's PLAY page
// (src/engine/events/shrines.cpp), and `scratch0` is the attempt count.
[[nodiscard]] RunController at_match_board() {
    RunController rc = at_phase(RunPhase::EVENT_DIALOG);
    rc.event.event_id = static_cast<uint16_t>(sts::registry::EventId::MATCH_AND_KEEP);
    rc.event.screen = 2;
    rc.event.scratch0 = 5;
    rc.event.scratch1 = -1;
    for (int i = 0; i < 12; ++i) {
        rc.event.board[i].card_id = static_cast<uint16_t>(i / 2 + 1);
        rc.event.board[i].upgrade = 0;
        rc.event.board[i].taken = 0;
    }
    return rc;
}

TEST(ReplayCommandMap, AMatchAndKeepPickIsTranslatedThroughTheScreenPermutation) {
    const RunController rc = at_match_board();
    // A full board offers all twelve, ascending by screen position: entry j IS
    // position j, whose board slot is match_group_index(j).
    std::vector<std::string> labels;
    for (int p = 0; p < 12; ++p) labels.push_back("card" + std::to_string(p));

    for (int n : {0, 1, 5, 10, 11}) {
        const MappedCommand m =
            map_command(rc, match_play_page(labels), "choose " + std::to_string(n));
        ASSERT_EQ(m.kind, MapKind::ACTIONS) << n << ": " << m.reason;
        ASSERT_EQ(m.actions.size(), 1u);
        EXPECT_EQ(action_arg0(m.actions[0]),
                  sts::replay::match_group_index(n))
            << "`choose " << n << "` is screen position " << n
            << ", which is board slot " << sts::replay::match_group_index(n);
    }
}

// The label re-states the answer on every face-down entry, so the alignment is
// checked at each pick rather than assumed.
TEST(ReplayCommandMap, AMatchAndKeepLabelThatContradictsTheAlignmentStops) {
    const RunController rc = at_match_board();
    std::vector<std::string> labels;
    for (int p = 0; p < 12; ++p) labels.push_back("card" + std::to_string(p));
    labels[3] = "card7";  // position 3's entry claims to be position 7
    const MappedCommand m = map_command(rc, match_play_page(labels), "choose 3");
    EXPECT_EQ(m.kind, MapKind::UNMAPPED);
    EXPECT_NE(m.reason.find("card7"), std::string::npos) << m.reason;
}

// A REVEALED card is labelled by cardID and carries no position, so it cannot
// re-state anything -- and must not be rejected for that.
TEST(ReplayCommandMap, AMatchAndKeepRevealedEntryIsAcceptedWithoutAPositionLabel) {
    const RunController rc = at_match_board();
    std::vector<std::string> labels;
    for (int p = 0; p < 12; ++p) labels.push_back("card" + std::to_string(p));
    labels[4] = "Reaper";
    const MappedCommand m = map_command(rc, match_play_page(labels), "choose 4");
    ASSERT_EQ(m.kind, MapKind::ACTIONS) << m.reason;
    EXPECT_EQ(action_arg0(m.actions[0]), sts::replay::match_group_index(4));
}

// The one-button pages of the SAME event stay on the ordinary path -- the
// translation is the board's, not the event's.
TEST(ReplayCommandMap, MatchAndKeepsOneButtonPagesAreOrdinaryEventChoices) {
    const RunController rc = at_match_board();
    const MappedCommand m = map_command(rc, match_play_page({"Continue"}), "choose 0");
    ASSERT_EQ(m.kind, MapKind::ACTIONS) << m.reason;
    ASSERT_EQ(m.actions.size(), 1u);
    EXPECT_EQ(action_arg0(m.actions[0]), 0);
}

// A capture offering a different number of cards than the sim still has face
// down is a board desync, and the ordinary path is not a safe fallback for it.
TEST(ReplayCommandMap, AMatchAndKeepListTheSimCannotMatchDoesNotTranslate) {
    RunController rc = at_match_board();
    rc.event.board[0].taken = 1;
    rc.event.board[1].taken = 1;  // the sim offers ten, the capture twelve
    std::vector<std::string> labels;
    for (int p = 0; p < 12; ++p) labels.push_back("card" + std::to_string(p));
    const MappedCommand m = map_command(rc, match_play_page(labels), "choose 3");
    // Falls back to the plain option path, which then addresses an ordinal the
    // sim's mask refuses -- but the point here is that the permutation is NOT
    // applied to a list it cannot have come from.
    ASSERT_EQ(m.kind, MapKind::ACTIONS) << m.reason;
    EXPECT_EQ(action_arg0(m.actions[0]), 3);
}

// The deferred-whole onEquip set is what the deferral message is allowed to
// name, and the list is here because a deferred surface is indistinguishable
// from an implemented one through `relic_on_equip_fn` -- both are real
// function pointers, by design. RE-DERIVED at the Wave-C integration: the
// five BOSS bodies this test used to pin as deferred are LIVE on the
// `on_equip_screen` surface (relic_pickup_boss.cpp), so they now belong on
// the FALSE side; what remains deferred whole is Dolly's Mirror (its own
// raw-deck grid) plus Orrery and Cauldron (reward-screen assembly) -- see
// `relic_on_equip_deferred`'s comment for the derivation.
TEST(ReplayCommandMap, TheDeferredOnEquipSetMatchesTheUnionTree) {
    using sts::replay::relic_on_equip_deferred;
    for (const RelicId id : {RelicId::ORRERY, RelicId::DOLLYS_MIRROR,
                             RelicId::CAULDRON})
        EXPECT_TRUE(relic_on_equip_deferred(id)) << static_cast<int>(id);
    for (const RelicId id : {RelicId::PANDORAS_BOX, RelicId::TINY_HOUSE,
                             RelicId::ASTROLABE, RelicId::EMPTY_CAGE,
                             RelicId::CALLING_BELL, RelicId::BURNING_BLOOD,
                             RelicId::NONE})
        EXPECT_FALSE(relic_on_equip_deferred(id)) << static_cast<int>(id);
}

}  // namespace
