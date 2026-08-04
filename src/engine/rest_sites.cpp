#include "sts/engine/rest_sites.hpp"

#include <cstdint>
#include <limits>

#include "sts/engine/cards.hpp"
#include "relics/relic_pickup.hpp"  // heal_out_of_combat
#include "sts/engine/relic_pools.hpp"  // master_card_purgeable_unbottled
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

// PeacePipe.java:48: the Toke OPTION's gate is
// `!getGroupWithoutBottledCards(getPurgeableCards()).isEmpty()` -- one
// exclusion stronger than has_purgeable_card above. Folded into
// build_rest_menu at the Wave-C integration (it lived at the run-layer
// consumers as build_rest_menu_with_bottle_gates only while this file was
// the other track's during the wave).
bool has_purgeable_unbottled_card(const RunState& rs) noexcept {
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        if (master_card_purgeable_unbottled(rs.master_deck[i])) {
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

// The canUseCampfireOption veto sweep (CampfireUI.initializeButtons,
// CampfireUI.java:87-93): after the whole button list is built, every button is
// offered to every relic in acquisition order, and the FIRST relic that refuses
// clears that button's `usable`.
//
// Two things about the Java are easy to model wrongly, so they are named here:
//
//   * The relics' own `updateUsability(false)` calls (SmithOption.java:24-27,
//     RestOption.java:43-48) are COSMETIC -- they swap the option's description
//     and image and never touch `usable`. The disable is the `co.usable = false`
//     at the call site, which is why this is a sweep over the built list rather
//     than something the relic does to the option.
//   * The tests are exact-class, not instanceof-plus-subclass: both relics write
//     `option instanceof XOption && option.getClass().getName().equals(
//     XOption.class.getName())` (FusionHammer.java:57-63, CoffeeDripper.java:
//     57-63). With no subclasses in S1 that is the same set, but it is why a
//     kind-equality test is the faithful translation rather than a category one.
//
// Fusion Hammer refuses SmithOption; Coffee Dripper refuses RestOption. Those
// are the complete S1 set: `grep -rn canUseCampfireOption com/` finds overrides
// only in these two relics and in AbstractRelic's `return true` base.
bool campfire_option_vetoed(const RunState& rs, RestOptionKind kind) noexcept {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        switch (static_cast<RelicId>(rs.relics[i].relic_id)) {
            case RelicId::FUSION_HAMMER:
                if (kind == RestOptionKind::SMITH) {
                    return true;
                }
                break;
            case RelicId::COFFEE_DRIPPER:
                if (kind == RestOptionKind::REST) {
                    return true;
                }
                break;
            default:
                break;
        }
    }
    return false;
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
    // three ids by name: Necronomicurse, CurseOfTheBell, AscendersBane. All
    // three are registry rows now that Necronomicon's rider curse has landed,
    // so the mirror is complete. Bottled instances are NOT excluded here on purpose:
    // this is the plain getPurgeableCards mirror, and the surfaces the Java
    // routes through getGroupWithoutBottledCards apply the stronger
    // master_card_purgeable_unbottled (relic_pools.hpp) one level up -- the
    // Toke option gate above, the Toke grid's dispatch guard
    // (run_advance.cpp), the event and shop purge grids.
    if (card.card_id == static_cast<uint16_t>(CardId::ASCENDERS_BANE) ||
        card.card_id == static_cast<uint16_t>(CardId::CURSE_OF_THE_BELL) ||
        card.card_id == static_cast<uint16_t>(CardId::NECRONOMICURSE)) {
        return false;
    }
    return card_def(static_cast<CardId>(card.card_id)) != nullptr;
}

RestMenu build_rest_menu(const RunState& rs) noexcept {
    RestMenu menu{};

    // CampfireUI.initializeButtons starts with these two even when unusable.
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
                // The bottled exclusion is the OPTION's own gate
                // (PeacePipe.java:48), not a veto -- see
                // has_purgeable_unbottled_card above.
                push_option(menu, RestOptionKind::TOKE,
                            has_purgeable_unbottled_card(rs), i);
                break;
            case RelicId::SHOVEL:
                push_option(menu, RestOptionKind::DIG, true, i);
                break;
            default:
                break;
        }
    }

    // The veto sweep runs LAST, over EVERY button including the relic-added
    // ones (CampfireUI.java:87-93). No S1 relic refuses a LIFT/TOKE/DIG option,
    // but sweeping the whole list rather than only the base two is the shape the
    // Java has, and it is what keeps a future refusing relic correct.
    //
    // A veto only ever CLEARS `usable` -- it never sets it -- so an option that
    // was already unusable for its own reason (no upgradeable card, Girya at 3)
    // stays unusable and is not double-counted.
    for (uint8_t i = 0; i < menu.count; ++i) {
        if (menu.entries[i].usable &&
            campfire_option_vetoed(
                rs, static_cast<RestOptionKind>(menu.entries[i].kind))) {
            menu.entries[i].usable = false;
        }
    }
    // RecallOption (CampfireUI.java:94-96) is appended AFTER the sweep, so it
    // is never vetoed and always usable. The gate is
    // `Settings.isFinalActAvailable && !Settings.hasRubyKey`:
    // isFinalActAvailable is the profile constant (kFinalActAvailable,
    // rest_sites.hpp) and hasRubyKey is the run's kKeyRuby bit. NOTE the
    // ordering consequence: the cannotProceed check that auto-completes an
    // all-unusable campfire (CampfireUI.java:97-104, applied at rest-room
    // entry in run_advance.cpp) runs AFTER this append, so a
    // boss-relic-locked campfire stays OPEN while the Ruby Key is still on
    // offer.
    if (kFinalActAvailable && (rs.keys & kKeyRuby) == 0u) {
        push_option(menu, RestOptionKind::RECALL, true);
    }
    return menu;
}

bool rest_menu_has_usable_option(const RestMenu& menu) noexcept {
    for (uint8_t i = 0; i < menu.count; ++i) {
        if (menu.entries[i].usable) {
            return true;
        }
    }
    return false;
}

bool rest_apply_heal(RunState& rs) noexcept {
    // The REST option's heal is out of combat by construction, so it goes
    // through the shared heal_out_of_combat door (relics/relic_pickup.hpp),
    // which owns the fan-out derivation (Magic Flower COMBAT-gated, powers
    // cleared at the room boundary) -- Wave-C integration consolidated the
    // three hand-spelled copies of this clamp into that one door.
    heal_out_of_combat(rs, rest_heal_amount(rs));
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
