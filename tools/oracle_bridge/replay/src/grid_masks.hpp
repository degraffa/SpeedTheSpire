#pragma once

// grid_masks.hpp -- the GRID-SCREEN LEGAL-ACTION MASK compare (S3.53 (b)),
// behind `replay_run_diff --masks`.
//
// INTERNAL header (conventions.md "Where a new header goes"): its only consumer
// is this tool's `main.cpp`, so it lives beside the source. It is split out for
// the same reason `command_map.hpp` is -- the index-space mappings it honours
// are exactly where a harness can silently compare the wrong two lists.
//
// THE GAP THIS CLOSES. Every card/relic grid the game shows is chosen from a
// CANDIDATE LIST the game itself builds, and CommunicationMod dumps that list
// verbatim: `ChoiceScreenUtils.getGridScreenCards()` -> `screen_state.cards`
// (GameStateConverter:554-559) and `AbstractDungeon.bossRelicScreen.relics` ->
// `screen_state.relics` (:494-500). The engine builds its own candidate list as
// a legal-action MASK (`RunActionMask::can_choose_master_deck[]`,
// `can_claim_reward[]`). Until this compare, nothing in the repository ever put
// the two side by side: `--replay` compares RunState, which the mask is not
// part of, and the command mapping only ever resolves the ONE row a capture
// happened to press -- so a mask that offered the wrong SET, or the right set
// in the wrong ORDER, was invisible on every row the capture did not click.
// (The Library GRID identity and the Match-and-Keep index-space findings of the
// S2 depth wave are both that shape, found by hand.)
//
// WHAT IS COMPARED, per screen kind. Each kind names its index space, because
// the whole point is that they are not the same space:
//
//   MASTER_DECK -- the run-layer grids: Neow's removal/transform/upgrade grid,
//       the campfire's Smith and Toke, an event's grid (`EventGridKind`), the
//       shop's purge grid, the boss chest's onEquip grid, and the two
//       phase-independent overlays (a pending Bottled trio, Dolly's Mirror).
//       The game lists a FILTERED master deck and indexes THAT list;
//       the run layer addresses the stable master-deck index and publishes
//       which of them are legal, so row i is "the i-th legal master-deck
//       index" -- ASCENDING, except under a pending bottle, whose group
//       getCardsOfType builds with a prepending addToBottom
//       (CardGroup.java:1052-1058 -> :459-461) and whose rows therefore run in
//       REVERSE master-deck order. That is `open_grid_session`'s rule, and it
//       is reused here rather than restated. The compare is POSITIONAL on
//       (card id, upgrades): the set alone would not catch an order defect,
//       and order is what a captured `choose i` spends.
//
//   CONFIRM -- a display grid with nothing to pick: `isJustForConfirming`,
//       raised by `openConfirmationGrid` for Pandora's Box's transform results
//       and Calling Bell's relics, where `getGridScreenChoices` returns an
//       EMPTY list (ChoiceScreenUtils.java:460-469) even though `cards` is
//       full. The claim compared is exactly the mask's: the sim must offer NO
//       master-deck row and must offer Proceed. The displayed cards are the
//       transform results, not deck rows, and `--replay` compares them as
//       master deck the moment the confirm commits, so they are not
//       re-compared here.
//
//       IT IS NOT THE SAME SCREEN AS `confirmScreenUp`, even though the
//       protocol folds both into one `confirm_up` key -- that conflation cost
//       this compare its first draft, which called every mid-selection Smith
//       grid a display grid and reported "0 rows offered -> 16" on all of
//       them. A `confirmScreenUp` grid is an ordinary MASTER_DECK grid with a
//       pick already made: `targetGroup` is untouched, so its `cards` is still
//       the whole candidate list and the positional compare below is exactly
//       right. What it is NOT is a place to check `getGridScreenChoices`'s
//       emptiness against the sim's mask, because the harness deliberately
//       holds a grid selection OUTSIDE the simulator until the capture
//       confirms (command_map.hpp `GridSession`) -- so the sim's rows are
//       still all live by design, and comparing button state across that seam
//       would report the harness's own buffering as an engine defect.
//       `ScreenInfo::grid_just_confirming` is the discriminator.
//
//   COMBAT_PILE -- Headbutt / Exhume / Secret Technique / Secret Weapon put
//       GridCardSelectScreen over a COMBAT pile. The candidate list is the
//       source pile filtered by the action's predicate, which is the engine's
//       own `choice_slot_eligible`. Compared as a MULTISET of (card id,
//       upgrades), not positionally, for a stated reason: a draw-pile grid's
//       rows are a temporary browse group built with addToRandomSpot, so its
//       ORDER is an RNG artifact the differ reconstructs only when it has to
//       map a press (command_map.hpp's rewind). The membership question -- did
//       the engine offer the same cards the game did -- needs no such rebuild.
//       On a MULTI-PICK grid the two lists legitimately differ in length: the
//       game keeps every row on screen and tracks picks in `selectedCards`,
//       while the engine applies each CHOOSE as it is made and its source pile
//       shrinks (the S3.23 two-index-spaces note). So a sim list SHORTER than
//       the capture's is judged by CONTAINMENT -- every remaining sim row must
//       still be one the capture offered -- and a sim list LONGER is always a
//       divergence.
//
//   LIBRARY_BOARD -- The Library hosts its twenty-card one-pick on
//       GridCardSelectScreen, so the capture says GRID, but the run layer
//       models it as the event's BOARD of twenty ordinary options with no
//       `EventGridKind`. The game builds the group with addToBottom in roll
//       order and addToBottom PREPENDS, so the grid runs in REVERSE roll order
//       while the sim's board is roll order. Compared positionally under that
//       reversal -- the same mapping `command_map.hpp` uses to resolve the
//       press, now checked on all twenty rows instead of the one pressed.
//
//   BOSS_RELIC -- `bossRelicScreen.relics` against the three
//       `can_claim_reward[]` rows the BOSS_TREASURE / RELIC_SELECT arm
//       publishes, positionally on relic id against `RunState::boss_chest
//       .relics[]` (schema v8, S2.47). The offers themselves are already in
//       RunState and therefore already in `--replay`'s field compare; what is
//       new here is the MASK -- that all three are offered, and that the same
//       three the game shows are the ones the engine will accept a claim for.
//
// WHAT IS DELIBERATELY NOT JUDGED. A record where the sim has no corresponding
// screen open at all is classified `UNPAIRED` and counted, never reported as a
// mask divergence: the two sides are simply on different screens, which is a
// desync the run-level `--replay` compare owns and names (see
// `unsimulated_grid_reason`). Turning that into a mask row would report the
// same fact twice under a name that misattributes it.

#include <algorithm>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/registry/game_ids.hpp"

#include "command_map.hpp"

namespace sts::replay {

// Which candidate list this record's screen presents. `NONE` means the record
// shows no card/relic grid at all; `UNPAIRED` means it does but the sim is not
// on the corresponding screen (counted, never judged -- see the header).
enum class MaskKind : uint8_t {
    NONE,
    MASTER_DECK,
    CONFIRM,
    COMBAT_PILE,
    LIBRARY_BOARD,
    BOSS_RELIC,
    UNPAIRED,
};

[[nodiscard]] inline const char* mask_kind_name(MaskKind k) noexcept {
    switch (k) {
        case MaskKind::NONE: return "none";
        case MaskKind::MASTER_DECK: return "master-deck grid";
        case MaskKind::CONFIRM: return "confirmation grid";
        case MaskKind::COMBAT_PILE: return "combat-pile grid";
        case MaskKind::LIBRARY_BOARD: return "Library board";
        case MaskKind::BOSS_RELIC: return "boss-relic screen";
        case MaskKind::UNPAIRED: return "unpaired";
    }
    return "?";
}

// One differing row, rendered GAME -> SIM like `sts::diff::FieldDiff` and
// `sts::translate::VitalsFieldDiff`.
struct MaskFieldDiff {
    std::string field;
    std::string game;
    std::string sim;
};

struct MaskReport {
    MaskKind kind = MaskKind::NONE;
    std::vector<MaskFieldDiff> diffs;

    [[nodiscard]] bool empty() const noexcept { return diffs.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return diffs.size(); }
    [[nodiscard]] std::string to_string() const {
        std::ostringstream os;
        for (const MaskFieldDiff& d : diffs)
            os << d.field << ": " << d.game << " -> " << d.sim << "\n";
        return os.str();
    }
};

namespace mask_detail {

inline void push(MaskReport& r, std::string field, std::string game,
                 std::string sim) {
    r.diffs.push_back(
        MaskFieldDiff{std::move(field), std::move(game), std::move(sim)});
}

// The card key the vitals compare prints, so a mask row and a vitals row name
// the same card the same way.
[[nodiscard]] inline std::string card_key(const std::string& id, int upgrades) {
    std::string k = id.empty() ? "<none>" : id;
    if (upgrades == 1) k += "+";
    else if (upgrades > 1) k += "+" + std::to_string(upgrades);
    return k;
}

[[nodiscard]] inline std::string capture_key(const ScreenInfo& s,
                                             std::size_t i) {
    const int up = i < s.card_offer_upgrades.size()
                       ? s.card_offer_upgrades[i] : 0;
    return card_key(s.card_offer[i], up);
}

[[nodiscard]] inline std::string sim_card_key(const sts::engine::CardInstance& c) {
    const auto id = static_cast<CardId>(c.card_id);
    return card_key(std::string(sts::registry::card_game_id(id)),
                    static_cast<int>(c.upgrade));
}

// The sim's ordered master-deck grid rows -- exactly `open_grid_session`'s
// index space, reused rather than restated so the two can never drift.
[[nodiscard]] inline std::vector<int> sim_grid_rows(const RunController& rc) {
    GridSession g;
    open_grid_session(rc, g);
    return g.filtered;
}

[[nodiscard]] inline std::string join_keys(const std::vector<std::string>& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) s += "|";
        s += v[i];
    }
    return s.empty() ? "(empty)" : s;
}

// Compare two key lists as multisets, reporting one row per key whose counts
// differ. `sim_subset_ok` is the multi-pick combat grid rule: a key the SIM has
// fewer of is the already-applied pick and is not judged.
inline void cmp_multiset(MaskReport& r, const char* prefix,
                         const std::vector<std::string>& game,
                         const std::vector<std::string>& sim,
                         bool sim_subset_ok) {
    std::map<std::string, int> g;
    std::map<std::string, int> s;
    for (const std::string& k : game) ++g[k];
    for (const std::string& k : sim) ++s[k];
    std::vector<std::string> keys;
    for (const auto& [k, v] : g) keys.push_back(k);
    for (const auto& [k, v] : s) {
        if (g.find(k) == g.end()) keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());
    for (const std::string& k : keys) {
        const auto gi = g.find(k);
        const auto si = s.find(k);
        const int gc = gi == g.end() ? 0 : gi->second;
        const int sc = si == s.end() ? 0 : si->second;
        if (gc == sc) continue;
        if (sim_subset_ok && sc < gc) continue;
        push(r, std::string(prefix) + "[" + k + "]", std::to_string(gc),
             std::to_string(sc));
    }
}

// The capture's grid rows as keys.
[[nodiscard]] inline std::vector<std::string> capture_keys(const ScreenInfo& s) {
    std::vector<std::string> out;
    out.reserve(s.card_offer.size());
    for (std::size_t i = 0; i < s.card_offer.size(); ++i)
        out.push_back(capture_key(s, i));
    return out;
}

// Is the sim showing one of the combat-pile grids (Headbutt / Exhume / Secret
// Technique / Secret Weapon)? Same test `command_map.hpp`'s GRID arm makes.
[[nodiscard]] inline bool sim_combat_pile_grid(const RunController& rc,
                                               ActionMask& cm) noexcept {
    if (rc.phase != static_cast<uint8_t>(RunPhase::COMBAT)) return false;
    legal_actions(rc.combat, cm);
    return cm.choice_pending &&
           (cm.choice_from_discard || cm.choice_from_exhaust ||
            cm.choice_from_draw);
}

}  // namespace mask_detail

// Compare the engine's candidate-list mask against the capture's for ONE
// record. `s` is that record's screen (main.cpp's JSON pass), `rc` the
// simulator standing at the same record. A `MaskReport` with kind NONE or
// UNPAIRED carries no rows by construction; every other kind carries the rows
// the two lists disagree on.
[[nodiscard]] inline MaskReport compare_grid_mask(const RunController& rc,
                                                  const ScreenInfo& s) {
    using namespace mask_detail;
    MaskReport r;

    // ---- the boss-relic screen -------------------------------------------
    if (s.screen_type == "BOSS_REWARD") {
        const bool sim_on_screen =
            rc.phase == static_cast<uint8_t>(RunPhase::BOSS_TREASURE) &&
            rc.run.boss_chest.screen ==
                static_cast<uint8_t>(BossChestScreen::RELIC_SELECT);
        if (!sim_on_screen) {
            r.kind = MaskKind::UNPAIRED;
            return r;
        }
        r.kind = MaskKind::BOSS_RELIC;
        RunActionMask m{};
        legal_actions(rc, m);
        std::vector<int> rows;
        for (int i = 0; i < static_cast<int>(sts::engine::kRewardItemCap); ++i)
            if (m.can_claim_reward[i]) rows.push_back(i);
        if (rows.size() != s.boss_relic_offer.size()) {
            push(r, "boss_relic.rows",
                 std::to_string(s.boss_relic_offer.size()),
                 std::to_string(rows.size()));
            return r;
        }
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto slot = static_cast<std::size_t>(rows[i]);
            const std::string sim_id =
                slot < static_cast<std::size_t>(kBossChestOfferCount)
                    ? std::string(sts::registry::relic_game_id(
                          static_cast<RelicId>(rc.run.boss_chest.relics[slot])))
                    : std::string("<off-chest row ") + std::to_string(rows[i]) +
                          ">";
            if (sim_id != s.boss_relic_offer[i]) {
                push(r, "boss_relic[" + std::to_string(i) + "]",
                     s.boss_relic_offer[i], sim_id);
            }
        }
        return r;
    }

    if (s.screen_type != "GRID") return r;  // kind NONE

    // ---- a display-only confirmation grid: nothing is selectable -----------
    if (s.grid_just_confirming) {
        // The engine's answer to a confirm-up grid is a Proceed and no rows.
        // It may be reached through the Neow arm or the boss chest's re-homed
        // one; `sim_choice_free_confirmation_grid` is the disjunction.
        if (!sim_grid_open(rc)) {
            r.kind = MaskKind::UNPAIRED;
            return r;
        }
        r.kind = MaskKind::CONFIRM;
        RunActionMask m{};
        legal_actions(rc, m);
        int rows = 0;
        for (int i = 0; i < kMasterDeckCap; ++i)
            if (m.can_choose_master_deck[i]) ++rows;
        if (rows != 0) push(r, "confirm.rows", "0", std::to_string(rows));
        if (!m.can_proceed) push(r, "confirm.can_proceed", "true", "false");
        if (!sim_choice_free_confirmation_grid(rc)) {
            push(r, "confirm.choice_free_grid", "true", "false");
        }
        return r;
    }

    // ---- a combat-pile grid ------------------------------------------------
    {
        ActionMask cm{};
        if (sim_combat_pile_grid(rc, cm)) {
            r.kind = MaskKind::COMBAT_PILE;
            const CardPoolIndex* pile = nullptr;
            uint8_t count = 0;
            if (cm.choice_from_discard) {
                pile = rc.combat.discard;
                count = rc.combat.discard_count;
            } else if (cm.choice_from_exhaust) {
                pile = rc.combat.exhaust;
                count = rc.combat.exhaust_count;
            } else {
                pile = rc.combat.draw;
                count = rc.combat.draw_count;
            }
            const auto& front = rc.combat.action_queue[rc.combat.action_head];
            const ChoiceKind kind =
                sts::engine::choose_kind_from_flags(front.flags);
            const uint8_t type_filter =
                sts::engine::choose_type_filter_from_flags(front.flags);
            std::vector<std::string> sim_keys;
            for (uint8_t i = 0; i < count; ++i) {
                if (!sts::engine::choice_slot_eligible(rc.combat, i, kind,
                                                       type_filter)) {
                    continue;
                }
                sim_keys.push_back(sim_card_key(rc.combat.card_pool[pile[i]]));
            }
            const std::vector<std::string> game_keys = capture_keys(s);
            if (sim_keys.size() > game_keys.size()) {
                push(r, "combat_grid.rows", std::to_string(game_keys.size()),
                     std::to_string(sim_keys.size()));
            }
            cmp_multiset(r, "combat_grid", game_keys, sim_keys,
                         /*sim_subset_ok=*/sim_keys.size() < game_keys.size());
            return r;
        }
    }

    // ---- The Library's twenty-card board -----------------------------------
    if (rc.phase == static_cast<uint8_t>(RunPhase::EVENT_DIALOG) &&
        rc.event.grid_kind == static_cast<uint8_t>(EventGridKind::NONE) &&
        !s.card_offer.empty()) {
        RunActionMask m{};
        legal_actions(rc, m);
        int options = 0;
        for (int i = 0; i < kEventOptionCap; ++i)
            if (m.can_choose_event_option[i]) ++options;
        if (options == static_cast<int>(s.card_offer.size())) {
            r.kind = MaskKind::LIBRARY_BOARD;
            for (int i = 0; i < options; ++i) {
                const auto id = static_cast<CardId>(
                    rc.event.board[static_cast<std::size_t>(options - 1 - i)]
                        .card_id);
                const std::string sim_id =
                    std::string(sts::registry::card_game_id(id));
                if (sim_id != s.card_offer[static_cast<std::size_t>(i)]) {
                    push(r, "library[" + std::to_string(i) + "]",
                         s.card_offer[static_cast<std::size_t>(i)], sim_id);
                }
            }
            return r;
        }
    }

    // ---- a run-layer master-deck grid --------------------------------------
    if (!sim_grid_open(rc)) {
        r.kind = MaskKind::UNPAIRED;
        return r;
    }
    r.kind = MaskKind::MASTER_DECK;
    const std::vector<int> rows = sim_grid_rows(rc);
    if (rows.size() != s.card_offer.size()) {
        std::vector<std::string> sim_keys;
        sim_keys.reserve(rows.size());
        for (int row : rows)
            sim_keys.push_back(sim_card_key(
                rc.run.master_deck[static_cast<std::size_t>(row)]));
        push(r, "grid.rows", std::to_string(s.card_offer.size()),
             std::to_string(rows.size()));
        push(r, "grid.cards", join_keys(capture_keys(s)), join_keys(sim_keys));
        return r;
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::string sim_key = sim_card_key(
            rc.run.master_deck[static_cast<std::size_t>(rows[i])]);
        const std::string game_key = capture_key(s, i);
        if (sim_key != game_key) {
            push(r, "grid[" + std::to_string(i) + "]", game_key, sim_key);
        }
    }
    return r;
}

}  // namespace sts::replay
