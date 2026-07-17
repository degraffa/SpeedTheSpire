// XXH3-64 content hash over the raw bytes of a state struct (design doc §4.1).
// See state_hash.hpp for the value-initialization precondition that makes a
// byte hash meaningful (padding must be deterministic/zeroed).
//
// xxHash is pulled in header-only: XXH_INLINE_ALL makes xxhash.h define every
// symbol with internal linkage in this single translation unit, so there is no
// separate library to build or link (the xxhash target is include-only). The
// FetchContent pin lives in the top-level CMakeLists.txt.

#define XXH_INLINE_ALL
#include "xxhash.h"

#include "sts/engine/state_hash.hpp"

#include "sts/engine/combat_state.hpp"
#include "sts/engine/run_state.hpp"

namespace sts::engine {

uint64_t hash_state(const CombatState& state) noexcept {
    return static_cast<uint64_t>(XXH3_64bits(&state, sizeof(state)));
}

uint64_t hash_state(const RunState& state) noexcept {
    return static_cast<uint64_t>(XXH3_64bits(&state, sizeof(state)));
}

}  // namespace sts::engine
