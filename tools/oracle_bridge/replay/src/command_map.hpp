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

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"  // MasterBottleKind (the bottle overlay)
#include "sts/engine/run_state.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"

#include "mk_board.hpp"
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
using sts::engine::kEventOptionCap;
using sts::engine::kMasterDeckCap;
using sts::engine::kRestOptionCap;
using sts::engine::legal_actions;
using sts::engine::make_action;
using sts::engine::MasterBottleKind;
using sts::engine::NeowScreen;
using sts::engine::RelicId;
using sts::engine::RestScreen;
using sts::engine::RunActionMask;
using sts::engine::RunController;
using sts::engine::RunPhase;
using sts::engine::kChooseShopColoredBase;
using sts::engine::kChooseShopColorlessBase;
using sts::engine::kChooseShopPotionBase;
using sts::engine::kChooseShopPurge;
using sts::engine::kChooseShopRelicBase;
using sts::engine::kShopColoredCount;
using sts::engine::kShopColorlessCount;
using sts::engine::kShopPotionCount;
using sts::engine::kShopRelicCount;
using sts::engine::CardId;
using sts::engine::PotionId;
using sts::engine::ShopScreenKind;
using sts::engine::ShopSlot;

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
    // EVENT: each button's `choice_index`, parallel to `option_labels`, and -1
    // for a button that carries none.
    //
    // WHY THE TWO INDEX SPACES ARE NOT THE SAME. `screen_state.options[]` lists
    // EVERY dialog button including the DISABLED ones (a `[Locked]` row whose
    // requirement the run does not meet), and the run layer's option ordinal is
    // that same full-list position -- an event body publishes `count` buttons
    // with an `enabled[]` mask beside them, so a disabled button still occupies
    // its slot. But a `choose N` command indexes `choice_list`, which
    // CommunicationMod builds from the ENABLED buttons only, and that is
    // exactly what `choice_index` records: the position a button has in the
    // command's index space, absent when the button has none.
    //
    // Passing the capture's N straight to the engine therefore addresses the
    // wrong button on any page with a disabled row. STS00856's Golden Wing is
    // the live case: options are [Pray, Locked(disabled), Leave] with
    // choice_index [0, -, 1], the capture pressed `choose 1` = Leave, and the
    // untranslated 1 named the locked gold branch instead -- which the sim's
    // own `enabled[]` mask then refused, leaving it parked on the intro page so
    // the NEXT press (the exit page's `choose 0`) was applied to the intro
    // page's option 0 and cost the sim 7 HP the run never lost.
    std::vector<int> option_choice_index;
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

// The relics whose `onEquip` the engine defers WHOLE. Re-derived at the Wave-C
// integration: the five BOSS bodies this list used to name (Pandora's Box,
// Tiny House, Astrolabe, Empty Cage, Calling Bell) are LIVE on the
// `on_equip_screen` surface (relic_pickup_boss.cpp) -- the sim opens their
// grids itself, so naming them here would repeat the misattribution this
// function exists to prevent, one build later. What remains deferred whole
// (registry/relics.yaml provenance, each row re-read): DOLLYS_MIRROR (the only
// one whose deferred onEquip is itself a master-deck grid, DollysMirror.java:
// 33-43), and ORRERY / CAULDRON (reward-screen assembly, Orrery.java:1270-1276
// / Cauldron.java:1075-1092 -- no grid of their own, but a capture that drives
// their unmodelled reward screens desyncs and can surface here, so ruling them
// "modelled" would be false). A deferred surface cannot be told from an
// implemented one through `relic_on_equip_fn` -- it maps to a real function
// pointer either way, by design. Naming them here is therefore a list and not
// a lookup; it is short, it is pinned by `replay_command_map_test`, and the
// alternative is the misattribution below.
[[nodiscard]] inline bool relic_on_equip_deferred(RelicId id) noexcept {
    switch (id) {
        case RelicId::ORRERY:
        case RelicId::DOLLYS_MIRROR:
        case RelicId::CAULDRON:
            return true;
        default:
            return false;
    }
}

// A relic's `onEquip` runs at ACQUISITION, and the run layer acquires relics on
// exactly these phases. Inside a live combat -- or after the run is over --
// nothing can be pending an onEquip, so a grid the capture opened there is not
// a deferred body at all.
[[nodiscard]] inline bool phase_can_follow_relic_pickup(uint8_t p) noexcept {
    switch (static_cast<RunPhase>(p)) {
        case RunPhase::NEOW:
        case RunPhase::MAP_CHOICE:
        case RunPhase::COMBAT_REWARD:
        case RunPhase::TREASURE_ROOM:
        case RunPhase::EVENT_DIALOG:
        case RunPhase::SHOP:
        case RunPhase::REST_SITE:
            return true;
        case RunPhase::NONE:
        case RunPhase::COMBAT:
        case RunPhase::ROOM_UNIMPLEMENTED:
        case RunPhase::RUN_OVER:
            return false;
    }
    return false;
}

// A capture that opens a grid the sim never opened is NOT a mapping bug to be
// papered over with an index guess. It has TWO causes, and this reason must say
// which, because the two want opposite responses from the reader.
//
//   1. A DEFERRED BODY. A Neow boss-relic blessing hands over one of the five
//      relics above, its onEquip opens a transform / removal grid, and the sim
//      -- which took the relic, popped its pool and moved every stream
//      correctly -- has no grid to drive. Naming the relic is the whole point,
//      and reporting "grid index has no legal master-deck slot" instead is what
//      made this look like an index-mapping defect.
//
//   2. A DESYNC. The two sides are simply on different screens, and the sim's
//      relic list has nothing to do with it.
//
// The old text asserted (1) unconditionally, reading the last entry of
// `rc.run.relics` whether or not a relic had just been acquired and whether or
// not its onEquip was deferred at all. On a fresh Ironclad that entry is
// BURNING BLOOD -- the STARTING relic, whose onEquip is modelled -- and
// STS02009 of the G6 campaign duly stopped with "the most recently acquired
// relic is Burning Blood, whose onEquip body is deferred", of which every
// clause after the comma is false: the sim had been desynced into COMBAT since
// seq 61 and the capture opened a master-deck grid it could not follow. Same
// class of defect as the bare phase ordinal this file already fixed above: a
// stop reason is read by a human, and a confidently wrong one costs more than
// no reason at all.
[[nodiscard]] inline std::string unsimulated_grid_reason(const RunController& rc) {
    const std::string where =
        "the capture opens a master-deck grid the sim never opened (sim phase " +
        std::string(phase_name(rc.phase)) + ")";

    const bool acquirable = phase_can_follow_relic_pickup(rc.phase);
    if (acquirable && rc.run.relic_count > 0) {
        const RelicId last = static_cast<RelicId>(
            rc.run.relics[rc.run.relic_count - 1].relic_id);
        const std::string who = std::string(sts::registry::relic_game_id(last));
        if (relic_on_equip_deferred(last)) {
            return where + ": the most recently acquired relic is " + who +
                   ", whose onEquip body is deferred";
        }
        return where + ": no deferred relic onEquip explains it -- the sim's "
                       "most recent relic is " +
               who +
               ", whose onEquip is modelled, so the sim diverged from the "
               "capture before this grid (read the `first divergence:` line, "
               "not this stop)";
    }
    return where +
           ": no relic onEquip can be pending in that phase, so the two sides "
           "are on different screens (read the `first divergence:` line, not "
           "this stop)";
}

// The capture's `choose N` -> the run layer's event-option ordinal, i.e. the
// position of the button whose `choice_index` is N (see
// `ScreenInfo::option_choice_index` for why the two spaces differ). Returns -1
// when no enabled button carries that index, which is a real desync and not
// something to guess through.
//
// A screen whose options carry NO `choice_index` at all -- an artifact written
// before the fork emitted the field -- falls back to the identity, which is the
// old behaviour and is exactly right on any page with no disabled button.
[[nodiscard]] inline int event_option_ordinal(const ScreenInfo& s, int choice) noexcept {
    if (s.option_choice_index.size() != s.option_labels.size()) return choice;
    bool any = false;
    for (const int ci : s.option_choice_index)
        if (ci >= 0) any = true;
    if (!any) return choice;
    for (std::size_t i = 0; i < s.option_choice_index.size(); ++i)
        if (s.option_choice_index[i] == choice) return static_cast<int>(i);
    return -1;
}

// --- Match and Keep!'s play screen -------------------------------------------
//
// The one EVENT page whose `choose N` is not an option ordinal at all.
//
// `GremlinMatchGame` puts its twelve cards on a 4-wide grid, and the fork's
// `getOrderedCards()` offers the cards that are still ON THE BOARD and still
// FACE DOWN, sorted by SCREEN POSITION -- so a `choose N` names the N-th
// smallest still-offered position, in a list that shrinks as the walk goes on.
// The run layer's option index is the BOARD SLOT (`cards.group` index): its
// `match_menu` publishes twelve options and enables `board[i].taken == 0 &&
// scratch1 != i`, which is exactly the same SET of cards in a different index
// space, related by `mk_board.hpp`'s `match_screen_position` permutation.
//
// Passing N straight through therefore picks an unrelated card -- and worse, it
// silently DOES something rather than stopping: the wrong flip either resolves
// a pair the capture never attempted or is refused outright, which does not
// decrement `attemptCount`, so the sim's walk desynchronises from the capture's
// and the event never ends. STS00683 loses the Double Tap it matched
// (`master_deck_count: 11 -> 10` from seq 32 to the run terminal), and STS00856
// -- which only reaches its floor-3 Match and Keep once the Golden Wing
// `choice_index` fix above lets it get there -- loses a Shame and is still
// parked in `EVENT_DIALOG` when the capture is two screens further on.
//
// The translation needs no cross-record state: the sim's own enabled set IS the
// offered set, so sorting it by screen position reproduces `getOrderedCards()`
// exactly. Every entry the capture labels `card<position>` then re-states the
// answer, which is checked rather than assumed -- the same discipline
// `decode_match_grid` applies to the whole walk.
struct MatchPlayScreen {
    bool live = false;
    std::vector<int> offered;  // board slots, ordered by screen position
};

[[nodiscard]] inline MatchPlayScreen match_play_screen(const RunController& rc,
                                                       const ScreenInfo& s) {
    MatchPlayScreen p;
    if (rc.phase != static_cast<uint8_t>(RunPhase::EVENT_DIALOG)) return p;
    if (sts::registry::event_from_game_id(s.event_id) !=
        sts::registry::EventId::MATCH_AND_KEEP)
        return p;
    // The intro / rules / done pages are one-button pages and take the ordinary
    // path; only the board itself offers more than one card.
    if (s.option_labels.size() < 2) return p;

    RunActionMask m{};
    legal_actions(rc, m);
    std::vector<int> slots;
    for (int i = 0; i < kEventOptionCap && i < kMatchBoardSlots; ++i)
        if (m.can_choose_event_option[i]) slots.push_back(i);
    if (slots.size() != s.option_labels.size()) return p;

    std::sort(slots.begin(), slots.end(), [](int a, int b) {
        return match_screen_position(a) < match_screen_position(b);
    });
    p.live = true;
    p.offered = std::move(slots);
    return p;
}

// --- the merchant ------------------------------------------------------------
//
// A shop visit arrives as TWO screen types, and the split is the game's, not
// the fork's. `SHOP_ROOM` is the room with the merchant standing in it: its
// whole `choice_list` is `["shop"]`, `choose 0` walks up to him and `proceed`
// opens the map. `SHOP_SCREEN` is the stock itself, where `choose i` buys and
// `leave` steps back into the room. (Verified over the 260 shop records in the
// artifact corpus: SHOP_ROOM carries only those two verbs and only that
// choice_list; SHOP_SCREEN carries `choose`, `leave`, and the belt's
// screen-independent `potion discard`.)
//
// THE RUN LAYER HAS ONE STATE FOR BOTH. Entering a ShopRoom puts the controller
// straight into `RunPhase::SHOP` / `ShopScreenKind::MENU` -- there is no
// walk-up-to-the-merchant step to model, because nothing about the game's is
// stateful. So the room's `choose` is a NOOP and the screen's `leave` is a
// NOOP; the only two commands that move the run layer are the room's `proceed`
// (which is the menu's `kChooseProceed`) and the screen's `choose` (a purchase
// or the purge service). The purge grid that a purge opens is an ordinary
// `GRID` screen and is handled by the GRID branch, whose `sim_grid_open`
// already knows about `ShopScreenKind::PURGE_GRID`.

[[nodiscard]] inline std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return s;
}

enum class ShopPick : uint8_t { NONE, COLORED, COLORLESS, RELIC, POTION, PURGE };

struct ShopTarget {
    ShopPick what = ShopPick::NONE;
    uint8_t index = 0;   // position in the CAPTURE's stock array
    std::string id;      // the capture row's game id; empty for PURGE / NONE
};

// Which shop row a `choose i` names, resolved through the capture's own
// choice_list: the game lists the AFFORDABLE unsold rows by DISPLAY name, so
// the index means nothing without it.
[[nodiscard]] inline ShopTarget resolve_shop_choice(const ScreenInfo& s, int choice) {
    ShopTarget t;
    if (choice < 0 || choice >= static_cast<int>(s.choice_list.size())) return t;
    const std::string want = lower(s.choice_list[static_cast<std::size_t>(choice)]);
    if (want == "purge") {
        t.what = ShopPick::PURGE;
        return t;
    }
    for (std::size_t i = 0; i < s.shop_cards.size(); ++i) {
        if (lower(s.shop_cards[i].name) != want) continue;
        t.what = i < static_cast<std::size_t>(kShopColoredCount) ? ShopPick::COLORED
                                                                 : ShopPick::COLORLESS;
        t.index = static_cast<uint8_t>(
            i < static_cast<std::size_t>(kShopColoredCount)
                ? i
                : i - static_cast<std::size_t>(kShopColoredCount));
        t.id = s.shop_cards[i].id;
        return t;
    }
    for (std::size_t i = 0; i < s.shop_relics.size(); ++i) {
        if (lower(s.shop_relics[i].name) != want) continue;
        t.what = ShopPick::RELIC;
        t.index = static_cast<uint8_t>(i);
        t.id = s.shop_relics[i].id;
        return t;
    }
    for (std::size_t i = 0; i < s.shop_potions.size(); ++i) {
        if (lower(s.shop_potions[i].name) != want) continue;
        t.what = ShopPick::POTION;
        t.index = static_cast<uint8_t>(i);
        t.id = s.shop_potions[i].id;
        return t;
    }
    return t;
}

// The run layer's dense CHOOSE ordinal for a resolved shop row, or -1 with a
// reason.
//
// WHY THE CAPTURE'S POSITION IS NOT THE ANSWER. The game DELETES a bought row
// from its shop screen (`ShopScreen` removes the purchased item), so the second
// purchase in a visit is at a different array position than the same item had
// before the first; the run layer, by contrast, keeps every slot at a FIXED
// index for the whole visit and just marks it sold (`ShopSlot::sold`; see the
// `kChooseShop*Base` note in run_advance.hpp on why slots are not compacted).
// Handing the capture's position straight to the engine therefore buys the
// WRONG ITEM after the first purchase -- silently, since the ordinal is legal.
//
// So the join goes through IDENTITY: the display name picks a row out of the
// capture's stock, that row's game id is matched against the sim's unsold slots
// in the same group, and the slot's fixed index is the ordinal. The `sold`
// filter is what makes a duplicated identity (two of the same potion on the
// shelf) resolve to the one still for sale.
[[nodiscard]] inline int shop_choose_ordinal(const RunController& rc, const ScreenInfo& s,
                                             int choice, std::string& why) {
    const ShopTarget t = resolve_shop_choice(s, choice);
    const std::string named =
        (choice >= 0 && choice < static_cast<int>(s.choice_list.size()))
            ? s.choice_list[static_cast<std::size_t>(choice)]
            : std::string("<index " + std::to_string(choice) + " off the list>");

    if (t.what == ShopPick::PURGE) return kChooseShopPurge;
    if (t.what == ShopPick::NONE) {
        why = "shop `choose " + std::to_string(choice) + "` names \"" + named +
              "\", which is on none of the merchant's three shelves in this "
              "record (" + std::to_string(s.shop_cards.size()) + " card, " +
              std::to_string(s.shop_relics.size()) + " relic, " +
              std::to_string(s.shop_potions.size()) + " potion rows)";
        return -1;
    }

    const ShopSlot* group = nullptr;
    int count = 0;
    int base = 0;
    switch (t.what) {
        case ShopPick::COLORED:
            group = rc.shop.colored; count = kShopColoredCount;
            base = kChooseShopColoredBase; break;
        case ShopPick::COLORLESS:
            group = rc.shop.colorless; count = kShopColorlessCount;
            base = kChooseShopColorlessBase; break;
        case ShopPick::RELIC:
            group = rc.shop.relics; count = kShopRelicCount;
            base = kChooseShopRelicBase; break;
        case ShopPick::POTION:
            group = rc.shop.potions; count = kShopPotionCount;
            base = kChooseShopPotionBase; break;
        default:
            break;
    }

    for (int i = 0; i < count; ++i) {
        if (group[i].sold) continue;
        std::string sim_id;
        switch (t.what) {
            case ShopPick::COLORED:
            case ShopPick::COLORLESS:
                sim_id = sts::registry::card_game_id(static_cast<CardId>(group[i].id));
                break;
            case ShopPick::RELIC:
                sim_id = sts::registry::relic_game_id(static_cast<RelicId>(group[i].id));
                break;
            case ShopPick::POTION:
                sim_id = sts::registry::potion_game_id(static_cast<PotionId>(group[i].id));
                break;
            default:
                break;
        }
        if (sim_id == t.id) return base + i;
    }

    why = "shop `choose " + std::to_string(choice) + "` names \"" + named +
          "\" (game id \"" + t.id +
          "\"), which the sim's merchant has no UNSOLD slot for -- the two "
          "stocks disagree, or the row was already bought sim-side";
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
            // Match and Keep's board is the one page whose index space is
            // SCREEN POSITIONS rather than option ordinals -- see
            // `match_play_screen`.
            if (const MatchPlayScreen play = match_play_screen(rc, s); play.live) {
                const int n = arg(1);
                if (n < 0 || n >= static_cast<int>(play.offered.size())) {
                    m.reason = "Match and Keep `choose " + std::to_string(n) +
                               "` is off the " +
                               std::to_string(play.offered.size()) +
                               "-card list the sim still has face down";
                    return m;
                }
                const int slot = play.offered[static_cast<std::size_t>(n)];
                const int pos = match_screen_position(slot);
                // A face-down card labels itself `card<screen position>`, so the
                // reconstruction is checked at every pick rather than assumed.
                const std::string& label = s.option_labels[static_cast<std::size_t>(n)];
                if (label == "card" + std::to_string(pos) ||
                    label.rfind("card", 0) != 0) {
                    m.kind = MapKind::ACTIONS;
                    m.actions.push_back(
                        make_action(ActionVerb::CHOOSE, static_cast<uint8_t>(slot)));
                    return m;
                }
                m.reason = "Match and Keep entry " + std::to_string(n) +
                           " is labelled \"" + label +
                           "\" but the sim's still-face-down cards, sorted by "
                           "screen position, put position " + std::to_string(pos) +
                           " (board slot " + std::to_string(slot) +
                           ") there; the two boards disagree";
                return m;
            }
            // `choice_list` -> the sim's option ordinal. The two differ exactly
            // when the page carries a disabled button; see
            // `ScreenInfo::option_choice_index`.
            const int ordinal = event_option_ordinal(s, arg(1));
            if (ordinal < 0) {
                m.reason = "event `choose " + std::to_string(arg(1)) +
                           "` names no enabled option on a page of " +
                           std::to_string(s.option_labels.size()) +
                           " button(s); `choice_index` is the command's index "
                           "space and no button carries that value";
                return m;
            }
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE,
                                            static_cast<uint8_t>(ordinal)));
            return m;
        }
        m.reason = "event command '" + verb + "' is not modelled";
        return m;
    }

    if (s.screen_type == "REST") {
        const bool in_rest = rc.phase == static_cast<uint8_t>(RunPhase::REST_SITE);
        if (verb == "choose") {
            const int i = arg(1);
            // THE CAMPFIRE'S MENU CAN BE LONGER THAN THE SIM'S. `build_rest_menu`
            // (rest_sites.cpp) deliberately omits `RecallOption`
            // (CampfireUI.java:94-96) -- it is the Ruby Key button, appended
            // after the veto sweep, and the run layer has no Act-4 key content.
            // The capture profile HAS the final act available, so every captured
            // rest site lists `["rest", "smith", "recall"]` and a capture that
            // presses index 2 is naming a button the sim does not have.
            //
            // Left unchecked, `CHOOSE 2` is simply illegal at the run layer and
            // therefore a NO-OP -- the sim silently stays parked on the campfire
            // while the capture walks on, and the first evidence is a `floor`
            // field a dozen records later with no hint of what caused it. That
            // is exactly what STS00052 (seq 78) and STS00054 (seq 122) did.
            // Ask the mask instead and name the button.
            if (in_rest) {
                RunActionMask mask{};
                legal_actions(rc, mask);
                if (i < 0 || i >= kRestOptionCap || !mask.can_choose_rest[i]) {
                    m.reason = "rest `choose " + std::to_string(i) +
                               "` is not a campfire option the sim offers; the "
                               "run layer models Rest/Smith plus the Girya, "
                               "Peace Pipe and Shovel buttons, and DEFERS "
                               "RecallOption (the Ruby Key button, "
                               "rest_sites.cpp `build_rest_menu`), which every "
                               "capture from this profile lists third";
                    return m;
                }
            }
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE,
                                            static_cast<uint8_t>(i)));
            return m;
        }
        if (verb == "proceed") {
            // The campfire's proceed opens the map -- and repeats, exactly like
            // an event's [Leave] page and a shop room's proceed. The map opens
            // over the still-mounted rest room, a map `return` dismisses it, and
            // the capture's policy presses proceed again (STS00052 seq 79/81/83,
            // STS00054 seq 110/112/114). One door out, no way back: the FIRST
            // press is the exit and the rest are UI bounces, discriminated on
            // the SIM's phase.
            if (!in_rest &&
                rc.phase == static_cast<uint8_t>(RunPhase::MAP_CHOICE)) {
                m.kind = MapKind::NOOP;
                return m;
            }
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE, kChooseProceed));
            return m;
        }
        m.reason = "REST command '" + verb + "' is not modelled";
        return m;
    }

    if (s.screen_type == "HAND_SELECT") {
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
        m.reason = "HAND_SELECT command '" + verb + "' is not modelled";
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
            // NEOW'S THREE-POTION PAYOUT IS NOT A COMBAT-REWARD ROOM, and the
            // lazy-leave convention below is wrong for it. Discriminate on the
            // SIM's phase, the same way the EVENT branch above separates an
            // event's real exit from its UI bounce -- the screen LABEL is
            // `COMBAT_REWARD` in both cases and says nothing.
            //
            // `NeowRewardType::THREE_SMALL_POTIONS` delivers through
            // `combatRewardScreen.open()` inside the NeowRoom (NeowReward.java:
            // 268-283; the engine's neow.cpp mirror sets
            // `NeowScreen::ITEM_REWARD`). One press of that screen's Proceed
            // takes the game all the way to the map: closing the screen leaves
            // NeowEvent already at screenNum 99 with `NeowRoom.update` having
            // set the room COMPLETE, so the [Leave] page every OTHER blessing
            // shows is never rendered. Both affected captures record exactly
            // that -- STS00283 and STS00700 go COMBAT_REWARD `proceed` (seq 5)
            // straight to a `MAP` (seq 6), with no EVENT page between.
            //
            // The run layer spends that one press over TWO CHOOSEs, because it
            // models the two states the game passed through in one frame:
            // `ITEM_REWARD` + kChooseProceed closes the payout screen
            // (run_advance.cpp:1478-1489 -> neow_finish_payout -> `DONE`), and
            // the `DONE` press is what sets `RunPhase::MAP_CHOICE` (:1490-1494).
            // Neither consumes RNG, so the pair is state-equivalent to what the
            // game did. Mapping it to NOOP instead left the sim in `NEOW`
            // forever: the following map `choose` became LEAVE_ROOM, and its
            // CHOOSE(dst) fell into `ITEM_REWARD`'s `else` branch as
            // `claim_reward(dst)`.
            if (rc.phase == static_cast<uint8_t>(RunPhase::NEOW) &&
                rc.neow.screen == static_cast<uint8_t>(NeowScreen::ITEM_REWARD)) {
                m.kind = MapKind::ACTIONS;
                m.actions.push_back(make_action(ActionVerb::CHOOSE, kChooseProceed));
                m.actions.push_back(make_action(ActionVerb::CHOOSE, kChooseProceed));
                return m;
            }
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

    if (s.screen_type == "SHOP_ROOM") {
        const bool in_shop = rc.phase == static_cast<uint8_t>(RunPhase::SHOP);
        if (verb == "choose") {
            // Walking up to the merchant. `choice_list` is `["shop"]`, and the
            // run layer put the controller on the shop MENU when it entered the
            // room, so this press moves nothing -- at any phase, including
            // after the visit is over (see `proceed` below for how a capture
            // gets back here). The label IS checked rather than assumed: a
            // SHOP_ROOM offering something other than the merchant would be a
            // screen this mapping has never seen, and silently no-opping it
            // would hide that.
            const int i = arg(1);
            if (i >= 0 && i < static_cast<int>(s.choice_list.size()) &&
                lower(s.choice_list[static_cast<std::size_t>(i)]) != "shop") {
                m.reason = "shop-room `choose " + std::to_string(i) + "` names \"" +
                           s.choice_list[static_cast<std::size_t>(i)] +
                           "\", not the merchant";
                return m;
            }
            m.kind = MapKind::NOOP;
            return m;
        }
        if (verb == "proceed") {
            // ShopRoom's proceed opens the map, which is exactly what
            // kChooseProceed on the shop menu does (`can_proceed` on
            // RunPhase::SHOP, run_advance.hpp).
            //
            // AND IT REPEATS, for the same reason an event's [Leave] page does.
            // The map opens over the still-mounted room; a map `return`
            // dismisses it and drops the capture back onto the SHOP_ROOM, whose
            // proceed then re-opens the same map. STS00052 does exactly that at
            // seq 48/50/51 and STS00054 at 102/104/105. The run layer has one
            // door out of a shop and no way back, so the FIRST press is the
            // exit and every later one is a UI bounce -- discriminated on the
            // SIM's phase, never on the record, precisely as the EVENT branch
            // separates a real exit from its bounce.
            if (in_shop) {
                m.kind = MapKind::ACTIONS;
                m.actions.push_back(make_action(ActionVerb::CHOOSE, kChooseProceed));
                return m;
            }
            if (rc.phase == static_cast<uint8_t>(RunPhase::MAP_CHOICE)) {
                m.kind = MapKind::NOOP;
                return m;
            }
            m.reason = "shop-room `proceed` arrived while the sim is in " +
                       std::string(phase_name(rc.phase)) +
                       ", neither a shop nor the map it opens";
            return m;
        }
        m.reason = "shop-room command '" + verb + "' is not modelled";
        return m;
    }

    if (s.screen_type == "SHOP_SCREEN") {
        if (verb == "leave") {
            // Back out to the room. The run layer never left the menu, and the
            // press that ends the visit is the room's `proceed`. Phase-free for
            // the same reason the room's `choose` is: it moves nothing.
            m.kind = MapKind::NOOP;
            return m;
        }
        if (verb == "choose") {
            // A purchase, and the one merchant command that MOVES the run. It
            // must not reach any other phase: `CHOOSE i` means a shop row here,
            // a map node in MAP_CHOICE and a reward row in COMBAT_REWARD, so a
            // desync would spend the run rather than stop it.
            if (rc.phase != static_cast<uint8_t>(RunPhase::SHOP)) {
                m.reason = "shop `choose` arrived while the sim is in " +
                           std::string(phase_name(rc.phase)) + ", not a shop";
                return m;
            }
            std::string why;
            const int ordinal = shop_choose_ordinal(rc, s, arg(1), why);
            if (ordinal < 0) {
                m.reason = why;
                return m;
            }
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE,
                                            static_cast<uint8_t>(ordinal)));
            return m;
        }
        m.reason = "shop command '" + verb + "' is not modelled";
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
