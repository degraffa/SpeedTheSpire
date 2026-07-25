// Artifact -- native power-hook body. One translation unit per power;
// see power_native.hpp for the dispatch plumbing and
// power_artifact.hpp for what this power does.

#include "power_artifact.hpp"

#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// Artifact -- DELIBERATELY EMPTY, not a missing body.
//
// ArtifactPower's only effect is the target-side nullify: ApplyPowerAction
// (ApplyPowerAction.java:106-138) consumes one Artifact stack and drops the
// incoming DEBUFF. That is implemented inline at the APPLY_POWER site, in
// apply_power_blocked_by_artifact (power_hooks.cpp), because it must answer
// "was the debuff blocked?" -- something a queue-only hook body cannot do. So
// Artifact has no source-side / per-power-list hook body, even though its
// registry row is `native: true` and lists on_apply_power (so the row's hook
// inventory still mirrors the Java power).
//
// This empty definition IS the record of that decision. The generated dispatch
// table (STS_REGISTRY_NATIVE_POWERS, expanded in power_hooks.cpp) odr-uses a
// handler for every `native: true` row, so omitting this function would be an
// undefined reference at link time rather than a silent no-op; writing it
// deliberately is how a "native, no handler" row is expressed. Behaviour is
// identical to the old `case PowerId::ARTIFACT: return nullptr;` --
// dispatch_native_hook either skips a null pointer or calls a body that does
// nothing.
void power_native_artifact(CombatState& /*s*/, Hook /*hook*/,
                           const HookContext& /*ctx*/) noexcept {}

}  // namespace sts::engine
