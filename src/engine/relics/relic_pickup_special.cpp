// SPECIAL-tier relic pickup bodies -- the out-of-combat overrides declared by
// `pickup:` on the RelicTier.SPECIAL rows of registry/relics.yaml. See
// relics/relic_pickup.hpp for the three surfaces and the generated dispatch.
//
// NO SPECIAL relic overrides canSpawn, and that absence is load-bearing rather
// than incidental: a SPECIAL relic is in no shuffled dungeon pool at all
// (relic_pools.cpp's `default: -1`), so it is never drawn and never asked. A
// spurious gate here would be dead code; a spurious POOL membership would move
// the relicRng draw order. Recorded so the emptiness is not read as an omission.

#include "relic_pickup.hpp"

#include "sts/engine/run_deck.hpp"  // add_card_to_master_deck (the obtain door)

namespace sts::engine {

// --- onEquip -----------------------------------------------------------------

void relic_on_equip_necronomicon(RunState& rs, RngStream& /*misc_rng*/,
                                 RelicSlot& /*slot*/) noexcept {
    // Necronomicon.onEquip (Necronomicon.java:39-45): the sound, a description
    // rewrite, UnlockTracker.markCardAsSeen -- all presentation -- and the one
    // line that matters,
    //     AbstractDungeon.effectList.add(
    //         new ShowCardAndObtainEffect(new Necronomicurse(), ...));
    //
    // That effect is the ordinary master-deck obtain door, not a bespoke path.
    // Its constructor (ShowCardAndObtainEffect.java:30-45) checks Omamori FIRST
    // and, for a CURSE-coloured card with a charge left, spends the charge and
    // marks itself done in the constructor -- so update() never runs and the
    // curse never reaches the deck. Otherwise update() (:94-109) fires every
    // relic's onObtainCard, appends through souls.obtain (CardGroup.addToTop is
    // an APPEND, CardGroup.java:455-457), then fires onMasterDeckChange.
    // add_card_to_master_deck is exactly that sequence, Omamori door included
    // (the Calling Bell precedent in relic_pickup_boss.cpp routes the same way).
    //
    // The rider is MANDATORY and carries no choice: unlike Calling Bell there is
    // no confirmation grid, so this is a plain 3-argument onEquip rather than an
    // on_equip_screen.
    //
    // No RNG: neither the effect nor the append draws.
    //
    // The relic's onUnequip (:47-58) -- remove the first Necronomicurse from the
    // master deck -- has NO counterpart here because it is unreachable: the only
    // relic-removal paths in the run layer act on a BOSS-tier slot (Neow's swap,
    // the boss-relic swap) and this relic is SPECIAL. It becomes reachable only
    // if some later stage adds a general relic-removal surface.
    (void)add_card_to_master_deck(rs, CardId::NECRONOMICURSE);
}

}  // namespace sts::engine
