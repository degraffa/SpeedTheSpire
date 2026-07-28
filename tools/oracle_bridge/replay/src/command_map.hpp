#pragma once

// command_map.hpp -- the screen-relative CommunicationMod command -> run-layer
// Action mapping that every mode of `replay_run_diff` interprets a captured
// command through.
//
// INTERNAL header (conventions.md "Where a new header goes"): its consumers are
// this tool's `main.cpp` and its own gtest, so it lives beside the source
// rather than in the engine's published surface. It is split out of `main.cpp`
// for one reason -- the mapping is where the harness gets a captured run WRONG,
// and until it was separable the only way to see a mapping bug was to re-drive
// a whole campaign artifact by hand. Everything here is plain data plus the
// engine's public run-layer headers, so the table is directly testable; the
// JSON pass that FILLS a `ScreenInfo` stays in `main.cpp`.

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"  // MasterBottleKind (the bottle overlay)
#include "sts/engine/run_state.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"

#include "readout_shapes.hpp"

namespace sts::replay {

using sts::engine::Action;
using sts::engine::ActionVerb;
using sts::engine::EventGridKind;
using sts::engine::kChooseBoss;
using sts::engine::kChooseOpenChest;
using sts::engine::kChooseProceed;
using sts::engine::kChooseSing;
using sts::engine::kChooseSkipCard;
using sts::engine::kMasterDeckCap;
using sts::engine::legal_actions;
using sts::engine::make_action;
using sts::engine::MasterBottleKind;
using sts::engine::NeowScreen;
using sts::engine::RelicId;
using sts::engine::RestScreen;
using sts::engine::RunActionMask;
using sts::engine::RunController;
using sts::engine::RunPhase;
using sts::engine::ShopScreenKind;

// --- the screen context the translator does not carry -----------------------

// The translator's output is RunState/CombatState; the transient screen the
// command was typed at is deliberately not part of either. The tool does one
// extra light JSON pass over the same file and keeps only the few presentation
// fields the mapping consults (plus the CARD_REWARD offer, which is the
// pool-order evidence).

// One SHOP_SCREEN row exactly as the merchant presented it. `name` is the
// DISPLAY name, which is what the stock command indexes through: the game's
// `choice_list` is a lowercased list of display names, not of slot indices, so
// the join from `choose i` back to a slot goes through this field.
struct StockRow {
    std::string id;
    std::string name;
    std::string rarity;  // cards only; the sale-slot inference reads it
    int price = 0;
    int upgrades = 0;
};

struct ScreenInfo {
    std::string screen_type;
    int floor = 0;
    std::string room_type;
    std::vector<int> map_next_x;
    // The MAP screen's outgoing edges by ROOM SYMBOL (`M`/`?`/`R`/`T`/`$`/`E`).
    // Parallel to `map_next_x`. A room's node symbol is the only capture-side
    // evidence of how it was ENTERED, and that changes what the sim has to do
    // before the room content runs: a `?` node fires the onEnterRoom fan-out and
    // the one committed eventRng draw before it becomes a chest / shop / fight
    // (AbstractDungeon.nextRoomTransition :1763-1779), while a `T` node does
    // not. Both arrive as `room_type: TreasureRoom`.
    std::vector<std::string> map_next_symbol;
    bool boss_available = false;
    std::vector<std::string> card_offer;     // CARD_REWARD: offered game ids
    std::vector<std::string> reward_types;   // COMBAT_REWARD: rewards[].reward_type
    // The same reward rows with the fields a chest read-out compares (gold
    // amount, relic identity, and a SAPPHIRE_KEY row's link). `reward_types`
    // stays because the B4.5 reward mode compares kinds only; both are filled
    // from one pass.
    std::vector<CaptureRewardRow> reward_rows;
    std::vector<std::string> option_labels;  // EVENT: the dialog buttons
    std::string event_id;                    // EVENT: the class's static ID
    std::string event_name;                  // EVENT: the localized display name
    std::string chest_type;                  // CHEST: Small/Medium/LargeChest
    bool chest_open = false;                 // CHEST: screen_state.chest_open
    std::vector<std::string> choice_list;    // the command's own index space
    // SHOP_SCREEN only:
    bool shop_screen = false;
    std::vector<StockRow> shop_cards;    // 5 colored, then the 2 colorless
    std::vector<StockRow> shop_relics;
    std::vector<StockRow> shop_potions;
    int purge_cost = 0;
    bool purge_available = false;
};

// --- naming the sim's phase --------------------------------------------------

// A STOP REASON IS READ BY A HUMAN. Both reasons below used to interpolate
// `rc.phase` as a bare enum ordinal, and the cost of that is on the record: the
// obligation row filed against STS00042 of the first b45 campaign quoted "the
// sim is in 3, not an event dialog", had to gloss the integer itself ("3
// [COMBAT]") before it could state the problem, and then asked whether the stop
// was an event/combat-boundary defect -- when the sim had in fact been stuck in
// a floor-1 fight for fourteen records by then. The name does not answer that
// question either, but it stops the reader having to look up an enum before
// they can start. `main.cpp` prints the same spelling on every `DIFF` line, so
// the function lives here where both callers can reach it and the table's gtest
// can prove no ordinal is unnamed.
[[nodiscard]] inline const char* phase_name(uint8_t p) noexcept {
    switch (static_cast<RunPhase>(p)) {
        case RunPhase::NONE: return "NONE";
        case RunPhase::NEOW: return "NEOW";
        case RunPhase::MAP_CHOICE: return "MAP_CHOICE";
        case RunPhase::COMBAT: return "COMBAT";
        case RunPhase::COMBAT_REWARD: return "COMBAT_REWARD";
        case RunPhase::ROOM_UNIMPLEMENTED: return "ROOM_UNIMPLEMENTED";
        case RunPhase::RUN_OVER: return "RUN_OVER";
        case RunPhase::REST_SITE: return "REST_SITE";
        case RunPhase::TREASURE_ROOM: return "TREASURE_ROOM";
        case RunPhase::EVENT_DIALOG: return "EVENT_DIALOG";
        case RunPhase::SHOP: return "SHOP";
    }
    return "?";
}

// --- command parsing ---------------------------------------------------------

[[nodiscard]] inline std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> parts;
    std::istringstream is(s);
    std::string tok;
    while (is >> tok) parts.push_back(tok);
    return parts;
}

// What one artifact command becomes on the sim side.
enum class MapKind : uint8_t {
    ACTIONS,      // apply `actions` in order
    NOOP,         // a pure UI command with no run-layer effect
    LEAVE_ROOM,   // proceed out of the reward screen first, then apply `actions`
    TERMINAL,     // the run ended here; stop cleanly
    UNMAPPED,     // no run-layer analogue -- stop and say so
    // The three grid verbs. A grid is the one screen whose commands cannot be
    // translated one-for-one, because the game's selection is BUFFERED and the
    // run layer's is not -- see GridSession below. The caller owns the buffer;
    // the table only says which of the three a command is.
    GRID_PICK,    // select grid row `grid_index` (not yet committed)
    GRID_CANCEL,  // clear the whole pending selection
    GRID_COMMIT,  // the confirm button: apply everything pending
};

struct MappedCommand {
    MapKind kind = MapKind::UNMAPPED;
    std::vector<Action> actions;
    int grid_index = -1;  // GRID_PICK only
    std::string reason;   // set when UNMAPPED
};

// --- grid screens ------------------------------------------------------------
//
// WHY A SESSION AND NOT A MAPPING. `GridCardSelectScreen` selects on click and
// commits on a button, and the two are separated by an arbitrary number of
// further commands: a one-pick grid shows the confirm button (`choose` selects,
// `proceed` commits, `cancel` clears the selection again -- STS00047's Neow
// removal uses `choose`, `cancel`, `choose`, `proceed`, and STS00057's shop
// purge cancels twice), while a two-pick grid commits on its second `choose`
// with no button at all. The run layer has no selection stage: its CHOOSE
// removes/upgrades/transforms the card there and then, and nothing can undo it.
// So the harness buffers what the capture selected and flushes at the moment
// the capture confirms; a `cancel` simply drops the buffer, and because nothing
// was applied, nothing has to be undone. That is the whole of the `cancel` gap.
//
// The session ALSO freezes the index space at open, which is a second, separate
// correctness point: CommunicationMod's `choice_list` is the UNSHRUNK filtered
// deck, so selecting the 5th row does not renumber the 8th, whereas the sim's
// legal mask drops a picked row immediately.
struct GridSession {
    bool open = false;
    std::vector<int> filtered;  // master-deck indices, in grid order
    std::vector<int> pending;   // grid indices selected, not yet committed
};

// The grid's index space, snapshotted from the sim's legal mask at open. The
// game's grid lists a FILTERED master deck (getPurgeableCards drops curses,
// getUpgradableCards additionally drops the already-upgraded) and indexes that
// list; the run layer addresses the stable master-deck index and publishes
// which of those are legal, so row i is "the i-th legal master-deck index".
//
// ONE ordering exception: a Bottled trio grid (the pending-bottle overlay,
// RunController.pending_bottle) is built by getCardsOfType, whose addToBottom
// is a PREPEND (CardGroup.java:1052-1058 -> :459-461), so the game's grid rows
// run in REVERSE master-deck order -- unlike getPurgeableCards, whose plain
// `group.add(c)` (CardGroup.java:978-985) keeps deck order. The session
// snapshots the legal indices DESCENDING for that grid so "the game's row i"
// still maps positionally.
inline void open_grid_session(const RunController& rc, GridSession& g) {
    g.open = true;
    g.filtered.clear();
    g.pending.clear();
    RunActionMask m{};
    legal_actions(rc, m);
    if (rc.pending_bottle !=
        static_cast<uint8_t>(MasterBottleKind::NONE)) {
        for (int i = kMasterDeckCap - 1; i >= 0; --i)
            if (m.can_choose_master_deck[i]) g.filtered.push_back(i);
        return;
    }
    for (int i = 0; i < kMasterDeckCap; ++i)
        if (m.can_choose_master_deck[i]) g.filtered.push_back(i);
}

// Is the sim showing a master-deck grid right now? Every phase that has one
// gates it behind its own sub-screen field, so this is the disjunction of those
// -- the run-layer analogue of "GridCardSelectScreen is up".
[[nodiscard]] inline bool sim_grid_open(const RunController& rc) noexcept {
    // The pending-bottle overlay is a master-deck grid over ANY phase (the
    // bottle was claimed on a reward screen or bought in a shop; the game
    // parks the room INCOMPLETE under the gridSelectScreen).
    if (rc.pending_bottle != static_cast<uint8_t>(MasterBottleKind::NONE)) {
        return true;
    }
    switch (static_cast<RunPhase>(rc.phase)) {
        case RunPhase::NEOW:
            return rc.neow.screen == static_cast<uint8_t>(NeowScreen::GRID);
        case RunPhase::REST_SITE:
            // Smith and Toke are the campfire's two master-deck grids; Dream
            // Catcher's screen is a card-reward pick, not a grid.
            return rc.rest.screen == static_cast<uint8_t>(RestScreen::SMITH) ||
                   rc.rest.screen == static_cast<uint8_t>(RestScreen::TOKE);
        case RunPhase::EVENT_DIALOG:
            return rc.event.grid_kind != static_cast<uint8_t>(EventGridKind::NONE);
        case RunPhase::SHOP:
            return rc.shop.screen == static_cast<uint8_t>(ShopScreenKind::PURGE_GRID);
        default:
            return false;
    }
}

// A capture that opens a grid the sim never opened is NOT a mapping bug to be
// papered over with an index guess -- the honest outcome is a stop that names
// the most likely owner. Until Wave-C track 2 this classification had exactly
// one producer: the five BOSS `onEquip` bodies (Pandora's Box, Tiny House,
// Astrolabe, Empty Cage, Calling Bell) were deferred, so a Neow boss swap's
// grid was always a grid the sim could not open, and the message could assert
// the deferral as fact. Those five bodies are now LIVE (the sim opens
// Astrolabe's / Empty Cage's grids itself), so a stop here means either a
// STILL-deferred body in the running build (a future relic, another track's
// tree) or a real divergence upstream -- the sim took a different screen path
// than the capture. The relic named is the best available suspect, not a
// verdict; keep the positional inference honest about that.
[[nodiscard]] inline std::string unsimulated_grid_reason(const RunController& rc) {
    std::string who = "?";
    if (rc.run.relic_count > 0) {
        who = std::string(sts::registry::relic_game_id(
            static_cast<RelicId>(rc.run.relics[rc.run.relic_count - 1].relic_id)));
    }
    return "the capture opens a master-deck grid the sim never opened (sim phase " +
           std::string(phase_name(rc.phase)) +
           "): the most recently acquired relic is " + who +
           " -- either its onEquip body is deferred in this build, or the sim "
           "diverged from the capture before this grid";
}

[[nodiscard]] inline MappedCommand map_command(const RunController& rc, const ScreenInfo& s,
                                        const std::string& cmd) {
    const std::vector<std::string> p = split_ws(cmd);
    MappedCommand m;
    if (p.empty()) {
        m.reason = "empty command";
        return m;
    }
    const std::string& verb = p[0];
    auto arg = [&](std::size_t i) -> int {
        return i < p.size() ? std::stoi(p[i]) : -1;
    };

    if (verb == "__terminal_observed__") {
        m.kind = MapKind::TERMINAL;
        return m;
    }

    // `potion discard i` is SCREEN-INDEPENDENT, so it is resolved ahead of the
    // screen dispatch rather than inside one branch. The potion belt lives on
    // the top panel, which is drawn over whatever screen is up: the captures
    // issue this at a MAP (STS00049 seq 46, STS00052 seq 49) exactly as they
    // could at a shop or mid-fight, and CommandExecutor.executePotionCommand
    // never consults the screen -- it checks the slot and canDiscard, then
    // destroys the slot. The run layer mirrors that with a phase-independent
    // DISCARD_POTION dispatched ahead of its own phase switch, so one entry
    // here covers every screen. `potion use` stays in the combat branch below:
    // it is the one that needs a live target.
    if (verb == "potion" && p.size() >= 3 && p[1] == "discard") {
        m.kind = MapKind::ACTIONS;
        m.actions.push_back(make_action(ActionVerb::DISCARD_POTION,
                                        static_cast<uint8_t>(arg(2) < 0 ? 0 : arg(2))));
        return m;
    }

    if (s.screen_type == "NONE") {  // in combat
        if (verb == "play") {
            const int hand_1based = arg(1);
            const int target = p.size() >= 3 ? arg(2) : 0;
            if (hand_1based < 1) {
                m.reason = "play with no card index";
                return m;
            }
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::PLAY_CARD,
                                            static_cast<uint8_t>(hand_1based - 1),
                                            static_cast<uint8_t>(target < 0 ? 0 : target)));
            return m;
        }
        if (verb == "end") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::END_TURN));
            return m;
        }
        if (verb == "potion" && p.size() >= 3 && p[1] == "use") {
            const int slot = arg(2);
            const int target = p.size() >= 4 ? arg(3) : 0;
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::USE_POTION,
                                            static_cast<uint8_t>(slot),
                                            static_cast<uint8_t>(target < 0 ? 0 : target)));
            return m;
        }
        m.reason = "combat command '" + verb + "' has no run-layer analogue";
        return m;
    }

    if (s.screen_type == "EVENT") {
        // ONE-BUTTON PAGES. Two of them are framing the run layer does not
        // model, and they are Neow's alone:
        //
        //   [Talk]  -- NeowEvent's opening page (screenNum 0->3), no state
        //              change at all.
        //   [Leave] -- Neow's closing page (screenNum 99). Its run-layer
        //              analogue is the proceed that opens the map.
        //
        // EVERY OTHER event also ends on a one-button page, and that one is
        // NOT framing: pressing it is the event's own exit. AbstractEvent
        // .openMap (AbstractEvent.java:120-123) sets the current ROOM's phase
        // to COMPLETE and opens the map with `doScrollingAnimation == false`,
        // which is precisely what makes the map DISMISSABLE (DungeonMapScreen
        // .java:287). The event, its room and its dialog panel all stay
        // mounted -- DungeonMapScreen.close() (:316-320) hides the map and
        // nothing else -- so a map `return` drops back onto the same page and
        // the capture's random-legal policy presses [Leave] again. Each repeat
        // re-enters buttonEffect at the same screenNum and just calls openMap
        // again (FountainOfCurseRemoval.java:73-79, LivingWall.java:116-119),
        // so every press after the first is state-free.
        //
        // The run layer has exactly one door out of an event and no way back,
        // so the FIRST press must be applied and the repeats must be elided --
        // the same shape as a reward screen's `proceed`, and elided the same
        // lazy way. The discriminator is the SIM's phase, not the label: while
        // the run layer is still in EVENT_DIALOG the press is the exit; once it
        // has left for MAP_CHOICE, every later press is the bounce.
        //
        // This mapping used to treat any single-[Leave] page as Neow framing
        // and no-op it whenever the phase was not NEOW, which swallowed the
        // real exit and parked the sim in EVENT_DIALOG for the rest of the run.
        const bool single = s.option_labels.size() == 1;
        const bool in_event = rc.phase == static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
        const bool in_neow = rc.phase == static_cast<uint8_t>(RunPhase::NEOW);
        if (single && s.option_labels[0] == "Talk") {
            m.kind = MapKind::NOOP;
            return m;
        }
        if (single && s.option_labels[0] == "Leave" && in_neow) {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE, kChooseProceed));
            return m;
        }
        if (single && !in_event && !in_neow) {
            // The post-openMap panel, pressed again behind a dismissed map.
            m.kind = MapKind::NOOP;
            return m;
        }
        if (!in_event && !in_neow) {
            // A page with real choices on it while the sim is in no event is a
            // genuine desync. Stopping with a reason beats handing the CHOOSE
            // to whatever phase is live -- in MAP_CHOICE it would pick a node
            // and move the run.
            m.reason = "event command '" + verb + "' arrived while the sim is in " +
                       std::string(phase_name(rc.phase)) + ", not an event dialog";
            return m;
        }
        if (verb == "choose") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE,
                                            static_cast<uint8_t>(arg(1))));
            return m;
        }
        m.reason = "event command '" + verb + "' is not modelled";
        return m;
    }

    if (s.screen_type == "REST" || s.screen_type == "HAND_SELECT") {
        if (verb == "choose") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE,
                                            static_cast<uint8_t>(arg(1))));
            return m;
        }
        if (verb == "proceed") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE, kChooseProceed));
            return m;
        }
        m.reason = s.screen_type + " command '" + verb + "' is not modelled";
        return m;
    }

    if (s.screen_type == "GRID") {
        // FIRST, the classification that used to be missing. If the sim has no
        // grid up, the capture is driving a screen the engine never opened, and
        // the old code found that out one step later as "grid choose index has
        // no legal master-deck slot" -- a mapping-shaped message for a
        // deferred-body condition. Ask the phase instead, and name the body.
        if (!sim_grid_open(rc)) {
            m.reason = unsimulated_grid_reason(rc);
            return m;
        }
        // Otherwise the three grid verbs, all buffered by the caller's
        // GridSession (see its comment): the run layer's CHOOSE is immediate
        // and irreversible, so nothing may be applied until the capture
        // confirms, and `cancel` then costs nothing to honour.
        if (verb == "choose") {
            m.kind = MapKind::GRID_PICK;
            m.grid_index = arg(1);
            return m;
        }
        if (verb == "cancel") {
            m.kind = MapKind::GRID_CANCEL;
            return m;
        }
        if (verb == "proceed") {
            m.kind = MapKind::GRID_COMMIT;
            return m;
        }
        m.reason = "grid command '" + verb + "' is not modelled";
        return m;
    }

    if (s.screen_type == "COMBAT_REWARD") {
        if (verb == "choose") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE,
                                            static_cast<uint8_t>(arg(1))));
            return m;
        }
        if (verb == "proceed") {
            // See the header note: leaving is deferred to the map choice.
            m.kind = MapKind::NOOP;
            return m;
        }
        m.reason = "reward-screen command '" + verb + "' is not modelled";
        return m;
    }

    if (s.screen_type == "CARD_REWARD") {
        if (verb == "choose") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE,
                                            static_cast<uint8_t>(arg(1))));
            return m;
        }
        if (verb == "skip") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE, kChooseSkipCard));
            return m;
        }
        if (verb == "sing" || verb == "bowl") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE, kChooseSing));
            return m;
        }
        m.reason = "card-screen command '" + verb + "' is not modelled";
        return m;
    }

    if (s.screen_type == "CHEST") {
        if (verb == "choose") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE, kChooseOpenChest));
            return m;
        }
        if (verb == "proceed") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE, kChooseProceed));
            return m;
        }
        m.reason = "chest command '" + verb + "' is not modelled";
        return m;
    }

    if (s.screen_type == "MAP") {
        if (verb == "return") {
            m.kind = MapKind::NOOP;
            return m;
        }
        if (verb == "choose") {
            const int idx = arg(1);
            // choice_list at a map screen is next_nodes in order, with the boss
            // edge appended when it is offered.
            uint8_t dst = 0;
            if (idx >= 0 && idx < static_cast<int>(s.map_next_x.size())) {
                dst = static_cast<uint8_t>(s.map_next_x[static_cast<std::size_t>(idx)]);
            } else if (s.boss_available) {
                dst = kChooseBoss;
            } else {
                m.reason = "map choose index out of range";
                return m;
            }
            m.kind = MapKind::LEAVE_ROOM;
            m.actions.push_back(make_action(ActionVerb::CHOOSE, dst));
            return m;
        }
        m.reason = "map command '" + verb + "' is not modelled";
        return m;
    }

    m.reason = "screen '" + s.screen_type + "' is not modelled by the run layer";
    return m;
}

}  // namespace sts::replay
