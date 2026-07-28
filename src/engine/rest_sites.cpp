#include "sts/engine/rest_sites.hpp"

#include <cstdint>
#include <limits>

#include "sts/engine/cards.hpp"
#include "sts/engine/run_deck.hpp"  // remove_master_deck_card

namespace sts::engine {

namespace {

constexpr uint8_t kNoRelicIndex = 0xFF;

bool has_upgradeable_card(const RunState& rs) noexcept {
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        if (rest_card_upgradeable(rs.master_deck[i])) {
            return true;
        }
    }
    return false;
}

bool has_purgeable_card(const RunState& rs) noexcept {
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        if (rest_card_purgeable(rs.master_deck[i])) {
            return true;
        }
    }
    return false;
}

void push_option(RestMenu& menu, RestOptionKind kind, bool usable,
                 uint8_t relic_index = kNoRelicIndex) noexcept {
    if (menu.count >= kRestOptionCap) {
        return;
    }
    RestOptionEntry& out = menu.entries[menu.count++];
    out.kind = static_cast<uint8_t>(kind);
    out.relic_index = relic_index;
    out.usable = usable;
    out.pad = 0;
}

}  // namespace

int rest_heal_amount(const RunState& rs) noexcept {
    int amount = static_cast<int>(static_cast<float>(rs.max_hp) * 0.3f);
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (static_cast<RelicId>(rs.relics[i].relic_id) ==
            RelicId::REGAL_PILLOW) {
            amount += 15;
            break;
        }
    }
    return amount;
}

bool rest_card_upgradeable(const CardInstance& card) noexcept {
    if (card.card_id == static_cast<uint16_t>(CardId::SEARING_BLOW)) {
        return card.upgrade != std::numeric_limits<uint8_t>::max();
    }
    const CardDef* def = card_def(static_cast<CardId>(card.card_id));
    if (def == nullptr || def->type == CardType::CURSE ||
        def->type == CardType::STATUS) {
        return false;
    }
    return card.upgrade == 0;
}

bool rest_card_purgeable(const CardInstance& card) noexcept {
    // CardGroup.getPurgeableCards (CardGroup.java:978-985) excludes exactly
    // three ids by name: Necronomicurse, CurseOfTheBell, AscendersBane. Two of
    // them are registry rows; Necronomicurse is not an S1 row (Act-2 event
    // relic content). Bottled-card flags are likewise not represented: bottling
    // at acquisition remains explicitly deferred.
    if (card.card_id == static_cast<uint16_t>(CardId::ASCENDERS_BANE) ||
        card.card_id == static_cast<uint16_t>(CardId::CURSE_OF_THE_BELL)) {
        return false;
    }
    return card_def(static_cast<CardId>(card.card_id)) != nullptr;
}

RestMenu build_rest_menu(const RunState& rs) noexcept {
    RestMenu menu{};

    // CampfireUI.initializeButtons starts with these two even when unusable.
    // Fusion Hammer / Coffee Dripper option locks are intentionally not read
    // here: their registered rows remain whole-effect deferrals.
    push_option(menu, RestOptionKind::REST, true);
    push_option(menu, RestOptionKind::SMITH, has_upgradeable_card(rs));

    // AbstractPlayer.relics is acquisition ordered, and addCampfireOption is
    // called in that order.
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        const RelicId id = static_cast<RelicId>(rs.relics[i].relic_id);
        switch (id) {
            case RelicId::GIRYA:
                push_option(menu, RestOptionKind::LIFT,
                            rs.relics[i].counter < 3, i);
                break;
            case RelicId::PEACE_PIPE:
                push_option(menu, RestOptionKind::TOKE,
                            has_purgeable_card(rs), i);
                break;
            case RelicId::SHOVEL:
                push_option(menu, RestOptionKind::DIG, true, i);
                break;
            default:
                break;
        }
    }
    return menu;
}

bool rest_apply_heal(RunState& rs) noexcept {
    const int amount = rest_heal_amount(rs);
    int hp = static_cast<int>(rs.hp) + amount;
    if (hp > rs.max_hp) {
        hp = rs.max_hp;
    }
    rs.hp = static_cast<int16_t>(hp);
    return true;
}

bool rest_upgrade_card(RunState& rs, uint16_t deck_index) noexcept {
    if (deck_index >= rs.master_deck_count ||
        !rest_card_upgradeable(rs.master_deck[deck_index])) {
        return false;
    }
    ++rs.master_deck[deck_index].upgrade;
    return true;
}

bool rest_purge_card(RunState& rs, uint16_t deck_index) noexcept {
    if (deck_index >= rs.master_deck_count ||
        !rest_card_purgeable(rs.master_deck[deck_index])) {
        return false;
    }
    return remove_master_deck_card(rs, deck_index);
}

bool rest_lift(RunState& rs, uint8_t relic_index) noexcept {
    if (relic_index >= rs.relic_count ||
        static_cast<RelicId>(rs.relics[relic_index].relic_id) !=
            RelicId::GIRYA ||
        rs.relics[relic_index].counter >= 3) {
        return false;
    }
    ++rs.relics[relic_index].counter;
    return true;
}

RelicId rest_dig_relic(RunState& rs) noexcept {
    RelicSpawnContext ctx{};
    ctx.floor = rs.floor;
    fill_deck_spawn_gates(rs, ctx);
    fill_campfire_relic_count(rs, ctx);
    fill_boss_spawn_gates(rs, ctx);
    const RelicTier tier = return_random_relic_tier(rs);
    return return_random_relic_key(rs, tier, ctx);
}

}  // namespace sts::engine
