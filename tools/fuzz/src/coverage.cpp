#include "sts/fuzz/coverage.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "sts/engine/map_rooms.hpp"
#include "sts/registry/game_ids.hpp"

namespace sts::fuzz {

const char* end_reason_name(EndReason r) noexcept {
    switch (r) {
        case EndReason::RUN_OVER: return "run_over";
        case EndReason::ROOM_UNIMPLEMENTED: return "room_unimplemented";
        case EndReason::NO_LEGAL_MOVES: return "no_legal_moves";
        case EndReason::ACTION_CAP: return "action_cap";
        case EndReason::LIVELOCK: return "livelock";
        case EndReason::NO_PROGRESS: return "no_progress";
        case EndReason::COUNT: break;
    }
    return "?";
}

namespace {

const char* room_name(int r) noexcept {
    switch (static_cast<engine::RoomType>(r)) {
        case engine::RoomType::None: return "none";
        case engine::RoomType::Monster: return "monster";
        case engine::RoomType::Event: return "event";
        case engine::RoomType::Elite: return "elite";
        case engine::RoomType::Rest: return "rest";
        case engine::RoomType::Shop: return "shop";
        case engine::RoomType::Treasure: return "treasure";
        case engine::RoomType::Boss: return "boss";
    }
    return "?";
}

const char* reward_kind_name(int k) noexcept {
    switch (k) {
        case 0: return "none";
        case 1: return "gold";
        case 2: return "potion";
        case 3: return "relic";
        case 4: return "cards";
        default: return "?";
    }
}

const char* turn_bucket_label(int i) noexcept {
    static const char* kLabels[kTurnBuckets] = {"1", "2", "3", "4", "5-6", "7-9",
                                                "10-19", "20+"};
    return kLabels[i];
}

// The ONE description of every scalar counter, shared by kv() and the parser so
// the two cannot drift (a merge that silently drops a field would understate a
// soak's totals, which is the failure mode this shape removes).
template <class C, class F>
void visit_scalars(C& c, F&& f) {
    f("cases", c.cases);
    f("runs", c.runs);
    f("actions", c.actions);
    f("actions_engine", c.actions_engine);
    for (int i = 0; i < static_cast<int>(EndReason::COUNT); ++i) {
        f(std::string("end.") + end_reason_name(static_cast<EndReason>(i)),
          c.end_reason[i]);
        f(std::string("end_actions.") + end_reason_name(static_cast<EndReason>(i)),
          c.end_actions[i]);
    }
    for (int i = 0; i < static_cast<int>(PolicyKind::COUNT); ++i) {
        f(std::string("pol_cases.") + policy_name(static_cast<PolicyKind>(i)),
          c.per_policy_cases[i]);
        f(std::string("pol_actions.") + policy_name(static_cast<PolicyKind>(i)),
          c.per_policy_actions[i]);
    }
    for (int i = 0; i < kRoomTypeCount; ++i) {
        f(std::string("room_entered.") + room_name(i), c.room_entered[i]);
        f(std::string("room_stalled.") + room_name(i), c.room_stalled[i]);
    }
    for (int i = 0; i < static_cast<int>(MoveCat::COUNT); ++i) {
        f(std::string("move_legal.") + move_cat_name(static_cast<MoveCat>(i)),
          c.move_legal[i]);
        f(std::string("move_taken.") + move_cat_name(static_cast<MoveCat>(i)),
          c.move_taken[i]);
    }
    for (int i = 0; i < kRewardKindCount; ++i) {
        f(std::string("reward_claimed.") + reward_kind_name(i), c.reward_claimed[i]);
    }
    f("combats_entered", c.combats_entered);
    f("combats_killed", c.combats_killed);
    f("combats_smoked", c.combats_smoked);
    f("deaths", c.deaths);
    f("victories", c.victories);
    f("reward_screens", c.reward_screens);
    f("cards_taken", c.cards_taken);
    f("cards_skipped", c.cards_skipped);
    f("potions_used", c.potions_used);
    f("relics_gained", c.relics_gained);
    f("escapes", c.escapes);
    for (int i = 0; i < kTurnBuckets; ++i) {
        f(std::string("turn_bucket.") + turn_bucket_label(i), c.turn_bucket[i]);
    }
    for (int i = 0; i < kFloorBuckets; ++i) {
        f(std::string("floor_bucket.") + std::to_string(i), c.floor_bucket[i]);
    }
}

// Maxima merge by max, not by sum, so they get their own visitor.
template <class C, class F>
void visit_maxima(C& c, F&& f) {
    f("max_turn", c.max_turn);
    f("max_floor", c.max_floor);
    f("max_actions_in_case", c.max_actions_in_case);
}

}  // namespace

void Coverage::merge(const Coverage& o) noexcept {
    // Two cursors over one visitor order: collect both sides' addresses, then
    // add pairwise. visit_scalars is the single description of the field set,
    // so a field added there is merged, printed and parsed without a second
    // edit anywhere.
    std::vector<uint64_t*> dst;
    visit_scalars(*this, [&](const std::string&, uint64_t& v) { dst.push_back(&v); });
    std::vector<const uint64_t*> src;
    visit_scalars(o, [&](const std::string&, const uint64_t& v) { src.push_back(&v); });
    for (size_t i = 0; i < dst.size() && i < src.size(); ++i) {
        *dst[i] += *src[i];
    }
    if (o.max_turn > max_turn) max_turn = o.max_turn;
    if (o.max_floor > max_floor) max_floor = o.max_floor;
    if (o.max_actions_in_case > max_actions_in_case) {
        max_actions_in_case = o.max_actions_in_case;
    }
    cards_played.merge(o.cards_played);
    monsters_fought.merge(o.monsters_fought);
    relics_owned.merge(o.relics_owned);
    potions_held.merge(o.potions_held);
    powers_applied.merge(o.powers_applied);
}

bool Coverage::merge_checked(const Coverage& o) noexcept {
    std::vector<uint64_t*> dst;
    visit_scalars(*this, [&](const std::string&, uint64_t& v) {
        dst.push_back(&v);
    });
    std::vector<const uint64_t*> src;
    visit_scalars(o, [&](const std::string&, const uint64_t& v) {
        src.push_back(&v);
    });
    for (size_t i = 0; i < dst.size() && i < src.size(); ++i) {
        if (*src[i] > UINT64_MAX - *dst[i]) return false;
    }
    merge(o);
    return true;
}

std::string Coverage::kv() const {
    std::ostringstream os;
    visit_scalars(*this, [&](const std::string& k, const uint64_t& v) {
        os << k << " " << v << "\n";
    });
    visit_maxima(*this, [&](const std::string& k, const uint32_t& v) {
        os << k << " " << v << "\n";
    });
    auto set_line = [&](const char* name, const uint64_t* w, size_t n) {
        os << "set." << name;
        for (size_t i = 0; i < n; ++i) {
            char buf[20];
            std::snprintf(buf, sizeof(buf), " %016llx",
                          static_cast<unsigned long long>(w[i]));
            os << buf;
        }
        os << "\n";
    };
    set_line("cards_played", cards_played.w, decltype(cards_played)::kWords);
    set_line("monsters_fought", monsters_fought.w, decltype(monsters_fought)::kWords);
    set_line("relics_owned", relics_owned.w, decltype(relics_owned)::kWords);
    set_line("potions_held", potions_held.w, decltype(potions_held)::kWords);
    set_line("powers_applied", powers_applied.w, decltype(powers_applied)::kWords);
    return os.str();
}

const std::vector<std::string>& legacy_optional_kv_keys() {
    // ONE entry today, and the list is the whole of the tolerance. `victories`
    // was added by 6d7efc4; every summary written before it lacks the key.
    // A future counter joins this list ONLY together with the archive it has to
    // read -- an entry here is a permanent statement that the field may be
    // silently 0, so it is not somewhere to park a field that is merely new.
    static const std::vector<std::string> kKeys = {"victories"};
    return kKeys;
}

namespace {

// The shared body. `optional` names the keys a summary may lack; anything else
// missing, unknown or malformed is still a hard failure.
bool parse_kv(const std::string& text, Coverage& out,
              const std::vector<std::string>* optional,
              std::vector<std::string>* defaulted) {
    Coverage c;
    std::vector<std::string> keys;
    std::vector<uint64_t*> slots;
    visit_scalars(c, [&](const std::string& k, uint64_t& v) {
        keys.push_back(k);
        slots.push_back(&v);
    });
    std::unordered_set<std::string> expected(keys.begin(), keys.end());
    expected.insert("max_turn");
    expected.insert("max_floor");
    expected.insert("max_actions_in_case");
    expected.insert("set.cards_played");
    expected.insert("set.monsters_fought");
    expected.insert("set.relics_owned");
    expected.insert("set.potions_held");
    expected.insert("set.powers_applied");
    std::unordered_set<std::string> seen;

    auto parse_u64 = [](const std::string& token, int base, uint64_t& value) {
        if (token.empty()) return false;
        const auto parsed = std::from_chars(
            token.data(), token.data() + token.size(), value, base);
        return parsed.ec == std::errc{} &&
               parsed.ptr == token.data() + token.size();
    };

    auto find_slot = [&](const std::string& k) -> uint64_t* {
        for (size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == k) return slots[i];
        }
        return nullptr;
    };

    std::istringstream is(text);
    std::string line;
    while (std::getline(is, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (!expected.contains(key) || !seen.insert(key).second) return false;
        if (key.rfind("set.", 0) == 0) {
            const std::string name = key.substr(4);
            uint64_t* w = nullptr;
            size_t n = 0;
            if (name == "cards_played") {
                w = c.cards_played.w; n = decltype(c.cards_played)::kWords;
            } else if (name == "monsters_fought") {
                w = c.monsters_fought.w; n = decltype(c.monsters_fought)::kWords;
            } else if (name == "relics_owned") {
                w = c.relics_owned.w; n = decltype(c.relics_owned)::kWords;
            } else if (name == "potions_held") {
                w = c.potions_held.w; n = decltype(c.potions_held)::kWords;
            } else if (name == "powers_applied") {
                w = c.powers_applied.w; n = decltype(c.powers_applied)::kWords;
            } else {
                return false;
            }
            for (size_t i = 0; i < n; ++i) {
                std::string hx;
                if (!(ls >> hx)) return false;
                if (hx.size() != 16 || !parse_u64(hx, 16, w[i])) return false;
            }
            std::string extra;
            if (ls >> extra) return false;
            continue;
        }
        if (key == "max_turn" || key == "max_floor" || key == "max_actions_in_case") {
            std::string token;
            uint64_t v = 0;
            if (!(ls >> token) || !parse_u64(token, 10, v) || v > UINT32_MAX) {
                return false;
            }
            std::string extra;
            if (ls >> extra) return false;
            const auto v32 = static_cast<uint32_t>(v);
            if (key == "max_turn") c.max_turn = v32;
            else if (key == "max_floor") c.max_floor = v32;
            else c.max_actions_in_case = v32;
            continue;
        }
        uint64_t* slot = find_slot(key);
        if (slot == nullptr) return false;
        std::string token;
        uint64_t v = 0;
        if (!(ls >> token) || !parse_u64(token, 10, v)) return false;
        std::string extra;
        if (ls >> extra) return false;
        *slot = v;
    }
    if (seen.size() != expected.size()) {
        // Something is missing. That is drift unless EVERY absentee is a known
        // legacy key and the caller asked for tolerance.
        if (optional == nullptr) return false;
        for (const std::string& k : expected) {
            if (seen.contains(k)) continue;
            if (std::find(optional->begin(), optional->end(), k) == optional->end())
                return false;
            if (defaulted != nullptr) defaulted->push_back(k);
        }
        // Every missing key was legacy-optional, and `c` already holds the
        // value-initialised 0 for each -- that is the default, stated.
    }
    out = c;
    return true;
}

}  // namespace

bool coverage_from_kv(const std::string& text, Coverage& out) {
    return parse_kv(text, out, /*optional=*/nullptr, /*defaulted=*/nullptr);
}

bool coverage_from_kv_legacy(const std::string& text, Coverage& out,
                             std::vector<std::string>& defaulted) {
    defaulted.clear();
    return parse_kv(text, out, &legacy_optional_kv_keys(), &defaulted);
}

namespace {

void bar(std::ostringstream& os, const char* label, uint64_t v, uint64_t total) {
    const double pct = total > 0 ? 100.0 * static_cast<double>(v) /
                                        static_cast<double>(total)
                                 : 0.0;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "  %-22s %14llu  %6.2f%%\n", label,
                  static_cast<unsigned long long>(v), pct);
    os << buf;
}

template <std::size_t N, class NameFn>
void set_report(std::ostringstream& os, const char* title, const SeenSet<N>& s,
                NameFn name_of) {
    // Only ids the registry actually defines are counted: `*_game_id` returns
    // "" for an id with no row, which is how a gap in an append-only id space
    // is told apart from a row that was never exercised.
    std::size_t rows = 0;
    std::size_t seen = 0;
    std::string never;
    std::size_t n_never = 0;
    for (std::size_t i = 1; i < N; ++i) {  // id 0 is the NONE sentinel
        const std::string_view g = name_of(i);
        if (g.empty()) continue;
        ++rows;
        if (s.test(i)) {
            ++seen;
            continue;
        }
        ++n_never;
        if (never.size() < 900) {
            if (!never.empty()) never += ", ";
            never += std::string(g);
        }
    }
    os << title << ": " << seen << " / " << rows << " registry rows seen\n";
    if (n_never == 0) {
        os << "    (every row seen)\n";
    } else {
        os << "    never seen (" << n_never << "): " << never;
        if (never.size() >= 900) os << ", ...";
        os << "\n";
    }
}

}  // namespace

std::string Coverage::report(double elapsed_s) const {
    std::ostringstream os;
    char buf[256];

    os << "==================== FUZZ SOAK SUMMARY ====================\n";
    std::snprintf(buf, sizeof(buf),
                  "cases (seed x policy x policy-seed) : %llu\n"
                  "engine runs (>=2 per case)          : %llu\n"
                  "ACTIONS (counted once per case)     : %llu\n"
                  "actions stepped incl. replay passes : %llu\n",
                  static_cast<unsigned long long>(cases),
                  static_cast<unsigned long long>(runs),
                  static_cast<unsigned long long>(actions),
                  static_cast<unsigned long long>(actions_engine));
    os << buf;
    if (elapsed_s > 0.0) {
        std::snprintf(buf, sizeof(buf),
                      "elapsed                             : %.1f s "
                      "(%.0f actions/s counted, %.0f/s stepped)\n",
                      elapsed_s, static_cast<double>(actions) / elapsed_s,
                      static_cast<double>(actions_engine) / elapsed_s);
        os << buf;
    }

    os << "\n-- how runs ended (cases, and the actions they spent) --\n";
    for (int i = 0; i < static_cast<int>(EndReason::COUNT); ++i) {
        const double capct = cases > 0 ? 100.0 * static_cast<double>(end_reason[i]) /
                                             static_cast<double>(cases)
                                       : 0.0;
        const double acpct = actions > 0 ? 100.0 * static_cast<double>(end_actions[i]) /
                                               static_cast<double>(actions)
                                         : 0.0;
        std::snprintf(buf, sizeof(buf),
                      "  %-20s cases %10llu (%5.1f%%)   actions %12llu (%5.1f%%)\n",
                      end_reason_name(static_cast<EndReason>(i)),
                      static_cast<unsigned long long>(end_reason[i]), capct,
                      static_cast<unsigned long long>(end_actions[i]), acpct);
        os << buf;
    }
    {
        const int lv = static_cast<int>(EndReason::LIVELOCK);
        if (end_actions[lv] > 0 && actions > 0) {
            const double share = 100.0 * static_cast<double>(end_actions[lv]) /
                                 static_cast<double>(actions);
            if (share >= 10.0) {
                std::snprintf(buf, sizeof(buf),
                              "  !! %.1f%% of the action budget went into runs that "
                              "ended up cycling. That share of the total is NOT "
                              "coverage.\n",
                              share);
                os << buf;
            }
        }
    }

    os << "\n-- per policy --\n";
    for (int i = 0; i < static_cast<int>(PolicyKind::COUNT); ++i) {
        std::snprintf(buf, sizeof(buf), "  %-16s cases %10llu   actions %12llu\n",
                      policy_name(static_cast<PolicyKind>(i)),
                      static_cast<unsigned long long>(per_policy_cases[i]),
                      static_cast<unsigned long long>(per_policy_actions[i]));
        os << buf;
    }

    os << "\n-- rooms --\n";
    for (int i = 1; i < kRoomTypeCount; ++i) {
        std::snprintf(buf, sizeof(buf), "  %-10s entered %10llu   parked-unimpl %10llu\n",
                      room_name(i), static_cast<unsigned long long>(room_entered[i]),
                      static_cast<unsigned long long>(room_stalled[i]));
        os << buf;
    }

    os << "\n-- combat depth (max turn reached, per fight) --\n";
    uint64_t fights = 0;
    for (int i = 0; i < kTurnBuckets; ++i) fights += turn_bucket[i];
    for (int i = 0; i < kTurnBuckets; ++i) {
        bar(os, turn_bucket_label(i), turn_bucket[i], fights);
    }
    std::snprintf(buf, sizeof(buf), "  deepest turn seen      %14u\n", max_turn);
    os << buf;

    os << "\n-- floors reached (per case, final floor) --\n";
    for (int i = 0; i < kFloorBuckets; ++i) {
        if (floor_bucket[i] == 0) continue;
        const std::string lbl =
            i == kFloorBuckets - 1 ? std::to_string(i) + "+" : std::to_string(i);
        bar(os, lbl.c_str(), floor_bucket[i], cases);
    }
    std::snprintf(buf, sizeof(buf), "  deepest floor seen     %14u\n", max_floor);
    os << buf;

    os << "\n-- what the policies actually did --\n";
    os << "  category                legal-steps      taken\n";
    for (int i = 0; i < static_cast<int>(MoveCat::COUNT); ++i) {
        std::snprintf(buf, sizeof(buf), "  %-20s %14llu %10llu\n",
                      move_cat_name(static_cast<MoveCat>(i)),
                      static_cast<unsigned long long>(move_legal[i]),
                      static_cast<unsigned long long>(move_taken[i]));
        os << buf;
    }

    os << "\n-- run-layer events --\n";
    std::snprintf(buf, sizeof(buf),
                  "  combats entered %12llu   killed %12llu   smoke-bomb escapes %8llu\n"
                  "  deaths          %12llu   victories %10llu   reward screens %6llu\n"
                  "  cards taken     %12llu   cards skipped %7llu\n"
                  "  potions used    %12llu   relics held (sum) %10llu\n",
                  static_cast<unsigned long long>(combats_entered),
                  static_cast<unsigned long long>(combats_killed),
                  static_cast<unsigned long long>(combats_smoked),
                  static_cast<unsigned long long>(deaths),
                  static_cast<unsigned long long>(victories),
                  static_cast<unsigned long long>(reward_screens),
                  static_cast<unsigned long long>(cards_taken),
                  static_cast<unsigned long long>(cards_skipped),
                  static_cast<unsigned long long>(potions_used),
                  static_cast<unsigned long long>(relics_gained));
    os << buf;
    os << "  reward claims by kind:";
    for (int i = 1; i < kRewardKindCount; ++i) {
        os << "  " << reward_kind_name(i) << "=" << reward_claimed[i];
    }
    os << "\n";

    os << "\n-- registry rows exercised (the design §7.4 'to-fuzz list') --\n";
    set_report(os, "cards played", cards_played, [](std::size_t i) {
        return registry::card_game_id(static_cast<registry::CardId>(i));
    });
    set_report(os, "monsters fought", monsters_fought, [](std::size_t i) {
        return registry::monster_game_id(static_cast<registry::MonsterId>(i));
    });
    set_report(os, "relics held", relics_owned, [](std::size_t i) {
        return registry::relic_game_id(static_cast<registry::RelicId>(i));
    });
    set_report(os, "potions held", potions_held, [](std::size_t i) {
        return registry::potion_game_id(static_cast<registry::PotionId>(i));
    });
    set_report(os, "powers applied", powers_applied, [](std::size_t i) {
        return registry::power_game_id(static_cast<registry::PowerId>(i));
    });

    os << "\n-- NEVER REACHED (stated, not inferred) --\n";
    for (int i = 1; i < kRoomTypeCount; ++i) {
        if (room_entered[i] == 0) os << "  room type never entered: " << room_name(i) << "\n";
    }
    for (int i = 0; i < static_cast<int>(MoveCat::COUNT); ++i) {
        if (move_legal[i] == 0) {
            os << "  move category never LEGAL: " << move_cat_name(static_cast<MoveCat>(i))
               << "\n";
        } else if (move_taken[i] == 0) {
            os << "  move category legal but never TAKEN: "
               << move_cat_name(static_cast<MoveCat>(i)) << "\n";
        }
    }
    for (int i = 1; i < kRewardKindCount; ++i) {
        if (reward_claimed[i] == 0) {
            os << "  reward kind never claimed: " << reward_kind_name(i) << "\n";
        }
    }
    if (combats_smoked == 0) os << "  no combat was ever escaped (Smoke Bomb)\n";
    if (deaths == 0) os << "  the player never died\n";
    if (potions_used == 0) os << "  no potion was ever used\n";
    os << "==========================================================\n";
    return os.str();
}

}  // namespace sts::fuzz
