// oracle_gate_check — G4 gate verifier (Stage B design §2.5, docs/stage-b-tasks.md
// G4 checklist items 1 & 2). Reads campaign JSONL artifacts (the driver's §2.7
// format) and checks, against the engine's own tier-1-tested RNG derivations:
//
//   PRESENCE (item 1, "present in every record"): every in-dungeon dump (every
//   action record carrying game_state.oracle) exposes all 13 dungeon stream
//   counters + neowRng-when-present (§2.5 phase rule), both save-parity pity
//   counters (cardBlizzRandomizer, blizzardPotionMod), the three EventHelper
//   event-pity floats (monster/shop/treasure), and purgeCost.
//
//   RNG CROSS-CHECK (item 2, "against the live game, not just golden vectors"):
//     * relicRng.counter == 5 at dungeon init (the 5 init relic-pool shuffles,
//       AbstractDungeon.java:1221-1256 / B1.2);
//     * each floor-scoped stream's pristine per-floor reseed (read off
//       cardRandomRng at counter==0, the B1.2 anchor) == sim
//       floor_stream(seed, floor) bit-for-bit, at EVERY floor entry;
//     * mapRng lies on the sim's map_stream(seed, act) trajectory: the dumped
//       raw state is reached from the pristine act-seed within `counter`-plus a
//       small margin of raw xorshift128+ steps (map generation mixes wrapper and
//       direct draws, so the wrapper counter under-counts next_long()s; matching
//       128-bit state at a specific step is a ~2^-128 coincidence, so trajectory
//       membership IS the seeding proof). This is the act-scoped derivation
//       holding against the live game.
//
// This tool #includes the engine's rng_stream.hpp / rng_xs128.hpp (the SAME
// constexpr code the tier-1 rng_stream_test pins), so the "sim derivation" is
// literally the engine's, not a re-port. It reads the uncommitted §7.3 campaign
// corpus at runtime; it is NOT a CI test (no game/data dependency in CI).
//
// Usage: oracle_gate_check <run.jsonl> [<run2.jsonl> ...]
// Exit 0 iff every record passed presence AND every RNG cross-check matched.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "sts/engine/rng_stream.hpp"
#include "sts/engine/rng_xs128.hpp"

using json = nlohmann::json;
namespace eng = sts::engine;

namespace {

// Signed Java long (as emitted) -> raw uint64 state bits (no sign extension bug).
uint64_t as_state(const json& v) { return static_cast<uint64_t>(v.get<int64_t>()); }

// The 13 dungeon streams that must be present in every in-dungeon dump (neowRng
// is the phase-gated 14th, checked separately).
const char* kDungeonStreams[13] = {
    "monsterRng", "eventRng", "merchantRng", "cardRng", "treasureRng", "relicRng",
    "potionRng", "monsterHpRng", "aiRng", "shuffleRng", "cardRandomRng", "miscRng",
    "mapRng"};

struct RunResult {
    std::string seed_str;
    int64_t seed = 0;
    int records = 0;
    int presence_fail = 0;         // records missing any required field
    std::string first_presence_err;
    bool relic_init_ok = false;    // relicRng.counter==5 at first dump
    int relic_init_counter = -1;
    std::map<int, bool> floor_ok;  // floor -> floor_stream match at pristine entry
    std::map<int, int> map_steps;  // act -> raw next_long steps to reach dumped mapRng
    std::map<int, bool> map_ok;    // act -> mapRng on map_stream(seed,act) trajectory
    bool ok() const {
        if (presence_fail || !relic_init_ok) return false;
        for (auto& [f, v] : floor_ok) if (!v) return false;
        for (auto& [a, v] : map_ok) if (!v) return false;
        return !floor_ok.empty() && !map_ok.empty();
    }
};

// Does `dumped` lie on the from_seed(seed_offset) trajectory within `bound`
// raw next_long steps? Returns step count (>=0) or -1 if not found.
int trajectory_steps(const eng::RngStream& start, uint64_t t0, uint64_t t1, int bound) {
    eng::RandomXS128 r(start.s0, start.s1);
    for (int n = 0; n <= bound; ++n) {
        if (r.state0() == t0 && r.state1() == t1) return n;
        (void)r.next_long();
    }
    return -1;
}

bool require_field(const json& obj, const char* key, RunResult& rr, const std::string& where) {
    if (!obj.contains(key)) {
        ++rr.presence_fail;
        if (rr.first_presence_err.empty())
            rr.first_presence_err = where + " missing " + key;
        return false;
    }
    return true;
}

RunResult check_run(const std::vector<std::string>& lines, const std::string& src) {
    RunResult rr;
    bool first = true;
    for (std::size_t li = 0; li < lines.size(); ++li) {
        if (lines[li].empty()) continue;
        json rec = json::parse(lines[li]);
        std::string kind = rec.value("record_kind", "");
        if (kind == "header") {
            if (rec.contains("seed")) {
                rr.seed = rec["seed"].value("long", static_cast<int64_t>(0));
                rr.seed_str = rec["seed"].value("string", std::string{});
            }
            continue;
        }
        if (kind != "action") continue;
        const json& sj = rec.at("state_json");
        if (!sj.contains("game_state")) continue;
        const json& gs = sj.at("game_state");
        if (!gs.contains("oracle")) continue;  // out-of-dungeon dump
        const json& o = gs.at("oracle");
        ++rr.records;
        const std::string where = src + " rec#" + std::to_string(li) +
                                   " floor" + std::to_string(gs.value("floor", -1));

        // ---- PRESENCE (item 1) ----
        bool present = true;
        if (require_field(o, "streams", rr, where)) {
            const json& st = o.at("streams");
            for (const char* s : kDungeonStreams)
                present &= require_field(st, s, rr, where + ".streams");
            // neowRng: phase-gated (§2.5); only checked/counted when present.
        } else present = false;
        present &= require_field(o, "cardBlizzRandomizer", rr, where);
        present &= require_field(o, "blizzardPotionMod", rr, where);
        if (require_field(o, "eventPity", rr, where)) {
            const json& ep = o.at("eventPity");
            present &= require_field(ep, "monster", rr, where + ".eventPity");
            present &= require_field(ep, "shop", rr, where + ".eventPity");
            present &= require_field(ep, "treasure", rr, where + ".eventPity");
        } else present = false;
        present &= require_field(o, "purgeCost", rr, where);
        (void)present;

        if (!o.contains("streams")) continue;
        const json& st = o.at("streams");
        int floor = gs.value("floor", -1);
        int act = o.value("act", gs.value("act", 1));

        // ---- relicRng.counter == 5 at dungeon init (item 2) ----
        if (first) {
            first = false;
            if (st.contains("relicRng")) {
                rr.relic_init_counter = st["relicRng"].value("counter", -1);
                rr.relic_init_ok = (rr.relic_init_counter == 5);
            }
        }

        // ---- floor_stream at pristine per-floor entry (item 2) ----
        if (st.contains("cardRandomRng")) {
            const json& cr = st["cardRandomRng"];
            if (cr.value("counter", -1) == 0 && rr.floor_ok.find(floor) == rr.floor_ok.end()) {
                eng::RngStream expected = eng::floor_stream(rr.seed, floor);
                bool m = (as_state(cr["s0"]) == expected.s0) &&
                         (as_state(cr["s1"]) == expected.s1);
                rr.floor_ok[floor] = m;
            }
        }

        // ---- mapRng on map_stream(seed, act) trajectory (item 2) ----
        if (st.contains("mapRng") && rr.map_ok.find(act) == rr.map_ok.end()) {
            const json& mr = st["mapRng"];
            eng::RngStream start = eng::map_stream(rr.seed, act);
            int steps = trajectory_steps(start, as_state(mr["s0"]), as_state(mr["s1"]), 8192);
            rr.map_ok[act] = (steps >= 0);
            rr.map_steps[act] = steps;
        }
    }
    return rr;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: oracle_gate_check <run.jsonl> [...]\n");
        return 2;
    }
    int total_records = 0, total_presence_fail = 0, failed_runs = 0, floors_checked = 0;
    for (int i = 1; i < argc; ++i) {
        std::ifstream is(argv[i], std::ios::binary);
        if (!is) { std::printf("ERR  cannot open %s\n", argv[i]); ++failed_runs; continue; }
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(is, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
        RunResult rr = check_run(lines, argv[i]);
        total_records += rr.records;
        total_presence_fail += rr.presence_fail;

        std::string floors, maps;
        for (auto& [f, v] : rr.floor_ok) { floors += std::to_string(f); floors += v ? "+" : "!"; floors += " "; ++floors_checked; }
        for (auto& [a, v] : rr.map_ok) { maps += "act" + std::to_string(a) + (v ? "+(" : "!(") + std::to_string(rr.map_steps[a]) + "steps) "; }
        bool ok = rr.ok();
        if (!ok) ++failed_runs;
        std::printf("%s %s(%lld): recs=%d presence_fail=%d relicRng_init=%d(%s) floors[%s] map[%s]\n",
                    ok ? "PASS" : "FAIL", rr.seed_str.c_str(), static_cast<long long>(rr.seed),
                    rr.records, rr.presence_fail, rr.relic_init_counter,
                    rr.relic_init_ok ? "ok" : "BAD", floors.c_str(), maps.c_str());
        if (rr.presence_fail) std::printf("     first presence error: %s\n", rr.first_presence_err.c_str());
    }
    std::printf("--- %d run(s): %d records checked, %d presence failures, %d floor-entries cross-checked, %d run(s) FAILED ---\n",
                argc - 1, total_records, total_presence_fail, floors_checked, failed_runs);
    return failed_runs == 0 ? 0 : 1;
}
