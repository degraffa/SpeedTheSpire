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

// --- CommunicationModOracleAdapter (B1.6) -----------------------------------

using engine::RunState;

bool CommunicationModOracleAdapter::load_run_trace(const std::string& path) {
    TraceHeaderV2 header{};
    std::vector<TraceRecordV2> records;
    if (!read_trace_v2(path, header, records)) {
        return false;  // unreadable, bad magic, unknown version, or size mismatch
    }
    if (records.empty()) {
        return false;  // a run trace must carry at least the initial record
    }
    runs_.push_back(Run{header.seed, std::move(records)});
    return true;
}

const TraceRecordV2* CommunicationModOracleAdapter::resolve(
    int64_t seed, std::span<const Action> action_prefix) const {
    for (const Run& run : runs_) {
        if (run.seed != seed) {
            continue;
        }
        // records[k] is the state after k actions; records[0] is the initial
        // state, records[k].action (k>=1) is actions[k-1].bits.
        const std::size_t num_actions = run.records.size() - 1;
        if (action_prefix.size() > num_actions) {
            return nullptr;  // prefix runs past this run's recorded sequence
        }
        for (std::size_t j = 0; j < action_prefix.size(); ++j) {
            if (run.records[j + 1].action != action_prefix[j].bits) {
                return nullptr;  // prefix diverges from the recorded run
            }
        }
        return &run.records[action_prefix.size()];
    }
    return nullptr;  // no run for this seed
}

bool CommunicationModOracleAdapter::query(int64_t seed,
                                          std::span<const Action> action_prefix,
                                          CombatState& out) const {
    const TraceRecordV2* rec = resolve(seed, action_prefix);
    if (rec == nullptr || rec->kind != StateKind::COMBAT) {
        return false;  // no record, or the record here is a RUN record
    }
    out = rec->combat;
    return true;
}

bool CommunicationModOracleAdapter::query_run(int64_t seed,
                                              std::span<const Action> action_prefix,
                                              RunState& out) const {
    const TraceRecordV2* rec = resolve(seed, action_prefix);
    if (rec == nullptr || rec->kind != StateKind::RUN) {
        return false;  // no record, or the record here is a COMBAT record
    }
    out = rec->run;
    return true;
}

}  // namespace sts::diff
