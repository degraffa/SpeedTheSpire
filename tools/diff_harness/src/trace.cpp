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
    // v1 writer stamps the v1 format tag (NOT engine::SCHEMA_VERSION, which now
    // advances to v2): write_trace/read_trace remain the exact Stage-A v1 path so
    // the frozen fixtures and existing producers are byte-unaffected by the bump.
    h.schema_version = kTraceFormatV1;
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
    if (h.schema_version != kTraceFormatV1) {
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

// =============================================================================
// Trace format v2 (design §3.3): per-record state_kind, mixed COMBAT/RUN.
// =============================================================================

using engine::RunState;

bool write_trace_v2(const std::string& path, int64_t seed,
                    std::span<const TraceRecordV2> records) {
    if (records.size() > 0xFFFFFFFFull) {
        return false;
    }

    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os) {
        return false;
    }

    TraceHeaderV2 h{};
    std::memcpy(h.magic, kMagic, sizeof(kMagic));
    h.schema_version = kTraceFormatV2;
    h.combat_state_size = static_cast<uint32_t>(sizeof(CombatState));
    h.run_state_size = static_cast<uint32_t>(sizeof(RunState));
    h.record_count = static_cast<uint32_t>(records.size());
    h.reserved = 0;
    h.seed = seed;

    os.write(reinterpret_cast<const char*>(&h), sizeof(h));

    for (const TraceRecordV2& r : records) {
        const uint8_t kind = static_cast<uint8_t>(r.kind);
        os.write(reinterpret_cast<const char*>(&kind), sizeof(kind));
        if (r.kind == StateKind::RUN) {
            os.write(reinterpret_cast<const char*>(&r.run), sizeof(RunState));
        } else {
            os.write(reinterpret_cast<const char*>(&r.combat), sizeof(CombatState));
        }
        os.write(reinterpret_cast<const char*>(&r.action), sizeof(r.action));
        os.write(reinterpret_cast<const char*>(&r.aux), sizeof(r.aux));
    }

    os.flush();
    return static_cast<bool>(os);
}

bool read_trace_v2(const std::string& path, TraceHeaderV2& header_out,
                   std::vector<TraceRecordV2>& records_out) {
    std::ifstream is(path, std::ios::binary);
    if (!is) {
        return false;
    }

    // Common prefix of every trace: magic[4] + schema_version u32.
    char magic[4]{};
    uint32_t version = 0;
    is.read(magic, sizeof(magic));
    is.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!is || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
        return false;
    }

    records_out.clear();

    // --- v1 compatibility read: every record is a COMBAT record. ---
    if (version == kTraceFormatV1) {
        uint32_t state_size = 0;
        uint32_t record_count = 0;
        int64_t seed = 0;
        is.read(reinterpret_cast<char*>(&state_size), sizeof(state_size));
        is.read(reinterpret_cast<char*>(&record_count), sizeof(record_count));
        is.read(reinterpret_cast<char*>(&seed), sizeof(seed));
        if (!is) {
            return false;
        }
        if (state_size != static_cast<uint32_t>(sizeof(CombatState))) {
            return false;  // v1 struct-size mismatch: refuse, do not garbage-load
        }
        records_out.reserve(record_count);
        for (uint32_t i = 0; i < record_count; ++i) {
            TraceRecordV2 r{};
            r.kind = StateKind::COMBAT;
            is.read(reinterpret_cast<char*>(&r.combat), sizeof(CombatState));
            is.read(reinterpret_cast<char*>(&r.action), sizeof(r.action));
            is.read(reinterpret_cast<char*>(&r.aux), sizeof(r.aux));
            if (!is) {
                return false;
            }
            records_out.push_back(r);
        }
        header_out = TraceHeaderV2{};
        std::memcpy(header_out.magic, kMagic, sizeof(kMagic));
        header_out.schema_version = kTraceFormatV1;
        header_out.combat_state_size = state_size;
        header_out.run_state_size = 0;  // v1 had no run records
        header_out.record_count = record_count;
        header_out.reserved = 0;
        header_out.seed = seed;
        return true;
    }

    // --- native v2 read. ---
    if (version == kTraceFormatV2) {
        uint32_t combat_size = 0;
        uint32_t run_size = 0;
        uint32_t record_count = 0;
        uint32_t reserved = 0;
        int64_t seed = 0;
        is.read(reinterpret_cast<char*>(&combat_size), sizeof(combat_size));
        is.read(reinterpret_cast<char*>(&run_size), sizeof(run_size));
        is.read(reinterpret_cast<char*>(&record_count), sizeof(record_count));
        is.read(reinterpret_cast<char*>(&reserved), sizeof(reserved));
        is.read(reinterpret_cast<char*>(&seed), sizeof(seed));
        if (!is) {
            return false;
        }
        // Refuse a container whose stamped struct sizes disagree with this build.
        if (combat_size != static_cast<uint32_t>(sizeof(CombatState)) ||
            run_size != static_cast<uint32_t>(sizeof(RunState))) {
            return false;
        }
        records_out.reserve(record_count);
        for (uint32_t i = 0; i < record_count; ++i) {
            uint8_t kind_byte = 0;
            is.read(reinterpret_cast<char*>(&kind_byte), sizeof(kind_byte));
            if (!is) {
                return false;
            }
            TraceRecordV2 r{};
            if (kind_byte == static_cast<uint8_t>(StateKind::COMBAT)) {
                r.kind = StateKind::COMBAT;
                is.read(reinterpret_cast<char*>(&r.combat), sizeof(CombatState));
            } else if (kind_byte == static_cast<uint8_t>(StateKind::RUN)) {
                r.kind = StateKind::RUN;
                is.read(reinterpret_cast<char*>(&r.run), sizeof(RunState));
            } else {
                return false;  // unknown record kind
            }
            is.read(reinterpret_cast<char*>(&r.action), sizeof(r.action));
            is.read(reinterpret_cast<char*>(&r.aux), sizeof(r.aux));
            if (!is) {
                return false;
            }
            records_out.push_back(r);
        }
        header_out = TraceHeaderV2{};
        std::memcpy(header_out.magic, kMagic, sizeof(kMagic));
        header_out.schema_version = kTraceFormatV2;
        header_out.combat_state_size = combat_size;
        header_out.run_state_size = run_size;
        header_out.record_count = record_count;
        header_out.reserved = reserved;
        header_out.seed = seed;
        return true;
    }

    return false;  // unrecognized container version
}

}  // namespace sts::diff
