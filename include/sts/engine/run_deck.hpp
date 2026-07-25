#pragma once

// Master-deck editing hooks. Parasite.java:onRemoveFromMasterDeck calls
// AbstractCreature.decreaseMaxHealth(3); this helper makes that effect part of
// the actual removal transaction rather than a combat-only card special case.
// The ADD-side transaction fires every owned relic's onObtainCard in acquisition
// order, so future reward/shop/event card grants route through one door.

#include <cstdint>

#include "sts/engine/cards.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// The relics' onObtainCard pass over a just-appended master-deck row: every owned
// relic, in ACQUISITION order (design §4.3 / stage-a trap 8), gets its
// onObtainCard body run against `card`. Defined in run_deck.cpp, where the
// RelicId -> handler table is GENERATED from registry/relics.yaml `pickup:
// on_obtain_card` -- the per-relic bodies live in per-tier translation units
// under src/engine/relics/, not in this header, so adding a relic with an
// obtain-time effect neither edits a shared switch nor rebuilds this header's
// consumers.
void dispatch_relics_on_obtain_card(RunState& run, CardInstance& card,
                                    const CardDef& def) noexcept;

// Append one card to the master deck and run that pass. Returns false (no
// mutation) when the deck is full or the id has no registry row.
[[nodiscard]] inline bool add_card_to_master_deck(RunState& run, CardId id,
                                                  uint8_t upgrade = 0) noexcept {
    if (run.master_deck_count >= kMasterDeckCap) {
        return false;
    }
    const CardDef* def = card_def(id);
    if (def == nullptr) {
        return false;
    }
    CardInstance& c = run.master_deck[run.master_deck_count];
    c = CardInstance{};
    c.card_id = static_cast<uint16_t>(id);
    c.upgrade = upgrade;
    ++run.master_deck_count;

    dispatch_relics_on_obtain_card(run, c, *def);
    return true;
}

// Removes one master-deck row, preserving order. Returns false for an invalid
// row without changing the run. The generated per-card removal field is zero
// for every poolable curse except Parasite.
[[nodiscard]] inline bool remove_master_deck_card(RunState& run,
                                                   uint16_t index) noexcept {
    if (index >= run.master_deck_count) {
        return false;
    }

    const CardDef* def =
        card_def(static_cast<CardId>(run.master_deck[index].card_id));
    const int loss = def == nullptr ? 0 : def->on_remove_max_hp_loss;

    for (uint16_t i = static_cast<uint16_t>(index + 1);
         i < run.master_deck_count; ++i) {
        run.master_deck[static_cast<uint16_t>(i - 1)] = run.master_deck[i];
    }
    --run.master_deck_count;
    run.master_deck[run.master_deck_count] = CardInstance{};

    if (loss != 0) {
        const int adjusted = static_cast<int>(run.max_hp) - loss;
        run.max_hp = static_cast<int16_t>(adjusted < 1 ? 1 : adjusted);
        if (run.hp > run.max_hp) {
            run.hp = run.max_hp;
        }
    }
    return true;
}

}  // namespace sts::engine
