#pragma once

// Master-deck editing hooks. Parasite.java:onRemoveFromMasterDeck calls
// AbstractCreature.decreaseMaxHealth(3); this helper makes that effect part of
// the actual removal transaction rather than a combat-only card special case.

#include <cstdint>

#include "sts/engine/cards.hpp"
#include "sts/engine/run_state.hpp"

namespace sts::engine {

// Removes one master-deck row, preserving order. Returns false for an invalid
// row without changing the run. The generated per-card removal field is zero
// for every B3.9 card except Parasite.
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
