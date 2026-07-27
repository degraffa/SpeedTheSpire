// The remaining five Exordium event-list bodies. Selection and shared screen
// plumbing live in event_framework.cpp.

#include "sts/engine/event_framework.hpp"

#include <algorithm>
#include <cstdint>
#include <span>

#include "../relics/relic_pickup.hpp"
#include "event_common.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"

namespace sts::engine {

namespace {

// Shared with every other event translation unit; see event_common.hpp.
using events::grid_has_card;
using events::has_upgradable_card;
using events::heal;
using events::one_proceed_menu;

[[nodiscard]] bool owns_relic(const RunState& rs, RelicId id) noexcept {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (rs.relics[i].relic_id == static_cast<uint16_t>(id)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] RelicId draw_screenless_event_relic(RunState& rs) noexcept {
    return events::draw_event_relic(rs, /*screenless=*/true);
}

// Sssserpent.<init>/buttonEffect (Sssserpent.java:41-81), read in full.
// Liars Game ---------------------------------------------------------------

void liars_game_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void liars_game_menu(const RunController& /*rc*/, const EventDialogState& es,
                     EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 0) {
        out.count = 2;
        out.enabled[0] = true;
        out.enabled[1] = true;
    } else {
        one_proceed_menu(out);
    }
}

EventDialogStatus liars_game_choose(RunController& rc, EventDialogState& es,
                                    uint8_t option) {
    if (es.screen == 0) {
        es.screen = option == 0 ? 1 : 3;
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen == 1) {
        // The second Agree page constructs ShowCardAndObtainEffect BEFORE
        // gainGold (Sssserpent.java:69-72). The obtain door consumes Omamori;
        // gain_gold independently observes Ectoplasm.
        (void)add_card_to_master_deck(rc.run, CardId::DOUBT);
        gain_gold(rc.run, rc.run.ascension >= 15 ? 150 : 175);
        es.screen = 2;
        return EventDialogStatus::CONTINUE;
    }
    return EventDialogStatus::FINISHED;
}

constexpr EventDialogImpl kLiarsGame = {
    &liars_game_enter,
    &liars_game_menu,
    &liars_game_choose,
};

// LivingWall.<init>/update/buttonEffect (LivingWall.java:34-104), read in full.
// Living Wall ---------------------------------------------------------------

void living_wall_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void living_wall_menu(const RunController& rc, const EventDialogState& es,
                      EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        return;
    }
    if (es.screen == 0) {
        out.count = 3;
        out.enabled[0] = true;
        out.enabled[1] = true;
        out.enabled[2] = has_upgradable_card(rc.run);
    } else {
        one_proceed_menu(out);
    }
}

EventDialogStatus living_wall_choose(RunController& rc, EventDialogState& es,
                                     uint8_t option) {
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        const auto kind = static_cast<EventGridKind>(es.grid_kind);
        if (kind == EventGridKind::PURGE) {
            (void)event_grid_remove_card(rc.run, es, option);
        } else if (kind == EventGridKind::TRANSFORMABLE) {
            (void)event_grid_transform_card(rc.run, es, rc.combat.misc_rng,
                                            option);
        } else if (kind == EventGridKind::UPGRADE) {
            (void)event_grid_upgrade_card(rc.run, es, option);
        }
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    const EventGridKind kind =
        option == 0 ? EventGridKind::PURGE
                    : option == 1 ? EventGridKind::TRANSFORMABLE
                                  : EventGridKind::UPGRADE;
    es.screen = 1;
    // LivingWall.buttonEffect only opens the grid when its source group is
    // non-empty, but still advances the dialog to RESULT either way
    // (LivingWall.java:68-89).
    if (grid_has_card(rc.run, kind)) {
        open_event_grid(es, kind);
    }
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kLivingWall = {
    &living_wall_enter,
    &living_wall_menu,
    &living_wall_choose,
};

// Mushrooms.<init>/buttonEffect (Mushrooms.java:45-100), read in full.
// Mushrooms -----------------------------------------------------------------

void mushrooms_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void mushrooms_menu(const RunController& /*rc*/, const EventDialogState& es,
                    EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 0) {
        out.count = 2;
        out.enabled[0] = true;
        out.enabled[1] = true;
    } else {
        one_proceed_menu(out);
    }
}

EventDialogStatus mushrooms_choose(RunController& rc, EventDialogState& es,
                                   uint8_t option) {
    if (es.screen == 0) {
        if (option == 0) {
            es.screen = 1;
        } else {
            heal(rc.run, static_cast<int>(rc.run.max_hp) / 4);
            (void)add_card_to_master_deck(rc.run, CardId::PARASITE);
            es.screen = 2;
        }
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen == 2) {
        return EventDialogStatus::FINISHED;
    }

    // The combat-confirm page preloads the EventRoom reward list in this
    // order, before enterCombat: miscRng gold, fixed event relic. The generic
    // battle-over append later adds optional POTION then one CARDS item.
    rc.rewards = RewardScreen{};
    rc.rewards.open_card_item = kNoOpenCardReward;
    const int gold = random(rc.combat.misc_rng, 20, 30);
    (void)add_event_combat_gold_reward(rc.run, rc.rewards, gold);
    const RelicId relic =
        owns_relic(rc.run, RelicId::ODD_MUSHROOM)
            ? RelicId::CIRCLET
            : RelicId::ODD_MUSHROOM;
    (void)add_event_combat_relic_reward(rc.rewards, relic);
    (void)enter_event_combat(rc, "The Mushroom Lair");
    return EventDialogStatus::TRANSITIONED;
}

constexpr EventDialogImpl kMushrooms = {
    &mushrooms_enter,
    &mushrooms_menu,
    &mushrooms_choose,
};

// ScrapOoze.<init>/buttonEffect (ScrapOoze.java:37-96), read in full.
// Scrap Ooze ----------------------------------------------------------------

void scrap_ooze_enter(RunController& rc, EventDialogState& es) {
    es.screen = 0;
    es.scratch0 = 25;  // displayed percentage; threshold gives 26/100 initially
    es.scratch1 = rc.run.ascension >= 15 ? 5 : 3;
}

void scrap_ooze_menu(const RunController& /*rc*/, const EventDialogState& es,
                     EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 0) {
        out.count = 2;
        out.enabled[0] = true;
        out.enabled[1] = true;
    } else {
        one_proceed_menu(out);
    }
}

EventDialogStatus scrap_ooze_choose(RunController& rc, EventDialogState& es,
                                    uint8_t option) {
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 1) {
        es.screen = 1;
        return EventDialogStatus::CONTINUE;
    }

    // Java continues the button body after player.damage, even if it killed
    // the player: damage first, then exactly one miscRng roll, then the
    // success acquisition or fail increments.
    const int chance = es.scratch0;
    const int damage = es.scratch1;
    (void)apply_event_damage(rc, damage,
                             EventDamageOwner::NULL_SOURCE);
    const int roll = random(rc.combat.misc_rng, 0, 99);
    if (roll >= 99 - chance) {
        const RelicId id = draw_screenless_event_relic(rc.run);
        (void)acquire_relic(rc.run, rc.combat.misc_rng, id);
        es.screen = 1;
    } else {
        es.scratch0 = static_cast<int16_t>(chance + 10);
        es.scratch1 = static_cast<int16_t>(damage + 1);
    }
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kScrapOoze = {
    &scrap_ooze_enter,
    &scrap_ooze_menu,
    &scrap_ooze_choose,
};

// ShiningLight.<init>/buttonEffect/upgradeCards
// (ShiningLight.java:45-112), read in full.
// Shining Light --------------------------------------------------------------

void shining_light_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void shining_light_menu(const RunController& rc, const EventDialogState& es,
                        EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 0) {
        out.count = 2;
        out.enabled[0] = has_upgradable_card(rc.run);
        out.enabled[1] = true;
    } else {
        one_proceed_menu(out);
    }
}

EventDialogStatus shining_light_choose(RunController& rc,
                                       EventDialogState& es,
                                       uint8_t option) {
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 1) {
        es.screen = 1;
        return EventDialogStatus::CONTINUE;
    }

    const float fraction = rc.run.ascension >= 15 ? 0.3f : 0.2f;
    const int damage =
        mathutils_round(static_cast<float>(rc.run.max_hp) * fraction);
    (void)apply_event_damage(rc, damage, EventDamageOwner::PLAYER);

    uint16_t candidates[kMasterDeckCap]{};
    uint16_t count = 0;
    EventDialogState probe{};
    open_event_grid(probe, EventGridKind::UPGRADE);
    for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
        if (event_grid_card_legal(rc.run, probe, i)) {
            candidates[count++] = i;
        }
    }
    // Collections.shuffle evaluates miscRng.randomLong() unconditionally,
    // including the one-card case, then upgrades the first two entries.
    JdkRandom jdk(random_long(rc.combat.misc_rng));
    jdk_shuffle(std::span<uint16_t>(candidates, count), jdk);
    const uint16_t take = std::min<uint16_t>(count, 2);
    for (uint16_t i = 0; i < take; ++i) {
        ++rc.run.master_deck[candidates[i]].upgrade;
    }
    es.screen = 1;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kShiningLight = {
    &shining_light_enter,
    &shining_light_menu,
    &shining_light_choose,
};

}  // namespace

const EventDialogImpl* event_native_liars_game() noexcept {
    return &kLiarsGame;
}

const EventDialogImpl* event_native_living_wall() noexcept {
    return &kLivingWall;
}

const EventDialogImpl* event_native_mushrooms() noexcept {
    return &kMushrooms;
}

const EventDialogImpl* event_native_scrap_ooze() noexcept {
    return &kScrapOoze;
}

const EventDialogImpl* event_native_shining_light() noexcept {
    return &kShiningLight;
}

}  // namespace sts::engine
