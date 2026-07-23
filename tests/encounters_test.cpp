// B3.12 encounter framework tests: composition resolution (miscRng) vs
// hand-derived draws, monsterHpRng spawn-order HP rolls, the multi-monster
// legal-action target grid over dead/alive slots, and the monsterRng pool-draw
// list generation (structural invariants + determinism + exclusions).
//
// The composition tests are DIFFERENTIAL: for each encounter and each seed, a
// from-scratch hand-derivation replays the exact miscRng draw sequence read from
// the decompiled MonsterHelper (getLouse/getSlaver eager list-build coin BEFORE
// the random(0,n) select; draw-without-replacement pools; seq/coin picks), and
// the resolver must match both the produced spawn order AND the number of draws
// consumed. This pins the draw ORDER, not just the outcome.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "sts/engine/advance.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_jaw_worm.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

using namespace sts::engine;
using sv = std::string_view;

namespace {

// Monster game_id join keys (the game's AbstractMonster.ID strings).
constexpr sv kNormal = "FuzzyLouseNormal";
constexpr sv kDef = "FuzzyLouseDefensive";
constexpr sv kSpikeS = "SpikeSlime_S";
constexpr sv kAcidS = "AcidSlime_S";
constexpr sv kSpikeM = "SpikeSlime_M";
constexpr sv kAcidM = "AcidSlime_M";
constexpr sv kSpikeL = "SpikeSlime_L";
constexpr sv kAcidL = "AcidSlime_L";
constexpr sv kCultist = "Cultist";
constexpr sv kLooter = "Looter";
constexpr sv kSlaverRed = "SlaverRed";
constexpr sv kSlaverBlue = "SlaverBlue";
constexpr sv kFungi = "FungiBeast";
constexpr sv kJawWorm = "JawWorm";

std::vector<sv> members(const ResolvedGroup& g) {
    return std::vector<sv>(g.members.begin(), g.members.begin() + g.count);
}

// Resolve `key` from a fresh stream at `seed`; report the draw count consumed.
ResolvedGroup resolve_at(sv key, int64_t seed, int32_t& draws) {
    RngStream misc = from_seed(seed);
    ResolvedGroup g{};
    EXPECT_TRUE(resolve_encounter(key, misc, g)) << key;
    draws = misc.counter;
    return g;
}

// Draw-without-replacement reference (matches spawnGremlins / spawnManySmallSlimes:
// random(pool.size()-1) then ArrayList.remove(index)).
std::vector<sv> pool_reference(RngStream& ref, std::vector<sv> pool, int n) {
    std::vector<sv> out;
    for (int k = 0; k < n; ++k) {
        const int32_t idx = random(ref, static_cast<int32_t>(pool.size()) - 1);
        out.push_back(pool[static_cast<std::size_t>(idx)]);
        pool.erase(pool.begin() + idx);
    }
    return out;
}

}  // namespace

// --- 1. Composition resolution (miscRng) ------------------------------------

TEST(Encounters, FixedMonsterCompositionsConsumeNoDraws) {
    struct Case { sv key; std::vector<sv> members; };
    const Case cases[] = {
        {"Cultist", {kCultist}},
        {"Jaw Worm", {kJawWorm}},
        {"Blue Slaver", {kSlaverBlue}},
        {"Red Slaver", {kSlaverRed}},
        {"Looter", {kLooter}},
        {"2 Fungi Beasts", {kFungi, kFungi}},
        {"Gremlin Nob", {"GremlinNob"}},
        {"Lagavulin", {"Lagavulin"}},
        {"3 Sentries", {"Sentry", "Sentry", "Sentry"}},
        {"The Guardian", {"TheGuardian"}},
        {"Hexaghost", {"Hexaghost"}},
        {"Slime Boss", {"SlimeBoss"}},
    };
    for (const auto& c : cases) {
        for (int64_t seed = 0; seed < 8; ++seed) {
            int32_t draws = -1;
            const ResolvedGroup g = resolve_at(c.key, seed, draws);
            EXPECT_EQ(draws, 0) << c.key << " must consume no miscRng draws";
            EXPECT_EQ(members(g), c.members) << c.key;
        }
    }
}

TEST(Encounters, LouseVariantsMatchHandDerivedBooleans) {
    // 2 Louse / 3 Louse = getLouse x N, each one randomBoolean (true -> Normal).
    for (int64_t seed = 0; seed < 40; ++seed) {
        for (int n : {2, 3}) {
            RngStream ref = from_seed(seed);
            std::vector<sv> exp;
            for (int i = 0; i < n; ++i) exp.push_back(random_boolean(ref) ? kNormal : kDef);
            int32_t draws = -1;
            const ResolvedGroup g =
                resolve_at(n == 2 ? sv("2 Louse") : sv("3 Louse"), seed, draws);
            EXPECT_EQ(members(g), exp) << "n=" << n << " seed=" << seed;
            EXPECT_EQ(draws, n) << "n=" << n;
            EXPECT_EQ(ref.counter, n);
        }
    }
}

TEST(Encounters, SmallSlimesAndLargeSlimePicks) {
    bool saw_true = false, saw_false = false;
    for (int64_t seed = 0; seed < 40; ++seed) {
        // Small Slimes: 1 randomBoolean picks a whole sequence.
        {
            RngStream ref = from_seed(seed);
            const bool b = random_boolean(ref);
            std::vector<sv> exp = b ? std::vector<sv>{kSpikeS, kAcidM}
                                    : std::vector<sv>{kAcidS, kSpikeM};
            int32_t draws = -1;
            const ResolvedGroup g = resolve_at("Small Slimes", seed, draws);
            EXPECT_EQ(members(g), exp) << "seed=" << seed;
            EXPECT_EQ(draws, 1);
            b ? saw_true = true : saw_false = true;
        }
        // Large Slime: 1 randomBoolean (true -> Acid_L).
        {
            RngStream ref = from_seed(seed);
            const bool b = random_boolean(ref);
            int32_t draws = -1;
            const ResolvedGroup g = resolve_at("Large Slime", seed, draws);
            EXPECT_EQ(members(g), std::vector<sv>{b ? kAcidL : kSpikeL});
            EXPECT_EQ(draws, 1);
        }
    }
    EXPECT_TRUE(saw_true && saw_false) << "seeds should exercise both slime mixes";
}

TEST(Encounters, GremlinGangDrawWithoutReplacementOrder) {
    const std::vector<sv> gremlins = {"GremlinWarrior", "GremlinWarrior",
                                      "GremlinThief",   "GremlinThief",
                                      "GremlinFat",     "GremlinFat",
                                      "GremlinTsundere", "GremlinWizard"};
    for (int64_t seed = 0; seed < 40; ++seed) {
        RngStream ref = from_seed(seed);
        const std::vector<sv> exp = pool_reference(ref, gremlins, 4);
        int32_t draws = -1;
        const ResolvedGroup g = resolve_at("Gremlin Gang", seed, draws);
        EXPECT_EQ(members(g), exp) << "seed=" << seed;
        EXPECT_EQ(draws, 4);
        EXPECT_EQ(ref.counter, 4);
    }
}

TEST(Encounters, LotsOfSlimesDrawWithoutReplacementOrder) {
    const std::vector<sv> slimes = {kSpikeS, kSpikeS, kSpikeS, kAcidS, kAcidS};
    for (int64_t seed = 0; seed < 40; ++seed) {
        RngStream ref = from_seed(seed);
        const std::vector<sv> exp = pool_reference(ref, slimes, 5);
        int32_t draws = -1;
        const ResolvedGroup g = resolve_at("Lots of Slimes", seed, draws);
        EXPECT_EQ(members(g), exp) << "seed=" << seed;
        EXPECT_EQ(draws, 5);
    }
}

TEST(Encounters, ExordiumThugsEagerPickOrder) {
    // [weakWildlife, strongHumanoid]. weakWildlife: getLouse coin (list build)
    // THEN random(0,2) select over [louse, SpikeSlime_M, AcidSlime_M].
    // strongHumanoid: getSlaver coin (list build) THEN random(0,2) over
    // [Cultist, slaver, Looter]. 4 draws total, in that order.
    for (int64_t seed = 0; seed < 60; ++seed) {
        RngStream ref = from_seed(seed);
        const bool bl = random_boolean(ref);
        const sv louse = bl ? kNormal : kDef;
        const int32_t s1 = random(ref, 0, 2);
        const sv weak = (s1 == 0) ? louse : (s1 == 1) ? kSpikeM : kAcidM;
        const bool bs = random_boolean(ref);
        const sv slaver = bs ? kSlaverRed : kSlaverBlue;
        const int32_t s2 = random(ref, 0, 2);
        const sv strong = (s2 == 0) ? kCultist : (s2 == 1) ? slaver : kLooter;

        int32_t draws = -1;
        const ResolvedGroup g = resolve_at("Exordium Thugs", seed, draws);
        EXPECT_EQ(members(g), (std::vector<sv>{weak, strong})) << "seed=" << seed;
        EXPECT_EQ(draws, 4);
        EXPECT_EQ(ref.counter, 4);
    }
}

TEST(Encounters, ExordiumWildlifeEagerPickOrder) {
    // [strongWildlife, weakWildlife]. strongWildlife: random(0,1) over
    // [FungiBeast, JawWorm] (no coin). weakWildlife: getLouse coin THEN
    // random(0,2). 3 draws total.
    for (int64_t seed = 0; seed < 60; ++seed) {
        RngStream ref = from_seed(seed);
        const int32_t s1 = random(ref, 0, 1);
        const sv strong = (s1 == 0) ? kFungi : kJawWorm;
        const bool bl = random_boolean(ref);
        const sv louse = bl ? kNormal : kDef;
        const int32_t s2 = random(ref, 0, 2);
        const sv weak = (s2 == 0) ? louse : (s2 == 1) ? kSpikeM : kAcidM;

        int32_t draws = -1;
        const ResolvedGroup g = resolve_at("Exordium Wildlife", seed, draws);
        EXPECT_EQ(members(g), (std::vector<sv>{strong, weak})) << "seed=" << seed;
        EXPECT_EQ(draws, 3);
        EXPECT_EQ(ref.counter, 3);
    }
}

TEST(Encounters, ResolveUnknownEncounterFails) {
    RngStream misc = from_seed(0);
    ResolvedGroup g{};
    EXPECT_FALSE(resolve_encounter("Not An Encounter", misc, g));
    EXPECT_EQ(misc.counter, 0);  // a failed lookup consumes nothing
}

// --- 2. Spawn: HP rolls consume monster_hp_rng in spawn order ---------------

TEST(Encounters, SpawnGroupHpRollsInSpawnOrder) {
    for (int64_t seed = 1; seed < 20; ++seed) {
        CombatState s{};
        s.monster_hp_rng = from_seed(seed);
        s.ai_rng = from_seed(seed + 777);

        const MonsterId group[] = {MonsterId::JAW_WORM, MonsterId::JAW_WORM,
                                   MonsterId::JAW_WORM};
        spawn_group(s, group);

        ASSERT_EQ(s.monster_count, 3u);
        // Reference: monster_hp_rng draws HP for slot 0,1,2 IN ORDER.
        RngStream hp_ref = from_seed(seed);
        for (int i = 0; i < 3; ++i) {
            const int32_t expected =
                random(hp_ref, kJawWormHpMin, kJawWormHpMax);
            EXPECT_EQ(s.monsters[i].hp, expected) << "slot " << i;
            EXPECT_EQ(s.monsters[i].max_hp, expected) << "slot " << i;
            EXPECT_EQ(s.monsters[i].move_history[0], kMoveChomp) << "slot " << i;
        }
        // Both streams advanced by exactly 3 (one HP roll + one rollMove each).
        EXPECT_EQ(s.monster_hp_rng.counter, 3);
        EXPECT_EQ(s.ai_rng.counter, 3);
    }
}

// --- 3. Multi-monster legal-action target grid over dead/alive slots --------

TEST(Encounters, LegalActionTargetGridOverDeadAndAliveSlots) {
    CombatState s{};
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    s.player_energy = 3;

    // Hand: slot 0 = Strike (enemy-target Attack), slot 1 = Defend (self Skill).
    s.card_pool[0].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.card_pool[0].cost_now = 1;
    s.card_pool[1].card_id = static_cast<uint16_t>(CardId::DEFEND);
    s.card_pool[1].cost_now = 1;
    s.hand[0] = 0;
    s.hand[1] = 1;
    s.hand_count = 2;

    // Three monster slots: 0 alive, 1 DEAD (hp 0), 2 alive.
    s.monster_count = 3;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = 40;
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[1].hp = 0;  // dead-in-place
    s.monsters[2].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[2].hp = 12;

    ASSERT_NE(card_def(CardId::STRIKE), nullptr);
    ASSERT_TRUE(card_def(CardId::STRIKE)->needs_target);
    ASSERT_FALSE(card_def(CardId::DEFEND)->needs_target);

    ActionMask m{};
    legal_actions(s, m);

    EXPECT_TRUE(m.can_play[0]);   // Strike affordable
    EXPECT_TRUE(m.can_play[1]);   // Defend affordable
    EXPECT_TRUE(m.can_end_turn);

    // Strike: legal against alive slots 0 and 2, NOT the dead slot 1, NOT the
    // empty slots 3..6.
    EXPECT_TRUE(m.can_play_target[0][0]);
    EXPECT_FALSE(m.can_play_target[0][1]);  // dead
    EXPECT_TRUE(m.can_play_target[0][2]);
    for (int t = 3; t < kMonsterCap; ++t) {
        EXPECT_FALSE(m.can_play_target[0][t]) << "empty slot " << t;
    }
    // Defend (self): no legal enemy targets at all.
    for (int t = 0; t < kMonsterCap; ++t) {
        EXPECT_FALSE(m.can_play_target[1][t]) << "self card, slot " << t;
    }
}

TEST(Encounters, TargetGridEmptyWhenUnaffordable) {
    CombatState s{};
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    s.player_energy = 0;  // cannot afford
    s.card_pool[0].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.card_pool[0].cost_now = 1;
    s.hand[0] = 0;
    s.hand_count = 1;
    s.monster_count = 1;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = 40;

    ActionMask m{};
    legal_actions(s, m);
    EXPECT_FALSE(m.can_play[0]);
    for (int t = 0; t < kMonsterCap; ++t) EXPECT_FALSE(m.can_play_target[0][t]);
}

// --- 4. Pool draw (monsterRng) list generation ------------------------------

TEST(Encounters, MonsterListsAreDeterministicAndWellFormed) {
    for (int64_t seed = 1; seed < 30; ++seed) {
        RngStream a = from_seed(seed);
        RngStream b = from_seed(seed);
        MonsterLists la{}, lb{};
        generate_monster_lists(1, a, la);
        generate_monster_lists(1, b, lb);

        // Determinism: identical seeds -> identical lists AND identical draw counts.
        ASSERT_EQ(la.monster_list_count, lb.monster_list_count);
        ASSERT_EQ(a.counter, b.counter);
        for (std::size_t i = 0; i < la.monster_list_count; ++i)
            EXPECT_EQ(la.monster_list[i], lb.monster_list[i]);

        // Shape: 3 weak + 1 first-strong + 12 strong = 16; 10 elites; 3 bosses.
        EXPECT_EQ(la.monster_list_count, 16u);
        EXPECT_EQ(la.elite_list_count, 10u);
        EXPECT_EQ(la.boss_list_count, 3u);

        // No immediate repeat + no A-B-A across the whole (weak+strong) list.
        for (std::size_t i = 1; i < la.monster_list_count; ++i) {
            EXPECT_NE(la.monster_list[i], la.monster_list[i - 1])
                << "immediate repeat at " << i << " seed=" << seed;
            if (i >= 2) {
                EXPECT_NE(la.monster_list[i], la.monster_list[i - 2])
                    << "A-B-A at " << i << " seed=" << seed;
            }
        }
        // Elites: no immediate repeat (A-B-A is allowed for elites).
        for (std::size_t i = 1; i < la.elite_list_count; ++i) {
            EXPECT_NE(la.elite_list[i], la.elite_list[i - 1])
                << "elite immediate repeat at " << i;
        }

        // Membership: weak keys in the first 3, strong keys after; every entry is
        // a real encounter of the expected pool.
        for (std::size_t i = 0; i < la.monster_list_count; ++i) {
            const auto* e = sts::registry::encounter_by_game_id(la.monster_list[i]);
            ASSERT_NE(e, nullptr) << la.monster_list[i];
            const auto expect_pool = (i < 3) ? sts::registry::EncounterPool::WEAK
                                             : sts::registry::EncounterPool::STRONG;
            EXPECT_EQ(e->pool, expect_pool) << "pos " << i;
        }
        for (std::size_t i = 0; i < la.elite_list_count; ++i) {
            const auto* e = sts::registry::encounter_by_game_id(la.elite_list[i]);
            ASSERT_NE(e, nullptr);
            EXPECT_EQ(e->pool, sts::registry::EncounterPool::ELITE);
        }

        // Exclusions honored: the first strong (index 3) is not in the 3rd weak's
        // exclusion set.
        const auto* weak3 = sts::registry::encounter_by_game_id(la.monster_list[2]);
        ASSERT_NE(weak3, nullptr);
        for (std::size_t x = 0; x < weak3->exclude_count; ++x) {
            EXPECT_NE(la.monster_list[3], weak3->excludes[x])
                << "first strong violates exclusion, seed=" << seed;
        }

        // Boss list is a permutation of the three Act-1 boss ENCOUNTER keys
        // (Exordium pool strings; the composition later maps them to monster ids).
        std::vector<sv> bosses(la.boss_list.begin(),
                               la.boss_list.begin() + la.boss_list_count);
        std::sort(bosses.begin(), bosses.end());
        EXPECT_EQ(bosses,
                  (std::vector<sv>{"Hexaghost", "Slime Boss", "The Guardian"}));
    }
}

// A seed that lands on "2 Louse" as the 3rd weak: the first strong must never be
// "3 Louse" (the exclusion), across a wide seed sweep.
TEST(Encounters, FirstStrongNeverViolatesLouseExclusion) {
    int seen_two_louse_third = 0;
    for (int64_t seed = 0; seed < 500; ++seed) {
        RngStream r = from_seed(seed);
        MonsterLists l{};
        generate_monster_lists(1, r, l);
        if (l.monster_list_count >= 4 && l.monster_list[2] == sv("2 Louse")) {
            ++seen_two_louse_third;
            EXPECT_NE(l.monster_list[3], sv("3 Louse")) << "seed=" << seed;
        }
    }
    EXPECT_GT(seen_two_louse_third, 0)
        << "expected some seeds to land 2 Louse as the 3rd weak monster";
}
