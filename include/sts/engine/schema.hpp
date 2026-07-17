#pragma once

// Single source of truth for the state-struct / trajectory schema version
// (design doc §8). This constant "must be bumped by any struct edit" -- any
// change to CombatState's or RunState's field layout (a new field, a widened
// type, a reordered group, a changed capacity) is a schema change and this
// number goes up by one.
//
// The trajectory writer (A6.1) stamps this value into each trace file's header
// (`{magic 'STS0', schema_version u32, state_size u32, record[]}`, design doc
// §8); loaders refuse mismatched versions. Both CombatState and RunState expose
// it as a `static constexpr uint32_t kSchemaVersion = SCHEMA_VERSION;` member so
// the value lives in exactly one place (this header) yet is reachable through
// either struct.
//
// It is deliberately NOT a per-instance data field: a stored field would be
// zeroed by the value-initialization every state undergoes (design doc §4.1),
// making it useless as a stamp, and it would waste bytes in every snapshot. The
// stamp belongs to the trajectory container, not to each state.

#include <cstdint>

namespace sts::engine {

inline constexpr uint32_t SCHEMA_VERSION = 1;

}  // namespace sts::engine
