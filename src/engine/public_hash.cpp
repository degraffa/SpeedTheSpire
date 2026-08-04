// public_hash -- XXH3-64 over a PublicView's object representation
// (docs/training-plan.md §2.1; task T0.7). The soundness argument for hashing
// raw bytes -- no implicit padding, and an encoder that assigns every byte --
// lives on the declaration in public_view.hpp; read it before changing this.
//
// Its own translation unit rather than a function in public_view.cpp: xxhash.h
// is pulled in header-only with XXH_INLINE_ALL, which defines every xxHash
// symbol with internal linkage in the including TU. state_hash.cpp does the
// same for the engine state hashes, and keeping the two encoders (public_view,
// resample) free of that include keeps the dependency where it is used.

#define XXH_INLINE_ALL
#include "xxhash.h"

#include "sts/engine/public_view.hpp"

#include "sts/engine/run_advance.hpp"

namespace sts::engine {

uint64_t public_hash(const PublicView& view) noexcept {
    return static_cast<uint64_t>(XXH3_64bits(&view, sizeof(view)));
}

uint64_t public_hash(const RunController& rc) noexcept {
    PublicView view{};
    encode_public_view(rc, view);
    return public_hash(view);
}

}  // namespace sts::engine
