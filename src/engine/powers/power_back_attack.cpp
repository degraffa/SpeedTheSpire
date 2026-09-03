// Back Attack -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and power_back_attack.hpp for
// what this power does.

#include "power_back_attack.hpp"

#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// Back Attack -- DELIBERATELY EMPTY, not a missing body. The Surrounded /
// Artifact shape (power_surrounded.cpp, power_artifact.cpp): the row is
// `native: true` so the generated dispatch table odr-uses a handler and the
// build, rather than a reviewer, is what notices a missing one.
//
// The reason there is nothing to write here is worth stating precisely, because
// this power LOOKS like it should carry the multiplier and does not. Its class
// overrides no hook (BackAttackPower.java:17-40 is a ctor and
// updateDescription); the 1.5x lives in AbstractMonster and is gated on the LIVE
// applyBackAttack() predicate, never on this power's presence. So a hook body
// here could only ever duplicate a number the damage pipeline already applies --
// and would apply it in the wrong pass, since an atDamageGive hook runs INSIDE
// the float pipeline while the real multiply happens to the already-floored
// output (back_attack.hpp note 3a).
void power_native_back_attack(CombatState& /*s*/, Hook /*hook*/,
                              const HookContext& /*ctx*/) noexcept {}

}  // namespace sts::engine
