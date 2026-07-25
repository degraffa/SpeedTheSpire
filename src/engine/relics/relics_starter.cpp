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
    // BurningBlood.onVictory: heal 6 at combat end (clamped to max HP).
    if (hook == RelicHook::ON_VICTORY) {
        heal_player_with_relics(s, 6);
    }
}

}  // namespace sts::engine
