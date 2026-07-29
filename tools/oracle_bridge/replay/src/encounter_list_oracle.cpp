// B5.2 raw encounter-list oracle.
//
// Each campaign artifact's first in-game dump carries the live
// AbstractDungeon monsterList / eliteMonsterList / bossList remaining order.
// Rebuild the same run from its signed seed and compare all entries plus the
// post-construction monsterRng state. This is deliberately separate from
// RunState translation: the lists are RunController state, not serialized
// RunState fields.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "sts/engine/run_advance.hpp"
#include "sts/engine/rng_stream.hpp"

namespace {

using nlohmann::json;
using sts::engine::MonsterLists;
using sts::engine::RngStream;
using sts::engine::RunController;

struct Capture {
    int64_t seed = 0;
    std::vector<std::string> monster;
    std::vector<std::string> elite;
    std::vector<std::string> boss;
    RngStream monster_rng{};
};

[[nodiscard]] RngStream stream_from_json(const json& value) {
    RngStream out{};
    out.counter = value.at("counter").get<int32_t>();
    out.s0 = static_cast<uint64_t>(value.at("s0").get<int64_t>());
    out.s1 = static_cast<uint64_t>(value.at("s1").get<int64_t>());
    return out;
}

[[nodiscard]] std::vector<std::string> string_list(const json& value,
                                                   const char* where) {
    if (!value.is_array()) throw std::runtime_error(std::string(where) + " is not an array");
    std::vector<std::string> out;
    out.reserve(value.size());
    for (const json& item : value) {
        if (!item.is_string()) {
            throw std::runtime_error(std::string(where) + " contains a non-string");
        }
        out.push_back(item.get<std::string>());
    }
    return out;
}

[[nodiscard]] Capture read_capture(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const json record = json::parse(line);
        if (record.value("record_kind", std::string{}) != "action") continue;
        const json& state = record.at("state_json");
        if (!state.value("in_game", false)) continue;
        const json& game = state.at("game_state");
        const json& oracle = game.at("oracle");
        const auto found = oracle.find("encounterLists");
        if (found == oracle.end()) {
            throw std::runtime_error(
                "first in-game oracle block lacks B5.2 encounterLists");
        }
        const json& lists = *found;
        Capture out;
        out.seed = game.at("seed").get<int64_t>();
        out.monster = string_list(lists.at("monster"), "encounterLists.monster");
        out.elite = string_list(lists.at("elite"), "encounterLists.elite");
        out.boss = string_list(lists.at("boss"), "encounterLists.boss");
        out.monster_rng =
            stream_from_json(oracle.at("streams").at("monsterRng"));
        return out;
    }
    throw std::runtime_error("artifact has no in-game action record");
}

template <std::size_t N>
bool compare_list(const char* name, const std::vector<std::string>& expected,
                  const std::array<std::string_view, N>& actual,
                  uint8_t actual_count) {
    bool clean = true;
    if (expected.size() != actual_count) {
        std::printf("  %s.count: oracle=%zu sim=%u\n", name, expected.size(),
                    static_cast<unsigned>(actual_count));
        clean = false;
    }
    const std::size_t shared =
        std::min(expected.size(), static_cast<std::size_t>(actual_count));
    for (std::size_t i = 0; i < shared; ++i) {
        if (expected[i] != actual[i]) {
            std::printf("  %s[%zu]: oracle=\"%s\" sim=\"%.*s\"\n", name, i,
                        expected[i].c_str(), static_cast<int>(actual[i].size()),
                        actual[i].data());
            clean = false;
        }
    }
    return clean;
}

[[nodiscard]] bool same_stream(const RngStream& left,
                               const RngStream& right) noexcept {
    return left.counter == right.counter && left.s0 == right.s0 &&
           left.s1 == right.s1;
}

[[nodiscard]] bool check_one(const std::string& path) {
    const Capture expected = read_capture(path);
    RunController actual = sts::engine::run_begin(expected.seed, 20);
    const MonsterLists& lists = actual.lists;

    bool clean = true;
    clean &= compare_list("monster", expected.monster, lists.monster_list,
                          lists.monster_list_count);
    clean &= compare_list("elite", expected.elite, lists.elite_list,
                          lists.elite_list_count);
    clean &= compare_list("boss", expected.boss, lists.boss_list,
                          lists.boss_list_count);
    if (!same_stream(expected.monster_rng, actual.run.monster_rng)) {
        std::printf(
            "  monsterRng: oracle={%d,%llu,%llu} sim={%d,%llu,%llu}\n",
            expected.monster_rng.counter,
            static_cast<unsigned long long>(expected.monster_rng.s0),
            static_cast<unsigned long long>(expected.monster_rng.s1),
            actual.run.monster_rng.counter,
            static_cast<unsigned long long>(actual.run.monster_rng.s0),
            static_cast<unsigned long long>(actual.run.monster_rng.s1));
        clean = false;
    }
    std::printf("%s %s seed=%lld monster=%zu elite=%zu boss=%zu\n",
                clean ? "LISTS OK  " : "LISTS DIFF", path.c_str(),
                static_cast<long long>(expected.seed), expected.monster.size(),
                expected.elite.size(), expected.boss.size());
    return clean;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: encounter_list_oracle <run.jsonl> [more.jsonl ...]\n");
        return 2;
    }
    int failures = 0;
    for (int i = 1; i < argc; ++i) {
        try {
            if (!check_one(argv[i])) ++failures;
        } catch (const std::exception& error) {
            std::printf("LISTS ERROR %s: %s\n", argv[i], error.what());
            ++failures;
        }
    }
    std::printf("--- %d file(s), %d list divergence/error ---\n", argc - 1,
                failures);
    return failures == 0 ? 0 : 1;
}
