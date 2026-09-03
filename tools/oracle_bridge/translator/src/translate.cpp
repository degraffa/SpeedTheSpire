#include "sts/translate/translate.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "sts/diff/trace.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/interp.hpp"  // CHOOSE_CARD encoding for the HAND_SELECT screen
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_deck.hpp"  // the master-deck bottle flag bits
#include "sts/engine/types.hpp"
#include "sts/registry/encounter_table.hpp"  // act_boss -> EncounterId
#include "sts/registry/game_ids.hpp"

// The translator is the one place the game's string ids and the fork's hidden
// oracle block become the sim's binary schema. Every JSON field is dispositioned
// through the typed walker below; provenance for each disposition is
// PROTOCOL.md §3 (stock GameStateConverter catalog) and §5 (the `oracle` block).

namespace sts::translate {
namespace {

using json = nlohmann::json;
namespace eng = sts::engine;
namespace reg = sts::registry;

// ---- error context -------------------------------------------------------

struct Ctx {
    std::string source;
    int record_idx = 0;
    DispositionStats* stats = nullptr;
    // Id-tolerance accounting mode (G4). When set, an unknown content id is
    // tallied here and joined to NONE instead of throwing; unknown fields /
    // stream names / anchor mismatches are unaffected.
    bool tolerate_ids = false;
    std::map<std::string, uint64_t>* unknown_ids = nullptr;
    uint64_t* unknown_id_hits = nullptr;
};

[[nodiscard]] std::string loc(const Ctx& c) {
    return c.source + ":record " + std::to_string(c.record_idx) + ":";
}

// Record one unknown-id occurrence under "<domain>:<id>" (id-tolerance mode).
void tally_unknown_id(const Ctx& c, const char* domain, const std::string& id) {
    if (c.unknown_ids) ++(*c.unknown_ids)[std::string(domain) + ":" + id];
    if (c.unknown_id_hits) ++(*c.unknown_id_hits);
}

// A typed-object reader that enforces the fail-loud disposition policy: it marks
// every key the parser touches, and finish() throws on any key left untouched
// (a field "in no list", design §2.6). It also tallies per-disposition counts.
class FieldReader {
public:
    FieldReader(const json& obj, std::string path, Ctx& ctx)
        : obj_(obj), path_(std::move(path)), ctx_(ctx) {
        if (!obj_.is_object()) {
            throw TranslateError(loc(ctx_) + " expected a JSON object at " + path_ +
                                 " (schema drift, translation aborted)");
        }
    }

    // Raw access; marks the key consumed but tallies nothing (caller decides the
    // disposition). Returns nullptr when absent.
    const json* take(std::string_view key) {
        std::string k(key);
        consumed_.insert(k);
        auto it = obj_.find(k);
        return it == obj_.end() ? nullptr : &*it;
    }
    const json& require(std::string_view key) {
        if (const json* v = take(key)) return *v;
        throw TranslateError(loc(ctx_) + " missing required field " + path_ + "." +
                             std::string(key) + " (schema drift, translation aborted)");
    }

    // Disposition helpers: mark consumed and, when the key is present, tally.
    void ignore(std::string_view key) { if (mark(key)) ++ctx_.stats->ignored; }
    void oracle(std::string_view key) { if (mark(key)) ++ctx_.stats->oracle; }
    void defer(std::string_view key)  { if (mark(key)) ++ctx_.stats->deferred; }
    void mapped()                     { ++ctx_.stats->mapped; }

    void finish() {
        for (auto it = obj_.begin(); it != obj_.end(); ++it) {
            if (!consumed_.count(it.key())) {
                throw TranslateError(
                    loc(ctx_) + " unknown field " + path_ + "." + it.key() +
                    " — not mapped, not on the ignore-list, not deferred "
                    "(schema drift, translation aborted)");
            }
        }
    }

    const std::string& path() const { return path_; }
    Ctx& ctx() const { return ctx_; }

private:
    bool mark(std::string_view key) {
        std::string k(key);
        consumed_.insert(k);
        return obj_.contains(k);
    }
    const json& obj_;
    std::string path_;
    Ctx& ctx_;
    std::unordered_set<std::string> consumed_;
};

// ---- small typed getters (loud on the wrong JSON type) -------------------

[[nodiscard]] int64_t as_i64(const json& v, const Ctx& c, const std::string& where) {
    if (!v.is_number_integer() && !v.is_number_unsigned()) {
        throw TranslateError(loc(c) + " expected integer at " + where);
    }
    return v.get<int64_t>();
}
[[nodiscard]] std::string as_str(const json& v, const Ctx& c, const std::string& where) {
    if (!v.is_string()) throw TranslateError(loc(c) + " expected string at " + where);
    return v.get<std::string>();
}
[[nodiscard]] bool as_bool(const json& v, const Ctx& c, const std::string& where) {
    if (!v.is_boolean()) throw TranslateError(loc(c) + " expected boolean at " + where);
    return v.get<bool>();
}
[[nodiscard]] float as_f32(const json& v, const Ctx& c, const std::string& where) {
    if (!v.is_number()) throw TranslateError(loc(c) + " expected number at " + where);
    // The game's chances are float literals (e.g. MONSTER_CHANCE = 0.1f). The
    // oracle serializes them as JSON numbers; parse as double then narrow to
    // float, which reproduces the original float bit-pattern (round-trip exact
    // for the game's clean literals). RunState stores float (§2.5 #5).
    return static_cast<float>(v.get<double>());
}

// ---- id joins (fail loud on an id the registry does not know) ------------

[[nodiscard]] eng::CardId join_card(const std::string& id, const Ctx& c,
                                    const std::string& where) {
    if (id.empty()) return eng::CardId::NONE;
    eng::CardId cid = reg::card_from_game_id(id);
    if (cid == eng::CardId::NONE) {
        if (c.tolerate_ids) { tally_unknown_id(c, "card", id); return eng::CardId::NONE; }
        throw TranslateError(loc(c) + " unknown card id \"" + id + "\" at " + where +
                             " — registry has no game_id mapping (schema drift, "
                             "translation aborted)");
    }
    return cid;
}
// The ONE non-exact power-id case in the protocol: a power whose POWER_ID is not
// a constant. TheBombPower's ctor builds `POWER_ID + bombIdOffset` from an
// ever-increasing static counter (TheBombPower.java:22,27,31-32), so the oracle
// reports "TheBomb0", "TheBomb1", ... -- one distinct string per instance, none
// of which the registry's exact game_id table can hold. Normalizing the numeric
// suffix back to the "TheBomb" prefix is the whole fix.
//
// SCOPED DELIBERATELY NARROW: it fires only for the literal prefix "TheBomb"
// followed by one or more DIGITS and nothing else, so it can neither swallow an
// unrelated unknown id nor collide with a future power whose id merely starts
// the same way. It is applied only after the exact lookup has failed, so no
// existing join changes. This was checked against the whole power surface --
// join_power is the single power-id door (parse_power is its only caller) and
// TheBombPower is the only game power that builds a non-constant ID.
[[nodiscard]] std::string normalize_instanced_power_id(const std::string& id) {
    constexpr std::string_view kBombPrefix = "TheBomb";
    if (id.size() <= kBombPrefix.size() ||
        id.compare(0, kBombPrefix.size(), kBombPrefix) != 0) {
        return id;
    }
    for (std::size_t i = kBombPrefix.size(); i < id.size(); ++i) {
        if (id[i] < '0' || id[i] > '9') return id;
    }
    return std::string(kBombPrefix);
}

[[nodiscard]] eng::PowerId join_power(const std::string& id, const Ctx& c,
                                      const std::string& where) {
    if (id.empty()) return eng::PowerId::NONE;
    eng::PowerId pid = reg::power_from_game_id(id);
    if (pid == eng::PowerId::NONE) {
        pid = reg::power_from_game_id(normalize_instanced_power_id(id));
    }
    if (pid == eng::PowerId::NONE) {
        if (c.tolerate_ids) { tally_unknown_id(c, "power", id); return eng::PowerId::NONE; }
        throw TranslateError(loc(c) + " unknown power id \"" + id + "\" at " + where +
                             " — registry has no game_id mapping (schema drift, "
                             "translation aborted)");
    }
    return pid;
}
[[nodiscard]] eng::MonsterId join_monster(const std::string& id, const Ctx& c,
                                          const std::string& where) {
    if (id.empty()) return eng::MonsterId::NONE;
    eng::MonsterId mid = reg::monster_from_game_id(id);
    if (mid == eng::MonsterId::NONE) {
        if (c.tolerate_ids) { tally_unknown_id(c, "monster", id); return eng::MonsterId::NONE; }
        throw TranslateError(loc(c) + " unknown monster id \"" + id + "\" at " + where +
                             " — registry has no game_id mapping (schema drift, "
                             "translation aborted)");
    }
    return mid;
}
[[nodiscard]] eng::RelicId join_relic(const std::string& id, const Ctx& c,
                                      const std::string& where) {
    if (id.empty()) return eng::RelicId::NONE;
    eng::RelicId rid = reg::relic_from_game_id(id);
    if (rid == eng::RelicId::NONE) {
        if (c.tolerate_ids) { tally_unknown_id(c, "relic", id); return eng::RelicId::NONE; }
        throw TranslateError(loc(c) + " unknown relic id \"" + id + "\" at " + where +
                             " — registry has no game_id mapping (schema drift, "
                             "translation aborted)");
    }
    return rid;
}
[[nodiscard]] reg::PotionId join_potion(const std::string& id, const Ctx& c,
                                        const std::string& where) {
    if (id.empty() || id == "Potion Slot") return reg::PotionId::NONE;
    reg::PotionId pid = reg::potion_from_game_id(id);
    if (pid == reg::PotionId::NONE) {
        if (c.tolerate_ids) { tally_unknown_id(c, "potion", id); return reg::PotionId::NONE; }
        throw TranslateError(loc(c) + " unknown potion id \"" + id + "\" at " + where +
                             " — registry has no game_id mapping (schema drift, "
                             "translation aborted)");
    }
    return pid;
}
[[nodiscard]] reg::EventId join_event(const std::string& id, const Ctx& c,
                                      const std::string& where) {
    if (id.empty()) return reg::EventId::NONE;
    reg::EventId eid = reg::event_from_game_id(id);
    if (eid == reg::EventId::NONE) {
        if (c.tolerate_ids) {
            tally_unknown_id(c, "event", id);
            return reg::EventId::NONE;
        }
        throw TranslateError(loc(c) + " unknown event id \"" + id + "\" at " +
                             where +
                             " — registry has no game_id mapping (schema drift, "
                             "translation aborted)");
    }
    return eid;
}

// Set the FIRED bit for every set bit of `fired_bits` in a pool block whose
// bit 0 is `first_id`. The FIRED bitset spans TWO RunState words --
// `event_flags` (ids 1..31, bit id-1) and `event_flags_hi` (ids 32..63, bit
// id-32) -- because S2.02's Act-2/3 ids do not fit one uint32_t and neither
// RunState nor PublicView could widen the first word in place (run_state.hpp's
// carve note). Routed one id at a time through the engine accessor so the split
// is stated in exactly one place; the block shift this replaced,
// `<< (first_id - 1)`, would be UB at TheCity's first_id of 32.
void set_event_flag_block(eng::RunState& rs, uint16_t first_id, int count,
                          uint32_t fired_bits) {
    for (int bit = 0; bit < count; ++bit) {
        if ((fired_bits & (1u << bit)) != 0u) {
            eng::event_flag_set(rs, static_cast<uint16_t>(first_id + bit));
        }
    }
}

// Translate one of the three remaining-event arrays into its RunState
// membership bitset. The game initializes each list in the act's canonical
// Java order and only ever removes elements, so every live dump must be a
// subsequence of that order. Validating that property is load-bearing: a
// bitset cannot represent a duplicate or an order mutation, and silently
// accepting either would make later generate_event draws use a different index
// order from the captured state.
//
// TWO ORDERS, NOT ONE (S2.13). The membership BIT is always `id - first_id`
// (registry order, act-independent -- event_framework.hpp explains why the
// bitset must stay byte-comparable across acts). The LIST ORDER an act's dump
// arrives in is a separate question, and for shrines it diverges: Exordium ends
// with Wheel of Change, TheCity and TheBeyond put it second
// (Exordium.java:238-246 vs TheCity.java:210-218 == TheBeyond.java:198-206). So
// the subsequence check runs against a POSITION table, and `order` supplies it;
// a null `order` means "positions are ids", the dense-and-in-add-order case
// that holds for every act's event list and for the special list.
[[nodiscard]] uint32_t parse_event_membership(const json& arr,
                                              const std::string& path,
                                              Ctx& ctx, uint16_t first_id,
                                              int count,
                                              const char* pool_name,
                                              const uint16_t* order = nullptr) {
    if (!arr.is_array()) {
        throw TranslateError(loc(ctx) + " expected array at " + path);
    }
    // position_of[bit] == where that bit's id sits in the act's list order.
    const auto position_of = [&](int bit) {
        if (order == nullptr) {
            return bit;
        }
        for (int p = 0; p < count; ++p) {
            if (order[p] == static_cast<uint16_t>(first_id + bit)) {
                return p;
            }
        }
        return -1;  // unreachable: the order table is a permutation of the block
    };
    uint32_t bits = 0;
    int previous_position = -1;
    for (std::size_t i = 0; i < arr.size(); ++i) {
        const std::string at = path + "[" + std::to_string(i) + "]";
        const reg::EventId eid =
            join_event(as_str(arr[i], ctx, at), ctx, at);
        if (eid == reg::EventId::NONE) {
            // Only reachable in the explicit unknown-id accounting mode.
            // There is no representable bit for an unknown event.
            continue;
        }
        const uint16_t id = static_cast<uint16_t>(eid);
        if (id < first_id ||
            id >= static_cast<uint16_t>(first_id + count)) {
            throw TranslateError(
                loc(ctx) + " event id \"" +
                std::string(reg::event_game_id(eid)) + "\" at " + at +
                " does not belong to oracle." + pool_name);
        }
        const int bit = static_cast<int>(id - first_id);
        const int position = position_of(bit);
        if (position <= previous_position) {
            throw TranslateError(
                loc(ctx) + " " + path +
                " is not a canonical-order subsequence (duplicate or "
                "out-of-order event at index " + std::to_string(i) + ")");
        }
        bits |= 1u << bit;
        previous_position = position;
    }
    return bits;
}

// ---- card / power parsers (PROTOCOL §3.13 / §3.14) -----------------------

// `master_deck` is true only on the RunState `deck` walk: the fork's
// in_bottle_* booleans (PROTOCOL §3.13, absent == false so pre-addition
// captures translate unchanged) become the engine's MASTER-DECK bottle flag
// bits there. On every other walk (combat piles, reward offers, grids) the
// keys are consumed and DROPPED: combat `flags` are registry-derived
// `CardFlag`s -- a different namespace, where bit 0 means EXHAUST -- and the
// bottled instance's combat rendering is the INNATE bit the combat builder
// derives itself (run_deck.hpp's encoding note).
// `vitals`, when given, receives the card's IDENTITY by game id -- the raw
// capture string, kept even when the registry join fails under tolerant
// translation, so `--vitals` can name an unresolved id (combat_vitals.hpp).
[[nodiscard]] eng::CardInstance parse_card(const json& j, const std::string& path,
                                           Ctx& ctx, bool master_deck = false,
                                           VitalsCard* vitals = nullptr) {
    FieldReader fr(j, path, ctx);
    eng::CardInstance ci{};
    struct BottleKey {
        const char* key;
        uint16_t bit;
    };
    static constexpr BottleKey kBottleKeys[] = {
        {"in_bottle_flame", eng::kMasterCardInBottleFlame},
        {"in_bottle_lightning", eng::kMasterCardInBottleLightning},
        {"in_bottle_tornado", eng::kMasterCardInBottleTornado},
    };
    for (const BottleKey& bk : kBottleKeys) {
        if (const json* b = fr.take(bk.key)) {
            if (as_bool(*b, ctx, path + "." + bk.key) && master_deck) {
                ci.flags = static_cast<uint16_t>(ci.flags | bk.bit);
            }
            fr.mapped();
        }
    }
    // id -> card_id (the translator join key, §2.6). Mapped.
    const std::string game_id = as_str(fr.require("id"), ctx, path + ".id");
    ci.card_id = static_cast<uint16_t>(join_card(game_id, ctx, path + ".id"));
    fr.mapped();
    if (vitals != nullptr) {
        vitals->id = game_id;
        vitals->known = ci.card_id != static_cast<uint16_t>(eng::CardId::NONE);
    }
    if (const json* u = fr.take("upgrades")) {  // timesUpgraded -> upgrade
        const int64_t up = as_i64(*u, ctx, path + ".upgrades");
        ci.upgrade = static_cast<uint8_t>(up);
        if (vitals != nullptr) vitals->upgrades = static_cast<int>(up);
        fr.mapped();
    }
    if (const json* cost = fr.take("cost")) {  // costForTurn -> cost_now (>=0 only)
        int64_t cv = as_i64(*cost, ctx, path + ".cost");
        ci.cost_now = static_cast<uint8_t>(cv < 0 ? 0 : cv);
        // The vitals projection keeps the number RAW, sentinels and all: -1
        // (X-cost) and -2 (unplayable) are real claims about the card that
        // CardInstance::cost_now, being unsigned, cannot hold, and the
        // `--costs` compare reconstructs the same two values on the sim side
        // from the XCOST / UNPLAYABLE flags (combat_vitals.hpp).
        if (vitals != nullptr) {
            vitals->cost = static_cast<int>(cv);
            vitals->cost_known = true;
        }
        fr.mapped();
    } else if (vitals != nullptr) {
        vitals->cost_known = false;
    }
    if (const json* m = fr.take("misc")) {  // per-instance misc (§3.13) -> CardInstance.misc
        ci.misc = static_cast<uint16_t>(as_i64(*m, ctx, path + ".misc"));
        fr.mapped();
    }
    fr.ignore("name");   // localization
    fr.ignore("uuid");   // nondeterministic per-instance id (§3.13)
    // Presentation / derivable-from-registry flags with no per-instance schema
    // storage yet (they live in the CardDef table, not CardInstance).
    fr.defer("is_playable");
    fr.defer("type");
    fr.defer("rarity");
    fr.defer("has_target");
    fr.defer("exhausts");
    fr.defer("ethereal");
    // Shop-only overlay (§3.9). Still storage-less -- a shop is derived state,
    // not RunState -- but TYPE-CHECKED now, so a price that arrives as a string
    // or a float is drift rather than a silently deferred key.
    if (const json* pr = fr.take("price")) {
        (void)as_i64(*pr, ctx, path + ".price");
    }
    fr.defer("price");
    fr.finish();
    return ci;
}

[[nodiscard]] eng::PowerSlot parse_power(const json& j, const std::string& path,
                                         Ctx& ctx, uint32_t* player_combat_flags,
                                         VitalsPower* vitals = nullptr) {
    FieldReader fr(j, path, ctx);
    eng::PowerSlot ps{};
    const std::string game_id = as_str(fr.require("id"), ctx, path + ".id");
    ps.power_id = static_cast<uint16_t>(join_power(game_id, ctx, path + ".id"));
    fr.mapped();
    if (vitals != nullptr) {
        // A RESOLVED power is keyed by its JOINED id, an unresolved one by the
        // raw capture string (combat_vitals.hpp: an id the registry does not
        // know must be nameable verbatim).
        //
        // The raw string cannot be the key for a resolved power. `powers` is a
        // MULTISET keyed by game id, and the sim side builds its key from the
        // registry game id -- which for TheBombPower is the constant
        // "TheBomb", while the live id is "TheBomb" plus an ever-increasing
        // STATIC counter (TheBombPower.java:22,27,31-32). Keying the capture
        // by the raw string therefore guaranteed a mismatch on every record a
        // fuse was alive, with equal amounts on both sides. Witness (S3.23):
        // s323_STS508459_keys, floor 33, seq 485-506 -- 22 consecutive vitals
        // records reporting `TheBomb0: 3 -> (absent)` beside `TheBomb:
        // (absent) -> 3`. Two live fuses still read distinctly, as the
        // multiset's sorted amounts (`TheBomb: [3, 1]`), which is the
        // projection's own contract.
        vitals->known = ps.power_id != static_cast<uint16_t>(eng::PowerId::NONE);
        vitals->id = vitals->known ? normalize_instanced_power_id(game_id)
                                   : game_id;
    }
    if (const json* a = fr.take("amount")) {
        const int64_t amt = as_i64(*a, ctx, path + ".amount");
        ps.amount = static_cast<int16_t>(amt);
        if (vitals != nullptr) vitals->amount = static_cast<int>(amt);
        fr.mapped();
    }
    fr.ignore("name");
    // GameStateConverter emits `damage` by REFLECTION over the field name
    // (convertCreaturePowersToJson, GameStateConverter.java:895-903:
    // getFieldIfExists(power, "damage")), so it is present for exactly the powers
    // that declare a private `damage` -- which is exactly the set that maps onto
    // the schema-6 PowerSlot.counter. It is imported for the two such powers in
    // the registry and left deferred for every other power that happens to carry
    // one, matching the narrow claim the Combust `misc` case below makes:
    //   * Panache -- the accumulated damage (PanachePower.java:28,48). `amount`
    //     is the 5-card countdown and joins on its own, above.
    //   * The Bomb -- the per-instance damage (TheBombPower.java:26,35). `amount`
    //     is the fuse in turns. Each instance is its own slot (`instanced`), and
    //     the oracle's per-instance id is normalized by join_power above.
    if (ps.power_id == static_cast<uint16_t>(eng::PowerId::PANACHE) ||
        ps.power_id == static_cast<uint16_t>(eng::PowerId::THE_BOMB)) {
        const int64_t dmg = as_i64(fr.require("damage"), ctx, path + ".damage");
        if (dmg < -32768 || dmg > 32767) {
            throw TranslateError(loc(ctx) + " power damage at " + path +
                                 ".damage must fit PowerSlot.counter (int16)");
        }
        ps.counter = static_cast<int16_t>(dmg);
        fr.mapped();
    } else {
        fr.defer("damage");    // optional intent damage (§3.14)
    }
    fr.defer("card");          // optional nested card (Nightmare etc.)
    // S3.21 (c) / PROTOCOL §3.14: the five-way `misc` union is TAGGED from the
    // 2026-09-03 fork on. `misc_field` names which private field the value was
    // read from (basePower / maxAmt / storedAmount / hpLoss /
    // cardsDoubledThisTurn). It is present exactly when `misc` is, and only on
    // captures made by the new jar -- so the contract is two-sided and
    // backward-compatible: an OLD capture carries no tag and the union is
    // resolved by inference from the power id exactly as it always was; a NEW
    // capture carries the tag and the inference is VERIFIED against it, which
    // turns a silent misread into a loud one. The tag is read here, before the
    // Combust arm, so both arms see it.
    std::string misc_field;
    bool has_misc_field = false;
    if (const json* mf = fr.take("misc_field")) {
        misc_field = as_str(*mf, ctx, path + ".misc_field");
        has_misc_field = true;
        static constexpr std::string_view kMiscFields[] = {
            "basePower", "maxAmt", "storedAmount", "hpLoss",
            "cardsDoubledThisTurn"};
        bool known = false;
        for (const std::string_view f : kMiscFields)
            known = known || (misc_field == f);
        if (!known) {
            throw TranslateError(loc(ctx) + " unknown power misc_field \"" +
                                 misc_field + "\" at " + path +
                                 ".misc_field — PROTOCOL §3.14 names exactly "
                                 "five union members (schema drift, "
                                 "translation aborted)");
        }
        if (!j.contains("misc")) {
            throw TranslateError(loc(ctx) + " power at " + path +
                                 " carries misc_field without misc — the tag "
                                 "is emitted only alongside its value "
                                 "(PROTOCOL §3.14)");
        }
        fr.oracle("misc_field");
    }
    if (ps.power_id == static_cast<uint16_t>(eng::PowerId::COMBUST) &&
        player_combat_flags != nullptr) {
        // GameStateConverter's first-present `misc` field is CombustPower's
        // private hpLoss for this specific player-owned power. B3.7 stores that
        // counter in reserved CombatState.flags bits; no other power `misc`
        // field is claimed here. With a tagged capture the "this is hpLoss"
        // claim stops being an inference: assert it.
        if (has_misc_field && misc_field != "hpLoss") {
            throw TranslateError(
                loc(ctx) + " Combust misc at " + path +
                " is tagged misc_field=\"" + misc_field +
                "\", not \"hpLoss\" — the translator's union inference and "
                "the fork's tag disagree (PROTOCOL §3.14)");
        }
        const int64_t hp_loss =
            as_i64(fr.require("misc"), ctx, path + ".misc");
        constexpr uint32_t kMaxCombustHpLoss =
            eng::kCombatFlagCombustHpLossMask >> eng::kCombatFlagCombustHpLossShift;
        if (hp_loss < 1 || hp_loss > static_cast<int64_t>(kMaxCombustHpLoss)) {
            throw TranslateError(
                loc(ctx) + " Combust misc/hpLoss at " + path +
                ".misc must be in [1, " + std::to_string(kMaxCombustHpLoss) +
                "] to fit CombatState.flags");
        }
        *player_combat_flags =
            (*player_combat_flags & ~eng::kCombatFlagCombustHpLossMask) |
            (static_cast<uint32_t>(hp_loss) << eng::kCombatFlagCombustHpLossShift);
        fr.mapped();
    } else {
        fr.defer("misc");      // other optional first-present bookkeeping values
    }
    fr.defer("just_applied");
    fr.finish();
    return ps;
}

// Parse a powers list into a fixed slot array + count. Loud on overflow.
void parse_powers(const json& arr, const std::string& path, Ctx& ctx,
                  eng::PowerSlot* slots, uint8_t& count,
                  uint32_t* player_combat_flags = nullptr,
                  std::vector<VitalsPower>* vitals = nullptr) {
    if (!arr.is_array()) throw TranslateError(loc(ctx) + " expected array at " + path);
    if (arr.size() > eng::kPowerCap) {
        throw TranslateError(loc(ctx) + " " + path + " has " +
                             std::to_string(arr.size()) + " powers > kPowerCap (" +
                             std::to_string(eng::kPowerCap) + ")");
    }
    count = 0;
    for (std::size_t i = 0; i < arr.size(); ++i) {
        VitalsPower vp;
        slots[count++] = parse_power(arr[i], path + "[" + std::to_string(i) + "]",
                                     ctx, player_combat_flags,
                                     vitals != nullptr ? &vp : nullptr);
        if (vitals != nullptr) vitals->push_back(std::move(vp));
    }
}

// ---- monster (PROTOCOL §3.12) --------------------------------------------

// `vitals`, when given, receives the slot's identity / hp / block / liveness
// flags and its power list by game id (combat_vitals.hpp).
[[nodiscard]] eng::MonsterState parse_monster(const json& j, const std::string& path,
                                              Ctx& ctx, VitalsMonster* vitals = nullptr) {
    FieldReader fr(j, path, ctx);
    eng::MonsterState ms{};
    const std::string game_id = as_str(fr.require("id"), ctx, path + ".id");
    ms.monster_id = static_cast<uint16_t>(join_monster(game_id, ctx, path + ".id"));
    fr.mapped();
    if (vitals != nullptr) {
        vitals->id = game_id;
        vitals->known = ms.monster_id != static_cast<uint16_t>(eng::MonsterId::NONE);
    }
    ms.hp = static_cast<int16_t>(as_i64(fr.require("current_hp"), ctx, path + ".current_hp"));
    if (vitals != nullptr) vitals->hp = ms.hp;
    fr.mapped();
    ms.max_hp = static_cast<int16_t>(as_i64(fr.require("max_hp"), ctx, path + ".max_hp"));
    fr.mapped();
    if (const json* b = fr.take("block")) {
        ms.block = static_cast<int16_t>(as_i64(*b, ctx, path + ".block"));
        if (vitals != nullptr) vitals->block = ms.block;
        fr.mapped();
    }
    if (const json* mv = fr.take("move_id")) {
        // move_id (EnemyMoveInfo.nextMove) is the SEMANTIC next-move anchor; the
        // monster's telegraphed move/intent is derived from it here, never from
        // the display `intent` string. Reclassified PROTOCOL §3.12 S->D at design
        // §11 v0.1.2 / B1.3: a stripped capture can carry intent=="DEBUG" on a
        // living monster while move_id stays byte-identical, so anchoring on the
        // string would corrupt the move; anchoring on move_id does not.
        ms.intent = static_cast<uint8_t>(as_i64(*mv, ctx, path + ".move_id"));
        fr.mapped();
    }
    if (const json* pw = fr.take("powers")) {
        parse_powers(*pw, path + ".powers", ctx, ms.powers, ms.power_count, nullptr,
                     vitals != nullptr ? &vitals->powers : nullptr);
        fr.mapped();
    }
    fr.ignore("name");            // localization
    // Display-derived (PROTOCOL §3.12 disposition D, reclassified from S at design
    // §11 v0.1.2 / B1.3): the intent banner enum and its shown damage, both a
    // presentation of the move_id anchor above. `intent` reads "DEBUG" on a living
    // monster until the banner refreshes; `move_adjusted_damage` == -1 in exactly
    // that state. The translator ignores both and reconstructs from move_id.
    fr.ignore("intent");
    fr.ignore("move_adjusted_damage");
    fr.defer("move_base_damage");  // semantic pre-power damage; no MonsterState slot yet
    fr.defer("move_hits");         // semantic attack multiplier; no slot yet
    // The two liveness flags (AbstractMonster.halfDead; isDeadOrEscaped) are
    // read into the vitals projection -- typed, like `price` -- but stay
    // DEFERRED as a schema disposition: MonsterState carries kMonsterFlagHalfDead
    // and kMonsterFlagEscaped, yet `is_gone` folds isDying into one boolean the
    // flag word cannot hold without inferring which, so neither is written
    // into the struct here.
    if (const json* hd = fr.take("half_dead")) {
        const bool v = as_bool(*hd, ctx, path + ".half_dead");
        if (vitals != nullptr) vitals->half_dead = v;
    }
    fr.defer("half_dead");
    if (const json* ig = fr.take("is_gone")) {
        const bool v = as_bool(*ig, ctx, path + ".is_gone");
        if (vitals != nullptr) vitals->gone = v;
    }
    fr.defer("is_gone");
    fr.oracle("last_move_id");        // stock 2-back; authoritative = oracle move history (§2.5 #9)
    fr.oracle("second_last_move_id"); // "
    fr.finish();
    return ms;
}

// ---- player (PROTOCOL §3.15) writes into CombatState ---------------------

void parse_player(const json& j, const std::string& path, Ctx& ctx,
                  eng::CombatState& cs, CombatVitals* vitals = nullptr) {
    FieldReader fr(j, path, ctx);
    cs.player_hp = static_cast<int16_t>(as_i64(fr.require("current_hp"), ctx, path + ".current_hp"));
    fr.mapped();
    cs.player_max_hp = static_cast<int16_t>(as_i64(fr.require("max_hp"), ctx, path + ".max_hp"));
    fr.mapped();
    if (const json* b = fr.take("block")) {
        cs.player_block = static_cast<int16_t>(as_i64(*b, ctx, path + ".block"));
        if (vitals != nullptr) vitals->player_block = cs.player_block;
        fr.mapped();
    }
    if (const json* e = fr.take("energy")) {
        cs.player_energy = static_cast<int16_t>(as_i64(*e, ctx, path + ".energy"));
        if (vitals != nullptr) vitals->player_energy = cs.player_energy;
        fr.mapped();
    }
    if (const json* pw = fr.take("powers")) {
        parse_powers(*pw, path + ".powers", ctx, cs.player_powers,
                     cs.player_power_count, &cs.flags,
                     vitals != nullptr ? &vitals->player_powers : nullptr);
        fr.mapped();
    }
    fr.defer("orbs");  // §3.18; Ironclad has no orbs, no schema storage
    fr.finish();
}

// ---- piles: build the shared card_pool + index lists ---------------------

// Append the cards of one pile to the shared pool and fill the pile's index
// list + count. Deterministic (same JSON -> same pool layout), which is what the
// B1.5 round-trip acceptance needs; it is NOT claimed to match a live sim's pool
// ordering (that is combat-replay equivalence, out of B1.5 scope).
void parse_pile(const json& arr, const std::string& path, Ctx& ctx,
                eng::CombatState& cs, int& pool_used, eng::CardPoolIndex* out,
                uint8_t& count, int cap, std::vector<VitalsCard>* vitals = nullptr) {
    if (!arr.is_array()) throw TranslateError(loc(ctx) + " expected array at " + path);
    if (static_cast<int>(arr.size()) > cap) {
        throw TranslateError(loc(ctx) + " " + path + " has " +
                             std::to_string(arr.size()) + " cards > pile cap (" +
                             std::to_string(cap) + ")");
    }
    count = 0;
    for (std::size_t i = 0; i < arr.size(); ++i) {
        if (pool_used >= eng::kCardPoolCap) {
            throw TranslateError(loc(ctx) + " combat card_pool overflow (> " +
                                 std::to_string(eng::kCardPoolCap) + ") at " + path);
        }
        VitalsCard vc;
        eng::CardInstance ci = parse_card(arr[i], path + "[" + std::to_string(i) + "]", ctx,
                                          /*master_deck=*/false,
                                          vitals != nullptr ? &vc : nullptr);
        if (vitals != nullptr) vitals->push_back(std::move(vc));
        eng::CardPoolIndex idx = static_cast<eng::CardPoolIndex>(pool_used);
        cs.card_pool[pool_used++] = ci;
        out[count++] = idx;
    }
}

// ---- oracle stream (PROTOCOL §5.2): {counter, s0, s1}, s0/s1 signed longs --

[[nodiscard]] eng::RngStream parse_stream(const json& j, const std::string& path,
                                          Ctx& ctx) {
    FieldReader fr(j, path, ctx);
    eng::RngStream s{};
    s.counter = static_cast<int32_t>(as_i64(fr.require("counter"), ctx, path + ".counter"));
    // s0/s1 emitted as SIGNED Java longs; reinterpret the bits into uint64 so the
    // raw xorshift128+ state is preserved exactly (design §2.5 #1).
    s.s0 = static_cast<uint64_t>(as_i64(fr.require("s0"), ctx, path + ".s0"));
    s.s1 = static_cast<uint64_t>(as_i64(fr.require("s1"), ctx, path + ".s1"));
    fr.finish();
    return s;
}

// The 14 stream names the oracle can emit (PROTOCOL §5.2). Any other key inside
// `streams` is drift.
constexpr std::array<std::string_view, 7> kRunStreams = {
    "monsterRng", "eventRng", "merchantRng", "cardRng",
    "treasureRng", "relicRng", "potionRng"};
constexpr std::array<std::string_view, 5> kFloorStreams = {
    "monsterHpRng", "aiRng", "shuffleRng", "cardRandomRng", "miscRng"};

// Route each stream to its schema home. Run streams + mapRng -> RunState; the 5
// floor streams -> CombatState (only meaningful when in combat); neowRng is
// deferred (no schema storage — §2.5 #2, B4.3). Unknown stream name -> drift.
void parse_streams(const json& j, const std::string& path, Ctx& ctx,
                   eng::RunState& rs, eng::CombatState& cs) {
    FieldReader fr(j, path, ctx);
    auto route_run = [&](std::string_view name, eng::RngStream& dst) {
        if (const json* v = fr.take(name)) {
            dst = parse_stream(*v, path + "." + std::string(name), ctx);
            fr.mapped();
        }
    };
    route_run("monsterRng", rs.monster_rng);
    route_run("eventRng", rs.event_rng);
    route_run("merchantRng", rs.merchant_rng);
    route_run("cardRng", rs.card_rng);
    route_run("treasureRng", rs.treasure_rng);
    route_run("relicRng", rs.relic_rng);
    route_run("potionRng", rs.potion_rng);
    route_run("mapRng", rs.map_rng);  // act-scoped, lives in RunState

    route_run("monsterHpRng", cs.monster_hp_rng);
    route_run("aiRng", cs.ai_rng);
    route_run("shuffleRng", cs.shuffle_rng);
    route_run("cardRandomRng", cs.card_random_rng);
    route_run("miscRng", cs.misc_rng);

    // neowRng: the event-scoped 14th stream (§2.5 #2). B4.3 gave it RunState
    // storage, so it is now MAPPED when present (it appears only at floor 0 /
    // Neow; most in-dungeon dumps omit it, in which case route_run maps nothing).
    route_run("neowRng", rs.neow_rng);
    fr.finish();
}

// ---- oracle block (PROTOCOL §5.1) ----------------------------------------

struct OracleAnchors {
    int64_t seed = 0;
    int64_t floor = 0;
    int64_t act = 0;
    int64_t ascension = 0;
    float playtime = 0.0f;      // CardCrawlGame.playtime, seconds (0 if absent)
    bool has_playtime = false;  // pre-2026-08-26 captures lack the field
    // S3.21 (a) / PROTOCOL §5.6. `has_keys` is false for every capture made
    // before the 2026-09-03 redeploy; those keep RunState::keys at whatever the
    // rest of the translation left it (0), which is byte-for-byte the
    // pre-S3.21 behaviour. Only a capture that actually carries the fields
    // writes them, so no existing verdict can move.
    bool has_keys = false;
    std::string dungeon_id;     // AbstractDungeon.id ("" if absent)
};

[[nodiscard]] OracleAnchors parse_oracle(const json& j, const std::string& path, Ctx& ctx,
                                         eng::RunState& rs, eng::CombatState& cs) {
    FieldReader fr(j, path, ctx);
    OracleAnchors a;
    a.seed = as_i64(fr.require("seed"), ctx, path + ".seed");
    a.floor = as_i64(fr.require("floor"), ctx, path + ".floor");
    a.act = as_i64(fr.require("act"), ctx, path + ".act");
    a.ascension = as_i64(fr.require("ascension"), ctx, path + ".ascension");
    // anchors are cross-checked against stock top-level by the caller.

    // playtime (s2-design §5 trap 5): wall-clock seconds the fork records so a
    // violated SecretPortal >= 800s pin is DETECTABLE. Still NEVER translated
    // into RunState and still dispositioned `oracle` -- it is not save-parity
    // state the differ compares, and the sim never advances it.
    //
    // S2.43 (2026-08-27) additionally READS it: it is the one input
    // SecretPortal's getShrine gate needs, and a missing SecretPortal shortens
    // getShrine's `tmp` and therefore moves the drawn INDEX, so without it
    // every Act-3 shrine draw past 800 s replays as the wrong event. `--replay`
    // hands it to `RunController::playtime_seconds` per record; see
    // event_framework.hpp's PLAYTIME block.
    //
    // A disposition mark, not a require(): pre-redeploy captures lack the
    // field, and absence is legal -- `has_playtime` stays false and the
    // consumer keeps the engine's 0.0f (i.e. the pin).
    if (const json* pt = fr.take("playtime")) {
        a.playtime = as_f32(*pt, ctx, path + ".playtime");
        a.has_playtime = true;
    }
    fr.oracle("playtime");  // the disposition tally, unchanged by the read above

    // -- S3.21 (a): the dungeon IDENTITY and the three run keys (PROTOCOL
    //    §5.6). Both arrived with the 2026-09-03 redeploy, so both are
    //    take()-and-check rather than require(): absence is legal and means
    //    "capture predates the redeploy".
    //
    //    `dungeonId` gets no schema field -- `act` already names acts 1..3
    //    uniquely and Act 4 is act 4 -- but it is not merely deferred either:
    //    it is the one string the GAME branches on (DungeonMap.java:68's
    //    `id.equals("TheEnding")`, getShrine's SecretPortal arm), so it is
    //    cross-checked against the act anchor. A dump whose id and act
    //    disagree is a capture the differ must not silently score.
    if (const json* did = fr.take("dungeonId")) {
        a.dungeon_id = as_str(*did, ctx, path + ".dungeonId");
        static constexpr std::string_view kDungeonIdByAct[] = {
            "Exordium", "TheCity", "TheBeyond", "TheEnding"};
        if (a.act >= 1 && a.act <= 4 &&
            a.dungeon_id != kDungeonIdByAct[a.act - 1]) {
            throw TranslateError(loc(ctx) + " oracle.dungeonId \"" +
                                 a.dungeon_id + "\" does not match oracle.act " +
                                 std::to_string(a.act) + " (expected \"" +
                                 std::string(kDungeonIdByAct[a.act - 1]) +
                                 "\") — anchor mismatch, translation aborted");
        }
        fr.oracle("dungeonId");
    }
    {
        const json* ruby = fr.take("hasRubyKey");
        const json* emerald = fr.take("hasEmeraldKey");
        const json* sapphire = fr.take("hasSapphireKey");
        const int present = (ruby != nullptr) + (emerald != nullptr) +
                            (sapphire != nullptr);
        if (present != 0 && present != 3) {
            throw TranslateError(loc(ctx) + " oracle key block at " + path +
                                 " is partial (" + std::to_string(present) +
                                 "/3) — the fork emits all three or none "
                                 "(PROTOCOL §5.6)");
        }
        if (present == 3) {
            a.has_keys = true;
            uint8_t keys = 0;
            if (as_bool(*ruby, ctx, path + ".hasRubyKey")) keys |= eng::kKeyRuby;
            if (as_bool(*emerald, ctx, path + ".hasEmeraldKey"))
                keys |= eng::kKeyEmerald;
            if (as_bool(*sapphire, ctx, path + ".hasSapphireKey"))
                keys |= eng::kKeySapphire;
            rs.keys = keys;
            fr.mapped();
        }
    }
    // The fourth conjunct of the same gate (SpireHeart.java:151,
    // MonsterRoomElite.java:90). It is a PROFILE unlock, not run state -- the
    // sanctioned save is fully unlocked (design §1.1), so it is true on every
    // campaign capture -- and it therefore has no schema home. Consumed as a
    // known oracle field so a future capture taken on a locked profile is
    // visible in the artifact rather than only in a divergence.
    fr.oracle("isFinalActAvailable");

    parse_streams(fr.require("streams"), path + ".streams", ctx, rs, cs);
    fr.mapped();

    rs.card_blizz_randomizer =
        static_cast<int16_t>(as_i64(fr.require("cardBlizzRandomizer"), ctx,
                                    path + ".cardBlizzRandomizer"));
    fr.mapped();
    rs.blizzard_potion_mod =
        static_cast<int16_t>(as_i64(fr.require("blizzardPotionMod"), ctx,
                                    path + ".blizzardPotionMod"));
    fr.mapped();

    // -- B4.3 un-deferral: the §2.5 items whose storage B4.3 added AND which are
    //    representable now (numeric / count -- no content-id registry needed). --

    // eventPity: EventHelper MONSTER/SHOP/TREASURE_CHANCE floats (§2.5 #5). The
    // sub-object's keys are enforced fail-loud too (an extra key is drift).
    if (const json* ep = fr.take("eventPity")) {
        FieldReader epr(*ep, path + ".eventPity", ctx);
        rs.event_pity_monster = as_f32(epr.require("monster"), ctx, path + ".eventPity.monster");
        rs.event_pity_shop = as_f32(epr.require("shop"), ctx, path + ".eventPity.shop");
        rs.event_pity_treasure = as_f32(epr.require("treasure"), ctx, path + ".eventPity.treasure");
        epr.finish();
        fr.mapped();
    }
    // purgeCost: shop purge ramp (§2.5 #6).
    rs.purge_cost = static_cast<int16_t>(as_i64(fr.require("purgeCost"), ctx, path + ".purgeCost"));
    fr.mapped();

    // -- relicPools (§2.5 #8): UN-DEFERRED. The RunState storage
    //    (relic_pools[5][kRelicPoolCap] + relic_pool_count[5]) has existed since
    //    the schema-v3 additive pass; what was missing was a COMPLETE
    //    registry/relics.yaml, because join_relic is fail-loud -- a single
    //    unregistered game_id in any of the five arrays throws and aborts the
    //    whole translation. That is why this key waited for the last relic tier
    //    rather than landing tier by tier: a partial registry would have turned
    //    every real capture into an error instead of a partial mapping.
    //
    //    The five keys are mapped by NAME, not by array position -- the oracle
    //    emits the object with its own key order (uncommon/shop/boss/common/rare
    //    in the captures), while RunState indexes pools by RelicPool
    //    (0=Common..4=Boss). Reading them positionally would silently transpose
    //    the pools. Each tier's array is the POST-shuffle dungeon order, so it is
    //    copied verbatim: this is a state translation, not a re-derivation. --
    if (const json* rp = fr.take("relicPools")) {
        FieldReader rpr(*rp, path + ".relicPools", ctx);
        struct PoolKey { const char* key; int index; };
        static constexpr PoolKey kPools[] = {
            {"common",   0}, {"uncommon", 1}, {"rare", 2},
            {"shop",     3}, {"boss",     4},
        };
        for (const PoolKey& pk : kPools) {
            const std::string where = path + ".relicPools." + pk.key;
            const json& arr = rpr.require(pk.key);
            if (!arr.is_array()) {
                throw TranslateError(loc(ctx) + " expected array at " + where);
            }
            if (arr.size() > eng::kRelicPoolCap) {
                throw TranslateError(loc(ctx) + " " + where + " has " +
                                     std::to_string(arr.size()) +
                                     " > kRelicPoolCap (" +
                                     std::to_string(eng::kRelicPoolCap) + ")");
            }
            std::size_t n = 0;
            for (std::size_t i = 0; i < arr.size(); ++i) {
                const std::string at = where + "[" + std::to_string(i) + "]";
                rs.relic_pools[pk.index][n++] = static_cast<uint16_t>(
                    join_relic(as_str(arr[i], ctx, at), ctx, at));
            }
            rs.relic_pool_count[pk.index] = static_cast<uint8_t>(n);
            rpr.mapped();
        }
        rpr.finish();
        fr.mapped();
    }

    // -- B4.10 un-deferral: remaining event/shrine/special lists (§2.5 #7).
    //    B4.3 added the storage; events.yaml now supplies the complete
    //    fail-loud game_id join and pins ids in the canonical Java list
    //    orders. Each captured list is a removal-only subsequence, so mapping
    //    validates that order before collapsing it to a bitset.
    //
    //    S2.13 made all three ACT-AWARE. `a.act` is this record's own oracle
    //    anchor (parsed at the top of this function), so the widths and orders
    //    come from the act the dump was taken in:
    //      eventList  Exordium 1..11 / TheCity 32..44 / TheBeyond 45..51,
    //                 each dense and in add order, so bit == position.
    //      shrineList the same six ids in every act but in TWO DIFFERENT
    //                 ORDERS -- hence the explicit order table, without which
    //                 an Act-2 dump's `[Match and Keep!, Wheel of Change, ...]`
    //                 would be rejected as "not a canonical-order subsequence".
    //      specials   act-independent (never rebuilt; see below).
    //
    //    THE FIRED-FLAG DERIVATION IS ACT-LOCAL, AND FROM ACT 2 ON THAT IS A
    //    KNOWN GAP, NOT A BUG HERE. It reconstructs "fired" as "initially in
    //    the list and now absent", which is complete only while the list is
    //    never refilled. dungeonTransitionSetup CLEARS eventList and shrineList
    //    (AbstractDungeon.java:2576-2577) and the new dungeon's constructor
    //    rebuilds them (:291, :293), so an Act-2 dump cannot witness an Act-1
    //    event or shrine fire at all, while the simulator's `event_flags`
    //    rightly still carries it. The one-time specials are the exception --
    //    they are handed over by identity (CardCrawlGame.java:1102-1119) and
    //    never rebuilt, so their half of the derivation stays complete for the
    //    whole run. Closing the other half needs cross-record accumulation over
    //    a capture that starts at floor 1; that is the capture campaign's call,
    //    and it is a live deferred-obligations row (owner S2.43). --
    const int rec_act = static_cast<int>(a.act);
    // S3.21 (e) / ACT 4. TheEnding overrides BOTH list initialisers with empty
    // bodies (TheEnding.java:198-200, :211-213), so an Act-4 dump's `eventList`
    // and `shrineList` are empty BY CONSTRUCTION, not by every entry having
    // fired. The act-local derivation below reads "initially present and now
    // absent" as "fired"; run unamended at act 4 it would fall through to the
    // Act-1 table (event_framework.hpp's `anything else falls to Act 1`) and
    // mark all eleven Exordium events plus all six shrines FIRED off two empty
    // arrays -- a fabricated RunState the differ would then compare. Act 4 is
    // therefore handled here rather than by widening event_framework.hpp's
    // per-act tables, which was S3.32's grant. S3.32 has since LANDED the
    // engine half: `event_list_count(4)` and `shrine_list_count(4)` are both 0,
    // so the sim now produces the same empty bitsets this branch asserts, and
    // the two sides agree by construction rather than by both refusing to
    // look.
    const bool act4_empty_pools = rec_act >= 4;
    if (act4_empty_pools) {
        for (const char* key : {"eventList", "shrineList"}) {
            if (const json* arr = fr.take(key)) {
                if (!arr->is_array() || !arr->empty()) {
                    throw TranslateError(
                        loc(ctx) + " " + path + "." + key +
                        " is non-empty at act " + std::to_string(rec_act) +
                        " — TheEnding initialises both lists empty "
                        "(TheEnding.java:198-200, :211-213)");
                }
                fr.oracle(key);
            }
        }
    }
    if (const json* events = act4_empty_pools ? nullptr : fr.take("eventList")) {
        const uint16_t first = eng::event_list_first_id(rec_act);
        const int count = eng::event_list_count(rec_act);
        rs.event_membership = static_cast<uint16_t>(parse_event_membership(
            *events, path + ".eventList", ctx, first, count, "eventList"));
        const uint32_t initial = (1u << count) - 1u;
        set_event_flag_block(rs, first, count,
                             initial & ~static_cast<uint32_t>(rs.event_membership));
        fr.mapped();
    }
    if (const json* shrines = act4_empty_pools ? nullptr : fr.take("shrineList")) {
        // Position -> id for this act's list (event_framework.hpp); the BIT is
        // still `id - kShrineListFirstId` in every act, which is why the
        // bitset stays byte-comparable across a crossing.
        const uint16_t* order = rec_act == 1
                                    ? eng::kShrineDrawOrderExordium
                                    : eng::kShrineDrawOrderCityBeyond;
        rs.shrine_membership = static_cast<uint8_t>(parse_event_membership(
            *shrines, path + ".shrineList", ctx, eng::kShrineListFirstId,
            eng::kShrineListCount, "shrineList", order));
        const uint32_t initial = (1u << eng::kShrineListCount) - 1u;
        set_event_flag_block(rs, eng::kShrineListFirstId, eng::kShrineListCount,
                             initial & ~static_cast<uint32_t>(rs.shrine_membership));
        fr.mapped();
    }
    if (const json* specials = fr.take("specialOneTimeEventList")) {
        rs.special_membership = static_cast<uint16_t>(parse_event_membership(
            *specials, path + ".specialOneTimeEventList", ctx,
            eng::kSpecialListFirstId, eng::kSpecialListCount,
            "specialOneTimeEventList"));
        uint32_t initial = (1u << eng::kSpecialListCount) - 1u;
        if (!eng::note_for_yourself_available(rs.ascension)) {
            initial &= ~(1u << eng::kNoteForYourselfBit);
        }
        set_event_flag_block(rs, eng::kSpecialListFirstId,
                             eng::kSpecialListCount,
                             initial & ~static_cast<uint32_t>(rs.special_membership));
        fr.mapped();
    }
    // B5.2 encounter-list oracle. These arrays live in RunController rather
    // than RunState, so encounter_list_oracle compares them directly against
    // run_begin. They are nevertheless a consumed ORACLE field here, and the
    // complete nested shape is validated so a new key cannot bypass the
    // translator's fail-loud unknown-field policy.
    if (const json* lists = fr.take("encounterLists")) {
        FieldReader lr(*lists, path + ".encounterLists", ctx);
        for (const char* key : {"monster", "elite", "boss"}) {
            const json& arr = lr.require(key);
            if (!arr.is_array()) {
                throw TranslateError(loc(ctx) + " expected array at " +
                                     path + ".encounterLists." + key);
            }
            for (std::size_t i = 0; i < arr.size(); ++i) {
                (void)as_str(arr[i], ctx, path + ".encounterLists." + key +
                                          "[" + std::to_string(i) + "]");
            }
            lr.oracle(key);
        }
        lr.finish();
        fr.oracle("encounterLists");
    }
    // monster_move_history (§2.5 #9 / PROTOCOL §5): the fork emits the FULL
    // `AbstractMonster.moveHistory` per monster, in room order -- up to 14
    // entries in the current corpus, where stock's own JSON gives 2. The schema
    // keeps the last THREE (`MonsterState.move_history[3]`, `[0]` most recent),
    // so the tail is what this can carry and the rest stays deferred.
    //
    // WHY THE LAST THREE AND NOT THE FIRST. `moveHistory` is appended
    // (`AbstractMonster.setMove`), so the most recent move is the LAST element;
    // the schema is most-recent-first. Reading the head would silently store a
    // monster's OPENING moves as if they were its latest, which is exactly the
    // kind of wrong-but-plausible value a differ cannot flag.
    //
    // The join is positional, because "one per monster in room order" is the
    // fork's own contract -- but it is CHECKED against the ids the combat block
    // already parsed rather than trusted, since a silent misalignment would
    // attribute one monster's history to another.
    if (const json* mh = fr.take("monster_move_history")) {
        if (!mh->is_array())
            throw TranslateError(loc(ctx) + " expected array at " +
                                 path + ".monster_move_history");
        if (mh->size() != static_cast<std::size_t>(cs.monster_count)) {
            throw TranslateError(
                loc(ctx) + " monster_move_history has " +
                std::to_string(mh->size()) + " entr" +
                (mh->size() == 1 ? "y" : "ies") + " but combat_state has " +
                std::to_string(cs.monster_count) +
                " monster(s) (schema drift, translation aborted)");
        }
        for (std::size_t i = 0; i < mh->size(); ++i) {
            const std::string at =
                path + ".monster_move_history[" + std::to_string(i) + "]";
            const json& e = (*mh)[i];
            if (!e.is_object())
                throw TranslateError(loc(ctx) + " expected object at " + at);
            const eng::MonsterId want =
                join_monster(as_str(e.at("id"), ctx, at + ".id"), ctx, at + ".id");
            if (static_cast<uint16_t>(want) != cs.monsters[i].monster_id) {
                throw TranslateError(
                    loc(ctx) + " " + at + " is \"" +
                    std::string(sts::registry::monster_game_id(want)) +
                    "\" but combat_state monster " + std::to_string(i) +
                    " is \"" +
                    std::string(sts::registry::monster_game_id(
                        static_cast<eng::MonsterId>(cs.monsters[i].monster_id))) +
                    "\" -- the two lists are not in the same room order");
            }
            const json& hist = e.at("move_history");
            if (!hist.is_array())
                throw TranslateError(loc(ctx) + " expected array at " +
                                     at + ".move_history");
            const std::size_t n = hist.size();
            for (std::size_t k = 0; k < 3; ++k) {
                if (k >= n) break;
                const int64_t v =
                    as_i64(hist[n - 1 - k], ctx,
                           at + ".move_history[" + std::to_string(n - 1 - k) + "]");
                if (v < 0 || v > 255)
                    throw TranslateError(loc(ctx) + " move id at " + at +
                                         " does not fit uint8");
                cs.monsters[i].move_history[k] = static_cast<uint8_t>(v);
            }
        }
        // Entries past the third have no schema home -- the deferred remainder
        // of this row. Nothing the run layer models reads more than two moves
        // back (stock's `lastTwoMoves`), so carrying them would be a
        // CombatState layout change with no consumer.
        fr.mapped();
    }
    fr.finish();
    return a;
}

// ---- combat_state (PROTOCOL §3.10) ---------------------------------------

// Returns the card_pool fill cursor it left behind: the HAND_SELECT screen state
// (below) allocates further pool rows for the cards the select screen has lifted
// out of the hand, and must not tread on the rows this filled.
//
// `vitals` (combat_vitals.hpp) is filled from the SAME walk: every id join and
// every scalar the CombatState receives, the projection receives too, so the
// two can never disagree about what the dump said.
int parse_combat_state(const json& j, const std::string& path, Ctx& ctx,
                       eng::CombatState& cs, CombatVitals* vitals = nullptr) {
    FieldReader fr(j, path, ctx);
    cs.phase = static_cast<uint8_t>(eng::CombatPhase::WAITING_ON_USER);
    int pool_used = 0;  // running fill cursor into cs.card_pool (no struct field)
    auto pile_vitals = [vitals](std::vector<VitalsCard> CombatVitals::*member) {
        return vitals != nullptr ? &(vitals->*member) : nullptr;
    };

    if (const json* mons = fr.take("monsters")) {
        if (!mons->is_array()) throw TranslateError(loc(ctx) + " expected array at " + path + ".monsters");
        if (mons->size() > eng::kMonsterCap) {
            throw TranslateError(loc(ctx) + " " + path + ".monsters has " +
                                 std::to_string(mons->size()) + " > kMonsterCap (" +
                                 std::to_string(eng::kMonsterCap) + ")");
        }
        cs.monster_count = 0;
        for (std::size_t i = 0; i < mons->size(); ++i) {
            VitalsMonster vm;
            cs.monsters[cs.monster_count++] =
                parse_monster((*mons)[i], path + ".monsters[" + std::to_string(i) + "]", ctx,
                              vitals != nullptr ? &vm : nullptr);
            if (vitals != nullptr) vitals->monsters.push_back(std::move(vm));
        }
        fr.mapped();
    }

    if (const json* p = fr.take("hand"))
        { parse_pile(*p, path + ".hand", ctx, cs, pool_used, cs.hand, cs.hand_count, eng::kHandCap, pile_vitals(&CombatVitals::hand)); fr.mapped(); }
    if (const json* p = fr.take("draw_pile")) {
        parse_pile(*p, path + ".draw_pile", ctx, cs, pool_used, cs.draw, cs.draw_count, eng::kDrawCap, pile_vitals(&CombatVitals::draw));
        fr.oracle("draw_pile");  // membership mapped; ORDER is advisory (§3.10 O)
    }
    if (const json* p = fr.take("discard_pile"))
        { parse_pile(*p, path + ".discard_pile", ctx, cs, pool_used, cs.discard, cs.discard_count, eng::kDiscardCap, pile_vitals(&CombatVitals::discard)); fr.mapped(); }
    if (const json* p = fr.take("exhaust_pile"))
        { parse_pile(*p, path + ".exhaust_pile", ctx, cs, pool_used, cs.exhaust, cs.exhaust_count, eng::kExhaustCap, pile_vitals(&CombatVitals::exhaust)); fr.mapped(); }
    if (const json* p = fr.take("limbo"))
        { parse_pile(*p, path + ".limbo", ctx, cs, pool_used, cs.limbo, cs.limbo_count, eng::kLimboCap, pile_vitals(&CombatVitals::limbo)); fr.mapped(); }
    if (const json* c = fr.take("card_in_play")) {
        // player.cardInUse: one card, appended to limbo (the sim's in-flight pile).
        if (cs.limbo_count >= eng::kLimboCap || pool_used >= eng::kCardPoolCap)
            throw TranslateError(loc(ctx) + " limbo/card_pool overflow at " + path + ".card_in_play");
        VitalsCard vc;
        eng::CardInstance ci = parse_card(*c, path + ".card_in_play", ctx, /*master_deck=*/false,
                                          vitals != nullptr ? &vc : nullptr);
        if (vitals != nullptr) vitals->limbo.push_back(std::move(vc));
        eng::CardPoolIndex idx = static_cast<eng::CardPoolIndex>(pool_used);
        cs.card_pool[pool_used++] = ci;
        cs.limbo[cs.limbo_count++] = idx;
        fr.mapped();
    }

    if (const json* pl = fr.take("player")) { parse_player(*pl, path + ".player", ctx, cs, vitals); fr.mapped(); }
    if (const json* t = fr.take("turn")) {
        cs.turn = static_cast<uint16_t>(as_i64(*t, ctx, path + ".turn"));
        if (vitals != nullptr) vitals->turn = cs.turn;
        fr.mapped();
    }
    fr.defer("cards_discarded_this_turn");  // no matching CombatState counter
    fr.defer("times_damaged");              // damagedThisCombat; no schema field
    fr.finish();
    return pool_used;
}

// ---- screen_state variants (PROTOCOL §3.3-3.9, §3.19) --------------------
//
// Almost no screen_state content has schema storage (map / events / shop are
// B4.x; the B4.5 REWARD screens are deliberately storage-less -- the sim
// derives its reward screen and the acceptance diffs the post-claim RunState).
// The two exceptions: HAND_SELECT (CombatState's limbo/hand storage) and, since
// schema v8 (S2.47), BOSS_REWARD -- its three offered relics land in
// RunState.boss_chest, because a *zero-diff boss-relic pick* (design §6 S2-G2
// item 2) needs the offers in the struct the differ compares. Each variant's
// key-set is enumerated so a KNOWN field never trips the drift error, while a
// genuinely new key still does. Nested objects (cards, relics, potions,
// options, nodes) are structurally validated; the B4.5 reward slice
// (CARD_REWARD / COMBAT_REWARD) is additionally content-validated: enumerated
// reward_type, typed gold/booleans, and id-joined potions/relics/cards.

void defer_all(FieldReader& fr, std::initializer_list<std::string_view> keys) {
    for (auto k : keys) fr.defer(k);
}

void parse_coord(const json& j, const std::string& path, Ctx& ctx) {
    FieldReader fr(j, path, ctx);
    fr.defer("x");
    fr.defer("y");
    fr.finish();
}

void parse_map_node(const json& j, const std::string& path, Ctx& ctx) {
    FieldReader fr(j, path, ctx);
    fr.defer("x");
    fr.defer("y");
    fr.defer("symbol");
    // S3.21 (a) / PROTOCOL §3.11: the fork emits MapRoomNode.hasEmeraldKey
    // ONLY when true, so absence is the default and every pre-redeploy capture
    // reads exactly as it did. The engine already stores the marked node as
    // `emerald_x`/`emerald_y` (map_rooms.hpp), which is a coordinate pair and
    // not a per-node bit, so there is no per-node schema field to write here;
    // this is consumed as a KNOWN field (a new/renamed one still trips the
    // fail-loud policy) and its comparison against the engine's marked
    // coordinates belongs to S3.11's reward row, which is what gives the flag
    // a consumer.
    fr.defer("has_emerald_key");
    if (const json* ps = fr.take("parents")) {
        for (std::size_t i = 0; i < ps->size(); ++i)
            parse_coord((*ps)[i], path + ".parents[" + std::to_string(i) + "]", ctx);
        fr.defer("parents");
    }
    if (const json* ch = fr.take("children")) {
        for (std::size_t i = 0; i < ch->size(); ++i)
            parse_coord((*ch)[i], path + ".children[" + std::to_string(i) + "]", ctx);
        fr.defer("children");
    }
    fr.finish();
}

// Validate a list of cards/relics/potions structurally (unknown ids there are
// still drift), but do not map them (reward/shop content is B4.x). Cards are
// validated leniently: a reward/shop card the skeleton registry lacks would be a
// real drift, so we DO join its id — consistent with fail-loud.
void defer_card_list(FieldReader& fr, std::string_view key, const std::string& base,
                     Ctx& ctx) {
    if (const json* arr = fr.take(key)) {
        for (std::size_t i = 0; i < arr->size(); ++i)
            (void)parse_card((*arr)[i],
                             base + "." + std::string(key) + "[" + std::to_string(i) + "]", ctx);
        fr.defer(key);
    }
}

void parse_relic(const json& j, const std::string& path, Ctx& ctx, eng::RelicSlot* out) {
    FieldReader fr(j, path, ctx);
    eng::RelicSlot rs{};
    rs.relic_id = static_cast<uint16_t>(
        join_relic(as_str(fr.require("id"), ctx, path + ".id"), ctx, path + ".id"));
    fr.mapped();
    if (const json* c = fr.take("counter")) {
        rs.counter = static_cast<int16_t>(as_i64(*c, ctx, path + ".counter"));
        fr.mapped();
    }
    fr.ignore("name");
    if (const json* pr = fr.take("price")) {  // shop overlay, type-checked
        (void)as_i64(*pr, ctx, path + ".price");
    }
    fr.defer("price");
    fr.finish();
    if (out) *out = rs;
}

// The HAND_SELECT screen (§3.19), mapped rather than deferred.
//
// getHandSelectState (GameStateConverter.java:538-557) emits FOUR things, and
// all four are modelled state here: `hand` is p.hand.group, `selected` is
// handCardSelectScreen.selectedCards.group IN PICK ORDER, `max_cards` is
// numCardsToSelect and `can_pick_zero` is canPickZero. The sim keeps the same
// two groups as ONE array -- the picks are the trailing suffix of `hand`, with
// their count in the open CHOOSE_CARD's flags (interp.hpp) -- so the mapping is
// a concatenation plus a count, with no information dropped and none invented.
//
// WHAT THIS DELIBERATELY DOES NOT CLAIM: the screen carries no indication of
// what will be DONE with the picks. `getHandSelectState` has no field for it and
// the reason is structural -- in the game the manipulation lives in the
// AbstractGameAction that opened the screen, which the protocol does not
// serialize at all. The synthesized queue item therefore carries the selection
// SHAPE (how many may be picked, whether zero is allowed, what is picked so far)
// and leaves the ChoiceKind at its zero value, which is why the item exists at
// all: without it the concatenated hand would read back as an ordinary
// seven-card hand rather than five cards with two picked, and that ambiguity
// would be a silently wrong state rather than an honestly partial one.
void parse_hand_select_state(FieldReader& fr, const std::string& path, Ctx& ctx,
                             eng::CombatState* cs, int* pool_used,
                             CombatVitals* vitals = nullptr) {
    if (cs == nullptr || pool_used == nullptr) {
        throw TranslateError(loc(ctx) + " HAND_SELECT at " + path +
                             " with no combat_state — the hand-select screen "
                             "only exists inside a combat (schema drift, "
                             "translation aborted)");
    }
    const json* hand = fr.take("hand");
    const json* selected = fr.take("selected");
    const json& max_cards_v = fr.require("max_cards");
    const json& pick_zero_v = fr.require("can_pick_zero");
    if (hand == nullptr || !hand->is_array() || selected == nullptr ||
        !selected->is_array()) {
        throw TranslateError(loc(ctx) + " " + path +
                             " needs array `hand` and `selected`");
    }
    if (!pick_zero_v.is_boolean()) {
        throw TranslateError(loc(ctx) + " expected boolean at " + path +
                             ".can_pick_zero");
    }
    // The screen's hand is the OPENING ACTION's eligible subset: ArmamentsAction
    // (:45-91) pulls every `cannotUpgrade` card out of `p.hand` into a private
    // list before `HandCardSelectScreen.open` and only appends them back in
    // `returnCards()`. Neither that list nor the action is serialized, and
    // `screen_state.hand` is the same filtered group as `combat_state.hand`, so
    // the dump's hand is missing them while the sim (which filters at choice
    // time instead, interp_cards.cpp `choice_slot_eligible`) still holds them.
    // Flagging the record is what lets `--vitals` judge the hand by containment
    // rather than equality there (combat_vitals.hpp).
    if (vitals != nullptr) vitals->hand_partial = true;
    const int64_t max_cards = as_i64(max_cards_v, ctx, path + ".max_cards");
    const bool can_pick_zero = pick_zero_v.get<bool>();
    const std::size_t picked = selected->size();

    // `hand` here IS combat_state.hand -- the same p.hand.group, emitted twice
    // by the same converter run. Cross-check rather than re-parse: if the two
    // ever disagree the protocol has changed underneath us.
    if (hand->size() != cs->hand_count) {
        throw TranslateError(loc(ctx) + " " + path + ".hand has " +
                             std::to_string(hand->size()) +
                             " cards but combat_state.hand has " +
                             std::to_string(cs->hand_count) +
                             " (schema drift, translation aborted)");
    }
    if (static_cast<int64_t>(picked) > max_cards) {
        throw TranslateError(loc(ctx) + " " + path + " has " +
                             std::to_string(picked) + " selected cards but "
                             "max_cards is " + std::to_string(max_cards));
    }
    if (!can_pick_zero && picked > 0) {
        // A MANDATORY screen mid-selection. The sim has no such state to
        // translate into: a mandatory pick is applied the moment it is made
        // (advance.cpp's CHOOSE), so its cards are already in their destination
        // pile and were never held aside. Refusing is the honest answer --
        // silently concatenating would put exhausted or moved cards back in hand.
        throw TranslateError(
            loc(ctx) + " " + path + " is a MANDATORY hand-select (can_pick_zero "
            "false) holding " + std::to_string(picked) + " selected card(s); the "
            "sim applies each mandatory selection immediately and has no "
            "held-aside state to translate (unmodelled shape, translation "
            "aborted)");
    }
    if (cs->hand_count + picked > static_cast<std::size_t>(eng::kHandCap) ||
        static_cast<std::size_t>(*pool_used) + picked >
            static_cast<std::size_t>(eng::kCardPoolCap)) {
        throw TranslateError(loc(ctx) + " " + path +
                             " hand + selected overflows kHandCap/kCardPoolCap");
    }
    for (std::size_t i = 0; i < picked; ++i) {
        // The picks rejoin the hand on the vitals side as well: the sim keeps a
        // picked card IN hand until the selection commits (the trailing-suffix
        // shape above), so the like-for-like hand multiset includes them.
        VitalsCard vc;
        eng::CardInstance ci = parse_card(
            (*selected)[i], path + ".selected[" + std::to_string(i) + "]", ctx,
            /*master_deck=*/false, vitals != nullptr ? &vc : nullptr);
        if (vitals != nullptr) vitals->hand.push_back(std::move(vc));
        cs->card_pool[*pool_used] = ci;
        cs->hand[cs->hand_count++] =
            static_cast<eng::CardPoolIndex>((*pool_used)++);
    }
    if (cs->action_count != 0) {
        throw TranslateError(loc(ctx) + " " + path +
                             " would synthesize an open choice over a non-empty "
                             "action queue");
    }
    eng::ActionQueueItem open{};
    open.opcode = static_cast<uint16_t>(eng::Opcode::CHOOSE_CARD);
    open.src = eng::kActorPlayer;
    open.tgt = eng::kActorPlayer;
    open.amount = static_cast<int32_t>(max_cards);
    open.flags = eng::with_choose_selected_count(
        can_pick_zero ? eng::kChoiceOptionalBit : 0u,
        static_cast<uint8_t>(picked));
    cs->action_head = 0;
    cs->action_queue[0] = open;
    cs->action_count = 1;
    // All four keys are now real state, not deferred ones.
    fr.mapped();
    fr.mapped();
    fr.mapped();
    fr.mapped();
}

void parse_screen_state(const json& j, const std::string& path, Ctx& ctx,
                        const std::string& screen_type,
                        eng::CombatState* cs = nullptr,
                        int* pool_used = nullptr,
                        eng::RunState* rs = nullptr,
                        CombatVitals* vitals = nullptr) {
    FieldReader fr(j, path, ctx);
    if (screen_type == "EVENT") {
        fr.ignore("body_text");
        fr.ignore("event_name");
        {
            const std::string id =
                as_str(fr.require("event_id"), ctx, path + ".event_id");
            // NEOW arrives on the EVENT screen. NeowRoom is an ordinary room
            // with an event (NeowRoom.java:16-20), so CommunicationMod emits
            // screen_type EVENT for it -- with a HARD-CODED sentinel id,
            // because NeowEvent is the one base-game event class with no static
            // `ID` field (GameStateConverter.getEventState :343-355). That
            // sentinel is deliberately not an events.yaml row: Neow is not in
            // any act's event/shrine/special pool, so giving it an EventId
            // would put a non-pool entry into the three membership bitsets that
            // pool ids index. Recognising it here is what discharges the
            // deferred Neow screen_state slice; everything below -- the option
            // list's shape and its choice_index typing -- is the same pass
            // every other event screen gets, and the slice stays storage-less
            // exactly like the reward slices (the sim DERIVES the blessing from
            // the seed, so the acceptance diffs post-choice RunState, not the
            // screen).
            //
            // S3.21 (e): `Spire Heart` joins Neow as the second recognised
            // non-pool event id. It is the Act-3 terminal VictoryRoom's event
            // (VictoryRoom.java:21-33, SpireHeart.java:47) and, like Neow, is
            // a member of NO act event/shrine/special list -- it is
            // constructed directly by the room, never drawn -- so giving it a
            // pool EventId here would put a non-pool entry into the three
            // membership bitsets that pool ids index. The registry row for it
            // (events.yaml 52, `SPIRE_HEART`) is S3.41's grant and is a
            // BEHAVIOUR row, not a join key; recognising the id here is what
            // stops the translator ABORTING on the four-click tail every
            // three-act victory capture carries, which is the whole reason the
            // tail had to be dropped before this task.
            if (id != "Neow Event" && id != "Spire Heart") {
                (void)join_event(id, ctx, path + ".event_id");
            }
            fr.defer("event_id");
        }
        if (const json* opts = fr.take("options")) {
            if (!opts->is_array()) {
                throw TranslateError(loc(ctx) + " expected array at " + path +
                                     ".options");
            }
            for (std::size_t i = 0; i < opts->size(); ++i) {
                const std::string op =
                    path + ".options[" + std::to_string(i) + "]";
                FieldReader o((*opts)[i], op, ctx);
                o.ignore("text");
                o.ignore("label");
                if (const json* disabled = o.take("disabled")) {
                    if (!disabled->is_boolean()) {
                        throw TranslateError(loc(ctx) + " expected boolean at " +
                                             op + ".disabled");
                    }
                    o.defer("disabled");
                }
                if (const json* choice = o.take("choice_index")) {
                    (void)as_i64(*choice, ctx, op + ".choice_index");
                    o.defer("choice_index");
                }
                o.finish();
            }
            fr.defer("options");
        }
    } else if (screen_type == "CHEST" || screen_type == "REST") {
        defer_all(fr, {"chest_type", "chest_open", "has_rested", "rest_options"});
    } else if (screen_type == "CARD_REWARD") {
        // B4.5 reward slice: content-validated + id-joined (fail loud), still
        // storage-less BY DESIGN -- the sim derives the reward screen
        // (RunController.rewards) and the acceptance diffs the post-claim
        // RunState, so screen content never lands in a schema field. What
        // changed from the structural pass: the two booleans are type-checked,
        // and the cards were already id-joined by defer_card_list.
        if (const json* b = fr.take("bowl_available")) {
            if (!b->is_boolean()) {
                throw TranslateError(loc(ctx) + " expected boolean at " + path +
                                     ".bowl_available");
            }
            fr.defer("bowl_available");
        }
        if (const json* sk = fr.take("skip_available")) {
            if (!sk->is_boolean()) {
                throw TranslateError(loc(ctx) + " expected boolean at " + path +
                                     ".skip_available");
            }
            fr.defer("skip_available");
        }
        defer_card_list(fr, "cards", path, ctx);
    } else if (screen_type == "COMBAT_REWARD") {
        // B4.5 reward slice (same storage-less contract as CARD_REWARD above):
        // reward_type is validated against RewardItem.RewardType's full name
        // set (RewardItem$RewardType -- an unknown name is drift, where the old
        // structural pass accepted anything), gold is type-checked, the potion
        // id is JOINED through the registry (an unknown potion fails loud /
        // tallies under id-tolerance), and relics were already joined.
        if (const json* rw = fr.take("rewards")) {
            for (std::size_t i = 0; i < rw->size(); ++i) {
                const std::string rp = path + ".rewards[" + std::to_string(i) + "]";
                FieldReader r((*rw)[i], rp, ctx);
                {
                    const std::string t =
                        as_str(r.require("reward_type"), ctx, rp + ".reward_type");
                    // RewardItem.RewardType (RewardItem.java): the S1-visible
                    // four plus the three S2/deferred members, kept so a
                    // capture that wanders into one is classified as KNOWN
                    // shape (STOLEN_GOLD lands with the Looter; the keys are
                    // final-act content).
                    if (t != "CARD" && t != "GOLD" && t != "RELIC" &&
                        t != "POTION" && t != "STOLEN_GOLD" &&
                        t != "EMERALD_KEY" && t != "SAPPHIRE_KEY") {
                        throw TranslateError(loc(ctx) + " unknown reward_type \"" +
                                             t + "\" at " + rp +
                                             " (schema drift, translation aborted)");
                    }
                    r.defer("reward_type");
                }
                if (const json* g = r.take("gold")) {
                    (void)as_i64(*g, ctx, rp + ".gold");
                    r.defer("gold");
                }
                if (const json* rl = r.take("relic")) { parse_relic(*rl, rp + ".relic", ctx, nullptr); r.defer("relic"); }
                if (const json* pt = r.take("potion")) {
                    FieldReader p(*pt, rp + ".potion", ctx);
                    (void)join_potion(as_str(p.require("id"), ctx, rp + ".potion.id"),
                                      ctx, rp + ".potion.id");
                    p.defer("id");
                    p.ignore("name");
                    p.defer("can_use");
                    p.defer("can_discard");
                    p.defer("requires_target");
                    p.defer("price");
                    p.finish();
                    r.defer("potion");
                }
                if (const json* lk = r.take("link")) { parse_relic(*lk, rp + ".link", ctx, nullptr); r.ignore("link"); }  // S2 scope
                r.finish();
            }
            fr.defer("rewards");
        }
    } else if (screen_type == "MAP") {
        if (const json* cn = fr.take("current_node")) { parse_map_node(*cn, path + ".current_node", ctx); fr.defer("current_node"); }
        if (const json* nn = fr.take("next_nodes")) {
            for (std::size_t i = 0; i < nn->size(); ++i)
                parse_map_node((*nn)[i], path + ".next_nodes[" + std::to_string(i) + "]", ctx);
            fr.defer("next_nodes");
        }
        fr.defer("first_node_chosen");
        fr.defer("boss_available");
    } else if (screen_type == "BOSS_REWARD") {
        // EMITTED since schema v8 (S2.47; S2.42 had promoted it I -> deferred).
        // The three offers land in RunState.boss_chest -- the storage the
        // s2-tasks.md deferred row demanded, because an offer held only in
        // transient controller state was invisible to diff_run_states and made
        // design 6's S2-G2 item 2 (a ZERO-DIFF boss-relic pick) unscorable.
        //
        // What this dump ATTESTS, and only this dump: the BOSS_REWARD screen is
        // AbstractDungeon.bossRelicScreen, up (BossRelicSelectScreen.java:353),
        // so the player has opened the chest (`seen` = 1, screen = RELIC_SELECT)
        // and has not yet picked (the isDone block closes the screen at the
        // pick, :101-108, so `chose_relic` = 0). Every non-BOSS_REWARD dump
        // leaves boss_chest value-init zero -- the replay differ gates the
        // comparison on the capture-side `seen`, the `keys` precedent made
        // conditional.
        //
        // The relic ids stay JOINED through the registry (fail-loud on an
        // unknown boss relic), exactly as the S2.42 pin requires; the join and
        // the emit are the same call now.
        if (const json* rl = fr.take("relics")) {
            // BossChest offers EXACTLY three (BossChest.java:37) and the screen
            // re-adds from that same list on every open
            // (BossRelicSelectScreen.open:342-373), so any other count is
            // schema drift, not a short offer.
            if (rl->size() != static_cast<std::size_t>(eng::kBossChestOfferCount)) {
                throw TranslateError(loc(ctx) + " " + path + ".relics has " +
                                     std::to_string(rl->size()) +
                                     " entries; a boss chest offers exactly " +
                                     std::to_string(eng::kBossChestOfferCount) +
                                     " (schema drift, translation aborted)");
            }
            for (std::size_t i = 0; i < rl->size(); ++i) {
                const std::string rp =
                    path + ".relics[" + std::to_string(i) + "]";
                eng::RelicSlot slot{};
                parse_relic((*rl)[i], rp, ctx, &slot);
                if (rs != nullptr) {
                    rs->boss_chest.relics[i] = slot.relic_id;
                }
            }
            if (rs != nullptr) {
                rs->boss_chest.screen =
                    static_cast<uint8_t>(eng::BossChestScreen::RELIC_SELECT);
                rs->boss_chest.seen = 1;
                rs->boss_chest.chose_relic = 0;
            }
            fr.mapped();
        }
    } else if (screen_type == "SHOP_SCREEN") {
        // The shop slice, content-validated on the same terms as the reward
        // slice above: registry-JOINED, TYPE-CHECKED, and deliberately
        // STORAGE-LESS. Storage-less is not an omission -- translation outputs
        // RunState/CombatState, and a merchant is derived state the game itself
        // rebuilds from (seed, merchantRng.counter), so there is nothing in the
        // frozen schema for it to land in. The one piece the game DOES persist,
        // the ramping purge cost, already has a RunState field and arrives
        // through the oracle block, not here.
        //
        // cards and relics are joined by parse_card / parse_relic (both of
        // which now also type-check the shop `price` overlay); the potion rows
        // are joined here, the way the COMBAT_REWARD potion is.
        defer_card_list(fr, "cards", path, ctx);
        if (const json* rl = fr.take("relics")) {
            for (std::size_t i = 0; i < rl->size(); ++i)
                parse_relic((*rl)[i], path + ".relics[" + std::to_string(i) + "]", ctx, nullptr);
            fr.defer("relics");
        }
        if (const json* pt = fr.take("potions")) {
            for (std::size_t i = 0; i < pt->size(); ++i) {
                const std::string pp = path + ".potions[" + std::to_string(i) + "]";
                FieldReader p((*pt)[i], pp, ctx);
                (void)join_potion(as_str(p.require("id"), ctx, pp + ".id"), ctx,
                                  pp + ".id");
                p.defer("id");
                p.ignore("name");
                p.defer("can_use");
                p.defer("can_discard");
                p.defer("requires_target");
                if (const json* pr = p.take("price")) {
                    (void)as_i64(*pr, ctx, pp + ".price");
                }
                p.defer("price");
                p.finish();
            }
            fr.defer("potions");
        }
        if (const json* pa = fr.take("purge_available")) {
            (void)as_bool(*pa, ctx, path + ".purge_available");
        }
        fr.defer("purge_available");
        if (const json* pc = fr.take("purge_cost")) {
            (void)as_i64(*pc, ctx, path + ".purge_cost");
        }
        fr.defer("purge_cost");
    } else if (screen_type == "GRID") {
        defer_card_list(fr, "cards", path, ctx);
        defer_card_list(fr, "selected_cards", path, ctx);
        defer_all(fr, {"num_cards", "any_number", "for_upgrade", "for_transform",
                       "for_purge", "confirm_up"});
    } else if (screen_type == "HAND_SELECT") {
        parse_hand_select_state(fr, path, ctx, cs, pool_used, vitals);
    } else if (screen_type == "GAME_OVER") {
        fr.ignore("score");   // out-of-model presentation
        fr.defer("victory");
    }
    // screen_type NONE (and any type with no extra keys) -> nothing to consume.
    fr.finish();
}

// ---- potion slots (PROTOCOL §3.17): 5 positional slots --------------------

void parse_potions(const json& arr, const std::string& path, Ctx& ctx, eng::RunState& rs) {
    if (!arr.is_array()) throw TranslateError(loc(ctx) + " expected array at " + path);
    if (arr.size() > eng::kPotionCap) {
        throw TranslateError(loc(ctx) + " " + path + " has " + std::to_string(arr.size()) +
                             " potion slots > kPotionCap (" + std::to_string(eng::kPotionCap) + ")");
    }
    // Potion-slot COUNT (§2.5-adjacent; A11 = one fewer). The potions array has
    // one entry per slot (empty slots carry id "Potion Slot"), so its length IS
    // the slot count. B4.3 gave RunState a potion_slots field; populate it here.
    rs.potion_slots = static_cast<uint8_t>(arr.size());
    for (std::size_t i = 0; i < arr.size(); ++i) {
        const std::string pp = path + "[" + std::to_string(i) + "]";
        FieldReader fr(arr[i], pp, ctx);
        std::string id = as_str(fr.require("id"), ctx, pp + ".id");
        if (id == "Potion Slot") {
            rs.potions[i] = static_cast<uint16_t>(reg::PotionId::NONE);  // empty slot
        } else {
            reg::PotionId pid = reg::potion_from_game_id(id);
            if (pid == reg::PotionId::NONE) {
                if (ctx.tolerate_ids) {
                    tally_unknown_id(ctx, "potion", id);
                    rs.potions[i] = static_cast<uint16_t>(reg::PotionId::NONE);
                } else {
                    throw TranslateError(loc(ctx) + " unknown potion id \"" + id + "\" at " +
                                         pp + ".id — registry has no game_id mapping "
                                         "(schema drift, translation aborted)");
                }
            } else {
                rs.potions[i] = static_cast<uint16_t>(pid);
            }
        }
        fr.mapped();
        fr.ignore("name");
        fr.defer("can_use");
        fr.defer("can_discard");
        fr.defer("requires_target");
        fr.defer("price");
        fr.finish();
    }
}

// ---- game_state (PROTOCOL §3.2) ------------------------------------------

void parse_game_state(const json& j, const std::string& path, Ctx& ctx,
                      TranslatedRecord& out) {
    FieldReader fr(j, path, ctx);
    eng::RunState& rs = out.run;

    // Character sheet + anchors.
    int64_t stock_seed = as_i64(fr.require("seed"), ctx, path + ".seed");
    rs.run_seed = stock_seed; fr.mapped();
    int64_t stock_floor = as_i64(fr.require("floor"), ctx, path + ".floor");
    rs.floor = static_cast<uint16_t>(stock_floor); fr.mapped();
    int64_t stock_act = as_i64(fr.require("act"), ctx, path + ".act");
    rs.act = static_cast<uint8_t>(stock_act); fr.mapped();
    int64_t stock_asc = as_i64(fr.require("ascension_level"), ctx, path + ".ascension_level");
    rs.ascension = static_cast<uint8_t>(stock_asc); fr.mapped();
    rs.hp = static_cast<int16_t>(as_i64(fr.require("current_hp"), ctx, path + ".current_hp")); fr.mapped();
    rs.max_hp = static_cast<int16_t>(as_i64(fr.require("max_hp"), ctx, path + ".max_hp")); fr.mapped();
    rs.gold = static_cast<int32_t>(as_i64(fr.require("gold"), ctx, path + ".gold")); fr.mapped();

    // master deck (§3.13 cards).
    if (const json* deck = fr.take("deck")) {
        if (!deck->is_array()) throw TranslateError(loc(ctx) + " expected array at " + path + ".deck");
        if (deck->size() > eng::kMasterDeckCap) {
            throw TranslateError(loc(ctx) + " deck has " + std::to_string(deck->size()) +
                                 " > kMasterDeckCap (" + std::to_string(eng::kMasterDeckCap) + ")");
        }
        rs.master_deck_count = 0;
        for (std::size_t i = 0; i < deck->size(); ++i) {
            rs.master_deck[rs.master_deck_count++] =
                parse_card((*deck)[i], path + ".deck[" + std::to_string(i) + "]", ctx,
                           /*master_deck=*/true);
        }
        fr.mapped();
    }
    // relics (§3.16), acquisition-ordered.
    if (const json* relics = fr.take("relics")) {
        if (!relics->is_array()) throw TranslateError(loc(ctx) + " expected array at " + path + ".relics");
        if (relics->size() > eng::kRelicCap) {
            throw TranslateError(loc(ctx) + " relics has " + std::to_string(relics->size()) +
                                 " > kRelicCap (" + std::to_string(eng::kRelicCap) + ")");
        }
        rs.relic_count = 0;
        for (std::size_t i = 0; i < relics->size(); ++i)
            parse_relic((*relics)[i], path + ".relics[" + std::to_string(i) + "]", ctx,
                        &rs.relics[rs.relic_count++]);
        fr.mapped();
    }
    // potions (§3.17).
    if (const json* potions = fr.take("potions")) { parse_potions(*potions, path + ".potions", ctx, rs); fr.mapped(); }

    // combat_state (§3.10) -> CombatState + the 5 floor streams via oracle.
    bool has_combat = false;
    int combat_pool_used = 0;
    if (const json* combat = fr.take("combat_state")) {
        out.in_combat = true;
        has_combat = true;
        combat_pool_used =
            parse_combat_state(*combat, path + ".combat_state", ctx, out.combat,
                               &out.vitals);
        fr.mapped();
    }

    // oracle block (§5) — always present in-dungeon; drives the streams + pity.
    OracleAnchors anchors{};
    bool has_oracle = false;
    if (const json* oracle = fr.take("oracle")) {
        anchors = parse_oracle(*oracle, path + ".oracle", ctx, rs, out.combat);
        has_oracle = true;
        fr.mapped();
    }

    // Sanity-anchor cross-checks (design §2.5 #10): oracle echoes must equal the
    // stock top-level values, else the dump is internally inconsistent.
    if (has_oracle) {
        auto check = [&](const char* nm, int64_t stock, int64_t oracle_v) {
            if (stock != oracle_v) {
                throw TranslateError(loc(ctx) + " oracle anchor mismatch: " + nm +
                                     " stock=" + std::to_string(stock) +
                                     " oracle=" + std::to_string(oracle_v) +
                                     " (schema drift, translation aborted)");
            }
        };
        out.playtime = anchors.playtime;
        out.has_playtime = anchors.has_playtime;
        out.has_keys = anchors.has_keys;
        check("seed", stock_seed, anchors.seed);
        check("floor", stock_floor, anchors.floor);
        check("act", stock_act, anchors.act);
        check("ascension", stock_asc, anchors.ascension);
    }
    (void)has_combat;

    // Everything else in game_state: no schema storage yet, or plumbing.
    fr.ignore("class");           // character identity (skeleton = IRONCLAD); no RunState field
    fr.ignore("current_action");  // transient in-flight action class name (§3.2)
    fr.defer("screen_name");
    fr.defer("is_screen_up");
    fr.defer("room_phase");
    fr.defer("action_phase");
    fr.defer("room_type");
    // act_boss (§3.2): the act's chosen boss, as `AbstractDungeon.bossKey` --
    // which is the SAME string the encounter registry keys on (`game_id`:
    // "The Guardian" / "Hexaghost" / "Slime Boss", `MonsterHelper.getEncounter`,
    // Exordium.initializeBoss). So the join is the registry's own lookup and not
    // a table of spellings: an unknown key is schema drift and aborts, exactly
    // like every other id join here.
    //
    // WHICH ID SPACE, and why EncounterId. `RunState.boss_ids` is documented as
    // "one boss id per act" without naming an enum, and nothing had ever written
    // it. EncounterId is the space the RUN LAYER already speaks: its
    // `boss_list[]` holds encounter key IDS -- the registry's own
    // `EncounterDef.id`, since the lists-ids change -- and `enter_combat` takes
    // the key that id resolves to (encounters.cpp `initializeBoss`), so a
    // consumer seeded from a translated RunState can join straight back to the
    // list the sim shuffles.
    // A MonsterId would not: "Slime Boss" the ENCOUNTER and `SlimeBoss` the
    // monster are different registries, and a two-monster boss would have no
    // single answer.
    //
    // HONEST LIMIT, and why this does not immediately turn the differ red: the
    // run layer does NOT populate `boss_ids` yet (grep: the field has no writer
    // in src/engine), so `--replay` neutralizes it the way it already
    // neutralizes `map[]`. Discharging the translator row does not discharge
    // that; see the ledger row.
    if (const json* ab = fr.take("act_boss")) {
        if (!ab->is_string())
            throw TranslateError(loc(ctx) + " expected string at " + path + ".act_boss");
        const std::string key = ab->get<std::string>();
        const sts::registry::EncounterDef* def =
            sts::registry::encounter_by_game_id(key);
        if (def == nullptr ||
            def->pool != sts::registry::EncounterPool::BOSS) {
            throw TranslateError(loc(ctx) + " act_boss \"" + key +
                                 "\" is not a BOSS row in the encounter registry "
                                 "(schema drift, translation aborted)");
        }
        const int64_t act_index = stock_act - 1;
        if (act_index >= 0 && act_index < eng::kBossIdCap) {
            rs.boss_ids[static_cast<std::size_t>(act_index)] = def->id;
        }
        fr.mapped();
    }
    fr.defer("choice_list");

    // map (§3.11): structurally validate nodes, then defer (storage is B4.1-4.3).
    if (const json* m = fr.take("map")) {
        if (!m->is_array()) throw TranslateError(loc(ctx) + " expected array at " + path + ".map");
        for (std::size_t i = 0; i < m->size(); ++i)
            parse_map_node((*m)[i], path + ".map[" + std::to_string(i) + "]", ctx);
        fr.defer("map");
    }

    // screen_type selects the screen_state sub-schema (§3.2 -> §3.3-3.9/§3.19).
    std::string screen_type;
    if (const json* stv = fr.take("screen_type")) {
        screen_type = stv->is_string() ? stv->get<std::string>() : "";
        fr.defer("screen_type");
    }
    if (const json* screen = fr.take("screen_state")) {
        // HAND_SELECT has combat-side schema storage, so it needs the combat it
        // belongs to and the pool cursor combat_state left off at; BOSS_REWARD
        // has run-side storage (RunState.boss_chest, schema v8), so the record's
        // RunState rides along too.
        parse_screen_state(*screen, path + ".screen_state", ctx, screen_type,
                           has_combat ? &out.combat : nullptr,
                           has_combat ? &combat_pool_used : nullptr,
                           &out.run,
                           has_combat ? &out.vitals : nullptr);
        fr.defer("screen_state");
    }
    fr.finish();
}

// ---- status wrapper (PROTOCOL §3.1) = state_json -------------------------

void parse_state_json(const json& j, const std::string& path, Ctx& ctx,
                      TranslatedRecord& out) {
    FieldReader fr(j, path, ctx);
    fr.defer("available_commands");   // legal-action oracle; no schema field
    fr.ignore("ready_for_command");   // protocol plumbing (§3.1)
    fr.ignore("in_game");             // menu/dungeon routing (§3.1)
    if (const json* gs = fr.take("game_state")) {
        parse_game_state(*gs, path + ".game_state", ctx, out);
        fr.mapped();
    }
    fr.finish();
}

// ---- one JSONL record ----------------------------------------------------

// Returns false for header/terminal records (nothing to translate), true for a
// translated action record (appended to `run`).
bool translate_record(const json& rec, Ctx& ctx, TranslatedRun& run) {
    if (!rec.is_object()) throw TranslateError(loc(ctx) + " top-level record is not an object");
    auto kind_it = rec.find("record_kind");
    std::string kind = (kind_it != rec.end() && kind_it->is_string()) ? kind_it->get<std::string>() : "";

    if (kind == "header") {
        run.schema_version = rec.value("schema_version", 0u);
        if (rec.contains("seed") && rec["seed"].is_object()) {
            run.seed = rec["seed"].value("long", static_cast<int64_t>(0));
            run.seed_string = rec["seed"].value("string", std::string{});
        }
        run.character = rec.value("character", std::string{});
        return false;
    }
    if (kind == "terminal") return false;
    if (kind != "action") {
        throw TranslateError(loc(ctx) + " unknown record_kind \"" + kind + "\"");
    }

    TranslatedRecord out;
    out.seq = rec.value("seq", 0);
    out.action_command = rec.value("action_command", std::string{});
    out.ready_for_command = rec.value("ready_for_command", true);

    const json& sj = rec.at("state_json");
    parse_state_json(sj, "state_json", ctx, out);

    // Header seed vs. dump seed cross-check (design §2.5 #10 anchor).
    if (run.seed != 0 && out.run.run_seed != run.seed) {
        throw TranslateError(loc(ctx) + " dump seed " + std::to_string(out.run.run_seed) +
                             " != header seed " + std::to_string(run.seed) +
                             " (schema drift, translation aborted)");
    }
    if (out.in_combat) ++run.combat_record_count;
    // `costs_available` is a WHOLE-RECORD fact, so it is folded once, here,
    // after every pile is filled -- including the HAND_SELECT screen's
    // `selected` cards, which rejoin the vitals hand after parse_combat_state
    // has already returned. A record where any card's dump carried no `cost`
    // key cannot be cost-compared, and `--costs` declines and counts it rather
    // than reading a defaulted 0 as a claim (combat_vitals.hpp).
    if (out.in_combat) {
        for (const std::vector<VitalsCard>* pile :
             {&out.vitals.hand, &out.vitals.draw, &out.vitals.discard,
              &out.vitals.exhaust, &out.vitals.limbo}) {
            for (const VitalsCard& c : *pile) {
                if (!c.cost_known) out.vitals.costs_available = false;
            }
        }
    }
    run.records.push_back(std::move(out));
    return true;
}

}  // namespace

// ---- public API ----------------------------------------------------------

TranslatedRun translate_lines(const std::vector<std::string>& lines,
                              const std::string& source_name,
                              const TranslateOptions& opts) {
    TranslatedRun run;
    Ctx ctx;
    ctx.source = source_name;
    ctx.stats = &run.stats;
    ctx.tolerate_ids = opts.tolerate_unknown_ids;
    ctx.unknown_ids = &run.unknown_ids;
    ctx.unknown_id_hits = &run.unknown_id_hits;

    // S3.21 (e): the post-victory ending tail is TRANSLATED, not dropped.
    // Before this task the translator recognised a victory artifact's trailing
    // `Spire Heart` records and `continue`d past them, because the id had no
    // recognition and would have ABORTED the whole run. It now has one
    // (parse_screen_state's EVENT arm), so the records go through the ordinary
    // walk and become real `TranslatedRecord`s that the differ can compare.
    // The counter survives as a labelled TALLY -- the tail is a distinct
    // structural region every three-act victory capture carries, the differ's
    // summary names how many of them it reached, and a reader needs the
    // denominator to read that line. `in_ending_tail` latches at the first
    // `Spire Heart` record and stays latched, so the `__terminal_observed__`
    // record that follows the four clicks is counted with them.
    bool victory_terminal = false;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        if (it->empty()) continue;
        json last;
        try { last = json::parse(*it); } catch (const json::parse_error&) {}
        victory_terminal = last.is_object() &&
                           last.value("record_kind", std::string{}) == "terminal" &&
                           last.value("outcome", std::string{}) == "victory";
        break;
    }
    bool in_ending_tail = false;

    int idx = 0;
    for (const std::string& line : lines) {
        ctx.record_idx = idx++;
        if (line.empty()) continue;
        json rec;
        try {
            rec = json::parse(line);
        } catch (const json::parse_error& e) {
            throw std::runtime_error(loc(ctx) + " JSON parse error: " + e.what());
        }
        if (victory_terminal && rec.is_object() &&
            rec.value("record_kind", std::string{}) == "action") {
            const json* ss = nullptr;
            if (auto sj = rec.find("state_json"); sj != rec.end() && sj->is_object()) {
                if (auto gs = sj->find("game_state"); gs != sj->end() && gs->is_object()) {
                    if (auto s = gs->find("screen_state"); s != gs->end() && s->is_object()) {
                        ss = &*s;
                    }
                }
            }
            if (in_ending_tail ||
                (ss && ss->value("event_id", std::string{}) == "Spire Heart")) {
                in_ending_tail = true;
                ++run.post_victory_ending_records;
                if (run.first_post_victory_ending_record < 0) {
                    run.first_post_victory_ending_record =
                        static_cast<int>(run.records.size());
                }
            }
        }
        translate_record(rec, ctx, run);
    }

    // S3.31: the RUN-OUTCOME KIND, derived from the artifact rather than from a
    // dump field.
    //
    // `RunState::victory_kind` has no counterpart in any dump: neither
    // CommunicationMod's `game_state` nor the fork's oracle block exposes
    // `victory` or `trueVictor` (they are Metrics.java:82,107 upload fields,
    // not run state), so a record-by-record join is impossible. What the
    // ARTIFACT does carry is the driver's own trailing `record_kind: terminal`
    // verdict, and `victory_terminal` above already reads it -- so the expected
    // value is written onto the LAST translated record, which for a victory run
    // is the `__terminal_observed__` action record whose state is the one the
    // sim reaches after the `Spire Heart` dialog's final click.
    //
    // This is a real comparison, not a neutralisation: a sim that ended the run
    // in the wrong place, or that reached a DEATH where the capture recorded a
    // win, diverges on this field at that record. It is written only for a
    // victory artifact; every other run leaves NONE on both sides, which is
    // what a loss is.
    //
    // ACT3_STOP vs HEART is read off that record's own act. The Act-3 stop
    // (SpireHeart.java:170-177) happens in act 3 -- the `Spire Heart`
    // VictoryRoom does not raise actNum -- while a true victory can only be
    // reached from inside Act 4 (TrueVictoryRoom, S3.33). No Act-4 capture
    // exists yet, so today this always resolves to ACT3_STOP; the act read is
    // written now so the S3.33 terminal needs no second pass here.
    if (victory_terminal && !run.records.empty()) {
        eng::RunState& last = run.records.back().run;
        last.victory_kind = static_cast<uint8_t>(
            last.act >= 4 ? eng::RunVictoryKind::HEART
                          : eng::RunVictoryKind::ACT3_STOP);
    }
    return run;
}

TranslatedRun translate_lines(const std::vector<std::string>& lines,
                              const std::string& source_name) {
    return translate_lines(lines, source_name, TranslateOptions{});
}

TranslatedRun translate_file(const std::string& jsonl_path,
                             const TranslateOptions& opts) {
    std::ifstream is(jsonl_path, std::ios::binary);
    if (!is) throw std::runtime_error("translate_file: cannot open " + jsonl_path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return translate_lines(lines, jsonl_path, opts);
}

TranslatedRun translate_file(const std::string& jsonl_path) {
    return translate_file(jsonl_path, TranslateOptions{});
}

bool write_combat_trace(const std::string& path, const TranslatedRun& run) {
    std::vector<eng::CombatState> states;
    for (const TranslatedRecord& r : run.records)
        if (r.in_combat) states.push_back(r.combat);
    if (states.empty()) return false;
    // The campaign driver leaves sim_action_bits null (B1.4 Log); action->bits
    // resolution is B1.6/B4.4, so per-record action bits are 0 here.
    std::vector<eng::Action> actions(states.size() - 1, eng::Action{0});
    return sts::diff::write_trace(path, run.seed, actions, states);
}

}  // namespace sts::translate
