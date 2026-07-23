#pragma once

// Single source of truth for the state-struct / trajectory schema version
// (design doc §8). This constant "must be bumped by any struct edit" -- any
// change to CombatState's or RunState's field layout (a new field, a widened
// type, a reordered group, a changed capacity) is a schema change and this
// number goes up by one.
//
// The trajectory writer stamps this value into each trace file's header
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

// v1 (=1): the Stage-A trajectory container -- a CombatState-only record stream
//   (`{magic 'STS0', schema_version u32, state_size u32, record[]}`).
// v2 (=2): B1.6 adds a per-record `state_kind` discriminator so one container
//   can hold both CombatState and RunState records (design §3.3 run-level
//   traces). The header now advertises BOTH sizeof(CombatState) and
//   sizeof(RunState) for the loader's refusal check. The 20 frozen v1 combat
//   fixtures still load via a compatibility read in the v2 loader (they are
//   NOT regenerated). The struct layouts themselves are unchanged by B1.6
//   (sizeof(CombatState)/sizeof(RunState) identical); the bump reflects the
//   container-format change, per §8's "bumped by any struct/format edit" and
//   design §3.3's "schema-version bump". See tools/diff_harness/sts/diff/trace.hpp.
// v3 (=3): B4.3 additively extends RunState (design §2.6): the NeowEvent rng
//   (14th stream), the three event-pity floats, the shop purge cost, the
//   potion-slot count, the event/shrine/special pool-membership bitsets, and the
//   five relic-pool orders; plus the schema-v2 map reorientation (kMapRows/
//   kMapCols renamed to game-native 15 floors x 7 cols -- a rename only, the 105
//   MapNode layout is byte-identical). sizeof(RunState) grows, so per §8 this is
//   a schema bump. The trace v2 container FORMAT is unchanged (still the B1.6
//   state_kind discriminator + both struct sizes in the header); the bump is
//   carried by the header's stamped version AND the run_state_size refusal check,
//   so an old-sized RunState trace is refused. The 20 frozen v1 combat fixtures
//   are untouched (kTraceFormatV1, zero regeneration); no run-level (RUN/v2)
//   trace goldens are committed, so nothing is regenerated. The on-disk v2 format
//   tag tracks this constant (kTraceFormatV2 == SCHEMA_VERSION).
inline constexpr uint32_t SCHEMA_VERSION = 3;

}  // namespace sts::engine
