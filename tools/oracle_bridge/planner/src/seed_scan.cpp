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
#include "sts/registry/encounter_table.hpp"  // kEncounters (boss identity)
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
        // TheCity.initializeEventList (TheCity.java:185-198) -- ids 32..44.
        {EventId::ADDICT, "ADDICT", "Addict"},
        {EventId::BACK_TO_BASICS, "BACK_TO_BASICS", "Back to Basics"},
        {EventId::BEGGAR, "BEGGAR", "Beggar"},
        {EventId::COLOSSEUM, "COLOSSEUM", "Colosseum"},
        {EventId::CURSED_TOME, "CURSED_TOME", "Cursed Tome"},
        {EventId::DRUG_DEALER, "DRUG_DEALER", "Drug Dealer"},
        {EventId::FORGOTTEN_ALTAR, "FORGOTTEN_ALTAR", "Forgotten Altar"},
        {EventId::GHOSTS, "GHOSTS", "Ghosts"},
        {EventId::MASKED_BANDITS, "MASKED_BANDITS", "Masked Bandits"},
        {EventId::NEST, "NEST", "Nest"},
        {EventId::THE_LIBRARY, "THE_LIBRARY", "The Library"},
        {EventId::THE_MAUSOLEUM, "THE_MAUSOLEUM", "The Mausoleum"},
        {EventId::VAMPIRES, "VAMPIRES", "Vampires"},
        // TheBeyond.initializeEventList (TheBeyond.java:179-186) -- ids 45..51.
        {EventId::FALLING, "FALLING", "Falling"},
        {EventId::MIND_BLOOM, "MIND_BLOOM", "MindBloom"},
        {EventId::THE_MOAI_HEAD, "THE_MOAI_HEAD", "The Moai Head"},
        {EventId::MYSTERIOUS_SPHERE, "MYSTERIOUS_SPHERE", "Mysterious Sphere"},
        {EventId::SENSORY_STONE, "SENSORY_STONE", "SensoryStone"},
        {EventId::TOMB_OF_LORD_RED_MASK, "TOMB_OF_LORD_RED_MASK",
         "Tomb of Lord Red Mask"},
        {EventId::WINDING_HALLS, "WINDING_HALLS", "Winding Halls"},
    };
    return kNames;
}

// The guard that makes the hand copy above honest: adding a row to
// registry/events.yaml grows the GENERATED table, and this stops compiling.
// (kEventTable is generated from the same yaml -- sts/registry/event_table.hpp.)
constexpr std::size_t kNameTableSize = 51;
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

bool event_flag_set(uint64_t flags, EventId id) {
    const auto v = static_cast<uint32_t>(id);
    // The combined-word layout the header pins: ids 1..31 at bit (id-1) --
    // the engine's low word verbatim -- and ids 32..63 at bit id, the
    // engine's hi-word bit (id-32) sitting 32 higher. EventId 0 is NONE and
    // never fires; ids past 63 have no bit in either engine word (the engine
    // accessor is a no-op there too), so both bounds stay live guards against
    // an out-of-range shift.
    if (v == 0 || v > 63) return false;
    const unsigned bit = v <= 31 ? v - 1u : v;
    return (flags & (uint64_t{1} << bit)) != 0u;
}

std::vector<EventId> decode_event_flags(uint64_t flags) {
    std::vector<EventId> out;
    for (const EventName& e : event_name_table()) {
        if (event_flag_set(flags, e.id)) out.push_back(e.id);
    }
    return out;
}

std::string event_flags_text(uint64_t flags) {
    std::string s;
    for (const EventName& e : event_name_table()) {
        if (!event_flag_set(flags, e.id)) continue;
        if (!s.empty()) s += '|';
        s.append(e.game_id);
    }
    return s;
}

// --- Act depth ---------------------------------------------------------------

std::string_view encounter_game_id_from_id(uint16_t id) {
    if (id == 0) return {};
    for (const registry::EncounterDef& e : registry::kEncounters) {
        if (static_cast<uint16_t>(e.id) == id) return e.game_id;
    }
    return {};
}

std::string boss_ids_text(const uint16_t (&boss_ids)[kMaxActs]) {
    std::string s;
    for (int i = 0; i < kMaxActs; ++i) {
        if (boss_ids[i] == 0) continue;
        if (!s.empty()) s += '|';
        s += "act" + std::to_string(i + 1) + "=";
        const std::string_view name = encounter_game_id_from_id(boss_ids[i]);
        // An id with no registry row is reported as the NUMBER rather than
        // dropped: a boss the encounter table does not know is a finding about
        // the registry, and silently emitting "" would hide it.
        if (name.empty()) {
            s += "#" + std::to_string(boss_ids[i]);
        } else {
            s.append(name);
        }
    }
    return s;
}

// --- Keys (S3.22) ------------------------------------------------------------

namespace {

struct KeyName {
    uint8_t bit;
    std::string_view name;
};

// Bit order, which is also the order keys_text prints in.
constexpr KeyName kKeyNames[3] = {
    {engine::kKeyEmerald, "emerald"},
    {engine::kKeyRuby, "ruby"},
    {engine::kKeySapphire, "sapphire"},
};

}  // namespace

std::string keys_text(uint8_t keys) {
    std::string s;
    for (const KeyName& k : kKeyNames) {
        if ((keys & k.bit) == 0) continue;
        if (!s.empty()) s += '|';
        s.append(k.name);
    }
    return s;
}

bool key_bit_from_name(std::string_view name, uint8_t& out) {
    std::string lower(name);
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    for (const KeyName& k : kKeyNames) {
        if (lower == k.name) {
            out = k.bit;
            return true;
        }
    }
    return false;
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
    uint64_t event_flags = 0;
    bool treasure = false;
    bool boss = false;
    // S2.42 per-act depth. Every one of these is a max / OR / latch, so the
    // double observation of the terminal controller costs nothing.
    uint8_t max_act = 0;
    uint8_t boss_reached_acts = 0;
    uint8_t boss_killed_acts = 0;
    bool victory = false;
    uint16_t boss_ids[kMaxActs]{};
    // S3.22: keys carried (OR), per-act elite kills, the A20 second Act-3 boss
    // room. All three are latches, same contract.
    uint8_t keys = 0;
    uint8_t elite_killed_acts = 0;
    bool double_boss_room = false;
    std::vector<RelicObs> relics;  // one per target, latched (OR) per step
};

void observe(const engine::RunController& rc, void* ctx) noexcept {
    auto* w = static_cast<Watch*>(ctx);
    const auto floor = static_cast<uint32_t>(rc.run.floor);
    if (floor > w->max_floor) w->max_floor = floor;
    // event_flags is a run-long accumulating bitset (event_framework.cpp's
    // event_flag_set), so OR-ing every observation is the same as reading the
    // terminal value -- and stays correct if a future engine change ever
    // clears a bit. BOTH engine words are taken, in the combined layout the
    // header pins: an Act-2/3 fire lives in event_flags_hi and would
    // otherwise never reach a seed-scan row.
    w->event_flags |= static_cast<uint64_t>(rc.run.event_flags) |
                      (static_cast<uint64_t>(rc.run.event_flags_hi) << 32);
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

    // --- S2.42 per-act depth -------------------------------------------------
    //
    // The three probes, and why each is the one it is, are in the header. The
    // act is read from the SAME controller as the room/phase, so a probe can
    // never be attributed to a neighbouring act across a transition.
    const auto act = static_cast<unsigned>(rc.run.act);
    if (act > w->max_act && act <= static_cast<unsigned>(kMaxActs)) {
        w->max_act = static_cast<uint8_t>(act);
    }
    if (rc.room_type == static_cast<uint8_t>(engine::RoomType::Boss)) {
        w->boss_reached_acts |= act_bit(act);
    }
    // Standing in the post-boss chest IS the act's boss kill: the chest is
    // entered only through the boss reward's proceed. Acts 1-2 only; act 3
    // opens no chest, and its kill is the victory below.
    if (rc.phase == static_cast<uint8_t>(engine::RunPhase::BOSS_TREASURE)) {
        w->boss_killed_acts |= act_bit(act);
    }
    if (engine::run_is_victory(rc)) {
        w->victory = true;
        // S3.32: kActBeyond, not kFinalAct. run_is_victory is the `Spire Heart`
        // dialog's DEATH arm, one floor after the ACT-3 boss, so the kill this
        // latches is act 3's. Once kFinalAct became 4 this would have set the
        // act-4 bit off an act-3 event and made --need-boss-kill-act 4 answer
        // yes for every won run.
        w->boss_killed_acts |= act_bit(engine::kActBeyond);
    }
    // S3.33 SHARPENS THE HEART PROBE, which the header asked for by name. The
    // act-4 boss-kill bit is now written by the ONE thing that can only follow
    // a Corrupt Heart kill: RunVictoryKind::HEART, set at the TrueVictoryRoom's
    // entry (run_advance.cpp) and nowhere else. `victory` above stays true for
    // both kinds -- an Act-3 stop and a true victory are both wins -- so this
    // is an additional bit, not a replacement, and a HEART run correctly
    // reports BOTH act-3 and act-4 kills (it had to kill the Act-3 boss to
    // reach the Door). The bit stays ZERO until S3.41/S3.43 give `The Heart` a
    // registry row and a body; the Act-4 boss ROOM is live as of S3.33 but its
    // encounter still parks, so `--need-heart-kill` answering nothing today is
    // an ENCOUNTER gap, not a probe gap.
    if (engine::run_is_true_victor(rc)) {
        w->boss_killed_acts |= act_bit(engine::kFinalAct);
    }
    // --- S3.22 keys, elite kills, and the A20 double-boss room --------------
    //
    // Keys: an OR over the run, not a terminal read (the event_flags rationale).
    // S3.11's two reward-row claims and the campfire Recall are the three
    // writers; nothing clears a bit.
    w->keys |= rc.run.keys;
    // An elite's reward screen is the elite's death: an Elite room always opens
    // one (only a non-endless TheBeyond BOSS is suppressed, AbstractRoom
    // .java:327), and the room type is still Elite while the screen is up.
    if (rc.room_type == static_cast<uint8_t>(engine::RoomType::Elite) &&
        rc.phase == static_cast<uint8_t>(engine::RunPhase::COMBAT_REWARD)) {
        w->elite_killed_acts |= act_bit(act);
    }
    // The A20 second Act-3 boss room. boss_cursor counts boss rooms COMPLETED,
    // so >= 1 inside an ACT-3 boss room means the first one is already dead and
    // this is goToDoubleBoss's synthetic node -- the exact witness the S2.V2
    // report's §6.1 correction had to reconstruct from max_floor.
    //
    // S3.32: kActBeyond, not kFinalAct. This probe was written when the two
    // were the same number; Act 4 has NO double boss at any ascension
    // (ProceedButton.java:101-109, s3-design §5 trap 8), so at kFinalAct == 4
    // the column would have started reporting the ordinary single Act-4 boss
    // room as a double boss the moment an Act-4 line existed.
    if (rc.room_type == static_cast<uint8_t>(engine::RoomType::Boss) &&
        act == static_cast<unsigned>(engine::kActBeyond) &&
        rc.boss_cursor >= 1) {
        w->double_boss_room = true;
    }

    // Boss identity, mirrored into RunState at act init
    // (run_advance.cpp:1601, :1769) as an ENCOUNTER id. Copied rather than
    // read at the end because a run that dies mid-act still testifies about
    // the boss it was walking towards.
    for (int i = 0; i < kMaxActs && i < engine::kBossIdCap; ++i) {
        if (rc.run.boss_ids[i] != 0) w->boss_ids[i] = rc.run.boss_ids[i];
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
                t.reward_offered = true;
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
                  const std::vector<registry::RelicId>& relic_targets,
                  std::vector<engine::Action>* trajectory_out) {
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
    row.max_act = w.max_act;
    row.boss_reached_acts = w.boss_reached_acts;
    row.boss_killed_acts = w.boss_killed_acts;
    row.victory = w.victory;
    for (int i = 0; i < kMaxActs; ++i) row.boss_ids[i] = w.boss_ids[i];
    row.keys = w.keys;
    row.elite_killed_acts = w.elite_killed_acts;
    row.double_boss_room = w.double_boss_room;
    row.relic_obs = std::move(w.relics);
    row.fail_kind = fuzz::fail_kind_name(result.failure.kind);
    if (trajectory_out != nullptr) {
        *trajectory_out = std::move(result.trajectory);
    }
    return row;
}

// --- Filtering ---------------------------------------------------------------

bool Filter::empty() const {
    return need_events.empty() && !need_treasure && !need_boss &&
           min_floor == 0 && need_relic_offered.empty() &&
           need_relic_reward_offered.empty() && need_relic_acquired.empty() &&
           need_shop_after_relic.empty() && need_boss_reached_act == 0 &&
           need_boss_killed_act == 0 && !need_victory && min_act == 0 &&
           need_boss_ids.empty() && need_keys == 0 && !need_heart_kill;
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
    if (row.max_act < f.min_act) return false;
    if (f.need_boss_reached_act != 0 &&
        !act_bit_set(row.boss_reached_acts, f.need_boss_reached_act)) {
        return false;
    }
    if (f.need_boss_killed_act != 0 &&
        !act_bit_set(row.boss_killed_acts, f.need_boss_killed_act)) {
        return false;
    }
    if (f.need_victory && !row.victory) return false;
    // S3.22 key clauses. ALL-OF on the mask (see the header for why all-of is
    // the satisfiable reading here); the heart clause is the act-4 boss-kill
    // bit, spelled separately because its consumer is S3.23/S3.62. S3.33
    // sharpened the bit's WRITER to run_is_true_victor (above); this reader is
    // unchanged.
    if (f.need_keys != 0 && (row.keys & f.need_keys) != f.need_keys) {
        return false;
    }
    if (f.need_heart_kill &&
        !act_bit_set(row.boss_killed_acts, static_cast<unsigned>(kMaxActs))) {
        return false;
    }
    if (!f.need_boss_ids.empty()) {
        bool any = false;
        for (uint16_t want : f.need_boss_ids) {
            for (int i = 0; i < kMaxActs; ++i) {
                if (row.boss_ids[i] == want && want != 0) any = true;
            }
        }
        if (!any) return false;
    }
    for (EventId id : f.need_events) {
        if (!event_flag_set(row.event_flags, id)) return false;
    }
    if (!any_relic_hits(row.relic_obs, f.need_relic_offered,
                        &RelicObs::offered)) {
        return false;
    }
    if (!any_relic_hits(row.relic_obs, f.need_relic_reward_offered,
                        &RelicObs::reward_offered)) {
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
    // Column order is the contract. The S2.42 columns are APPENDED AFTER
    // `fail_kind` rather than inserted next to `boss`, so a script that has
    // been doing `cut -f10` for the boss column since S1 still selects the
    // boss column.
    return "seed\tseed_int\tpolicy\tpolicy_seed\tascension\tend_reason\tactions\t"
           "max_floor\ttreasure\tboss\tevent_flags\tevents\trelic_obs\t"
           "final_hash\tfail_kind\t"
           "act\tboss_reached_acts\tboss_killed_acts\tvictory\tboss_ids\t"
           // S3.22, appended after boss_ids for the same contract reason.
           "keys\telite_killed_acts\tdouble_boss";
}

namespace {

std::string relic_obs_text(const std::vector<RelicObs>& obs) {
    // `<game_id>=<offered><reward_offered><acquired><shop_while_owned>` per
    // target, '|'-joined
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
        s += o.reward_offered ? '1' : '0';
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
        std::to_string(static_cast<unsigned>(row.max_act)),
        std::to_string(static_cast<unsigned>(row.boss_reached_acts)),
        std::to_string(static_cast<unsigned>(row.boss_killed_acts)),
        row.victory ? "1" : "0",
        boss_ids_text(row.boss_ids),
        keys_text(row.keys),
        std::to_string(static_cast<unsigned>(row.elite_killed_acts)),
        row.double_boss_room ? "1" : "0",
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
        s += std::string("\"reward_offered\":") +
             (o.reward_offered ? "true" : "false") + ",";
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
    s += "\"fail_kind\":\"" + json_escape(row.fail_kind) + "\",";
    // S2.42. The masks are emitted as NUMBERS beside a decoded per-act array:
    // the number is what a script filters on, the array is what a human reads,
    // and neither side has to own a copy of the bit layout -- the same
    // arrangement `event_flags` / `events` already uses above.
    s += "\"act\":" + std::to_string(static_cast<unsigned>(row.max_act)) + ",";
    s += "\"boss_reached_acts\":" +
         std::to_string(static_cast<unsigned>(row.boss_reached_acts)) + ",";
    s += "\"boss_killed_acts\":" +
         std::to_string(static_cast<unsigned>(row.boss_killed_acts)) + ",";
    s += "\"boss_reached\":[";
    first = true;
    for (int a = 1; a <= kMaxActs; ++a) {
        if (!act_bit_set(row.boss_reached_acts, static_cast<unsigned>(a))) continue;
        if (!first) s += ',';
        first = false;
        s += std::to_string(a);
    }
    s += "],\"boss_killed\":[";
    first = true;
    for (int a = 1; a <= kMaxActs; ++a) {
        if (!act_bit_set(row.boss_killed_acts, static_cast<unsigned>(a))) continue;
        if (!first) s += ',';
        first = false;
        s += std::to_string(a);
    }
    s += "],";
    s += std::string("\"victory\":") + (row.victory ? "true" : "false") + ",";
    s += "\"boss_ids\":[";
    first = true;
    for (int i = 0; i < kMaxActs; ++i) {
        if (row.boss_ids[i] == 0) continue;
        if (!first) s += ',';
        first = false;
        s += "{\"act\":" + std::to_string(i + 1) + ",\"id\":" +
             std::to_string(row.boss_ids[i]) + ",\"encounter\":\"" +
             json_escape(encounter_game_id_from_id(row.boss_ids[i])) + "\"}";
    }
    s += "],";
    // S3.22: the number and the array, the same pair shape as event_flags.
    s += "\"keys\":" + std::to_string(static_cast<unsigned>(row.keys)) + ",";
    s += "\"keys_held\":[";
    first = true;
    for (int i = 0; i < 3; ++i) {
        if ((row.keys & static_cast<uint8_t>(1u << i)) == 0) continue;
        if (!first) s += ',';
        first = false;
        s += "\"" + std::string(i == 0   ? "emerald"
                                : i == 1 ? "ruby"
                                         : "sapphire") +
             "\"";
    }
    s += "],\"elite_killed_acts\":" +
         std::to_string(static_cast<unsigned>(row.elite_killed_acts)) + ",";
    s += std::string("\"double_boss\":") +
         (row.double_boss_room ? "true" : "false");
    s += "}";
    return s;
}

// --- Cohort triples ----------------------------------------------------------

CohortTriple cohort_triple(const ScanRow& row) {
    CohortTriple t;
    t.seed = row.seed.text;
    t.policy = row.policy;
    t.policy_seed = row.policy_seed;
    t.boss_reached_acts = row.boss_reached_acts;
    t.boss_killed_acts = row.boss_killed_acts;
    for (int i = 0; i < kMaxActs; ++i) t.boss_ids[i] = row.boss_ids[i];
    t.keys = row.keys;
    return t;
}

std::string_view cohort_tsv_header() {
    return "seed\tpolicy\tpolicy_seed\tboss_reached_acts\tboss_killed_acts\t"
           "boss_ids\tkeys";
}

std::string cohort_triple_to_tsv(const CohortTriple& t) {
    std::string s = t.seed;
    s += '\t';
    s.append(fuzz::policy_name(t.policy));
    s += '\t' + std::to_string(t.policy_seed);
    s += '\t' + std::to_string(static_cast<unsigned>(t.boss_reached_acts));
    s += '\t' + std::to_string(static_cast<unsigned>(t.boss_killed_acts));
    s += '\t' + boss_ids_text(t.boss_ids);
    s += '\t' + keys_text(t.keys);
    return s;
}

std::string row_to_text(const ScanRow& row, Format f) {
    return f == Format::JSONL ? row_to_jsonl(row) : row_to_tsv(row);
}

// --- Aggregate report --------------------------------------------------------

void ActDepth::add(const ScanRow& row) {
    ++rows;
    for (int a = 1; a <= kMaxActs; ++a) {
        const auto act = static_cast<unsigned>(a);
        if (act_bit_set(row.boss_reached_acts, act)) ++boss_reached[a - 1];
        if (act_bit_set(row.boss_killed_acts, act)) ++boss_killed[a - 1];
    }
    if (row.victory) ++victories;
    if (row.end_reason == fuzz::EndReason::ACTION_CAP) ++action_cap;
    // --- S3.22 -------------------------------------------------------------
    for (int a = 1; a <= kMaxActs; ++a) {
        if (act_bit_set(row.elite_killed_acts, static_cast<unsigned>(a))) {
            ++elite_killed[a - 1];
        }
    }
    if (row.double_boss_room) ++double_boss_rooms;
    for (int i = 0; i < 3; ++i) {
        if ((row.keys & static_cast<uint8_t>(1u << i)) != 0) ++key_carry[i];
    }
    if ((row.keys & kAllKeys) == kAllKeys) {
        ++key_carry_all;
        if (row.victory) ++key_carry_all_victory;
    }
}

void ScanSummary::add(const ScanRow& row) {
    ++rows;
    actions += row.actions;
    if (row.treasure_entered) ++treasure_rows;
    if (row.boss_reached) ++boss_rows;
    depth.add(row);
    const auto pk = static_cast<int>(row.policy);
    if (pk >= 0 && pk < static_cast<int>(fuzz::PolicyKind::COUNT)) {
        per_policy[pk].add(row);
    }
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
        if (o.reward_offered) ++rr->reward_offered;
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

// One per-act depth stanza, for the whole scan or for one policy.
std::string depth_text(std::string_view label, const ActDepth& d) {
    std::string s = "depth [" + std::string(label) + "] rows=" +
                    std::to_string(d.rows) + "\n";
    s += "  act boss FIGHT:";
    for (int a = 0; a < kMaxActs; ++a) {
        s += " a" + std::to_string(a + 1) + "=" +
             std::to_string(d.boss_reached[a]) + " (" +
             pct(d.boss_reached[a], d.rows) + ")";
    }
    s += "\n  act boss KILL: ";
    for (int a = 0; a < kMaxActs; ++a) {
        s += " a" + std::to_string(a + 1) + "=" +
             std::to_string(d.boss_killed[a]) + " (" +
             pct(d.boss_killed[a], d.rows) + ")";
    }
    s += "\n  act elite KILL:";
    for (int a = 0; a < kMaxActs; ++a) {
        s += " a" + std::to_string(a + 1) + "=" +
             std::to_string(d.elite_killed[a]) + " (" +
             pct(d.elite_killed[a], d.rows) + ")";
    }
    s += "\n  victories=" + std::to_string(d.victories) + " (" +
         pct(d.victories, d.rows) + ")  action_cap=" +
         std::to_string(d.action_cap) + " (" + pct(d.action_cap, d.rows) +
         ")\n";
    // S3.22: the key block. `keys all three` is the Act-4 door's precondition
    // (SpireHeart.java:151), and `keys all + victory` is the whole of what
    // s3-design §6.1 calls the brutal precondition -- a keyed A20 double-boss
    // win. Both are printed even when zero: a zero here is the reportable
    // result the S3.22 Acceptance names, not a missing line.
    s += "  keys carried:  emerald=" + std::to_string(d.key_carry[0]) + " (" +
         pct(d.key_carry[0], d.rows) + ") ruby=" +
         std::to_string(d.key_carry[1]) + " (" + pct(d.key_carry[1], d.rows) +
         ") sapphire=" + std::to_string(d.key_carry[2]) + " (" +
         pct(d.key_carry[2], d.rows) + ")\n";
    s += "  keys all three=" + std::to_string(d.key_carry_all) + " (" +
         pct(d.key_carry_all, d.rows) + ")  keys all + victory=" +
         std::to_string(d.key_carry_all_victory) + " (" +
         pct(d.key_carry_all_victory, d.rows) + ")\n";
    s += "  double-boss rooms=" + std::to_string(d.double_boss_rooms) + " (" +
         pct(d.double_boss_rooms, d.rows) + ")\n";
    return s;
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
    // S2.42: the per-act depth block IS the reach report's table. Fight and
    // kill are printed on adjacent lines on purpose -- the gap between them is
    // the number the S2-G2 depth bars are about -- and ACTION_CAP sits next to
    // them so a truncation artifact (ScanLimits::max_actions) is visible rather
    // than inferred from a suspiciously low kill rate.
    s += depth_text("all policies", depth);
    for (int p = 0; p < static_cast<int>(fuzz::PolicyKind::COUNT); ++p) {
        if (per_policy[p].rows == 0) continue;
        s += depth_text(
            fuzz::policy_name(static_cast<fuzz::PolicyKind>(p)),
            per_policy[p]);
    }
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
        s += "relic targets (rows offered / reward-offered / acquired / "
             "shop-while-owned):\n";
        for (const RelicRows& rr : relic_rows) {
            s += "  " + std::string(registry::relic_game_id(rr.id)) + ": " +
                 std::to_string(rr.offered) + " (" + pct(rr.offered, rows) +
                 ") / " + std::to_string(rr.reward_offered) + " (" +
                 pct(rr.reward_offered, rows) + ") / " +
                 std::to_string(rr.acquired) + " (" + pct(rr.acquired, rows) +
                 ") / " + std::to_string(rr.shop_while_owned) + " (" +
                 pct(rr.shop_while_owned, rows) + ")\n";
        }
    }
    return s;
}

}  // namespace sts::planner
