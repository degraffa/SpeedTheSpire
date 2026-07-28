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
#include "sts/engine/run_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::replay {

using sts::engine::Action;
using sts::engine::ActionVerb;
using sts::engine::kChooseBoss;
using sts::engine::kChooseOpenChest;
using sts::engine::kChooseProceed;
using sts::engine::kChooseSing;
using sts::engine::kChooseSkipCard;
using sts::engine::kMasterDeckCap;
using sts::engine::legal_actions;
using sts::engine::make_action;
using sts::engine::RunActionMask;
using sts::engine::RunController;
using sts::engine::RunPhase;

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
    bool boss_available = false;
    std::vector<std::string> card_offer;     // CARD_REWARD: offered game ids
    std::vector<std::string> reward_types;   // COMBAT_REWARD: rewards[].reward_type
    std::vector<std::string> option_labels;  // EVENT: the dialog buttons
    std::string event_id;
    std::vector<std::string> choice_list;    // the command's own index space
    // SHOP_SCREEN only:
    bool shop_screen = false;
    std::vector<StockRow> shop_cards;    // 5 colored, then the 2 colorless
    std::vector<StockRow> shop_relics;
    std::vector<StockRow> shop_potions;
    int purge_cost = 0;
    bool purge_available = false;
};

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
    ACTIONS,    // apply `actions` in order
    NOOP,       // a pure UI command with no run-layer effect
    LEAVE_ROOM, // proceed out of the reward screen first, then apply `actions`
    TERMINAL,   // the run ended here; stop cleanly
    UNMAPPED,   // no run-layer analogue -- stop and say so
};

struct MappedCommand {
    MapKind kind = MapKind::UNMAPPED;
    std::vector<Action> actions;
    std::string reason;  // set when UNMAPPED
};

// The game's grid screens list a FILTERED master deck (getPurgeableCards drops
// curses, getUpgradableCards additionally drops already-upgraded cards) and its
// `choose i` indexes that filtered list. The run layer instead addresses the
// stable master-deck index and publishes which of those are legal, so the
// translation from one to the other is "the i-th legal master-deck index".
[[nodiscard]] inline int grid_index_to_master_deck(const RunController& rc, int filtered) {
    if (filtered < 0) return -1;
    RunActionMask m{};
    legal_actions(rc, m);
    int seen = 0;
    for (int i = 0; i < kMasterDeckCap; ++i) {
        if (!m.can_choose_master_deck[i]) continue;
        if (seen == filtered) return i;
        ++seen;
    }
    return -1;
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
                       std::to_string(static_cast<int>(rc.phase)) +
                       ", not an event dialog";
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
        if (verb == "choose") {
            const int deck_index = grid_index_to_master_deck(rc, arg(1));
            if (deck_index < 0) {
                m.reason = "grid choose index has no legal master-deck slot";
                return m;
            }
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE,
                                            static_cast<uint8_t>(deck_index)));
            return m;
        }
        if (verb == "proceed") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE, kChooseProceed));
            return m;
        }
        m.reason = "grid command '" + verb +
                   "' has no run-layer analogue (a grid pick cannot be undone)";
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
        m.reason = "map command '" + verb + "' is not modelled "
                   "(the run layer has no out-of-combat potion discard)";
        return m;
    }

    m.reason = "screen '" + s.screen_type + "' is not modelled by the run layer";
    return m;
}

}  // namespace sts::replay
