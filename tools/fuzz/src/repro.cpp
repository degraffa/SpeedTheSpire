#include "sts/fuzz/repro.hpp"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace sts::fuzz {

using engine::Action;
using engine::ActionVerb;

std::string case_id_line(const CaseId& c) {
    std::ostringstream os;
    os << "seed=" << c.run_seed << " asc=" << static_cast<int>(c.ascension)
       << " policy=" << policy_name(c.policy) << " pseed=" << c.policy_seed;
    return os.str();
}

std::string decode_action(Action a) {
    std::ostringstream os;
    const uint8_t a0 = engine::action_arg0(a);
    const uint8_t a1 = engine::action_arg1(a);
    switch (engine::action_verb(a)) {
        case ActionVerb::PLAY_CARD:
            os << "PLAY_CARD hand=" << static_cast<int>(a0)
               << " target=" << static_cast<int>(a1);
            break;
        case ActionVerb::END_TURN:
            os << "END_TURN";
            break;
        case ActionVerb::USE_POTION:
            os << "USE_POTION slot=" << static_cast<int>(a0)
               << " target=" << static_cast<int>(a1);
            break;
        case ActionVerb::CHOOSE:
            os << "CHOOSE arg0=" << static_cast<int>(a0);
            // Name the run-layer sentinels, which are the ones a reader cannot
            // decode from the number alone.
            if (a0 == engine::kChooseProceed) os << " (proceed)";
            else if (a0 == engine::kChooseSkipCard) os << " (skip-card)";
            else if (a0 == engine::kChooseSing) os << " (sing)";
            else if (a0 == engine::kChooseBoss) os << " (boss-edge)";
            break;
    }
    return os.str();
}

namespace {

[[nodiscard]] std::string hex64(uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
    return std::string(buf);
}

template <class T>
bool parse_decimal_token(const std::string& token, T& out) {
    if (token.empty()) return false;
    T value{};
    const auto parsed =
        std::from_chars(token.data(), token.data() + token.size(), value, 10);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != token.data() + token.size()) return false;
    out = value;
    return true;
}

bool parse_hex_token(const std::string& token, uint64_t& out) {
    if (token.size() != 16) return false;
    uint64_t value = 0;
    const auto parsed =
        std::from_chars(token.data(), token.data() + token.size(), value, 16);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != token.data() + token.size()) return false;
    out = value;
    return true;
}

}  // namespace

bool write_fuzz_repro(const std::string& path, const ReproFile& r) {
    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os) return false;
    os << "STSFUZZ v1\n";
    os << "seed " << r.id.run_seed << "\n";
    os << "ascension " << static_cast<int>(r.id.ascension) << "\n";
    os << "policy " << policy_name(r.id.policy) << "\n";
    os << "policy_seed " << r.id.policy_seed << "\n";
    if (!r.fail_kind.empty()) {
        os << "fail " << r.fail_kind << "\n";
        os << "fail_step " << r.fail_step << "\n";
    }
    if (r.has_hashes) {
        os << "hash_a " << hex64(r.hash_a) << "\n";
        os << "hash_b " << hex64(r.hash_b) << "\n";
        os << "final_hash " << hex64(r.final_hash) << "\n";
    }
    os << "actions " << r.actions.size() << "\n";
    for (const Action a : r.actions) {
        os << a.bits << "   # " << decode_action(a) << "\n";
    }
    os.flush();
    return static_cast<bool>(os);
}

bool read_fuzz_repro(const std::string& path, ReproFile& out, std::string& error) {
    std::ifstream is(path, std::ios::binary);
    if (!is) {
        error = "cannot open " + path;
        return false;
    }
    ReproFile r;
    std::string line;
    if (!std::getline(is, line)) {
        error = "empty file";
        return false;
    }
    // Tolerate a trailing CR so a file edited on Windows still parses.
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (line != "STSFUZZ v1") {
        error = "bad version line: '" + line + "' (expected 'STSFUZZ v1')";
        return false;
    }

    bool have_count = false;
    std::unordered_set<std::string> seen_keys;
    bool have_hash_a = false;
    bool have_hash_b = false;
    bool have_final_hash = false;
    size_t want = 0;
    while (std::getline(is, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);

        if (have_count) {
            // Body: one Action.bits per line, '#' trailer ignored.
            std::string token;
            uint64_t bits = 0;
            if (!(ls >> token) || !parse_decimal_token(token, bits) ||
                bits > UINT32_MAX) {
                error = "malformed action line: '" + line + "'";
                return false;
            }
            std::string tail;
            if (ls >> tail && !tail.starts_with("#")) {
                error = "trailing data on action line: '" + line + "'";
                return false;
            }
            r.actions.push_back(Action{static_cast<uint32_t>(bits)});
            continue;
        }

        std::string key;
        ls >> key;
        if (!seen_keys.insert(key).second) {
            error = "duplicate key '" + key + "'";
            return false;
        }
        std::string token;
        auto one_token = [&]() -> bool {
            if (!(ls >> token)) return false;
            std::string extra;
            return !(ls >> extra);
        };
        if (key == "seed") {
            if (!one_token() || !parse_decimal_token(token, r.id.run_seed)) {
                error = "bad seed"; return false;
            }
        } else if (key == "ascension") {
            unsigned v = 0;
            if (!one_token() || !parse_decimal_token(token, v) || v > 20) {
                error = "bad ascension"; return false;
            }
            r.id.ascension = static_cast<uint8_t>(v);
        } else if (key == "policy") {
            if (!one_token() || !policy_from_name(token, r.id.policy)) {
                error = "unknown policy '" + token + "'";
                return false;
            }
        } else if (key == "policy_seed") {
            if (!one_token() || !parse_decimal_token(token, r.id.policy_seed)) {
                error = "bad policy_seed"; return false;
            }
        } else if (key == "fail") {
            if (!one_token() || token.empty()) { error = "bad fail"; return false; }
            r.fail_kind = token;
        } else if (key == "fail_step") {
            if (!one_token() || !parse_decimal_token(token, r.fail_step)) {
                error = "bad fail_step"; return false;
            }
        } else if (key == "hash_a" || key == "hash_b" || key == "final_hash") {
            uint64_t v = 0;
            if (!one_token() || !parse_hex_token(token, v)) {
                error = "bad " + key; return false;
            }
            if (key == "hash_a") r.hash_a = v;
            else if (key == "hash_b") r.hash_b = v;
            else r.final_hash = v;
            if (key == "hash_a") have_hash_a = true;
            else if (key == "hash_b") have_hash_b = true;
            else have_final_hash = true;
        } else if (key == "actions") {
            if (!one_token() || !parse_decimal_token(token, want) ||
                want >= UINT32_MAX) {
                error = "bad action count"; return false;
            }
            have_count = true;
            r.actions.reserve(want);
        } else {
            // Unknown key is fatal on purpose (see the header): a v2 field that
            // a v1 reader drops is a reproducer that stops reproducing.
            error = "unknown key '" + key + "'";
            return false;
        }
    }
    if (!have_count) {
        error = "missing 'actions' count";
        return false;
    }
    for (const char* required : {"seed", "ascension", "policy", "policy_seed"}) {
        if (!seen_keys.contains(required)) {
            error = std::string("missing '") + required + "'";
            return false;
        }
    }
    if (have_hash_a != have_hash_b || have_hash_a != have_final_hash) {
        error = "hash_a/hash_b/final_hash must appear together";
        return false;
    }
    if (seen_keys.contains("fail") != seen_keys.contains("fail_step")) {
        error = "fail/fail_step must appear together";
        return false;
    }
    r.has_hashes = have_hash_a;
    if (r.actions.size() != want) {
        error = "action count mismatch: header says " + std::to_string(want) +
                ", body has " + std::to_string(r.actions.size());
        return false;
    }
    out = std::move(r);
    return true;
}

}  // namespace sts::fuzz
