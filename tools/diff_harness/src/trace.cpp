#include "sts/diff/trace.hpp"

#include <cstring>
#include <fstream>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/schema.hpp"

namespace sts::diff {

using engine::Action;
using engine::CombatState;

namespace {

constexpr char kMagic[4] = {'S', 'T', 'S', '0'};

// aux for a state: bit 0 set iff the combat is over (design doc §8 "aux").
uint32_t aux_for(const CombatState& s) noexcept {
    return (s.phase == static_cast<uint8_t>(engine::CombatPhase::COMBAT_OVER))
               ? kAuxTerminal
               : 0u;
}

}  // namespace

bool write_trace(const std::string& path, int64_t seed,
                 std::span<const Action> actions,
                 std::span<const CombatState> states) {
    // Record convention: states[0] is the initial state, states[k] follows
    // actions[k-1] -> exactly one more state than actions.
    if (states.size() != actions.size() + 1) {
        return false;
    }
    if (states.size() > 0xFFFFFFFFull) {
        return false;
    }

    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os) {
        return false;
    }

    TraceHeader h{};
    std::memcpy(h.magic, kMagic, sizeof(kMagic));
    h.schema_version = engine::SCHEMA_VERSION;
    h.state_size = static_cast<uint32_t>(sizeof(CombatState));
    h.record_count = static_cast<uint32_t>(states.size());
    h.seed = seed;

    os.write(reinterpret_cast<const char*>(&h), sizeof(h));

    for (std::size_t i = 0; i < states.size(); ++i) {
        const CombatState& st = states[i];
        // action that PRODUCED this state: 0 for the initial record, else the
        // (i-1)'th action's bits.
        const uint32_t action_bits = (i == 0) ? 0u : actions[i - 1].bits;
        const uint32_t aux = aux_for(st);

        os.write(reinterpret_cast<const char*>(&st), sizeof(CombatState));
        os.write(reinterpret_cast<const char*>(&action_bits), sizeof(action_bits));
        os.write(reinterpret_cast<const char*>(&aux), sizeof(aux));
    }

    os.flush();
    return static_cast<bool>(os);
}

bool read_trace(const std::string& path, TraceHeader& header_out,
                std::vector<TraceRecord>& records_out) {
    std::ifstream is(path, std::ios::binary);
    if (!is) {
        return false;
    }

    TraceHeader h{};
    is.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (!is || is.gcount() != static_cast<std::streamsize>(sizeof(h))) {
        return false;
    }

    // Hard checks -- refuse a mismatched trace cleanly rather than loading
    // garbage (design doc §8: "loaders refuse mismatched schema_version").
    if (std::memcmp(h.magic, kMagic, sizeof(kMagic)) != 0) {
        return false;
    }
    if (h.schema_version != engine::SCHEMA_VERSION) {
        return false;
    }
    if (h.state_size != static_cast<uint32_t>(sizeof(CombatState))) {
        return false;
    }

    records_out.clear();
    records_out.reserve(h.record_count);
    for (uint32_t i = 0; i < h.record_count; ++i) {
        TraceRecord r{};
        is.read(reinterpret_cast<char*>(&r.state), sizeof(CombatState));
        is.read(reinterpret_cast<char*>(&r.action), sizeof(r.action));
        is.read(reinterpret_cast<char*>(&r.aux), sizeof(r.aux));
        if (!is) {
            return false;
        }
        records_out.push_back(r);
    }

    header_out = h;
    return true;
}

}  // namespace sts::diff
