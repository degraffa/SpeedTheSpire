// The first six Exordium event bodies. Selection and dialog plumbing live in
// event_framework.cpp; this file owns only the event-specific state machines.

#include "sts/engine/event_framework.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "../relics/relic_pickup.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"

namespace sts::engine {

namespace {

[[nodiscard]] bool owns_relic(const RunState& rs, RelicId id) noexcept {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (rs.relics[i].relic_id == static_cast<uint16_t>(id)) {
            return true;
        }
    }
    return false;
}

void heal(RunState& rs, int amount) noexcept {
    if (amount <= 0 || rs.hp <= 0) {
        return;
    }
    const int next = std::min(static_cast<int>(rs.max_hp),
                              static_cast<int>(rs.hp) + amount);
    rs.hp = static_cast<int16_t>(next);
}

void increase_max_hp(RunState& rs, int amount) noexcept {
    if (amount <= 0) {
        return;
    }
    rs.max_hp = static_cast<int16_t>(static_cast<int>(rs.max_hp) + amount);
    heal(rs, amount);
}

void decrease_max_hp(RunState& rs, int amount) noexcept {
    if (amount <= 0) {
        return;
    }
    const int next = std::max(1, static_cast<int>(rs.max_hp) - amount);
    rs.max_hp = static_cast<int16_t>(next);
    if (rs.hp > rs.max_hp) {
        rs.hp = rs.max_hp;
    }
}

[[nodiscard]] RelicSpawnContext event_relic_context(
    const RunState& rs) noexcept {
    RelicSpawnContext ctx{};
    ctx.floor = rs.floor;
    fill_deck_spawn_gates(rs, ctx);
    fill_campfire_relic_count(rs, ctx);
    fill_boss_spawn_gates(rs, ctx);
    return ctx;
}

[[nodiscard]] RelicId draw_event_relic(RunState& rs,
                                       bool screenless) noexcept {
    const RelicTier tier = return_random_relic_tier(rs);
    const RelicSpawnContext ctx = event_relic_context(rs);
    RelicId id = return_random_relic_key(rs, tier, ctx);
    while (screenless &&
           (id == RelicId::BOTTLED_FLAME ||
            id == RelicId::BOTTLED_LIGHTNING ||
            id == RelicId::BOTTLED_TORNADO ||
            id == RelicId::WHETSTONE)) {
        id = return_random_relic_key(rs, tier, ctx);
    }
    return id;
}

void acquire_event_relic(RunController& rc, bool screenless) noexcept {
    const RelicId id = draw_event_relic(rc.run, screenless);
    (void)acquire_relic(rc.run, rc.combat.misc_rng, id);
}

[[nodiscard]] bool has_purgeable_card(const RunState& rs) noexcept {
    EventDialogState probe{};
    open_event_grid(probe, EventGridKind::PURGE);
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        if (event_grid_card_legal(rs, probe, i)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool card_has_base_damage_at_least(const CardInstance& card,
                                                 int threshold) noexcept {
    const CardDef* def = card_def(static_cast<CardId>(card.card_id));
    if (def == nullptr || def->type != CardType::ATTACK) {
        return false;
    }
    // The original five skeleton rows predate the registry's upgrade dimension
    // and still mirror their base program when upgraded. CardHelper reads the
    // live AbstractCard.baseDamage instead: Bash is 8 -> 10 and Pommel Strike
    // is 9 -> 10. Those are the only threshold-crossing rows in that prefix.
    if (card.upgrade > 0 &&
        (static_cast<CardId>(card.card_id) == CardId::BASH ||
         static_cast<CardId>(card.card_id) == CardId::POMMEL_STRIKE) &&
        threshold <= 10) {
        return true;
    }
    const CardEffectView view = card_effect_steps(*def, card.upgrade);
    for (uint8_t i = 0; i < view.count; ++i) {
        const CardEffectStep& step = view.steps[i];
        const Opcode op = static_cast<Opcode>(step.op);
        switch (op) {
            case Opcode::DAMAGE:
            case Opcode::DAMAGE_STR_MULT:
            case Opcode::DAMAGE_PER_STRIKE:
            case Opcode::DROPKICK:
            case Opcode::DAMAGE_UPGRADE_SCALE:
            case Opcode::DAMAGE_RAMPAGE:
            case Opcode::DAMAGE_FEED:
            case Opcode::FIEND_FIRE:
            case Opcode::VAMPIRE_DAMAGE_ALL:
                if (step.amount >= threshold) {
                    return true;
                }
                break;
            default:
                break;
        }
    }
    return false;
}

[[nodiscard]] bool deck_has_base_damage_at_least(const RunState& rs,
                                                 int threshold) noexcept {
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        if (card_has_base_damage_at_least(rs.master_deck[i], threshold)) {
            return true;
        }
    }
    return false;
}

void one_proceed_menu(EventDialogMenu& out) noexcept {
    out = EventDialogMenu{};
    out.count = 1;
    out.enabled[0] = true;
}

// Big Fish -------------------------------------------------------------------

void big_fish_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void big_fish_menu(const RunController& /*rc*/, const EventDialogState& es,
                   EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 0) {
        out.count = 3;
        out.enabled[0] = true;
        out.enabled[1] = true;
        out.enabled[2] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus big_fish_choose(RunController& rc, EventDialogState& es,
                                  uint8_t option) {
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 0) {
        heal(rc.run, static_cast<int>(rc.run.max_hp) / 3);
    } else if (option == 1) {
        increase_max_hp(rc.run, 5);
    } else {
        (void)add_card_to_master_deck(rc.run, CardId::REGRET);
        acquire_event_relic(rc, /*screenless=*/true);
    }
    es.screen = 1;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kBigFish = {
    &big_fish_enter,
    &big_fish_menu,
    &big_fish_choose,
};

// The Cleric -----------------------------------------------------------------

constexpr int cleric_purify_cost(const RunState& rs) noexcept {
    return rs.ascension >= 15 ? 75 : 50;
}

void cleric_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void cleric_menu(const RunController& rc, const EventDialogState& es,
                 EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        return;
    }
    if (es.screen == 0) {
        out.count = 3;
        out.enabled[0] = rc.run.gold >= 35;
        out.enabled[1] = rc.run.gold >= cleric_purify_cost(rc.run);
        out.enabled[2] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus cleric_choose(RunController& rc, EventDialogState& es,
                                uint8_t option) {
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        if (event_grid_remove_card(rc.run, es, option)) {
            es.screen = 1;
        }
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 0) {
        rc.run.gold -= 35;
        heal(rc.run, static_cast<int>(
                         static_cast<float>(rc.run.max_hp) * 0.25f));
        es.screen = 1;
    } else if (option == 1) {
        if (!has_purgeable_card(rc.run)) {
            es.screen = 1;
            return EventDialogStatus::CONTINUE;
        }
        rc.run.gold -= cleric_purify_cost(rc.run);
        open_event_grid(es, EventGridKind::PURGE);
    } else {
        es.screen = 1;
    }
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kCleric = {
    &cleric_enter,
    &cleric_menu,
    &cleric_choose,
};

// Dead Adventurer ------------------------------------------------------------

enum class DeadReward : uint8_t {
    GOLD = 0,
    NOTHING = 1,
    RELIC = 2,
};

constexpr uint8_t kDeadIntro = 0;
constexpr uint8_t kDeadFight = 1;
constexpr uint8_t kDeadDone = 2;

[[nodiscard]] int dead_reward_count(const EventDialogState& es) noexcept {
    return (static_cast<uint16_t>(es.scratch0) >> 6u) & 0x3u;
}

void set_dead_reward_count(EventDialogState& es, int count) noexcept {
    const uint16_t packed = static_cast<uint16_t>(es.scratch0);
    const uint16_t count_bits = static_cast<uint16_t>(
        static_cast<uint32_t>(static_cast<uint16_t>(count)) << 6u);
    es.scratch0 = static_cast<int16_t>(
        static_cast<uint16_t>((packed & 0x3Fu) | count_bits));
}

[[nodiscard]] DeadReward dead_reward_at(const EventDialogState& es,
                                        int index) noexcept {
    const uint16_t packed = static_cast<uint16_t>(es.scratch0);
    return static_cast<DeadReward>((packed >> (2u * static_cast<unsigned>(index))) &
                                   0x3u);
}

[[nodiscard]] int dead_search_chance(const RunController& rc,
                                     const EventDialogState& es) noexcept {
    const int start = rc.run.ascension >= 15 ? 35 : 25;
    return start + 25 * dead_reward_count(es);
}

void dead_enter(RunController& rc, EventDialogState& es) {
    std::array<DeadReward, 3> rewards = {
        DeadReward::GOLD,
        DeadReward::NOTHING,
        DeadReward::RELIC,
    };
    JdkRandom jr(random_long(rc.combat.misc_rng));
    jdk_shuffle(std::span<DeadReward>(rewards), jr);
    uint16_t packed = 0;
    for (uint16_t i = 0; i < rewards.size(); ++i) {
        packed = static_cast<uint16_t>(
            packed | static_cast<uint16_t>(
                         static_cast<uint16_t>(rewards[i]) << (2u * i)));
    }
    es.scratch0 = static_cast<int16_t>(packed);
    es.scratch1 = static_cast<int16_t>(random(rc.combat.misc_rng, 0, 2));
    es.screen = kDeadIntro;
}

void dead_menu(const RunController& /*rc*/, const EventDialogState& es,
               EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == kDeadIntro) {
        out.count = 2;
        out.enabled[0] = true;
        out.enabled[1] = true;
    } else {
        one_proceed_menu(out);
    }
}

void dead_add_remaining_combat_rewards(RunController& rc,
                                       const EventDialogState& es) noexcept {
    const int first = dead_reward_count(es);
    for (int i = first; i < 3; ++i) {
        switch (dead_reward_at(es, i)) {
            case DeadReward::GOLD:
                (void)add_event_combat_gold_reward(rc.run, rc.rewards, 30);
                break;
            case DeadReward::RELIC:
                (void)add_event_combat_relic_reward(
                    rc.rewards, draw_event_relic(rc.run, /*screenless=*/false));
                break;
            case DeadReward::NOTHING:
            default:
                break;
        }
    }
}

EventDialogStatus dead_choose(RunController& rc, EventDialogState& es,
                              uint8_t option) {
    if (es.screen == kDeadFight) {
        dead_add_remaining_combat_rewards(rc, es);
        constexpr std::string_view encounters[] = {
            "3 Sentries",
            "Gremlin Nob",
            "Lagavulin",
        };
        const int which = std::clamp(static_cast<int>(es.scratch1), 0, 2);
        const EventCombatVariant variant =
            which == 2 ? EventCombatVariant::LAGAVULIN_AWAKE
                       : EventCombatVariant::NONE;
        (void)enter_event_combat(rc, encounters[which], variant);
        rc.event = EventDialogState{};
        return EventDialogStatus::TRANSITIONED;
    }
    if (es.screen == kDeadDone) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 1) {
        es.screen = kDeadDone;
        return EventDialogStatus::CONTINUE;
    }

    if (random(rc.combat.misc_rng, 0, 99) < dead_search_chance(rc, es)) {
        (void)add_event_combat_gold_reward(
            rc.run, rc.rewards, random(rc.combat.misc_rng, 25, 35));
        es.screen = kDeadFight;
        return EventDialogStatus::CONTINUE;
    }

    const int index = dead_reward_count(es);
    set_dead_reward_count(es, index + 1);
    switch (dead_reward_at(es, index)) {
        case DeadReward::GOLD:
            gain_gold(rc.run, 30);
            break;
        case DeadReward::RELIC:
            acquire_event_relic(rc, /*screenless=*/true);
            break;
        case DeadReward::NOTHING:
        default:
            break;
    }
    if (index + 1 == 3) {
        es.screen = kDeadDone;
    }
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kDeadAdventurer = {
    &dead_enter,
    &dead_menu,
    &dead_choose,
};

// Golden Idol ---------------------------------------------------------------

void golden_idol_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void golden_idol_menu(const RunController& /*rc*/, const EventDialogState& es,
                      EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 0) {
        out.count = 2;
        out.enabled[0] = true;
        out.enabled[1] = true;
    } else if (es.screen == 1) {
        out.count = 3;
        out.enabled[0] = true;
        out.enabled[1] = true;
        out.enabled[2] = true;
    } else {
        one_proceed_menu(out);
    }
}

EventDialogStatus golden_idol_choose(RunController& rc, EventDialogState& es,
                                     uint8_t option) {
    if (es.screen == 0) {
        if (option == 1) {
            es.screen = 2;
            return EventDialogStatus::CONTINUE;
        }
        const RelicId id = owns_relic(rc.run, RelicId::GOLDEN_IDOL)
                               ? RelicId::CIRCLET
                               : RelicId::GOLDEN_IDOL;
        (void)acquire_relic(rc.run, rc.combat.misc_rng, id);
        es.screen = 1;
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen == 1) {
        const float damage_percent =
            rc.run.ascension >= 15 ? 0.35f : 0.25f;
        const float max_hp_percent =
            rc.run.ascension >= 15 ? 0.10f : 0.08f;
        if (option == 0) {
            (void)add_card_to_master_deck(rc.run, CardId::INJURY);
        } else if (option == 1) {
            const bool alive = apply_event_damage(
                rc,
                static_cast<int>(
                    static_cast<float>(rc.run.max_hp) * damage_percent),
                EventDamageOwner::NULL_SOURCE);
            if (!alive) {
                return EventDialogStatus::TRANSITIONED;
            }
        } else {
            decrease_max_hp(
                rc.run,
                std::max(1, static_cast<int>(
                                static_cast<float>(rc.run.max_hp) *
                                max_hp_percent)));
        }
        es.screen = 2;
        return rc.phase == static_cast<uint8_t>(RunPhase::RUN_OVER)
                   ? EventDialogStatus::TRANSITIONED
                   : EventDialogStatus::CONTINUE;
    }
    return EventDialogStatus::FINISHED;
}

constexpr EventDialogImpl kGoldenIdol = {
    &golden_idol_enter,
    &golden_idol_menu,
    &golden_idol_choose,
};

// Golden Wing ---------------------------------------------------------------

constexpr uint8_t kWingIntro = 0;
constexpr uint8_t kWingPurgePrompt = 1;
constexpr uint8_t kWingDone = 2;

void golden_wing_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = kWingIntro;
}

void golden_wing_menu(const RunController& rc, const EventDialogState& es,
                      EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        return;
    }
    if (es.screen == kWingIntro) {
        out.count = 3;
        out.enabled[0] = true;
        out.enabled[1] = deck_has_base_damage_at_least(rc.run, 10);
        out.enabled[2] = true;
    } else {
        one_proceed_menu(out);
    }
}

EventDialogStatus golden_wing_choose(RunController& rc, EventDialogState& es,
                                     uint8_t option) {
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        if (event_grid_remove_card(rc.run, es, option)) {
            es.screen = kWingDone;
        }
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen == kWingIntro) {
        if (option == 0) {
            const bool alive =
                apply_event_damage(rc, 7, EventDamageOwner::PLAYER);
            if (!alive) {
                return EventDialogStatus::TRANSITIONED;
            }
            es.screen = kWingPurgePrompt;
            return EventDialogStatus::CONTINUE;
        }
        if (option == 1) {
            gain_gold(rc.run, random(rc.combat.misc_rng, 50, 80));
            es.screen = kWingDone;
            return EventDialogStatus::CONTINUE;
        }
        es.screen = kWingDone;
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen == kWingPurgePrompt) {
        if (has_purgeable_card(rc.run)) {
            open_event_grid(es, EventGridKind::PURGE);
        } else {
            es.screen = kWingDone;
        }
        return EventDialogStatus::CONTINUE;
    }
    return EventDialogStatus::FINISHED;
}

constexpr EventDialogImpl kGoldenWing = {
    &golden_wing_enter,
    &golden_wing_menu,
    &golden_wing_choose,
};

// World of Goop -------------------------------------------------------------

void goop_enter(RunController& rc, EventDialogState& es) {
    const int loss = rc.run.ascension >= 15
                         ? random(rc.combat.misc_rng, 35, 75)
                         : random(rc.combat.misc_rng, 20, 50);
    es.scratch0 = static_cast<int16_t>(
        std::min(static_cast<int>(rc.run.gold), loss));
    es.screen = 0;
}

void goop_menu(const RunController& /*rc*/, const EventDialogState& es,
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

EventDialogStatus goop_choose(RunController& rc, EventDialogState& es,
                              uint8_t option) {
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 0) {
        const bool alive =
            apply_event_damage(rc, 11, EventDamageOwner::PLAYER);
        gain_gold(rc.run, 75);
        if (!alive) {
            return EventDialogStatus::TRANSITIONED;
        }
    } else {
        rc.run.gold -= es.scratch0;
    }
    es.screen = 1;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kWorldOfGoop = {
    &goop_enter,
    &goop_menu,
    &goop_choose,
};

}  // namespace

const EventDialogImpl* event_native_big_fish() noexcept {
    return &kBigFish;
}

const EventDialogImpl* event_native_the_cleric() noexcept {
    return &kCleric;
}

const EventDialogImpl* event_native_dead_adventurer() noexcept {
    return &kDeadAdventurer;
}

const EventDialogImpl* event_native_golden_idol() noexcept {
    return &kGoldenIdol;
}

const EventDialogImpl* event_native_golden_wing() noexcept {
    return &kGoldenWing;
}

const EventDialogImpl* event_native_world_of_goop() noexcept {
    return &kWorldOfGoop;
}

}  // namespace sts::engine
