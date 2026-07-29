// SHOP-tier relic pickup bodies -- the out-of-combat overrides declared by
// `pickup:` on the RelicTier.SHOP rows of registry/relics.yaml. See
// relics/relic_pickup.hpp for the three surfaces and the generated dispatch.
//
// No SHOP relic overrides canSpawn: none of the seventeen files defines one, so
// every shop row takes AbstractRelic's `return true` default. That absence is
// itself RNG-relevant -- a spurious gate would change the relicRng draw order --
// so it is recorded here rather than left to be inferred from an empty file.

#include "relic_pickup.hpp"

namespace sts::engine {

// --- onEquip -----------------------------------------------------------------

void relic_on_equip_lees_waffle(RunState& rs, RngStream& /*misc_rng*/,
                                RelicSlot& /*slot*/) noexcept {
    // Waffle.onEquip (Waffle.java:1300-1303):
    //     player.increaseMaxHp(7, false);
    //     player.heal(player.maxHealth);
    // The `false` means increaseMaxHp does NOT heal the gained amount; the
    // heal-to-full immediately after makes that invisible today. The two steps
    // are still written separately so the shape survives a future modifier that
    // can make the heal partial (the run-layer heal has no Magic Flower
    // multiplier -- MagicFlower.onPlayerHeal only fires while the room phase is
    // COMBAT, MagicFlower.java:32).
    rs.max_hp = static_cast<int16_t>(rs.max_hp + 7);
    rs.hp = rs.max_hp;
}

}  // namespace sts::engine
