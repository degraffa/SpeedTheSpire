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
    // Waffle.onEquip (Waffle.java:28-31):
    //     player.increaseMaxHp(7, false);
    //     player.heal(player.maxHealth);
    // The `false` does NOT suppress the heal. AbstractCreature.increaseMaxHp(int
    // amount, boolean showEffect) (AbstractCreature.java:199-209) is the only
    // definition of the method in the tree -- AbstractPlayer does not override it
    // -- and its body NEVER READS showEffect: the `this.heal(amount, true)` at
    // :206 is unconditional, as is the TextAboveCreatureEffect at :205. So
    // `increaseMaxHp(7, false)` raises max HP by 7 AND heals 7; the
    // heal(maxHealth) on the next line then tops off whatever remains.
    //
    // The sim's `max_hp += 7; hp = max_hp` reaches that same end state in one
    // step, which is why there is no observable divergence. DO NOT delete the
    // `hp = rs.max_hp` on the strength of the `false` -- the flag is inert, both
    // Java steps heal, and dropping the assignment would leave Waffle granting
    // max HP without the HP.
    //
    // (An earlier version of this comment justified the two separate steps as
    // surviving "a future partial-heal modifier". That rationale was inverted:
    // under such a modifier the Java's increaseMaxHp heal would itself be scaled,
    // while the sim does one raw assignment. The heal here is out-of-combat, so
    // no Magic Flower multiplier applies either way -- MagicFlower.onPlayerHeal
    // only fires while the room phase is COMBAT, MagicFlower.java:32.)
    rs.max_hp = static_cast<int16_t>(rs.max_hp + 7);
    rs.hp = rs.max_hp;
}

}  // namespace sts::engine
