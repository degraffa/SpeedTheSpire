#pragma once

// readout_shapes.hpp -- the two SHAPE decisions the `--treasure` and `--event`
// read-outs make before any comparison happens:
//
//   1. `strip_sapphire_key_row` -- which captured reward rows a chest open is
//      allowed to carry that the simulator does not model, and which absence or
//      misplacement of that row is a real divergence.
//   2. `join_capture_event` -- the capture's event identity -> the registry's
//      `EventId`, fail-loud on anything the registry does not know.
//
// INTERNAL header, same rationale as `command_map.hpp` next door (conventions
// "Where a new header goes"): its consumers are this tool's `main.cpp` and its
// own gtest. It is split out for the same reason the command table was -- both
// decisions are places the harness can quietly call a divergence benign, and
// until they were separable the only way to see one go wrong was to read a
// whole campaign's output by eye. Everything here is plain data plus the
// generated registry, so both are directly testable without an artifact.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "sts/registry/game_ids.hpp"
#include "sts/registry/ids.hpp"

namespace sts::replay {

// --- 1. The chest-linked SAPPHIRE_KEY row ------------------------------------
//
// WHY THIS EXISTS AT ALL. `AbstractChest.open` (AbstractChest.java:62-102) adds
// the base relic row and then, under `Settings.isFinalActAvailable &&
// !Settings.hasSapphireKey` (:95-96), appends a SECOND row through
// `AbstractRoom.addSapphireKey` (AbstractRoom.java:545-547) that is LINKED to
// the row before it (`RewardItem(RewardItem, RewardType)`, RewardItem.java:
// 86-93, which sets the link in BOTH directions). On the frozen audited profile
// `isFinalActAvailable` is true and `hasSapphireKey` starts false
// (CardCrawlGame.java:473), so **every Act-1 chest open in a capture carries
// that extra trailing row** until the run actually takes a key.
//
// The row consumes no RNG and grants no relic, so the engine models no key row
// at all (combat_rewards.hpp's "Deliberately NOT modelled" block). A read-out
// that compared reward rows literally would therefore report a divergence on
// every single chest. Eliding it blindly would be worse: a key row on a screen
// that is NOT a chest open, a key row that is not trailing or not linked, or a
// MISSING key row on a chest open, are each a real finding. This function is
// the one place that distinction is made, so it is the one place to test.
//
// THE ONE LEGITIMATE ABSENCE is N'loth's Mask: its `onChestOpenAfter`
// (NlothsMask.java:23-32) calls `AbstractRoom.removeOneRelicFromRewards`
// (AbstractRoom.java:549-559), which deletes the first RELIC row AND the row
// immediately after it when that row is its `relicLink` -- taking the key with
// the relic. The second is arithmetic rather than a special case: once the run
// has taken a sapphire key, `!Settings.hasSapphireKey` is false and no later
// chest appends one.

// One reward row exactly as the capture presented it (`screen_state.rewards[]`).
// Only the fields a comparison reads are kept; `link_id` is the linked relic's
// game id on a SAPPHIRE_KEY row, which is what proves the link is the one the
// Java built rather than a coincidence of ordering.
struct CaptureRewardRow {
    std::string type;      // reward_type
    int gold = 0;          // GOLD / STOLEN_GOLD
    std::string relic_id;  // RELIC
    std::string link_id;   // SAPPHIRE_KEY: rewards[].link.id
};

// What the game's own gate would have decided for this screen. Supplied by the
// caller from the capture, never guessed here -- that is what keeps this
// function a rule and not a heuristic.
struct KeyRowContext {
    // This reward screen is a TREASURE-ROOM CHEST OPEN. Only such a screen may
    // carry a chest-linked key row.
    bool chest_open = false;
    // `Settings.hasSapphireKey` was already true when the chest was opened --
    // the run claimed a key on an earlier chest -- so the :95-96 gate is shut
    // and no row is expected.
    bool already_has_key = false;
    // A N'loth's Mask with a live charge ran `removeOneRelicFromRewards` on
    // this open, so the base relic row and its linked key row are both gone.
    bool nloths_mask_fired = false;
};

struct KeyRowVerdict {
    bool ok = false;
    std::string problem;                 // empty iff ok
    std::vector<CaptureRewardRow> rows;  // capture rows, key row elided
    int key_index = -1;                  // index the key row was at, or -1
};

[[nodiscard]] inline KeyRowVerdict strip_sapphire_key_row(
    const std::vector<CaptureRewardRow>& capture, const KeyRowContext& ctx) {
    KeyRowVerdict v;
    v.rows = capture;

    int keys = 0;
    int at = -1;
    for (std::size_t i = 0; i < capture.size(); ++i) {
        if (capture[i].type != "SAPPHIRE_KEY") continue;
        ++keys;
        if (at < 0) at = static_cast<int>(i);
    }

    int relic_rows = 0;
    for (const CaptureRewardRow& r : capture)
        if (r.type == "RELIC") ++relic_rows;

    if (keys > 1) {
        v.problem = "the screen carries " + std::to_string(keys) +
                    " SAPPHIRE_KEY rows; AbstractChest.open appends at most one "
                    "(AbstractChest.java:95-96)";
        return v;
    }

    if (keys == 0) {
        if (!ctx.chest_open) {
            v.ok = true;  // nothing to elide, nothing to expect
            return v;
        }
        if (ctx.already_has_key) {
            v.ok = true;  // the !Settings.hasSapphireKey gate is shut
            return v;
        }
        if (ctx.nloths_mask_fired && relic_rows == 0) {
            // removeOneRelicFromRewards took the relic AND its linked key.
            v.ok = true;
            return v;
        }
        v.problem =
            "a chest open with " + std::to_string(relic_rows) +
            " RELIC row(s) carries NO trailing SAPPHIRE_KEY row, and neither "
            "an already-claimed key nor a N'loth's Mask removal explains it "
            "(AbstractChest.java:95-96; AbstractRoom.java:549-559)";
        return v;
    }

    // Exactly one key row.
    if (!ctx.chest_open) {
        v.problem =
            "a SAPPHIRE_KEY row appears on a reward screen that is NOT a "
            "treasure-chest open; the only producer is AbstractChest.open "
            "(AbstractChest.java:95-96)";
        return v;
    }
    if (ctx.already_has_key) {
        v.problem =
            "a SAPPHIRE_KEY row appears although the run already holds a "
            "sapphire key; the !Settings.hasSapphireKey gate should have "
            "suppressed it (AbstractChest.java:95-96)";
        return v;
    }
    if (at != static_cast<int>(capture.size()) - 1) {
        v.problem = "the SAPPHIRE_KEY row is at index " + std::to_string(at) +
                    " of " + std::to_string(capture.size()) +
                    "; addSapphireKey appends it LAST (AbstractRoom.java:545-547)";
        return v;
    }
    if (at == 0 || capture[static_cast<std::size_t>(at) - 1].type != "RELIC") {
        v.problem =
            "the SAPPHIRE_KEY row does not follow a RELIC row; addSapphireKey "
            "links it to rewards.get(size-1), which the chest just made the "
            "base relic (AbstractChest.java:95-96)";
        return v;
    }
    const CaptureRewardRow& base = capture[static_cast<std::size_t>(at) - 1];
    if (capture[static_cast<std::size_t>(at)].link_id != base.relic_id) {
        v.problem = "the SAPPHIRE_KEY row links to \"" +
                    capture[static_cast<std::size_t>(at)].link_id +
                    "\" but follows the RELIC row \"" + base.relic_id +
                    "\"; the link is set in both directions at "
                    "RewardItem.java:86-93";
        return v;
    }

    v.key_index = at;
    v.rows.erase(v.rows.begin() + at);
    v.ok = true;
    return v;
}

// Which reward row a captured COMBAT_REWARD `choose i` names, translated into
// the SIM's row index -- i.e. with the elided key row taken out of the index
// space. A `choose` that names the key row itself has NO sim index: per
// RewardItem.java:317-322 claiming the key sets the linked base relic's
// `isDone`/`ignoreReward`, so the relic is ABANDONED rather than acquired, and
// the run-layer analogue of that is claiming nothing at all. (The mirror case,
// RewardItem.java:298-300, is why claiming the relic is safe: it marks the KEY
// done and the run keeps the relic.)
enum class ClaimTarget : uint8_t {
    SIM_ROW,        // claim `sim_index` on the simulator's screen
    ABANDONS_RELIC, // the capture claimed the key: nothing is claimed sim-side
    OUT_OF_RANGE,
};

struct ClaimMapping {
    ClaimTarget what = ClaimTarget::OUT_OF_RANGE;
    int sim_index = -1;
};

[[nodiscard]] inline ClaimMapping map_reward_claim(
    const std::vector<CaptureRewardRow>& capture, int key_index, int choice) {
    ClaimMapping m;
    if (choice < 0 || choice >= static_cast<int>(capture.size())) return m;
    if (choice == key_index) {
        m.what = ClaimTarget::ABANDONS_RELIC;
        return m;
    }
    m.what = ClaimTarget::SIM_ROW;
    m.sim_index = (key_index >= 0 && choice > key_index) ? choice - 1 : choice;
    return m;
}

// --- 2. The event identity join ----------------------------------------------
//
// A capture's EVENT screen carries BOTH `screen_state.event_id` -- the event
// class's static `ID`, which is exactly the `game_id` column of
// `registry/events.yaml` -- and `screen_state.event_name`, the LOCALIZED
// display name. They differ for six of the nineteen ids these campaigns show
// (`Liars Game`/`The Ssssserpent`, `Golden Wing`/`Wing Statue`, `FaceTrader`/
// `Face Trader`, `Fountain of Cleansing`/`The Divine Fountain`, `Bonfire
// Elementals`/`Bonfire Spirits`, `Transmorgrifier`/`Transmogrifier` -- note the
// game's own misspelling in the id), so the join key is the ID and only the ID.
// The name is carried alongside purely so a read-out line is readable by a
// human who knows the game's UI and not its class names.
//
// FAIL LOUD. The translator's `join_event` throws on an id the registry does
// not know (translate.cpp:232-247) because an unknown id means schema drift.
// This read-out is downstream of that, but it makes the same judgement itself
// rather than inheriting it: `--event` reads `screen_state` in a SECOND, light
// JSON pass that the translator's field discipline never sees (the same second
// pass `ScreenInfo` has always been), so an id that never reached the
// translator -- a Neow sentinel on a floor the mode did not expect, a
// hand-edited artifact -- would otherwise silently compare as "no event".
struct EventJoin {
    uint16_t id = 0;      // EventId as u16; 0 == not joined
    std::string problem;  // empty iff joined
};

[[nodiscard]] inline EventJoin join_capture_event(const std::string& game_id) {
    EventJoin j;
    if (game_id.empty()) {
        j.problem = "the capture's EVENT screen carries an empty event_id";
        return j;
    }
    if (game_id == "Neow Event") {
        // The one EVENT screen with no events.yaml row, on purpose: NeowEvent
        // is the single base-game event class with no static `ID`, so
        // GameStateConverter hard-codes this sentinel, and Neow is in no act's
        // pool (translate.cpp's note). It is the --neow mode's subject, never
        // this one's.
        j.problem = "\"Neow Event\" is the floor-0 blessing sentinel, not a "
                    "pool event -- use --neow";
        return j;
    }
    const sts::registry::EventId id =
        sts::registry::event_from_game_id(game_id);
    if (id == sts::registry::EventId::NONE) {
        j.problem = "unknown event id \"" + game_id +
                    "\" -- registry/events.yaml has no game_id mapping "
                    "(schema drift)";
        return j;
    }
    j.id = static_cast<uint16_t>(id);
    return j;
}

}  // namespace sts::replay
