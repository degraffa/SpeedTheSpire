#pragma once

// Sharp Hide -- the Guardian's Defensive-Mode retaliation power.
//
// SharpHidePower.onUseCard (SharpHidePower.java:43-49): when the played card is
// an ATTACK, deal `amount` THORNS-typed damage to the player, owned by the power
// holder. The type is what keeps it honest -- THORNS skips every NORMAL-only
// modifier in the DamageInfo pipeline, so a Vulnerable player is not amplified
// and the Guardian's own Strength does not scale it (interp.hpp DamageType).
//
// Applied by TheGuardian.useCloseUp at 3, or 4 at A19+ (TheGuardian.java:74,
// 185-189), and removed at the end of Twin Slam (:197) -- so it retaliates for
// exactly the Close Up -> Roll Attack -> Twin Slam stretch of the defensive
// cycle. Native rather than a data program because the trigger is conditional on
// the played card's TYPE, the same shape as Rage (registry/powers.yaml id 12).

#include "power_native.hpp"

namespace sts::engine {

extern PowerNativeSig power_native_sharp_hide;

}  // namespace sts::engine
