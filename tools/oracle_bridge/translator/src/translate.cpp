#include "sts/translate/translate.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "sts/diff/trace.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
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
};

[[nodiscard]] std::string loc(const Ctx& c) {
    return c.source + ":record " + std::to_string(c.record_idx) + ":";
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

// ---- id joins (fail loud on an id the registry does not know) ------------

[[nodiscard]] eng::CardId join_card(const std::string& id, const Ctx& c,
                                    const std::string& where) {
    if (id.empty()) return eng::CardId::NONE;
    eng::CardId cid = reg::card_from_game_id(id);
    if (cid == eng::CardId::NONE) {
        throw TranslateError(loc(c) + " unknown card id \"" + id + "\" at " + where +
                             " — registry has no game_id mapping (schema drift, "
                             "translation aborted)");
    }
    return cid;
}
[[nodiscard]] eng::PowerId join_power(const std::string& id, const Ctx& c,
                                      const std::string& where) {
    if (id.empty()) return eng::PowerId::NONE;
    eng::PowerId pid = reg::power_from_game_id(id);
    if (pid == eng::PowerId::NONE) {
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
        throw TranslateError(loc(c) + " unknown relic id \"" + id + "\" at " + where +
                             " — registry has no game_id mapping (schema drift, "
                             "translation aborted)");
    }
    return rid;
}

// ---- card / power parsers (PROTOCOL §3.13 / §3.14) -----------------------

[[nodiscard]] eng::CardInstance parse_card(const json& j, const std::string& path,
                                           Ctx& ctx) {
    FieldReader fr(j, path, ctx);
    eng::CardInstance ci{};
    // id -> card_id (the translator join key, §2.6). Mapped.
    ci.card_id = static_cast<uint16_t>(join_card(as_str(fr.require("id"), ctx, path + ".id"),
                                                 ctx, path + ".id"));
    fr.mapped();
    if (const json* u = fr.take("upgrades")) {  // timesUpgraded -> upgrade
        ci.upgrade = static_cast<uint8_t>(as_i64(*u, ctx, path + ".upgrades"));
        fr.mapped();
    }
    if (const json* cost = fr.take("cost")) {  // costForTurn -> cost_now (>=0 only)
        int64_t cv = as_i64(*cost, ctx, path + ".cost");
        ci.cost_now = static_cast<uint8_t>(cv < 0 ? 0 : cv);
        fr.mapped();
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
    fr.defer("price");  // shop-only overlay (§3.9)
    fr.finish();
    return ci;
}

[[nodiscard]] eng::PowerSlot parse_power(const json& j, const std::string& path,
                                         Ctx& ctx) {
    FieldReader fr(j, path, ctx);
    eng::PowerSlot ps{};
    ps.power_id = static_cast<uint16_t>(
        join_power(as_str(fr.require("id"), ctx, path + ".id"), ctx, path + ".id"));
    fr.mapped();
    if (const json* a = fr.take("amount")) {
        ps.amount = static_cast<int16_t>(as_i64(*a, ctx, path + ".amount"));
        fr.mapped();
    }
    fr.ignore("name");
    fr.defer("damage");        // optional intent damage (§3.14)
    fr.defer("card");          // optional nested card (Nightmare etc.)
    fr.defer("misc");          // optional first-present bookkeeping value
    fr.defer("just_applied");
    fr.finish();
    return ps;
}

// Parse a powers list into a fixed slot array + count. Loud on overflow.
void parse_powers(const json& arr, const std::string& path, Ctx& ctx,
                  eng::PowerSlot* slots, uint8_t& count) {
    if (!arr.is_array()) throw TranslateError(loc(ctx) + " expected array at " + path);
    if (arr.size() > eng::kPowerCap) {
        throw TranslateError(loc(ctx) + " " + path + " has " +
                             std::to_string(arr.size()) + " powers > kPowerCap (" +
                             std::to_string(eng::kPowerCap) + ")");
    }
    count = 0;
    for (std::size_t i = 0; i < arr.size(); ++i) {
        slots[count++] = parse_power(arr[i], path + "[" + std::to_string(i) + "]", ctx);
    }
}

// ---- monster (PROTOCOL §3.12) --------------------------------------------

[[nodiscard]] eng::MonsterState parse_monster(const json& j, const std::string& path,
                                              Ctx& ctx) {
    FieldReader fr(j, path, ctx);
    eng::MonsterState ms{};
    ms.monster_id = static_cast<uint16_t>(
        join_monster(as_str(fr.require("id"), ctx, path + ".id"), ctx, path + ".id"));
    fr.mapped();
    ms.hp = static_cast<int16_t>(as_i64(fr.require("current_hp"), ctx, path + ".current_hp"));
    fr.mapped();
    ms.max_hp = static_cast<int16_t>(as_i64(fr.require("max_hp"), ctx, path + ".max_hp"));
    fr.mapped();
    if (const json* b = fr.take("block")) {
        ms.block = static_cast<int16_t>(as_i64(*b, ctx, path + ".block"));
        fr.mapped();
    }
    if (const json* mv = fr.take("move_id")) {  // telegraphed next move byte -> intent
        ms.intent = static_cast<uint8_t>(as_i64(*mv, ctx, path + ".move_id"));
        fr.mapped();
    }
    if (const json* pw = fr.take("powers")) {
        parse_powers(*pw, path + ".powers", ctx, ms.powers, ms.power_count);
        fr.mapped();
    }
    fr.ignore("name");            // localization
    fr.ignore("intent");          // presentation of move_id (§3.12); the byte is move_id
    fr.defer("move_base_damage");
    fr.defer("move_adjusted_damage");
    fr.defer("move_hits");
    fr.defer("half_dead");        // no schema flag yet
    fr.defer("is_gone");          // no schema flag yet
    fr.oracle("last_move_id");        // stock 2-back; authoritative = oracle move history (§2.5 #9)
    fr.oracle("second_last_move_id"); // "
    fr.finish();
    return ms;
}

// ---- player (PROTOCOL §3.15) writes into CombatState ---------------------

void parse_player(const json& j, const std::string& path, Ctx& ctx,
                  eng::CombatState& cs) {
    FieldReader fr(j, path, ctx);
    cs.player_hp = static_cast<int16_t>(as_i64(fr.require("current_hp"), ctx, path + ".current_hp"));
    fr.mapped();
    cs.player_max_hp = static_cast<int16_t>(as_i64(fr.require("max_hp"), ctx, path + ".max_hp"));
    fr.mapped();
    if (const json* b = fr.take("block")) {
        cs.player_block = static_cast<int16_t>(as_i64(*b, ctx, path + ".block"));
        fr.mapped();
    }
    if (const json* e = fr.take("energy")) {
        cs.player_energy = static_cast<int16_t>(as_i64(*e, ctx, path + ".energy"));
        fr.mapped();
    }
    if (const json* pw = fr.take("powers")) {
        parse_powers(*pw, path + ".powers", ctx, cs.player_powers, cs.player_power_count);
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
                uint8_t& count, int cap) {
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
        eng::CardInstance ci = parse_card(arr[i], path + "[" + std::to_string(i) + "]", ctx);
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

    fr.defer("neowRng");  // 14th stream; no schema field yet (§2.5 #2, B4.3)
    fr.finish();
}

// ---- oracle block (PROTOCOL §5.1) ----------------------------------------

struct OracleAnchors {
    int64_t seed = 0;
    int64_t floor = 0;
    int64_t act = 0;
    int64_t ascension = 0;
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

    // §2.5 items with no schema storage yet -> deferred to B4.3.
    fr.defer("eventPity");                // MONSTER/SHOP/TREASURE_CHANCE floats (§2.5 #5)
    fr.defer("purgeCost");                // shop purge ramp (§2.5 #6)
    fr.defer("eventList");                // remaining events (§2.5 #7)
    fr.defer("shrineList");               // remaining shrines (§2.5 #7)
    fr.defer("specialOneTimeEventList");  // remaining specials (§2.5 #7)
    fr.defer("relicPools");               // 5-tier pool orders (§2.5 #8)
    fr.defer("monster_move_history");     // full per-monster history (§2.5 #9);
                                          // CombatState holds only move_history[3]
    fr.finish();
    return a;
}

// ---- combat_state (PROTOCOL §3.10) ---------------------------------------

void parse_combat_state(const json& j, const std::string& path, Ctx& ctx,
                        eng::CombatState& cs) {
    FieldReader fr(j, path, ctx);
    cs.phase = static_cast<uint8_t>(eng::CombatPhase::WAITING_ON_USER);
    int pool_used = 0;  // running fill cursor into cs.card_pool (no struct field)

    if (const json* mons = fr.take("monsters")) {
        if (!mons->is_array()) throw TranslateError(loc(ctx) + " expected array at " + path + ".monsters");
        if (mons->size() > eng::kMonsterCap) {
            throw TranslateError(loc(ctx) + " " + path + ".monsters has " +
                                 std::to_string(mons->size()) + " > kMonsterCap (" +
                                 std::to_string(eng::kMonsterCap) + ")");
        }
        cs.monster_count = 0;
        for (std::size_t i = 0; i < mons->size(); ++i) {
            cs.monsters[cs.monster_count++] =
                parse_monster((*mons)[i], path + ".monsters[" + std::to_string(i) + "]", ctx);
        }
        fr.mapped();
    }

    if (const json* p = fr.take("hand"))
        { parse_pile(*p, path + ".hand", ctx, cs, pool_used, cs.hand, cs.hand_count, eng::kHandCap); fr.mapped(); }
    if (const json* p = fr.take("draw_pile")) {
        parse_pile(*p, path + ".draw_pile", ctx, cs, pool_used, cs.draw, cs.draw_count, eng::kDrawCap);
        fr.oracle("draw_pile");  // membership mapped; ORDER is advisory (§3.10 O)
    }
    if (const json* p = fr.take("discard_pile"))
        { parse_pile(*p, path + ".discard_pile", ctx, cs, pool_used, cs.discard, cs.discard_count, eng::kDiscardCap); fr.mapped(); }
    if (const json* p = fr.take("exhaust_pile"))
        { parse_pile(*p, path + ".exhaust_pile", ctx, cs, pool_used, cs.exhaust, cs.exhaust_count, eng::kExhaustCap); fr.mapped(); }
    if (const json* p = fr.take("limbo"))
        { parse_pile(*p, path + ".limbo", ctx, cs, pool_used, cs.limbo, cs.limbo_count, eng::kLimboCap); fr.mapped(); }
    if (const json* c = fr.take("card_in_play")) {
        // player.cardInUse: one card, appended to limbo (the sim's in-flight pile).
        if (cs.limbo_count >= eng::kLimboCap || pool_used >= eng::kCardPoolCap)
            throw TranslateError(loc(ctx) + " limbo/card_pool overflow at " + path + ".card_in_play");
        eng::CardInstance ci = parse_card(*c, path + ".card_in_play", ctx);
        eng::CardPoolIndex idx = static_cast<eng::CardPoolIndex>(pool_used);
        cs.card_pool[pool_used++] = ci;
        cs.limbo[cs.limbo_count++] = idx;
        fr.mapped();
    }

    if (const json* pl = fr.take("player")) { parse_player(*pl, path + ".player", ctx, cs); fr.mapped(); }
    if (const json* t = fr.take("turn")) {
        cs.turn = static_cast<uint16_t>(as_i64(*t, ctx, path + ".turn"));
        fr.mapped();
    }
    fr.defer("cards_discarded_this_turn");  // no matching CombatState counter
    fr.defer("times_damaged");              // damagedThisCombat; no schema field
    fr.finish();
}

// ---- screen_state variants (PROTOCOL §3.3-3.9, §3.19) --------------------
//
// None of the screen_state content has schema storage yet (map / events / shop /
// rewards are B4.x). Each variant's key-set is enumerated so a KNOWN field never
// trips the drift error, while a genuinely new key still does. Nested objects
// (cards, relics, potions, options, nodes) are structurally validated too.

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
    fr.defer("price");  // shop overlay
    fr.finish();
    if (out) *out = rs;
}

void parse_screen_state(const json& j, const std::string& path, Ctx& ctx,
                        const std::string& screen_type) {
    FieldReader fr(j, path, ctx);
    if (screen_type == "EVENT") {
        fr.ignore("body_text");
        fr.ignore("event_name");
        fr.defer("event_id");
        if (const json* opts = fr.take("options")) {
            for (std::size_t i = 0; i < opts->size(); ++i) {
                FieldReader o((*opts)[i], path + ".options[" + std::to_string(i) + "]", ctx);
                o.ignore("text");
                o.ignore("label");
                o.defer("disabled");
                o.defer("choice_index");
                o.finish();
            }
            fr.defer("options");
        }
    } else if (screen_type == "CHEST" || screen_type == "REST") {
        defer_all(fr, {"chest_type", "chest_open", "has_rested", "rest_options"});
    } else if (screen_type == "CARD_REWARD") {
        fr.defer("bowl_available");
        fr.defer("skip_available");
        defer_card_list(fr, "cards", path, ctx);
    } else if (screen_type == "COMBAT_REWARD") {
        if (const json* rw = fr.take("rewards")) {
            for (std::size_t i = 0; i < rw->size(); ++i) {
                const std::string rp = path + ".rewards[" + std::to_string(i) + "]";
                FieldReader r((*rw)[i], rp, ctx);
                r.defer("reward_type");
                r.defer("gold");
                if (const json* rl = r.take("relic")) { parse_relic(*rl, rp + ".relic", ctx, nullptr); r.defer("relic"); }
                if (const json* pt = r.take("potion")) { FieldReader p(*pt, rp + ".potion", ctx); p.defer("id"); p.ignore("name"); p.defer("can_use"); p.defer("can_discard"); p.defer("requires_target"); p.defer("price"); p.finish(); r.defer("potion"); }
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
        if (const json* rl = fr.take("relics")) {  // S2 scope; validated structurally
            for (std::size_t i = 0; i < rl->size(); ++i)
                parse_relic((*rl)[i], path + ".relics[" + std::to_string(i) + "]", ctx, nullptr);
            fr.ignore("relics");
        }
    } else if (screen_type == "SHOP_SCREEN") {
        defer_card_list(fr, "cards", path, ctx);
        if (const json* rl = fr.take("relics")) {
            for (std::size_t i = 0; i < rl->size(); ++i)
                parse_relic((*rl)[i], path + ".relics[" + std::to_string(i) + "]", ctx, nullptr);
            fr.defer("relics");
        }
        if (const json* pt = fr.take("potions")) {
            for (std::size_t i = 0; i < pt->size(); ++i) {
                FieldReader p((*pt)[i], path + ".potions[" + std::to_string(i) + "]", ctx);
                p.defer("id"); p.ignore("name"); p.defer("can_use"); p.defer("can_discard");
                p.defer("requires_target"); p.defer("price");
                p.finish();
            }
            fr.defer("potions");
        }
        fr.defer("purge_available");
        fr.defer("purge_cost");
    } else if (screen_type == "GRID") {
        defer_card_list(fr, "cards", path, ctx);
        defer_card_list(fr, "selected_cards", path, ctx);
        defer_all(fr, {"num_cards", "any_number", "for_upgrade", "for_transform",
                       "for_purge", "confirm_up"});
    } else if (screen_type == "HAND_SELECT") {
        defer_card_list(fr, "hand", path, ctx);
        defer_card_list(fr, "selected", path, ctx);
        defer_all(fr, {"max_cards", "can_pick_zero"});
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
    for (std::size_t i = 0; i < arr.size(); ++i) {
        const std::string pp = path + "[" + std::to_string(i) + "]";
        FieldReader fr(arr[i], pp, ctx);
        std::string id = as_str(fr.require("id"), ctx, pp + ".id");
        if (id == "Potion Slot") {
            rs.potions[i] = static_cast<uint16_t>(reg::PotionId::NONE);  // empty slot
        } else {
            reg::PotionId pid = reg::potion_from_game_id(id);
            if (pid == reg::PotionId::NONE) {
                throw TranslateError(loc(ctx) + " unknown potion id \"" + id + "\" at " +
                                     pp + ".id — registry has no game_id mapping "
                                     "(schema drift, translation aborted)");
            }
            rs.potions[i] = static_cast<uint16_t>(pid);
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
                parse_card((*deck)[i], path + ".deck[" + std::to_string(i) + "]", ctx);
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
    if (const json* combat = fr.take("combat_state")) {
        out.in_combat = true;
        has_combat = true;
        parse_combat_state(*combat, path + ".combat_state", ctx, out.combat);
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
    fr.defer("act_boss");         // bossKey -> boss_ids placeholder (B4.3)
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
        parse_screen_state(*screen, path + ".screen_state", ctx, screen_type);
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
    run.records.push_back(std::move(out));
    return true;
}

}  // namespace

// ---- public API ----------------------------------------------------------

TranslatedRun translate_lines(const std::vector<std::string>& lines,
                              const std::string& source_name) {
    TranslatedRun run;
    Ctx ctx;
    ctx.source = source_name;
    ctx.stats = &run.stats;
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
        translate_record(rec, ctx, run);
    }
    return run;
}

TranslatedRun translate_file(const std::string& jsonl_path) {
    std::ifstream is(jsonl_path, std::ios::binary);
    if (!is) throw std::runtime_error("translate_file: cannot open " + jsonl_path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return translate_lines(lines, jsonl_path);
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
