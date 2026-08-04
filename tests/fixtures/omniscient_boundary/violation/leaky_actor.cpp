// FIXTURE, not a compiled translation unit. It is the negative control for
// tools/check_omniscient_boundary.sh: a training-facing file that reaches the
// omniscient (full-state) observation surface, which the check must reject with
// a non-zero exit. Two distinct spellings appear on purpose -- the include and
// the encoder call -- so the check cannot pass by matching only one of them.
//
// Deliberately NOT carrying the `omniscient-boundary-ok` hatch: this is the
// leak, not prose about it.

#include "sts/engine/omniscient_observation.hpp"

namespace {

int peek(const sts::engine::CombatState& state) {
    sts::engine::OmniscientObsBuffer obs{};
    sts::engine::omniscient_encode_observation(state, obs);
    return obs.monsters[0].intent;  // the true telegraphed move: a leak
}

}  // namespace
