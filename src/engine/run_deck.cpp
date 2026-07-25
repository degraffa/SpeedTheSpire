// The master deck's obtain-time relic pass.
//
// AbstractCard obtain fans out to every owned relic's onObtainCard in ACQUISITION
// order (design §4.3 / stage-a trap 8). The RelicId -> handler table below is
// GENERATED from registry/relics.yaml: every row whose `pickup:` lists
// on_obtain_card contributes one X(<RelicId>, <handler>) entry to
// STS_REGISTRY_RELIC_ON_OBTAIN_CARD (generated relic_table.hpp, reached via
// sts/engine/relics.hpp), and this file expands that list twice -- once for the
// extern declarations, once for the switch -- exactly as relic_pools.cpp does for
// canSpawn/onEquip and relic_hooks.cpp for the combat bodies.
//
// This replaces a hand-written four-case switch that sat INLINE in the public
// run_deck.hpp. Two things follow. First, a relic with an obtain-time effect no
// longer edits a switch shared with every other tier, so tiers can be brought up
// independently. Second, because each handler is odr-used by the switch below, a
// row that lists the surface but whose body nobody wrote is an undefined
// reference at link time rather than a silently skipped case.

#include "sts/engine/run_deck.hpp"

#include "relics/relic_pickup.hpp"  // RelicOnObtainCardSig/Fn
#include "sts/engine/relics.hpp"    // STS_REGISTRY_RELIC_ON_OBTAIN_CARD
#include "sts/engine/types.hpp"

namespace sts::engine {

#define STS_RELIC_ON_OBTAIN_CARD_DECL(ID, FN) extern RelicOnObtainCardSig FN;
STS_REGISTRY_RELIC_ON_OBTAIN_CARD(STS_RELIC_ON_OBTAIN_CARD_DECL)
#undef STS_RELIC_ON_OBTAIN_CARD_DECL

RelicOnObtainCardFn relic_on_obtain_card_fn(RelicId id) noexcept {
    switch (id) {
#define STS_RELIC_ON_OBTAIN_CARD_CASE(ID, FN) \
    case RelicId::ID:                         \
        return &FN;
        STS_REGISTRY_RELIC_ON_OBTAIN_CARD(STS_RELIC_ON_OBTAIN_CARD_CASE)
#undef STS_RELIC_ON_OBTAIN_CARD_CASE
        default:
            return nullptr;  // no onObtainCard override on this row
    }
}

void dispatch_relics_on_obtain_card(RunState& run, CardInstance& card,
                                    const CardDef& def) noexcept {
    for (uint8_t i = 0; i < run.relic_count; ++i) {  // index order == acquisition
        const RelicId rid = static_cast<RelicId>(run.relics[i].relic_id);
        if (rid == RelicId::NONE) {
            continue;
        }
        const RelicOnObtainCardFn fn = relic_on_obtain_card_fn(rid);
        if (fn != nullptr) {
            fn(run, card, def);
        }
    }
}

}  // namespace sts::engine
