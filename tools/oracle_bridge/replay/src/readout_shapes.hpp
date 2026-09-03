#pragma once

// readout_shapes.hpp -- the five SHAPE decisions the replay read-outs make
// before any comparison happens:
//
//   1. `strip_sapphire_key_row` -- which captured reward rows a chest open is
//      allowed to carry that the simulator does not model, and which absence or
//      misplacement of that row is a real divergence.
//   2. `join_capture_event` -- the capture's event identity -> the registry's
//      `EventId`, fail-loud on anything the registry does not know.
//   3. `is_escape_settlement_fields` -- the exact ordinary field set that may
//      lag while a Smoke Bomb escape animation settles, plus the separately
//      named counter-reset extension for a proved onVictory settlement.
//   4. `is_potion_obtain_animation_fields` -- the exact field set that may lag
//      while Entropic Brew's ObtainPotionEffects animate.
//   5. `is_transform_preview_rng_advance` -- a proved cardRng-only advance
//      caused by the transform confirmation's curse preview animation.
//
// INTERNAL header, same rationale as `command_map.hpp` next door (conventions
// "Where a new header goes"): its consumers are this tool's `main.cpp` and its
// own gtest. It is split out for the same reason the command table was -- both
// decisions are places the harness can quietly call a divergence benign, and
// until they were separable the only way to see one go wrong was to read a
// whole campaign's output by eye. Everything here is plain data plus the
// generated registry, so all five are directly testable without an artifact.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "sts/engine/rng_stream.hpp"
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
// The row consumed no RNG and granted no relic, so until S3.11 the engine
// modelled no key row at all and a read-out that compared reward rows literally
// would have reported a divergence on every single chest. Eliding it blindly
// would have been worse: a key row on a screen that is NOT a chest open, a key
// row that is not trailing or not linked, or a MISSING key row on a chest open,
// are each a real finding. This function is the one place that distinction is
// made, so it is the one place to test.
//
// S3.11 MADE THE ENGINE ASSEMBLE THE ROW (AbstractChest.java:95-97 ->
// AbstractRoom.java:545-547, with the real two-way claim semantics). What
// survives that is the SHAPE RULE -- everything this function decides about
// whether the capture's own list is well formed, which is still exactly the
// question a read-out must answer before it asks the simulator to agree. What
// no longer has a consumer is the ELISION: `KeyRowVerdict::rows` is the capture
// with the key row taken out, and `main.cpp` now compares against the full
// list. `rows` and `map_reward_claim`'s key-index argument are kept because
// they are the definition of the rule the tests pin (a rule that is checked and
// then not applied is still the rule), and because the identity call
// `map_reward_claim(rows, -1, choice)` is the bounds check the walk needs.
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
// the SIM's row index -- i.e. with an elided key row taken out of the index
// space. A `choose` that names the key row itself then has NO sim index: per
// RewardItem.java:317-322 claiming the key sets the linked base relic's
// `isDone`/`ignoreReward`, so the relic is ABANDONED rather than acquired, and
// with no key row sim-side the analogue of that was claiming nothing at all.
// (The mirror case, RewardItem.java:298-300, is why claiming the relic was
// safe: it marks the KEY done and the run keeps the relic.)
//
// FROM S3.11 the caller passes `key_index = -1`, because the simulator now has
// the key row and both index spaces are the same one: the mapping is the
// identity plus a bounds check, and the engine's own claim arm applies the two
// mutually destructive branches. The `key_index >= 0` behaviour is retained
// as the definition of the elided-index-space rule.
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

// --- 3. The Smoke-Bomb escape-settlement race --------------------------------
//
// The escape-animation analogue of `is_obtain_race` (main.cpp), and the same
// single-frame family: SmokeBomb.use latches the escape, but the game's combat
// only ENDS when `AbstractPlayer.updateEscapeAnimation` reaches
// `getCurrRoom().endBattle()` (AbstractPlayer.java:2281-2292) -- an animation
// clock away. A capture dump taken inside that window still lists the fight,
// while the sim (which has no animation clock) settled the escape
// synchronously on the `potion use`: onVictory ran (Burning Blood's heal,
// BurningBlood.java:30) and the battle-over screen assembled (the potion-drop
// roll + ratchet and the gold roll -- potionRng, blizzardPotionMod,
// treasureRng). Elite rooms also pop their relic reward during this same
// assembly, advancing relicRng and removing the selected row from its dungeon
// pool. STS00241 seq 96 is the ordinary-room live pin; STS400327 seq 83 and
// STS401257 seq 95 are the elite-room pins. Every pin is exactly one record and
// zero-diff again on the following capture record.
//
// THE FIELD SET IS THE NARROWNESS, exactly as the obtain race's deck-suffix
// shape is: every differing field must be one the one-frame settlement can
// move -- hp (the victory heal) or the reward-assembly movers
// (blizzard_potion_mod, treasure_rng.*, potion_rng.*), plus the elite relic
// reward movers (relic_rng.* and relic_pool[*]). Anything else -- gold
// (assembled rewards sit on the SCREEN, never in the purse), floor, a deck or
// an acquired-relic field, any other stream -- makes this return false and the
// record a real divergence. The caller supplies the equally-narrow WINDOW
// gates (the capture still in-combat, the sim already on a reward screen, the sim's
// combat flagged PLAYER-ESCAPED); a settlement computed WRONG rather than
// early still surfaces, because the capture's own settled records from the
// next seq on no longer satisfy the window and diff for real.  Relic counters
// are deliberately NOT part of this ordinary set: a counter may change for
// unrelated combat reasons, so the narrowly proved onVictory-reset extension
// below has its own source, previous-command, value, and reconvergence gates in
// main.cpp.
[[nodiscard]] inline bool is_escape_settlement_fields(
    const std::vector<std::string>& field_names) {
    if (field_names.empty()) return false;
    for (const std::string& f : field_names) {
        if (f == "hp" || f == "blizzard_potion_mod") continue;
        if (f.rfind("treasure_rng.", 0) == 0) continue;
        if (f.rfind("potion_rng.", 0) == 0) continue;
        if (f.rfind("relic_rng.", 0) == 0) continue;
        if (f.rfind("relic_pool[", 0) == 0) continue;
        return false;
    }
    return true;
}

// A field spelling accepted only by the counter-reset extension below.  It is
// deliberately strict: pool rows, id changes, and every other RelicSlot member
// stay ordinary divergences.
[[nodiscard]] inline bool is_relic_counter_field(std::string_view field) {
    constexpr std::string_view prefix = "relics[";
    constexpr std::string_view suffix = "].counter";
    if (!field.starts_with(prefix) || !field.ends_with(suffix) ||
        field.size() <= prefix.size() + suffix.size()) {
        return false;
    }
    const std::size_t number_end = field.size() - suffix.size();
    for (std::size_t i = prefix.size(); i < number_end; ++i) {
        if (field[i] < '0' || field[i] > '9') {
            return false;
        }
    }
    return true;
}

// The field-set half of the rare counter-reset escape settlement.  This alone
// is NOT a race classifier: main.cpp also proves the immediately preceding
// command spent Smoke Bomb, every changed counter is a non-negative -> -1
// onVictory reset, and the next capture record equals the simulator exactly.
// The helper insists on at least one counter so callers cannot accidentally
// substitute it for the ordinary settlement set above.
[[nodiscard]] inline bool is_escape_settlement_with_relic_counter_resets(
    const std::vector<std::string>& field_names) {
    if (field_names.empty()) return false;
    bool saw_counter = false;
    for (const std::string& f : field_names) {
        if (is_relic_counter_field(f)) {
            saw_counter = true;
            continue;
        }
        if (f == "hp" || f == "blizzard_potion_mod") continue;
        if (f.rfind("treasure_rng.", 0) == 0) continue;
        if (f.rfind("potion_rng.", 0) == 0) continue;
        if (f.rfind("relic_rng.", 0) == 0) continue;
        if (f.rfind("relic_pool[", 0) == 0) continue;
        return false;
    }
    return saw_counter;
}

// --- 4. Entropic Brew's out-of-combat obtain-animation race -----------------
//
// The out-of-combat branch queues one ObtainPotionEffect per roll
// (EntropicBrew.java:46-48). The potion RNG advances synchronously, but each
// effect inserts its potion only after its animation clock runs. A driver dump
// may therefore see the consumed Brew's slots empty for one ready-for-command
// record while the headless simulator has already filled them. This predicate
// is only the field-set half of the classifier; main.cpp additionally requires
// the immediately preceding command to have used an Entropic Brew and the
// immediately following capture record to contain exactly the simulator's
// potion identities.
[[nodiscard]] inline bool is_potion_obtain_animation_fields(
    const std::vector<std::string>& field_names) {
    if (field_names.empty()) return false;
    for (const std::string& f : field_names) {
        if (f.rfind("potions[", 0) != 0) return false;
    }
    return true;
}

// --- 5. Transform-grid curse preview's wall-clock cardRng burn ---------------
//
// GridCardSelectScreen's transform confirmation animates a replacement preview
// every 0.1 seconds (GridCardSelectScreen.java:264-268). It calls
// returnTrulyRandomCardFromAvailable(preview, new Random()), which normally
// confines the roll to that throwaway RNG -- except its CURSE branch ignores
// the supplied RNG and calls CardLibrary.getCurse(), consuming GLOBAL cardRng
// instead (AbstractDungeon.java:1016-1045; CardLibrary.java:1022-1029).
// Therefore the number of cardRng draws depends on wall-clock time spent on
// the confirmation animation, information the headless action API neither has
// nor should invent. STS303586's Living Wall confirmation burned three.
//
// This is the stream-proof half of the classifier. The caller additionally
// requires the immediately preceding replay command to have confirmed an
// event TRANSFORMABLE grid whose selected card was a CURSE. We accept exactly
// card_rng.{s0,s1,counter}, require a positive bounded counter delta, and replay
// that many getCurse() `random(0, 9)` calls from the sim stream to prove the
// captured endpoint byte-for-byte. A wrong stream state, any gameplay field,
// or any other grid therefore cannot hide here.
[[nodiscard]] inline bool is_transform_preview_rng_advance(
    const std::vector<std::string>& field_names,
    const sts::engine::RngStream& sim,
    const sts::engine::RngStream& capture) noexcept {
    if (field_names.size() != 3) return false;
    bool s0 = false;
    bool s1 = false;
    bool counter = false;
    for (const std::string& f : field_names) {
        if (f == "card_rng.s0") {
            s0 = true;
        } else if (f == "card_rng.s1") {
            s1 = true;
        } else if (f == "card_rng.counter") {
            counter = true;
        } else {
            return false;
        }
    }
    const int32_t draws = capture.counter - sim.counter;
    if (!s0 || !s1 || !counter || draws <= 0 || draws > 1000) return false;
    sts::engine::RngStream probe = sim;
    for (int32_t i = 0; i < draws; ++i) {
        (void)sts::engine::random(probe, 0, 9);  // ten ordinary curse ids
    }
    return probe.s0 == capture.s0 && probe.s1 == capture.s1 &&
           probe.counter == capture.counter && probe.pad == capture.pad;
}

}  // namespace sts::replay
