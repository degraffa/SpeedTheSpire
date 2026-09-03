// Surrounded -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and power_surrounded.hpp for
// what this power does.

#include "power_surrounded.hpp"

#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// Surrounded -- DELIBERATELY EMPTY, not a missing body.
//
// SurroundedPower (SurroundedPower.java:11-29) has NO hook overrides: the class
// is a ctor that sets name/ID/owner/`amount = -1`/loadRegion, plus
// updateDescription. Nothing about it is ever called back.
//
// It is nevertheless registered `native: true` rather than left as an inert
// identity row, and the difference is a link error versus a shrug. The generated
// STS_REGISTRY_NATIVE_POWERS table odr-uses a handler for every native row, so
// this file's existence is what the build checks; an identity row with no flag
// would have compiled equally well the day someone deleted the real reader by
// accident. And there IS a real reader -- the whole back-attack mechanic
// (back_attack.hpp) is gated on `player.hasPower("Surrounded")` -- so "this power
// does nothing" would be the wrong sentence to leave in the tree. What is true is
// narrower: it responds to no HOOK, because it is read by a predicate rather than
// dispatched to. The Artifact precedent (power_artifact.cpp) is the same shape
// for a different reason.
//
// Behaviour is identical to `case PowerId::SURROUNDED: return nullptr;` --
// dispatch_native_hook either skips a null pointer or calls a body that does
// nothing.
void power_native_surrounded(CombatState& /*s*/, Hook /*hook*/,
                             const HookContext& /*ctx*/) noexcept {}

}  // namespace sts::engine
