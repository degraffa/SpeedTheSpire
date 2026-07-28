// The seed pre-scanner's substance. See seed_scan.hpp for why it exists and
// why the run loop is borrowed from tools/fuzz rather than restated.

#include "sts/planner/seed_scan.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include "sts/engine/combat_rewards.hpp"  // run_has_relic, RewardItemKind
#include "sts/engine/map_rooms.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/shop.hpp"  // kShopRelicCount (the merchant shelf)
#include "sts/registry/event_table.hpp"
#include "sts/registry/game_ids.hpp"  // relic_game_id (the join key)

namespace sts::planner {

using registry::EventId;

// --- Event naming ------------------------------------------------------------

namespace {

// registry/events.yaml, one row per event, in id order: the enum symbol and
// the game_id. See the header for why this is a hand copy and what guards it.
const std::vector<EventName>& event_name_table() {
    static const std::vector<EventName> kNames = {
        // Exordium.initializeEventList (Exordium.java:223-236) -- ids 1..11.
        {EventId::BIG_FISH, "BIG_FISH", "Big Fish"},
        {EventId::THE_CLERIC, "THE_CLERIC", "The Cleric"},
        {EventId::DEAD_ADVENTURER, "DEAD_ADVENTURER", "Dead Adventurer"},
        {EventId::GOLDEN_IDOL, "GOLDEN_IDOL", "Golden Idol"},
        {EventId::GOLDEN_WING, "GOLDEN_WING", "Golden Wing"},
        {EventId::WORLD_OF_GOOP, "WORLD_OF_GOOP", "World of Goop"},
        {EventId::LIARS_GAME, "LIARS_GAME", "Liars Game"},
        {EventId::LIVING_WALL, "LIVING_WALL", "Living Wall"},
        {EventId::MUSHROOMS, "MUSHROOMS", "Mushrooms"},
        {EventId::SCRAP_OOZE, "SCRAP_OOZE", "Scrap Ooze"},
        {EventId::SHINING_LIGHT, "SHINING_LIGHT", "Shining Light"},
        // Exordium.initializeShrineList (Exordium.java:238-246) -- ids 12..17.
        {EventId::MATCH_AND_KEEP, "MATCH_AND_KEEP", "Match and Keep!"},
        {EventId::GOLDEN_SHRINE, "GOLDEN_SHRINE", "Golden Shrine"},
        {EventId::TRANSMORGRIFIER, "TRANSMORGRIFIER", "Transmorgrifier"},
        {EventId::PURIFIER, "PURIFIER", "Purifier"},
        {EventId::UPGRADE_SHRINE, "UPGRADE_SHRINE", "Upgrade Shrine"},
        {EventId::WHEEL_OF_CHANGE, "WHEEL_OF_CHANGE", "Wheel of Change"},
        // AbstractDungeon.initializeSpecialOneTimeEventList (:1340-1358)
        // -- ids 18..31.
        {EventId::ACCURSED_BLACKSMITH, "ACCURSED_BLACKSMITH", "Accursed Blacksmith"},
        {EventId::BONFIRE_ELEMENTALS, "BONFIRE_ELEMENTALS", "Bonfire Elementals"},
        {EventId::DESIGNER, "DESIGNER", "Designer"},
        {EventId::DUPLICATOR, "DUPLICATOR", "Duplicator"},
        {EventId::FACE_TRADER, "FACE_TRADER", "FaceTrader"},
        {EventId::FOUNTAIN_OF_CLEANSING, "FOUNTAIN_OF_CLEANSING", "Fountain of Cleansing"},
        {EventId::KNOWING_SKULL, "KNOWING_SKULL", "Knowing Skull"},
        {EventId::LAB, "LAB", "Lab"},
        {EventId::NLOTH, "NLOTH", "N'loth"},
        {EventId::NOTE_FOR_YOURSELF, "NOTE_FOR_YOURSELF", "NoteForYourself"},
        {EventId::SECRET_PORTAL, "SECRET_PORTAL", "SecretPortal"},
        {EventId::THE_JOUST, "THE_JOUST", "The Joust"},
        {EventId::WE_MEET_AGAIN, "WE_MEET_AGAIN", "WeMeetAgain"},
        {EventId::THE_WOMAN_IN_BLUE, "THE_WOMAN_IN_BLUE", "The Woman in Blue"},
    };
    return kNames;
}

// The guard that makes the hand copy above honest: adding a row to
// registry/events.yaml grows the GENERATED table, and this stops compiling.
// (kEventTable is generated from the same yaml -- sts/registry/event_table.hpp.)
constexpr std::size_t kNameTableSize = 31;
static_assert(registry::kEventTable.size() == kNameTableSize,
              "registry/events.yaml gained or lost an event row: add the matching "
              "{EventId, symbol, game_id} entry to event_name_table() in "
              "tools/oracle_bridge/planner/src/seed_scan.cpp and update "
              "kNameTableSize. This table is a hand copy of the yaml's game_id "
              "column because the generator emits no name strings.");

char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    }
    return true;
}

}  // namespace

const std::vector<EventName>& event_names() { return event_name_table(); }

bool event_id_from_name(std::string_view name, EventId& out) {
    for (const EventName& e : event_name_table()) {
        if (iequals(name, e.symbol) || iequals(name, e.game_id)) {
            out = e.id;
            return true;
        }
    }
    return false;
}

std::string_view event_game_id(EventId id) {
    for (const EventName& e : event_name_table()) {
        if (e.id == id) return e.game_id;
    }
    return {};
}

bool event_flag_set(uint32_t flags, EventId id) {
    const auto v = static_cast<uint32_t>(id);
    // event_framework.cpp:392 writes bit (id-1); EventId 0 is NONE and never
    // fires, and ids run 1..31 (event_framework.hpp:164-169), so the shift is
    // always in range for a real id.
    if (v == 0 || v > 31) return false;
    return (flags & (1u << (v - 1u))) != 0u;
}

std::vector<EventId> decode_event_flags(uint32_t flags) {
    std::vector<EventId> out;
    for (const EventName& e : event_name_table()) {
        if (event_flag_set(flags, e.id)) out.push_back(e.id);
    }
    return out;
}

std::string event_flags_text(uint32_t flags) {
    std::string s;
    for (const EventName& e : event_name_table()) {
        if (!event_flag_set(flags, e.id)) continue;
        if (!s.empty()) s += '|';
        s.append(e.game_id);
    }
    return s;
}

// --- Scanning ----------------------------------------------------------------

fuzz::CaseId ScanCase::case_id() const {
    fuzz::CaseId id;
    id.run_seed = seed.value;
    id.ascension = ascension;
    id.policy = policy;
    id.policy_seed = policy_seed;
    return id;
}

namespace {

// What the observer accumulates as pass A goes past. Every member is updated
// idempotently (max / OR), which is what fuzz_run.hpp's StepObserver contract
// requires -- the terminal controller is observed twice.
struct Watch {
    uint32_t max_floor = 0;
    uint32_t event_flags = 0;
    bool treasure = false;
    bool boss = false;
    std::vector<RelicObs> relics;  // one per target, latched (OR) per step
};

void observe(const engine::RunController& rc, void* ctx) noexcept {
    auto* w = static_cast<Watch*>(ctx);
    const auto floor = static_cast<uint32_t>(rc.run.floor);
    if (floor > w->max_floor) w->max_floor = floor;
    // event_flags is a run-long accumulating bitset (event_framework.cpp:392),
    // so OR-ing every observation is the same as reading the terminal value --
    // and stays correct if a future engine change ever clears a bit.
    w->event_flags |= rc.run.event_flags;
    // Both the static Treasure map node and a ? room whose eventRng roll came
    // up TREASURE reach the same on_player_entry(RoomType::Treasure), which
    // sets this phase (src/engine/run_advance.cpp:695-701, :724-727) -- so one
    // phase test covers both producers.
    if (rc.phase == static_cast<uint8_t>(engine::RunPhase::TREASURE_ROOM)) {
        w->treasure = true;
    }
    // The boss is not a grid node; it is entered through the MAP_CHOICE boss
    // edge, which sets room_type (src/engine/run_advance.cpp:849, :679-681).
    if (rc.room_type == static_cast<uint8_t>(engine::RoomType::Boss)) {
        w->boss = true;
    }

    // Relic targets. Every latch is idempotent, per the StepObserver contract.
    const bool in_shop =
        rc.phase == static_cast<uint8_t>(engine::RunPhase::SHOP);
    for (RelicObs& t : w->relics) {
        const auto raw = static_cast<uint16_t>(t.id);
        // Ownership -- and The Courier's compound question: a live merchant
        // while owned. (`acquired` is latched, but shop_while_owned reads the
        // CURRENT ownership on purpose: run_has_relic can only ever grow in
        // S1 -- purges remove cards, never relics -- so the two agree, and
        // reading the live state keeps the observation honest if that ever
        // changes.)
        if (engine::run_has_relic(rc.run, t.id)) {
            t.acquired = true;
            if (in_shop) t.shop_while_owned = true;
        }
        // Offers: RELIC rows on the live reward screen (elite / chest / event
        // combat rewards all assemble into rc.rewards)...
        for (uint8_t i = 0; i < rc.rewards.count && i < engine::kRewardItemCap;
             ++i) {
            const engine::RunRewardItem& item = rc.rewards.items[i];
            if (item.kind ==
                    static_cast<uint8_t>(engine::RewardItemKind::RELIC) &&
                item.id == raw) {
                t.offered = true;
            }
        }
        // ... and the merchant's three shelf slots (a Bottled relic CAN be
        // stocked -- shop.hpp's on_equip_screen note; The Courier cannot, per
        // its canSpawn, and this observation is how that stays checkable).
        if (in_shop) {
            for (int i = 0; i < engine::kShopRelicCount; ++i) {
                if (rc.shop.relics[i].id == raw) t.offered = true;
            }
        }
    }
}

}  // namespace

ScanRow scan_case(const ScanCase& c, const ScanLimits& lim,
                  const std::vector<registry::RelicId>& relic_targets) {
    ScanRow row;
    row.seed = c.seed;
    row.ascension = c.ascension;
    row.policy = c.policy;
    row.policy_seed = c.policy_seed;

    fuzz::RunLimits limits;
    limits.max_actions = lim.max_actions;
    limits.revisit_limit = lim.revisit_limit;

    Watch w;
    w.relics.reserve(relic_targets.size());
    for (registry::RelicId id : relic_targets) {
        RelicObs t;
        t.id = id;
        w.relics.push_back(t);
    }
    fuzz::StepObserver obs;
    obs.fn = &observe;
    obs.ctx = &w;

    fuzz::CaseResult result;
    // verify_repro=false: pass C re-drives pass A's literal action log, which
    // is triage machinery for a divergence the scan is not looking for and
    // would cost a third of the scan's wall clock. Passes A and B still run,
    // so the determinism guard is not weakened.
    fuzz::run_case(c.case_id(), limits, nullptr, result, /*verify_repro=*/false,
                   fuzz::Inject{}, obs);

    row.end_reason = result.end_reason;
    row.actions = result.actions;
    row.final_hash = result.final_hash;
    row.max_floor = w.max_floor;
    row.event_flags = w.event_flags;
    row.treasure_entered = w.treasure;
    row.boss_reached = w.boss;
    row.relic_obs = std::move(w.relics);
    row.fail_kind = fuzz::fail_kind_name(result.failure.kind);
    return row;
}

// --- Filtering ---------------------------------------------------------------

bool Filter::empty() const {
    return need_events.empty() && !need_treasure && !need_boss &&
           min_floor == 0 && need_relic_offered.empty() &&
           need_relic_acquired.empty() && need_shop_after_relic.empty();
}

namespace {

// ANY-OF over one relic clause (see the Filter declaration for why relic
// clauses are any-of where need_events is all-of). An untracked relic never
// satisfies -- the row cannot testify about a relic it did not watch.
bool any_relic_hits(const std::vector<RelicObs>& obs,
                    const std::vector<registry::RelicId>& wanted,
                    bool RelicObs::* field) {
    if (wanted.empty()) return true;
    for (registry::RelicId id : wanted) {
        for (const RelicObs& o : obs) {
            if (o.id == id && o.*field) return true;
        }
    }
    return false;
}

}  // namespace

bool row_hits(const ScanRow& row, const Filter& f) {
    if (f.need_treasure && !row.treasure_entered) return false;
    if (f.need_boss && !row.boss_reached) return false;
    if (row.max_floor < f.min_floor) return false;
    for (EventId id : f.need_events) {
        if (!event_flag_set(row.event_flags, id)) return false;
    }
    if (!any_relic_hits(row.relic_obs, f.need_relic_offered,
                        &RelicObs::offered)) {
        return false;
    }
    if (!any_relic_hits(row.relic_obs, f.need_relic_acquired,
                        &RelicObs::acquired)) {
        return false;
    }
    if (!any_relic_hits(row.relic_obs, f.need_shop_after_relic,
                        &RelicObs::shop_while_owned)) {
        return false;
    }
    return true;
}

uint32_t count_hits(const std::vector<ScanRow>& rows, const Filter& f) {
    uint32_t n = 0;
    for (const ScanRow& r : rows) {
        if (row_hits(r, f)) ++n;
    }
    return n;
}

bool seed_qualifies(const std::vector<ScanRow>& rows, const Filter& f) {
    // min_hit_count 0 would qualify every seed including ones with no rows at
    // all, which is never what a caller means; treat it as 1 (the CLI clamps
    // it too, but the library must not depend on its front end).
    const uint32_t need = f.min_hit_count == 0 ? 1u : f.min_hit_count;
    return count_hits(rows, f) >= need;
}

// --- Output ------------------------------------------------------------------

bool format_from_name(std::string_view name, Format& out) {
    if (iequals(name, "tsv")) { out = Format::TSV; return true; }
    if (iequals(name, "jsonl")) { out = Format::JSONL; return true; }
    return false;
}

std::string_view tsv_header() {
    return "seed\tseed_int\tpolicy\tpolicy_seed\tascension\tend_reason\tactions\t"
           "max_floor\ttreasure\tboss\tevent_flags\tevents\trelic_obs\t"
           "final_hash\tfail_kind";
}

namespace {

std::string relic_obs_text(const std::vector<RelicObs>& obs) {
    // `<game_id>=<offered><acquired><shop_while_owned>` per target, '|'-joined
    // -- the same separator rationale as the events column (no relic game id
    // contains '|' or a tab; `SeparatorNeverOccursInsideAName` pins the event
    // claim and the relic ids share the character set). "" when untracked, so
    // an untracked scan's rows are unchanged but for the empty column.
    std::string s;
    for (const RelicObs& o : obs) {
        if (!s.empty()) s += '|';
        s.append(registry::relic_game_id(o.id));
        s += '=';
        s += o.offered ? '1' : '0';
        s += o.acquired ? '1' : '0';
        s += o.shop_while_owned ? '1' : '0';
    }
    return s;
}

}  // namespace

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string row_to_tsv(const ScanRow& row) {
    // Column order MUST match tsv_header(). The `events` column is the decoded
    // '|'-joined form of `event_flags` next to it: the number is what a script
    // filters on, the names are what a human reads, and keeping both means
    // neither side has to own a copy of the bit layout.
    char hash_buf[32];
    std::snprintf(hash_buf, sizeof(hash_buf), "%016llx",
                  static_cast<unsigned long long>(row.final_hash));
    const std::string cols[] = {
        row.seed.text,
        std::to_string(row.seed.value),
        fuzz::policy_name(row.policy),
        std::to_string(row.policy_seed),
        std::to_string(static_cast<unsigned>(row.ascension)),
        fuzz::end_reason_name(row.end_reason),
        std::to_string(row.actions),
        std::to_string(row.max_floor),
        row.treasure_entered ? "1" : "0",
        row.boss_reached ? "1" : "0",
        std::to_string(row.event_flags),
        event_flags_text(row.event_flags),
        relic_obs_text(row.relic_obs),
        hash_buf,
        row.fail_kind,
    };
    std::string s;
    bool first = true;
    for (const std::string& c : cols) {
        if (!first) s += '\t';
        first = false;
        s += c;
    }
    return s;
}

std::string row_to_jsonl(const ScanRow& row) {
    std::string s = "{";
    s += "\"seed\":\"" + json_escape(row.seed.text) + "\",";
    s += "\"seed_int\":" + std::to_string(row.seed.value) + ",";
    s += "\"policy\":\"" + std::string(fuzz::policy_name(row.policy)) + "\",";
    s += "\"policy_seed\":" + std::to_string(row.policy_seed) + ",";
    s += "\"ascension\":" + std::to_string(static_cast<unsigned>(row.ascension)) + ",";
    s += "\"end_reason\":\"" + std::string(fuzz::end_reason_name(row.end_reason)) + "\",";
    s += "\"actions\":" + std::to_string(row.actions) + ",";
    s += "\"max_floor\":" + std::to_string(row.max_floor) + ",";
    s += std::string("\"treasure\":") + (row.treasure_entered ? "true" : "false") + ",";
    s += std::string("\"boss\":") + (row.boss_reached ? "true" : "false") + ",";
    s += "\"event_flags\":" + std::to_string(row.event_flags) + ",";
    s += "\"events\":[";
    bool first = true;
    for (EventId id : decode_event_flags(row.event_flags)) {
        if (!first) s += ',';
        first = false;
        // Qualified: sts::registry::event_game_id (game_ids.hpp) is also
        // visible here via ADL since this file gained the registry include,
        // and the two would otherwise be ambiguous. The planner's own table
        // is the one this column documents.
        s += "\"" + json_escape(sts::planner::event_game_id(id)) + "\"";
    }
    s += "],";
    s += "\"relic_obs\":[";
    first = true;
    for (const RelicObs& o : row.relic_obs) {
        if (!first) s += ',';
        first = false;
        s += "{\"relic\":\"" +
             json_escape(registry::relic_game_id(o.id)) + "\",";
        s += std::string("\"offered\":") + (o.offered ? "true" : "false") + ",";
        s += std::string("\"acquired\":") + (o.acquired ? "true" : "false") + ",";
        s += std::string("\"shop_while_owned\":") +
             (o.shop_while_owned ? "true" : "false") + "}";
    }
    s += "],";
    s += "\"final_hash\":\"" + [&] {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%016llx",
                      static_cast<unsigned long long>(row.final_hash));
        return std::string(buf);
    }() + "\",";
    s += "\"fail_kind\":\"" + json_escape(row.fail_kind) + "\"";
    s += "}";
    return s;
}

std::string row_to_text(const ScanRow& row, Format f) {
    return f == Format::JSONL ? row_to_jsonl(row) : row_to_tsv(row);
}

// --- Aggregate report --------------------------------------------------------

void ScanSummary::add(const ScanRow& row) {
    ++rows;
    actions += row.actions;
    if (row.treasure_entered) ++treasure_rows;
    if (row.boss_reached) ++boss_rows;
    if (row.max_floor > max_floor) max_floor = row.max_floor;
    const uint32_t b = row.max_floor < static_cast<uint32_t>(kFloorHistogramBuckets)
                           ? row.max_floor
                           : static_cast<uint32_t>(kFloorHistogramBuckets) - 1u;
    ++floor_hist[b];
    const auto er = static_cast<int>(row.end_reason);
    if (er >= 0 && er < static_cast<int>(fuzz::EndReason::COUNT)) ++end_reason[er];
    if (row.fail_kind != "none") ++failures;
    for (EventId id : decode_event_flags(row.event_flags)) {
        const auto v = static_cast<std::size_t>(id);
        if (v < 32) ++event_rows[v];
    }
    for (const RelicObs& o : row.relic_obs) {
        RelicRows* rr = nullptr;
        for (RelicRows& cand : relic_rows) {
            if (cand.id == o.id) {
                rr = &cand;
                break;
            }
        }
        if (rr == nullptr) {
            RelicRows fresh;
            fresh.id = o.id;
            relic_rows.push_back(fresh);
            rr = &relic_rows.back();
        }
        if (o.offered) ++rr->offered;
        if (o.acquired) ++rr->acquired;
        if (o.shop_while_owned) ++rr->shop_while_owned;
    }
}

namespace {

std::string pct(uint64_t num, uint64_t den) {
    char buf[32];
    const double p = den == 0 ? 0.0 : 100.0 * static_cast<double>(num) /
                                          static_cast<double>(den);
    std::snprintf(buf, sizeof(buf), "%.2f%%", p);
    return buf;
}

}  // namespace

std::string ScanSummary::text() const {
    std::string s;
    s += "rows=" + std::to_string(rows) + " seeds=" + std::to_string(seeds) +
         " actions=" + std::to_string(actions) +
         " max_floor=" + std::to_string(max_floor) +
         " failures=" + std::to_string(failures) + "\n";
    s += "treasure_entered: " + std::to_string(treasure_rows) + "/" +
         std::to_string(rows) + " (" + pct(treasure_rows, rows) + ")\n";
    s += "boss_reached:     " + std::to_string(boss_rows) + "/" +
         std::to_string(rows) + " (" + pct(boss_rows, rows) + ")\n";
    s += "end reasons:\n";
    for (int i = 0; i < static_cast<int>(fuzz::EndReason::COUNT); ++i) {
        if (end_reason[i] == 0) continue;
        s += "  " + std::string(fuzz::end_reason_name(static_cast<fuzz::EndReason>(i))) +
             " " + std::to_string(end_reason[i]) + " (" + pct(end_reason[i], rows) +
             ")\n";
    }
    s += "floor histogram (max floor reached, last bucket is >=" +
         std::to_string(kFloorHistogramBuckets - 1) + "):\n";
    for (int i = 0; i < kFloorHistogramBuckets; ++i) {
        s += "  floor " + std::to_string(i);
        if (i == kFloorHistogramBuckets - 1) s += "+";
        s += ": " + std::to_string(floor_hist[i]) + " (" + pct(floor_hist[i], rows) +
             ")\n";
    }
    s += "events fired (rows in which the event fired at least once):\n";
    for (const EventName& e : event_names()) {
        const auto v = static_cast<std::size_t>(e.id);
        if (v >= 32 || event_rows[v] == 0) continue;
        s += "  " + std::string(e.game_id) + ": " + std::to_string(event_rows[v]) +
             " (" + pct(event_rows[v], rows) + ")\n";
    }
    if (!relic_rows.empty()) {
        s += "relic targets (rows offered / acquired / shop-while-owned):\n";
        for (const RelicRows& rr : relic_rows) {
            s += "  " + std::string(registry::relic_game_id(rr.id)) + ": " +
                 std::to_string(rr.offered) + " (" + pct(rr.offered, rows) +
                 ") / " + std::to_string(rr.acquired) + " (" +
                 pct(rr.acquired, rows) + ") / " +
                 std::to_string(rr.shop_while_owned) + " (" +
                 pct(rr.shop_while_owned, rows) + ")\n";
        }
    }
    return s;
}

}  // namespace sts::planner
