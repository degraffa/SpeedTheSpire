// STARTER-tier relics -- native hook bodies (see relic_native.hpp for the
// split's rationale). Parameters a body does not read are left unnamed to keep
// -Wextra quiet; the signature is the uniform RelicNativeFn.

#include "relics_starter.hpp"

#include "relic_native.hpp"             // heal_player
#include "sts/engine/combat_state.hpp"
#include "sts/engine/run_state.hpp"     // RelicSlot
#include "sts/engine/types.hpp"

namespace sts::engine {

void relic_native_burning_blood(CombatState& s, RelicHook hook,
                                RelicSlot& /*slot*/,
                                const RelicHookContext& /*ctx*/) noexcept {
    // BurningBlood.onVictory (BurningBlood.java:30-37): heal 6 at combat end,
    // clamped to max HP -- BEHIND `if (p.currentHealth > 0)` (:34). An earlier
    // comment here (and the one on Black Blood, which cited this body) claimed
    // the heal was unconditional and that Black Blood's `> 0` test was the odd
    // one out. The re-read says the two relics are the same shape: neither
    // heals a player who is already at zero. The guard is not decorative --
    // AbstractCreature.heal's own `isDying` early-out (:391-393) is a MONSTER
    // gate, since nothing ever sets isDying on the player (the only assignment
    // outside AbstractMonster.die is SpireHeart.java:171), so without :34 a
    // corpse would be healed back up.
    if (hook == RelicHook::ON_VICTORY && s.player_hp > 0) {
        heal_player_with_relics(s, 6);
    }
}

}  // namespace sts::engine
