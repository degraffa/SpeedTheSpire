#include "sts/fuzz/repro.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

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
    size_t want = 0;
    while (std::getline(is, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);

        if (have_count) {
            // Body: one Action.bits per line, '#' trailer ignored.
            unsigned long long bits = 0;
            if (!(ls >> bits)) {
                error = "malformed action line: '" + line + "'";
                return false;
            }
            r.actions.push_back(Action{static_cast<uint32_t>(bits)});
            continue;
        }

        std::string key;
        ls >> key;
        if (key == "seed") {
            if (!(ls >> r.id.run_seed)) { error = "bad seed"; return false; }
        } else if (key == "ascension") {
            int v = 0;
            if (!(ls >> v) || v < 0 || v > 255) { error = "bad ascension"; return false; }
            r.id.ascension = static_cast<uint8_t>(v);
        } else if (key == "policy") {
            std::string name;
            ls >> name;
            if (!policy_from_name(name, r.id.policy)) {
                error = "unknown policy '" + name + "'";
                return false;
            }
        } else if (key == "policy_seed") {
            if (!(ls >> r.id.policy_seed)) { error = "bad policy_seed"; return false; }
        } else if (key == "fail") {
            ls >> r.fail_kind;
        } else if (key == "fail_step") {
            if (!(ls >> r.fail_step)) { error = "bad fail_step"; return false; }
        } else if (key == "hash_a" || key == "hash_b" || key == "final_hash") {
            std::string hx;
            ls >> hx;
            const uint64_t v = std::strtoull(hx.c_str(), nullptr, 16);
            if (key == "hash_a") r.hash_a = v;
            else if (key == "hash_b") r.hash_b = v;
            else r.final_hash = v;
            r.has_hashes = true;
        } else if (key == "actions") {
            if (!(ls >> want)) { error = "bad action count"; return false; }
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
    if (r.actions.size() != want) {
        error = "action count mismatch: header says " + std::to_string(want) +
                ", body has " + std::to_string(r.actions.size());
        return false;
    }
    out = std::move(r);
    return true;
}

}  // namespace sts::fuzz
