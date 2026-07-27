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

// AbstractRelic.onMasterDeckChange, fired from every master-deck edit. Du-Vu
// Doll is the only S1 override (DuVuDoll.java:321-332): its counter is the
// number of CURSE-type cards in the master deck, RECOMPUTED from scratch each
// time rather than adjusted by the delta -- which is what the Java does, and is
// also what keeps a RunState assembled by hand (tests, the fixture loader)
// consistent without a separate seeding step. Inline rather than a generated
// dispatch surface: one relic does not justify a fourth pickup table, and the
// row's `pickup: on_equip` already covers the acquisition-time half.
inline void dispatch_relics_on_master_deck_change(RunState& run) noexcept {
    int16_t curses = 0;
    for (uint16_t i = 0; i < run.master_deck_count; ++i) {
        const CardDef* def =
            card_def(static_cast<CardId>(run.master_deck[i].card_id));
        if (def != nullptr && def->type == CardType::CURSE) {
            ++curses;
        }
    }
    for (uint8_t i = 0; i < run.relic_count; ++i) {
        if (run.relics[i].relic_id ==
            static_cast<uint16_t>(RelicId::DU_VU_DOLL)) {
            run.relics[i].counter = curses;
        }
    }
}

// ShowCardAndObtainEffect's acquisition door: curses are first offered to the
// first owned Omamori, whose nonzero counter consumes the card; otherwise the
// card is appended and the obtain/master-deck-change passes run. The Omamori
// check precedes the append exactly as ShowCardAndObtainEffect.<init>
// (ShowCardAndObtainEffect.java:30-36) precedes its update-time obtain
// (:72-82). Returns false only when an unblocked card cannot be appended.
[[nodiscard]] inline bool add_card_to_master_deck(RunState& run, CardId id,
                                                  uint8_t upgrade = 0) noexcept {
    const CardDef* def = card_def(id);
    if (def == nullptr) {
        return false;
    }
    if (def->type == CardType::CURSE) {
        for (uint8_t i = 0; i < run.relic_count; ++i) {
            RelicSlot& slot = run.relics[i];
            if (slot.relic_id == static_cast<uint16_t>(RelicId::OMAMORI)) {
                if (slot.counter != 0) {
                    --slot.counter;
                    return true;
                }
                break;  // getRelic returns the first copy, even when used up.
            }
        }
    }
    if (run.master_deck_count >= kMasterDeckCap) {
        return false;
    }
    CardInstance& c = run.master_deck[run.master_deck_count];
    c = CardInstance{};
    c.card_id = static_cast<uint16_t>(id);
    c.upgrade = upgrade;
    ++run.master_deck_count;

    dispatch_relics_on_obtain_card(run, c, *def);
    dispatch_relics_on_master_deck_change(run);
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
    dispatch_relics_on_master_deck_change(run);
    return true;
}

}  // namespace sts::engine
