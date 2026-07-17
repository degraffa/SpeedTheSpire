#include "sts/diff/oracle.hpp"

#include <cstring>
#include <utility>

namespace sts::diff {

using engine::Action;
using engine::CombatState;

bool FixtureFileOracleAdapter::load_fixture(const std::string& path) {
    TraceHeader header{};
    std::vector<TraceRecord> records;
    if (!read_trace(path, header, records)) {
        return false;  // unreadable, bad magic, or schema mismatch
    }
    if (records.empty()) {
        return false;  // a fixture must carry at least the initial state
    }
    fixtures_.push_back(Fixture{header.seed, std::move(records)});
    return true;
}

bool FixtureFileOracleAdapter::query(int64_t seed,
                                     std::span<const Action> action_prefix,
                                     CombatState& out) const {
    for (const Fixture& f : fixtures_) {
        if (f.seed != seed) {
            continue;
        }
        // records[k] is the state after k actions; record[0] is the initial
        // state, record[k].action (k>=1) is actions[k-1].bits.
        const std::size_t num_actions = f.records.size() - 1;
        if (action_prefix.size() > num_actions) {
            return false;  // prefix runs past this fixture's recorded sequence
        }
        // Verify the requested prefix matches the recorded actions.
        for (std::size_t j = 0; j < action_prefix.size(); ++j) {
            if (f.records[j + 1].action != action_prefix[j].bits) {
                return false;  // prefix diverges from the recorded fight
            }
        }
        out = f.records[action_prefix.size()].state;
        return true;
    }
    return false;  // no fixture for this seed
}

}  // namespace sts::diff
