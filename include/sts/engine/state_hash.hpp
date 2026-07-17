#pragma once

// Deterministic content hash of a state struct (design doc §4.1: "hash = xxh3
// over the struct bytes with padding zero-initialized"). Used by the diff
// harness (design doc §8) and the determinism checks (design doc §2: replay
// twice, compare final-state hashes) to detect any divergence -- including a
// stray write that ASan misses -- as a single 64-bit mismatch.
//
// Implementation: XXH3 64-bit over the raw struct bytes (sizeof the struct).
//
// PRECONDITION (design doc §4.1): the state must have been value-initialized
// (`CombatState s{};` / `RunState s{};`) at construction. CombatState and
// RunState are plain aggregates with no user-declared constructors, so
// value-initialization zero-fills the entire object -- every field AND every
// compiler-inserted padding byte. Only then is a byte hash meaningful: two
// states that are equal in every field but whose padding was left indeterminate
// would hash differently. Callers that build a state any other way (e.g.
// `memcpy` from a wire buffer) must ensure the source bytes originated from a
// value-initialized state. state_test's "two value-initialized states
// hash-equal" case is the standing guard on this precondition.

#include <cstdint>

namespace sts::engine {

struct CombatState;
struct RunState;

// XXH3-64 over the raw bytes of the state. See the precondition above.
[[nodiscard]] uint64_t hash_state(const CombatState& state) noexcept;
[[nodiscard]] uint64_t hash_state(const RunState& state) noexcept;

}  // namespace sts::engine
