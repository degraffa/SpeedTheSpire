#pragma once

// Master-deck editing hooks. Parasite.java:onRemoveFromMasterDeck calls
// AbstractCreature.decreaseMaxHealth(3); this helper makes that effect part of
// the actual removal transaction rather than a combat-only card special case.
// B3.25 adds the ADD-side transaction: obtaining a card fires every owned
// relic's onObtainCard in acquisition order (the egg upgrades + Darkstone
// Periapt), so future reward/shop/event card grants route through one door.

#include <cstdint>

#include "sts/engine/cards.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// Append one card to the master deck and run the relics' onObtainCard pass
// (AbstractCard obtain -> for each player relic r.onObtainCard(card), in
// acquisition order). Returns false (no mutation) when the deck is full or the
// id has no registry row. B3.25 bodies:
//   * MOLTEN_EGG  -- an un-upgraded ATTACK is added upgraded
//     (MoltenEgg2.onObtainCard, MoltenEgg2.java:46-50: canUpgrade && !upgraded).
//   * TOXIC_EGG   -- likewise for a SKILL (ToxicEgg2.java:46-50).
//   * FROZEN_EGG  -- likewise for a POWER (FrozenEgg2.java:46-50); inert until
//     B3.7 lands POWER-type cards (no POWER CardType exists in the registry).
//   * DARKSTONE_PERIAPT -- a CURSE grants +6 max HP, healing the gained amount
//     (DarkstonePeriapt.onObtainCard, DarkstonePeriapt.java:28-36:
//     increaseMaxHp(6, true); color CURSE == type CURSE for every S1 curse row).
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

    for (uint8_t i = 0; i < run.relic_count; ++i) {  // acquisition order (trap 8)
        switch (static_cast<RelicId>(run.relics[i].relic_id)) {
            case RelicId::MOLTEN_EGG:
                if (def->type == CardType::ATTACK && c.upgrade == 0) {
                    c.upgrade = 1;  // MoltenEgg2.java:46-50
                }
                break;
            case RelicId::TOXIC_EGG:
                if (def->type == CardType::SKILL && c.upgrade == 0) {
                    c.upgrade = 1;  // ToxicEgg2.java:46-50
                }
                break;
            case RelicId::FROZEN_EGG:
                // FrozenEgg2.java:46-50 upgrades a POWER-type obtain; no POWER
                // CardType exists until B3.7 -- documented no-op branch.
                break;
            case RelicId::DARKSTONE_PERIAPT:
                if (def->type == CardType::CURSE) {
                    // increaseMaxHp(6, true): +6 max AND +6 current (heals the
                    // gained amount; DarkstonePeriapt.java:34).
                    run.max_hp = static_cast<int16_t>(run.max_hp + 6);
                    run.hp = static_cast<int16_t>(run.hp + 6);
                }
                break;
            default:
                break;
        }
    }
    return true;
}

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
