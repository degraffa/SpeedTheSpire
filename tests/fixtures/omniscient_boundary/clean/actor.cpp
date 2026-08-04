// FIXTURE, not a compiled translation unit. It stands in for a training-facing
// actor that stays on the public side of the T0.7 information boundary:
// everything it reads comes from PublicView, and it identifies a state by
// public_hash(). The boundary check must report this directory clean; the
// test that runs it lives beside the other fixtures' one.

#include "sts/engine/public_view.hpp"

namespace {

uint64_t choose(const sts::engine::RunController& rc) {
    sts::engine::PublicView view{};
    sts::engine::encode_public_view(rc, view);
    // The legality channel rides inside the view (PvMask), so an actor never
    // needs a second, unhashed source for it.
    return sts::engine::public_hash(view);
}

}  // namespace
