#include "sts/diff/reproducer.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace sts::diff {

using engine::Action;

namespace {

constexpr const char* kVersionLine = "STSREPRO v1";

// Human-readable decode of an action for the reproducer's inline comment.
std::string decode_action(Action a) {
    std::ostringstream os;
    switch (engine::action_verb(a)) {
        case engine::ActionVerb::PLAY_CARD:
            os << "PLAY_CARD hand=" << static_cast<int>(engine::action_arg0(a))
               << " target=" << static_cast<int>(engine::action_arg1(a));
            break;
        case engine::ActionVerb::END_TURN:
            os << "END_TURN";
            break;
        case engine::ActionVerb::USE_POTION:
            os << "USE_POTION slot=" << static_cast<int>(engine::action_arg0(a))
               << " target=" << static_cast<int>(engine::action_arg1(a));
            break;
        case engine::ActionVerb::CHOOSE:
            os << "CHOOSE option=" << static_cast<int>(engine::action_arg0(a));
            break;
        default:
            os << "UNKNOWN";
            break;
    }
    return os.str();
}

}  // namespace

bool write_reproducer(const std::string& path, int64_t seed,
                      std::span<const Action> action_prefix) {
    std::ofstream os(path, std::ios::trunc);
    if (!os) {
        return false;
    }

    os << kVersionLine << "\n";
    os << "seed " << seed << "\n";
    os << "actions " << action_prefix.size() << "\n";
    for (const Action a : action_prefix) {
        os << a.bits << "  # " << decode_action(a) << "\n";
    }

    os.flush();
    return static_cast<bool>(os);
}

bool read_reproducer(const std::string& path, int64_t& seed_out,
                     std::vector<Action>& actions_out) {
    std::ifstream is(path);
    if (!is) {
        return false;
    }

    std::string line;

    // Version line (first non-blank, non-comment line).
    auto next_line = [&](std::string& out) -> bool {
        while (std::getline(is, out)) {
            // Trim leading whitespace to detect blank/comment lines.
            std::size_t p = out.find_first_not_of(" \t\r");
            if (p == std::string::npos) continue;      // blank
            if (out[p] == '#') continue;               // full-line comment
            return true;
        }
        return false;
    };

    if (!next_line(line)) return false;
    {
        // Compare after trimming trailing CR (Windows-written files).
        std::string v = line;
        while (!v.empty() && (v.back() == '\r' || v.back() == ' ' || v.back() == '\t')) {
            v.pop_back();
        }
        if (v != kVersionLine) return false;
    }

    // seed <int64>
    if (!next_line(line)) return false;
    {
        std::istringstream ss(line);
        std::string key;
        int64_t seed = 0;
        if (!(ss >> key >> seed) || key != "seed") return false;
        seed_out = seed;
    }

    // actions <count>
    std::size_t count = 0;
    if (!next_line(line)) return false;
    {
        std::istringstream ss(line);
        std::string key;
        if (!(ss >> key >> count) || key != "actions") return false;
    }

    actions_out.clear();
    actions_out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (!next_line(line)) return false;
        std::istringstream ss(line);
        uint32_t bits = 0;
        if (!(ss >> bits)) return false;  // leading integer; rest of line ignored
        actions_out.push_back(Action{bits});
    }

    return true;
}

}  // namespace sts::diff
